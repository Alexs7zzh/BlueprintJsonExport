#include "Core/BPJEExportService.h"
#include "BPJESerializer.h"
#include "LogBlueprintJsonExport.h"
#include "Models/BPJEData.h"
#include "Utils/BPJENodeAnalysis.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/UserDefinedStruct.h"
#include "Engine/MemberReference.h"
#include "HAL/FileManager.h"
#include "K2Node.h"
#include "K2Node_AsyncAction.h"
#include "K2Node_BaseAsyncTask.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Composite.h"
#include "K2Node_CreateDelegate.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_Knot.h"
#include "K2Node_Timeline.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UnrealType.h"

namespace
{
	constexpr uint8 BPJE_NodeFlag_Pure = 1 << 0;
	constexpr uint8 BPJE_NodeFlag_Latent = 1 << 1;

	constexpr uint8 BPJE_PinFlag_Connected = 1 << 0;
	constexpr uint8 BPJE_PinFlag_Reference = 1 << 1;
	constexpr uint8 BPJE_PinFlag_Const = 1 << 2;

	constexpr uint8 BPJE_LinkKind_Exec = 0;
	constexpr uint8 BPJE_LinkKind_Data = 1;

	bool IsBlueprintDefinedPath(const FString& ObjectPath)
	{
		return FPackageName::IsValidObjectPath(ObjectPath) && !ObjectPath.StartsWith(TEXT("/Script/"));
	}

	FString CleanClassName(FString Name)
	{
		if (Name.StartsWith(TEXT("SKEL_")))
		{
			Name.RightChopInline(5);
		}
		if (Name.EndsWith(TEXT("_C")))
		{
			Name.LeftChopInline(2);
		}
		return Name;
	}

	void AddUniqueNonEmpty(TArray<FString>& Values, TSet<FString>& SeenValues, const FString& Value)
	{
		if (!Value.IsEmpty() && !SeenValues.Contains(Value))
		{
			SeenValues.Add(Value);
			Values.Add(Value);
		}
	}

	struct FBPJELinkKey
	{
		uint8 Kind = 0;
		int32 SourceNodeIndex = INDEX_NONE;
		int32 SourcePinIndex = INDEX_NONE;
		int32 TargetNodeIndex = INDEX_NONE;
		int32 TargetPinIndex = INDEX_NONE;

		bool operator==(const FBPJELinkKey& Other) const
		{
			return Kind == Other.Kind &&
			SourceNodeIndex == Other.SourceNodeIndex &&
			SourcePinIndex == Other.SourcePinIndex &&
			TargetNodeIndex == Other.TargetNodeIndex &&
			TargetPinIndex == Other.TargetPinIndex;
		}
	};

	uint32 GetTypeHash(const FBPJELinkKey& Key)
	{
		uint32 Hash = ::GetTypeHash(Key.Kind);
		Hash = HashCombine(Hash, ::GetTypeHash(Key.SourceNodeIndex));
		Hash = HashCombine(Hash, ::GetTypeHash(Key.SourcePinIndex));
		Hash = HashCombine(Hash, ::GetTypeHash(Key.TargetNodeIndex));
		return HashCombine(Hash, ::GetTypeHash(Key.TargetPinIndex));
	}

	const UBlueprint* GetOwningBlueprint(const UEdGraph* Graph)
	{
		for (const UObject* Outer = Graph; Outer; Outer = Outer->GetOuter())
		{
			if (const UBlueprint* Blueprint = Cast<UBlueprint>(Outer))
			{
				return Blueprint;
			}
		}

		return nullptr;
	}

	bool ShouldTraverseExternalChildGraph(EBPJENodeType NodeType)
	{
		switch (NodeType)
		{
		case EBPJENodeType::ForEachLoop:
		case EBPJENodeType::WhileLoop:
		case EBPJENodeType::ForLoop:
		case EBPJENodeType::Gate:
		case EBPJENodeType::DoOnce:
		case EBPJENodeType::MacroInstance:
			return true;

		default:
			return false;
		}
	}

	FString BuildOutputFilePath(const FString& OutputDir, const UBlueprint* Blueprint)
	{
		const FString PackageName = Blueprint ? Blueprint->GetOutermost()->GetName() : TEXT("");
		FString RelativePath = PackageName;
		RelativePath.RemoveFromStart(TEXT("/"));

		TArray<FString> PathParts;
		RelativePath.ParseIntoArray(PathParts, TEXT("/"), true);
		for (FString& PathPart : PathParts)
		{
			PathPart = FPaths::MakeValidFileName(PathPart);
		}

		FString RelativeOutputPath = FString::Join(PathParts, TEXT("/"));
		if (RelativeOutputPath.IsEmpty())
		{
			RelativeOutputPath = TEXT("BlueprintExport");
		}

		return OutputDir / (RelativeOutputPath + TEXT(".json"));
	}

	FString GetObjectNameFromPath(const FString& ObjectPath)
	{
		if (ObjectPath.IsEmpty())
		{
			return TEXT("");
		}

		const FString ObjectName = FPackageName::ObjectPathToObjectName(ObjectPath);
		return CleanClassName(ObjectName.IsEmpty() ? ObjectPath : ObjectName);
	}

	FString GraphTypeToSemanticKind(EBPJEGraphType GraphType)
	{
		switch (GraphType)
		{
		case EBPJEGraphType::EventGraph:
			return TEXT("event_graph");
		case EBPJEGraphType::Function:
			return TEXT("function");
		case EBPJEGraphType::Macro:
			return TEXT("macro");
		case EBPJEGraphType::Construction:
			return TEXT("construction");
		case EBPJEGraphType::Animation:
			return TEXT("animation");
		case EBPJEGraphType::Composite:
			return TEXT("composite");
		default:
			return TEXT("graph");
		}
	}

	bool LooksLikeAssetReference(const FString& Value)
	{
		return Value.StartsWith(TEXT("/")) && Value.Contains(TEXT("."));
	}

	bool IsClassReferencePin(const FBPJECompactPin& Pin)
	{
		switch (Pin.PinType)
		{
		case EBPJEPinType::Object:
		case EBPJEPinType::Class:
		case EBPJEPinType::Interface:
		case EBPJEPinType::SoftObject:
		case EBPJEPinType::SoftClass:
		case EBPJEPinType::AssetId:
		case EBPJEPinType::Material:
		case EBPJEPinType::Texture:
		case EBPJEPinType::StaticMesh:
		case EBPJEPinType::SkeletalMesh:
		case EBPJEPinType::Animation:
		case EBPJEPinType::BlendSpace:
			return true;
		default:
			return false;
		}
	}

	bool ShouldIncludeClassReference(const FString& ClassName)
	{
		return !ClassName.IsEmpty() && !ClassName.Equals(TEXT("self"), ESearchCase::IgnoreCase);
	}

	bool IsGameplayTagPin(const FBPJECompactPin& Pin)
	{
		if (Pin.PinType != EBPJEPinType::Struct)
		{
			return false;
		}

		const FString SubTypeName = GetObjectNameFromPath(Pin.SubType);
		return SubTypeName == TEXT("GameplayTag") || SubTypeName == TEXT("GameplayTagContainer");
	}

