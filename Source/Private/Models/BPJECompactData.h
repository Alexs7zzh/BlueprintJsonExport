#pragma once

#include "Models/BPJEData.h"
#include "Templates/Tuple.h"

struct FBPJECompactPin
{
    uint8 Direction = 0;
    EBPJEPinType PinType = EBPJEPinType::Wildcard;
    EBPJEPinContainerType ContainerType = EBPJEPinContainerType::None;
    FString Name;
    FString SubType;
    FString DefaultValue;
    uint8 PinFlags = 0;
};

struct FBPJECompactNode
{
    EBPJENodeType NodeType = EBPJENodeType::CallFunction;
    FString Title;
    FString MemberParent;
    FString MemberName;
    bool bMemberIsBlueprintDefined = false;
    uint8 NodeFlags = 0;
    TArray<FBPJECompactPin> Pins;
};

struct FBPJECompactLink
{
    uint8 Kind = 1;
    int32 SourceNodeIndex = INDEX_NONE;
    int32 SourcePinIndex = INDEX_NONE;
    int32 TargetNodeIndex = INDEX_NONE;
    int32 TargetPinIndex = INDEX_NONE;
};

struct FBPJECompactGraph
{
    FString Name;
    EBPJEGraphType GraphType = EBPJEGraphType::EventGraph;
    TArray<FBPJECompactNode> Nodes;
    TArray<FBPJECompactLink> Links;
};

struct FBPJESemanticRefs
{
    TArray<FString> Classes;
    TArray<FString> Assets;
    TArray<FString> Tags;
};

struct FBPJESemanticSummary
{
    TArray<FString> Calls;
    TArray<FString> NativeCalls;
    TArray<FString> LatentTasks;
    FBPJESemanticRefs Refs;
};

struct FBPJESemanticNode
{
    int32 Id = INDEX_NONE;
    FString Kind;
    FString Title;
    FString MemberParent;
    FString MemberName;
};

struct FBPJESemanticExecLink
{
    int32 From = INDEX_NONE;
    FString FromPin;
    int32 To = INDEX_NONE;
    FString ToPin;
};

struct FBPJESemanticGraph
{
    int32 GraphIndex = INDEX_NONE;
    FString Name;
    FString Kind;
    TArray<int32> EntryNodes;
    FBPJESemanticSummary Summary;
    TArray<FBPJESemanticNode> Nodes;
    TArray<FBPJESemanticExecLink> Exec;
};

struct FBPJESemanticIndex
{
    int32 SchemaVersion = 1;
    TArray<FBPJESemanticGraph> Graphs;
};

struct FBPJESkippedGraphEntry
{
    FString OwnerGraphName;
    FString SkippedGraphName;
    FString SkippedOwnerPath;
    FString Reason;
};

struct FBPJECompactMetadata
{
    FString BlueprintName;
    FString PackagePath;
    FString ObjectPath;
    FString GeneratedClass;
    EBPJEBlueprintType BlueprintType = EBPJEBlueprintType::Normal;
    int32 MaxDepth = 5;
    bool bTruncated = false;
};

struct FBPJECompactBlueprint
{
    FBPJECompactMetadata Metadata;
    TArray<FBPJECompactGraph> Graphs;
    TArray<FBPJEStruct> Structs;
    TArray<FBPJEEnum> Enums;
    TArray<FBPJESkippedGraphEntry> SkippedGraphs;
    FBPJESemanticIndex SemanticIndex;
};
