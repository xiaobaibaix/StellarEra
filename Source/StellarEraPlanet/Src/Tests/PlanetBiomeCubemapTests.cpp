// Copyright Voxel Plugin SAS. All Rights Reserved.
// biome cubemap 的方向映射：纯数学，无 RHI。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PlanetBiomeCubemap.h"

#if WITH_DEV_AUTOMATION_TESTS

// 面 + UV → 方向 → 面 + UV 必须回到原处。这条钉的是**面表本身**（主轴/u 轴/v 轴符号）：
// 任一轴写反，对应面就是镜像方向，而错误长得像「配色不对」，只看画面分不出。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraBiomeCubeFaceRoundTripTest,
	"StellarEra.Planet.BiomeCubemap.FaceRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraBiomeCubeFaceRoundTripTest::RunTest(const FString& Parameters)
{
	// 面中心、面内随机两处、以及左右上下两条边的中点。
	const float UVs[][2] =
	{
		{ 0.5f, 0.5f },
		{ 0.12f, 0.31f },
		{ 0.87f, 0.66f },
		{ 0.03f, 0.5f },
		{ 0.5f, 0.97f },
	};

	for (int32 Face = 0; Face < PlanetBiomeCubemap::NumFaces; ++Face)
	{
		for (const auto& UV : UVs)
		{
			const FVector Direction = PlanetBiomeCubemap::FaceUVToDirection(Face, UV[0], UV[1]);

			int32 OutFace = -1;
			float OutU = -1.f;
			float OutV = -1.f;
			PlanetBiomeCubemap::DirectionToFaceUV(Direction, OutFace, OutU, OutV);

			TestEqual(
				FString::Printf(TEXT("面 %d (%.2f, %.2f) 回过头仍在同一面"), Face, UV[0], UV[1]),
				OutFace, Face);
			TestTrue(
				FString::Printf(TEXT("面 %d (%.2f, %.2f) U 往返"), Face, UV[0], UV[1]),
				FMath::IsNearlyEqual(OutU, UV[0], 1e-4f));
			TestTrue(
				FString::Printf(TEXT("面 %d (%.2f, %.2f) V 往返"), Face, UV[0], UV[1]),
				FMath::IsNearlyEqual(OutV, UV[1], 1e-4f));
		}
	}

	return true;
}

// 面的边缘上两侧算出的**方向**必须相同 —— 这就是「cubemap 无缝」的全部内容。方向对不上
// 会在面边界看到一条颜色线，与 LOD 接缝长得一样但根因不同，故单独钉一条。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraBiomeCubeEdgeContinuityTest,
	"StellarEra.Planet.BiomeCubemap.EdgeContinuity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraBiomeCubeEdgeContinuityTest::RunTest(const FString& Parameters)
{
	// 四条边的中点。取中点而不是角点：角点有三面共线，方向换算的并列判定
	// 本来就允许落到任一面。
	const float Edges[][2] =
	{
		{ 0.f, 0.5f }, { 1.f, 0.5f }, { 0.5f, 0.f }, { 0.5f, 1.f },
	};

	for (int32 Face = 0; Face < PlanetBiomeCubemap::NumFaces; ++Face)
	{
		for (const auto& EdgeUV : Edges)
		{
			const FVector Source = PlanetBiomeCubemap::FaceUVToDirection(Face, EdgeUV[0], EdgeUV[1]);

			int32 NeighborFace = -1;
			float NeighborU = -1.f;
			float NeighborV = -1.f;
			PlanetBiomeCubemap::DirectionToFaceUV(Source, NeighborFace, NeighborU, NeighborV);

			const FVector RoundTripped =
				PlanetBiomeCubemap::FaceUVToDirection(NeighborFace, NeighborU, NeighborV);

			TestTrue(
				FString::Printf(TEXT("面 %d 边 (%.2f, %.2f) 与邻面方向一致"), Face, EdgeUV[0], EdgeUV[1]),
				RoundTripped.Equals(Source, 1e-4));
		}
	}

	return true;
}

#endif
