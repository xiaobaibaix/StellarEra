// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

// 界面层：HUD、星图面板、矿物图例。
// .h 与 .cpp 同目录并列在 Src/ 下，测试在 Src/Tests。
// 编辑按钮布局（PlanetEditUI）已搬到 StellarEraPlanet/Src/Edit/，本模块只消费它 ——
// HUD 绘制与两个 Pawn 的鼠标命中共用同一份几何。
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
// 这条依赖是相机头还住在本模块时留下的：当时 StarMapCameraPawn.h 里的
// TObjectPtr<UVoxelSimpleInvokerComponent> UPROPERTY 让本模块的生成代码
// 需要 Voxel 的 UClass（早先靠 StellarEraTerrain 的 **Public** 依赖传递；
// 传递链断掉后链接期报 LNK2019: Z_Construct_UClass_UVoxelSimpleInvokerComponent）。
//
// 2026-11-06 相机搬进 StellarEraPawns/Src/StarMap/ 之后，本模块已经没有直接
// 使用 Voxel 类型的地方：Voxel 类型只经 Planet 的公开头（VoxelPlanetActor.h）
// 间接带进来，而 Planet → Voxel 是 Public 依赖。这一项目前留着未删 ——
// 删之前得先编译验证没有隐藏的 UClass 引用。
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

			// 本模块的公开头只对 Planet 的类型做前置声明（StarMapUIInputs.h 的
			// UPlanetMineralViewComponent、StellarEraHUD.h 的 UPlanetEditComponent），
			// 完整类型一律在 .cpp 里使用。按「公开头里 include 了对方才算 Public」
			// 的判据，这一项其实可以降回 Private；目前保持 Public 未动，
			// 待编译验证后由维护者决定。
			// （2026-11-06 之前它是 Public 的硬理由：StarMapCameraPawn.h 是本模块的
			//  公开头且实现 IVoxelViewpointInvokers；相机搬进 Pawns/Src/StarMap/ 后
			//  这个理由不再成立。）
			"StellarEraPlanet",

			// 本模块的两个公开头直接 include 了 Pawns 的头：
			//   StarMapUISystem.h → StarMapPanelLayout.h（面板几何，绘制与命中共用）
			//   StellarEraHUD.h   → StellarEraNoticeSink.h（HUD 实现提示接口）
			// 按「公开头里 include 了对方才算 Public」的判据，这条边必须是 Public，
			// 否则只依赖 UI 的下游模块 include 这两个头时会 C1083。
			"StellarEraPawns",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"StellarEraCosmos",   // 选中天体、轨迹、星图状态、CosmosTypes 的枚举
			// 地质场与元素类型（StarMapUISystem.cpp 直接用）随矿物并入 Planet，
			// 不再单独依赖 StellarEraGeology。
			"Voxel",              // 相机头搬走后已无直接使用；理由见上方「关于 Voxel 依赖」
		});
	}
}
