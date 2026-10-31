// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

// 装配层：游戏框架（GameMode / PlayerController）。HUD 与星图在 StellarEraUI。
// .h 与 .cpp 同目录并列在 Src/ 下，跨模块集成测试在 Src/Tests。
//
// 本模块**保留原名 StellarEra**，因此它仍是 primary game module ——
// StellarEra.cpp 里的 IMPLEMENT_PRIMARY_GAME_MODULE 不动。
// 保留原名还有个副作用：AStellarEraGameMode / AStellarEraPlayerController 的
// /Script/ 路径不变，DefaultEngine.ini 与 UniverseMap.umap 里指向它们的引用
// 无需任何改动。
//
// 依赖方向（严格分层，无环）。五层，层号即最大依赖深度：
//
//   L5 StellarEra         ─Public→ UI          ─Private→ Planet / Pawns / Cosmos
//   L4 StellarEraUI       ─Public→ Planet      ─Private→ Pawns / Cosmos
//   L3 StellarEraCosmos   ─Private→ Planet / Pawns
//   L2 StellarEraPawns    ─Public→ Planet
//   L1 StellarEraPlanet   无项目依赖（只依赖引擎与 Voxel 插件）
//
// 两个 Public 依赖都是被 IVoxelViewpointInvokers 逼出来的：UI 的星图相机与
// Pawns 的两个 Pawn 都在**公开头**里继承它，而它住在 Planet/Src/Invokers/。
// 其余跨模块引用一律走 Private（完整类型只在 .cpp 里用）。
//
// StellarEraUI 是 **Public**：公开头 StellarEraPlayerController.h include 了
// StarMapUIInputs.h —— AStellarEraPlayerController 公开继承 IStarMapUIInputs，
// 所以 UI 的 include 路径必须向下游传播。写成 Private 的话本模块自己编得过，
// 但任何公开依赖 StellarEra 的模块 include 控制器头就会撞 C1083，而且报错
// 指向 UI 的文件而不是它自己的 Build.cs。
// 反向（UI → Game）禁止，需要控制器数据的地方走 IStarMapUIInputs。
//
// StellarEraPlanet 是 **Private**：本模块没有任何公开头 include 行星模块的头，
// StellarEraPlayerController.h 里的行星类型一律前置声明 + TObjectPtr，用到完整类型的
// 地方都在 .cpp 里。行星模块的头因而不需要向下游传播。
public class StellarEra : ModuleRules
{
	public StellarEra(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Src/ 与所有子目录进搜索路径，说明见 StellarEraPlanet.Build.cs。
		// 模块入口 StellarEra.h/.cpp 与 Build.cs 并列在模块根，不放进 Src/。
		PublicIncludePaths.Add(ModuleDirectory);

		string SrcDir = Path.Combine(ModuleDirectory, "Src");
		PublicIncludePaths.Add(SrcDir);
		foreach (string SubDir in Directory.GetDirectories(SrcDir, "*", SearchOption.AllDirectories))
		{
			PublicIncludePaths.Add(SubDir);
		}

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine",
			// 公开头 StellarEraPlayerController.h include 了 StarMapUIInputs.h
			// （公开继承 IStarMapUIInputs），include 路径必须向下游传播。
			"StellarEraUI",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			// 行星：Actor / 表面网格 / 编辑组件 / 笔刷 / 矿物观察体 / 大气，
			// 以及 IVoxelViewpointInvokers（控制器在 OnPossess 里逐个调用，
			// 该接口住在 Planet/Src/Invokers/）。
			// 全部只在 .cpp 与测试里 include，公开头不出现。
			"StellarEraPlanet",

			// 受控实体：角色 / 飞船 / 星图相机。换视点时控制器逐个 possess。
			"StellarEraPawns",

			// N 体积分与编排（求解器 / 子系统 / 轨道组件 / 轨迹组件）；
			// 只在 .cpp 与测试里 include。原 StellarEraCore 已并入本模块。
			"StellarEraCosmos",

			// 视点门控的测试（VoxelViewpointInvokerTests.cpp）直接摸插件的
			// UVoxelInvokerComponentBase 与 FVoxelMessages，链接期需要本模块。
			"Voxel",
			"InputCore",
			"EnhancedInput",   // 只有 StellarEraPlayerController.cpp 用到
		});
	}
}
