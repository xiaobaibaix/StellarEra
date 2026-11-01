// 行星在没有世界的宿主上重建的回归测试。
//
// 蓝图类默认值对象（Default__Xxx_C）不在任何世界里，GetWorld() 返回 nullptr。
// 而 AVoxelWorld::CreateWorld 第一件事就是取 GetWorld()->WorldType
// （VoxelWorld.cpp:245），所以 RebuildPlanet 一旦走到 CreateWorld 就是空指针
// 访问违例。PostEditChangeProperty 会把 CDO 上的每一次属性编辑导到这里，
// 于是「在蓝图编辑器的类默认值面板里改 Terrain / Radius」会直接崩编辑器。
//
// 本测试的宿主就是原生 CDO：它与蓝图 CDO 同样满足 GetWorld() == nullptr。

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

	// 生成器也必须没被建出来：CDO 上出现一个同名的 VoxelPlanetGenerator 子对象，
	// 会让关卡行星的同名子对象把它解析成 archetype，外部 Actor 包从此存不下去
	// （Illegal reference to private object）。PostLoad 里也有同样的兜底。
	TestNull(TEXT("CDO 不应该持有生成器"), CDO->PlanetGenerator);

	return true;
}

#endif
