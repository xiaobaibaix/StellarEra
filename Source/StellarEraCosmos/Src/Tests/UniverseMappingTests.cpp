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

#endif
