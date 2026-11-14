// 地质可视化：图层表完整、调试场不改哈希、剖面与配方深度一致、地图海陆占比、贴图 Outer、剖切、导出。
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/TextureCube.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "GeoLithology.h"
#include "GeoRng.h"
#include "PlanetBakePipeline.h"
#include "PlanetGeoDebugRender.h"
#include "PlanetGeoGenerator.h"
#include "PlanetShapeEval.h"
#include "PlanetStage.h"
#include "VoxelItemStack.h"
#include "VoxelIntBox.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FPlanetRecipeConfig GeoDebugTestConfig(bool bKeep)
	{
		FPlanetRecipeConfig C;
		C.Radius = 4000.0;
		C.FaceRes = 128;
		C.bKeepDebugFields = bKeep;
		return C;
	}

	/** 地壳的三种编号（未分 / 陆壳 / 洋壳）算同一层：剖面上它们的分界是海陆，不是圈层。 */
	int32 GeoDebugTestLayerGroup(float V)
	{
		const int32 Id = FMath::RoundToInt(V);
		return (Id == GeoLithology::ContinentalCrust || Id == GeoLithology::OceanicCrust) ? GeoLithology::Crust : Id;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoDebugRegistryTest, "StellarEra.Planet.Geo.Debug.Registry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoDebugRegistryTest::RunTest(const FString& Parameters)
{
	const TArray<FGeoDebugLayer>& Layers = FPlanetBakePipeline::DefaultDebugLayers();
	TSet<FName> Ids;
	for (const FGeoDebugLayer& L : Layers)
	{
		TestFalse(FString::Printf(TEXT("图层编号不重复：%s"), *L.Id.ToString()), Ids.Contains(L.Id));
		Ids.Add(L.Id);
		TestTrue(FString::Printf(TEXT("%s 有取值函数"), *L.Id.ToString()), bool(L.Sample));
		TestFalse(FString::Printf(TEXT("%s 有中文名"), *L.Id.ToString()), L.DisplayName.IsEmpty());
		if (L.Kind == EGeoDebugKind::Categorical)
		{
			TestTrue(FString::Printf(TEXT("%s 分类图层有图例"), *L.Id.ToString()), L.Legend.Num() >= 2);
		}
	}
	// 每个默认阶段都至少登记了一个图层：新阶段忘了 DescribeDebugLayers 会在这里暴露。
	// 管线必须留在局部：Sorted() 返回的是管线内阶段的裸指针，临时对象析构后全悬空。
	const FPlanetBakePipeline Pipeline = FPlanetBakePipeline::MakeDefault();
	for (const IPlanetStage* S : Pipeline.Sorted())
	{
		const uint32 Index = S->GetStageIndex();
		TestTrue(FString::Printf(TEXT("阶段 %u 至少一个图层"), Index),
			Layers.ContainsByPredicate([Index](const FGeoDebugLayer& L) { return L.Stage == Index; }));
	}
	for (const TCHAR* Id : { TEXT("Lithology"), TEXT("MohoWobble"), TEXT("Elevation"), TEXT("PlateId"), TEXT("BoundaryType"),
		TEXT("CrustType"), TEXT("ContinentalMask"), TEXT("CrustAge"), TEXT("CrustThickness") })
	{
		TestNotNull(FString::Printf(TEXT("能按编号找到 %s"), Id), FPlanetBakePipeline::FindDefaultDebugLayer(Id));
	}
	const FGeoDebugLayer* Lith = FPlanetBakePipeline::FindDefaultDebugLayer(TEXT("Lithology"));
	TestTrue(TEXT("Lithology 是地下图层"), Lith && Lith->Domain == EGeoDebugDomain::Volume);
	TestNull(TEXT("空名字找不到"), FPlanetBakePipeline::FindDefaultDebugLayer(NAME_None));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoDebugFieldsTest, "StellarEra.Planet.Geo.Debug.Fields",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoDebugFieldsTest::RunTest(const FString& Parameters)
{
	FPlanetBakeReport R1, R2;
	const TSharedPtr<const FPlanetSnapshot> Plain = FPlanetBakePipeline::MakeDefault().Bake(GeoDebugTestConfig(false), R1);
	const TSharedPtr<const FPlanetSnapshot> Debug = FPlanetBakePipeline::MakeDefault().Bake(GeoDebugTestConfig(true), R2);
	if (!TestTrue(TEXT("两次都烘焙成功"), Plain.IsValid() && Debug.IsValid())) { return false; }
	TestEqual(TEXT("保留调试场不改内容哈希"), Debug->ContentHash, Plain->ContentHash);
	TestTrue(TEXT("不保留时没有调试场"), Plain->DebugFields.IsEmpty());
	for (const TCHAR* Id : { TEXT("PlateId"), TEXT("BoundaryType"), TEXT("CrustType"), TEXT("ContinentalMask"), TEXT("CrustAge"), TEXT("CrustThickness") })
	{
		const FGeoField2D* F = Debug->DebugFields.Find(Id);
		TestTrue(FString::Printf(TEXT("调试场 %s 存在且非空"), Id), F && F->Res > 0);
	}

	// 无调试场时辅助图层返回「无数据」，不崩。
	const FGeoDebugLayer* Plate = FPlanetBakePipeline::FindDefaultDebugLayer(TEXT("PlateId"));
	const FVector3d P(0.0, 0.0, 4000.0);
	TestTrue(TEXT("无调试场 → 板块图层无数据"), FMath::IsNaN(Plate->Sample(*Plain, P)));
	const float Id = Plate->Sample(*Debug, P);
	TestTrue(FString::Printf(TEXT("有调试场 → 板块编号合法（%.0f）"), Id), Id >= 0.f && Id < 15.f && Id == FMath::RoundToFloat(Id));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoDebugRenderTest, "StellarEra.Planet.Geo.Debug.Render",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoDebugRenderTest::RunTest(const FString& Parameters)
{
	const FPlanetRecipeConfig C = GeoDebugTestConfig(true);
	FPlanetBakeReport Report;
	const TSharedPtr<const FPlanetSnapshot> Snap = FPlanetBakePipeline::MakeDefault().Bake(C, Report);
	if (!TestTrue(TEXT("烘焙成功"), Snap.IsValid())) { return false; }

	// 剖面：中间一列从上往下扫，圈层分界深度必须与配方比例一致。
	{
		const FGeoDebugLayer* Lith = FPlanetBakePipeline::FindDefaultDebugLayer(TEXT("Lithology"));
		const int32 Size = 1024;
		TArray<FColor> Colors;
		TArray<float> Values;
		PlanetGeoDebugRender::RenderSection(*Snap, *Lith, Size, PlanetGeoDebugRender::ESectionPlane::XZ, Colors, &Values);
		TestEqual(TEXT("剖面像素数"), Colors.Num(), Size * Size);
		TArray<double> Depths;
		int32 Prev = -1;
		for (int32 Y = 0; Y < Size / 2; ++Y)
		{
			const float V = Values[Y * Size + Size / 2];
			if (FMath::IsNaN(V)) { continue; }
			const int32 G = GeoDebugTestLayerGroup(V);
			if (Prev >= 0 && G != Prev)
			{
				Depths.Add(C.Radius - PlanetGeoDebugRender::SectionPixelToPos(*Snap, Size / 2, Y, Size,
					PlanetGeoDebugRender::ESectionPlane::XZ).Size());
			}
			Prev = G;
		}
		const float Ratios[5] = { C.Sphere.CrustRatio, C.Sphere.UpperMantleRatio, C.Sphere.TransitionRatio,
			C.Sphere.MantleOuterRatio, C.Sphere.OuterCoreRatio };
		const double Pixel = 2.0 * PlanetGeoDebugRender::SectionHalfExtent(*Snap) / double(Size);
		if (TestEqual(TEXT("剖面上能数出 5 条圈层分界"), Depths.Num(), 5))
		{
			for (int32 i = 0; i < 5; ++i)
			{
				const double Want = double(Ratios[i]) * C.Radius;
				// 莫霍面带 1% R 的起伏，其余界面 0.2%；再加两个像素的量化。
				const double Tol = (i == 0 ? C.Sphere.MohoWobbleRatio : 0.002) * C.Radius + 2.0 * Pixel;
				TestTrue(FString::Printf(TEXT("分界 %d：实测 %.0f，配方 %.0f，容差 %.0f"), i, Depths[i], Want, Tol),
					FMath::Abs(Depths[i] - Want) <= Tol);
			}
		}
		TestTrue(TEXT("剖面角上是太空（无数据）"), FMath::IsNaN(Values[0]));
	}

	// 展开地图：按 cos(纬度) 加权的 E>0 占比 = 配方陆地占比。
	{
		const FGeoDebugLayer* Elev = FPlanetBakePipeline::FindDefaultDebugLayer(TEXT("Elevation"));
		const int32 W = 512, H = 256;
		TArray<FColor> Colors;
		TArray<float> Values;
		PlanetGeoDebugRender::RenderMap(*Snap, *Elev, W, H, Colors, &Values);
		double Land = 0.0, Total = 0.0;
		for (int32 Y = 0; Y < H; ++Y)
		{
			const double Wt = FMath::Cos(((double(Y) + 0.5) / H - 0.5) * PI);
			for (int32 X = 0; X < W; ++X) { Total += Wt; Land += Values[Y * W + X] > 0.f ? Wt : 0.0; }
		}
		const double Frac = Land / Total;
		TestTrue(FString::Printf(TEXT("地图陆地占比 %.4f ≈ %.2f"), Frac, C.Continents.LandFrac),
			FMath::Abs(Frac - C.Continents.LandFrac) <= 0.02);
	}

	// 叠加贴图：瞬态包当 Outer，否则挂进体素材质管理器后会让切图泄漏崩溃。
	{
		UTextureCube* Cube = PlanetGeoDebugRender::BakeCube(*Snap, *FPlanetBakePipeline::FindDefaultDebugLayer(TEXT("PlateId")), 32);
		if (TestNotNull(TEXT("立方体贴图建成"), Cube))
		{
			TestTrue(TEXT("贴图 Outer 是瞬态包"), Cube->GetOuter() == GetTransientPackage());
		}
	}

	// 导出：每个图层两张图。
	{
		const FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("GeoDebugExport"));
		IFileManager::Get().DeleteDirectory(*Dir, false, true);
		const TArray<FGeoDebugLayer>& Layers = FPlanetBakePipeline::DefaultDebugLayers();
		const int32 N = PlanetGeoDebugRender::ExportAll(*Snap, Layers, Dir, 128);
		TestEqual(TEXT("每个图层导出两张 PNG"), N, 2 * Layers.Num());
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *FPaths::Combine(Dir, TEXT("*.png")), true, false);
		TestEqual(TEXT("磁盘上的 PNG 数"), Files.Num(), 2 * Layers.Num());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoDebugCutawayTest, "StellarEra.Planet.Geo.Debug.Cutaway",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoDebugCutawayTest::RunTest(const FString& Parameters)
{
	FPlanetBakeReport Report;
	const TSharedPtr<const FPlanetSnapshot> Snap = FPlanetBakePipeline::MakeDefault().Bake(GeoDebugTestConfig(true), Report);
	if (!TestTrue(TEXT("烘焙成功"), Snap.IsValid())) { return false; }

	UPlanetGeoGenerator* Gen = NewObject<UPlanetGeoGenerator>();
	Gen->SetSnapshot(Snap);
	Gen->Cutaway = EPlanetGeoCutaway::Half;
	Gen->VolumeLayer = TEXT("Lithology");
	const TVoxelSharedRef<FVoxelGeneratorInstance> Base = Gen->GetInstance();
	const FPlanetGeoGeneratorInstance& Inst = static_cast<const FPlanetGeoGeneratorInstance&>(*Base);
	const FVoxelItemStack& Items = FVoxelItemStack::Empty;

	TestTrue(TEXT("+X 半球内部被切成空气"), Inst.GetValueImpl(1000.0, 0.0, 0.0, 0, Items) > 0);
	TestTrue(TEXT("−X 半球内部仍是实心"), Inst.GetValueImpl(-1000.0, 0.0, 0.0, 0, Items) < 0);
	TestTrue(TEXT("切面以外的太空仍是空气"), Inst.GetValueImpl(-5000.0, 0.0, 0.0, 0, Items) > 0);

	// 切面（X≈0）着色 = 岩性图层在该点的颜色。
	const FGeoDebugLayer* Lith = FPlanetBakePipeline::FindDefaultDebugLayer(TEXT("Lithology"));
	int32 Bad = 0;
	for (int32 i = 1; i <= 7; ++i)
	{
		const FVector3d P(-1.0, 0.0, 500.0 * i - 2000.0);
		const FVoxelMaterial M = Inst.GetMaterialImpl(P.X, P.Y, P.Z, 0, Items);
		const FColor Want = Lith->ColorOf(*Snap, Lith->Sample(*Snap, P));
		Bad += (M.GetR() != Want.R || M.GetG() != Want.G || M.GetB() != Want.B) ? 1 : 0;
	}
	TestEqual(TEXT("切面颜色 = 岩性图层颜色"), Bad, 0);

	// 值域保守：盒内采样都落在 GetValueRange 里（剖切把密度改成 max(V, 切距)）。
	FGeoRng Rng = FGeoRng::Derive(5, 0, 0);
	bool bCovers = true;
	for (int32 i = 0; i < 3000; ++i)
	{
		const FIntVector Cn(int32(Rng.Range(-4200.f, 4200.f)), int32(Rng.Range(-4200.f, 4200.f)), int32(Rng.Range(-4200.f, 4200.f)));
		const FVoxelIntBox Box(Cn - FIntVector(32), Cn + FIntVector(32));
		const TVoxelRange<v_flt> R = Inst.GetValueRangeImpl(Box, 0, Items);
		const v_flt V = Inst.GetValueImpl(Cn.X + Rng.Range(-32.f, 32.f), Cn.Y + Rng.Range(-32.f, 32.f), Cn.Z + Rng.Range(-32.f, 32.f), 0, Items);
		bCovers &= V >= R.Min && V <= R.Max;
	}
	TestTrue(TEXT("剖切后值域仍覆盖盒内采样"), bCovers);
	return true;
}

#endif