	bool TryExtractGameplayTag(const FBPJECompactPin& Pin, FString& OutTag)
	{
		if (!IsGameplayTagPin(Pin))
		{
			return false;
		}

		const FString Trimmed = Pin.DefaultValue.TrimStartAndEnd();
		if (Trimmed.IsEmpty() || Trimmed.Contains(TEXT("/")) || Trimmed.Contains(TEXT(" ")) || !Trimmed.Contains(TEXT(".")))
		{
			return false;
		}

		bool bHasAlpha = false;
		for (const TCHAR Character : Trimmed)
		{
			if (!FChar::IsAlnum(Character) && Character != TEXT('.') && Character != TEXT('_'))
			{
				return false;
			}

			if (FChar::IsAlpha(Character))
			{
				bHasAlpha = true;
			}
		}

		if (!bHasAlpha || !FChar::IsAlpha(Trimmed[0]))
		{
			return false;
		}

		OutTag = Trimmed;
		return true;
	}

	FString ToNormalizedFolderPath(FString FolderPath)
	{
		FolderPath.TrimStartAndEndInline();
		FolderPath.ReplaceInline(TEXT("\""), TEXT(""));

		if (FolderPath.IsEmpty())
		{
			return FolderPath;
		}

		if (!FolderPath.StartsWith(TEXT("/")))
		{
			FolderPath = FString::Printf(TEXT("/%s"), *FolderPath);
		}

		FolderPath.RemoveFromEnd(TEXT("/"));
		return FolderPath;
	}

	FString ToNormalizedBlueprintObjectPath(FString BlueprintPath)
	{
		BlueprintPath.TrimStartAndEndInline();
		BlueprintPath.ReplaceInline(TEXT("\""), TEXT(""));

		if (BlueprintPath.StartsWith(TEXT("Blueprint'")))
		{
			BlueprintPath.RightChopInline(10);
		}

		if (BlueprintPath.EndsWith(TEXT("'")))
		{
			BlueprintPath.LeftChopInline(1);
		}

		if (!BlueprintPath.StartsWith(TEXT("/")) && BlueprintPath.StartsWith(TEXT("Game/")))
		{
			BlueprintPath = FString::Printf(TEXT("/%s"), *BlueprintPath);
		}

		int32 DotIndex = INDEX_NONE;
		if (!BlueprintPath.FindChar(TEXT('.'), DotIndex))
		{
			const FString AssetName = FPackageName::GetLongPackageAssetName(BlueprintPath);
			if (!AssetName.IsEmpty())
			{
				BlueprintPath = FString::Printf(TEXT("%s.%s"), *BlueprintPath, *AssetName);
			}
		}
		else
		{
			FString ObjectName = BlueprintPath.Mid(DotIndex + 1);
			if (ObjectName.EndsWith(TEXT("_C")))
			{
				ObjectName.LeftChopInline(2);
				BlueprintPath = BlueprintPath.Left(DotIndex + 1) + ObjectName;
			}
		}

		return BlueprintPath;
	}

	EBPJEBlueprintType DetermineBlueprintType(const UBlueprint* Blueprint)
	{
		if (!Blueprint)
		{
			return EBPJEBlueprintType::Normal;
		}

		switch (Blueprint->BlueprintType)
		{
		case BPTYPE_Const:
			return EBPJEBlueprintType::Const;
		case BPTYPE_MacroLibrary:
			return EBPJEBlueprintType::MacroLibrary;
		case BPTYPE_Interface:
			return EBPJEBlueprintType::Interface;
		case BPTYPE_LevelScript:
			return EBPJEBlueprintType::LevelScript;
		case BPTYPE_FunctionLibrary:
			return EBPJEBlueprintType::FunctionLibrary;
		default:
			return EBPJEBlueprintType::Normal;
		}
	}

	EBPJEGraphType DetermineGraphType(const UBlueprint* Blueprint, const UEdGraph* Graph)
	{
		if (!Graph)
		{
			return EBPJEGraphType::EventGraph;
		}

		if (const UEdGraphNode* OuterNode = Cast<UEdGraphNode>(Graph->GetOuter()))
		{
			if (OuterNode->IsA<UK2Node_Composite>())
			{
				return EBPJEGraphType::Composite;
			}
		}

		if (Blueprint)
		{
			if (Blueprint->MacroGraphs.Contains(const_cast<UEdGraph*>(Graph)))
			{
				return EBPJEGraphType::Macro;
			}

			if (Blueprint->FunctionGraphs.Contains(const_cast<UEdGraph*>(Graph)))
			{
				if (Graph->GetName().Contains(TEXT("ConstructionScript")))
				{
					return EBPJEGraphType::Construction;
				}
				return EBPJEGraphType::Function;
			}

			if (Blueprint->UbergraphPages.Contains(const_cast<UEdGraph*>(Graph)))
			{
				return EBPJEGraphType::EventGraph;
			}
		}

		for (const UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && Node->IsA<UK2Node_FunctionEntry>())
			{
				return EBPJEGraphType::Function;
			}
		}

		const FString LowerName = Graph->GetName().ToLower();
		if (LowerName.Contains(TEXT("construction")))
		{
			return EBPJEGraphType::Construction;
		}
		if (LowerName.Contains(TEXT("macro")))
		{
			return EBPJEGraphType::Macro;
		}
		if (LowerName.Contains(TEXT("anim")))
		{
			return EBPJEGraphType::Animation;
		}

