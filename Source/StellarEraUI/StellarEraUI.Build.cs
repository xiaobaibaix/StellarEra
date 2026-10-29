// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

// 显示与控制层：HUD、星图相机、星图面板、矿物图例、编辑按钮布局。
//
// 这是唯一允许同时依赖多个功能模块的模块。反向依赖一律禁止：
// 功能模块不得 include 本模块的头。
//
// 本模块**不能**依赖 StellarEra（primary game module）—— Game 依赖 UI，
// UI 再依赖 Game 就成环。需要控制器数据的地方走 IStarMapUIInputs。
//
// 下面这些是本模块依赖的功能模块，各管一摊：
//   StellarEraCosmos（天体 / 轨迹 / 星图状态）、StellarEraTerrain（行星 Actor、
//   行星面射线、体素调用器）、StellarEraGeology（地质场与元素类型）、
//   StellarEraMinerals（矿物观察体组件）、StellarEraCharacter（行星角色）、
//   StellarEraSpaceship（飞船）、StellarEraPlanetEdit（编辑组件与笔刷）、
//   StellarEraCore（Cosmos 单位类型）。
public class StellarEraUI : ModuleRules
{
	public StellarEraUI(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine",
			// 公开头 StarMapCameraPawn.h 公开继承 IVoxelViewpointInvokers，
			// 而 VoxelViewpointInvokers.h 在 StellarEraTerrain —— 公开头 include 的头
			// 所在模块必须是公开依赖，否则下游 include StarMapCameraPawn.h 时找不到它。
			"StellarEraTerrain",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"StellarEraCore",    // Cosmos 单位类型 CosmosTypes.h（生产代码：StarMapUISystem.cpp、StellarEraHUD.cpp）
			"StellarEraCosmos",  // 选中天体、轨迹、星图状态
			"StellarEraGeology", // 地质场/元素类型（StarMapUISystem.cpp 直接用）
			"StellarEraMinerals", // 矿物观察体组件 PlanetMineralViewComponent.h（StarMapUISystem.cpp 直接用）
			"StellarEraSpaceship", // 飞船（StellarEraHUD.cpp 用 ASpaceshipPawn）
			"StellarEraPlanetEdit", // 编辑组件 / 笔刷（StarMapCameraPawn.cpp、StellarEraHUD.cpp）
			"StellarEraCharacter", // 角色（StellarEraHUD.cpp 用 APlanetCharacter）
			"Voxel",             // StarMapCameraInvokerTests.cpp 的体素调用器
		});
	}
}
