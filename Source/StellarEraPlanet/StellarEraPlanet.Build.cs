// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

// 体素星球层：星球 Actor、生成器、地形模块与两种地形实现。
//
// Voxel 必须是 **Public** 依赖：本模块的 5 个公开头直接 include 插件头并继承
// 插件类型（如 AVoxelPlanetActor : public AVoxelWorld），下游模块要用这些类型
// 就必须拿到插件的包含路径 —— 依赖声明为 Public 才会向下游传递。
public class StellarEraPlanet : ModuleRules
{
	public StellarEraPlanet(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// 地质场：本模块的公开头直接 include PlanetGeologyTypes.h，并在
		// VoxelPlanetTerrainModule.h 等公开结构体里暴露 FPlanetGeologyField，
		// 下游模块要用就必须拿到它的包含路径，所以是 Public。
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "Voxel",
			"StellarEraGeology",
		});

		// 编辑器里给每颗行星懒创建 / 加载 UVoxelWorldSaveObject 资产（VoxelPlanetActor.cpp
		// 的 EnsureSaveObject），创建后要通知资产注册表，否则内容浏览器里看不到。
		PrivateDependencyModuleNames.AddRange(new string[] { "AssetRegistry" });

		// 没有 UnrealEd 依赖：本模块的编辑器态工作只用到 Engine 的运行时接口。
		// 大气材质重建（PlanetAtmosphereMaterialBuilder.cpp）已随大气整体迁到
		// StellarEraAtmosphere（Task 10），原先"它只用 UMaterial / 表达式节点 /
		// SavePackage 接口、所以不需要 UnrealEd"这条理由也一并移出了本模块。
		// 早先这里有 UnrealEd，是为了从活动视口取相机位置（GEditor / FEditorViewportClient）；
		// 相机位置改由材质的视图 uniform 提供后，那两个符号不再被引用。
		// 地质分支曾按旧代码加过编辑器态 UnrealEd 依赖，合并时以主线的新做法为准。
	}
}
