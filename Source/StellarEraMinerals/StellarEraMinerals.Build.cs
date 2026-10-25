// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

// 矿物层：逐元素等值面构建、观察体 Actor、矿体材质。
//
// Geology 是 **Public**：PlanetMineralViewActor.h / PlanetMineralViewComponent.h
// 在公开头里 include PlanetGeologyTypes.h。
// Planet 只在 .cpp 里用：PlanetMineralViewComponent.cpp include 了 VoxelPlanetActor.h，
// 还 include 了仍住在 StellarEraPlanet 公开目录里的 PlanetAtmosphereComponent.h。
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
			"StellarEraPlanet",  // AVoxelPlanetActor（.cpp）、PlanetAtmosphereComponent.h（.cpp）
			"GeometryCore",      // FMarchingCubes / FDynamicMesh3 / FMeshNormals
			"GeometryFramework", // UDynamicMeshComponent
		});
	}
}
