// 阶段 7（出露结算）：抬升后不得高于地表，形状逐位不变，且确有原语被抬到出露。
#include "CoreMinimal.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "GeoMineralRules.h"
#include "GeoPrimitive.h"
#include "GeoRng.h"
#include "PlanetBakePipeline.h"
#include "Stage02Plates.h"
#include "Stage03Terrain.h"
#include "Stage04Magmatism.h"
#include "Stage05Climate.h"
#include "Stage06Erosion.h"
#include "Stage06bExposure.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** 跑 2→3→4（含撒矿）→5→6，留住阶段 6 的原语副本，再跑阶段 7；两份原语都给出。 */
	void Stage06bBake(const FPlanetRecipeConfig& C, FPlanetBakeFields& F, TArray<FGeoPrimitive>& Before, FPlanetBakeReport& R, double& OutStage7Sec)
	{
		FPlanetBakeContext C2{ C, F, FGeoRng::Derive(C.Seed, 2, 0), R }; FStage02Plates().Bake(C2);
		FPlanetBakeContext C3{ C, F, FGeoRng::Derive(C.Seed, 3, 0), R }; FStage03Terrain().Bake(C3);
		FPlanetBakeContext C4{ C, F, FGeoRng::Derive(C.Seed, 4, 0), R }; FStage04Magmatism().Bake(C4);
		// 撒矿流必须与管线逐位一致：规则挂在阶段 4 之后跑，否则这里的矿体只是测试自造的。
		for (const TSharedPtr<const IGeoMineralRule>& Rule : C.MineralRules)
		{
			if (!Rule) { continue; }
			const uint64 Purpose = GeoHash::Combine(GeoHash::Combine(GeoHash::Str("scatter"), Rule->HashParams()), uint64(Rule->SeedOffset));
			FGeoScatterContext SC{ C, F, FGeoRng::Derive(C.Seed, Rule->GetStage(), Purpose), R.Errors };
			FGeoScatterOutput Out;
			Rule->Scatter(SC, Out);
			for (FGeoPrimitive& B : Out.Bodies) { B.MineralId = Rule->MineralId; F.Primitives.Add(B); }
			for (FGeoMineralOverlay& O : Out.Overlays) { O.MineralId = Rule->MineralId; F.MineralOverlays.Add(MoveTemp(O)); }
		}
		FPlanetBakeContext C5{ C, F, FGeoRng::Derive(C.Seed, 5, 0), R }; FStage05Climate().Bake(C5);
		FPlanetBakeContext C6{ C, F, FGeoRng::Derive(C.Seed, 6, 0), R }; FStage06Erosion().Bake(C6);
		Before = F.Primitives;
		FPlanetBakeContext C7{ C, F, FGeoRng::Derive(C.Seed, 7, 0), R };
		const double T7 = FPlatformTime::Seconds();
		FStage06bExposure().Bake(C7);
		OutStage7Sec = FPlatformTime::Seconds() - T7;
	}

	/** 按阶段同一规则找宿主：中心落在椭球内、归一化距离最小（平手取编号小者）；没有返回 INDEX_NONE。 */
	int32 Stage06bHostIndex(const TArray<FGeoPrimitive>& Prims, int32 FollowerIdx)
	{
		const FGeoPrimitive& Follower = Prims[FollowerIdx];
		int32 Host = INDEX_NONE;
		float BestD = 1.f;
		for (int32 h = 0; h < Prims.Num(); ++h)
		{
			const FGeoPrimitive& Pr = Prims[h];
			if (Pr.MineralId != 0 || Pr.LithologyId == 0) { continue; }
			const float D = GeoPrimitiveDistance(Pr, Follower.Center);
			if (D < BestD) { BestD = D; Host = h; }
		}
		return Host;
	}

	bool Stage06bIsHalo(const FGeoPrimitive& P)
	{
		return P.MineralId == 0 && P.LithologyId == 0 && P.ClueId != 0;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage06bLiftTest, "StellarEra.Planet.Geo.Stage06b.Lift",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage06bLiftTest::RunTest(const FString& Parameters)
{
	FPlanetRecipeConfig C;
	C.FaceRes = 256;
	{
		TSharedPtr<FGeoPorphyryRule> Porphyry = MakeShared<FGeoPorphyryRule>(); Porphyry->MineralId = 1;
		TSharedPtr<FGeoPegmatiteRule> Pegmatite = MakeShared<FGeoPegmatiteRule>(); Pegmatite->MineralId = 2;
		C.MineralRules.Add(Porphyry);
		C.MineralRules.Add(Pegmatite);
	}
	FPlanetBakeReport R;
	FPlanetBakeFields F;
	TArray<FGeoPrimitive> Before;
	double Stage7Sec = 0.0;
	Stage06bBake(C, F, Before, R, Stage7Sec);
	if (!TestFalse(FString::Printf(TEXT("无错误（%s）"), *FString::Join(R.Errors, TEXT(" | "))), R.HasErrors())) { return false; }
	if (!TestEqual(TEXT("原语数不变"), F.Primitives.Num(), Before.Num())) { return false; }

	// 形状字段逐位不变：出露只搬中心，改形会让矿物材质与体素存档全部失配。
	int32 ShapeDiff = 0;
	for (int32 i = 0; i < Before.Num(); ++i)
	{
		const FGeoPrimitive& A = Before[i];
		const FGeoPrimitive& B = F.Primitives[i];
		const bool bSame = FMemory::Memcmp(&A.Radii, &B.Radii, sizeof(FVector3f)) == 0
			&& FMemory::Memcmp(&A.Rotation, &B.Rotation, sizeof(FQuat4f)) == 0
			&& FMemory::Memcmp(&A.Hard, &B.Hard, sizeof(float)) == 0
			&& A.MineralId == B.MineralId && A.Grade == B.Grade;
		ShapeDiff += bSame ? 0 : 1;
	}
	TestEqual(TEXT("Radii / Rotation / Hard / MineralId / Grade 逐位不变"), ShapeDiff, 0);

	// 不高于地表。
	int32 Above = 0, Exposed = 0, OreMiss = 0, HaloMiss = 0, HaloCount = 0, Lifted = 0;
	int32 HaloDrift = 0;
	float WorstOver = 0.f, MaxExposure = 0.f, MaxLiftVox = 0.f;
	double LiftSum = 0.0;
	int32 Hist[6] = { 0 };
	for (int32 i = 0; i < F.Primitives.Num(); ++i)
	{
		const FGeoPrimitive& P = F.Primitives[i];
		const FVector3f Dir = P.Center.GetSafeNormal();
		if (Dir.IsNearlyZero()) { continue; }
		const float S = float(C.Radius) + F.Elevation.Sample(Dir);
		const float Over = P.Center.Size() + GeoPrimitiveSupport(P, Dir) - S;
		if (Over > 1.f) { ++Above; WorstOver = FMath::Max(WorstOver, Over); }
		const float Exp = float(P.Exposure) / 255.f;
		MaxExposure = FMath::Max(MaxExposure, Exp);
		Exposed += Exp > 0.5f ? 1 : 0;
		++Hist[Exp <= 0.f ? 0 : (Exp < 0.25f ? 1 : (Exp < 0.5f ? 2 : (Exp < 0.75f ? 3 : (Exp < 1.f ? 4 : 5))))];
		const float LiftVox = (P.Center - Before[i].Center).Size();
		LiftSum += LiftVox;
		MaxLiftVox = FMath::Max(MaxLiftVox, LiftVox);
		Lifted += LiftVox > 0.5f ? 1 : 0;
		const int32 Host = Stage06bHostIndex(Before, i);
		if (P.MineralId != 0 && Host == INDEX_NONE) { ++OreMiss; }
		if (Stage06bIsHalo(Before[i]))
		{
			++HaloCount;
			if (Host == INDEX_NONE) { ++HaloMiss; }
			// 晕必须和宿主同进同退：分开抬会让晕描述几百体素外的空气。
			else if (FMath::Abs((P.Center - Before[i].Center).Size() - (F.Primitives[Host].Center - Before[Host].Center).Size()) > 1.f)
			{
				++HaloDrift;
			}
		}
	}
	TestEqual(FString::Printf(TEXT("全部原语 Top ≤ S + 1（超出的 %d 个，最差 %.3f 体素）"), Above, WorstOver), Above, 0);
	TestTrue(FString::Printf(TEXT("至少一个原语 Exposure > 0.5（%d 个，最大 %.3f）"), Exposed, MaxExposure), Exposed > 0);
	TestEqual(FString::Printf(TEXT("每个有宿主的蚀变晕位移与宿主相差 ≤ 1 体素（漂移 %d / %d）"), HaloDrift, HaloCount), HaloDrift, 0);
	AddInfo(FString::Printf(TEXT("原语 %d（矿体 %d，蚀变晕 %d），出露 >0.5 的 %d 个，最大出露 %.3f，无宿主矿体 %d 个、蚀变晕 %d 个，阶段 7 用时 %.1f ms"),
		F.Primitives.Num(), [&]() { int32 N = 0; for (const FGeoPrimitive& P : Before) { N += P.MineralId != 0 ? 1 : 0; } return N; }(), HaloCount,
		Exposed, MaxExposure, OreMiss, HaloMiss, Stage7Sec * 1000.0));
	AddInfo(FString::Printf(TEXT("出露直方图：0=%d，(0,0.25)=%d，[0.25,0.5)=%d，[0.5,0.75)=%d，[0.75,1)=%d，1=%d；位移 %d 个，均值 %.1f，最大 %.1f 体素"),
		Hist[0], Hist[1], Hist[2], Hist[3], Hist[4], Hist[5], Lifted, F.Primitives.Num() > 0 ? LiftSum / F.Primitives.Num() : 0.0, MaxLiftVox));
	return true;
}

#endif
