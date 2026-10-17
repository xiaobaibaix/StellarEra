// VoxelPlanet 模块构建规则。
//
// 这个文件告诉 UBT（UnrealBuild Tool）三件事：
//   1. 编译时能引用哪些模块（下面那个 Voxel 是关键）
//   2. 头文件搜索路径在哪
//   3. 用哪个 C++ 标准
//
// 少了Voxel 依赖，编译会报「找不到 VoxelGenerator.h」或一堆 undefined symbol。

using System.IO;
using UnrealBuildTool;

public class VoxelPlanet : ModuleRules
{
    public VoxelPlanet(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        bLegacyPublicIncludePaths = false;

        // 按引擎版本选择 C++ 标准（与插件官方模块保持一致）
#if UE_5_5_OR_LATER
        CppStandard = CppStandardVersion.Cpp20;
#else
        CppStandard = CppStandardVersion.Cpp17;
#endif

        // 让 #include "VoxelPlanetGenerator.h" 这种写法能找到文件
        PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Public"));
        PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Private"));

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                // Voxel Plugin 的运行时模块 —— 本模块所有体素类型都来自这里
                "Voxel",
                "Core",
                "CoreUObject",
                "Engine"
            }
        );
    }
}
