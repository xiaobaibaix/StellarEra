// 阶段 3：山只长在部分边界、有海沟、侵蚀生效、无接缝、确定、并且尺度随半径等比（星球大小可变）。
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "GeoContinentField.h"
#include "GeoMineralRules.h"
#include "GeoPlates.h"
#include "GeoRng.h"
#include "PlanetBakePipeline.h"
#include "Stage02Plates.h"
#include "Stage03Terrain.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** 只跑阶段 2 + 3，返回阶段 2 的高程副本（作对照）。 */
	FGeoField2D Stage03TestBake(const FPlanetRecipeConfig& C, FPlanetBakeFields& F, FPlanetBakeReport& R)
	{
		FPlanetBakeContext C2{ C, F, FGeoRng::Derive(C.Seed, 2, 0), R };
		FStage02Plates().Bake(C2);
		const FGeoField2D Base = F.Elevation;
		FPlanetBakeContext C3{ C, F, FGeoRng::Derive(C.Seed, 3, 0), R };
		FStage03Terrain().Bake(C3);
		return Base;
	}

	double Stage03TestArea(const FGeoField2D& Field, TFunctionRef<bool(int32, int32, int32)> Pred)
	{
		double In = 0.0, All = 0.0;
		for (int32 Face = 0; Face < 6; ++Face)
		for (int32 Y = 0; Y < Field.Res; ++Y)
		for (int32 X = 0; X < Field.Res; ++X)
		{
			const double W = GeoContinentField::TexelWeight(Field, X, Y);
			All += W;
			In += Pred(Face, X, Y) ? W : 0.0;
		}
		return In / FMath::Max(All, 1e-30);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage03Test, "StellarEra.Planet.Geo.Stage03",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage03Test::RunTest(const FString& Parameters)
{
	FPlanetRecipeConfig C;
	C.FaceRes = 512;
	FPlanetBakeFields F;
	FPlanetBakeReport Report;
	const FGeoField2D Base = Stage03TestBake(C, F, Report);
	TestFalse(FString::Printf(TEXT("无错误（%s）"), *FString::Join(Report.Errors, TEXT(" | "))), Report.HasErrors());
	const double R = C.Radius;
	const float ATrench = float(C.Terrain.TrenchDepthRatio * R);

	const double MountainArea = Stage03TestArea(F.MountainMask, [&F](int32 Fc, int32 X, int32 Y) { return F.MountainMask.At(Fc, X, Y) > 0.5f; });
	const double HighArea = Stage03TestArea(F.ReliefType, [&F](int32 Fc, int32 X, int32 Y) { return F.ReliefType.At(Fc, X, Y) == 4.f; });
	const double TrenchArea = Stage03TestArea(F.ReliefType, [&F](int32 Fc, int32 X, int32 Y) { return F.ReliefType.At(Fc, X, Y) == 5.f; });
	const float Ratio = F.ReliefDeltaMax / FMath::Max(F.ReliefRawMax, 1e-6f);
	AddInfo(FString::Printf(TEXT("山带(M>0.5) %.4f，高山 %.4f，海沟 %.4f；高程 [%.0f, %.0f]；侵蚀后/前峰值 %.3f；陆地 %.4f"),
		MountainArea, HighArea, TrenchArea, F.MinElevation, F.MaxElevation, Ratio, F.LandAreaFrac));

	TestTrue(FString::Printf(TEXT("山带覆盖 %.3f ∈ [0.02, 0.20]"), MountainArea), MountainArea >= 0.02 && MountainArea <= 0.20);
	TestTrue(TEXT("有高山"), HighArea > 0.0);
	TestTrue(TEXT("有海沟"), TrenchArea > 0.0);
	TestTrue(FString::Printf(TEXT("山顶海拔 %.0f ∈ [0.03R, 0.08R]"), F.MaxElevation), F.MaxElevation >= 0.03 * R && F.MaxElevation <= 0.08 * R);
	TestTrue(TEXT("海沟比最深洋盆更深"), F.MinElevation < -float(C.Continents.OceanDepthRatio * R) - 0.3f * ATrench);
	TestTrue(FString::Printf(TEXT("侵蚀削峰 %.3f ∈ [0.6, 0.995]"), Ratio), Ratio >= 0.6f && Ratio <= 0.995f);
	TestTrue(FString::Printf(TEXT("陆地占比仍≈目标（%.4f）"), F.LandAreaFrac), FMath::Abs(F.LandAreaFrac - C.Continents.LandFrac) <= 0.03f);

	// 海沟只在俯冲边界，且不在大陆核心；离散边界上几乎无山。
	{
		int32 BadTrench = 0, CoreTrench = 0, Trenches = 0;
		double DivergentMountain = 0.0, DivergentAll = 0.0;
		for (int32 Face = 0; Face < 6; ++Face)
		for (int32 Y = 0; Y < F.ReliefType.Res; ++Y)
		for (int32 X = 0; X < F.ReliefType.Res; ++X)
		{
			const FVector3f Dir = F.ReliefType.TexelDir(Face, X, Y);
			int32 Near, Second; float D1, D2;
			GeoPlates::NearestTwo(F.PlateFrame, GeoPlates::WarpDirection(F.PlateFrame, Dir, C.Plates), Near, Second, D1, D2);
			const uint8 Type = F.PlateFrame.BoundaryTypeBetween(Near, Second);
			if (F.ReliefType.At(Face, X, Y) == 5.f)
			{
				++Trenches;
				BadTrench += (Type >= 1 && Type <= 3) ? 0 : 1;
				CoreTrench += F.ContinentalMask.Sample(Dir) > 0.9f ? 1 : 0;
			}
			if (Type == 4 && 0.5f * (D2 - D1) < 0.5f * C.Terrain.BeltWidthRatio)
			{
				DivergentAll += 1.0;
				DivergentMountain += F.MountainMask.At(Face, X, Y) > 0.5f ? 1.0 : 0.0;
			}
		}
		TestTrue(TEXT("确实有海沟 texel"), Trenches > 0);
		TestEqual(TEXT("海沟全在俯冲边界"), BadTrench, 0);
		TestEqual(TEXT("大陆核心无海沟"), CoreTrench, 0);
		TestTrue(TEXT("离散边界几乎不长山"), DivergentAll == 0.0 || DivergentMountain / DivergentAll < 0.05);
	}

	// 无接缝：立方体棱两侧 ±1 texel 的高差与面内相邻同量级。
	{
		const FGeoField2D& E = F.Elevation;
		float MaxInner = 0.f, MaxSeam = 0.f;
		for (int32 Face = 0; Face < 6; ++Face)
		for (int32 Y = 0; Y < E.Res; ++Y)
		{
			for (int32 X = 0; X + 1 < E.Res; ++X) { MaxInner = FMath::Max(MaxInner, FMath::Abs(E.At(Face, X + 1, Y) - E.At(Face, X, Y))); }
			const float V = (float(Y) + 0.5f) / float(E.Res) * 2.f - 1.f, Du = 1.f / float(E.Res);
			MaxSeam = FMath::Max(MaxSeam, FMath::Abs(E.Sample(FGeoField2D::FaceToDir(Face, 1.f - Du, V)) - E.Sample(FGeoField2D::FaceToDir(Face, 1.f + Du, V))));
		}
		TestTrue(FString::Printf(TEXT("无接缝：棱 %.1f ≤ 3×面内 %.1f + 1"), MaxSeam, MaxInner), MaxSeam <= 3.f * MaxInner + 1.f);
	}

	// 确定性：单线程与并行逐位一致。
	{
		FPlanetRecipeConfig Single = C;
		Single.bSingleThreadBake = true;
		FPlanetBakeFields F2;
		FPlanetBakeReport R2;
		Stage03TestBake(Single, F2, R2);
		TestTrue(TEXT("单线程与并行高程逐位一致"), F2.Elevation.Hash() == F.Elevation.Hash());
		TestTrue(TEXT("单线程与并行地形类型一致"), F2.ReliefType.Hash() == F.ReliefType.Hash());
	}

	// 尺度随半径等比：同配方只改半径，高程 / 半径逐 texel 几乎相同（星球大小可变的前提）。
	{
		FPlanetRecipeConfig Small = C;
		Small.Radius = 4000.0;
		FPlanetBakeFields Fs;
		FPlanetBakeReport Rs;
		Stage03TestBake(Small, Fs, Rs);
		double Worst = 0.0;
		for (int32 Face = 0; Face < 6; ++Face)
		for (int32 Y = 0; Y < F.Elevation.Res; Y += 3)
		for (int32 X = 0; X < F.Elevation.Res; X += 3)
		{
			Worst = FMath::Max(Worst, FMath::Abs(double(F.Elevation.At(Face, X, Y)) / R - double(Fs.Elevation.At(Face, X, Y)) / Small.Radius));
		}
		AddInfo(FString::Printf(TEXT("R=40000 与 R=4000 的 高程/R 最大差 %.2e"), Worst));
		TestTrue(FString::Printf(TEXT("高程按半径等比缩放（最大差 %.2e R）"), Worst), Worst < 2e-4);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoTrenchFillTest, "StellarEra.Planet.Geo.Stage03.TrenchFill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoTrenchFillTest::RunTest(const FString& Parameters)
{
	FPlanetRecipeConfig Bare;
	Bare.FaceRes = 512;
	Bare.Terrain.TrenchSedimentFill = 0.f;
	Bare.Terrain.TrenchSlump = 0.f;
	Bare.Terrain.TrenchWedge = 0.f;
	FPlanetRecipeConfig Filled = Bare;
	Filled.Terrain.TrenchSedimentFill = 1.f;
	FPlanetRecipeConfig Slumped = Bare;
	Slumped.Terrain.TrenchSlump = 1.f;
	FPlanetRecipeConfig Def;
	Def.FaceRes = 512;

	FPlanetBakeFields F0, F1, F2, F3;
	FPlanetBakeReport R0, R1, R2, R3;
	const FGeoField2D Base = Stage03TestBake(Bare, F0, R0);
	Stage03TestBake(Filled, F1, R1);
	Stage03TestBake(Slumped, F2, R2);
	Stage03TestBake(Def, F3, R3);
	const double Rad = Bare.Radius;
	const float ATrench = float(Bare.Terrain.TrenchDepthRatio * Rad), OceanD = float(Bare.Continents.OceanDepthRatio * Rad);

	// 近岸 vs 远洋：同一批海沟 texel，充填后剩下的深度占比，近岸应明显小于远洋。
	double NearKeep = 0.0, FarKeep = 0.0;
	int32 NearN = 0, FarN = 0, Surfaced = 0;
	const FGeoField2D& D0 = F0.ReliefDelta;
	for (int32 Face = 0; Face < 6; ++Face)
	for (int32 Y = 0; Y < D0.Res; ++Y)
	for (int32 X = 0; X < D0.Res; ++X)
	{
		const FVector3f Dir = D0.TexelDir(Face, X, Y);
		const float E0 = Base.Sample(Dir);
		const float Bare0 = D0.At(Face, X, Y);
		if (Bare0 > -0.3f * ATrench) { continue; }
		if (F3.Elevation.Sample(Dir) >= 0.f) { ++Surfaced; }
		const double Keep = double(F1.ReliefDelta.At(Face, X, Y)) / double(Bare0);
		if (-E0 < 0.6f * OceanD) { NearKeep += Keep; ++NearN; } else if (-E0 > 0.97f * OceanD) { FarKeep += Keep; ++FarN; }
	}
	NearKeep /= FMath::Max(NearN, 1);
	FarKeep /= FMath::Max(FarN, 1);
	AddInfo(FString::Printf(TEXT("充填后剩余深度：近岸 %.2f（%d texel） / 远洋 %.2f（%d texel）"), NearKeep, NearN, FarKeep, FarN));
	TestTrue(TEXT("有近岸与远洋海沟样本"), NearN > 0 && FarN > 0);
	TestTrue(FString::Printf(TEXT("近岸海沟被填得更浅：%.2f < %.2f"), NearKeep, FarKeep), NearKeep < FarKeep - 0.2);
	TestEqual(TEXT("默认参数下海沟带没有露出海面"), Surfaced, 0);
	TestTrue(TEXT("充填后最深处不比不填时更深"), F1.MinElevation >= F0.MinElevation - 1.f);

	// 坍塌：海沟带内最大坡度变小。
	const auto TrenchSlope = [&](const FPlanetBakeFields& Fx)
	{
		float Worst = 0.f;
		const FGeoField2D& E = Fx.Elevation;
		for (int32 Face = 0; Face < 6; ++Face)
		for (int32 Y = 0; Y < E.Res; ++Y)
		for (int32 X = 0; X + 1 < E.Res; ++X)
		{
			const FVector3f A = E.TexelDir(Face, X, Y);
			if (D0.Sample(A) > -0.3f * ATrench) { continue; }
			const FVector3f B = E.TexelDir(Face, X + 1, Y);
			const float Arc = float(Rad) * FMath::Acos(FMath::Clamp(FVector3f::DotProduct(A, B), -1.f, 1.f));
			Worst = FMath::Max(Worst, FMath::Abs(E.At(Face, X + 1, Y) - E.At(Face, X, Y)) / FMath::Max(Arc, 1e-3f));
		}
		return Worst;
	};
	const float S0 = TrenchSlope(F0), S2 = TrenchSlope(F2);
	AddInfo(FString::Printf(TEXT("海沟带最大坡度：不塌 %.3f / 坍塌 %.3f"), S0, S2));
	TestTrue(FString::Printf(TEXT("坍塌削缓海沟壁：%.3f < %.3f"), S2, S0), S2 < S0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoChromiteTest, "StellarEra.Planet.Geo.Mineral.Chromite",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoChromiteTest::RunTest(const FString& Parameters)
{
	FPlanetRecipeConfig C;
	C.FaceRes = 256;
	C.bKeepDebugFields = true;
	TSharedPtr<FGeoChromiteRule> Rule = MakeShared<FGeoChromiteRule>();
	Rule->MineralId = 1;
	C.MineralRules = { Rule };
	FPlanetBakeReport Report;
	const TSharedPtr<const FPlanetSnapshot> Snap = FPlanetBakePipeline::MakeDefault().Bake(C, Report);
	if (!TestTrue(FString::Printf(TEXT("烘焙成功（%s）"), *FString::Join(Report.Errors, TEXT(" | "))), Snap.IsValid())) { return false; }
	const FGeoField2D* Belt = Snap->DebugFields.Find(TEXT("MountainMask"));
	if (!TestNotNull(TEXT("有山带场"), Belt)) { return false; }
	int32 N = 0, Bad = 0;
	for (const FGeoPrimitive& P : Snap->Primitives.GetPrimitives())
	{
		++N;
		Bad += Belt->Sample(P.Center.GetSafeNormal()) < 0.3f ? 1 : 0;
	}
	AddInfo(FString::Printf(TEXT("铬铁矿豆荚体 %d 个"), N));
	TestTrue(TEXT("撒出了铬铁矿"), N > 0);
	TestEqual(TEXT("铬铁矿全在碰撞造山带"), Bad, 0);
	return true;
}

#endif
