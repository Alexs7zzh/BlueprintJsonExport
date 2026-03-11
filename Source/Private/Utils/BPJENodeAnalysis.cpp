#include "Utils/BPJENodeAnalysis.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "K2Node.h"
#include "K2Node_ActorBoundEvent.h"
#include "K2Node_AddComponent.h"
#include "K2Node_AddComponentByClass.h"
#include "K2Node_AddDelegate.h"
#include "K2Node_AddPinInterface.h"
#include "K2Node_AssignDelegate.h"
#include "K2Node_AssignmentStatement.h"
#include "K2Node_AsyncAction.h"
#include "K2Node_BaseAsyncTask.h"
#include "K2Node_BaseMCDelegate.h"
#include "K2Node_BitmaskLiteral.h"
#include "K2Node_BreakStruct.h"
#include "K2Node_CallArrayFunction.h"
#include "K2Node_CallDataTableFunction.h"
#include "K2Node_CallDelegate.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CallFunctionOnMember.h"
#include "K2Node_CallMaterialParameterCollectionFunction.h"
#include "K2Node_CallParentFunction.h"
#include "K2Node_CastByteToEnum.h"
#include "K2Node_ClassDynamicCast.h"
#include "K2Node_ClearDelegate.h"
#include "K2Node_CommutativeAssociativeBinaryOperator.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_Composite.h"
#include "K2Node_ConstructObjectFromClass.h"
#include "K2Node_ConvertAsset.h"
#include "K2Node_Copy.h"
#include "K2Node_CreateDelegate.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_DeadClass.h"
#include "K2Node_DelegateSet.h"
#include "K2Node_DoOnceMultiInput.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_EaseFunction.h"
#include "K2Node_EditablePinBase.h"
#include "K2Node_EnumEquality.h"
#include "K2Node_EnumInequality.h"
#include "K2Node_EnumLiteral.h"
#include "K2Node_Event.h"
#include "K2Node_EventNodeInterface.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_ExternalGraphInterface.h"
#include "K2Node_ForEachElementInEnum.h"
#include "K2Node_FormatText.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_FunctionTerminator.h"
#include "K2Node_GenericCreateObject.h"
#include "K2Node_GetArrayItem.h"
#include "K2Node_GetClassDefaults.h"
#include "K2Node_GetDataTableRow.h"
#include "K2Node_GetEnumeratorName.h"
#include "K2Node_GetEnumeratorNameAsString.h"
#include "K2Node_GetInputAxisKeyValue.h"
#include "K2Node_GetInputAxisValue.h"
#include "K2Node_GetInputVectorAxisValue.h"
#include "K2Node_GetNumEnumEntries.h"
#include "K2Node_GetSubsystem.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_InputAction.h"
#include "K2Node_InputActionEvent.h"
#include "K2Node_InputAxisEvent.h"
#include "K2Node_InputAxisKeyEvent.h"
#include "K2Node_InputKey.h"
#include "K2Node_InputKeyEvent.h"
#include "K2Node_InputTouch.h"
#include "K2Node_InputTouchEvent.h"
#include "K2Node_InputVectorAxisEvent.h"
#include "K2Node_Knot.h"
#include "K2Node_Literal.h"
#include "K2Node_LoadAsset.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_MakeArray.h"
#include "K2Node_MakeContainer.h"
#include "K2Node_MakeMap.h"
#include "K2Node_MakeSet.h"
#include "K2Node_MakeStruct.h"
#include "K2Node_MakeVariable.h"
#include "K2Node_MathExpression.h"
#include "K2Node_Message.h"
#include "K2Node_MultiGate.h"
#include "K2Node_PromotableOperator.h"
#include "K2Node_PureAssignmentStatement.h"
#include "K2Node_RemoveDelegate.h"
#include "K2Node_Select.h"
#include "K2Node_Self.h"
#include "K2Node_SetFieldsInStruct.h"
#include "K2Node_SetVariableOnPersistentFrame.h"
#include "K2Node_SpawnActor.h"
#include "K2Node_SpawnActorFromClass.h"
#include "K2Node_StructMemberGet.h"
#include "K2Node_StructMemberSet.h"
#include "K2Node_StructOperation.h"
#include "K2Node_Switch.h"
#include "K2Node_SwitchEnum.h"
#include "K2Node_SwitchInteger.h"
#include "K2Node_SwitchName.h"
#include "K2Node_SwitchString.h"
#include "K2Node_TemporaryVariable.h"
#include "K2Node_Timeline.h"
#include "K2Node_Tunnel.h"
#include "K2Node_TunnelBoundary.h"
#include "K2Node_Variable.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_VariableSetRef.h"

namespace
{
	bool IsBlueprintGeneratedClass(const UClass* Class)
	{
		return Class && Class->ClassGeneratedBy && Class->ClassGeneratedBy->IsA<UBlueprint>();
	}

	FString BPJECleanNodeAnalysisClassName(FString Name)
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

	FString GetBaseNodeType(const UClass* NodeClass)
	{
		if (!NodeClass)
		{
			return TEXT("");
		}

		FString ClassName = NodeClass->GetName();
		static const FString Prefix = TEXT("K2Node_");
		if (ClassName.StartsWith(Prefix))
		{
			return ClassName.RightChop(Prefix.Len());
		}

		return ClassName;
	}

