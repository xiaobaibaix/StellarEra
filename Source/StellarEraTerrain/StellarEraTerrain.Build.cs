// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

// 体素星球层：行星 Actor、生成器、地形模块与两种地形实现、行星面射线与网格、
// LOD 体素调用器。矿物与大气已分别拆到 StellarEraMinerals / StellarEraAtmosphere。
//
// Voxel 必须是 **Public** 依赖：本模块的 6 个公开头直接 include 插件头
// （AlwaysLocalVoxelInvokerComponent.h、VoxelPlanetActor.h、VoxelPlanetGenerator.h、
// VoxelPlanetNoiseTerrain.h、VoxelPlanetSphereTerrain.h、VoxelPlanetTerrainModule.h）
// 并继承插件类型（如 AVoxelPlanetActor : public AVoxelWorld），下游模块要用这些类型
// 就必须拿到插件的包含路径 —— 依赖声明为 Public 才会向下游传递。
public class StellarEraTerrain : ModuleRules
{
	public StellarEraTerrain(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "Voxel",
			// 地质场：本模块的公开头并不 include PlanetGeologyTypes.h，只**前置声明**
			// FPlanetGeologyField 并用 TSharedPtr<const FPlanetGeologyField> 持有
			// （VoxelPlanetActor.h、VoxelPlanetGenerator.h、VoxelPlanetNoiseTerrain.h、
			// VoxelPlanetTerrainModule.h），编译期不需要它的定义。仍声明为 Public 是刻意的：
			// 下游模块要用地质类型，得直接从这条依赖拿到包含路径。
			"StellarEraGeology",
		});

		// 编辑器里给每颗行星懒创建 / 加载 UVoxelWorldSaveObject 资产（VoxelPlanetActor.cpp
		// 的 EnsureSaveObject），创建后要通知资产注册表，否则内容浏览器里看不到。
		PrivateDependencyModuleNames.AddRange(new string[] { "AssetRegistry" });

		// 没有 UnrealEd 依赖：编辑器态工作走 AssetRegistry（存档资产的创建通知，
		// 见 VoxelPlanetActor.cpp 的 EnsureSaveObject）与 Engine 的运行时接口。
		// 大气材质重建（PlanetAtmosphereMaterialBuilder.cpp）已随大气整体迁到
		// StellarEraAtmosphere（Task 10）；它原先是本文件"不需要 UnrealEd"的论据之一，
		// 迁走后那条论据随文件一起退出本模块，现在只由上面这句承担。
		// 早先这里有 UnrealEd，是为了从活动视口取相机位置（GEditor / FEditorViewportClient）；
		// 相机位置改由材质的视图 uniform 提供后，那两个符号不再被引用。
		// 地质分支曾按旧代码加过编辑器态 UnrealEd 依赖，合并时以主线的新做法为准。
	}
}
