// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

// 天体层：行星与恒星两种天体的视觉与结构实现，共用体素、大气与地质管线。
//
// 目录按天体分工：Planet/Src 与 Star/Src 各放自己的业务代码，测试在各自的 Tests/，
// 两种天体共用的基类放 Common/。.h 与 .cpp 同目录并列。
//
// Voxel 必须是 **Public** 依赖：本模块多个公开头直接 include 插件头并继承插件类型
// （VoxelPlanetActor.h 的 AVoxelPlanetActor : public AVoxelWorld），下游模块要用
// 这些类型就必须拿到插件的包含路径。
public class StellarEraCelestial : ModuleRules
{
	public StellarEraCelestial(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// 模块根与所有子目录都进搜索路径。UBT 只认 Public/ Private/ Classes/ Internal
		// 这四个名字且默认不递归（BuildSettings V7 关闭了 bLegacyPublicIncludePaths），
		// 所以自己展开；新增子目录不需要改本文件。
		// 用 PublicIncludePaths：下游（Pawns / Cosmos / UI / 主模块）都按裸名 include 这些头。
		PublicIncludePaths.Add(ModuleDirectory);
		foreach (string SubDir in Directory.GetDirectories(ModuleDirectory, "*", SearchOption.AllDirectories))
		{
			PublicIncludePaths.Add(SubDir);
		}

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "Voxel",

			// GeometryCore 必须是 Public：公开头里直接暴露 UE::Geometry::FDynamicMesh3。
			// 声明成 Private 时本模块自己编得过，第一个跨模块消费者会找不到这个头。
			"GeometryCore",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			// 编辑器里给每颗行星懒创建 / 加载 UVoxelWorldSaveObject 资产，
			// 创建后要通知资产注册表，否则内容浏览器里看不到。
			"AssetRegistry",

			// 动态网格组件：只在 .cpp 里用，不需要向下游传递。
			"GeometryFramework",

			// 材质构建器收尾时要主动查一次着色器编译结果（GMaxRHIShaderPlatform）。
			"RHI",
		});

		if (Target.bBuildEditor)
		{
			// 编辑器世界的天光预览要取视口相机；地质查看器（Planet/Src/Geo/Viewer）
			// 全部包在 #if WITH_EDITOR 里，所以这些依赖只出现在编辑器目标。
			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"UnrealEd",
				"Slate", "SlateCore", "InputCore", "WorkspaceMenuStructure", "PropertyEditor",
			});
		}
	}
}