	const TMap<FName, EBPJENodeType>& GetExactNodeTypeMappings()
	{
		static const TMap<FName, EBPJENodeType> Mappings = []()
		{
			TMap<FName, EBPJENodeType> Result;

			Result.Add(FName(TEXT("CallFunction")), EBPJENodeType::CallFunction);
			Result.Add(FName(TEXT("CallArrayFunction")), EBPJENodeType::CallArrayFunction);
			Result.Add(FName(TEXT("CallDataTableFunction")), EBPJENodeType::CallDataTableFunction);
			Result.Add(FName(TEXT("CallDelegate")), EBPJENodeType::CallDelegate);
			Result.Add(FName(TEXT("CallFunctionOnMember")), EBPJENodeType::CallFunctionOnMember);
			Result.Add(FName(TEXT("CallMaterialParameterCollectionFunction")), EBPJENodeType::CallMaterialParameterCollection);
			Result.Add(FName(TEXT("CallParentFunction")), EBPJENodeType::CallParentFunction);
			Result.Add(FName(TEXT("FunctionEntry")), EBPJENodeType::FunctionEntry);
			Result.Add(FName(TEXT("FunctionResult")), EBPJENodeType::FunctionResult);
			Result.Add(FName(TEXT("FunctionTerminator")), EBPJENodeType::FunctionTerminator);

			Result.Add(FName(TEXT("Variable")), EBPJENodeType::Variable);
			Result.Add(FName(TEXT("VariableGet")), EBPJENodeType::VariableGet);
			Result.Add(FName(TEXT("VariableSet")), EBPJENodeType::VariableSet);
			Result.Add(FName(TEXT("VariableSetRef")), EBPJENodeType::VariableSetRef);
			Result.Add(FName(TEXT("LocalVariable")), EBPJENodeType::LocalVariable);
			Result.Add(FName(TEXT("MakeVariable")), EBPJENodeType::MakeVariable);
			Result.Add(FName(TEXT("TemporaryVariable")), EBPJENodeType::TemporaryVariable);
			Result.Add(FName(TEXT("SetVariableOnPersistentFrame")), EBPJENodeType::SetVariableOnPersistentFrame);

			Result.Add(FName(TEXT("Event")), EBPJENodeType::Event);
			Result.Add(FName(TEXT("CustomEvent")), EBPJENodeType::CustomEvent);
			Result.Add(FName(TEXT("ActorBoundEvent")), EBPJENodeType::ActorBoundEvent);
			Result.Add(FName(TEXT("ComponentBoundEvent")), EBPJENodeType::ComponentBoundEvent);
			Result.Add(FName(TEXT("InputAction")), EBPJENodeType::InputAction);
			Result.Add(FName(TEXT("InputActionEvent")), EBPJENodeType::InputActionEvent);
			Result.Add(FName(TEXT("InputAxisEvent")), EBPJENodeType::InputAxisEvent);
			Result.Add(FName(TEXT("InputAxisKeyEvent")), EBPJENodeType::InputAxisKeyEvent);
			Result.Add(FName(TEXT("InputKey")), EBPJENodeType::InputKey);
			Result.Add(FName(TEXT("InputKeyEvent")), EBPJENodeType::InputKeyEvent);
			Result.Add(FName(TEXT("InputTouch")), EBPJENodeType::InputTouch);
			Result.Add(FName(TEXT("InputTouchEvent")), EBPJENodeType::InputTouchEvent);
			Result.Add(FName(TEXT("InputVectorAxisEvent")), EBPJENodeType::InputVectorAxisEvent);

			Result.Add(FName(TEXT("ExecutionSequence")), EBPJENodeType::Sequence);
			Result.Add(FName(TEXT("IfThenElse")), EBPJENodeType::Branch);
			Result.Add(FName(TEXT("DoOnceMultiInput")), EBPJENodeType::DoOnceMultiInput);
			Result.Add(FName(TEXT("MultiGate")), EBPJENodeType::MultiGate);
			Result.Add(FName(TEXT("Knot")), EBPJENodeType::Knot);
			Result.Add(FName(TEXT("Tunnel")), EBPJENodeType::Tunnel);
			Result.Add(FName(TEXT("TunnelBoundary")), EBPJENodeType::TunnelBoundary);

			Result.Add(FName(TEXT("Switch")), EBPJENodeType::Switch);
			Result.Add(FName(TEXT("SwitchInteger")), EBPJENodeType::SwitchInt);
			Result.Add(FName(TEXT("SwitchString")), EBPJENodeType::SwitchString);
			Result.Add(FName(TEXT("SwitchEnum")), EBPJENodeType::SwitchEnum);
			Result.Add(FName(TEXT("SwitchName")), EBPJENodeType::SwitchName);

			Result.Add(FName(TEXT("MakeStruct")), EBPJENodeType::MakeStruct);
			Result.Add(FName(TEXT("BreakStruct")), EBPJENodeType::BreakStruct);
			Result.Add(FName(TEXT("SetFieldsInStruct")), EBPJENodeType::SetFieldsInStruct);
			Result.Add(FName(TEXT("StructMemberGet")), EBPJENodeType::StructMemberGet);
			Result.Add(FName(TEXT("StructMemberSet")), EBPJENodeType::StructMemberSet);
			Result.Add(FName(TEXT("StructOperation")), EBPJENodeType::StructOperation);

			Result.Add(FName(TEXT("MakeArray")), EBPJENodeType::MakeArray);
			Result.Add(FName(TEXT("MakeMap")), EBPJENodeType::MakeMap);
			Result.Add(FName(TEXT("MakeSet")), EBPJENodeType::MakeSet);
			Result.Add(FName(TEXT("MakeContainer")), EBPJENodeType::MakeContainer);
			Result.Add(FName(TEXT("GetArrayItem")), EBPJENodeType::GetArrayItem);

			Result.Add(FName(TEXT("DynamicCast")), EBPJENodeType::DynamicCast);
			Result.Add(FName(TEXT("ClassDynamicCast")), EBPJENodeType::ClassDynamicCast);
			Result.Add(FName(TEXT("CastByteToEnum")), EBPJENodeType::CastByteToEnum);
			Result.Add(FName(TEXT("ConvertAsset")), EBPJENodeType::ConvertAsset);

			Result.Add(FName(TEXT("AddDelegate")), EBPJENodeType::AddDelegate);
			Result.Add(FName(TEXT("CreateDelegate")), EBPJENodeType::CreateDelegate);
			Result.Add(FName(TEXT("ClearDelegate")), EBPJENodeType::ClearDelegate);
			Result.Add(FName(TEXT("RemoveDelegate")), EBPJENodeType::RemoveDelegate);
			Result.Add(FName(TEXT("AssignDelegate")), EBPJENodeType::AssignDelegate);
			Result.Add(FName(TEXT("DelegateSet")), EBPJENodeType::DelegateSet);

			Result.Add(FName(TEXT("AsyncAction")), EBPJENodeType::AsyncAction);
			Result.Add(FName(TEXT("BaseAsyncTask")), EBPJENodeType::BaseAsyncTask);

			Result.Add(FName(TEXT("AddComponent")), EBPJENodeType::AddComponent);
			Result.Add(FName(TEXT("AddComponentByClass")), EBPJENodeType::AddComponentByClass);
			Result.Add(FName(TEXT("AddPinInterface")), EBPJENodeType::AddPinInterface);

			Result.Add(FName(TEXT("ConstructObjectFromClass")), EBPJENodeType::ConstructObjectFromClass);
			Result.Add(FName(TEXT("GenericCreateObject")), EBPJENodeType::GenericCreateObject);
			Result.Add(FName(TEXT("Timeline")), EBPJENodeType::Timeline);
			Result.Add(FName(TEXT("SpawnActor")), EBPJENodeType::SpawnActor);
			Result.Add(FName(TEXT("SpawnActorFromClass")), EBPJENodeType::SpawnActorFromClass);
			Result.Add(FName(TEXT("FormatText")), EBPJENodeType::FormatText);
			Result.Add(FName(TEXT("GetClassDefaults")), EBPJENodeType::GetClassDefaults);
			Result.Add(FName(TEXT("GetSubsystem")), EBPJENodeType::GetSubsystem);
			Result.Add(FName(TEXT("LoadAsset")), EBPJENodeType::LoadAsset);
			Result.Add(FName(TEXT("Copy")), EBPJENodeType::Copy);

			Result.Add(FName(TEXT("BitmaskLiteral")), EBPJENodeType::BitmaskLiteral);
			Result.Add(FName(TEXT("EnumEquality")), EBPJENodeType::EnumEquality);
			Result.Add(FName(TEXT("EnumInequality")), EBPJENodeType::EnumInequality);
			Result.Add(FName(TEXT("EnumLiteral")), EBPJENodeType::EnumLiteral);
			Result.Add(FName(TEXT("GetEnumeratorName")), EBPJENodeType::GetEnumeratorName);
			Result.Add(FName(TEXT("GetEnumeratorNameAsString")), EBPJENodeType::GetEnumeratorNameAsString);
			Result.Add(FName(TEXT("GetNumEnumEntries")), EBPJENodeType::GetNumEnumEntries);
			Result.Add(FName(TEXT("MathExpression")), EBPJENodeType::MathExpression);
			Result.Add(FName(TEXT("EaseFunction")), EBPJENodeType::EaseFunction);
			Result.Add(FName(TEXT("CommutativeAssociativeBinaryOperator")), EBPJENodeType::CommutativeAssociativeBinaryOperator);
			Result.Add(FName(TEXT("PureAssignmentStatement")), EBPJENodeType::PureAssignmentStatement);
			Result.Add(FName(TEXT("AssignmentStatement")), EBPJENodeType::AssignmentStatement);

			Result.Add(FName(TEXT("Self")), EBPJENodeType::Self);
			Result.Add(FName(TEXT("Composite")), EBPJENodeType::Composite);
			Result.Add(FName(TEXT("DeadClass")), EBPJENodeType::DeadClass);
			Result.Add(FName(TEXT("Literal")), EBPJENodeType::Literal);
			Result.Add(FName(TEXT("Message")), EBPJENodeType::Message);
			Result.Add(FName(TEXT("PromotableOperator")), EBPJENodeType::PromotableOperator);
			Result.Add(FName(TEXT("MacroInstance")), EBPJENodeType::MacroInstance);
			Result.Add(FName(TEXT("BaseMCDelegate")), EBPJENodeType::BaseMCDelegate);

			return Result;
		}();

		return Mappings;
	}

