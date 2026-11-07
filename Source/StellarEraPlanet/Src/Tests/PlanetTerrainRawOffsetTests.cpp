// 「海平面不再削平地形」回归：旧的 ComputePlanetOffset 末尾有 FMath::Max(Noise, SeaLevel)，
// 低于海平面的地形被削平；现在改为 ComputeRawPlanetOffset，输出域 [-1, 1]。
// 不断言「一定存在低于海平面的方向」（那是数据性质，不是契约），只钉输出范围与 SeaLevel 不参与形状。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "VoxelPlanetNoiseTerrain.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraTerrainRawOffsetTest,
	"StellarEra.Planet.Terrain.RawOffsetRange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraTerrainRawOffsetTest::RunTest(const FString& Parameters)
{
	FPlanetNoiseParams Params;
	Params.SeedOffset = FVector(137.5, -42.25, 911.75);

	// 均匀铺满球面的方向集：斐波那契球，避免极点堆积也避免规则网格的对称性。
	constexpr int32 NumDirections = 4096;
	TArray<FVector> Directions;
	Directions.Reserve(NumDirections);
	const double GoldenAngle = PI * (3.0 - FMath::Sqrt(5.0));
	for (int32 Index = 0; Index < NumDirections; ++Index)
	{
		const double Z = 1.0 - 2.0 * (double(Index) + 0.5) / double(NumDirections);
		const double R = FMath::Sqrt(FMath::Max(1.0 - Z * Z, 0.0));
		const double Theta = GoldenAngle * double(Index);
		Directions.Add(FVector(R * FMath::Cos(Theta), R * FMath::Sin(Theta), Z));
	}

	// --- 契约 1：输出落在 [-1, 1] ---
	double MinOffset = TNumericLimits<double>::Max();
	double MaxOffset = -TNumericLimits<double>::Max();
	for (const FVector& Dir : Directions)
	{
		const double Offset = double(ComputeRawPlanetOffset(Params, Dir));
		MinOffset = FMath::Min(MinOffset, Offset);
		MaxOffset = FMath::Max(MaxOffset, Offset);
	}
	TestTrue(FString::Printf(TEXT("位移下界 %.4f 必须 >= -1"), MinOffset), MinOffset >= -1.0 - 1e-4);
	TestTrue(FString::Printf(TEXT("位移上界 %.4f 必须 <= 1"), MaxOffset), MaxOffset <= 1.0 + 1e-4);

	// --- 契约 2：SeaLevel 不参与形状 ---
	// 旧实现下 SeaLevel 0.9 会把整颗星削成平面；新实现必须逐点完全相等。
	FPlanetNoiseParams HighSea = Params;
	HighSea.SeaLevel = 0.9f;
	int32 NumMismatches = 0;
	for (const FVector& Dir : Directions)
	{
		if (ComputeRawPlanetOffset(Params, Dir) != ComputeRawPlanetOffset(HighSea, Dir))
		{
			++NumMismatches;
		}
	}
	TestEqual(TEXT("SeaLevel 改变形状的采样点数（必须为 0）"), NumMismatches, 0);

	return true;
}

#endif
