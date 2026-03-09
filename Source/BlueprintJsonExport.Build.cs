using UnrealBuildTool;

public class BlueprintJsonExport : ModuleRules
{
    public BlueprintJsonExport(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "AssetRegistry",
                "BlueprintGraph",
                "Core",
                "CoreUObject",
                "Engine",
                "InputCore",
                "Json",
                "UnrealEd"
            }
        );
    }
}