		return EBPJEGraphType::EventGraph;
	}

	EBPJEPinType DeterminePinType(const UEdGraphPin* Pin)
	{
		if (!Pin)
		{
			return EBPJEPinType::Wildcard;
		}

		const FName PinCategory = Pin->PinType.PinCategory;

		if (PinCategory == UEdGraphSchema_K2::PC_Exec)
		{
			return EBPJEPinType::Exec;
		}
		if (Pin->PinType.PinSubCategory == UEdGraphSchema_K2::PSC_Bitmask)
		{
			return EBPJEPinType::Bitmask;
		}
		if (Pin->PinType.PinSubCategory == UEdGraphSchema_K2::PSC_Self)
		{
			return EBPJEPinType::Self;
		}
		if (Pin->PinType.PinSubCategory == UEdGraphSchema_K2::PSC_Index)
		{
			return EBPJEPinType::Index;
		}
		if (PinCategory == UEdGraphSchema_K2::PC_Boolean)
		{
			return EBPJEPinType::Boolean;
		}
		if (PinCategory == UEdGraphSchema_K2::PC_Byte)
		{
			return EBPJEPinType::Byte;
		}
		if (PinCategory == UEdGraphSchema_K2::PC_Int)
		{
			return EBPJEPinType::Integer;
		}
		if (PinCategory == UEdGraphSchema_K2::PC_Int64)
		{
			return EBPJEPinType::Integer64;
		}
		if (PinCategory == UEdGraphSchema_K2::PC_Float)
		{
			return EBPJEPinType::Float;
		}
		if (PinCategory == UEdGraphSchema_K2::PC_Double)
		{
			return EBPJEPinType::Double;
		}
		if (PinCategory == UEdGraphSchema_K2::PC_Real)
		{
			return EBPJEPinType::Real;
		}
		if (PinCategory == UEdGraphSchema_K2::PC_String)
		{
			return EBPJEPinType::String;
		}
		if (PinCategory == UEdGraphSchema_K2::PC_Name)
		{
			return EBPJEPinType::Name;
		}
		if (PinCategory == UEdGraphSchema_K2::PC_Text)
		{
			return EBPJEPinType::Text;
		}
		if (PinCategory == UEdGraphSchema_K2::PC_Object)
		{
			return EBPJEPinType::Object;
		}
		if (PinCategory == UEdGraphSchema_K2::PC_Class)
		{
			return EBPJEPinType::Class;
		}
		if (PinCategory == UEdGraphSchema_K2::PC_Interface)
		{
			return EBPJEPinType::Interface;
		}
		if (PinCategory == UEdGraphSchema_K2::PC_Struct)
		{
			return EBPJEPinType::Struct;
		}
		if (PinCategory == UEdGraphSchema_K2::PC_Enum)
		{
			return EBPJEPinType::Enum;
		}
		if (PinCategory == UEdGraphSchema_K2::PC_Delegate)
		{
			return EBPJEPinType::Delegate;
		}
		if (PinCategory == UEdGraphSchema_K2::PC_MCDelegate)
		{
			return EBPJEPinType::MulticastDelegate;
		}
		if (PinCategory == UEdGraphSchema_K2::PC_FieldPath)
		{
			return EBPJEPinType::FieldPath;
		}
		if (PinCategory == UEdGraphSchema_K2::PC_SoftObject)
		{
			return EBPJEPinType::SoftObject;
		}
		if (PinCategory == UEdGraphSchema_K2::PC_SoftClass)
		{
			return EBPJEPinType::SoftClass;
		}

		return EBPJEPinType::Wildcard;
	}

	FString GetPinDefaultValue(const UEdGraphPin* Pin)
	{
		if (!Pin)
		{
			return TEXT("");
		}

		if (!Pin->DefaultValue.IsEmpty())
		{
			return Pin->DefaultValue;
		}

		if (Pin->DefaultObject)
		{
			return Pin->DefaultObject->GetPathName();
		}

		if (!Pin->DefaultTextValue.IsEmpty())
		{
			return Pin->DefaultTextValue.ToString();
		}

		return TEXT("");
	}

	uint8 BuildPinFlags(const UEdGraphPin* Pin)
	{
		if (!Pin)
		{
			return 0;
		}

		uint8 Flags = 0;
		if (Pin->LinkedTo.Num() > 0)
		{
			Flags |= BPJE_PinFlag_Connected;
		}
		if (Pin->PinType.bIsReference)
		{
			Flags |= BPJE_PinFlag_Reference;
		}
		if (Pin->PinType.bIsConst)
		{
			Flags |= BPJE_PinFlag_Const;
		}
		return Flags;
	}

	EBPJEPinContainerType DeterminePinContainerType(const UEdGraphPin* Pin)
	{
		if (!Pin)
		{
			return EBPJEPinContainerType::None;
		}

		switch (Pin->PinType.ContainerType)
		{
		case EPinContainerType::Array:
			return EBPJEPinContainerType::Array;
		case EPinContainerType::Set:
			return EBPJEPinContainerType::Set;
		case EPinContainerType::Map:
			return EBPJEPinContainerType::Map;
		default:
			return EBPJEPinContainerType::None;
		}
	}

	uint8 BuildNodeFlags(const UK2Node* Node)
	{
		if (!Node)
		{
			return 0;
		}

		uint8 Flags = 0;
		if (Node->IsNodePure())
		{
			Flags |= BPJE_NodeFlag_Pure;
		}

		if (Node->IsA<UK2Node_AsyncAction>() ||
			Node->IsA<UK2Node_BaseAsyncTask>() ||
			Node->IsA<UK2Node_Timeline>())
		{
			Flags |= BPJE_NodeFlag_Latent;
		}

		if (const UK2Node_CallFunction* CallFunction = Cast<UK2Node_CallFunction>(Node))
		{
			if (CallFunction->IsLatentFunction())
			{
				Flags |= BPJE_NodeFlag_Latent;
			}
		}

		return Flags;
	}

	bool IsHiddenEnumValue(const FString& Name)
	{
		return Name.EndsWith(TEXT("_MAX")) || Name.StartsWith(TEXT("MAX_")) || Name.Equals(TEXT("MAX"));
	}

	void TraceConnectionsThroughKnots(UEdGraphPin* StartPin, TArray<UEdGraphPin*>& OutPins, TSet<const UEdGraphPin*>& VisitedPins)
	{
		if (!StartPin || VisitedPins.Contains(StartPin))
		{
			return;
		}

		VisitedPins.Add(StartPin);

		UEdGraphNode* Node = StartPin->GetOwningNode();
		if (!Node)
		{
			return;
		}

		if (!Node->IsA<UK2Node_Knot>())
		{
			OutPins.Add(StartPin);
			return;
		}

		for (UEdGraphPin* Candidate : Node->Pins)
		{
			if (!Candidate || Candidate == StartPin || Candidate->Direction == StartPin->Direction)
			{
				continue;
			}

			for (UEdGraphPin* LinkedPin : Candidate->LinkedTo)
			{
				TraceConnectionsThroughKnots(LinkedPin, OutPins, VisitedPins);
			}
		}
	}

	EBPJEStructMemberType ConvertPropertyToStructMemberType(FProperty* Property)
	{
		if (!Property)
		{
			return EBPJEStructMemberType::Custom;
		}

		if (CastField<FBoolProperty>(Property))
		{
			return EBPJEStructMemberType::Bool;
		}
		if (CastField<FByteProperty>(Property))
		{
			return EBPJEStructMemberType::Byte;
		}
		if (CastField<FIntProperty>(Property) || CastField<FInt64Property>(Property))
		{
			return EBPJEStructMemberType::Int;
		}
		if (CastField<FFloatProperty>(Property) || CastField<FDoubleProperty>(Property))
		{
			return EBPJEStructMemberType::Float;
		}
		if (CastField<FStrProperty>(Property))
		{
			return EBPJEStructMemberType::String;
		}
		if (CastField<FNameProperty>(Property))
		{
			return EBPJEStructMemberType::Name;
		}
		if (CastField<FTextProperty>(Property))
		{
			return EBPJEStructMemberType::Text;
		}

		if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			if (StructProperty->Struct)
			{
				if (StructProperty->Struct->GetFName() == NAME_Vector)
				{
					return EBPJEStructMemberType::Vector;
				}
				if (StructProperty->Struct->GetFName() == NAME_Vector2D)
				{
					return EBPJEStructMemberType::Vector2D;
				}
				if (StructProperty->Struct->GetFName() == NAME_Rotator)
				{
					return EBPJEStructMemberType::Rotator;
				}
				if (StructProperty->Struct->GetFName() == NAME_Transform)
				{
					return EBPJEStructMemberType::Transform;
				}
			}
			return EBPJEStructMemberType::Struct;
		}

		if (CastField<FEnumProperty>(Property))
		{
			return EBPJEStructMemberType::Enum;
		}
		if (CastField<FClassProperty>(Property))
		{
			return EBPJEStructMemberType::Class;
		}
		if (CastField<FObjectProperty>(Property))
		{
			return EBPJEStructMemberType::Object;
		}

		return EBPJEStructMemberType::Custom;
	}

	FString ExportStructMemberDefaultValue(UScriptStruct* OwnerStruct, FProperty* Property)
	{
		const UUserDefinedStruct* UserDefinedStruct = Cast<UUserDefinedStruct>(OwnerStruct);
		if (!UserDefinedStruct || !Property)
		{
			return TEXT("");
		}

		const uint8* DefaultInstance = UserDefinedStruct->GetDefaultInstance();
		if (!DefaultInstance)
		{
			return TEXT("");
		}

		const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(DefaultInstance);
		if (!ValuePtr)
		{
			return TEXT("");
		}

		FString DefaultValue;
		Property->ExportTextItem_Direct(DefaultValue, ValuePtr, ValuePtr, nullptr, PPF_SerializedAsImportText);
		return DefaultValue;
	}

	class FBlueprintExtractor
	{
	public:
		FBlueprintExtractor(UBlueprint* InBlueprint, int32 InMaxDepth, TArray<FString>& InWarnings)
			: Blueprint(InBlueprint)
			, MaxDepth(InMaxDepth)
			, Warnings(InWarnings) {}

		FBPJECompactBlueprint Extract()
		{
			FBPJECompactBlueprint Output;
			Output.Metadata.BlueprintName = Blueprint->GetName();
			Output.Metadata.PackagePath = Blueprint->GetOutermost()->GetName();
			Output.Metadata.ObjectPath = Blueprint->GetPathName();
			Output.Metadata.GeneratedClass = Blueprint->GeneratedClass ? Blueprint->GeneratedClass->GetPathName() : TEXT("");
			Output.Metadata.BlueprintType = DetermineBlueprintType(Blueprint);
			Output.Metadata.MaxDepth = MaxDepth;

			Data = &Output;

			TSet<const UEdGraph*> SeenRoots;
			auto QueueRootGraph = [&](UEdGraph* Graph)
			{
				if (Graph && !SeenRoots.Contains(Graph))
				{
					SeenRoots.Add(Graph);
					ProcessGraph(Graph, 0, Graph->GetName());
				}
			};

			for (UEdGraph* Graph : Blueprint->UbergraphPages)
			{
				QueueRootGraph(Graph);
			}
			for (UEdGraph* Graph : Blueprint->FunctionGraphs)
			{
				QueueRootGraph(Graph);
			}
			for (UEdGraph* Graph : Blueprint->MacroGraphs)
			{
				QueueRootGraph(Graph);
			}

			Output.Metadata.bTruncated = !Output.SkippedGraphs.IsEmpty();
			return Output;
		}

	private:
		struct FGraphBuildContext
		{
			TMap<const UEdGraphNode*, int32> NodeIndexByNode;
			TMap<const UEdGraphPin*, int32> PinIndexByPin;
		};

		void AddWarning(const FString& Warning)
		{
			Warnings.Add(Warning);
		}

		void RecordSkippedGraph(
			const FString& OwnerGraphName,
			const FString& GraphName,
			const FString& SkippedOwnerPath,
			const FString& Reason,
			const FString& Warning)
		{
			const FString PairKey = FString::Printf(TEXT("%s|%s|%s|%s"), *OwnerGraphName, *GraphName, *SkippedOwnerPath, *Reason);
			if (!SkippedPairKeys.Contains(PairKey))
			{
				SkippedPairKeys.Add(PairKey);
				FBPJESkippedGraphEntry& Entry = Data->SkippedGraphs.AddDefaulted_GetRef();
				Entry.OwnerGraphName = OwnerGraphName;
				Entry.SkippedGraphName = GraphName;
				Entry.SkippedOwnerPath = SkippedOwnerPath;
				Entry.Reason = Reason;
				AddWarning(Warning);
			}
		}

		FString DetermineSemanticNodeKind(const FBPJECompactNode& Node) const
		{
			switch (Node.NodeType)
			{
			case EBPJENodeType::Event:
			case EBPJENodeType::CustomEvent:
			case EBPJENodeType::ActorBoundEvent:
			case EBPJENodeType::ComponentBoundEvent:
			case EBPJENodeType::GeneratedBoundEvent:
			case EBPJENodeType::InputAction:
			case EBPJENodeType::InputActionEvent:
			case EBPJENodeType::InputAxisEvent:
			case EBPJENodeType::InputAxisKeyEvent:
			case EBPJENodeType::InputKey:
			case EBPJENodeType::InputKeyEvent:
			case EBPJENodeType::InputTouch:
			case EBPJENodeType::InputTouchEvent:
			case EBPJENodeType::InputVectorAxisEvent:
				return TEXT("event");

			case EBPJENodeType::FunctionEntry:
				return TEXT("entry");

			case EBPJENodeType::FunctionResult:
			case EBPJENodeType::FunctionTerminator:
				return TEXT("exit");

			case EBPJENodeType::VariableGet:
			case EBPJENodeType::LocalVariableGet:
			case EBPJENodeType::StructMemberGet:
				return TEXT("variable_get");

			case EBPJENodeType::VariableSet:
			case EBPJENodeType::VariableSetRef:
			case EBPJENodeType::LocalVariableSet:
			case EBPJENodeType::StructMemberSet:
			case EBPJENodeType::SetFieldsInStruct:
			case EBPJENodeType::SetVariableOnPersistentFrame:
				return TEXT("variable_set");

			case EBPJENodeType::Branch:
			case EBPJENodeType::Sequence:
			case EBPJENodeType::Switch:
			case EBPJENodeType::SwitchInt:
			case EBPJENodeType::SwitchString:
			case EBPJENodeType::SwitchEnum:
			case EBPJENodeType::SwitchName:
			case EBPJENodeType::ForLoop:
			case EBPJENodeType::ForEachLoop:
			case EBPJENodeType::ForEachElementInEnum:
			case EBPJENodeType::WhileLoop:
			case EBPJENodeType::Gate:
			case EBPJENodeType::MultiGate:
			case EBPJENodeType::DoOnce:
			case EBPJENodeType::DoOnceMultiInput:
			case EBPJENodeType::Select:
				return TEXT("flow");

			case EBPJENodeType::CreateDelegate:
			case EBPJENodeType::AddDelegate:
			case EBPJENodeType::AssignDelegate:
			case EBPJENodeType::ClearDelegate:
			case EBPJENodeType::RemoveDelegate:
			case EBPJENodeType::CallDelegate:
			case EBPJENodeType::DelegateSet:
			case EBPJENodeType::BaseMCDelegate:
				return TEXT("delegate");

			default:
				break;
			}

			if ((Node.NodeFlags & BPJE_NodeFlag_Latent) != 0)
			{
				return TEXT("latent_task");
			}

			if (!Node.MemberName.IsEmpty())
			{
				return Node.bMemberIsBlueprintDefined ? TEXT("call_blueprint") : TEXT("call_native");
			}

			return TEXT("node");
		}

		void BuildSemanticGraph(const FBPJECompactGraph& Graph, int32 GraphIndex)
		{
			FBPJESemanticGraph SemanticGraph;
			SemanticGraph.GraphIndex = GraphIndex;
			SemanticGraph.Name = Graph.Name;
			SemanticGraph.Kind = GraphTypeToSemanticKind(Graph.GraphType);
			TSet<FString> SeenCalls;
			TSet<FString> SeenNativeCalls;
			TSet<FString> SeenLatentTasks;
			TSet<FString> SeenClassRefs;
			TSet<FString> SeenAssetRefs;
			TSet<FString> SeenTagRefs;

			TSet<int32> NodesWithIncomingExec;
			TSet<int32> NodesWithOutgoingExec;
			for (const FBPJECompactLink& Link : Graph.Links)
			{
				if (Link.Kind != BPJE_LinkKind_Exec)
				{
					continue;
				}

				NodesWithIncomingExec.Add(Link.TargetNodeIndex);
				NodesWithOutgoingExec.Add(Link.SourceNodeIndex);

				FBPJESemanticExecLink ExecLink;
				ExecLink.From = Link.SourceNodeIndex;
				ExecLink.To = Link.TargetNodeIndex;

				if (Graph.Nodes.IsValidIndex(Link.SourceNodeIndex) &&
					Graph.Nodes[Link.SourceNodeIndex].Pins.IsValidIndex(Link.SourcePinIndex))
				{
					ExecLink.FromPin = Graph.Nodes[Link.SourceNodeIndex].Pins[Link.SourcePinIndex].Name;
				}

				if (Graph.Nodes.IsValidIndex(Link.TargetNodeIndex) &&
					Graph.Nodes[Link.TargetNodeIndex].Pins.IsValidIndex(Link.TargetPinIndex))
				{
					ExecLink.ToPin = Graph.Nodes[Link.TargetNodeIndex].Pins[Link.TargetPinIndex].Name;
				}

				SemanticGraph.Exec.Add(MoveTemp(ExecLink));
			}

			for (int32 NodeIndex = 0; NodeIndex < Graph.Nodes.Num(); ++NodeIndex)
			{
				const FBPJECompactNode& Node = Graph.Nodes[NodeIndex];
				const FString NodeKind = DetermineSemanticNodeKind(Node);

				if (!NodesWithIncomingExec.Contains(NodeIndex) &&
					(NodeKind == TEXT("event") || NodeKind == TEXT("entry") || NodesWithOutgoingExec.Contains(NodeIndex)))
				{
					SemanticGraph.EntryNodes.Add(NodeIndex);
				}

				FBPJESemanticNode SemanticNode;
				SemanticNode.Id = NodeIndex;
				SemanticNode.Kind = NodeKind;
				SemanticNode.Title = Node.Title;
				SemanticNode.MemberParent = Node.MemberParent;
				SemanticNode.MemberName = Node.MemberName;
				SemanticGraph.Nodes.Add(MoveTemp(SemanticNode));

				if ((Node.NodeFlags & BPJE_NodeFlag_Latent) != 0)
				{
					AddUniqueNonEmpty(
						SemanticGraph.Summary.LatentTasks,
						SeenLatentTasks,
						Node.Title.IsEmpty() ? Node.MemberName : Node.Title);
				}

				if (!Node.MemberName.IsEmpty() &&
					!Node.bMemberIsBlueprintDefined &&
					ShouldIncludeClassReference(Node.MemberParent))
				{
					AddUniqueNonEmpty(SemanticGraph.Summary.Refs.Classes, SeenClassRefs, Node.MemberParent);
				}

				if (!Node.MemberName.IsEmpty())
				{
					if (Node.bMemberIsBlueprintDefined)
					{
						AddUniqueNonEmpty(SemanticGraph.Summary.Calls, SeenCalls, Node.MemberName);
					}
					else if (!Node.MemberParent.IsEmpty())
					{
						AddUniqueNonEmpty(
							SemanticGraph.Summary.NativeCalls,
							SeenNativeCalls,
							FString::Printf(TEXT("%s.%s"), *Node.MemberParent, *Node.MemberName));
					}
				}

				for (const FBPJECompactPin& Pin : Node.Pins)
				{
					if (LooksLikeAssetReference(Pin.DefaultValue))
					{
						AddUniqueNonEmpty(SemanticGraph.Summary.Refs.Assets, SeenAssetRefs, Pin.DefaultValue);
					}

					if (LooksLikeAssetReference(Pin.SubType))
					{
						AddUniqueNonEmpty(SemanticGraph.Summary.Refs.Assets, SeenAssetRefs, Pin.SubType);
					}

					if (IsClassReferencePin(Pin))
					{
						const FString SubTypeName = GetObjectNameFromPath(Pin.SubType);
						if (ShouldIncludeClassReference(SubTypeName))
						{
							AddUniqueNonEmpty(SemanticGraph.Summary.Refs.Classes, SeenClassRefs, SubTypeName);
						}
					}

					FString GameplayTag;
					if (TryExtractGameplayTag(Pin, GameplayTag))
					{
						AddUniqueNonEmpty(SemanticGraph.Summary.Refs.Tags, SeenTagRefs, GameplayTag);
					}
				}
			}

			SemanticGraph.EntryNodes.Sort();
			SemanticGraph.Exec.Sort([](const FBPJESemanticExecLink& A, const FBPJESemanticExecLink& B)
			{
				if (A.From != B.From)
				{
					return A.From < B.From;
				}
				if (A.To != B.To)
				{
					return A.To < B.To;
				}
				if (A.FromPin != B.FromPin)
				{
					return A.FromPin < B.FromPin;
				}
				return A.ToPin < B.ToPin;
			});

			Data->SemanticIndex.Graphs.Add(MoveTemp(SemanticGraph));
		}

		bool ShouldSkipForDepth(const FString& OwnerGraphName, UEdGraph* Graph, int32 Depth)
		{
			if (MaxDepth == -1 || Depth <= MaxDepth)
			{
				return false;
			}

			const FString GraphName = Graph ? Graph->GetName() : TEXT("<null>");
			RecordSkippedGraph(
				OwnerGraphName,
				GraphName,
				TEXT(""),
				TEXT("depth_limit"),
				FString::Printf(TEXT("Depth limit reached. Skipping '%s' referenced from '%s'"), *GraphName, *OwnerGraphName));
			return true;
		}

		void ProcessGraph(UEdGraph* Graph, int32 Depth, const FString& OwnerGraphName)
		{
			if (!Graph || ProcessedGraphs.Contains(Graph))
			{
				return;
			}

			if (ShouldSkipForDepth(OwnerGraphName, Graph, Depth))
			{
				return;
			}

			ProcessedGraphs.Add(Graph);

			FBPJECompactGraph OutGraph;
			OutGraph.Name = Graph->GetName();
			OutGraph.GraphType = DetermineGraphType(Blueprint, Graph);

			FGraphBuildContext BuildContext;
			TArray<UEdGraph*> ChildGraphs;

			for (UEdGraphNode* GraphNode : Graph->Nodes)
			{
				UK2Node* K2Node = Cast<UK2Node>(GraphNode);
				if (!K2Node || K2Node->IsA<UK2Node_Knot>())
				{
					continue;
				}

				FBPJECompactNode OutNode;
				FBPJENodeAnalysisResult NodeAnalysis = AnalyzeNode(K2Node);
				OutNode.NodeType = NodeAnalysis.NodeType;
				OutNode.Title = K2Node->GetNodeTitle(ENodeTitleType::MenuTitle).ToString();
				OutNode.bMemberIsBlueprintDefined = NodeAnalysis.bMemberIsBlueprintDefined;
				OutNode.NodeFlags = BuildNodeFlags(K2Node);
				OutNode.MemberParent = MoveTemp(NodeAnalysis.MemberParent);
				OutNode.MemberName = MoveTemp(NodeAnalysis.MemberName);

				const int32 NodeIndex = OutGraph.Nodes.Add(MoveTemp(OutNode));
				BuildContext.NodeIndexByNode.Add(K2Node, NodeIndex);

				FBPJECompactNode& AddedNode = OutGraph.Nodes[NodeIndex];
				for (UEdGraphPin* Pin : K2Node->Pins)
				{
					if (!Pin || Pin->bHidden)
					{
						continue;
					}

					FBPJECompactPin OutPin;
					OutPin.Direction = Pin->Direction == EGPD_Output ? 1 : 0;
					OutPin.PinType = DeterminePinType(Pin);
					OutPin.ContainerType = DeterminePinContainerType(Pin);
					OutPin.Name = Pin->GetDisplayName().ToString();
					OutPin.SubType = ResolvePinSubType(K2Node, Pin);
					OutPin.DefaultValue = GetPinDefaultValue(Pin);
					OutPin.PinFlags = BuildPinFlags(Pin);

					const int32 PinIndex = AddedNode.Pins.Add(MoveTemp(OutPin));
					BuildContext.PinIndexByPin.Add(Pin, PinIndex);

					ProcessPinTypes(Pin);
				}

				if (NodeAnalysis.StructType)
				{
					AddStruct(NodeAnalysis.StructType);
				}

				if (NodeAnalysis.EnumType)
				{
					AddEnum(NodeAnalysis.EnumType);
				}

				if (NodeAnalysis.ChildGraph && !ChildGraphs.Contains(NodeAnalysis.ChildGraph))
				{
					const UBlueprint* ChildGraphBlueprint = GetOwningBlueprint(NodeAnalysis.ChildGraph);
					const bool bIsExternalChildGraph = ChildGraphBlueprint && ChildGraphBlueprint != Blueprint;
					if (!bIsExternalChildGraph || ShouldTraverseExternalChildGraph(NodeAnalysis.NodeType))
					{
						ChildGraphs.Add(NodeAnalysis.ChildGraph);
					}
					else
					{
						RecordSkippedGraph(
							Graph->GetName(),
							NodeAnalysis.ChildGraph->GetName(),
							ChildGraphBlueprint->GetPathName(),
							TEXT("external_blueprint_graph"),
							FString::Printf(
								TEXT("Skipping external child graph '%s' owned by '%s' referenced from '%s'"),
								*NodeAnalysis.ChildGraph->GetName(),
								*ChildGraphBlueprint->GetPathName(),
								*Graph->GetName()));
					}
				}
			}

			BuildLinks(Graph, BuildContext, OutGraph);
			const int32 GraphIndex = Data->Graphs.Add(MoveTemp(OutGraph));
			BuildSemanticGraph(Data->Graphs[GraphIndex], GraphIndex);

			for (UEdGraph* ChildGraph : ChildGraphs)
			{
				ProcessGraph(ChildGraph, Depth + 1, Graph->GetName());
			}
		}

		void BuildLinks(UEdGraph* Graph, const FGraphBuildContext& BuildContext, FBPJECompactGraph& OutGraph)
		{
			TSet<FBPJELinkKey> UniqueLinks;

			for (UEdGraphNode* GraphNode : Graph->Nodes)
			{
				UK2Node* SourceNode = Cast<UK2Node>(GraphNode);
				if (!SourceNode || SourceNode->IsA<UK2Node_Knot>())
				{
					continue;
				}

				const int32* SourceNodeIndex = BuildContext.NodeIndexByNode.Find(SourceNode);
				if (!SourceNodeIndex)
				{
					continue;
				}

				for (UEdGraphPin* SourcePin : SourceNode->Pins)
				{
					if (!SourcePin || SourcePin->bHidden || SourcePin->Direction != EGPD_Output)
					{
						continue;
					}

					const int32* SourcePinIndex = BuildContext.PinIndexByPin.Find(SourcePin);
					if (!SourcePinIndex)
					{
						continue;
					}

					for (UEdGraphPin* LinkedPin : SourcePin->LinkedTo)
					{
						TArray<UEdGraphPin*> TargetPins;
						TSet<const UEdGraphPin*> VisitedPins;
						TraceConnectionsThroughKnots(LinkedPin, TargetPins, VisitedPins);

						for (UEdGraphPin* TargetPin : TargetPins)
						{
							if (!TargetPin)
							{
								continue;
							}

							UK2Node* TargetNode = Cast<UK2Node>(TargetPin->GetOwningNode());
							if (!TargetNode)
							{
								continue;
							}

							const int32* TargetNodeIndex = BuildContext.NodeIndexByNode.Find(TargetNode);
							const int32* TargetPinIndex = BuildContext.PinIndexByPin.Find(TargetPin);
							if (!TargetNodeIndex || !TargetPinIndex)
							{
								continue;
							}

							const bool bExecLink =
							SourcePin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec ||
							TargetPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec;

							FBPJELinkKey LinkKey;
							LinkKey.Kind = bExecLink ? BPJE_LinkKind_Exec : BPJE_LinkKind_Data;
							LinkKey.SourceNodeIndex = *SourceNodeIndex;
							LinkKey.SourcePinIndex = *SourcePinIndex;
							LinkKey.TargetNodeIndex = *TargetNodeIndex;
							LinkKey.TargetPinIndex = *TargetPinIndex;
							if (UniqueLinks.Contains(LinkKey))
							{
								continue;
							}
							UniqueLinks.Add(LinkKey);

							FBPJECompactLink Link;
							Link.Kind = LinkKey.Kind;
							Link.SourceNodeIndex = *SourceNodeIndex;
							Link.SourcePinIndex = *SourcePinIndex;
							Link.TargetNodeIndex = *TargetNodeIndex;
							Link.TargetPinIndex = *TargetPinIndex;
							OutGraph.Links.Add(Link);
						}
					}
				}
			}
		}

		FString ResolvePinSubType(UK2Node* OwnerNode, UEdGraphPin* Pin) const
		{
			if (!Pin)
			{
				return TEXT("");
			}

			if (UK2Node_CreateDelegate* CreateDelegateNode = Cast<UK2Node_CreateDelegate>(OwnerNode))
			{
				if (Pin->Direction == EGPD_Output && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Delegate)
				{
					return CleanClassName(CreateDelegateNode->GetFunctionName().ToString());
				}
			}

			if (Pin->PinType.PinSubCategoryObject.IsValid())
			{
				return CleanClassName(Pin->PinType.PinSubCategoryObject->GetName());
			}

			if (!Pin->PinType.PinSubCategory.IsNone())
			{
				return CleanClassName(Pin->PinType.PinSubCategory.ToString());
			}

			return TEXT("");
		}

		void ProcessPinTypes(UEdGraphPin* Pin)
		{
			if (!Pin)
			{
				return;
			}

			if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct)
			{
				if (UScriptStruct* Struct = Cast<UScriptStruct>(Pin->PinType.PinSubCategoryObject.Get()))
				{
					AddStruct(Struct);
				}
			}
			else if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Enum ||
				Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Byte)
			{
				if (UEnum* Enum = Cast<UEnum>(Pin->PinType.PinSubCategoryObject.Get()))
				{
					AddEnum(Enum);
				}
			}
		}

		void AddStruct(UScriptStruct* Struct)
		{
			if (!Struct)
			{
				return;
			}

			const FString StructPath = Struct->GetPathName();
			if (!IsBlueprintDefinedPath(StructPath) || ProcessedStructs.Contains(StructPath))
			{
				return;
			}

			ProcessedStructs.Add(StructPath);

			FBPJEStruct StructDef;
			StructDef.Name = Struct->GetName();
			StructDef.Comment = Struct->GetMetaData(TEXT("ToolTip"));

			for (TFieldIterator<FProperty> PropertyIt(Struct); PropertyIt; ++PropertyIt)
			{
				if (FProperty* Property = *PropertyIt)
				{
					StructDef.Members.Add(BuildStructMember(Struct, Property));
				}
			}

			Data->Structs.Add(MoveTemp(StructDef));
		}

		FBPJEStructMember BuildStructMember(UScriptStruct* OwnerStruct, FProperty* Property)
		{
			FBPJEStructMember Member;
			if (!Property)
			{
				return Member;
			}

			Member.Name = Property->GetName();
			Member.Comment = Property->GetMetaData(TEXT("ToolTip"));
			Member.DefaultValue = ExportStructMemberDefaultValue(OwnerStruct, Property);
			Member.Type = ConvertPropertyToStructMemberType(Property);

			if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
			{
				Member.bIsArray = true;
				if (ArrayProperty->Inner)
				{
					Member.Type = ConvertPropertyToStructMemberType(ArrayProperty->Inner);
					if (FStructProperty* InnerStruct = CastField<FStructProperty>(ArrayProperty->Inner))
					{
						Member.TypeName = InnerStruct->Struct ? InnerStruct->Struct->GetName() : TEXT("");
						AddStruct(InnerStruct->Struct);
					}
					else if (FEnumProperty* InnerEnum = CastField<FEnumProperty>(ArrayProperty->Inner))
					{
						Member.TypeName = InnerEnum->GetEnum() ? InnerEnum->GetEnum()->GetName() : TEXT("");
						AddEnum(InnerEnum->GetEnum());
					}
				}
				return Member;
			}

			if (FSetProperty* SetProperty = CastField<FSetProperty>(Property))
			{
				Member.bIsSet = true;
				if (SetProperty->ElementProp)
				{
					Member.Type = ConvertPropertyToStructMemberType(SetProperty->ElementProp);
					if (FStructProperty* ValueStruct = CastField<FStructProperty>(SetProperty->ElementProp))
					{
						Member.TypeName = ValueStruct->Struct ? ValueStruct->Struct->GetName() : TEXT("");
						AddStruct(ValueStruct->Struct);
					}
					else if (FEnumProperty* ValueEnum = CastField<FEnumProperty>(SetProperty->ElementProp))
					{
						Member.TypeName = ValueEnum->GetEnum() ? ValueEnum->GetEnum()->GetName() : TEXT("");
						AddEnum(ValueEnum->GetEnum());
					}
				}
				return Member;
			}

			if (FMapProperty* MapProperty = CastField<FMapProperty>(Property))
			{
				Member.bIsMap = true;
				Member.KeyType = ConvertPropertyToStructMemberType(MapProperty->KeyProp);
				if (FStructProperty* KeyStruct = CastField<FStructProperty>(MapProperty->KeyProp))
				{
					Member.KeyTypeName = KeyStruct->Struct ? KeyStruct->Struct->GetName() : TEXT("");
					AddStruct(KeyStruct->Struct);
				}
				else if (FEnumProperty* KeyEnum = CastField<FEnumProperty>(MapProperty->KeyProp))
				{
					Member.KeyTypeName = KeyEnum->GetEnum() ? KeyEnum->GetEnum()->GetName() : TEXT("");
					AddEnum(KeyEnum->GetEnum());
				}

				Member.Type = ConvertPropertyToStructMemberType(MapProperty->ValueProp);
				if (FStructProperty* ValueStruct = CastField<FStructProperty>(MapProperty->ValueProp))
				{
					Member.TypeName = ValueStruct->Struct ? ValueStruct->Struct->GetName() : TEXT("");
					AddStruct(ValueStruct->Struct);
				}
				else if (FEnumProperty* ValueEnum = CastField<FEnumProperty>(MapProperty->ValueProp))
				{
					Member.TypeName = ValueEnum->GetEnum() ? ValueEnum->GetEnum()->GetName() : TEXT("");
					AddEnum(ValueEnum->GetEnum());
				}

				return Member;
			}

			if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
			{
				Member.TypeName = StructProperty->Struct ? StructProperty->Struct->GetName() : TEXT("");
				AddStruct(StructProperty->Struct);
			}
			else if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
			{
				Member.TypeName = EnumProperty->GetEnum() ? EnumProperty->GetEnum()->GetName() : TEXT("");
				AddEnum(EnumProperty->GetEnum());
			}

			return Member;
		}

		void AddEnum(UEnum* Enum)
		{
			if (!Enum)
			{
				return;
			}

			const FString EnumPath = Enum->GetPathName();
			if (!IsBlueprintDefinedPath(EnumPath) || ProcessedEnums.Contains(EnumPath))
			{
				return;
			}

			ProcessedEnums.Add(EnumPath);

			FBPJEEnum EnumDef;
			EnumDef.Name = Enum->GetName();
			EnumDef.Comment = Enum->GetMetaData(TEXT("ToolTip"));

			for (int32 EnumIndex = 0; EnumIndex < Enum->NumEnums(); ++EnumIndex)
			{
				const FString ValueName = Enum->GetDisplayNameTextByIndex(EnumIndex).ToString();
				if (IsHiddenEnumValue(ValueName))
				{
					continue;
				}

				FBPJEEnumValue Value;
				Value.Name = ValueName;
				Value.Comment = Enum->GetToolTipTextByIndex(EnumIndex).ToString();
				EnumDef.Values.Add(MoveTemp(Value));
			}

			Data->Enums.Add(MoveTemp(EnumDef));
		}

	private:
		UBlueprint* Blueprint = nullptr;
		int32 MaxDepth = 5;
		TArray<FString>& Warnings;
		FBPJECompactBlueprint* Data = nullptr;

		TSet<const UEdGraph*> ProcessedGraphs;
		TSet<FString> ProcessedStructs;
		TSet<FString> ProcessedEnums;
		TSet<FString> SkippedPairKeys;
	};
}

