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

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "Voxel" });

		// 没有 UnrealEd 依赖：编辑器里的大气材质重建（PlanetAtmosphereMaterialBuilder.cpp）
		// 只用 Engine 模块的 UMaterial / 表达式节点 / SavePackage 接口。
		// 早先这里有 UnrealEd，是为了从活动视口取相机位置（GEditor / FEditorViewportClient）；
		// 相机位置改由材质的视图 uniform 提供后，那两个符号不再被引用。
	}
}
