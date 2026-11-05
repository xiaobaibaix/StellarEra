// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

// 界面层：HUD、星图面板、矿物图例、编辑按钮布局。
// .h 与 .cpp 同目录并列在 Src/ 下，测试在 Src/Tests。
//
// 这是唯一允许同时依赖多个功能模块的模块。反向依赖一律禁止：
// 功能模块不得 include 本模块的头。
//
// 本模块**不能**依赖 StellarEra（primary game module）—— Game 依赖 UI，
// UI 再依赖 Game 就成环。需要控制器数据的地方走 IStarMapUIInputs。
//
// ── 星图相机已经搬走 ────────────────────────────────────────────────
// AStarMapCameraPawn 现在在 StellarEraPawns/Src/StarMap/。它依赖 Cosmos 的
// ICosmosBody（飞船实现的接口）之后，Pawns → Cosmos 这条边才成立 ——
// 之前搬不动是因为 Cosmos 内部 Cast<ASpaceshipPawn>，即 Cosmos → Pawns。
// 本模块仍然依赖 Pawns：HUD 要 include 相机头来认「现在是不是星图视角」。
//
// ── 关于 Voxel 依赖 ───────────────────────────────────────────────────
// 合并前本模块靠 StellarEraTerrain 的 **Public** 依赖传递拿到 Voxel 模块，
// 所以相机头里的 TObjectPtr<UVoxelSimpleInvokerComponent> 能链接上。
// 接口搬去 Core、Terrain 依赖删掉之后，这条传递链断了，链接期报
// LNK2019: Z_Construct_UClass_UVoxelSimpleInvokerComponent 未解析。
//
// 用 Private 而不是 Public：相机头对 Voxel 类型只有前向声明，
// 下游模块 include 它只需要指针，不需要 Voxel 的包含路径与库。
public class StellarEraUI : ModuleRules
{
	public StellarEraUI(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Src/ 与所有子目录进搜索路径，说明见 StellarEraPlanet.Build.cs。
		// 模块入口 StellarEraUI.h/.cpp 与 Build.cs 并列在模块根，不放进 Src/。
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

			// StarMapCameraPawn.h 是本模块的公开头，且实现 IVoxelViewpointInvokers
			// —— 该接口随体素接线层住在 StellarEraPlanet/Src/Invokers/，
			// 下游 include 相机头时必须能找到它，所以 Planet 是 **Public** 依赖。
			// （原 StellarEraCore 住着这个接口，它拆掉后本项从 Private 升为 Public。）
			// 本模块对 Planet 的其余使用（行星 Actor、编辑组件与笔刷、矿物与地质场）
			// 本来全在 .cpp 里，升级只多传递几个 include 路径，不引入环。
			"StellarEraPlanet",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"StellarEraPawns",    // 角色 / 飞船 / 星图相机
			"StellarEraCosmos",   // 选中天体、轨迹、星图状态、CosmosTypes 的枚举
			// 地质场与元素类型（StarMapUISystem.cpp 直接用）随矿物并入 Planet，
			// 不再单独依赖 StellarEraGeology。
			"Voxel",              // 相机头里的 UPROPERTY 需要 UVoxelSimpleInvokerComponent 的 UClass
		});
	}
}
