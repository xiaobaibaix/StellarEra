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
// Planet 是 **Private**：本模块的公开头不 include 它的头，完整类型只在 .cpp 里用
// （CosmosBody.h 里只对 AVoxelPlanetActor 做前置声明）。
//
// 受控实体（ASpaceshipPawn）不再被本模块直接引用：原先的
// StellarEraCosmos → StellarEraPawns 反向依赖（4 + 2 处 Cast<ASpaceshipPawn>）
// 已随 ICosmosBody 接口抽离还清，接口落在 Src/CosmosBody.h。现在
// Pawns → Cosmos 才是正确方向，Pawns 也不再出现在本模块的依赖列表里。
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
		});
	}
}
