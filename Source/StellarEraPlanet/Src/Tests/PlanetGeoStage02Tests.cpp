// 阶段 2 是第一个改密度的阶段：陆地占比可反解、高程保界、海岸线连续、单线程与并行逐位一致。
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Stage02Plates.h"
#include "GeoRng.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	uint64 FieldsHash(const FPlanetBakeFields& F)
	{
		uint64 H = F.PlateFrame.Hash();
		const FGeoField2D* All[] = { &F.PlateId, &F.BoundaryType, &F.BoundaryDistance, &F.CrustAge,
			&F.ContinentalMask, &F.ContinentalGrad, &F.CrustType, &F.CrustThickness, &F.StyleHeight, &F.Elevation };
		for (const FGeoField2D* Field : All) { H = GeoHash::Combine(H, Field->Hash()); }
		H = GeoHash::Combine(H, GeoHash::Float(F.LandAreaFrac));
		H = GeoHash::Combine(H, GeoHash::Float(F.MinElevation));
		H = GeoHash::Combine(H, GeoHash::Float(F.MaxElevation));
		return H;
	}

	void ScanField(const FGeoField2D& F, float& OutMin, float& OutMax)
	{
		OutMin = TNumericLimits<float>::Max();
		OutMax = -TNumericLimits<float>::Max();
		for (int32 Face = 0; Face < 6; ++Face)
		for (int32 Y = 0; Y < F.Res; ++Y)
		for (int32 X = 0; X < F.Res; ++X)
		{
			const float V = F.At(Face, X, Y);
			OutMin = FMath::Min(OutMin, V);
			OutMax = FMath::Max(OutMax, V);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage02Test, "StellarEra.Planet.Geo.Stage02",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage02Test::RunTest(const FString& Parameters)
{
	FPlanetRecipeConfig C;
	C.FaceRes = 256;   // 测速度；全尺寸在 Task 5

	FPlanetBakeFields Fields;
	FPlanetBakeReport R1;
	FPlanetBakeContext C1{ C, Fields, FGeoRng::Derive(C.Seed, 2, 0), R1 };
	FStage02Plates().Bake(C1);

	TestFalse(TEXT("无错误"), R1.HasErrors());
	TestEqual(TEXT("Elevation 全分辨率"), Fields.Elevation.Res, 256);
	TestEqual(TEXT("宏观场 = FaceRes/4"), Fields.PlateId.Res, 64);
	TestEqual(TEXT("风格场 = FaceRes/4"), Fields.StyleHeight.Res, 64);
	TestTrue(TEXT("陆核非空"), Fields.PlateFrame.Cores.Num() > 0);
	TestTrue(FString::Printf(TEXT("陆地占比 0.30 ± 2%% (%.4f)"), Fields.LandAreaFrac),
		FMath::Abs(Fields.LandAreaFrac - 0.30f) <= 0.02f);

	float MinM, MaxM, MinE, MaxE;
	ScanField(Fields.ContinentalMask, MinM, MaxM);
	ScanField(Fields.Elevation, MinE, MaxE);
	TestTrue(FString::Printf(TEXT("M ∈ [0,1] 且不是常数 (Min=%f Max=%f)"), MinM, MaxM),
		MinM >= 0.f && MaxM <= 1.f && MaxM - MinM > 0.3f);

	const float Limit = C.Plates.LandTableVoxels + C.Plates.OceanBasinVoxels
		+ 1.4f * C.Plates.StyleAmpVoxels + 0.2f * C.Plates.LandTableVoxels;
	TestTrue(FString::Printf(TEXT("高程在保守界内 (Min=%f Max=%f Limit=%f)"), MinE, MaxE, Limit),
		FMath::Abs(MinE) <= Limit && FMath::Abs(MaxE) <= Limit);
	TestTrue(TEXT("记录的值域与实际一致"),
		FMath::Abs(Fields.MinElevation - MinE) < 1.f && FMath::Abs(Fields.MaxElevation - MaxE) < 1.f);

	// 穿越次数：8 条大圆 × 512 步。阈值 12 是 R=40000 的实测标定，附录的 6–10 是按小星球推的。
	int32 WorstCrossings = 0;
	FGeoRng Rng = FGeoRng::Derive(11, 0, 0);
	for (int32 Circle = 0; Circle < 8; ++Circle)
	{
		FVector3f B1(Rng.Range(-1.f, 1.f), Rng.Range(-1.f, 1.f), Rng.Range(-1.f, 1.f));
		B1 = B1.IsNearlyZero() ? FVector3f::UpVector : B1.GetSafeNormal();
		const FVector3f Ref = FMath::Abs(B1.Z) < 0.9f ? FVector3f::UpVector : FVector3f::ForwardVector;
		const FVector3f B2 = FVector3f::CrossProduct(B1, Ref).GetSafeNormal();

		int32 Crossings = 0;
		bool bPrevLand = Fields.Elevation.Sample(B1) > 0.f;
		for (int32 i = 1; i <= 512; ++i)
		{
			const float A = 2.f * PI * float(i) / 512.f;
			const bool bLand = Fields.Elevation.Sample(B1 * FMath::Cos(A) + B2 * FMath::Sin(A)) > 0.f;
			if (bLand != bPrevLand) { ++Crossings; }
			bPrevLand = bLand;
		}
		WorstCrossings = FMath::Max(WorstCrossings, Crossings);
	}
	TestTrue(FString::Printf(TEXT("穿越次数 ∈ [2,12]（最差 %d）"), WorstCrossings),
		WorstCrossings >= 2 && WorstCrossings <= 12);

	// 海岸线：M 跨过 0.5 的相邻 texel 对之间线性插值出海岸方向，那里的 h_base 应贴着海平面。
	// 两处口径修正：断言 h_base = Elevation − StyleHeight——风格项最大 ±216 体素，本来就把高程推离
	// 海平面；且必须在 FaceRes=1024 上验——256 的 texel 脚印 312 体素已宽于陆坡 240 体素，
	// 高程场表示不了这段坡，双线性混合两片饱和台阶会给出 A_up 量级的假高程。
	FPlanetRecipeConfig CoastCfg = C;
	CoastCfg.FaceRes = 1024;
	FPlanetBakeFields CoastFields;
	FPlanetBakeReport CoastReport;
	FPlanetBakeContext CoastCtx{ CoastCfg, CoastFields, FGeoRng::Derive(CoastCfg.Seed, 2, 0), CoastReport };
	FStage02Plates().Bake(CoastCtx);
	TestFalse(TEXT("海岸线烘焙无错误"), CoastReport.HasErrors());

	int32 CoastSamples = 0;
	float WorstCoastBase = 0.f, WorstCoastE = 0.f;
	const FGeoField2D& Mask = CoastFields.ContinentalMask;
	for (int32 Face = 0; Face < 6; ++Face)
	for (int32 Y = 0; Y < Mask.Res; ++Y)
	for (int32 X = 0; X + 1 < Mask.Res; ++X)
	{
		const float A0 = Mask.At(Face, X, Y), A1 = Mask.At(Face, X + 1, Y);
		if ((A0 - 0.5f) * (A1 - 0.5f) >= 0.f) { continue; }
		const float T = (0.5f - A0) / (A1 - A0);
		const FVector3f D0 = Mask.TexelDir(Face, X, Y);
		const FVector3f D1 = Mask.TexelDir(Face, X + 1, Y);
		const FVector3f D = FMath::Lerp(D0, D1, T).GetSafeNormal();
		const float E = CoastFields.Elevation.Sample(D);
		WorstCoastE = FMath::Max(WorstCoastE, FMath::Abs(E));
		WorstCoastBase = FMath::Max(WorstCoastBase, FMath::Abs(E - CoastFields.StyleHeight.Sample(D)));
		++CoastSamples;
	}
	TestTrue(TEXT("确实找到了海岸样本"), CoastSamples > 0);
	const float CoastLimit = 0.3f * C.Plates.LandTableVoxels;
	TestTrue(FString::Printf(TEXT("海岸处 |h_base| < 0.3·A_up（最差 %.1f，阈值 %.1f）"), WorstCoastBase, CoastLimit),
		WorstCoastBase < CoastLimit);

	// 年龄场：洋壳 texel 按 BoundaryDistance 分两半，离边界远的一半 CrustAge 均值更大。
	TArray<TPair<float, float>> Ocean;   // (BoundaryDistance, CrustAge)
	for (int32 Face = 0; Face < 6; ++Face)
	for (int32 Y = 0; Y < Fields.CrustType.Res; ++Y)
	for (int32 X = 0; X < Fields.CrustType.Res; ++X)
	{
		if (Fields.CrustType.At(Face, X, Y) == 0.f)
		{
			Ocean.Emplace(Fields.BoundaryDistance.At(Face, X, Y), Fields.CrustAge.At(Face, X, Y));
		}
	}
	Ocean.Sort([](const TPair<float, float>& A, const TPair<float, float>& B) { return A.Key < B.Key; });
	const int32 Half = Ocean.Num() / 2;
	float NearSum = 0.f, FarSum = 0.f;
	for (int32 i = 0; i < Half; ++i) { NearSum += Ocean[i].Value; }
	for (int32 i = Half; i < Ocean.Num(); ++i) { FarSum += Ocean[i].Value; }
	const float NearMean = Half > 0 ? NearSum / float(Half) : 0.f;
	const float FarMean = Ocean.Num() - Half > 0 ? FarSum / float(Ocean.Num() - Half) : 0.f;
	TestTrue(FString::Printf(TEXT("远离边界的洋壳更老 (near=%.3f far=%.3f)"), NearMean, FarMean), FarMean > NearMean);

	// 确定性：连烘两次逐位一致；单线程与并行逐位一致。
	FPlanetBakeFields Fields2;
	FPlanetBakeReport R2;
	FPlanetBakeContext C2{ C, Fields2, FGeoRng::Derive(C.Seed, 2, 0), R2 };
	FStage02Plates().Bake(C2);
	TestTrue(TEXT("同配方两次烘焙场哈希一致"), FieldsHash(Fields) == FieldsHash(Fields2));

	FPlanetRecipeConfig Single = C;
	Single.bSingleThreadBake = true;
	FPlanetBakeFields Fields3;
	FPlanetBakeReport R3;
	FPlanetBakeContext C3{ Single, Fields3, FGeoRng::Derive(Single.Seed, 2, 0), R3 };
	FStage02Plates().Bake(C3);
	TestTrue(TEXT("单线程与并行 Elevation 逐位一致"), Fields3.Elevation.Hash() == Fields.Elevation.Hash());
	TestTrue(TEXT("单线程与并行全部场逐位一致"), FieldsHash(Fields3) == FieldsHash(Fields));

	AddInfo(FString::Printf(TEXT("核数 %d，陆地占比 %.4f，M ∈ [%.3f, %.3f]，高程 ∈ [%.1f, %.1f]"),
		Fields.PlateFrame.Cores.Num(), Fields.LandAreaFrac, MinM, MaxM, MinE, MaxE));
	AddInfo(FString::Printf(TEXT("8 条大圆最差穿越 %d，海岸样本 %d，最差海岸 |h_base| %.1f（含风格 |E| %.1f）"),
		WorstCrossings, CoastSamples, WorstCoastBase, WorstCoastE));
	return true;
}

#endif
