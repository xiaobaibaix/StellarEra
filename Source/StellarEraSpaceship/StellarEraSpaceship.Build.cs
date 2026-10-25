// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

// 飞船层：街机飞船 Pawn。位移由 N 体求解器推进，本模块只给推力与阻尼。
//
// 飞船**不能**依赖 StellarEraCosmos（那会成环）：推力加速度由子系统每帧下来读走。
// 大气组件住在 StellarEraAtmosphere（Task 10 已拆出）。
//
// Terrain / Character 是 **Private**：公开头 SpaceshipPawn.h 只 include
// CoreMinimal.h 与 Pawn.h，行星、角色、体素调用器的完整类型只在 .cpp 与测试里用到。
// Atmosphere 同样是 **Private**：SpaceshipPawn.cpp include PlanetAtmosphereComponent.h
// 并 FindComponentByClass 取壳，公开头不出现大气类型。
//
// Voxel 必须**显式**列出，尽管本模块的公开头不 include 任何插件头：
// Terrain 把 Voxel 声明成 Public，编译期 include 路径确实传了下来；
// 但链接行只含本模块的直接依赖，二级依赖的 .lib 不会跟着传 ——
// 于是本 DLL 缺插件里的 UClass 符号，LNK2019：
//   · SpaceshipPawn.h 的 UPROPERTY TObjectPtr<UVoxelInvokerWithPredictionComponent>
//     需要插件里的 UClass（Module.*.gen.cpp 的
//     Z_Construct_UClass_UVoxelInvokerWithPredictionComponent）。
public class StellarEraSpaceship : ModuleRules
{
	public StellarEraSpaceship(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"StellarEraTerrain",
			"StellarEraAtmosphere",
			"StellarEraCharacter",
			"Voxel",
		});
	}
}
