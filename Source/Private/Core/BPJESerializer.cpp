#include "Core/BPJESerializer.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
constexpr int32 BPJE_SchemaVersion = 1;

class FStringTableBuilder
{
public:
    FStringTableBuilder()
    {
        Strings.Add(TEXT(""));
        StringToIndex.Add(TEXT(""), 0);
    }

    int32 Add(const FString& Value)
    {
        const FString Normalized = Value.IsEmpty() ? TEXT("") : Value;
        if (const int32* Existing = StringToIndex.Find(Normalized))
        {
            return *Existing;
        }

        const int32 NewIndex = Strings.Add(Normalized);
        StringToIndex.Add(Normalized, NewIndex);
        return NewIndex;
    }

    const TArray<FString>& GetStrings() const
    {
        return Strings;
    }

private:
    TArray<FString> Strings;
    TMap<FString, int32> StringToIndex;
};

static TSharedPtr<FJsonValueNumber> Num(int32 Value)
{
    return MakeShared<FJsonValueNumber>(Value);
}

static TSharedPtr<FJsonValueArray> Arr(const TArray<TSharedPtr<FJsonValue>>& Values)
{
    return MakeShared<FJsonValueArray>(Values);
}

static TArray<TSharedPtr<FJsonValue>> ToStringArray(const TArray<FString>& Values)
{
    TArray<TSharedPtr<FJsonValue>> JsonValues;
    JsonValues.Reserve(Values.Num());
    for (const FString& Value : Values)
    {
        JsonValues.Add(MakeShared<FJsonValueString>(Value));
    }
    return JsonValues;
}

static TArray<TSharedPtr<FJsonValue>> ToNumberArray(const TArray<int32>& Values)
{
    TArray<TSharedPtr<FJsonValue>> JsonValues;
    JsonValues.Reserve(Values.Num());
    for (const int32 Value : Values)
    {
        JsonValues.Add(Num(Value));
    }
    return JsonValues;
}
}

