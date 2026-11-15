// 阶段 4b：合成原语直喂材质求值，不跑烘焙 —— 只验岩性 / 蚀变圈在单次查询里怎么落地；
// 放置侧跑真实 2→3→4 烘焙，验六类侵入体的数量、深度、确定性与尺度不变。
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "GeoLithology.h"
#include "GeoPrimitive.h"
#include "GeoRng.h"
#include "PlanetMaterialEval.h"
#include "PlanetSnapshot.h"
#include "Stage02Plates.h"
#include "Stage03Terrain.h"
#include "Stage04Magmatism.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	// 跑阶段 2→3→4（阶段 4 内含 4b 放置）：与生产管线同一入口，避免测试自己拼一个近似的场。
	void Stage04bTestBake(const FPlanetRecipeConfig& C, FPlanetBakeFields& F, FPlanetBakeReport& R)
	{
		FPlanetBakeContext C2{ C, F, FGeoRng::Derive(C.Seed, 2, 0), R }; FStage02Plates().Bake(C2);
		FPlanetBakeContext C3{ C, F, FGeoRng::Derive(C.Seed, 3, 0), R }; FStage03Terrain().Bake(C3);
		FPlanetBakeContext C4{ C, F, FGeoRng::Derive(C.Seed, 4, 0), R }; FStage04Magmatism().Bake(C4);
	}
	int32 Stage04bCount(const TArray<FGeoPrimitive>& P, uint8 Litho)
	{
		int32 N = 0; for (const FGeoPrimitive& Q : P) { N += Q.LithologyId == Litho ? 1 : 0; } return N;
	}
	int32 Stage04bClueCount(const TArray<FGeoPrimitive>& P)
	{
		int32 N = 0; for (const FGeoPrimitive& Q : P) { N += Q.ClueId != 0 ? 1 : 0; } return N;
	}
	// TArray<FGeoPrimitive> 没有 Hash：借快照索引的哈希，它逐字段混入（含 Center/Radii/Rotation）。
	uint64 Stage04bPrimHash(const TArray<FGeoPrimitive>& P)
	{
		FGeoPrimitiveIndex I; I.Build(P); return I.Hash();
	}
	TSharedPtr<FPlanetSnapshot> Stage04bSnap(const TArray<FGeoPrimitive>& Prims)
	{
		TSharedPtr<FPlanetSnapshot> S = MakeShared<FPlanetSnapshot>();
		S->R0 = 40000.0;
		S->Radial.Layers = { { 1.f, GeoLithology::Crust, 0.f } };
		S->Primitives.Build(Prims);
		return S;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage04bMaterialTest, "StellarEra.Planet.Geo.Stage04b.Material",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage04bMaterialTest::RunTest(const FString& Parameters)
{
	const FVector3f Center(0.f, 0.f, float(40000.0 * 0.95));
	FGeoPrimitive Rock = FGeoPrimitive::MakeBlob(Center, FVector3f(500.f), 0.02f);
	Rock.LithologyId = GeoLithology::Granite;
	const TSharedPtr<FPlanetSnapshot> S = Stage04bSnap({ Rock });
	TestEqual(TEXT("岩体内岩性覆盖为花岗岩"), int32(PlanetMaterialEval::Sample(*S, FVector3d(Center)).Lithology), int32(GeoLithology::Granite));
	// 剖面层写的是未拆分的地壳，Sample 会按海拔拆成陆壳 / 洋壳：地表以上无高程数据，落陆壳。
	TestEqual(TEXT("岩体外恢复地壳"), int32(PlanetMaterialEval::Sample(*S, FVector3d(Center + FVector3f(900.f, 0.f, 0.f))).Lithology), int32(GeoLithology::ContinentalCrust));

	// 蚀变四圈：同一晕上按归一化距离取 4 点。
	FGeoPrimitive Halo = FGeoPrimitive::MakeBlob(Center, FVector3f(1000.f), 0.02f);
	Halo.ClueId = 1; Halo.ClueStrength = 15;
	const TSharedPtr<FPlanetSnapshot> H = Stage04bSnap({ Halo });
	const float Fract[4] = { 0.15f, 0.42f, 0.68f, 0.9f };
	const uint8 Want[4] = { GeoClue::Keratitic, GeoClue::Sericitic, GeoClue::Argillic, GeoClue::Propylitic };
	int32 Order = 0, Mono = 0; uint8 Prev = 255;
	for (int32 k = 0; k < 4; ++k)
	{
		const FGeoSample Sm = PlanetMaterialEval::Sample(*H, FVector3d(Center + FVector3f(Fract[k] * 1000.f, 0.f, 0.f)));
		Order += Sm.Clue == Want[k] ? 1 : 0;
		Mono += Sm.ClueStrength < Prev ? 1 : 0;
		Prev = Sm.ClueStrength;
	}
	TestEqual(TEXT("四圈顺序：钾化→绢英岩化→泥化→青磐岩化"), Order, 4);
	TestEqual(TEXT("强度由内到外单调递减"), Mono, 4);

	// 矿优先：矿体与岩体重叠时两者都写。
	FGeoPrimitive Ore = FGeoPrimitive::MakeBlob(Center, FVector3f(300.f), 0.02f);
	Ore.MineralId = 7; Ore.Grade = 200;
	const TSharedPtr<FPlanetSnapshot> M = Stage04bSnap({ Rock, Ore });
	const FGeoSample Ms = PlanetMaterialEval::Sample(*M, FVector3d(Center));
	TestEqual(TEXT("矿仍被写出"), int32(Ms.Mineral), 7);
	TestEqual(TEXT("岩性同时被覆盖"), int32(Ms.Lithology), int32(GeoLithology::Granite));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage04bPlacementTest, "StellarEra.Planet.Geo.Stage04b.Placement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage04bPlacementTest::RunTest(const FString& Parameters)
{
	FPlanetRecipeConfig C; C.FaceRes = 512;
	FPlanetBakeFields F; FPlanetBakeReport R;
	Stage04bTestBake(C, F, R);
	TestFalse(TEXT("无错误"), R.HasErrors());
	const int32 NB = Stage04bCount(F.Primitives, GeoLithology::Granite);
	const int32 NS = Stage04bCount(F.Primitives, GeoLithology::Diorite);
	const int32 NG = Stage04bCount(F.Primitives, GeoLithology::Gabbro);
	const int32 NL = Stage04bCount(F.Primitives, GeoLithology::LayeredMafic);
	const int32 NK = Stage04bCount(F.Primitives, GeoLithology::Kimberlite);
	AddInfo(FString::Printf(TEXT("岩基 %d，岩株 %d，辉长岩（岩墙+岩床）%d，层状 %d，金伯利 %d"), NB, NS, NG, NL, NK));
	TestTrue(TEXT("有岩基"), NB > 0);
	TestTrue(TEXT("有岩株"), NS > 0);
	TestTrue(TEXT("有岩墙 / 岩床"), NG > 0);
	TestTrue(TEXT("有金伯利岩"), NK > 0 && NK <= C.Intrusion.KimberliteMax);
	TestTrue(TEXT("有层状侵入体"), NL > 0);

	const int32 Arc = F.Volcanoes.FilterByPredicate([](const FGeoVolcano& V) { return V.OtherPlate >= 0; }).Num();
	TestEqual(TEXT("岩株数 = 弧火山数"), NS, Arc);

	// 深度：用椭球支撑函数按各自中心方向量真实顶部。Bounds 是 AABB 角点，会高估径向高度，
	// 漏掉半高偏移写错、或岩体整体偏浅这类 bug。
	{
		struct FDepthFloor { uint8 Litho; float Floor; const TCHAR* Name; };
		const FDepthFloor Floors[] = {
			{ GeoLithology::Granite, 0.01f, TEXT("岩基") },
			{ GeoLithology::Diorite, 0.01f, TEXT("岩株") },
			{ GeoLithology::Gabbro, 0.01f, TEXT("岩墙 / 岩床") },
			{ GeoLithology::LayeredMafic, 0.01f, TEXT("层状") },
			{ GeoLithology::Kimberlite, 0.004f, TEXT("金伯利") },
		};
		const float Eps = 1e-4f;   // 顶深由 float 中心与双精度半径拼出来，留一位余量
		for (const FDepthFloor& Df : Floors)
		{
			float MinDepth = TNumericLimits<float>::Max();
			int32 TooHigh = 0;
			for (const FGeoPrimitive& P : F.Primitives)
			{
				if (P.LithologyId != Df.Litho) { continue; }
				const FVector3f Dir = P.Center.GetSafeNormal();
				const FVector3f Local = P.Kind == EGeoPrimitiveKind::OrientedBlob ? P.Rotation.UnrotateVector(Dir) : Dir;
				const float Support = (Local * P.Radii).Size();
				const float TopDepth = float((C.Radius + double(F.Elevation.Sample(Dir)) - double(P.Center.Size() + Support)) / C.Radius);
				MinDepth = FMath::Min(MinDepth, TopDepth);
				TooHigh += TopDepth >= Df.Floor - Eps ? 0 : 1;
			}
			AddInfo(FString::Printf(TEXT("%s 最浅顶深 %.4f（下限 %.4f）"), Df.Name,
				MinDepth == TNumericLimits<float>::Max() ? -1.f : MinDepth, Df.Floor));
			TestEqual(FString::Printf(TEXT("%s 不露头"), Df.Name), TooHigh, 0);
		}
	}

	// 确定性
	FPlanetBakeFields F2; FPlanetBakeReport R2;
	Stage04bTestBake(C, F2, R2);
	TestEqual(TEXT("原语哈希可复现"), Stage04bPrimHash(F.Primitives), Stage04bPrimHash(F2.Primitives));

	// 尺度不变
	FPlanetRecipeConfig Half = C; Half.Radius = C.Radius * 0.5;
	FPlanetBakeFields Fh; FPlanetBakeReport Rh;
	Stage04bTestBake(Half, Fh, Rh);
	TestEqual(TEXT("半径减半各类数量不变（岩基）"), Stage04bCount(Fh.Primitives, GeoLithology::Granite), NB);
	TestEqual(TEXT("半径减半各类数量不变（岩株）"), Stage04bCount(Fh.Primitives, GeoLithology::Diorite), NS);
	TestTrue(TEXT("半径减半单个原语半轴减半"), Fh.Primitives.Num() > 0 && F.Primitives.Num() > 0
		&& FMath::IsNearlyEqual(Fh.Primitives.Last().Bounds.GetExtent().GetMax(), 0.5f * F.Primitives.Last().Bounds.GetExtent().GetMax(), 1e-3f));

	// 开关
	const auto Off = [&](TFunctionRef<void(FPlanetIntrusionParams&)> Set, uint8 Litho, const TCHAR* Name)
	{
		FPlanetRecipeConfig X = C; Set(X.Intrusion);
		FPlanetBakeFields Fx; FPlanetBakeReport Rx; Stage04bTestBake(X, Fx, Rx);
		TestEqual(FString::Printf(TEXT("关闭%s"), Name), Stage04bCount(Fx.Primitives, Litho), 0);
	};
	Off([](FPlanetIntrusionParams& P) { P.bBatholiths = false; }, GeoLithology::Granite, TEXT("岩基"));
	Off([](FPlanetIntrusionParams& P) { P.bStocks = false; }, GeoLithology::Diorite, TEXT("岩株"));
	Off([](FPlanetIntrusionParams& P) { P.bLayeredIntrusions = false; }, GeoLithology::LayeredMafic, TEXT("层状侵入体"));
	Off([](FPlanetIntrusionParams& P) { P.bKimberlites = false; }, GeoLithology::Kimberlite, TEXT("金伯利岩"));

	// 岩墙与岩床共用辉长岩编号：只关一类它可能悄悄消失，两个方向都要查。
	{
		FPlanetRecipeConfig Both = C; Both.Intrusion.bDikes = false; Both.Intrusion.bSills = false;
		FPlanetBakeFields Fb; FPlanetBakeReport Rb; Stage04bTestBake(Both, Fb, Rb);
		TestEqual(TEXT("岩墙与岩床都关掉后没有辉长岩"), Stage04bCount(Fb.Primitives, GeoLithology::Gabbro), 0);

		FPlanetRecipeConfig NoDike = C; NoDike.Intrusion.bDikes = false;
		FPlanetBakeFields Fd; FPlanetBakeReport Rd; Stage04bTestBake(NoDike, Fd, Rd);
		const int32 NGNoDike = Stage04bCount(Fd.Primitives, GeoLithology::Gabbro);
		TestTrue(TEXT("只关岩墙时辉长岩至少少 DikePerRift 个"), NGNoDike <= NG - C.Intrusion.DikePerRift);
		TestTrue(TEXT("只关岩墙时岩床仍在"), NGNoDike > 0);
	}
	return true;
}

#endif
