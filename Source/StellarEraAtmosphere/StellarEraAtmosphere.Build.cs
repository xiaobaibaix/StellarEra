// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

// 大气层：球外可见的大气壳、参数推导、壳材质。
//
// Planet 是 **Private**：PlanetAtmosphereComponent.cpp 在 .cpp 里
// Cast<AVoxelPlanetActor>(GetOwner()) 取表面半径，公开头不 include 行星模块。
public class StellarEraAtmosphere : ModuleRules
{
	public StellarEraAtmosphere(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"StellarEraPlanet",
		});
	}
}
