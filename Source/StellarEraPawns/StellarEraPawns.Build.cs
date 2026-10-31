// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

// 受控实体层：玩家能 possess 的三个 Pawn —— 行星角色、飞船、星图相机。
//
// 由原来的 StellarEraCharacter / StellarEraSpaceship 合并而来，
// 并从 StellarEraUI 收编了 AStarMapCameraPawn。
//
// 它们的共同点是「被 AStellarEraPlayerController 通过 Possess 切换」，
// 且都实现 Core/Public/Interfaces/VoxelViewpointInvokers.h 里的
// IVoxelViewpointInvokers（换视点时要门控各自的体素调用器）。
// 把三者放在一起，换视点的逻辑才有唯一的归属地。
//
// 按目录分组：
//   Public/Character   角色、动画实例、移动组件
//   Public/Spaceship   飞船
//   Public/Camera      星图相机
//   Private/Tests      本模块全部自动化测试
public class StellarEraPawns : ModuleRules
{
	public StellarEraPawns(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// 同 StellarEraPlanet：UBT 只加 Public/ 本身，不递归子目录。
		// 不写这两行，`#include "PlanetCharacter.h"` 会 C1083 —— 主模块和 UI
		// 都是按裸名 include 这两个头的，全项目一贯如此。
		PublicIncludePaths.AddRange(new string[]
		{
			Path.Combine(ModuleDirectory, "Public", "Character"),
			Path.Combine(ModuleDirectory, "Public", "Spaceship"),
		});

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine",

			// 三个 Pawn 的公开头都继承 IVoxelViewpointInvokers，
			// 并且直接持有插件的 UAlwaysLocalVoxelInvokerWithPredictionComponent，
			// 所以插件头必须对下游可见 —— Voxel 是 Public 依赖。
			"Voxel",

			"StellarEraCore",

			// 角色贴地、飞船大气阻力、相机取行星面位置，都直接调行星模块的接口。
			"StellarEraPlanet",
		});
	}
}
