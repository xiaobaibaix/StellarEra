// 行星 LOD 过渡设置回归测试。
//
// ChunksDitheringDuration 决定旧 LOD 块在替换块完成后再淡出多久
// （VoxelDefaultRenderer.cpp:884/900：同 LOD 用 1×，换 LOD 用 2×）。
// 默认 1 秒 ⇒ 换 LOD 时两套网格共存 2 秒；高速飞行时叠影肉眼可见。
//
// MeshUpdatesBudget 是每帧应用已完成块网格的毫秒预算（只在 Game play type
// 生效，编辑器预览固定 1000）。默认 1000 毫秒意味着一批块同时完成时游戏线程
// 可以卡满一秒 —— 高速飞行「卡一下」的直接来源。
//
// 断言构造函数产出的默认值，防止有人把它们改回插件的默认。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "VoxelPlanetActor.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraPlanetLODTransitionSettingsTest,
	"StellarEra.Planet.LODTransitionSettings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraPlanetLODTransitionSettingsTest::RunTest(const FString& Parameters)
{
	AVoxelPlanetActor* CDO = GetMutableDefault<AVoxelPlanetActor>();
	if (!TestNotNull(TEXT("AVoxelPlanetActor CDO"), CDO))
	{
		return false;
	}

	TestEqual(TEXT("ChunksDitheringDuration（秒）"), CDO->ChunksDitheringDuration, 0.3f, 1e-4f);
	TestEqual(TEXT("MeshUpdatesBudget（毫秒）"), CDO->MeshUpdatesBudget, 8.f, 1e-4f);

	return true;
}

#endif