FString BPJEExportService::GetDefaultOutputDir()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("BlueprintExports"));
}

void BPJEExportService::CollectBlueprintAssets(const TArray<FString>& ExplicitBlueprints, const TArray<FString>& Folders, TArray<FAssetData>& OutAssets, TArray<FString>& OutWarnings)
{
	OutAssets.Reset();

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	TMap<FString, FAssetData> DedupedAssets;

	for (const FString& RawFolder : Folders)
	{
		const FString FolderPath = ToNormalizedFolderPath(RawFolder);
		if (FolderPath.IsEmpty())
		{
			continue;
		}

		FARFilter Filter;
		Filter.PackagePaths.Add(*FolderPath);
		Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
		Filter.bRecursivePaths = true;
		Filter.bRecursiveClasses = true;

		TArray<FAssetData> FolderAssets;
		AssetRegistry.GetAssets(Filter, FolderAssets);
		for (const FAssetData& AssetData : FolderAssets)
		{
			DedupedAssets.Add(AssetData.GetObjectPathString(), AssetData);
		}
	}

	for (const FString& RawBlueprintPath : ExplicitBlueprints)
	{
		const FString BlueprintObjectPath = ToNormalizedBlueprintObjectPath(RawBlueprintPath);
		if (BlueprintObjectPath.IsEmpty())
		{
			continue;
		}

		FAssetData AssetData;
		const UE::AssetRegistry::EExists Exists = AssetRegistry.TryGetAssetByObjectPath(FSoftObjectPath(BlueprintObjectPath), AssetData);
		if (Exists != UE::AssetRegistry::EExists::DoesNotExist && AssetData.IsValid() && AssetData.IsInstanceOf(UBlueprint::StaticClass()))
		{
			DedupedAssets.Add(AssetData.GetObjectPathString(), AssetData);
			continue;
		}

		if (UBlueprint* LoadedBlueprint = Cast<UBlueprint>(StaticLoadObject(UBlueprint::StaticClass(), nullptr, *BlueprintObjectPath)))
		{
			FAssetData LoadedAsset(LoadedBlueprint);
			if (LoadedAsset.IsValid())
			{
				DedupedAssets.Add(LoadedAsset.GetObjectPathString(), LoadedAsset);
				continue;
			}
		}

		OutWarnings.Add(FString::Printf(TEXT("Could not resolve Blueprint path '%s'"), *RawBlueprintPath));
	}

	DedupedAssets.GenerateValueArray(OutAssets);
	OutAssets.Sort([](const FAssetData& Left, const FAssetData& Right)
	{
		return Left.GetObjectPathString() < Right.GetObjectPathString();
	});
}

