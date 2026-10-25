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
//
// StellarEraPlanet 是 **Private**：本模块没有任何公开头 include 行星模块的头，
// StellarEraPlayerController.h 里的行星类型一律前置声明 + TObjectPtr，用到完整类型的
// 地方都在 .cpp 里。行星模块的头因而不需要向下游传播。
// 之所以曾经写成 Public：早期公开头以成员类型引用了行星模块里的一个类型定义，
// 那处引用已随旧系统一并删除。现在公开头只以前置声明 + 指针接触行星模块的类型，
// 例如 AStellarEraPlayerController::GetActiveMineralView() 返回
// UPlanetMineralViewComponent*，完整类型只在 .cpp 里用到。
public class StellarEra : ModuleRules
{
	public StellarEra(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"StellarEraPlanet", // 行星 Actor / 地质场 / 纹理；只在 .cpp 里 include
			"StellarEraCore",
			"StellarEraVehicle",
			"Voxel",           // StarMapCameraPawn.cpp 的体素调用器（VoxelInvokerComponent.h）
			"InputCore",
			"EnhancedInput",   // 只有 StellarEraPlayerController.cpp 用到
		});
	}
}
