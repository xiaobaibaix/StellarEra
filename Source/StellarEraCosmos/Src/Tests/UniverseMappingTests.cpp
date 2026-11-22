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
	TestTrue(TEXT("近场判定正例"), Mapping.IsNear(Mapping.NearDistanceCm() * 0.5));

	const FVector Rendered = Mapping.ToRender(FVector3d(RealDistance, 0.0, 0.0), false);
	TestEqual(TEXT("1 AU 渲染为 RenderCmPerAu"), Rendered.Size(), Mapping.RenderCmPerAu, 1.0);
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

	TestEqual(TEXT("另一颗进入近场时抢占"),
		PickCurrentBody({ Enter * 0.5, Exit * 0.75 }, 1, Mapping), 0);
	TestEqual(TEXT("上一颗在退出带、更近的竞争者在 (Enter, Exit] 内 → 保留上一颗"),
		PickCurrentBody({ Enter * 1.5, Enter * 1.8 }, 1, Mapping), 1);
	TestEqual(TEXT("飞船在 B 表面、A 仍在退出带 → 选 B"),
		PickCurrentBody({ Enter * 0.5, Enter * 1.5 }, 1, Mapping), 0);
	TestEqual(TEXT("上一颗超出退出区，最近一颗在进入区内"),
		PickCurrentBody({ Exit * 1.25, Enter * 0.5 }, 0, Mapping), 1);
	TestEqual(TEXT("上一颗超出退出区，最近一颗未进进入区"),
		PickCurrentBody({ Exit * 1.25, Exit * 0.75 }, 0, Mapping), INDEX_NONE);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraSurfaceDistancesTest,
	"StellarEra.Cosmos.UniverseMapping.SurfaceDistances",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraSurfaceDistancesTest::RunTest(const FString& Parameters)
{
	const TArray<FVector3d> Positions = {
		FVector3d(1.0e-3, 0.0, 0.0),   // 0: 有地表，距飞船 1e-3 AU
		FVector3d(2.0e-3, 0.0, 0.0),   // 1: 无地表
		FVector3d(0.0, 0.0, 0.0)       // 2: 飞船自己，排除
	};
	const TArray<double> Radii = { 1.0e-7, 0.0, 1.0e-7 };
	const TArray<double> Distances = SurfaceDistancesCm(
		Positions, Radii, FVector3d::ZeroVector, 1.0e6, /*ExcludeIndex=*/2);

	TestEqual(TEXT("长度对齐"), Distances.Num(), 3);
	TestEqual(TEXT("有地表按距离换算"), Distances[0], 1.0e3, 1e-6);
	TestTrue(TEXT("无地表记为无穷"), Distances[1] == TNumericLimits<double>::Max());
	TestTrue(TEXT("排除下标记为无穷"), Distances[2] == TNumericLimits<double>::Max());

	return true;
}

#endif
