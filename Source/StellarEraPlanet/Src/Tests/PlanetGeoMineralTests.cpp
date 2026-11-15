// 撒矿器：可插拔、可叠加、位置由场决定、只改材质不改密度、确定性、配方 → 规则拷贝正确。
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "GeoMineralCatalog.h"
#include "GeoMineralRules.h"
#include "GeoMineralScatterer.h"
#include "GeoRng.h"
#include "PlanetBakePipeline.h"
#include "PlanetMaterialEval.h"
#include "PlanetRecipe.h"
#include "PlanetShapeEval.h"
#include "PlanetGeoDebugView3D.h"
#include "PlanetGeoDebugRender.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FPlanetRecipeConfig GeoMineralTestConfig()
	{
		FPlanetRecipeConfig C;
		C.FaceRes = 256;
		C.bKeepDebugFields = true;
		TSharedPtr<FGeoBifRule> Bif = MakeShared<FGeoBifRule>(); Bif->MineralId = 1;
		TSharedPtr<FGeoVmsRule> Vms = MakeShared<FGeoVmsRule>(); Vms->MineralId = 2;
		TSharedPtr<FGeoNoduleRule> Nod = MakeShared<FGeoNoduleRule>(); Nod->MineralId = 3;
		C.MineralRules = { Bif, Vms, Nod };
		C.Minerals = { { TEXT("条带状铁矿"), FColor::Red }, { TEXT("块状硫化物"), FColor::Yellow }, { TEXT("锰结核"), FColor::Black } };
		return C;
	}

	int32 GeoMineralTestCount(const FPlanetSnapshot& S, uint8 Id)
	{
		int32 N = 0;
		for (const FGeoPrimitive& P : S.Primitives.GetPrimitives()) { N += P.MineralId == Id ? 1 : 0; }
		return N;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoOrientedBlobTest, "StellarEra.Planet.Geo.Mineral.OrientedBlob",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoOrientedBlobTest::RunTest(const FString& Parameters)
{
	const FVector3f Up = FVector3f(0.3f, -0.5f, 0.8f).GetSafeNormal();
	const FQuat4f Rot = GeoMineralRules::TangentFrame(Up, 0.7f);
	TestTrue(TEXT("切平面坐标系的 Z 轴 = 径向"), Rot.RotateVector(FVector3f::ZAxisVector).Equals(Up, 1e-4f));
	const FVector3f C = Up * 1000.f;
	const FGeoPrimitive P = FGeoPrimitive::MakeOrientedBlob(C, FVector3f(100.f, 40.f, 5.f), Rot, 0.1f);
	TestEqual(TEXT("中心强度 1"), GeoPrimitiveEvaluate(P, C), 1.f);
	TestTrue(TEXT("沿长轴 85 体素仍在矿体里"), GeoPrimitiveEvaluate(P, C + Rot.RotateVector(FVector3f::XAxisVector) * 85.f) > 0.5f);
	TestEqual(TEXT("沿径向 6 体素已出矿体"), GeoPrimitiveEvaluate(P, C + Up * 6.f), 0.f);

	FGeoRng Rng = FGeoRng::Derive(4, 0, 0);
	bool bBounds = true, bRange = true;
	for (int32 i = 0; i < 4000; ++i)
	{
		const FVector3f X = C + FVector3f(Rng.Range(-120.f, 120.f), Rng.Range(-120.f, 120.f), Rng.Range(-120.f, 120.f));
		const float V = GeoPrimitiveEvaluate(P, X);
		bBounds &= V == 0.f || P.Bounds.IsInsideOrOn(X);
		const FBox3f Box(X - FVector3f(8.f), X + FVector3f(8.f));
		const FGeoRange R = GeoPrimitiveRange(P, Box);
		bRange &= R.Contains(GeoPrimitiveEvaluate(P, X + FVector3f(Rng.Range(-8.f, 8.f), Rng.Range(-8.f, 8.f), Rng.Range(-8.f, 8.f))));
	}
	TestTrue(TEXT("包围盒包住全部非零强度"), bBounds);
	TestTrue(TEXT("值域保守"), bRange);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoMineralScatterTest, "StellarEra.Planet.Geo.Mineral.Scatter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoMineralScatterTest::RunTest(const FString& Parameters)
{
	const FPlanetRecipeConfig C = GeoMineralTestConfig();
	FPlanetBakeReport Report;
	const TSharedPtr<const FPlanetSnapshot> Snap = FPlanetBakePipeline::MakeDefault().Bake(C, Report);
	if (!TestTrue(FString::Printf(TEXT("烘焙成功（%s）"), *FString::Join(Report.Errors, TEXT(" | "))), Snap.IsValid())) { return false; }

	const int32 NBif = GeoMineralTestCount(*Snap, 1), NVms = GeoMineralTestCount(*Snap, 2);
	AddInfo(FString::Printf(TEXT("BIF %d 个，VMS 透镜 %d 个，覆层 %d 张"), NBif, NVms, Snap->MineralOverlays.Num()));
	TestEqual(TEXT("BIF 撒满默认个数"), NBif, FGeoBifParams().Count);
	TestTrue(TEXT("VMS 至少 10 个透镜体"), NVms >= 10);
	TestEqual(TEXT("锰结核一张覆层"), Snap->MineralOverlays.Num(), 1);

	// 位置由场决定：每个矿体都满足自己的成矿条件；矿体中心的材质就是这种矿；矿不改密度。
	const FGeoField2D* Type = Snap->DebugFields.Find(TEXT("CrustType"));
	const FGeoField2D* Boundary = Snap->DebugFields.Find(TEXT("BoundaryType"));
	if (!TestTrue(TEXT("有调试场"), Type && Boundary)) { return false; }
	int32 BadPlace = 0, BadMaterial = 0, Buried = 0, NOre = 0;
	for (const FGeoPrimitive& P : Snap->Primitives.GetPrimitives())
	{
		// 阶段 4b 会把侵入体（MineralId == 0）追加进同一张原语表：下面全是矿体断言，必须先跳过。
		if (P.MineralId == 0) { continue; }
		++NOre;
		const FVector3f Dir = P.Center.GetSafeNormal();
		if (P.MineralId == 1) { BadPlace += Type->SampleNearest(Dir) != 1.f ? 1 : 0; }
		if (P.MineralId == 2) { BadPlace += Boundary->SampleNearest(Dir) != 4.f ? 1 : 0; }
		BadMaterial += PlanetMaterialEval::Sample(*Snap, FVector3d(P.Center)).Mineral != P.MineralId ? 1 : 0;
		Buried += PlanetShapeEval::Value(*Snap, FVector3d(P.Center)) < 0.f ? 1 : 0;
	}
	TestEqual(TEXT("每个矿体都落在自己的成矿环境里"), BadPlace, 0);
	TestEqual(TEXT("矿体中心的材质是这种矿"), BadMaterial, 0);
	TestEqual(TEXT("矿体全在地下"), Buried, NOre);

	// 体积撒矿：矿体分布在整个地壳厚度里，且不越过莫霍面。
	{
		const double Floor = C.Radius * (1.0 - double(C.Sphere.CrustRatio) + double(C.Sphere.MohoWobbleRatio));
		double MaxDepth = 0.0;
		int32 BelowFloor = 0, Deep = 0;
		for (const FGeoPrimitive& P : Snap->Primitives.GetPrimitives())
		{
			if (P.MineralId == 0) { continue; }   // 侵入体不是矿体，不参与矿体的深度分布
			const double R = FVector3d(P.Center).Size();
			MaxDepth = FMath::Max(MaxDepth, C.Radius - R);
			BelowFloor += R < Floor ? 1 : 0;
			Deep += C.Radius - R > 0.5 * C.Sphere.CrustRatio * C.Radius ? 1 : 0;
		}
		AddInfo(FString::Printf(TEXT("最深矿体在海平面下 %.0f 体素，深于半个地壳的 %d 个"), MaxDepth, Deep));
		TestEqual(TEXT("矿体都在莫霍面之上"), BelowFloor, 0);
		TestTrue(TEXT("地壳下半部也有矿体"), Deep > 0);
	}

	// 显示放大：真实尺寸只在矿体内命中，放大 20 倍后十倍半径处也命中。
	{
		const FGeoPrimitive& B = Snap->Primitives.GetPrimitives()[0];
		const FVector3d Far = FVector3d(B.Center + B.Rotation.RotateVector(FVector3f::XAxisVector) * (B.Radii.X * 10.f));
		TestEqual(TEXT("中心处真实尺寸命中"), int32(PlanetGeoDebugRender::MineralAt(*Snap, FVector3d(B.Center), 1.f)), int32(B.MineralId));
		TestEqual(TEXT("放大 20 倍后十倍半径处命中"), int32(PlanetGeoDebugRender::MineralAt(*Snap, Far, 20.f)), int32(B.MineralId));
	}

	FPlanetRecipeConfig Plain = C;
	Plain.MineralRules.Reset();
	FPlanetBakeReport PlainReport;
	const TSharedPtr<const FPlanetSnapshot> NoOre = FPlanetBakePipeline::MakeDefault().Bake(Plain, PlainReport);
	if (TestTrue(TEXT("无撒矿器也能烘焙"), NoOre.IsValid()))
	{
		bool bSameDensity = true;
		FGeoRng Rng = FGeoRng::Derive(8, 0, 0);
		for (int32 i = 0; i < 2000; ++i)
		{
			const FVector3d Q(Rng.Range(-40300.f, 40300.f), Rng.Range(-40300.f, 40300.f), Rng.Range(-40300.f, 40300.f));
			bSameDensity &= PlanetShapeEval::Value(*Snap, Q) == PlanetShapeEval::Value(*NoOre, Q);
		}
		TestTrue(TEXT("撒矿不改密度"), bSameDensity);
		TestTrue(TEXT("撒矿改了内容哈希"), NoOre->ContentHash != Snap->ContentHash);
	}

	// 锰结核：覆层的矿只出现在洋壳上。
	{
		const FGeoMineralOverlay& O = Snap->MineralOverlays[0];
		int32 Covered = 0, BadCover = 0;
		FGeoRng Rng = FGeoRng::Derive(9, 0, 0);
		for (int32 i = 0; i < 20000; ++i)
		{
			const float Z = Rng.Range(-1.f, 1.f), Phi = Rng.Range(0.f, 2.f * PI), Sx = FMath::Sqrt(1.f - Z * Z);
			const FVector3f Dir(Sx * FMath::Cos(Phi), Sx * FMath::Sin(Phi), Z);
			if (O.Mask.Sample(Dir) <= 0.5f) { continue; }
			++Covered;
			BadCover += Snap->Elevation.Sample(Dir) >= Snap->CrustSplitElevation ? 1 : 0;
		}
		AddInfo(FString::Printf(TEXT("锰结核覆盖 %d/20000 个方向"), Covered));
		TestTrue(TEXT("锰结核确实有覆盖"), Covered > 0);
		TestEqual(TEXT("锰结核只在洋底（陆架以下）"), BadCover, 0);
	}

	// 确定性 + 叠加：同配方两次一致；同一规则再叠一份只改种子偏移，矿体数翻倍。
	FPlanetBakeReport R2;
	const TSharedPtr<const FPlanetSnapshot> Again = FPlanetBakePipeline::MakeDefault().Bake(C, R2);
	TestTrue(TEXT("同配方两次哈希一致"), Again && Again->ContentHash == Snap->ContentHash);
	FPlanetRecipeConfig Stacked = C;
	TSharedPtr<FGeoBifRule> Bif2 = MakeShared<FGeoBifRule>(); Bif2->MineralId = 1; Bif2->SeedOffset = 7;
	Stacked.MineralRules.Add(Bif2);
	FPlanetBakeReport R3;
	const TSharedPtr<const FPlanetSnapshot> Two = FPlanetBakePipeline::MakeDefault().Bake(Stacked, R3);
	if (TestTrue(TEXT("叠加两份 BIF 能烘焙"), Two.IsValid()))
	{
		TestEqual(TEXT("叠加后 BIF 数翻倍"), GeoMineralTestCount(*Two, 1), 2 * NBif);
	}
	TestTrue(TEXT("种子偏移改哈希"), Stacked.Hash() != C.Hash());

	// 3D 视图：隐藏「无矿」后正对一个 BIF 矿体看过去，画面中心就是这种矿（矿体比步进步长小，靠解析求交）。
	{
		const FGeoDebugLayer* Ore = FPlanetBakePipeline::FindDefaultDebugLayer(TEXT("Mineral"));
		const FGeoPrimitive* Bif = Snap->Primitives.GetPrimitives().FindByPredicate([](const FGeoPrimitive& P) { return P.MineralId == 1; });
		if (TestNotNull(TEXT("矿体图层"), Ore) && TestNotNull(TEXT("有 BIF"), Bif))
		{
			const FVector3d Dir = FVector3d(Bif->Center).GetSafeNormal();
			PlanetGeoDebugView3D::FView V;
			V.Clip = PlanetGeoDebugView3D::EClip::None;
			V.YawDeg = float(FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X)));
			V.PitchDeg = float(FMath::RadiansToDegrees(FMath::Asin(Dir.Z)));
			V.Pan = FVector2f::ZeroVector;
			V.Hidden = { true };
			// 视线中心是球心：相机放在矿体正上方 0.02 R 处，沿径向往下看。
			V.Distance = float((FVector3d(Bif->Center).Size() + 0.02 * Snap->R0) / Snap->R0);
			TArray<FColor> Colors; TArray<FVector3d> Hit; TArray<float> Val;
			const int32 N = 64;
			PlanetGeoDebugView3D::Render(*Snap, *Ore, nullptr, V, N, N, Colors, &Hit, &Val);
			const float Center = Val[(N / 2) * N + N / 2];
			TestEqual(FString::Printf(TEXT("3D 中心看到 BIF（%.0f）"), Center), FMath::RoundToInt(Center), 1);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoMineralRecipeTest, "StellarEra.Planet.Geo.Mineral.Recipe",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoMineralRecipeTest::RunTest(const FString& Parameters)
{
	UPlanetRecipe* Recipe = NewObject<UPlanetRecipe>();
	UGeoMineralCatalog* Catalog = NewObject<UGeoMineralCatalog>();
	Catalog->Minerals = { { TEXT("BIF"), TEXT("条带状铁矿"), FLinearColor::Red }, { TEXT("VMS"), TEXT("块状硫化物"), FLinearColor::Yellow } };
	Recipe->MineralCatalog = Catalog;
	UGeoBifScatterer* Bif = NewObject<UGeoBifScatterer>(Recipe);
	Bif->Count = 12;
	UGeoVmsScatterer* Off = NewObject<UGeoVmsScatterer>(Recipe);
	Off->bEnabled = false;
	UGeoNoduleScatterer* Missing = NewObject<UGeoNoduleScatterer>(Recipe);   // MnNodule 不在目录里
	Recipe->MineralScatterers = { Bif, Off, Missing };

	AddExpectedError(TEXT("不在矿物目录里"), EAutomationExpectedErrorFlags::Contains, 1);
	const FPlanetRecipeConfig C = Recipe->ToConfig(40000.0);
	TestEqual(TEXT("停用与缺目录的撒矿器被过滤"), C.MineralRules.Num(), 1);
	if (C.MineralRules.Num() == 1)
	{
		TestEqual(TEXT("矿物编号 = 目录下标 + 1"), int32(C.MineralRules[0]->MineralId), 1);
		const FGeoBifRule* Rule = static_cast<const FGeoBifRule*>(C.MineralRules[0].Get());
		TestEqual(TEXT("参数拷进规则"), Rule->P.Count, 12);
	}
	TestEqual(TEXT("调色板来自目录"), C.Minerals.Num(), 2);
	TestEqual(TEXT("下拉框列出目录"), Bif->GetMineralOptions().Num(), 2);

	const uint64 H0 = C.Hash();
	Bif->Count = 13;
	AddExpectedError(TEXT("不在矿物目录里"), EAutomationExpectedErrorFlags::Contains, 1);
	TestTrue(TEXT("撒矿器参数进哈希"), Recipe->ToConfig(40000.0).Hash() != H0);
	return true;
}

#endif
