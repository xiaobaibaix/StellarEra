// 水文烘焙的回归测试。
//
// 用两条解析地形（碗 + 谷），断言填洼水位、湖泊特征、河流存在性与跨面连续性。
// 地形刻意不用噪声：噪声的分布是数据性质，不是契约，写成断言会随 seed 漂。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PlanetHydrologyBake.h"
#include "PlanetBiomeCubemap.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr double TestRadius = 1000.0;

	FPlanetHydrologyParams MakeTestParams()
	{
		FPlanetHydrologyParams Params;
		Params.SeaLevel = -0.3f;
		Params.Amplitude = 8.f;
		Params.MinRiverFlow = 24.f;
		Params.MaxCarveDepth = 4.f;
		Params.Resolution = 128;
		Params.Epsilon = 0.01f;
		return Params;
	}

	/**
	 * 碗：角半径 0.3 的二次凹坑，碗缘 −0.05（高于海面 −0.3），碗外一律 −0.5（全是海）。
	 * 于是碗里必然蓄成一片湖，水位 ≈ 碗缘，且不与海连通。
	 */
	float BowlHeight(const FVector& Dir)
	{
		const float Angle = FMath::Acos(FMath::Clamp(float(Dir.Z), -1.f, 1.f));
		constexpr float BowlAngle = 0.3f;
		if (Angle > BowlAngle)
		{
			return -0.5f;
		}
		const float T = Angle / BowlAngle;
		return -0.5f + 0.45f * T * T;
	}

	/**
	 * 谷：从 +Z（−0.8，海）向 −Z（+0.5，高山）一路升高的斜坡，再叠一个「离 lon = 0
	 * 越远越高」的横向项。横向项让水从两侧**汇聚**到 lon = 0 的谷底 —— 没有它，
	 * 斜坡上的水只会各自沿经线直下，汇流量永远停在 1，长不出河。
	 */
	float ValleyHeight(const FVector& Dir)
	{
		const double SafeZ = FMath::Clamp(double(Dir.Z), -1.0, 1.0);
		const double Angle = FMath::Acos(SafeZ);
		const double Lon = FMath::Atan2(double(Dir.Y), double(Dir.X));
		const double Normalized = -0.8 + 1.3 * (Angle / PI) + 0.4 * FMath::Square(Lon / PI);
		return float(Normalized);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraHydrologyLakeTest,
	"StellarEra.Planet.Hydrology.BakeLake",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraHydrologyLakeTest::RunTest(const FString& Parameters)
{
	const FPlanetHydrologyParams Params = MakeTestParams();
	const TSharedPtr<FPlanetHydrologyField> Field =
		FPlanetHydrologyBake::Bake(&BowlHeight, Params, TestRadius);
	TestTrue(TEXT("烘焙必须返回有效的场"), Field.IsValid() && Field->IsValid());
	if (!Field.IsValid() || !Field->IsValid())
	{
		return false;
	}

	// --- 海面 ---
	const float ExpectedSea = float(TestRadius + double(Params.Amplitude) * double(Params.SeaLevel));
	TestTrue(FString::Printf(TEXT("海面 %.2f 必须是 Radius + Amplitude × SeaLevel = %.2f"),
		Field->GetSeaLevelRadius(), ExpectedSea),
		FMath::Abs(Field->GetSeaLevelRadius() - ExpectedSea) < 0.01f);

	// --- 恰好一片湖，水位 ≈ 碗缘 ---
	const TArray<FPlanetLakeBasin>& Lakes = Field->GetLakes();
	TestEqual(TEXT("湖泊数量"), Lakes.Num(), 1);
	if (Lakes.Num() == 1)
	{
		const float ExpectedLevel = float(TestRadius + double(Params.Amplitude) * -0.05);
		TestTrue(FString::Printf(TEXT("湖面 %.3f 必须 ≈ 碗缘 %.3f（差 <= 0.6 体素）"),
			Lakes[0].WaterLevelRadius, ExpectedLevel),
			FMath::Abs(Lakes[0].WaterLevelRadius - ExpectedLevel) < 0.6f);
		TestTrue(FString::Printf(TEXT("湖心方向必须接近 +Z（实测 Z=%.4f）"), Lakes[0].CentroidDir.Z),
			Lakes[0].CentroidDir.Z > 0.99);
		TestTrue(FString::Printf(TEXT("湖的角半径 %.4f 必须 ≈ 0.3"), Lakes[0].AngularRadius),
			Lakes[0].AngularRadius > 0.29f && Lakes[0].AngularRadius < 0.33f);
		TestTrue(TEXT("湖水必须高于海面（它是湖，不是海的一部分）"),
			Lakes[0].WaterLevelRadius > Field->GetSeaLevelRadius() + 1.f);
	}

	// --- 点采样：碗心是水，碗外是海，岸边是陆地 ---
	const auto SampleAt = [&](const FVector& Dir) { return Field->Sample(Dir); };

	const FVector BowlCenter = FVector::UpVector;
	TestTrue(TEXT("碗心的水位必须高于地形（是水不是地）"),
		SampleAt(BowlCenter).WaterLevelRadius
			> float(TestRadius + double(Params.Amplitude) * double(BowlHeight(BowlCenter))) + 0.1f);

	const FVector OpenOcean = FVector(1.0, 1.0, -1.0).GetSafeNormal();
	TestTrue(TEXT("碗外必须是海面高度"),
		FMath::Abs(SampleAt(OpenOcean).WaterLevelRadius - Field->GetSeaLevelRadius()) < 0.5f);
	TestEqual(TEXT("海里的下切必须为 0"), SampleAt(OpenOcean).CarveDepth, 0.f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraHydrologyRiverTest,
	"StellarEra.Planet.Hydrology.BakeRiver",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraHydrologyRiverTest::RunTest(const FString& Parameters)
{
	const FPlanetHydrologyParams Params = MakeTestParams();
	const TSharedPtr<FPlanetHydrologyField> Field =
		FPlanetHydrologyBake::Bake(&ValleyHeight, Params, TestRadius);
	TestTrue(TEXT("烘焙必须返回有效的场"), Field.IsValid() && Field->IsValid());
	if (!Field.IsValid() || !Field->IsValid())
	{
		return false;
	}

	// --- 河网 ---
	const TArray<FPlanetRiverPolyline>& Rivers = Field->GetRivers();
	TestTrue(FString::Printf(TEXT("谷地必须至少长出一条河（实测 %d 条）"), Rivers.Num()),
		Rivers.Num() >= 1);

	int32 LongestPoints = 0;
	for (const FPlanetRiverPolyline& River : Rivers)
	{
		LongestPoints = FMath::Max(LongestPoints, River.Points.Num());

		// 沿折线水位单调不增（水只往低处流）。
		bool bMonotone = true;
		for (int32 Index = 1; Index < River.Points.Num(); ++Index)
		{
			bMonotone &= River.Points[Index].WaterLevelRadius
				<= River.Points[Index - 1].WaterLevelRadius + 1e-3f;
		}
		TestTrue(TEXT("河流折线的水位必须单调不增"), bMonotone);

		// 所有的点都必须有正河宽与有限的半径。
		bool bSane = true;
		for (const FPlanetRiverPoint& Point : River.Points)
		{
			bSane &= FMath::IsFinite(Point.WaterLevelRadius)
				&& Point.WidthVoxels > 0.f
				&& FMath::IsFinite(Point.Dir.SizeSquared());
		}
		TestTrue(TEXT("河流折线的点必须是有限值且河宽为正"), bSane);
	}
	TestTrue(FString::Printf(TEXT("最长河流至少 8 个点（实际 %d）"), LongestPoints),
		LongestPoints >= 8);

	// --- 下切：谷底 lon = 0、极角 1.8 rad 的中段必须有河道 ---
	// 方向 = (sin θ, 0, cos θ)，即 lon = 0、离 +Z 极 1.8 rad。
	const FPlanetHydrologyField::FSample Sample =
		Field->Sample(FVector(FMath::Sin(1.8), 0.0, FMath::Cos(1.8)));
	TestTrue(FString::Printf(TEXT("谷底必须有下切（实测 %.3f 体素）"), Sample.CarveDepth),
		Sample.CarveDepth > 0.5f);
	TestTrue(FString::Printf(TEXT("下切 %.3f 不得超过 MaxCarveDepth %.3f"),
		Sample.CarveDepth, Field->GetMaxCarveDepth()),
		Sample.CarveDepth <= Field->GetMaxCarveDepth() + 1e-3f);
	TestTrue(FString::Printf(TEXT("河流强度必须在 (0, 1] 内（实测 %.3f）"), Sample.RiverStrength),
		Sample.RiverStrength > 0.f && Sample.RiverStrength <= 1.f);

	// --- 下切的**幅度**：把公式钉死，而不是只钉「有下切」 ---
	//
	// 旧断言 `CarveDepth > 0.5` 是公式下界（1.0）就满足的，常量实现照样能过。
	// 河宽是汇流量的公开代理：WidthVoxels = clamp(√(Accum / MinRiverFlow) × RiverWidthScale, 1, 8)，
	// 本测试用默认的 RiverWidthScale = 1。取一个没被夹住的河宽就能反算出该格的汇流量，
	// 进而算出**应有**的下切深度，再与场里采样到的值对齐 —— 常量 1.0 与常量 4.0 都会露馅。
	// 采样点就是格心方向，双线性权重为 0，所以这里比的是精确值。
	const auto ExpectedCarve = [&](float WidthVoxels)
	{
		return FMath::Clamp(1.f + 2.f * WidthVoxels, 1.f, Field->GetMaxCarveDepth());
	};

	int32 NumCarveProbes = 0;
	double WorstCarveError = 0.0;
	for (const FPlanetRiverPolyline& River : Rivers)
	{
		for (const FPlanetRiverPoint& Point : River.Points)
		{
			// 贴着上下夹取边界的点反算不可靠，只挑中间的。
			if (Point.WidthVoxels <= 1.2f || Point.WidthVoxels >= 7.8f)
			{
				continue;
			}
			WorstCarveError = FMath::Max(WorstCarveError, double(FMath::Abs(
				Field->Sample(Point.Dir).CarveDepth - ExpectedCarve(Point.WidthVoxels))));
			++NumCarveProbes;
		}
	}
	TestTrue(FString::Printf(TEXT("必须有未被夹住的河宽可反算下切（实测 %d 个点）"), NumCarveProbes),
		NumCarveProbes > 0);
	TestTrue(FString::Printf(TEXT("下切必须等于 clamp(1 + 2·√(Accum/MinRiverFlow), 1, Max)（最大误差 %.4f）"),
		WorstCarveError), WorstCarveError < 0.01);

	// --- 河网最终汇入海（或湖） ---
	//
	// 折线终点必须是「最后一道河格」：再往前一步就是水面。沿终点切向扫一圈，
	// 只要有一个方向的水位贴着海面、或高于该方向的地形（湖面），就说明河确实到了水里。
	const double CellAngle = (PI * 0.5) / double(Params.Resolution);
	const auto HasWaterAround = [&](const FVector& Dir)
	{
		const FVector Axis = (FMath::Abs(Dir.Z) < 0.9) ? FVector::UpVector : FVector::RightVector;
		const FVector Tangent1 = FVector::CrossProduct(Dir, Axis).GetSafeNormal();
		const FVector Tangent2 = FVector::CrossProduct(Dir, Tangent1).GetSafeNormal();
		for (int32 Step = 0; Step < 8; ++Step)
		{
			const double Angle = double(Step) * PI / 4.0;
			const FVector Probe = (Dir
				+ (Tangent1 * FMath::Cos(Angle) + Tangent2 * FMath::Sin(Angle)) * (0.75 * CellAngle))
				.GetSafeNormal();
			const FPlanetHydrologyField::FSample Neighbor = Field->Sample(Probe);
			const float TerrainRadius =
				float(TestRadius + double(Params.Amplitude) * double(ValleyHeight(Probe)));
			const bool bOcean = FMath::Abs(Neighbor.WaterLevelRadius - Field->GetSeaLevelRadius()) < 0.5f;
			const bool bLake = Neighbor.WaterLevelRadius > TerrainRadius + 0.5f;
			if (bOcean || bLake)
			{
				return true;
			}
		}
		return false;
	};

	int32 NumEndsInWater = 0;
	for (const FPlanetRiverPolyline& River : Rivers)
	{
		if (River.Points.Num() > 0 && HasWaterAround(River.Points.Last().Dir))
		{
			++NumEndsInWater;
		}
	}
	TestTrue(FString::Printf(TEXT("河网必须汇入海或湖（%d / %d 条河的终点贴着水面）"),
		NumEndsInWater, Rivers.Num()), NumEndsInWater > 0);

	// --- 跨面连续：水面在 cubemap 接缝两侧不得跳变 ---
	// 地形是光滑解析函数，一格的角距约 (π/2)/128 ≈ 0.012 rad，坡度最多 3 体素/rad，
	// 所以两侧的水位差应当在 0.1 体素以内。超过 2 体素就说明接缝映射错了。
	double MaxSeamJump = 0.0;
	for (int32 Face = 0; Face < PlanetBiomeCubemap::NumFaces; ++Face)
	{
		for (int32 Along = 0; Along <= 16; ++Along)
		{
			const float T = float(Along) / 16.f;
			const FVector Edge = PlanetBiomeCubemap::FaceUVToDirection(Face, 1.f, T);
			const FVector Inside = PlanetBiomeCubemap::FaceUVToDirection(Face, 0.99f, T);
			const FVector Outside = (Edge * 2.0 - Inside).GetSafeNormal();
			MaxSeamJump = FMath::Max(MaxSeamJump, FMath::Abs(
				double(Field->Sample(Inside).WaterLevelRadius)
				- double(Field->Sample(Outside).WaterLevelRadius)));
		}
	}
	TestTrue(FString::Printf(TEXT("跨面水位跳变 %.3f 必须 <= 2 体素"), MaxSeamJump),
		MaxSeamJump <= 2.0);

	return true;
}

#endif
