// 行星 LOD 过渡设置回归：ChunksDitheringDuration 默认 1 s ⇒ 换 LOD 时两套网格共存 2 s，
// 高速飞行叠影可见；MeshUpdatesBudget 默认 1000 ms ⇒ 一批块同时完成时游戏线程可卡满一秒。
// 断言构造函数产出的默认值，防止有人改回插件的默认。

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
