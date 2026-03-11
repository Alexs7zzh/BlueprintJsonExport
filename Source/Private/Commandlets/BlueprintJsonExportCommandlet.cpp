#include "Commandlets/BlueprintJsonExportCommandlet.h"
#include "Core/BPJEExportService.h"
#include "LogBlueprintJsonExport.h"
#include "Misc/Parse.h"

UBlueprintJsonExportCommandlet::UBlueprintJsonExportCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
	ShowErrorCount = true;
	HelpDescription = TEXT("Export Blueprint assets into compact AI-only schema v1 JSON files with optional semantic index.");
	HelpUsage = TEXT("-run=BlueprintJsonExport -Blueprints=/Game/BP_A.BP_A+/Game/BP_B.BP_B -Folder=/Game/MyFolder -OutputDir=Saved/BlueprintExports -Depth=5 -Pretty=false");
}

int32 UBlueprintJsonExportCommandlet::Main(const FString& Params)
{
	FString BlueprintsValue;
	FString FolderValue;
	FString OutputDir;
	int32 Depth = 5;
	bool bPrettyPrint = false;

	FParse::Value(*Params, TEXT("Blueprints="), BlueprintsValue);
	FParse::Value(*Params, TEXT("Folder="), FolderValue);
	FParse::Value(*Params, TEXT("OutputDir="), OutputDir);
	FParse::Value(*Params, TEXT("Depth="), Depth);
	FParse::Bool(*Params, TEXT("Pretty="), bPrettyPrint);

	if (Depth < -1)
	{
		Depth = -1;
	}

	TArray<FString> BlueprintInputs;
	TArray<FString> FolderInputs;
	const auto SplitPlusSeparated = [](const FString& Value, TArray<FString>& OutValues)
	{
		TArray<FString> Parts;
		Value.ParseIntoArray(Parts, TEXT("+"), true);
		for (FString& Part : Parts)
		{
			Part.TrimStartAndEndInline();
			if (!Part.IsEmpty())
			{
				OutValues.Add(Part);
			}
		}
	};

	SplitPlusSeparated(BlueprintsValue, BlueprintInputs);
	SplitPlusSeparated(FolderValue, FolderInputs);

	if (BlueprintInputs.IsEmpty() && FolderInputs.IsEmpty())
	{
		UE_LOG(LogBlueprintJsonExport, Error,
			TEXT("No inputs provided. Use -Blueprints=<objPath+objPath> and/or -Folder=<packagePath+packagePath>."));
		return 1;
	}

	FBPJEExportOptions Options;
	Options.OutputDir = OutputDir;
	Options.MaxDepth = Depth;
	Options.bPrettyPrint = bPrettyPrint;

	TArray<FAssetData> AssetsToExport;
	TArray<FString> DiscoveryWarnings;
	BPJEExportService::CollectBlueprintAssets(BlueprintInputs, FolderInputs, AssetsToExport, DiscoveryWarnings);

	for (const FString& Warning : DiscoveryWarnings)
	{
		UE_LOG(LogBlueprintJsonExport, Warning, TEXT("%s"), *Warning);
	}

	if (AssetsToExport.IsEmpty())
	{
		UE_LOG(LogBlueprintJsonExport, Error, TEXT("No Blueprint assets resolved from the provided inputs."));
		return 1;
	}

	UE_LOG(LogBlueprintJsonExport, Display, TEXT("Exporting %d Blueprint asset(s)..."), AssetsToExport.Num());

	int32 SuccessCount = 0;
	int32 FailureCount = 0;

	for (const FAssetData& Asset : AssetsToExport)
	{
		FBPJEExportResult Result;
		const bool bExportSucceeded = BPJEExportService::ExportBlueprintAsset(Asset, Options, Result);

		for (const FString& Warning : Result.Warnings)
		{
			UE_LOG(LogBlueprintJsonExport, Warning, TEXT("%s"), *Warning);
		}

		if (bExportSucceeded)
		{
			++SuccessCount;
			UE_LOG(LogBlueprintJsonExport, Display, TEXT("Exported %s -> %s"), *Asset.GetObjectPathString(), *Result.OutputFilePath);
		}
		else
		{
			++FailureCount;
			UE_LOG(LogBlueprintJsonExport, Error, TEXT("Failed to export %s: %s"), *Asset.GetObjectPathString(), *Result.Error);
		}
	}

	UE_LOG(LogBlueprintJsonExport, Display, TEXT("BlueprintJsonExport completed. Success: %d, Failed: %d"), SuccessCount, FailureCount);

	return FailureCount == 0 ? 0 : 1;
}
