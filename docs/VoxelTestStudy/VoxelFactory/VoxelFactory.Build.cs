// VoxelFactory 模块构建规则。

using System.IO;
using UnrealBuildTool;

public class VoxelFactory : ModuleRules
{
    public VoxelFactory(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        bLegacyPublicIncludePaths = false;

#if UE_5_5_OR_LATER
        CppStandard = CppStandardVersion.Cpp20;
#else
        CppStandard = CppStandardVersion.Cpp17;
#endif

        PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Public"));
        PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Private"));

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                // 体素星球模块 —— 挖掘机需要 AVoxelPlanetActor 和它的 VoxelWorld
                "VoxelPlanet",
                // N 体宇宙模块 —— 需要 ACosmosBodyActor 做锚定
                "VoxelCosmos",

                "Core",
                "CoreUObject",
                "Engine"
            }
        );
    }
}
