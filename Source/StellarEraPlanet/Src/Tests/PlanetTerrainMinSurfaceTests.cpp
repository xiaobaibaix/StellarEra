// 大气的解析地面取 GetMinSurfaceRadiusVoxels：它一旦高过任何方向的真实地表，
// 相机高过它时地平线下就会截出一条黑带（射线撞上悬空假地面、积分被截短）。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Math/RandomStream.h"
#include "VoxelPlanetNoiseTerrain.h"
#include "VoxelPlanetSphereTerrain.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraTerrainMinSurfaceTest,
	"StellarEra.Planet.Terrain.MinSurfaceRadius",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraTerrainMinSurfaceTest::RunTest(const FString& Parameters)
{
	FVoxelPlanetTerrainContext Context;
	Context.Radius = 20000.f;

	UVoxelPlanetNoiseTerrain* Noise = NewObject<UVoxelPlanetNoiseTerrain>();
	const float MinRadius = Noise->GetMinSurfaceRadiusVoxels(Context);
	TestTrue(TEXT("噪声地形的最低地表低于基准半径"), MinRadius < Context.Radius);

	FRandomStream Stream(1234);
	float Lowest = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < 4096; ++Index)
	{
		const float Surface = Noise->GetSurfaceRadiusVoxels(Context, Stream.GetUnitVector());
		Lowest = FMath::Min(Lowest, Surface);
	}
	TestTrue(FString::Printf(TEXT("最低地表 %.3f 不高于采样到的最低真实地表 %.3f"), MinRadius, Lowest),
		MinRadius <= Lowest + KINDA_SMALL_NUMBER);

	// 半径变了它要跟着变：大气在运行中按它重写材质。
	FVoxelPlanetTerrainContext Bigger = Context;
	Bigger.Radius = 40000.f;
	TestEqual(TEXT("最低地表随半径平移"),
		Noise->GetMinSurfaceRadiusVoxels(Bigger) - MinRadius, 20000.f, 0.01f);

	UVoxelPlanetSphereTerrain* Sphere = NewObject<UVoxelPlanetSphereTerrain>();
	TestEqual(TEXT("光滑球的最低地表就是基准半径"),
		Sphere->GetMinSurfaceRadiusVoxels(Context), Context.Radius);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