	TMap<const UClass*, EBPJENodeType>& GetNodeTypeCache()
	{
		static TMap<const UClass*, EBPJENodeType> Cache;
		return Cache;
	}

	EBPJENodeType ResolveMacroInstanceNodeType(const UK2Node_MacroInstance* Node)
	{
		if (!Node)
		{
			return EBPJENodeType::MacroInstance;
		}

		UEdGraph* MacroGraph = Node->GetMacroGraph();
		if (!MacroGraph)
		{
			return EBPJENodeType::MacroInstance;
		}

		FString MacroName = MacroGraph->GetName();
		MacroName.ReplaceInline(TEXT(" "), TEXT(""));

		if (MacroName == TEXT("ForLoop") || MacroName == TEXT("ForLoopWithBreak"))
		{
			return EBPJENodeType::ForLoop;
		}
		if (MacroName == TEXT("ForEachLoop") || MacroName == TEXT("ForEachLoopWithBreak"))
		{
			return EBPJENodeType::ForEachLoop;
		}
		if (MacroName == TEXT("WhileLoop"))
		{
			return EBPJENodeType::WhileLoop;
		}
		if (MacroName == TEXT("Gate"))
		{
			return EBPJENodeType::Gate;
		}
		if (MacroName == TEXT("DoOnce"))
		{
			return EBPJENodeType::DoOnce;
		}

		return EBPJENodeType::MacroInstance;
	}

	UEnum* ResolveEnumTypeFromPins(const UK2Node* Node)
	{
		if (!Node)
		{
			return nullptr;
		}

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin)
			{
				continue;
			}

