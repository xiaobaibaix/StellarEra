// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

// 界面层：HUD、星图面板、矿物图例、编辑按钮布局。
// （星图相机 AStarMapCameraPawn 已随模块合并搬到 StellarEraPawns ——
//   它是个被 possess 的 Pawn，不是绘制逻辑。）
//
// 这是唯一允许同时依赖多个功能模块的模块。反向依赖一律禁止：
// 功能模块不得 include 本模块的头。
//
// 本模块**不能**依赖 StellarEra（primary game module）—— Game 依赖 UI，
// UI 再依赖 Game 就成环。需要控制器数据的地方走 IStarMapUIInputs。
//
// 合并前本模块有一个**Public** 项目依赖 StellarEraTerrain，原因写在旧注释里：
// StarMapCameraPawn.h 公开继承 IVoxelViewpointInvokers，而那个接口住在 Terrain，
// 于是下游 include StarMapCameraPawn.h 时必须能找到 Terrain 的包含路径。
// 合并后两件事同时消失了：相机搬去了 Pawns，接口搬去了 Core。现在
// 四个公开头（StellarEraHUD / StarMapUISystem / PlanetEditUI / StarMapUIInputs）
// 一个项目内头都不 include，Public 依赖只剩引擎本身。
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
			"StellarEraCore",     // Cosmos 单位类型 CosmosTypes.h（StarMapUISystem.cpp、StellarEraHUD.cpp）
			"StellarEraPlanet",   // 行星 Actor / 表面网格 / 编辑组件与笔刷 / 矿物观察体组件
			"StellarEraPawns",    // 角色 / 飞船 / 星图相机
			"StellarEraCosmos",   // 选中天体、轨迹、星图状态
			"StellarEraGeology",  // 地质场 / 元素类型（StarMapUISystem.cpp 直接用）
		});
	}
}
