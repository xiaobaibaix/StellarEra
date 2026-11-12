// 求值核是体素线程唯一读的东西：符号、值域、岩性三件事必须与快照内容一致，值域估窄会让插件整块跳过。
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PlanetSnapshot.h"
#include "PlanetShapeEval.h"
#include "PlanetMaterialEval.h"
#include "GeoLithology.h"
#include "GeoRng.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoSnapshotTest, "StellarEra.Planet.Geo.Snapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoSnapshotTest::RunTest(const FString& Parameters)
{
	FPlanetSnapshot S;
	S.R0 = 1000.0;
	S.Radial.Layers = { { 0.25f, GeoLithology::Crust, 0.f }, { 1.f, GeoLithology::InnerCore, 0.f } };
	S.MohoWobble.Init(16);
	S.MohoWobble.Fill([](const FVector3f&) { return 0.f; });
	S.MinSurfaceRadius = S.MaxSurfaceRadius = S.R0;
	S.FinalizeHash();

	const FVector3d Dir = FVector3d(1.0, 2.0, 3.0).GetSafeNormal();
	TestTrue(TEXT("地表以下实心"), PlanetShapeEval::Value(S, Dir * 900.0) < 0.f);
	TestTrue(TEXT("地表以上空气"), PlanetShapeEval::Value(S, Dir * 1100.0) > 0.f);
	TestTrue(TEXT("地表处接近 0"), FMath::Abs(PlanetShapeEval::Value(S, Dir * 1000.0)) < 1e-3f);
	TestTrue(TEXT("球心不出 NaN"), FMath::IsFinite(PlanetShapeEval::Value(S, FVector3d::ZeroVector)));

	TestEqual(TEXT("深度 0.1 是地壳"), PlanetMaterialEval::Sample(S, Dir * 900.0).Lithology, GeoLithology::Crust);
	TestEqual(TEXT("深度 0.5 是内核层"), PlanetMaterialEval::Sample(S, Dir * 500.0).Lithology, GeoLithology::InnerCore);

	FGeoRng Rng = FGeoRng::Derive(5, 0, 0);
	bool bCovers = true;
	for (int32 i = 0; i < 20000; ++i)
	{
		const FVector3d C(Rng.Range(-1200.f, 1200.f), Rng.Range(-1200.f, 1200.f), Rng.Range(-1200.f, 1200.f));
		const double H = Rng.Range(1.f, 64.f);
		const FVector3d Lo = C - FVector3d(H), Hi = C + FVector3d(H);
		const FGeoRange R = PlanetShapeEval::ValueRange(S, Lo, Hi);
		const FVector3d P(FMath::Lerp(Lo.X, Hi.X, double(Rng.NextFloat())),
			FMath::Lerp(Lo.Y, Hi.Y, double(Rng.NextFloat())), FMath::Lerp(Lo.Z, Hi.Z, double(Rng.NextFloat())));
		bCovers &= R.Contains(PlanetShapeEval::Value(S, P));
	}
	TestTrue(TEXT("值域覆盖盒内任意采样"), bCovers);
	TestTrue(TEXT("深部盒整块实心"), PlanetShapeEval::ValueRange(S, FVector3d(-50.0), FVector3d(50.0)).Hi < 0.f);

	// 值域紧致：跨地表的盒，Lo/Hi 必须等于解析值，(-inf,+inf) 不能过。
	{
		const FVector3d Lo(900.0, -30.0, 20.0), Hi(1100.0, 40.0, 60.0);
		const FGeoRange R = PlanetShapeEval::ValueRange(S, Lo, Hi);
		const double Near = FVector3d(900.0, 0.0, 20.0).Size();
		const double Far = FVector3d(1100.0, 40.0, 60.0).Size();
		TestTrue(TEXT("跨地表盒 Lo 紧致"), FMath::Abs(R.Lo - float(Near - S.MaxSurfaceRadius)) < 1e-3f);
		TestTrue(TEXT("跨地表盒 Hi 紧致"), FMath::Abs(R.Hi - float(Far - S.MinSurfaceRadius)) < 1e-3f);
	}
	TestTrue(TEXT("盒整块在球外 Lo>0"), PlanetShapeEval::ValueRange(S, FVector3d(1500.0, 0.0, 0.0), FVector3d(1600.0, 50.0, 50.0)).Lo > 0.f);

	// Min < R0 < Max：Lo 用 Max、Hi 用 Min，覆盖仍须成立。
	{
		FPlanetSnapshot W = S;
		W.MinSurfaceRadius = 990.0;
		W.MaxSurfaceRadius = 1010.0;
		W.FinalizeHash();
		const FGeoRange R = PlanetShapeEval::ValueRange(W, FVector3d(900.0, -30.0, 20.0), FVector3d(1100.0, 40.0, 60.0));
		TestTrue(TEXT("Min/Max 分离 Lo 用 Max"), FMath::Abs(R.Lo - float(FVector3d(900.0, 0.0, 20.0).Size() - 1010.0)) < 1e-3f);
		TestTrue(TEXT("Min/Max 分离 Hi 用 Min"), FMath::Abs(R.Hi - float(FVector3d(1100.0, 40.0, 60.0).Size() - 990.0)) < 1e-3f);
		FGeoRng Rng2 = FGeoRng::Derive(6, 0, 0);
		bool bCovers2 = true;
		for (int32 i = 0; i < 20000; ++i)
		{
			const FVector3d C(Rng2.Range(-1200.f, 1200.f), Rng2.Range(-1200.f, 1200.f), Rng2.Range(-1200.f, 1200.f));
			const double H = Rng2.Range(1.f, 64.f);
			const FVector3d BLo = C - FVector3d(H), BHi = C + FVector3d(H);
			const FGeoRange RR = PlanetShapeEval::ValueRange(W, BLo, BHi);
			const FVector3d P(FMath::Lerp(BLo.X, BHi.X, double(Rng2.NextFloat())),
				FMath::Lerp(BLo.Y, BHi.Y, double(Rng2.NextFloat())), FMath::Lerp(BLo.Z, BHi.Z, double(Rng2.NextFloat())));
			bCovers2 &= RR.Contains(PlanetShapeEval::Value(W, P));
		}
		TestTrue(TEXT("Min/Max 分离时值域仍覆盖"), bCovers2);
	}

	FPlanetSnapshot T = S;
	TestTrue(TEXT("同内容同哈希"), T.ContentHash == S.ContentHash);
	T.R0 = 1001.0;
	T.FinalizeHash();
	TestTrue(TEXT("改半径哈希变"), T.ContentHash != S.ContentHash);

	// float 哈希在 R=40000 分辨不出 0.001 的差。
	FPlanetSnapshot A = S, B = S;
	A.R0 = 40000.0;
	B.R0 = 40000.001;
	A.FinalizeHash();
	B.FinalizeHash();
	TestTrue(TEXT("R0 差 0.001 哈希也变"), A.ContentHash != B.ContentHash);
	return true;
}

#endif
