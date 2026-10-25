// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

// 角色控制层：行星角色、球面移动组件、动画实例。
//
// Terrain 与 PlanetEdit 是 **Private**：公开头只前置声明 AVoxelPlanetActor /
// UPlanetEditComponent，完整定义只在 .cpp 里 include。
//
// Voxel 必须**显式**列出，尽管本模块的公开头不 include 任何插件头：
//   - Terrain 把 Voxel 声明成 Public，那只是把插件的 include 路径向下游传，
//     编译期确实能过；
//   - 本 DLL 的链接行只含**直接**依赖。UBT 给模块生成链接环境时，只有直接依赖的
//     binary 会进 .dll.rsp；Terrain 对 Voxel 的依赖属于二级/传递 DLL 依赖，
//     不会递归收集到自己这里 —— 实测本模块的 .dll.rsp 里没有 UnrealEditor-Voxel.lib，
//     于是下面两处符号 LNK2019：
//       · PlanetCharacter.h 的 UPROPERTY TObjectPtr<UVoxelSimpleInvokerComponent>
//         需要插件里的 UClass（Module.*.gen.cpp 的 Z_Construct_UClass_...）；
//       · PlanetCharacter.cpp 的 EnableInvoker / DisableInvoker / IsInvokerEnabled。
// 其他模块的 StellarEraSpaceship / StellarEraPlanetEdit / StellarEraUI 也都是直接列 Voxel。
public class StellarEraCharacter : ModuleRules
{
	public StellarEraCharacter(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"StellarEraTerrain",
			"StellarEraPlanetEdit",
			"Voxel",
		});
	}
}
