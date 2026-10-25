// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

// 角色与载具层：行星角色、飞船 Pawn。
//
// Planet 与 Voxel 放 **Private** 即可：本模块的公开头只用前向声明持有
// TObjectPtr<AVoxelPlanetActor> / TObjectPtr<UVoxelSimpleInvokerComponent>，
// 完整定义只在 .cpp 里用到（VoxelPlanetActor.h、VoxelComponents/VoxelInvokerComponent.h）。
public class StellarEraVehicle : ModuleRules
{
	public StellarEraVehicle(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });

		PrivateDependencyModuleNames.AddRange(new string[] { "StellarEraPlanet", "Voxel", "StellarEraPlanetEdit" });
	}
}
