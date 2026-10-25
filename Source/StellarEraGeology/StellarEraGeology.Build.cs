// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

// 地质层：元素表、行星地质场、JSON 预设、噪声采样。
//
// Voxel 是 **Public**：PlanetGeologyField.h 在公开头里 include
// FastNoise/VoxelFastNoise.h（FVoxelFastNoise 按值持有）。
// Json 只在 .cpp 里用。
public class StellarEraGeology : ModuleRules
{
	public StellarEraGeology(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Voxel",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Json",
		});
	}
}
