// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

// 地质层：元素表、行星地质场、JSON 预设、噪声采样。
//
// Voxel 是 **Public**：PlanetGeologyField.h 在公开头里 include
// FastNoise/VoxelFastNoise.h（FVoxelFastNoise 按值持有）。
// CoreUObject 是 **Public**：本模块其实没有 UCLASS / USTRUCT / UENUM ——
// PlanetGeologyTypes.h 里的值类型是刻意不进反射的裸 struct，公开头只需 CoreMinimal。
// 仍与 Core 一起列为 Public 是为了和其余模块的 Core / CoreUObject 依赖保持一致；
// 要收紧的话可以先试 Private。
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
