// Copyright Voxel Plugin SAS. All Rights Reserved.
// biome cubemap 的方向映射与烘焙：纯数学 + CPU，无 RHI。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PlanetBiomeCubemap.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** 一套配色差异明显、且确定有海有陆的参数，避免测试断言依赖资产默认值。 */
	FPlanetNoiseParams MakeTestParams()
	{
		FPlanetNoiseParams Params;
		Params.Amplitude = 8.f;
		Params.Frequency = 2.5f;
		Params.Octaves = 4;
		Params.SeaLevel = -0.35f;
		Params.WaterDepthRange = 0.25f;
		Params.SandToGrassHeight = 0.08f;
		Params.GrassToRockHeight = 0.55f;
		Params.PolarStart = 0.72f;

		Params.DeepWaterColor = FLinearColor(0.02f, 0.06f, 0.22f);
		Params.ShallowWaterColor = FLinearColor(0.05f, 0.22f, 0.38f);
		Params.SandColor = FLinearColor(0.72f, 0.66f, 0.44f);
		Params.GrassColor = FLinearColor(0.18f, 0.42f, 0.16f);
		Params.RockColor = FLinearColor(0.32f, 0.29f, 0.26f);
		Params.SnowColor = FLinearColor(0.92f, 0.94f, 0.97f);
		return Params;
	}
}

// 面 + UV → 方向 → 面 + UV 必须回到原处。
//
// 这条钉的是**面表本身**（主轴 / u 轴 / v 轴的符号）：任何一个轴写反，
// 对应面上就会得到镜像的方向，颜色在整个面上是错的，而错误又长得像「配色
// 不对」—— 只看画面没法区分是表错了还是配色错了。
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

// 面的边缘上，两侧算出的**方向**必须相同 —— 这就是「cubemap 无缝」的全部内容。
//
// 相邻两面在公共边上的方向若对不上，材质查表会在面边界处看到一条颜色线，
// 与我们要修的 LOD 接缝长得一模一样，但根因完全不同。所以单独钉一条。
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

// 烘焙出的每个像素必须等于「同一个方向上的地表配色」。
//
// 这条钉的是烘焙侧的接线：深度参数用哪个半径、像素中心怎么取、sRGB 怎么编码。
// 任何一处偏了，材质查到的颜色和顶点色就不是同一套 —— 接缝会从「采样密度差」
// 变成「两套配色不一致」，而且只在 LOD 切换的瞬间可见，极难定位。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraBiomeCubeBakeMatchesColorTest,
	"StellarEra.Planet.BiomeCubemap.BakeMatchesColorFunction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraBiomeCubeBakeMatchesColorTest::RunTest(const FString& Parameters)
{
	const FPlanetNoiseParams Params = MakeTestParams();
	const double Radius = 100.0;
	const int32 Resolution = 16;

	TArray<FColor> Colors;
	PlanetBiomeCubemap::BakeFaceColors(
		Params, /*Geology=*/nullptr, Radius, /*Face=*/0, Resolution, Colors);

	TestEqual(TEXT("像素数 = 分辨率²"), Colors.Num(), Resolution * Resolution);
	if (Colors.Num() != Resolution * Resolution)
	{
		return false;
	}

	// 至少两种颜色：参数里海平面把一部分地形削平，配色必然分岔。
	// 全同色的烘焙图意味着参数没生效（例如颜色字段被漏拷），但那条路径
	// 不会报错，只会让整颗星变成一片纯色。
	TSet<uint32> UniqueColors;
	for (const FColor& Color : Colors)
	{
		UniqueColors.Add(Color.ToPackedARGB());
	}
	TestTrue(TEXT("烘焙图不是纯色"), UniqueColors.Num() >= 2);

	// 逐像素对照。独立重算一遍「方向 → 颜色」，而不是调烘焙函数自己。
	bool bAllMatch = true;
	for (int32 Y = 0; Y < Resolution; ++Y)
	{
		for (int32 X = 0; X < Resolution; ++X)
		{
			const FVector Direction = PlanetBiomeCubemap::FaceUVToDirection(
				0,
				(float(X) + 0.5f) / float(Resolution),
				(float(Y) + 0.5f) / float(Resolution));

			const double SurfaceRadius =
				Radius + double(Params.Amplitude) * double(ComputePlanetOffset(Params, Direction));
			const FColor Expected = ComputePlanetSurfaceColor(
				Params, nullptr, Radius, Direction, SurfaceRadius).ToFColor(true);

			if (Colors[Y * Resolution + X].ToPackedARGB() != Expected.ToPackedARGB())
			{
				bAllMatch = false;
				AddError(FString::Printf(
					TEXT("像素 (%d, %d) 与配色函数不一致：烘焙 %08x，重算 %08x"),
					X, Y,
					Colors[Y * Resolution + X].ToPackedARGB(),
					Expected.ToPackedARGB()));
			}
		}
	}
	TestTrue(TEXT("每个像素都等于同方向的配色函数值"), bAllMatch);

	return true;
}

// 非法分辨率：烘焙函数必须交出空数组，而不是带着上一次的数据往下走
// （材质会把那张图当成全黑，整颗星变暗且没有任何报错）。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraBiomeCubeZeroResolutionTest,
	"StellarEra.Planet.BiomeCubemap.ZeroResolutionIsEmpty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraBiomeCubeZeroResolutionTest::RunTest(const FString& Parameters)
{
	const FPlanetNoiseParams Params = MakeTestParams();

	TArray<FColor> Colors;
	Colors.Add(FColor::Red);
	PlanetBiomeCubemap::BakeFaceColors(Params, nullptr, 100.0, 0, 0, Colors);

	TestEqual(TEXT("分辨率 0 → 空数组"), Colors.Num(), 0);
	return true;
}

#endif
