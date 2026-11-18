// 行星在没有世界的宿主上重建的回归：CDO（Default__Xxx_C）不在任何世界里，GetWorld()
// 返回 nullptr，而 AVoxelWorld::CreateWorld 第一件事就是取 GetWorld()->WorldType
// （VoxelWorld.cpp:245）—— RebuildPlanet 走到 CreateWorld 就是空指针违例。
// 蓝图编辑器改 Terrain / Radius 会经 PostEditChangeProperty 打到 CDO；本测试宿主就是原生 CDO。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "VoxelPlanetActor.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraPlanetRebuildWithoutWorldTest,
	"StellarEra.Planet.RebuildWithoutWorld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraPlanetRebuildWithoutWorldTest::RunTest(const FString& Parameters)
{
	AVoxelPlanetActor* CDO = GetMutableDefault<AVoxelPlanetActor>();
	if (!TestNotNull(TEXT("AVoxelPlanetActor CDO"), CDO))
	{
		return false;
	}

	// 前提不成立就不往下走：这条测试的全部意义是「无世界宿主」。
	if (!TestNull(TEXT("CDO 没有世界"), CDO->GetWorld()))
	{
		return false;
	}

	// 修复前：这里在 VoxelWorld.cpp:245 上访问违例，整个进程挂掉。
	CDO->RebuildPlanet();

	TestFalse(TEXT("无世界宿主不应该建出体素世界"), CDO->IsCreated());

	return true;
}

#endif
