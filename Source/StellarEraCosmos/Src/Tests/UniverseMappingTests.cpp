#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "UniverseMapping.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraUniverseMappingTest,
	"StellarEra.Cosmos.UniverseMapping.Core",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraUniverseMappingTest::RunTest(const FString& Parameters)
{
	const FUniverseMapping Mapping;

	TestEqual(TEXT("FarScale 由两套标定相除"),
		Mapping.FarScale(), Mapping.RenderCmPerAu / Mapping.PhysicalCmPerAu, 1e-18);
	TestTrue(TEXT("近场阈值满足 k·R_near = D_min"),
		FMath::IsNearlyEqual(Mapping.NearDistanceCm() * Mapping.FarScale(), Mapping.MinFarDistanceCm, 1.0));

	const FVector3d NearPoint(1.0e6, -2.0e6, 3.0e5);
	TestTrue(TEXT("近场位置恒等"), Mapping.ToRender(NearPoint, true).Equals(FVector(NearPoint)));
	TestEqual(TEXT("近场缩放为 1"), Mapping.ScaleFor(true), 1.0, 0.0);

	const double RealDistance = 1.496e13;
	const double BodyRadius = 6.96e10;
	TestFalse(TEXT("1 AU 走远场"), Mapping.IsNear(RealDistance));

	const FVector Rendered = Mapping.ToRender(FVector3d(RealDistance, 0.0, 0.0), false);
	const double RenderedRadius = BodyRadius * Mapping.ScaleFor(false);
	TestTrue(TEXT("远场角直径守恒"),
		FMath::IsNearlyEqual(RenderedRadius / Rendered.Size(), BodyRadius / RealDistance, 1e-12));

	const FVector3d FarPoint(3.0e12, 4.0e12, -1.0e12);
	TestTrue(TEXT("远场往返一致"),
		Mapping.ToSim(Mapping.ToRender(FarPoint, false), false).Equals(FarPoint, 1.0));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraUniverseCurrentBodyTest,
	"StellarEra.Cosmos.UniverseMapping.CurrentBody",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraUniverseCurrentBodyTest::RunTest(const FString& Parameters)
{
	const FUniverseMapping Mapping;
	const double Enter = Mapping.NearDistanceCm();
	const double Exit = Enter * 2.0;

	TestEqual(TEXT("没有天体"), PickCurrentBody({}, INDEX_NONE, Mapping), INDEX_NONE);
	TestEqual(TEXT("最近且在近场内"), PickCurrentBody({ Enter * 0.5, Enter * 3.0 }, INDEX_NONE, Mapping), 0);
	TestEqual(TEXT("全部在近场外"), PickCurrentBody({ Enter * 3.0, Enter * 4.0 }, INDEX_NONE, Mapping), INDEX_NONE);

	// 滞回：已选中天体在 [Enter, Exit] 之间时不丢，出了 Exit 才丢。
	TestEqual(TEXT("滞回区间保留"), PickCurrentBody({ Enter * 1.5, Enter * 9.0 }, 0, Mapping), 0);
	TestEqual(TEXT("超出滞回区间"), PickCurrentBody({ Enter * 2.5, Enter * 9.0 }, 0, Mapping), INDEX_NONE);

	TestEqual(TEXT("上一颗在退出区内，别的更近也保留"),
		PickCurrentBody({ Enter * 3.0, Exit * 0.75 }, 1, Mapping), 1);
	TestEqual(TEXT("上一颗超出退出区，最近一颗在进入区内"),
		PickCurrentBody({ Exit * 1.25, Enter * 0.5 }, 0, Mapping), 1);
	TestEqual(TEXT("上一颗超出退出区，最近一颗未进进入区"),
		PickCurrentBody({ Exit * 1.25, Exit * 0.75 }, 0, Mapping), INDEX_NONE);

	return true;
}

#endif
