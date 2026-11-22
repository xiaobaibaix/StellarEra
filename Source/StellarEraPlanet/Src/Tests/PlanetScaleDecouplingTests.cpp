#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Components/SceneComponent.h"
#include "VoxelPlanetActor.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraPlanetScaleDecouplingTest,
	"StellarEra.Planet.Geo.ScaleDecoupling",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraPlanetScaleDecouplingTest::RunTest(const FString& Parameters)
{
	AVoxelPlanetActor* Planet = NewObject<AVoxelPlanetActor>(GetTransientPackage());
	if (!TestNotNull(TEXT("行星 Actor"), Planet))
	{
		return false;
	}
	if (!Planet->GetRootComponent())
	{
		Planet->SetRootComponent(NewObject<USceneComponent>(Planet));
	}

	Planet->Radius = 100.f;
	Planet->VoxelSize = 100.f;
	Planet->SetActorScale3D(FVector(1.0));
	const float BaseRadiusCm = Planet->GetSurfaceRadiusCm();
	const int32 BaseLOD = Planet->ComputeOrbitLOD();

	Planet->SetActorScale3D(FVector(2.0));
	TestEqual(TEXT("地表半径随缩放线性变化（不是 S²）"),
		Planet->GetSurfaceRadiusCm(), BaseRadiusCm * 2.f, 1.f);
	TestEqual(TEXT("LOD 不随缩放变化"), Planet->ComputeOrbitLOD(), BaseLOD);
	TestEqual(TEXT("生成器半径不含缩放"), Planet->GetGeneratorRadiusVoxels(), 100.f, 0.f);

	// 物理半径 = 未缩放体素半径 × 体素尺寸；缩放到 2 时不变。
	TestEqual(TEXT("物理半径不含缩放"), Planet->GetPhysicalRadiusCm(), 100.f * 100.f, 0.01f);

	return true;
}

#endif
