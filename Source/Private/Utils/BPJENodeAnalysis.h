#pragma once

#include "Models/BPJEData.h"

class UEdGraph;
class UK2Node;
class UEnum;
class UScriptStruct;

struct FBPJENodeAnalysisResult
{
    EBPJENodeType NodeType = EBPJENodeType::CallFunction;
    FString MemberParent;
    FString MemberName;
    bool bMemberIsBlueprintDefined = false;
    UEdGraph* ChildGraph = nullptr;
    UScriptStruct* StructType = nullptr;
    UEnum* EnumType = nullptr;
};

FBPJENodeAnalysisResult AnalyzeNode(UK2Node* Node);
