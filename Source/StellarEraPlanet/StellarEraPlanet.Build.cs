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
// 按目录分组，不按模块分组：
//   Public/Terrain      生成器、地形模块、射线、表面网格、生物群系、调用器
//   Public/Atmosphere   大气组件与参数
//   Public/Minerals     矿物表面、观察组件、观察 Actor、材质
//   Public/Edit         编辑组件、笔刷、涂抹、渐进编辑、网格几何
//   Private/Tests       本模块的全部自动化测试（*Tests.cpp 只能出现在这里）
//
// Voxel 必须是 **Public** 依赖：本模块多个公开头直接 include 插件头
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

		// UBT **不会**递归把 Public/ 的子目录加入搜索路径 —— 只加 Public/ 本身。
		// 所以下面四行必须显式写出来，否则 `#include "VoxelPlanetGenerator.h"`
		// 这类裸文件名 include 会直接 C1083。PublicIncludePaths 会向下游传递，
		// Pawns / Cosmos / UI / 主模块因此也能按裸名 include 这些头。
		PublicIncludePaths.AddRange(new string[]
		{
			Path.Combine(ModuleDirectory, "Public", "Terrain"),
			Path.Combine(ModuleDirectory, "Public", "Atmosphere"),
			Path.Combine(ModuleDirectory, "Public", "Minerals"),
			Path.Combine(ModuleDirectory, "Public", "Edit"),
		});

		// 纯实现头（PlanetProgressiveEditKernel.h）不进 Public，只在本模块内可见。
		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Private", "Edit"));

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "Voxel",

			// 地质场：本模块的公开头并不 include PlanetGeologyTypes.h，只**前置声明**
			// FPlanetGeologyField 并用 TSharedPtr<const FPlanetGeologyField> 持有。
			"StellarEraGeology",

			// IVoxelViewpointInvokers 在 Core/Public/Interfaces —— 实现它的是
			// Pawns 里的三个 Pawn，而本模块的 AlwaysLocalVoxelInvokerComponent
			// 提供配套的工具函数。
			"StellarEraCore",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			// 编辑器里给每颗行星懒创建 / 加载 UVoxelWorldSaveObject 资产
			// （VoxelPlanetActor.cpp 的 EnsureSaveObject），创建后要通知资产注册表，
			// 否则内容浏览器里看不到。
			"AssetRegistry",

			// 矿物等值面用到的几何库（原 StellarEraMinerals 的依赖）。
			"GeometryCore",
			"GeometryFramework",
		});
	}
}
