// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

// 世界层：一颗行星的全部内容 —— 地形生成、大气、矿物、编辑。
//
// 由原来的 StellarEraTerrain / StellarEraAtmosphere / StellarEraMinerals /
// StellarEraPlanetEdit 四个模块合并而来。合并的理由是它们共享同一份体素密度场：
// 编辑内核直接写它，地形生成直接写它，矿物与大气按它取采样点。拆成四个模块时
// 每一次跨模块调用都得把内部实现提成 Public，反而比合在一起更难约束。
//
// 目录按功能分组，.h 与 .cpp 同目录并列：
//   Src/Terrain      生成器、地形模块、射线、表面网格、生物群系、调用器
//   Src/Atmosphere   大气组件与参数
//   Src/Minerals     矿物域四层：Geology（元素表/地质场/预设）、
//                    Distributions（矿脉形态）、Surface（表面与材质）、View（观测）
//   Src/Edit         编辑组件、笔刷、涂抹、渐进编辑、网格几何
//   Src/Tests        本模块的全部自动化测试（*Tests.cpp 只能出现在这里）
//
// Voxel 必须是 **Public** 依赖：本模块多个头直接 include 插件头
// （VoxelPlanetActor.h、VoxelPlanetGenerator.h、VoxelPlanetNoiseTerrain.h、
// VoxelPlanetSphereTerrain.h、VoxelPlanetTerrainModule.h、
// AlwaysLocalVoxelInvokerComponent.h）并继承插件类型
// （AVoxelPlanetActor : public AVoxelWorld），下游模块要用这些类型
// 就必须拿到插件的包含路径 —— 依赖声明为 Public 才会向下游传递。
public class StellarEraPlanet : ModuleRules
{
	public StellarEraPlanet(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Src/ 与它的每个子目录都要进搜索路径。
		// UBT 只认 Public/ Private/ Classes/ Internal 这四个名字，且默认不递归
		// （BuildSettings V7 关闭了 bLegacyPublicIncludePaths）。这里自己展开，
		// 新增功能子目录不需要改本文件。
		// 用 PublicIncludePaths：下游（Pawns / Cosmos / UI / 主模块）都是按裸名
		// include 这些头的，路径必须向下传递。
		// 模块入口 StellarEraPlanet.h/.cpp 与 Build.cs 并列在模块根，不放进 Src/。
		// 根目录不是 UBT 的默认搜索路径，显式加上，入口 cpp 才能按裸名 include 自己的头。
		PublicIncludePaths.Add(ModuleDirectory);

		string SrcDir = Path.Combine(ModuleDirectory, "Src");
		PublicIncludePaths.Add(SrcDir);
		foreach (string SubDir in Directory.GetDirectories(SrcDir, "*", SearchOption.AllDirectories))
		{
			PublicIncludePaths.Add(SubDir);
		}

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "Voxel",

			// IVoxelViewpointInvokers 就在本模块的 Src/Invokers/ —— 实现它的是
			// Pawns 的三个 Pawn 与 UI 的星图相机，它们都已 Public 依赖本模块，
			// 而本模块的 AlwaysLocalVoxelInvokerComponent 提供配套的工具函数。
			// （原 StellarEraCore 在 2026-10 拆掉，接口随体素接线层落到这里。）

			// GeometryCore 必须是 Public：PlanetWaterSurfaceBuilder.h 在公开 include
			// 路径上，API 里直接暴露 UE::Geometry::FDynamicMesh3。声明成 Private 时
			// 本模块自己编得过，第一个跨模块消费者会找不到这个头。
			"GeometryCore",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			// 编辑器里给每颗行星懒创建 / 加载 UVoxelWorldSaveObject 资产
			// （VoxelPlanetActor.cpp 的 EnsureSaveObject），创建后要通知资产注册表，
			// 否则内容浏览器里看不到。
			"AssetRegistry",

			// 矿物等值面用到的动态网格组件（原 StellarEraMinerals 的依赖）。
			// 只在 .cpp 里用，不需要向下游传递。
			"GeometryFramework",
		});

		if (Target.bBuildEditor)
		{
			// 编辑器世界的天光预览要取视口相机（GEditor / FLevelEditorViewportClient）。
			// 只在编辑器目标里编，打包时不会有这条依赖。
			PrivateDependencyModuleNames.Add("UnrealEd");
		}
	}
}
