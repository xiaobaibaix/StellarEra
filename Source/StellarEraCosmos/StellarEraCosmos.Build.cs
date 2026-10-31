// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

// 万有引力层：N 体积分、天体类型、编排（组件 + 世界子系统）与轨迹可视化。
// .h 与 .cpp 同目录并列在 Src/ 下，测试在 Src/Tests。
//
// 本模块自带 FNBodySimulator（原 StellarEraCore 的主体）与 CosmosTypes.h。
// Core 当年单独成模块只是因为「纯计算不该混进 Actor」，但它总共 2687 行里
// 2411 行就是这一个求解器，剩下一个枚举头和一个接口头 —— 为它维持一个
// 模块的代价（依赖声明、模块入口、Target 登记）大于收益，故并入本模块。
//
// Planet / Pawns 是 **Private**：本模块的公开头不 include 它们的头，
// 完整类型只在 .cpp 里用（公开头里这几个类只出现在注释中，连前置声明都没有）。
// 本模块只用到这两者：Planet 提供行星 Actor，Pawns 提供受控天体 ASpaceshipPawn
// （子系统每帧下来读它的推力与阻尼）。
//
// 注意 StellarEraCosmos → StellarEraPawns 这条依赖是**模拟层反向依赖受控实体**，
// 属于已知的债：CosmosSimulationSubsystem.cpp 里有 4 处 Cast<ASpaceshipPawn>。
// 解法是定义一个 ICosmosBody 接口，让模拟层只认接口不认具体 Pawn。
// 但接口没有天然的落脚点：放本模块则 Pawns 反过来要依赖 Cosmos，立刻成环。
// Core 拆掉后这个位置只能新开，所以这条债与本次合并无关，单独安排。
public class StellarEraCosmos : ModuleRules
{
	public StellarEraCosmos(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Src/ 与所有子目录进搜索路径，See StellarEraPlanet.Build.cs 的说明。
		// 模块入口 StellarEraCosmos.h/.cpp 与 Build.cs 并列在模块根，不放进 Src/。
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
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"StellarEraPlanet",
			"StellarEraPawns",
		});
	}
}
