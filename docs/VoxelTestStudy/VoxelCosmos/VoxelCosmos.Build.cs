// VoxelCosmos 模块构建规则。

using System.IO;
using UnrealBuildTool;

public class VoxelCosmos : ModuleRules
{
    public VoxelCosmos(ReadOnlyTargetRules Target) : base(Target)
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
                //体素星球模块。与之解耦，可按需移除。
                "VoxelPlanet",

                "Core",
                "CoreUObject",
                "Engine",
                "InputCore",

                // 相机与输入
                "Camera",
                "PhysicsCore"
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "RenderCore",
                "RHI"
            }
        );
    }
}
