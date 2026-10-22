// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

// 框架层：N 体编排（组件 + 世界子系统）与游戏框架（GameMode / PlayerController）。
//
// 本模块**保留原名 StellarEra**，因此它仍是 primary game module ——
// StellarEra.cpp 里的 IMPLEMENT_PRIMARY_GAME_MODULE 不动。
// 保留原名还有个副作用：AStellarEraGameMode / AStellarEraPlayerController /
// UCosmosOrbitComponent / UCosmosSimulationSubsystem 的 /Script/ 路径全都不变，
// DefaultEngine.ini 与 UniverseMap.umap 里指向它们的引用无需任何改动。
//
// 依赖方向（严格分层，无环）：
//     StellarEra ─→ StellarEraVehicle ─→ StellarEraPlanet
//          └──────→ StellarEraCore
//
// Core 必须是 **Public**：CosmosSimulationSubsystem.h 按值持有 FNBodySimulator，
// 而 NBodySimulator.h 在 StellarEraCore 里，该头是本模块公开头的一部分。
public class StellarEra : ModuleRules
{
	public StellarEra(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"StellarEraCore",
			"StellarEraPlanet",
			"StellarEraVehicle",
			"Voxel",           // StarMapCameraPawn.cpp 的体素调用器（VoxelInvokerComponent.h）
			"InputCore",
			"EnhancedInput",   // 只有 StellarEraPlayerController.cpp 用到
		});
	}
}