FString ToJson(const FBPJECompactBlueprint& Blueprint, bool bPrettyPrint)
{
    FStringTableBuilder StringTable;

    TSharedPtr<FJsonObject> RootObject = MakeShared<FJsonObject>();
    RootObject->SetNumberField(TEXT("v"), BPJE_SchemaVersion);

    TArray<TSharedPtr<FJsonValue>> MetadataTuple;
    MetadataTuple.Add(Num(StringTable.Add(Blueprint.Metadata.BlueprintName)));
    MetadataTuple.Add(Num(StringTable.Add(Blueprint.Metadata.PackagePath)));
    MetadataTuple.Add(Num(StringTable.Add(Blueprint.Metadata.ObjectPath)));
    MetadataTuple.Add(Num(StringTable.Add(Blueprint.Metadata.GeneratedClass)));
    MetadataTuple.Add(Num(static_cast<int32>(Blueprint.Metadata.BlueprintType)));
    MetadataTuple.Add(Num(Blueprint.Metadata.MaxDepth));
    MetadataTuple.Add(Num(Blueprint.Metadata.bTruncated ? 1 : 0));
    RootObject->SetArrayField(TEXT("m"), MetadataTuple);

    TArray<TSharedPtr<FJsonValue>> GraphArray;
    GraphArray.Reserve(Blueprint.Graphs.Num());
    for (const FBPJECompactGraph& Graph : Blueprint.Graphs)
    {
        TArray<TSharedPtr<FJsonValue>> NodeArray;
        NodeArray.Reserve(Graph.Nodes.Num());
        for (const FBPJECompactNode& Node : Graph.Nodes)
        {
            TArray<TSharedPtr<FJsonValue>> PinArray;
            PinArray.Reserve(Node.Pins.Num());
            for (const FBPJECompactPin& Pin : Node.Pins)
            {
                TArray<TSharedPtr<FJsonValue>> PinTuple;
                PinTuple.Add(Num(Pin.Direction));
                PinTuple.Add(Num(static_cast<int32>(Pin.PinType)));
                PinTuple.Add(Num(static_cast<int32>(Pin.ContainerType)));
                PinTuple.Add(Num(StringTable.Add(Pin.Name)));
                PinTuple.Add(Num(StringTable.Add(Pin.SubType)));
                PinTuple.Add(Num(StringTable.Add(Pin.DefaultValue)));
                PinTuple.Add(Num(Pin.PinFlags));
                PinArray.Add(Arr(PinTuple));
            }

            TArray<TSharedPtr<FJsonValue>> NodeTuple;
            NodeTuple.Add(Num(static_cast<int32>(Node.NodeType)));
            NodeTuple.Add(Num(StringTable.Add(Node.Title)));
            NodeTuple.Add(Num(StringTable.Add(Node.MemberParent)));
            NodeTuple.Add(Num(StringTable.Add(Node.MemberName)));
            NodeTuple.Add(Num(Node.NodeFlags));
            NodeTuple.Add(Arr(PinArray));
            NodeArray.Add(Arr(NodeTuple));
        }

        TArray<TSharedPtr<FJsonValue>> LinkArray;
        LinkArray.Reserve(Graph.Links.Num());
        for (const FBPJECompactLink& Link : Graph.Links)
        {
            TArray<TSharedPtr<FJsonValue>> LinkTuple;
            LinkTuple.Add(Num(Link.Kind));
            LinkTuple.Add(Num(Link.SourceNodeIndex));
            LinkTuple.Add(Num(Link.SourcePinIndex));
            LinkTuple.Add(Num(Link.TargetNodeIndex));
            LinkTuple.Add(Num(Link.TargetPinIndex));
            LinkArray.Add(Arr(LinkTuple));
        }

        TArray<TSharedPtr<FJsonValue>> GraphTuple;
        GraphTuple.Add(Num(StringTable.Add(Graph.Name)));
        GraphTuple.Add(Num(static_cast<int32>(Graph.GraphType)));
        GraphTuple.Add(Arr(NodeArray));
        GraphTuple.Add(Arr(LinkArray));
        GraphArray.Add(Arr(GraphTuple));
    }
    RootObject->SetArrayField(TEXT("g"), GraphArray);

    if (!Blueprint.Structs.IsEmpty() || !Blueprint.Enums.IsEmpty())
    {
        TArray<TSharedPtr<FJsonValue>> StructArray;
        StructArray.Reserve(Blueprint.Structs.Num());
        for (const FBPJEStruct& StructDef : Blueprint.Structs)
        {
            TArray<TSharedPtr<FJsonValue>> MemberArray;
            MemberArray.Reserve(StructDef.Members.Num());
            for (const FBPJEStructMember& Member : StructDef.Members)
            {
                uint8 MemberFlags = 0;
                if (Member.bIsArray)
                {
                    MemberFlags |= 1 << 0;
                }
                if (Member.bIsSet)
                {
                    MemberFlags |= 1 << 1;
                }
                if (Member.bIsMap)
                {
                    MemberFlags |= 1 << 2;
                }

                const int32 KeyTypeValue = Member.bIsMap ? static_cast<int32>(Member.KeyType) : 0;

                TArray<TSharedPtr<FJsonValue>> MemberTuple;
                MemberTuple.Add(Num(StringTable.Add(Member.Name)));
                MemberTuple.Add(Num(static_cast<int32>(Member.Type)));
                MemberTuple.Add(Num(StringTable.Add(Member.TypeName)));
                MemberTuple.Add(Num(MemberFlags));
                MemberTuple.Add(Num(KeyTypeValue));
                MemberTuple.Add(Num(StringTable.Add(Member.KeyTypeName)));
                MemberTuple.Add(Num(StringTable.Add(Member.DefaultValue)));
                MemberTuple.Add(Num(StringTable.Add(Member.Comment)));
                MemberArray.Add(Arr(MemberTuple));
            }

            TArray<TSharedPtr<FJsonValue>> StructTuple;
            StructTuple.Add(Num(StringTable.Add(StructDef.Name)));
            StructTuple.Add(Num(StringTable.Add(StructDef.Comment)));
            StructTuple.Add(Arr(MemberArray));
            StructArray.Add(Arr(StructTuple));
        }

        TArray<TSharedPtr<FJsonValue>> EnumArray;
        EnumArray.Reserve(Blueprint.Enums.Num());
        for (const FBPJEEnum& EnumDef : Blueprint.Enums)
        {
            TArray<TSharedPtr<FJsonValue>> ValueArray;
            ValueArray.Reserve(EnumDef.Values.Num());
            for (const FBPJEEnumValue& Value : EnumDef.Values)
            {
                TArray<TSharedPtr<FJsonValue>> ValueTuple;
                ValueTuple.Add(Num(StringTable.Add(Value.Name)));
                ValueTuple.Add(Num(StringTable.Add(Value.Comment)));
                ValueArray.Add(Arr(ValueTuple));
            }

            TArray<TSharedPtr<FJsonValue>> EnumTuple;
            EnumTuple.Add(Num(StringTable.Add(EnumDef.Name)));
            EnumTuple.Add(Num(StringTable.Add(EnumDef.Comment)));
            EnumTuple.Add(Arr(ValueArray));
            EnumArray.Add(Arr(EnumTuple));
        }

        TArray<TSharedPtr<FJsonValue>> TypesTuple;
        TypesTuple.Add(Arr(StructArray));
        TypesTuple.Add(Arr(EnumArray));
        RootObject->SetArrayField(TEXT("t"), TypesTuple);
    }

    if (!Blueprint.SkippedGraphs.IsEmpty())
    {
        TArray<TSharedPtr<FJsonValue>> SkippedArray;
        SkippedArray.Reserve(Blueprint.SkippedGraphs.Num());
        for (const FBPJESkippedGraphEntry& Entry : Blueprint.SkippedGraphs)
        {
            TArray<TSharedPtr<FJsonValue>> EntryTuple;
            EntryTuple.Add(Num(StringTable.Add(Entry.OwnerGraphName)));
            EntryTuple.Add(Num(StringTable.Add(Entry.SkippedGraphName)));
            EntryTuple.Add(Num(StringTable.Add(Entry.SkippedOwnerPath)));
            EntryTuple.Add(Num(StringTable.Add(Entry.Reason)));
            SkippedArray.Add(Arr(EntryTuple));
        }
        RootObject->SetArrayField(TEXT("x"), SkippedArray);
    }

    if (!Blueprint.SemanticIndex.Graphs.IsEmpty())
    {
        TSharedPtr<FJsonObject> SemanticObject = MakeShared<FJsonObject>();
        SemanticObject->SetNumberField(TEXT("schema"), Blueprint.SemanticIndex.SchemaVersion);

        TArray<TSharedPtr<FJsonValue>> SemanticGraphs;
        SemanticGraphs.Reserve(Blueprint.SemanticIndex.Graphs.Num());
        for (const FBPJESemanticGraph& Graph : Blueprint.SemanticIndex.Graphs)
        {
            TSharedPtr<FJsonObject> GraphObject = MakeShared<FJsonObject>();
            GraphObject->SetNumberField(TEXT("graph"), Graph.GraphIndex);
            GraphObject->SetStringField(TEXT("name"), Graph.Name);
            GraphObject->SetStringField(TEXT("kind"), Graph.Kind);
            GraphObject->SetArrayField(TEXT("entryNodes"), ToNumberArray(Graph.EntryNodes));

            TSharedPtr<FJsonObject> SummaryObject = MakeShared<FJsonObject>();
            SummaryObject->SetArrayField(TEXT("calls"), ToStringArray(Graph.Summary.Calls));
            SummaryObject->SetArrayField(TEXT("nativeCalls"), ToStringArray(Graph.Summary.NativeCalls));
            SummaryObject->SetArrayField(TEXT("latentTasks"), ToStringArray(Graph.Summary.LatentTasks));

            TSharedPtr<FJsonObject> RefsObject = MakeShared<FJsonObject>();
            RefsObject->SetArrayField(TEXT("classes"), ToStringArray(Graph.Summary.Refs.Classes));
            RefsObject->SetArrayField(TEXT("assets"), ToStringArray(Graph.Summary.Refs.Assets));
            RefsObject->SetArrayField(TEXT("tags"), ToStringArray(Graph.Summary.Refs.Tags));
            SummaryObject->SetObjectField(TEXT("refs"), RefsObject);
            GraphObject->SetObjectField(TEXT("summary"), SummaryObject);

            TArray<TSharedPtr<FJsonValue>> NodesArray;
            NodesArray.Reserve(Graph.Nodes.Num());
            for (const FBPJESemanticNode& Node : Graph.Nodes)
            {
                TSharedPtr<FJsonObject> NodeObject = MakeShared<FJsonObject>();
                NodeObject->SetNumberField(TEXT("id"), Node.Id);
                NodeObject->SetStringField(TEXT("kind"), Node.Kind);
                NodeObject->SetStringField(TEXT("title"), Node.Title);
                NodeObject->SetStringField(TEXT("memberParent"), Node.MemberParent);
                NodeObject->SetStringField(TEXT("memberName"), Node.MemberName);
                NodesArray.Add(MakeShared<FJsonValueObject>(NodeObject));
            }
            GraphObject->SetArrayField(TEXT("nodes"), NodesArray);

            TArray<TSharedPtr<FJsonValue>> ExecArray;
            ExecArray.Reserve(Graph.Exec.Num());
            for (const FBPJESemanticExecLink& Edge : Graph.Exec)
            {
                TSharedPtr<FJsonObject> ExecObject = MakeShared<FJsonObject>();
                ExecObject->SetNumberField(TEXT("from"), Edge.From);
                ExecObject->SetStringField(TEXT("fromPin"), Edge.FromPin);
                ExecObject->SetNumberField(TEXT("to"), Edge.To);
                ExecObject->SetStringField(TEXT("toPin"), Edge.ToPin);
                ExecArray.Add(MakeShared<FJsonValueObject>(ExecObject));
            }
            GraphObject->SetArrayField(TEXT("exec"), ExecArray);

            SemanticGraphs.Add(MakeShared<FJsonValueObject>(GraphObject));
        }

        SemanticObject->SetArrayField(TEXT("graphs"), SemanticGraphs);
        RootObject->SetObjectField(TEXT("i"), SemanticObject);
    }

    TArray<TSharedPtr<FJsonValue>> StringArray;
    const TArray<FString>& FinalStrings = StringTable.GetStrings();
    StringArray.Reserve(FinalStrings.Num());
    for (const FString& Value : FinalStrings)
    {
        StringArray.Add(MakeShared<FJsonValueString>(Value));
    }
    RootObject->SetArrayField(TEXT("s"), StringArray);

    FString JsonOutput;
    if (bPrettyPrint)
    {
        TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
            TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&JsonOutput);
        FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer);
    }
    else
    {
        TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
            TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&JsonOutput);
        FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer);
    }

    return JsonOutput;
}
