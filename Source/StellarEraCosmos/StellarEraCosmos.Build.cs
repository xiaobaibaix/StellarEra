// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

// 万有引力层：N 体编排（组件 + 世界子系统）与轨迹可视化。
//
// Core 是 **Public**：CosmosSimulationSubsystem.h 按值持有 FNBodySimulator。
// Planet / Vehicle 是 **Private**：公开头只用前置声明接触它们。
// Vehicle 这两行是临时依赖，Task 6 / 7 会分别换成 Character / Spaceship。
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
			"StellarEraPlanet",
			"StellarEraVehicle",
		});
	}
}
