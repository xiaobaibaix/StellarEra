// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

// 受控实体层：玩家能 possess 的 Pawn —— 行星角色、飞船。
//
// 由原来的 StellarEraCharacter / StellarEraSpaceship 合并而来。
// 它们的共同点是「被 AStellarEraPlayerController 通过 Possess 切换」，
// 且都实现 Core/Src/Interfaces/VoxelViewpointInvokers.h 里的
// IVoxelViewpointInvokers（换视点时要门控各自的体素调用器）。
// 把两者放在一起，换视点的逻辑才有唯一的归属地。
//
// 星图相机**不在**这里：它调 UCosmosSimulationSubsystem，而 Cosmos 内部有
// 4 处 Cast<ASpaceshipPawn>，搬过来就是 Pawns → Cosmos → Pawns。它留在 UI。
//
// 目录按功能分组，.h 与 .cpp 同目录并列：
//   Src/Character   角色、动画实例、移动组件
//   Src/Spaceship   飞船
//   Src/Tests       本模块全部自动化测试
public class StellarEraPawns : ModuleRules
{
	public StellarEraPawns(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// 同 StellarEraPlanet：Src/ 与所有子目录都进搜索路径。
		// 不这样写，`#include "PlanetCharacter.h"` 会 C1083 —— 主模块和 UI
		// 都是按裸名 include 这些头的，全项目一贯如此。
		// 模块入口 StellarEraPawns.h/.cpp 与 Build.cs 并列在模块根，不放进 Src/。
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

			// 两个 Pawn 的头都继承 IVoxelViewpointInvokers，
			// 并且直接持有插件的 UAlwaysLocalVoxelInvokerWithPredictionComponent，
			// 所以插件头必须对下游可见 —— Voxel 是 Public 依赖。
			"Voxel",

			// 角色贴地、飞船大气阻力，都直接调行星模块的接口。
			// IVoxelViewpointInvokers 也在本模块的公开头里用（两个 Pawn 继承它），
			// 该接口随体素接线层住在 Planet/Src/Invokers/ —— 所以是 Public 依赖，
			// 下游 include 本模块的 Pawn 头时能找到它。（原 StellarEraCore 已拆除。）
			"StellarEraPlanet",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			// 角色 / 飞船自带输入绑定（SetupPlayerInputComponent）：从控制器给的
			// FStellarEraInputActions 里取动作，再绑到自己的回调上。
			// 只有 .cpp 用到 EnhancedInput 的类型，公开头只前置声明 UInputAction，
			// 所以是私有依赖。
			"EnhancedInput",

			// EKeys（左 Ctrl 粗调笔刷）。EKeys 的静态成员是 dllimport，不链
			// InputCore 会报 LNK2019 —— 以前这段代码在主模块，那边本来就有这条依赖。
			"InputCore",
		});
	}
}
