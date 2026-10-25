// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

// 矿物层：逐元素等值面构建、观察体 Actor、矿体材质。
//
// Geology 是 **Public**：PlanetMineralViewActor.h / PlanetMineralViewComponent.h
// 在公开头里 include PlanetGeologyTypes.h。
// Planet 只在 .cpp 里用它的**完整类型**：PlanetMineralViewComponent.cpp include 了
// VoxelPlanetActor.h。公开头 PlanetMineralViewComponent.h 里仍有 AVoxelPlanetActor
// 的前置声明、参数、返回值与 TWeakObjectPtr 成员，但都不需要完整定义。
// Atmosphere 也是 **Private**：同文件 include PlanetAtmosphereComponent.h 并
// FindComponentByClass 取壳，公开头不出现大气类型。
//
// Voxel 只有**间接**依赖，所以不列在这里：PlanetMineralViewComponent.cpp 访问
// AVoxelPlanetActor::VoxelSize 是字段偏移，不产生链接符号；需要的插件 include 路径
// 由 Terrain 的 **Public** Voxel 依赖传递下来。
//
// 没有 UnrealEd 依赖：PlanetMineralViewTests.cpp 只用 UEngine 的世界上下文接口
// （GetWorldContexts + EWorldType 过滤），不碰 UEditorEngine / GEditor 这类编辑器符号。
public class StellarEraMinerals : ModuleRules
{
	public StellarEraMinerals(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "StellarEraGeology",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"StellarEraTerrain",  // AVoxelPlanetActor（.cpp）
			"StellarEraAtmosphere", // UPlanetAtmosphereComponent（.cpp，FindComponentByClass）
			"GeometryCore",      // FMarchingCubes / FDynamicMesh3 / FMeshNormals
			"GeometryFramework", // UDynamicMeshComponent
		});
	}
}
