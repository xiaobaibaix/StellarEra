// U1 是材质区分「地表生物群系色」与「挖开岩性色」的唯一依据；取值或带宽错了整颗星都会变岩石色。
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PlanetGeoGenerator.h"
#include "PlanetBakePipeline.h"
#include "PlanetShapeEval.h"
#include "GeoMaterialCodec.h"
#include "GeoPalette.h"
#include "GeoLithology.h"
#include "GeoMineral.h"
#include "GeoPrimitive.h"
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

	bool bSurface = true, bDeep = true, bMonotonic = true, bMidBand = true;
	for (int32 LOD = 0; LOD <= 3; ++LOD)
	{
		const double Band = GeoMaterialMaskBand(LOD);
		bSurface &= GeoMaterialMaskSample(Inst, *Snap, Dir, 0.0, LOD, Items) >= 250;
		bDeep &= GeoMaterialMaskSample(Inst, *Snap, Dir, Band, LOD, Items) == 0;
		bDeep &= GeoMaterialMaskSample(Inst, *Snap, Dir, Band * 2.0 + 4.0, LOD, Items) == 0;

		// 半带宽处应为半值；抹掉 LOD 项时 LOD 3 这里会掉到 0。
		if (LOD == 0 || LOD == 3)
		{
			const uint8 Mid = GeoMaterialMaskSample(Inst, *Snap, Dir, Band * 0.5, LOD, Items);
			bMidBand &= FMath::Abs(int32(Mid) - 128) <= 3;
		}

		uint8 Prev = 255;
		for (double Depth = 0.0; Depth <= Band + 1e-9; Depth += 0.25)
		{
			const uint8 U1 = GeoMaterialMaskSample(Inst, *Snap, Dir, Depth, LOD, Items);
			bMonotonic &= U1 <= Prev;
			Prev = U1;
		}
	}
	// 地表之上（负深度）必须仍是满遮罩：写成 Abs(Surface − D) 会掉到 ~153。
	const bool bAboveSurface = GeoMaterialMaskSample(Inst, *Snap, Dir, -4.0, 0, Items) == 255;

	TestTrue(TEXT("地表 U1 ≥ 250"), bSurface);
	TestTrue(TEXT("深度 ≥ 带宽处 U1 为 0"), bDeep);
	TestTrue(TEXT("中间段随深度单调不增"), bMonotonic);
	TestTrue(TEXT("半带宽处 U1 ≈ 128"), bMidBand);
	TestTrue(TEXT("地表之上 U1 == 255"), bAboveSurface);

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoPaletteLUTTest, "StellarEra.Planet.Geo.Material.Palette",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoPaletteLUTTest::RunTest(const FString& Parameters)
{
	TArray<FGeoMineralInfo> Minerals;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		FGeoMineralInfo M;
		M.Name = FString::Printf(TEXT("矿%d"), Index);
		M.Color = FColor(uint8(10 + Index * 20), uint8(200 - Index * 30), uint8(50 + Index * 40));
		Minerals.Add(M);
	}

	const TArray<FColor> LUT = GeoPalette::BuildLUT(Minerals);
	if (!TestEqual(TEXT("LUT 长度 = 256×3"), LUT.Num(), GeoPalette::Width * GeoPalette::Rows)) { return false; }

	bool bLithology = true, bLithologyRest = true, bMineral = true, bMineralRest = true, bClue = true, bClueRest = true;
	for (int32 X = 0; X < GeoPalette::Width; ++X)
	{
		if (X < GeoLithology::Count) { bLithology &= LUT[X] == GeoLithology::DebugColor(uint8(X)); }
		else { bLithologyRest &= LUT[X] == FColor::Black; }

		// 矿物编号 = 下标 + 1：0 号留黑，越界也黑。
		if (X == 0) { bMineralRest &= LUT[GeoPalette::Width + X] == FColor::Black; }
		else if (X <= Minerals.Num()) { bMineral &= LUT[GeoPalette::Width + X] == Minerals[X - 1].Color; }
		else { bMineralRest &= LUT[GeoPalette::Width + X] == FColor::Black; }

		if (X < GeoClue::Count) { bClue &= LUT[2 * GeoPalette::Width + X] == GeoClue::DebugColor(uint8(X)); }
		else { bClueRest &= LUT[2 * GeoPalette::Width + X] == FColor::Black; }
	}
	TestTrue(TEXT("行 0 岩性色与 GeoLithology::DebugColor 逐项相等"), bLithology);
	TestTrue(TEXT("行 0 越界为黑"), bLithologyRest);
	TestTrue(TEXT("行 1 矿物色与传入调色板逐项相等"), bMineral);
	TestTrue(TEXT("行 1 的 0 号与越界为黑"), bMineralRest);
	TestTrue(TEXT("行 2 线索色与 GeoClue::DebugColor 逐项相等"), bClue);
	TestTrue(TEXT("行 2 越界为黑"), bClueRest);
	return true;
}

#endif
