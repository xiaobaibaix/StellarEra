// 阶段 2 是第一个改密度的阶段：陆地占比精确命中、高程保界、壳型与高程同源、单线程与并行逐位一致。
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
			&F.ContinentalMask, &F.ContinentalGrad, &F.CrustType, &F.CrustThickness, &F.Elevation };
		for (const FGeoField2D* Field : All) { H = GeoHash::Combine(H, Field->Hash()); }
		H = GeoHash::Combine(H, GeoHash::Float(F.LandAreaFrac));
		H = GeoHash::Combine(H, GeoHash::Float(F.CrustSplitElevation));
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
	TestTrue(FString::Printf(TEXT("加权陆地占比 0.30 ± 0.01 (%.4f)"), Fields.LandAreaFrac),
		FMath::Abs(Fields.LandAreaFrac - C.Continents.LandFrac) <= 0.01f);

	float MinM, MaxM, MinE, MaxE;
	ScanField(Fields.ContinentalMask, MinM, MaxM);
	ScanField(Fields.Elevation, MinE, MaxE);
	TestTrue(FString::Printf(TEXT("M ∈ [0,1] 且跨过海岸 (Min=%f Max=%f)"), MinM, MaxM),
		MinM >= 0.f && MaxM <= 1.f && MinM < 0.1f && MaxM > 0.9f);
	// 模糊只会把端点往里收：上下界不越曲线端点，且仍有深海与高地。
	TestTrue(FString::Printf(TEXT("高程在曲线端点内且非退化 (Min=%.1f Max=%.1f)"), MinE, MaxE),
		MinE >= -(C.Continents.OceanDepthRatio * float(C.Radius)) - 1e-3f && MaxE <= (C.Continents.LandHeightRatio * float(C.Radius)) + 1e-3f
		&& MinE < -0.8f * (C.Continents.OceanDepthRatio * float(C.Radius)) && MaxE > 0.5f * (C.Continents.LandHeightRatio * float(C.Radius)));
	TestTrue(TEXT("记录的值域与实际一致"),
		FMath::Abs(Fields.MinElevation - MinE) < 1e-3f && FMath::Abs(Fields.MaxElevation - MaxE) < 1e-3f);

	// 壳型与高程同源：CrustType 只由 E 与 CrustSplitElevation 决定，二者分叉会让阶段 3 读错背景。
	{
		int32 Bad = 0, Shelf = 0;
		for (int32 Face = 0; Face < 6; ++Face)
		for (int32 Y = 0; Y < Fields.CrustType.Res; ++Y)
		for (int32 X = 0; X < Fields.CrustType.Res; ++X)
		{
			const float E = Fields.Elevation.Sample(Fields.CrustType.TexelDir(Face, X, Y));
			const float Want = E > 0.f ? 1.f : (E >= Fields.CrustSplitElevation ? 2.f : 0.f);
			Bad += Fields.CrustType.At(Face, X, Y) != Want ? 1 : 0;
			Shelf += Want == 2.f ? 1 : 0;
		}
		TestEqual(TEXT("CrustType 与高程一致"), Bad, 0);
		TestTrue(FString::Printf(TEXT("存在陆架 texel（%d）"), Shelf), Shelf > 0);
	}

	// 穿越次数只设下限：上限由格局参数决定（群岛本来就多），不在这里卡。
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
	TestTrue(FString::Printf(TEXT("最差穿越 ≥ 2（%d）"), WorstCrossings), WorstCrossings >= 2);

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

	AddInfo(FString::Printf(TEXT("陆地占比 %.4f，M ∈ [%.3f, %.3f]，高程 ∈ [%.1f, %.1f]，最差穿越 %d"),
		Fields.LandAreaFrac, MinM, MaxM, MinE, MaxE, WorstCrossings));
	return true;
}

#endif
