// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

// 万有引力层：N 体编排（组件 + 世界子系统）与轨迹可视化。
//
// Core 是 **Public**：CosmosSimulationSubsystem.h 按值持有 FNBodySimulator。
// Terrain / Spaceship 是 **Private**：本模块的公开头不 include 它们的头，
// 完整类型只在 .cpp 里用（公开头里这两个类只出现在注释中，连前置声明都没有）。
// 本模块只用到这两者：Terrain 提供行星 Actor，Spaceship 提供受控天体 ASpaceshipPawn
// （子系统每帧下来读它的推力与阻尼）。Character 不需要。
public class StellarEraCosmos : ModuleRules
{
	public StellarEraCosmos(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "StellarEraCore",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"StellarEraTerrain",
			"StellarEraSpaceship",
		});
	}
}
