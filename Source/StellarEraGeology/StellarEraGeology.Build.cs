// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

// 地质层：元素表、行星地质场、UDataAsset 预设、噪声采样。
// .h 与 .cpp 同目录并列在 Src/ 下，测试在 Src/Tests。
//
// Voxel 是 **Public**：PlanetGeologyField.h 在公开头里 include
// FastNoise/VoxelFastNoise.h（FVoxelFastNoise 按值持有）。
// Core 是 **Public**：.cpp 里用 Core 的 FRandomStream 撒矿囊，公开头也依赖 CoreMinimal。
// Engine 是 **Public**：PlanetGeologyPreset.h 在公开头里继承 UDataAsset
// （include Engine/DataAsset.h），UDataAsset 来自 Engine 模块。
// CoreUObject 是 **Public**：本模块含一个 UDataAsset 子类（UPlanetGeologyPreset）
// 与两个 USTRUCT（FPlanetGeologyAppearance、FPlanetGeologyElementParams），
// 反射是真依赖，不再是「与其余模块保持一致」的摆设。
// AssetRegistry 是 **Public**：PlanetGeologyPreset.cpp 末尾的
// StellarEra.CreateEarthGeologyPreset 命令用 FAssetRegistryModule::AssetCreated 把新建的
// 资产登记进资产注册表。它只在 #if WITH_EDITOR 下被用到，但 UBT 的依赖表不区分预处理器
// 分支，所以照样要列出来。它是 Runtime 模块而不是项目模块，模块分层不变。
// 没有 Private 依赖：.cpp 里用到的 Voxel / UObject 反射 / 资产保存都来自上面的 Public 项。
public class StellarEraGeology : ModuleRules
{
	public StellarEraGeology(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Src/ 与所有子目录进搜索路径，说明见 StellarEraPlanet.Build.cs。
		// 模块入口 StellarEraGeology.h/.cpp 与 Build.cs 并列在模块根，不放进 Src/。
		PublicIncludePaths.Add(ModuleDirectory);

		string SrcDir = Path.Combine(ModuleDirectory, "Src");
		PublicIncludePaths.Add(SrcDir);
		foreach (string SubDir in Directory.GetDirectories(SrcDir, "*", SearchOption.AllDirectories))
		{
			PublicIncludePaths.Add(SubDir);
		}

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "Voxel", "AssetRegistry",
		});
	}
}