bool BPJEExportService::ExportBlueprintAsset(const FAssetData& BlueprintAsset, const FBPJEExportOptions& Options, FBPJEExportResult& OutResult)
{
	OutResult = FBPJEExportResult();
	OutResult.BlueprintObjectPath = BlueprintAsset.GetObjectPathString();

	UBlueprint* Blueprint = Cast<UBlueprint>(BlueprintAsset.GetAsset());
	if (!Blueprint)
	{
		OutResult.Error = TEXT("Asset is not a Blueprint or failed to load");
		return false;
	}

	return ExportBlueprint(Blueprint, Options, OutResult);
}

bool BPJEExportService::ExportBlueprintObjectPath(const FString& BlueprintObjectPath, const FBPJEExportOptions& Options, FBPJEExportResult& OutResult)
{
	OutResult = FBPJEExportResult();
	OutResult.BlueprintObjectPath = ToNormalizedBlueprintObjectPath(BlueprintObjectPath);

	UBlueprint* Blueprint = Cast<UBlueprint>(StaticLoadObject(UBlueprint::StaticClass(), nullptr, *OutResult.BlueprintObjectPath));
	if (!Blueprint)
	{
		OutResult.Error = FString::Printf(TEXT("Failed to load Blueprint '%s'"), *BlueprintObjectPath);
		return false;
	}

	return ExportBlueprint(Blueprint, Options, OutResult);
}