			if ((Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Enum ||
					Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Byte) &&
				Pin->PinType.PinSubCategoryObject.IsValid())
			{
				if (UEnum* Enum = Cast<UEnum>(Pin->PinType.PinSubCategoryObject.Get()))
				{
					return Enum;
				}
			}
		}

		return nullptr;
	}

	EBPJENodeType DetermineVariableNodeType(const UK2Node_Variable* Node)
	{
		if (!Node)
		{
			return EBPJENodeType::Variable;
		}

		FProperty* VariableProperty = Node->GetPropertyForVariable();
		if (!VariableProperty)
		{
			return EBPJENodeType::Variable;
		}

		UClass* VariableBlueprintClass = Node->GetBlueprintClassFromNode();
		if (!VariableBlueprintClass)
		{
			return EBPJENodeType::Variable;
		}

		const UStruct* VariableScope = Node->VariableReference.GetMemberScope(VariableBlueprintClass);
		if (VariableProperty->HasAnyPropertyFlags(CPF_Parm))
		{
			return EBPJENodeType::FunctionParameter;
		}

		if (!VariableProperty->HasAnyPropertyFlags(CPF_Parm) && Node->VariableReference.IsLocalScope())
		{
			return EBPJENodeType::LocalFunctionVariable;
		}

		if (Node->IsA<UK2Node_VariableGet>())
		{
			return VariableScope != nullptr ? EBPJENodeType::LocalVariableGet : EBPJENodeType::VariableGet;
		}

		if (Node->IsA<UK2Node_VariableSet>())
		{
			return VariableScope != nullptr ? EBPJENodeType::LocalVariableSet : EBPJENodeType::VariableSet;
		}

		return EBPJENodeType::Variable;
	}

	bool ResolveNodeTypeFromInheritance(const UK2Node* Node, EBPJENodeType& OutType)
	{
		if (Node->IsA<UK2Node_CallFunction>())
		{
			OutType = EBPJENodeType::CallFunction;
			return true;
		}

		if (Node->IsA<UK2Node_Event>())
		{
			OutType = EBPJENodeType::Event;
			return true;
		}

		if (Node->IsA<UK2Node_MakeStruct>())
		{
			OutType = EBPJENodeType::MakeStruct;
			return true;
		}

		if (Node->IsA<UK2Node_VariableSetRef>())
		{
			OutType = EBPJENodeType::VariableSetRef;
			return true;
		}

		if (Node->IsA<UK2Node_ActorBoundEvent>())
		{
			OutType = EBPJENodeType::ActorBoundEvent;
			return true;
		}

		if (Node->IsA<UK2Node_AddComponent>())
		{
			OutType = EBPJENodeType::AddComponent;
			return true;
		}

		if (Node->IsA<UK2Node_AddComponentByClass>())
		{
			OutType = EBPJENodeType::AddComponentByClass;
			return true;
		}

		if (Node->IsA<UK2Node_AddDelegate>())
		{
			OutType = EBPJENodeType::AddDelegate;
			return true;
		}

		if (Node->IsA<UK2Node_AddPinInterface>())
		{
			OutType = EBPJENodeType::AddPinInterface;
			return true;
		}

		if (Node->IsA<UK2Node_AssignDelegate>())
		{
			OutType = EBPJENodeType::AssignDelegate;
			return true;
		}

		if (Node->IsA<UK2Node_AssignmentStatement>())
		{
			OutType = EBPJENodeType::AssignmentStatement;
			return true;
		}

		if (Node->IsA<UK2Node_AsyncAction>())
		{
			OutType = EBPJENodeType::AsyncAction;
			return true;
		}

		if (Node->IsA<UK2Node_BaseAsyncTask>())
		{
			OutType = EBPJENodeType::BaseAsyncTask;
			return true;
		}

		if (Node->IsA<UK2Node_BaseMCDelegate>())
		{
			OutType = EBPJENodeType::BaseMCDelegate;
			return true;
		}

		if (Node->IsA<UK2Node_BitmaskLiteral>())
		{
			OutType = EBPJENodeType::BitmaskLiteral;
			return true;
		}

		if (Node->IsA<UK2Node_BreakStruct>())
		{
			OutType = EBPJENodeType::BreakStruct;
			return true;
		}

		if (Node->IsA<UK2Node_CallArrayFunction>())
		{
			OutType = EBPJENodeType::CallArrayFunction;
			return true;
		}

		if (Node->IsA<UK2Node_CallDataTableFunction>())
		{
			OutType = EBPJENodeType::CallDataTableFunction;
			return true;
		}

		if (Node->IsA<UK2Node_CallDelegate>())
		{
			OutType = EBPJENodeType::CallDelegate;
			return true;
		}

		if (Node->IsA<UK2Node_CallFunctionOnMember>())
		{
			OutType = EBPJENodeType::CallFunctionOnMember;
			return true;
		}

		if (Node->IsA<UK2Node_CallMaterialParameterCollectionFunction>())
		{
			OutType = EBPJENodeType::CallMaterialParameterCollection;
			return true;
		}

		if (Node->IsA<UK2Node_CallParentFunction>())
		{
			OutType = EBPJENodeType::CallParentFunction;
			return true;
		}

		if (Node->IsA<UK2Node_CastByteToEnum>())
		{
			OutType = EBPJENodeType::CastByteToEnum;
			return true;
		}

		if (Node->IsA<UK2Node_ClassDynamicCast>())
		{
			OutType = EBPJENodeType::ClassDynamicCast;
			return true;
		}

		if (Node->IsA<UK2Node_ClearDelegate>())
		{
			OutType = EBPJENodeType::ClearDelegate;
			return true;
		}

		if (Node->IsA<UK2Node_CommutativeAssociativeBinaryOperator>())
		{
			OutType = EBPJENodeType::CommutativeAssociativeBinaryOperator;
			return true;
		}

		if (Node->IsA<UK2Node_ComponentBoundEvent>())
		{
			OutType = EBPJENodeType::ComponentBoundEvent;
			return true;
		}

		if (Node->IsA<UK2Node_Composite>())
		{
			OutType = EBPJENodeType::Composite;
			return true;
		}

		if (Node->IsA<UK2Node_ConstructObjectFromClass>())
		{
			OutType = EBPJENodeType::ConstructObjectFromClass;
			return true;
		}

		if (Node->IsA<UK2Node_ConvertAsset>())
		{
			OutType = EBPJENodeType::ConvertAsset;
			return true;
		}

		if (Node->IsA<UK2Node_Copy>())
		{
			OutType = EBPJENodeType::Copy;
			return true;
		}

		if (Node->IsA<UK2Node_CreateDelegate>())
		{
			OutType = EBPJENodeType::CreateDelegate;
			return true;
		}

		if (Node->IsA<UK2Node_CustomEvent>())
		{
			OutType = EBPJENodeType::CustomEvent;
			return true;
		}

		if (Node->IsA<UK2Node_DeadClass>())
		{
			OutType = EBPJENodeType::DeadClass;
			return true;
		}

		if (Node->IsA<UK2Node_DelegateSet>())
		{
			OutType = EBPJENodeType::DelegateSet;
			return true;
		}

		if (Node->IsA<UK2Node_DoOnceMultiInput>())
		{
			OutType = EBPJENodeType::DoOnceMultiInput;
			return true;
		}

		if (Node->IsA<UK2Node_DynamicCast>())
		{
			OutType = EBPJENodeType::DynamicCast;
			return true;
		}

		if (Node->IsA<UK2Node_EaseFunction>())
		{
			OutType = EBPJENodeType::EaseFunction;
			return true;
		}

		if (Node->IsA<UK2Node_EditablePinBase>())
		{
			OutType = EBPJENodeType::EditablePinBase;
			return true;
		}

		if (Node->IsA<UK2Node_EnumEquality>())
		{
			OutType = EBPJENodeType::EnumEquality;
			return true;
		}

		if (Node->IsA<UK2Node_EnumInequality>())
		{
			OutType = EBPJENodeType::EnumInequality;
			return true;
		}

		if (Node->IsA<UK2Node_EnumLiteral>())
		{
			OutType = EBPJENodeType::EnumLiteral;
			return true;
		}

		if (Node->IsA<UK2Node_EventNodeInterface>())
		{
			OutType = EBPJENodeType::EventNodeInterface;
			return true;
		}

		if (Node->IsA<UK2Node_ExecutionSequence>())
		{
			OutType = EBPJENodeType::Sequence;
			return true;
		}

		if (Node->IsA<UK2Node_ExternalGraphInterface>())
		{
			OutType = EBPJENodeType::ExternalGraphInterface;
			return true;
		}

		if (Node->IsA<UK2Node_ForEachElementInEnum>())
		{
			OutType = EBPJENodeType::ForEachElementInEnum;
			return true;
		}

		if (Node->IsA<UK2Node_FormatText>())
		{
			OutType = EBPJENodeType::FormatText;
			return true;
		}

		if (Node->IsA<UK2Node_FunctionEntry>())
		{
			OutType = EBPJENodeType::FunctionEntry;
			return true;
		}

		if (Node->IsA<UK2Node_FunctionResult>())
		{
			OutType = EBPJENodeType::FunctionResult;
			return true;
		}

		if (Node->IsA<UK2Node_FunctionTerminator>())
		{
			OutType = EBPJENodeType::FunctionTerminator;
			return true;
		}

		if (Node->IsA<UK2Node_GenericCreateObject>())
		{
			OutType = EBPJENodeType::GenericCreateObject;
			return true;
		}

		if (Node->IsA<UK2Node_GetArrayItem>())
		{
			OutType = EBPJENodeType::GetArrayItem;
			return true;
		}

		if (Node->IsA<UK2Node_GetClassDefaults>())
		{
			OutType = EBPJENodeType::GetClassDefaults;
			return true;
		}

		if (Node->IsA<UK2Node_GetDataTableRow>())
		{
			OutType = EBPJENodeType::GetDataTableRow;
			return true;
		}

		if (Node->IsA<UK2Node_GetEnumeratorName>())
		{
			OutType = EBPJENodeType::GetEnumeratorName;
			return true;
		}

		if (Node->IsA<UK2Node_GetEnumeratorNameAsString>())
		{
			OutType = EBPJENodeType::GetEnumeratorNameAsString;
			return true;
		}

		if (Node->IsA<UK2Node_GetInputAxisKeyValue>())
		{
			OutType = EBPJENodeType::GetInputAxisKeyValue;
			return true;
		}

		if (Node->IsA<UK2Node_GetInputAxisValue>())
		{
			OutType = EBPJENodeType::GetInputAxisValue;
			return true;
		}

		if (Node->IsA<UK2Node_GetInputVectorAxisValue>())
		{
			OutType = EBPJENodeType::GetInputVectorAxisValue;
			return true;
		}

		if (Node->IsA<UK2Node_GetNumEnumEntries>())
		{
			OutType = EBPJENodeType::GetNumEnumEntries;
			return true;
		}

		if (Node->IsA<UK2Node_GetSubsystem>())
		{
			OutType = EBPJENodeType::GetSubsystem;
			return true;
		}

		if (Node->IsA<UK2Node_IfThenElse>())
		{
			OutType = EBPJENodeType::Branch;
			return true;
		}

		if (Node->IsA<UK2Node_InputAction>())
		{
			OutType = EBPJENodeType::InputAction;
			return true;
		}

		if (Node->IsA<UK2Node_InputActionEvent>())
		{
			OutType = EBPJENodeType::InputActionEvent;
			return true;
		}

		if (Node->IsA<UK2Node_InputAxisEvent>())
		{
			OutType = EBPJENodeType::InputAxisEvent;
			return true;
		}

		if (Node->IsA<UK2Node_InputAxisKeyEvent>())
		{
			OutType = EBPJENodeType::InputAxisKeyEvent;
			return true;
		}

		if (Node->IsA<UK2Node_InputKey>())
		{
			OutType = EBPJENodeType::InputKey;
			return true;
		}

		if (Node->IsA<UK2Node_InputKeyEvent>())
		{
			OutType = EBPJENodeType::InputKeyEvent;
			return true;
		}

		if (Node->IsA<UK2Node_InputTouch>())
		{
			OutType = EBPJENodeType::InputTouch;
			return true;
		}

		if (Node->IsA<UK2Node_InputTouchEvent>())
		{
			OutType = EBPJENodeType::InputTouchEvent;
			return true;
		}

		if (Node->IsA<UK2Node_InputVectorAxisEvent>())
		{
			OutType = EBPJENodeType::InputVectorAxisEvent;
			return true;
		}

		if (Node->IsA<UK2Node_Knot>())
		{
			OutType = EBPJENodeType::Knot;
			return true;
		}

		if (Node->IsA<UK2Node_Literal>())
		{
			OutType = EBPJENodeType::Literal;
			return true;
		}

		if (Node->IsA<UK2Node_LoadAsset>())
		{
			OutType = EBPJENodeType::LoadAsset;
			return true;
		}

		if (Node->IsA<UK2Node_MacroInstance>())
		{
			OutType = EBPJENodeType::MacroInstance;
			return true;
		}

		if (Node->IsA<UK2Node_MakeArray>())
		{
			OutType = EBPJENodeType::MakeArray;
			return true;
		}

		if (Node->IsA<UK2Node_MakeContainer>())
		{
			OutType = EBPJENodeType::MakeContainer;
			return true;
		}

		if (Node->IsA<UK2Node_MakeMap>())
		{
			OutType = EBPJENodeType::MakeMap;
			return true;
		}

		if (Node->IsA<UK2Node_MakeSet>())
		{
			OutType = EBPJENodeType::MakeSet;
			return true;
		}

		if (Node->IsA<UK2Node_MakeVariable>())
		{
			OutType = EBPJENodeType::MakeVariable;
			return true;
		}

		if (Node->IsA<UK2Node_MathExpression>())
		{
			OutType = EBPJENodeType::MathExpression;
			return true;
		}

		if (Node->IsA<UK2Node_Message>())
		{
			OutType = EBPJENodeType::Message;
			return true;
		}

		if (Node->IsA<UK2Node_MultiGate>())
		{
			OutType = EBPJENodeType::MultiGate;
			return true;
		}

		if (Node->IsA<UK2Node_PromotableOperator>())
		{
			OutType = EBPJENodeType::PromotableOperator;
			return true;
		}

		if (Node->IsA<UK2Node_PureAssignmentStatement>())
		{
			OutType = EBPJENodeType::PureAssignmentStatement;
			return true;
		}

		if (Node->IsA<UK2Node_RemoveDelegate>())
		{
			OutType = EBPJENodeType::RemoveDelegate;
			return true;
		}

		if (Node->IsA<UK2Node_Select>())
		{
			OutType = EBPJENodeType::Select;
			return true;
		}

		if (Node->IsA<UK2Node_Self>())
		{
			OutType = EBPJENodeType::Self;
			return true;
		}

		if (Node->IsA<UK2Node_SetFieldsInStruct>())
		{
			OutType = EBPJENodeType::SetFieldsInStruct;
			return true;
		}

		if (Node->IsA<UK2Node_SetVariableOnPersistentFrame>())
		{
			OutType = EBPJENodeType::SetVariableOnPersistentFrame;
			return true;
		}

		if (Node->IsA<UK2Node_SpawnActor>())
		{
			OutType = EBPJENodeType::SpawnActor;
			return true;
		}

		if (Node->IsA<UK2Node_SpawnActorFromClass>())
		{
			OutType = EBPJENodeType::SpawnActorFromClass;
			return true;
		}

		if (Node->IsA<UK2Node_StructMemberGet>())
		{
			OutType = EBPJENodeType::StructMemberGet;
			return true;
		}

		if (Node->IsA<UK2Node_StructMemberSet>())
		{
			OutType = EBPJENodeType::StructMemberSet;
			return true;
		}

		if (Node->IsA<UK2Node_StructOperation>())
		{
			OutType = EBPJENodeType::StructOperation;
			return true;
		}

		if (Node->IsA<UK2Node_Switch>())
		{
			OutType = EBPJENodeType::Switch;
			return true;
		}

		if (Node->IsA<UK2Node_SwitchEnum>())
		{
			OutType = EBPJENodeType::SwitchEnum;
			return true;
		}

		if (Node->IsA<UK2Node_SwitchInteger>())
		{
			OutType = EBPJENodeType::SwitchInt;
			return true;
		}

		if (Node->IsA<UK2Node_SwitchName>())
		{
			OutType = EBPJENodeType::SwitchName;
			return true;
		}

		if (Node->IsA<UK2Node_SwitchString>())
		{
			OutType = EBPJENodeType::SwitchString;
			return true;
		}

		if (Node->IsA<UK2Node_TemporaryVariable>())
		{
			OutType = EBPJENodeType::TemporaryVariable;
			return true;
		}

		if (Node->IsA<UK2Node_Timeline>())
		{
			OutType = EBPJENodeType::Timeline;
			return true;
		}

		if (Node->IsA<UK2Node_Tunnel>())
		{
			OutType = EBPJENodeType::Tunnel;
			return true;
		}

		if (Node->IsA<UK2Node_TunnelBoundary>())
		{
			OutType = EBPJENodeType::TunnelBoundary;
			return true;
		}

		OutType = EBPJENodeType::CallFunction;
		return false;
	}

	EBPJENodeType ResolveNodeType(const UK2Node* Node)
	{
		if (!Node)
		{
			return EBPJENodeType::CallFunction;
		}

		if (const UK2Node_Variable* VariableNode = Cast<UK2Node_Variable>(Node))
		{
			return DetermineVariableNodeType(VariableNode);
		}

		if (const UK2Node_MacroInstance* MacroInstanceNode = Cast<UK2Node_MacroInstance>(Node))
		{
			return ResolveMacroInstanceNodeType(MacroInstanceNode);
		}

		const UClass* NodeClass = Node->GetClass();
		TMap<const UClass*, EBPJENodeType>& Cache = GetNodeTypeCache();
		if (const EBPJENodeType* CachedType = Cache.Find(NodeClass))
		{
			return *CachedType;
		}

		const FName BaseNodeType(*GetBaseNodeType(NodeClass));
		if (const EBPJENodeType* ExactType = GetExactNodeTypeMappings().Find(BaseNodeType))
		{
			Cache.Add(NodeClass, *ExactType);
			return *ExactType;
		}

		EBPJENodeType ResolvedType = EBPJENodeType::CallFunction;
		ResolveNodeTypeFromInheritance(Node, ResolvedType);
		Cache.Add(NodeClass, ResolvedType);
		return ResolvedType;
	}

	void PopulateMemberData(UK2Node* Node, FBPJENodeAnalysisResult& OutResult)
	{
		switch (OutResult.NodeType)
		{
		case EBPJENodeType::CallFunction:
		case EBPJENodeType::CallArrayFunction:
		case EBPJENodeType::CallDataTableFunction:
		case EBPJENodeType::CallFunctionOnMember:
		case EBPJENodeType::CallMaterialParameterCollection:
		case EBPJENodeType::CallParentFunction:
		case EBPJENodeType::CommutativeAssociativeBinaryOperator:
		case EBPJENodeType::PromotableOperator:
		case EBPJENodeType::GetDataTableRow:
			{
				if (UK2Node_CallFunction* CallFunction = Cast<UK2Node_CallFunction>(Node))
				{
					if (UFunction* Function = CallFunction->GetTargetFunction())
					{
						OutResult.MemberName = Function->GetName();
						if (UClass* OwnerClass = Function->GetOwnerClass())
						{
							OutResult.bMemberIsBlueprintDefined = IsBlueprintGeneratedClass(OwnerClass);
							OutResult.MemberParent = BPJECleanNodeAnalysisClassName(OwnerClass->GetName());
						}
					}
				}
				break;
			}

		case EBPJENodeType::Variable:
		case EBPJENodeType::VariableGet:
		case EBPJENodeType::VariableSet:
		case EBPJENodeType::FunctionParameter:
		case EBPJENodeType::LocalFunctionVariable:
		case EBPJENodeType::LocalVariableGet:
		case EBPJENodeType::LocalVariableSet:
			{
				if (UK2Node_Variable* VariableNode = Cast<UK2Node_Variable>(Node))
				{
					OutResult.MemberName = VariableNode->VariableReference.GetMemberName().ToString();
					if (UClass* ParentClass = VariableNode->VariableReference.GetMemberParentClass())
					{
						OutResult.bMemberIsBlueprintDefined = IsBlueprintGeneratedClass(ParentClass);
						OutResult.MemberParent = BPJECleanNodeAnalysisClassName(ParentClass->GetName());
					}
				}
				break;
			}

		case EBPJENodeType::Event:
		case EBPJENodeType::CustomEvent:
		case EBPJENodeType::ActorBoundEvent:
		case EBPJENodeType::ComponentBoundEvent:
		case EBPJENodeType::InputActionEvent:
		case EBPJENodeType::InputAxisEvent:
		case EBPJENodeType::InputAxisKeyEvent:
		case EBPJENodeType::InputKeyEvent:
		case EBPJENodeType::InputTouchEvent:
		case EBPJENodeType::InputVectorAxisEvent:
			{
				if (UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node))
				{
					OutResult.MemberName = EventNode->EventReference.GetMemberName().ToString();
					if (UClass* ParentClass = EventNode->EventReference.GetMemberParentClass())
					{
						OutResult.bMemberIsBlueprintDefined = IsBlueprintGeneratedClass(ParentClass);
						OutResult.MemberParent = BPJECleanNodeAnalysisClassName(ParentClass->GetName());
					}
				}
				break;
			}

		case EBPJENodeType::InputAction:
			{
				if (UK2Node_InputAction* InputActionNode = Cast<UK2Node_InputAction>(Node))
				{
					OutResult.MemberParent = TEXT("Input");
					OutResult.MemberName = InputActionNode->InputActionName.ToString();
				}
				break;
			}

		case EBPJENodeType::InputKey:
			{
				if (UK2Node_InputKey* InputKeyNode = Cast<UK2Node_InputKey>(Node))
				{
					OutResult.MemberParent = TEXT("Input");
					OutResult.MemberName = InputKeyNode->InputKey.GetFName().ToString();
				}
				break;
			}

		case EBPJENodeType::InputTouch:
			{
				if (Cast<UK2Node_InputTouch>(Node))
				{
					OutResult.MemberParent = TEXT("Input");
					OutResult.MemberName = TEXT("Touch");
				}
				break;
			}

		default:
			break;
		}
	}

	UEdGraph* ResolveChildGraphFromFunction(UFunction* Function)
	{
		if (!Function)
		{
			return nullptr;
		}

		if (UBlueprintGeneratedClass* BlueprintClass = Cast<UBlueprintGeneratedClass>(Function->GetOwnerClass()))
		{
			if (UBlueprint* FunctionBlueprint = Cast<UBlueprint>(BlueprintClass->ClassGeneratedBy))
			{
				for (UEdGraph* FunctionGraph : FunctionBlueprint->FunctionGraphs)
				{
					if (FunctionGraph && FunctionGraph->GetFName() == Function->GetFName())
					{
						return FunctionGraph;
					}
				}
			}
		}

		return nullptr;
	}

	UEdGraph* ResolveChildGraphFromDelegateScope(UClass* ScopeClass, FName FunctionName)
	{
		if (!ScopeClass)
		{
			return nullptr;
		}

		if (UBlueprint* ScopeBlueprint = Cast<UBlueprint>(ScopeClass->ClassGeneratedBy))
		{
			for (UEdGraph* FunctionGraph : ScopeBlueprint->FunctionGraphs)
			{
				if (FunctionGraph && FunctionGraph->GetFName() == FunctionName)
				{
					return FunctionGraph;
				}
			}
		}

		return nullptr;
	}

	void PopulateChildGraphAndTypes(UK2Node* Node, FBPJENodeAnalysisResult& OutResult)
	{
		switch (OutResult.NodeType)
		{
		case EBPJENodeType::Composite:
			{
				if (UK2Node_Composite* CompositeNode = Cast<UK2Node_Composite>(Node))
				{
					OutResult.ChildGraph = CompositeNode->BoundGraph;
				}
				break;
			}

		case EBPJENodeType::ForEachLoop:
		case EBPJENodeType::WhileLoop:
		case EBPJENodeType::ForLoop:
		case EBPJENodeType::Gate:
		case EBPJENodeType::DoOnce:
		case EBPJENodeType::MacroInstance:
			{
				if (UK2Node_MacroInstance* MacroInstance = Cast<UK2Node_MacroInstance>(Node))
				{
					OutResult.ChildGraph = MacroInstance->GetMacroGraph();
				}
				break;
			}

		case EBPJENodeType::CallFunction:
		case EBPJENodeType::CallArrayFunction:
		case EBPJENodeType::CallDataTableFunction:
		case EBPJENodeType::CallFunctionOnMember:
		case EBPJENodeType::CallMaterialParameterCollection:
		case EBPJENodeType::CallParentFunction:
		case EBPJENodeType::GetDataTableRow:
			{
				if (UK2Node_CallFunction* FunctionNode = Cast<UK2Node_CallFunction>(Node))
				{
					OutResult.ChildGraph = ResolveChildGraphFromFunction(FunctionNode->GetTargetFunction());
				}
				break;
			}

		case EBPJENodeType::CreateDelegate:
			{
				if (UK2Node_CreateDelegate* CreateDelegateNode = Cast<UK2Node_CreateDelegate>(Node))
				{
					OutResult.ChildGraph = ResolveChildGraphFromDelegateScope(
						CreateDelegateNode->GetScopeClass(),
						CreateDelegateNode->GetFunctionName());
				}
				break;
			}

		case EBPJENodeType::StructOperation:
			{
				if (UK2Node_StructOperation* StructOperation = Cast<UK2Node_StructOperation>(Node))
				{
					OutResult.StructType = StructOperation->StructType;
				}
				break;
			}

		case EBPJENodeType::MakeStruct:
			{
				if (UK2Node_MakeStruct* MakeStruct = Cast<UK2Node_MakeStruct>(Node))
				{
					OutResult.StructType = MakeStruct->StructType;
				}
				break;
			}

		case EBPJENodeType::BreakStruct:
			{
				if (UK2Node_BreakStruct* BreakStruct = Cast<UK2Node_BreakStruct>(Node))
				{
					OutResult.StructType = BreakStruct->StructType;
				}
				break;
			}

		default:
			break;
		}

		switch (OutResult.NodeType)
		{
		case EBPJENodeType::CastByteToEnum:
			{
				if (UK2Node_CastByteToEnum* CastByteToEnum = Cast<UK2Node_CastByteToEnum>(Node))
				{
					OutResult.EnumType = CastByteToEnum->GetEnum();
				}
				break;
			}

		case EBPJENodeType::EnumLiteral:
			{
				if (UK2Node_EnumLiteral* EnumLiteral = Cast<UK2Node_EnumLiteral>(Node))
				{
					OutResult.EnumType = EnumLiteral->GetEnum();
				}
				break;
			}

		case EBPJENodeType::GetEnumeratorName:
		case EBPJENodeType::GetEnumeratorNameAsString:
			{
				OutResult.EnumType = ResolveEnumTypeFromPins(Node);
				break;
			}

		case EBPJENodeType::GetNumEnumEntries:
			{
				if (UK2Node_GetNumEnumEntries* GetNumEnumEntries = Cast<UK2Node_GetNumEnumEntries>(Node))
				{
					OutResult.EnumType = GetNumEnumEntries->GetEnum();
				}
				break;
			}

		case EBPJENodeType::ForEachElementInEnum:
			{
				if (UK2Node_ForEachElementInEnum* ForEachElementInEnum = Cast<UK2Node_ForEachElementInEnum>(Node))
				{
					OutResult.EnumType = ForEachElementInEnum->GetEnum();
				}
				break;
			}

		case EBPJENodeType::SwitchEnum:
			{
				if (UK2Node_SwitchEnum* SwitchEnum = Cast<UK2Node_SwitchEnum>(Node))
				{
					OutResult.EnumType = SwitchEnum->GetEnum();
				}
				break;
			}

		default:
			break;
		}
	}
}

FBPJENodeAnalysisResult AnalyzeNode(UK2Node* Node)
{
	FBPJENodeAnalysisResult Result;
	Result.NodeType = ResolveNodeType(Node);
	PopulateMemberData(Node, Result);
	PopulateChildGraphAndTypes(Node, Result);
	return Result;
}
