// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

// 框架层：游戏框架（GameMode / PlayerController）。HUD 与星图已搬到 StellarEraUI。
//
// 本模块**保留原名 StellarEra**，因此它仍是 primary game module ——
// StellarEra.cpp 里的 IMPLEMENT_PRIMARY_GAME_MODULE 不动。
// 保留原名还有个副作用：AStellarEraGameMode / AStellarEraPlayerController 的
// /Script/ 路径不变，DefaultEngine.ini 与 UniverseMap.umap 里指向它们的引用
// 无需任何改动。Cosmos 三个类已搬到 StellarEraCosmos，旧路径由
// DefaultEngine.ini 的 [CoreRedirects] 兜底。
//
// 依赖方向（严格分层，无环）：
//     StellarEra ─→ StellarEraUI ─→ StellarEraCosmos ─→ StellarEraVehicle ─→ StellarEraPlanet
//          │            └──────→ StellarEraCore
//          └──────→ StellarEraCore
//
// StellarEraUI 是 **Public**：公开头 StellarEraPlayerController.h include 了
// StarMapUIInputs.h —— AStellarEraPlayerController 公开继承 IStarMapUIInputs，
// 所以 UI 的 include 路径必须向下游传播。写成 Private 的话本模块自己编得过，
// 但任何公开依赖 StellarEra 的模块 include 控制器头就会撞 C1083，而且报错
// 指向 UI 的文件而不是它自己的 Build.cs。
// 反向（UI → Game）禁止，需要控制器数据的地方走 IStarMapUIInputs。
//
// Core 是 **Public**：拆分前 CosmosSimulationSubsystem.h（按值持有 FNBodySimulator）
// 是本模块公开头的一部分；那部分已随 Cosmos 搬走，这里保留 Public 只是沿用既有
// 声明，不影响任何下游。
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
			// 公开头 StellarEraPlayerController.h include 了 StarMapUIInputs.h
			// （公开继承 IStarMapUIInputs），include 路径必须向下游传播。
			"StellarEraUI",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"StellarEraPlanet", // 行星 Actor / 地质场 / 纹理；只在 .cpp 里 include
			"StellarEraCore",
			"StellarEraVehicle",
			"StellarEraCharacter", // 角色；只在 .cpp 与测试里 include
			"StellarEraPlanetEdit", // 编辑组件 / 笔刷；只在 .cpp 与测试里 include
			"StellarEraCosmos", // N 体编排；公开头只在 .cpp 里 include
			"Voxel",           // 已无直接 include（星图代码搬到 StellarEraUI），待清理
			"InputCore",
			"EnhancedInput",   // 只有 StellarEraPlayerController.cpp 用到
		});
	}
}