bool BPJEExportService::ExportBlueprint(UBlueprint* Blueprint, const FBPJEExportOptions& Options, FBPJEExportResult& OutResult)
{
	OutResult = FBPJEExportResult();

	if (!Blueprint)
	{
		OutResult.Error = TEXT("Null Blueprint provided");
		return false;
	}

	const FString OutputDir = Options.OutputDir.IsEmpty()
	? GetDefaultOutputDir()
	: (FPaths::IsRelative(Options.OutputDir)
		? FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / Options.OutputDir)
		: Options.OutputDir);

	if (!IFileManager::Get().MakeDirectory(*OutputDir, true))
	{
		OutResult.Error = FString::Printf(TEXT("Failed to create output directory '%s'"), *OutputDir);
		return false;
	}

	FBlueprintExtractor Extractor(Blueprint, Options.MaxDepth, OutResult.Warnings);
	FBPJECompactBlueprint CompactBlueprint = Extractor.Extract();

	const FString JsonOutput = ToJson(CompactBlueprint, Options.bPrettyPrint);
	if (JsonOutput.IsEmpty())
	{
		OutResult.Error = TEXT("Serialization returned empty JSON");
		return false;
	}

	const FString OutputFilePath = BuildOutputFilePath(OutputDir, Blueprint);
	const FString OutputDirectoryPath = FPaths::GetPath(OutputFilePath);
	if (!OutputDirectoryPath.IsEmpty() && !IFileManager::Get().MakeDirectory(*OutputDirectoryPath, true))
	{
		OutResult.Error = FString::Printf(TEXT("Failed to create output directory '%s'"), *OutputDirectoryPath);
		return false;
	}

	if (!FFileHelper::SaveStringToFile(JsonOutput, *OutputFilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		OutResult.Error = FString::Printf(TEXT("Failed to write '%s'"), *OutputFilePath);
		return false;
	}

	OutResult.BlueprintObjectPath = Blueprint->GetPathName();
	OutResult.OutputFilePath = OutputFilePath;
	OutResult.bTruncated = CompactBlueprint.Metadata.bTruncated;
	OutResult.ExportedGraphCount = CompactBlueprint.Graphs.Num();
	OutResult.SkippedGraphCount = CompactBlueprint.SkippedGraphs.Num();
	OutResult.bSuccess = true;
	return true;
}
