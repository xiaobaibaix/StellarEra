// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

// 地质层：元素表、行星地质场、JSON 预设、噪声采样。
//
// Voxel 是 **Public**：PlanetGeologyField.h 在公开头里 include
// FastNoise/VoxelFastNoise.h（FVoxelFastNoise 按值持有）。
// Engine 是 **Public**：PlanetGeologyPreset.h 在公开头里继承 UDataAsset
// （include Engine/DataAsset.h），UDataAsset 来自 Engine 模块。
// CoreUObject 是 **Public**：本模块含一个 UDataAsset 子类（UPlanetGeologyPreset）
// 与两个 USTRUCT（FPlanetGeologyAppearance、FPlanetGeologyElementParams），
// 反射是真依赖，不再是「与其余模块保持一致」的摆设。
// Json 只在 .cpp 里用。
public class StellarEraGeology : ModuleRules
{
	public StellarEraGeology(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "Voxel",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Json",
		});
	}
}
