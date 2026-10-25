// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

// 行星编辑层：编辑组件、笔刷几何、笔刷 → 体素工具的适配。
//
// Planet 与 Voxel 是 **Private**：公开头只用前置声明接触行星类型，
// 完整定义（VoxelPlanetActor.h、VoxelIntBox.h、VoxelTools/*）只在 .cpp 里 include。
public class StellarEraPlanetEdit : ModuleRules
{
	public StellarEraPlanetEdit(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"StellarEraPlanet",
			"Voxel",
		});
	}
}
