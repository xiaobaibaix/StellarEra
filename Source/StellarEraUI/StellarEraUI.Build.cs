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
// StellarEraPlanet 现在仍是临时依赖：Task 12 会把它换成 Terrain / Minerals / Geology。
// 角色与飞船已分别由 StellarEraCharacter / StellarEraSpaceship 提供，不再是临时项。
public class StellarEraUI : ModuleRules
{
	public StellarEraUI(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"StellarEraCore",    // CosmosTypes.h（StarMapUISystemTests.cpp）
			"StellarEraCosmos",  // 选中天体、轨迹、星图状态
			"StellarEraPlanet",  // 行星 Actor、矿物观察（临时）
			"StellarEraGeology", // 地质场/元素类型（StarMapUISystem.cpp 直接用）
			"StellarEraSpaceship", // 飞船（StellarEraHUD.cpp 用 ASpaceshipPawn）
			"StellarEraPlanetEdit", // 编辑组件/笔刷（临时，Task 7 再核）
			"StellarEraCharacter", // 角色（StellarEraHUD.cpp 用 APlanetCharacter）
			"Voxel",             // StarMapCameraInvokerTests.cpp 的体素调用器
		});
	}
}
