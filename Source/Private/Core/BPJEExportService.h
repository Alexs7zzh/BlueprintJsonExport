#pragma once

#include "AssetRegistry/AssetData.h"
#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "CoreTypes.h"

class UBlueprint;

struct FBPJEExportOptions
{
    FString OutputDir;
    int32 MaxDepth = 5;
    bool bPrettyPrint = false;
};

struct FBPJEExportResult
{
    FString BlueprintObjectPath;
    FString OutputFilePath;
    bool bSuccess = false;
    bool bTruncated = false;
    int32 ExportedGraphCount = 0;
    int32 SkippedGraphCount = 0;
    FString Error;
    TArray<FString> Warnings;
};

namespace BPJEExportService
{
    FString GetDefaultOutputDir();

    bool ExportBlueprintAsset(const FAssetData& BlueprintAsset, const FBPJEExportOptions& Options, FBPJEExportResult& OutResult);
    bool ExportBlueprintObjectPath(const FString& BlueprintObjectPath, const FBPJEExportOptions& Options, FBPJEExportResult& OutResult);

    bool ExportBlueprint(UBlueprint* Blueprint, const FBPJEExportOptions& Options, FBPJEExportResult& OutResult);

    void CollectBlueprintAssets(const TArray<FString>& ExplicitBlueprints, const TArray<FString>& Folders, TArray<FAssetData>& OutAssets, TArray<FString>& OutWarnings);
}
