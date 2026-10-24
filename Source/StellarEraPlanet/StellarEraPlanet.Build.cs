// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

// 体素星球层：星球 Actor、生成器、地形模块与两种地形实现。
//
// Voxel 必须是 **Public** 依赖：本模块的 5 个公开头直接 include 插件头并继承
// 插件类型（如 AVoxelPlanetActor : public AVoxelWorld），下游模块要用这些类型
// 就必须拿到插件的包含路径 —— 依赖声明为 Public 才会向下游传递。
public class StellarEraPlanet : ModuleRules
{
	public StellarEraPlanet(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "Voxel" });

		// JSON 只在 PlanetGeologyField.cpp 里用，不需要向下游传递。
		PrivateDependencyModuleNames.AddRange(new string[] { "Json" });

		// 大气壳的编辑器预览要取活动视口相机（GEditor / FEditorViewportClient），
		// 这两个符号在 UnrealEd 模块里。只对编辑器目标加，运行时构建不受影响，
		// 对应的代码也都在 #if WITH_EDITOR 里。
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.Add("UnrealEd");
		}
	}
}
