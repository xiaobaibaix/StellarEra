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
//   L5 StellarEra         ─Public→ UI / Pawns  ─Private→ Planet / Cosmos
//   L4 StellarEraUI       ─Public→ Planet      ─Private→ Pawns / Cosmos
//   L3 StellarEraPawns    ─Public→ Cosmos / Planet / Voxel
//   L2 StellarEraCosmos   ─Private→ Planet
//   L1 StellarEraPlanet   无项目依赖（只依赖引擎与 Voxel 插件）
//
// **Public 与 Private 的分界只有一条：公开头里出没出现那个模块的类型。**
// 出现了就必须 Public —— 否则本模块自己编得过，但任何公开依赖 StellarEra 的模块
// include 控制器头就会撞 C1083，而且报错指向被 include 的文件而不是它自己的
// Build.cs，极难查。
//
// StellarEraUI 是 **Public**：StellarEraPlayerController.h include 了
// StarMapUIInputs.h —— 控制器公开继承 IStarMapUIInputs。
// 反向（UI → Game）禁止，需要控制器数据的地方走 IStarMapUIInputs。
//
// StellarEraPawns 是 **Public**：同一个头还 include 了 StellarEraInputActions.h
// （IStellarEraInputSource，控制器也公开继承）与 StarMapCameraInputComponent.h
// （IStarMapLookContext，控制器同样要实现）。
//
// StellarEraPlanet / StellarEraCosmos 是 **Private**：本模块没有任何公开头
// include 它们的头；控制器头里的行星与天体类型一律前置声明 + TObjectPtr，
// 用到完整类型的地方都在 .cpp 里。
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
			// 公开头里出现的：StarMapUIInputs.h（公开继承 IStarMapUIInputs）、
			// StellarEraInputActions.h（公开继承 IStellarEraInputSource）、
			// StarMapCameraInputComponent.h（实现 IStarMapLookContext）。
			"StellarEraUI",
			"StellarEraPawns",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			// 行星：Actor / 表面网格 / 编辑组件 / 笔刷 / 矿物观察体 / 大气，
			// 以及 IVoxelViewpointInvokers（控制器在 OnPossess 里逐个调用，
			// 该接口住在 Planet/Src/Invokers/）。
			// 只在 .cpp 与测试里 include，公开头不出现。
			"StellarEraPlanet",

			// N 体积分与编排（求解器 / 子系统 / 轨道组件 / 轨迹组件）；
			// 只在 .cpp 与测试里 include。原 StellarEraCore 已并入本模块。
			"StellarEraCosmos",

			// 视点门控的测试（VoxelViewpointInvokerTests.cpp）直接摸插件的
			// UVoxelInvokerComponentBase 与 FVoxelMessages，链接期需要本模块。
			"Voxel",
			"InputCore",
			// 控制器 + StarMap 接线测试（StarMapInputWiringTests.cpp 直接构造
			// UEnhancedInputComponent 并读 GetActionEventBindings）。
			"EnhancedInput",
		});
	}
}
