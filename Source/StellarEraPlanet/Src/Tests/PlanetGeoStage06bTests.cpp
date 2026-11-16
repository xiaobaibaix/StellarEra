// 阶段 7（出露结算）：抬升后不得高于地表，形状逐位不变，且有可见出露。
// 另验宿主跟随、出露公式、原始深度、包围盒平移、零剥蚀、山带出露与开关 / 确定性。
#include "CoreMinimal.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "GeoMineralRules.h"
#include "GeoPrimitive.h"
#include "GeoPrimitiveIndex.h"
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
	/** 三条真实撒矿器：斑岩（深）、伟晶岩（岩基边缘）、浅成低温（近地表脉），矿体深度覆盖足够宽。 */
	FPlanetRecipeConfig Stage06bConfig()
	{
		FPlanetRecipeConfig C;
		C.FaceRes = 256;
		TSharedPtr<FGeoPorphyryRule> Porphyry = MakeShared<FGeoPorphyryRule>(); Porphyry->MineralId = 1;
		TSharedPtr<FGeoPegmatiteRule> Pegmatite = MakeShared<FGeoPegmatiteRule>(); Pegmatite->MineralId = 2;
		TSharedPtr<FGeoEpithermalRule> Epithermal = MakeShared<FGeoEpithermalRule>(); Epithermal->MineralId = 3;
		C.MineralRules.Add(Porphyry);
		C.MineralRules.Add(Pegmatite);
		C.MineralRules.Add(Epithermal);
		return C;
	}

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

	bool Stage06bIsRock(const FGeoPrimitive& P) { return P.MineralId == 0 && P.LithologyId != 0; }
	bool Stage06bIsHalo(const FGeoPrimitive& P) { return P.MineralId == 0 && P.LithologyId == 0 && P.ClueId != 0; }

	/** 按阶段同一规则找宿主：中心落在椭球内、归一化距离最小（平手取编号小者）；没有返回 INDEX_NONE。 */
	int32 Stage06bHostIndex(const TArray<FGeoPrimitive>& Prims, int32 FollowerIdx)
	{
		const FGeoPrimitive& Follower = Prims[FollowerIdx];
		int32 Host = INDEX_NONE;
		float BestD = 1.f;
		for (int32 h = 0; h < Prims.Num(); ++h)
		{
			const FGeoPrimitive& Pr = Prims[h];
			if (!Stage06bIsRock(Pr)) { continue; }
			const float D = GeoPrimitiveDistance(Pr, Follower.Center);
			if (D < BestD) { BestD = D; Host = h; }
		}
		return Host;
	}

	/** 与阶段同一公式的独立重算：抓「采样方向 / 夹取上限 / 缩放比例」被改坏。 */
	float Stage06bLiftAt(const FPlanetRecipeConfig& C, const FPlanetBakeFields& F, const FVector3f& Center)
	{
		const FVector3f Dir = Center.GetSafeNormal();
		if (Dir.IsNearlyZero()) { return 0.f; }
		const float MaxLift = C.Exposure.MaxLiftRatio * float(C.Radius);
		return FMath::Clamp(C.Exposure.LiftScale * F.ErosionDepth.Sample(Dir), 0.f, MaxLift);
	}

	/** 岩体按自身方向；矿体与晕跟随宿主；其余原语不抬。 */
	float Stage06bExpectedLift(const FPlanetRecipeConfig& C, const FPlanetBakeFields& F, const TArray<FGeoPrimitive>& Before, int32 Idx)
	{
		const FGeoPrimitive& P = Before[Idx];
		if (Stage06bIsRock(P)) { return Stage06bLiftAt(C, F, P.Center); }
		if (P.MineralId == 0 && !Stage06bIsHalo(P)) { return 0.f; }
		const int32 Host = Stage06bHostIndex(Before, Idx);
		return Host != INDEX_NONE ? Stage06bLiftAt(C, F, Before[Host].Center) : Stage06bLiftAt(C, F, P.Center);
	}

	float Stage06bSurface(const FPlanetRecipeConfig& C, const FPlanetBakeFields& F, const FVector3f& Dir)
	{
		return float(C.Radius) + F.Elevation.Sample(Dir);
	}

	uint64 Stage06bPrimHash(const TArray<FGeoPrimitive>& Prims)
	{
		FGeoPrimitiveIndex I; I.Build(Prims); return I.Hash();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage06bLiftTest, "StellarEra.Planet.Geo.Stage06b.Lift",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage06bLiftTest::RunTest(const FString& Parameters)
{
	const FPlanetRecipeConfig C = Stage06bConfig();
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
	int32 Above = 0, Exposed = 0, OreExposed = 0, OreCount = 0, OreMiss = 0, HaloMiss = 0, HaloCount = 0, Lifted = 0;
	int32 HaloDrift = 0;
	float WorstOver = 0.f, MaxExposure = 0.f, MaxLiftVox = 0.f;
	double LiftSum = 0.0;
	int32 Hist[6] = { 0 };
	for (int32 i = 0; i < F.Primitives.Num(); ++i)
	{
		const FGeoPrimitive& P = F.Primitives[i];
		const FVector3f Dir = P.Center.GetSafeNormal();
		if (Dir.IsNearlyZero()) { continue; }
		const float S = Stage06bSurface(C, F, Dir);
		const float Over = P.Center.Size() + GeoPrimitiveSupport(P, Dir) - S;
		if (Over > 1.f) { ++Above; WorstOver = FMath::Max(WorstOver, Over); }
		const float Exp = float(P.Exposure) / 255.f;
		MaxExposure = FMath::Max(MaxExposure, Exp);
		Exposed += Exp > 0.5f ? 1 : 0;
		if (P.MineralId != 0) { ++OreCount; OreExposed += Exp > 0.5f ? 1 : 0; }
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
	TestTrue(FString::Printf(TEXT("至少一个矿体 Exposure > 0.5（%d / %d）"), OreExposed, OreCount), OreExposed > 0);
	TestEqual(FString::Printf(TEXT("每个有宿主的蚀变晕位移与宿主相差 ≤ 1 体素（漂移 %d / %d）"), HaloDrift, HaloCount), HaloDrift, 0);
	AddInfo(FString::Printf(TEXT("原语 %d（矿体 %d，蚀变晕 %d），出露 >0.5：全部 %d 个、矿体 %d 个，最大出露 %.3f，无宿主矿体 %d 个、蚀变晕 %d 个，阶段 7 用时 %.1f ms"),
		F.Primitives.Num(), OreCount, HaloCount, Exposed, OreExposed, MaxExposure, OreMiss, HaloMiss, Stage7Sec * 1000.0));
	AddInfo(FString::Printf(TEXT("出露直方图：0=%d，(0,0.25)=%d，[0.25,0.5)=%d，[0.5,0.75)=%d，[0.75,1)=%d，1=%d；位移 %d 个，均值 %.1f，最大 %.1f 体素"),
		Hist[0], Hist[1], Hist[2], Hist[3], Hist[4], Hist[5], Lifted, F.Primitives.Num() > 0 ? LiftSum / F.Primitives.Num() : 0.0, MaxLiftVox));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage06bHostFollowTest, "StellarEra.Planet.Geo.Stage06b.HostFollow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage06bHostFollowTest::RunTest(const FString& Parameters)
{
	const FPlanetRecipeConfig C = Stage06bConfig();
	FPlanetBakeReport R;
	FPlanetBakeFields F;
	TArray<FGeoPrimitive> Before;
	double Stage7Sec = 0.0;
	Stage06bBake(C, F, Before, R, Stage7Sec);
	if (!TestFalse(FString::Printf(TEXT("无错误（%s）"), *FString::Join(R.Errors, TEXT(" | "))), R.HasErrors())) { return false; }

	int32 OreCount = 0, OreMiss = 0, OreDrift = 0, HaloCount = 0, HaloMiss = 0, HaloDrift = 0;
	int32 OreOwnDiff = 0;
	float WorstOre = 0.f, WorstHalo = 0.f, MaxOwnDiff = 0.f;
	for (int32 i = 0; i < F.Primitives.Num(); ++i)
	{
		const FGeoPrimitive& P = F.Primitives[i];
		const int32 Host = Stage06bHostIndex(Before, i);
		if (P.MineralId != 0)
		{
			++OreCount;
			if (Host == INDEX_NONE) { ++OreMiss; continue; }
			const float D = FMath::Abs((P.Center - Before[i].Center).Size() - (F.Primitives[Host].Center - Before[Host].Center).Size());
			WorstOre = FMath::Max(WorstOre, D);
			OreDrift += D > 1.f ? 1 : 0;
			// 自身方向与宿主方向的 D 若处处一样，「跟宿主」这条断言分不出对错。
			const float OwnDiff = FMath::Abs(Stage06bLiftAt(C, F, Before[i].Center) - Stage06bLiftAt(C, F, Before[Host].Center));
			MaxOwnDiff = FMath::Max(MaxOwnDiff, OwnDiff);
			OreOwnDiff += OwnDiff > 1.f ? 1 : 0;
		}
		if (Stage06bIsHalo(Before[i]))
		{
			++HaloCount;
			if (Host == INDEX_NONE) { ++HaloMiss; continue; }
			const float D = FMath::Abs((P.Center - Before[i].Center).Size() - (F.Primitives[Host].Center - Before[Host].Center).Size());
			WorstHalo = FMath::Max(WorstHalo, D);
			HaloDrift += D > 1.f ? 1 : 0;
		}
	}
	TestTrue(FString::Printf(TEXT("样本里有蚀变晕（%d 个），晕分支不得空转"), HaloCount), HaloCount > 0);
	TestTrue(FString::Printf(TEXT("至少一个矿体的自身方向抬升与宿主相差 > 1 体素（%d 个，最大 %.2f），否则跟随分支测不出错"),
		OreOwnDiff, MaxOwnDiff), OreOwnDiff > 0);
	TestEqual(FString::Printf(TEXT("每个有宿主的矿体位移与宿主相差 ≤ 1 体素（漂移 %d / %d，最差 %.3f）"), OreDrift, OreCount - OreMiss, WorstOre), OreDrift, 0);
	TestEqual(FString::Printf(TEXT("每个有宿主的蚀变晕位移与宿主相差 ≤ 1 体素（漂移 %d / %d，最差 %.3f）"), HaloDrift, HaloCount - HaloMiss, WorstHalo), HaloDrift, 0);
	AddInfo(FString::Printf(TEXT("矿体 %d（无宿主 %d），蚀变晕 %d（无宿主 %d）；最差位移差：矿体 %.3f、晕 %.3f 体素；自身方向与宿主抬升差 > 1 的矿体 %d 个（最大 %.2f）"),
		OreCount, OreMiss, HaloCount, HaloMiss, WorstOre, WorstHalo, OreOwnDiff, MaxOwnDiff));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage06bExposureFormulaTest, "StellarEra.Planet.Geo.Stage06b.ExposureFormula",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage06bExposureFormulaTest::RunTest(const FString& Parameters)
{
	const FPlanetRecipeConfig C = Stage06bConfig();
	FPlanetBakeReport R;
	FPlanetBakeFields F;
	TArray<FGeoPrimitive> Before;
	double Stage7Sec = 0.0;
	Stage06bBake(C, F, Before, R, Stage7Sec);
	if (!TestFalse(FString::Printf(TEXT("无错误（%s）"), *FString::Join(R.Errors, TEXT(" | "))), R.HasErrors())) { return false; }

	int32 Checked = 0, Capped = 0, BadFormula = 0, BadCap = 0;
	float MaxErr = 0.f;
	for (int32 i = 0; i < F.Primitives.Num(); ++i)
	{
		const FGeoPrimitive& B = Before[i];
		const FVector3f Dir = B.Center.GetSafeNormal();
		if (Dir.IsNearlyZero()) { continue; }
		const float Support = GeoPrimitiveSupport(B, Dir);
		const float BeforeTop = B.Center.Size() + Support;
		const float S = Stage06bSurface(C, F, Dir);
		const float Lift = Stage06bExpectedLift(C, F, Before, i);
		const float Exp = float(F.Primitives[i].Exposure) / 255.f;
		if (BeforeTop + Lift > S)
		{
			++Capped;
			BadCap += F.Primitives[i].Exposure == 255 ? 0 : 1;
			continue;
		}
		++Checked;
		const float Expected = FMath::Clamp(Lift / FMath::Max(2.f * Support, 1.f), 0.f, 1.f);
		const float Err = FMath::Abs(Exp - Expected);
		MaxErr = FMath::Max(MaxErr, Err);
		BadFormula += Err > 0.01f ? 1 : 0;
	}
	TestTrue(FString::Printf(TEXT("至少一个原语进了收回分支（%d 个）"), Capped), Capped > 0);
	TestEqual(FString::Printf(TEXT("收回分支的 Exposure 全为 255（不符 %d 个）"), BadCap), BadCap, 0);
	TestEqual(FString::Printf(TEXT("未收回原语按 Lift/(2·Support) 计算出露（超差 %d / %d，最差 %.4f）"), BadFormula, Checked, MaxErr), BadFormula, 0);
	AddInfo(FString::Printf(TEXT("公式核对 %d 个（最差 %.4f），收回分支 %d 个"), Checked, MaxErr, Capped));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage06bRawDepthTest, "StellarEra.Planet.Geo.Stage06b.RawDepth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage06bRawDepthTest::RunTest(const FString& Parameters)
{
	const FPlanetRecipeConfig C = Stage06bConfig();
	FPlanetBakeReport R;
	FPlanetBakeFields F;
	TArray<FGeoPrimitive> Before;
	double Stage7Sec = 0.0;
	Stage06bBake(C, F, Before, R, Stage7Sec);
	if (!TestFalse(FString::Printf(TEXT("无错误（%s）"), *FString::Join(R.Errors, TEXT(" | "))), R.HasErrors())) { return false; }

	int32 Bad = 0, Negative = 0;
	float MaxErr = 0.f, MostNegative = 0.f;
	for (int32 i = 0; i < F.Primitives.Num(); ++i)
	{
		const FGeoPrimitive& B = Before[i];
		const FVector3f Dir = B.Center.GetSafeNormal();
		if (Dir.IsNearlyZero()) { continue; }
		const float Expected = Stage06bSurface(C, F, Dir) - (B.Center.Size() + GeoPrimitiveSupport(B, Dir));
		const float Err = FMath::Abs(F.Primitives[i].RawDepthBefore - Expected);
		MaxErr = FMath::Max(MaxErr, Err);
		Bad += Err > 1.f ? 1 : 0;
		if (F.Primitives[i].RawDepthBefore < 0.f) { ++Negative; MostNegative = FMath::Min(MostNegative, F.Primitives[i].RawDepthBefore); }
	}
	TestEqual(FString::Printf(TEXT("RawDepthBefore = S −（|Center| + Support），误差 > 1 体素的 %d 个（最差 %.3f）"), Bad, MaxErr), Bad, 0);
	AddInfo(FString::Printf(TEXT("RawDepthBefore 核对 %d 个（最差 %.3f 体素）；负值 %d 个，最负 %.1f"),
		F.Primitives.Num(), MaxErr, Negative, MostNegative));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage06bBoundsTest, "StellarEra.Planet.Geo.Stage06b.Bounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage06bBoundsTest::RunTest(const FString& Parameters)
{
	const FPlanetRecipeConfig C = Stage06bConfig();
	FPlanetBakeReport R;
	FPlanetBakeFields F;
	TArray<FGeoPrimitive> Before;
	double Stage7Sec = 0.0;
	Stage06bBake(C, F, Before, R, Stage7Sec);
	if (!TestFalse(FString::Printf(TEXT("无错误（%s）"), *FString::Join(R.Errors, TEXT(" | "))), R.HasErrors())) { return false; }

	// 1e-3 是「明显没搬 / 被重算」的判据；R=40000 时 float 的 ulp 约 0.005，纯绝对容差会被量化误差顶破。
	const float Tol = 1e-3f + 1e-6f * float(C.Radius);
	int32 Moved = 0, BadCenter = 0, BadExtent = 0;
	float MaxCenterErr = 0.f, MaxExtentErr = 0.f;
	for (int32 i = 0; i < F.Primitives.Num(); ++i)
	{
		const FGeoPrimitive& A = Before[i];
		const FGeoPrimitive& B = F.Primitives[i];
		const float LiftVox = (B.Center - A.Center).Size();
		if (LiftVox <= 0.5f) { continue; }
		++Moved;
		const FVector3f Delta = B.Center - A.Center;
		const FVector3f BoundsDelta = B.Bounds.GetCenter() - A.Bounds.GetCenter();
		const float CenterErr = (BoundsDelta - Delta).Size();
		MaxCenterErr = FMath::Max(MaxCenterErr, CenterErr);
		BadCenter += CenterErr > Tol ? 1 : 0;
		const float ExtentErr = (B.Bounds.GetExtent() - A.Bounds.GetExtent()).Size();
		MaxExtentErr = FMath::Max(MaxExtentErr, ExtentErr);
		BadExtent += ExtentErr > Tol ? 1 : 0;
	}
	TestTrue(FString::Printf(TEXT("样本里有被抬升的原语（%d 个）"), Moved), Moved > 0);
	TestEqual(FString::Printf(TEXT("包围盒中心位移 = 中心位移（超差 %d / %d，最差 %.4f，容差 %.4f）"), BadCenter, Moved, MaxCenterErr, Tol), BadCenter, 0);
	TestEqual(FString::Printf(TEXT("包围盒半轴不变（超差 %d / %d，最差 %.4f）"), BadExtent, Moved, MaxExtentErr), BadExtent, 0);
	AddInfo(FString::Printf(TEXT("包围盒核对 %d 个；中心位移误差最大 %.4f（容差 %.4f），半轴误差最大 %.4f"), Moved, MaxCenterErr, Tol, MaxExtentErr));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage06bZeroErosionTest, "StellarEra.Planet.Geo.Stage06b.ZeroErosion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage06bZeroErosionTest::RunTest(const FString& Parameters)
{
	FPlanetRecipeConfig C = Stage06bConfig();
	C.Exposure.LiftScale = 0.f;
	FPlanetBakeReport R;
	FPlanetBakeFields F;
	TArray<FGeoPrimitive> Before;
	double Stage7Sec = 0.0;
	Stage06bBake(C, F, Before, R, Stage7Sec);
	if (!TestFalse(FString::Printf(TEXT("无错误（%s）"), *FString::Join(R.Errors, TEXT(" | "))), R.HasErrors())) { return false; }

	int32 Displaced = 0, Exposed = 0;
	float MaxMove = 0.f;
	float MaxExposure = 0.f;
	for (int32 i = 0; i < F.Primitives.Num(); ++i)
	{
		MaxMove = FMath::Max(MaxMove, (F.Primitives[i].Center - Before[i].Center).Size());
		Displaced += (F.Primitives[i].Center - Before[i].Center).SizeSquared() > 1e-6f ? 1 : 0;
		Exposed += F.Primitives[i].Exposure != 0 ? 1 : 0;
		MaxExposure = FMath::Max(MaxExposure, float(F.Primitives[i].Exposure) / 255.f);
	}
	TestEqual(FString::Printf(TEXT("LiftScale=0 时零位移（位移 %d 个，最大 %.4f 体素）"), Displaced, MaxMove), Displaced, 0);
	TestEqual(FString::Printf(TEXT("LiftScale=0 时 Exposure 全 0（非零 %d 个，最大 %.3f）"), Exposed, MaxExposure), Exposed, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage06bMountainsTest, "StellarEra.Planet.Geo.Stage06b.Mountains",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage06bMountainsTest::RunTest(const FString& Parameters)
{
	const FPlanetRecipeConfig C = Stage06bConfig();
	FPlanetBakeReport R;
	FPlanetBakeFields F;
	TArray<FGeoPrimitive> Before;
	double Stage7Sec = 0.0;
	Stage06bBake(C, F, Before, R, Stage7Sec);
	if (!TestFalse(FString::Printf(TEXT("无错误（%s）"), *FString::Join(R.Errors, TEXT(" | "))), R.HasErrors())) { return false; }

	double SumM = 0.0, SumP = 0.0;
	int32 NM = 0, NP = 0;
	for (int32 i = 0; i < F.Primitives.Num(); ++i)
	{
		const FVector3f Dir = Before[i].Center.GetSafeNormal();
		if (Dir.IsNearlyZero()) { continue; }
		const float M = F.MountainMask.Sample(Dir);
		const float Exp = float(F.Primitives[i].Exposure) / 255.f;
		if (M > 0.5f) { SumM += Exp; ++NM; }
		else if (M < 0.1f) { SumP += Exp; ++NP; }
	}
	const double MeanM = NM ? SumM / NM : 0.0, MeanP = NP ? SumP / NP : 0.0;
	TestTrue(FString::Printf(TEXT("山带与非山都有原语（%d / %d）"), NM, NP), NM > 0 && NP > 0);
	TestTrue(FString::Printf(TEXT("山带平均出露 %.4f > 非山 %.4f 的 2 倍"), MeanM, MeanP), MeanM > 2.0 * MeanP);
	AddInfo(FString::Printf(TEXT("山带（M>0.5）%d 个平均出露 %.4f；非山（M<0.1）%d 个平均 %.4f"), NM, MeanM, NP, MeanP));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage06bSwitchTest, "StellarEra.Planet.Geo.Stage06b.Switch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage06bSwitchTest::RunTest(const FString& Parameters)
{
	const FPlanetRecipeConfig C = Stage06bConfig();
	{
		FPlanetBakeReport R1, R2;
		FPlanetBakeFields F1, F2;
		TArray<FGeoPrimitive> B1, B2;
		double S1 = 0.0, S2 = 0.0;
		Stage06bBake(C, F1, B1, R1, S1);
		Stage06bBake(C, F2, B2, R2, S2);
		if (!TestFalse(FString::Printf(TEXT("无错误（%s）"), *FString::Join(R1.Errors, TEXT(" | "))), R1.HasErrors() || R2.HasErrors())) { return false; }
		TestTrue(TEXT("同一配方两次烘焙原语哈希一致"), Stage06bPrimHash(F1.Primitives) == Stage06bPrimHash(F2.Primitives));

		FPlanetRecipeConfig Single = C;
		Single.bSingleThreadBake = true;
		FPlanetBakeReport R3;
		FPlanetBakeFields F3;
		TArray<FGeoPrimitive> B3;
		double S3 = 0.0;
		Stage06bBake(Single, F3, B3, R3, S3);
		TestFalse(TEXT("单线程无错误"), R3.HasErrors());
		TestTrue(TEXT("并行与单线程原语哈希一致"), Stage06bPrimHash(F1.Primitives) == Stage06bPrimHash(F3.Primitives));
	}
	{
		FPlanetRecipeConfig Off = C;
		Off.Exposure.bExposure = false;
		FPlanetBakeReport R;
		FPlanetBakeFields F;
		TArray<FGeoPrimitive> Before;
		double S7 = 0.0;
		Stage06bBake(Off, F, Before, R, S7);
		if (!TestFalse(FString::Printf(TEXT("无错误（%s）"), *FString::Join(R.Errors, TEXT(" | "))), R.HasErrors())) { return false; }
		int32 Displaced = 0, Exposed = 0;
		for (int32 i = 0; i < F.Primitives.Num(); ++i)
		{
			Displaced += (F.Primitives[i].Center - Before[i].Center).SizeSquared() > 0.f ? 1 : 0;
			Exposed += F.Primitives[i].Exposure != 0 ? 1 : 0;
		}
		TestEqual(FString::Printf(TEXT("bExposure=false 时零位移（%d 个）"), Displaced), Displaced, 0);
		TestEqual(FString::Printf(TEXT("bExposure=false 时 Exposure 全 0（%d 个）"), Exposed), Exposed, 0);
		TestTrue(TEXT("bExposure=false 时原语哈希与阶段 6 结束时一致"), Stage06bPrimHash(F.Primitives) == Stage06bPrimHash(Before));
	}
	return true;
}

#endif
