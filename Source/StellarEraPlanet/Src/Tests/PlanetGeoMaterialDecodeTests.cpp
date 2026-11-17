// U1 是材质区分「地表生物群系色」与「挖开岩性色」的唯一依据；取值或带宽错了整颗星都会变岩石色。
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PlanetGeoGenerator.h"
#include "PlanetBakePipeline.h"
#include "PlanetShapeEval.h"
#include "GeoMaterialCodec.h"
#include "VoxelItemStack.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoMaterialSurfaceMaskTest, "StellarEra.Planet.Geo.Material.SurfaceMask",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	/** 沿 Dir、按体素深度 Depth 取生成器物料的 U1。 */
	uint8 GeoMaterialMaskSample(const FPlanetGeoGeneratorInstance& Inst, const FPlanetSnapshot& Snap,
		const FVector3f& Dir, double Depth, int32 LOD, const FVoxelItemStack& Items)
	{
		const double R = PlanetShapeEval::SurfaceRadius(Snap, Dir) - Depth;
		const FVector3d P(double(Dir.X) * R, double(Dir.Y) * R, double(Dir.Z) * R);
		return Inst.GetMaterialImpl(v_flt(P.X), v_flt(P.Y), v_flt(P.Z), LOD, Items).GetU1();
	}

	/** 带宽约定：8 体素起步，粗 LOD 采样点离表面更远，按 2·(1<<LOD) 放宽。 */
	double GeoMaterialMaskBand(int32 LOD) { return 8.0 + 2.0 * double(1 << LOD); }
}

bool FGeoMaterialSurfaceMaskTest::RunTest(const FString& Parameters)
{
	FPlanetRecipeConfig Config;
	Config.Radius = 2000.0;
	Config.FaceRes = 128;
	FPlanetBakeReport Report;
	const TSharedPtr<const FPlanetSnapshot> Snap = FPlanetBakePipeline::MakeDefault().Bake(Config, Report);
	if (!TestTrue(TEXT("烘焙成功"), Snap.IsValid())) { return false; }

	const FVoxelItemStack& Items = FVoxelItemStack::Empty;
	const FVector3f Dir = FVector3f(0.42f, -0.31f, 0.85f).GetSafeNormal();

	UPlanetGeoGenerator* Gen = NewObject<UPlanetGeoGenerator>();
	Gen->DebugColor = EPlanetGeoDebugColor::None;
	Gen->SetSnapshot(Snap);
	const TVoxelSharedRef<FVoxelGeneratorInstance> Base = Gen->GetInstance();
	const FPlanetGeoGeneratorInstance& Inst = static_cast<const FPlanetGeoGeneratorInstance&>(*Base);

	bool bSurface = true, bDeep = true, bMonotonic = true;
	for (int32 LOD = 0; LOD <= 3; ++LOD)
	{
		const double Band = GeoMaterialMaskBand(LOD);
		bSurface &= GeoMaterialMaskSample(Inst, *Snap, Dir, 0.0, LOD, Items) >= 250;
		bDeep &= GeoMaterialMaskSample(Inst, *Snap, Dir, Band, LOD, Items) == 0;
		bDeep &= GeoMaterialMaskSample(Inst, *Snap, Dir, Band * 2.0 + 4.0, LOD, Items) == 0;

		uint8 Prev = 255;
		for (double Depth = 0.0; Depth <= Band + 1e-9; Depth += 0.25)
		{
			const uint8 U1 = GeoMaterialMaskSample(Inst, *Snap, Dir, Depth, LOD, Items);
			bMonotonic &= U1 <= Prev;
			Prev = U1;
		}
	}
	TestTrue(TEXT("地表 U1 ≥ 250"), bSurface);
	TestTrue(TEXT("深度 ≥ 带宽处 U1 为 0"), bDeep);
	TestTrue(TEXT("中间段随深度单调不增"), bMonotonic);

	// 调试着色只改 RGB，遮罩必须照写，否则调试模式下材质拿不到地表信息。
	UPlanetGeoGenerator* Debug = NewObject<UPlanetGeoGenerator>();
	Debug->DebugColor = EPlanetGeoDebugColor::Lithology;
	Debug->SetSnapshot(Snap);
	const TVoxelSharedRef<FVoxelGeneratorInstance> DebugBase = Debug->GetInstance();
	const FPlanetGeoGeneratorInstance& DebugInst = static_cast<const FPlanetGeoGeneratorInstance&>(*DebugBase);

	bool bDebugSame = true;
	for (int32 LOD = 0; LOD <= 3; ++LOD)
	{
		const double Band = GeoMaterialMaskBand(LOD);
		for (double Depth = 0.0; Depth <= Band + 1e-9; Depth += 0.25)
		{
			bDebugSame &= GeoMaterialMaskSample(DebugInst, *Snap, Dir, Depth, LOD, Items)
				== GeoMaterialMaskSample(Inst, *Snap, Dir, Depth, LOD, Items);
		}
	}
	TestTrue(TEXT("调试着色不影响 U1"), bDebugSame);
	return true;
}

#endif
