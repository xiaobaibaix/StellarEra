// U1 是材质区分「地表生物群系色」与「挖开岩性色」的唯一依据；取值或带宽错了整颗星都会变岩石色。
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PlanetGeoGenerator.h"
#include "PlanetBakePipeline.h"
#include "PlanetShapeEval.h"
#include "PlanetMaterialEval.h"
#include "GeoMaterialCodec.h"
#include "GeoPalette.h"
#include "GeoLithology.h"
#include "GeoMineral.h"
#include "GeoPrimitive.h"
#include "VoxelItemStack.h"
#include "VoxelPlanetActor.h"
#include "PlanetSnapshot.h"
#include "PlanetRecipe.h"
#include "GeoMaterialBuilder.h"
#include "Materials/Material.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialExpressionFunctionOutput.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureSampleParameter.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureCube.h"

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

// 材质图无法单测：只冒烟「资产在、函数调用指向 MF、参数齐、重建幂等」。视觉验收才是主判据。
// 表达式 API 与 BuildAll 都是 WITH_EDITOR-only，而 WITH_DEV_AUTOMATION_TESTS 在 Game 目标同样为 1。
#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoMaterialAssetsTest, "StellarEra.Planet.Geo.Material.Assets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoMaterialAssetsTest::RunTest(const FString& Parameters)
{
	// 先求值再拼消息：Printf 参数里的调用顺序没有保证。
	FString Error;
	const bool bBuilt = GeoMaterialBuild::BuildAll(Error);
	if (!TestTrue(FString::Printf(TEXT("BuildAll 成功：%s"), *Error), bBuilt))
	{
		return false;
	}

	UMaterialFunction* Function = LoadObject<UMaterialFunction>(nullptr, GeoMaterialBuild::GetDecodeFunctionPath());
	UMaterial* Material = LoadObject<UMaterial>(nullptr, GeoMaterialBuild::GetPlanetMaterialPath());
	if (!TestNotNull(TEXT("MF_GeoDecode"), Function) || !TestNotNull(TEXT("M_GeoPlanet"), Material))
	{
		return false;
	}

	UMaterialExpressionMaterialFunctionCall* Call = nullptr;
	bool bPalette = false, bBiomeCube = false, bBlend = false;
	for (UMaterialExpression* Expression : Material->GetExpressionCollection().Expressions)
	{
		if (UMaterialExpressionMaterialFunctionCall* Found = Cast<UMaterialExpressionMaterialFunctionCall>(Expression))
		{
			if (Found->MaterialFunction.Get() == Function)
			{
				Call = Found;
			}
		}

		if (const UMaterialExpressionTextureSampleParameter* Texture = Cast<UMaterialExpressionTextureSampleParameter>(Expression))
		{
			bPalette |= Texture->ParameterName == FName(TEXT("GeoPalette"));
			bBiomeCube |= Texture->ParameterName == FName(TEXT("BiomeCube"));
		}

		if (const UMaterialExpressionScalarParameter* Scalar = Cast<UMaterialExpressionScalarParameter>(Expression))
		{
			if (Scalar->ParameterName == FName(TEXT("BiomeCubeBlend")))
			{
				bBlend = true;
				TestEqual(TEXT("BiomeCubeBlend 默认 1"), Scalar->DefaultValue, 1.f);
			}
		}
	}

	TestTrue(TEXT("有指向 MF_GeoDecode 的函数调用"), Call != nullptr);
	TestTrue(TEXT("纹理参数 GeoPalette"), bPalette);
	TestTrue(TEXT("纹理参数 BiomeCube"), bBiomeCube);
	TestTrue(TEXT("标量参数 BiomeCubeBlend"), bBlend);
	if (!Call)
	{
		return false;
	}

	TestEqual(TEXT("函数调用 3 个输入"), Call->FunctionInputs.Num(), 3);

	TSet<FName> OutputNames;
	for (const FFunctionExpressionOutput& FunctionOutput : Call->FunctionOutputs)
	{
		if (FunctionOutput.ExpressionOutput)
		{
			OutputNames.Add(FunctionOutput.ExpressionOutput->OutputName);
		}
	}
	TestTrue(TEXT("输出 RockColor"), OutputNames.Contains(FName(TEXT("RockColor"))));
	TestTrue(TEXT("输出 MineralCover"), OutputNames.Contains(FName(TEXT("MineralCover"))));
	TestTrue(TEXT("输出 SurfaceMask"), OutputNames.Contains(FName(TEXT("SurfaceMask"))));
	TestTrue(TEXT("输出 Debug"), OutputNames.Contains(FName(TEXT("Debug"))));

	const int32 MaterialExpressions = Material->GetExpressionCollection().Expressions.Num();
	const int32 FunctionExpressions = Function->GetExpressionCollection().Expressions.Num();
	if (!TestTrue(TEXT("第二次 BuildAll 成功"), GeoMaterialBuild::BuildAll(Error)))
	{
		return false;
	}

	TestEqual(TEXT("重建后材质表达式数不变"), Material->GetExpressionCollection().Expressions.Num(), MaterialExpressions);
	TestEqual(TEXT("重建后函数表达式数不变"), Function->GetExpressionCollection().Expressions.Num(), FunctionExpressions);
	return true;
}
#endif // WITH_EDITOR

// geo 行星的材质必须由 ApplyGeoDebugOverlay 装，否则调色板与 BiomeCube
// 在唯一有快照的模式里全是死代码（地表一片黑 / 岩性解不出来）。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoMaterialActorGeoTest, "StellarEra.Planet.Geo.Material.ActorGeoMaterial",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoMaterialActorGeoTest::RunTest(const FString& Parameters)
{
	AVoxelPlanetActor* Planet = NewObject<AVoxelPlanetActor>(GetTransientPackage());
	if (!TestNotNull(TEXT("行星 Actor"), Planet)) { return false; }
	Planet->GeoRecipe = NewObject<UPlanetRecipe>(GetTransientPackage());
	if (!TestNotNull(TEXT("配方"), Planet->GeoRecipe.Get())) { return false; }

	TSharedPtr<FPlanetSnapshot> Snap = MakeShared<FPlanetSnapshot>();
	Snap->R0 = 1000.0;
	Snap->MinSurfaceRadius = Snap->MaxSurfaceRadius = Snap->R0;
	FGeoMineralInfo Mineral;
	Mineral.Name = TEXT("矿1");
	Mineral.Color = FColor(30, 190, 70);
	Snap->Minerals.Add(Mineral);
	// Biome 层从 DebugFields 取场；缺了只能烘出一片「无数据」色，这条测试就测不到底图接线。
	FGeoField2D Biome;
	Biome.Init(16);
	Biome.Fill([](const FVector3f&) { return 3.f; });
	Snap->DebugFields.Add(TEXT("Biome"), MoveTemp(Biome));
	Snap->FinalizeHash();
	Planet->SetGeoSnapshotForTest(Snap);

	Planet->VoxelMaterial = NewObject<UMaterial>(GetTransientPackage());
	// 默认没选叠加图层：材质只带 Biome 底图 + 调色板，混合权重应为 1。
	Planet->ApplyGeoDebugOverlayForTest();

	UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Planet->VoxelMaterial.Get());
	if (!TestNotNull(TEXT("geo 行星的 VoxelMaterial 是 MID"), MID)) { return false; }

	UTexture* Palette = nullptr;
	TestTrue(TEXT("MID 上有 GeoPalette"),
		MID->GetTextureParameterValue(FMaterialParameterInfo(TEXT("GeoPalette")), Palette));
	UTexture2D* LUT = Cast<UTexture2D>(Palette);
	if (TestNotNull(TEXT("GeoPalette 是 Texture2D"), LUT))
	{
		TestEqual(TEXT("LUT 宽 256"), LUT->GetSizeX(), GeoPalette::Width);
		TestEqual(TEXT("LUT 高 3"), LUT->GetSizeY(), GeoPalette::Rows);
		TestTrue(TEXT("纹理 Outer 是瞬态包"), LUT->GetOuter() == GetTransientPackage());
		TestTrue(TEXT("LUT 是 sRGB"), LUT->SRGB);
		TestTrue(TEXT("最近邻采样（双线性会把矿色与 0 号黑混开）"), LUT->Filter == TF_Nearest);
		TestTrue(TEXT("U 方向钳制（Wrap 会让 255 号绕回 0）"), LUT->AddressX == TA_Clamp);
		TestTrue(TEXT("V 方向钳制"), LUT->AddressY == TA_Clamp);

		// Mip0 逐字节对照纯函数：FColor 的内存布局就是 BGRA。
		const TArray<FColor> Expected = GeoPalette::BuildLUT(Snap->Minerals);
		FTexture2DMipMap& Mip = LUT->GetPlatformData()->Mips[0];
		const FColor* Pixels = static_cast<const FColor*>(Mip.BulkData.Lock(LOCK_READ_ONLY));
		if (TestNotNull(TEXT("Mip0 可加锁读取"), Pixels))
		{
			TestEqual(TEXT("岩性 1 号像素与 BuildLUT 一致"),
				Pixels[1].ToPackedARGB(), Expected[1].ToPackedARGB());
			TestEqual(TEXT("矿物 1 号像素与 BuildLUT 一致"),
				Pixels[GeoPalette::Width + 1].ToPackedARGB(),
				Expected[GeoPalette::Width + 1].ToPackedARGB());
			Mip.BulkData.Unlock();
		}
	}

	UTexture* Cube = nullptr;
	TestTrue(TEXT("MID 上有 BiomeCube"),
		MID->GetTextureParameterValue(FMaterialParameterInfo(TEXT("BiomeCube")), Cube));
	TestNotNull(TEXT("BiomeCube 非空"), Cube);

	float Blend = -1.f;
	TestTrue(TEXT("MID 上有 BiomeCubeBlend"),
		MID->GetScalarParameterValue(FMaterialParameterInfo(TEXT("BiomeCubeBlend")), Blend));
	TestEqual(TEXT("没有叠加时混合权重 = 1"), Blend, 1.f);
	return true;
}

// 剖切面若被调试图层盖成调色板色，材质就解不出岩性；默认不叠图层时 RGB 必须保持 Encode 的结果。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoMaterialCutawayEncodedTest, "StellarEra.Planet.Geo.Material.CutawayEncoded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoMaterialCutawayEncodedTest::RunTest(const FString& Parameters)
{
	FPlanetRecipeConfig Config;
	Config.Radius = 2000.0;
	Config.FaceRes = 128;
	FPlanetBakeReport Report;
	const TSharedPtr<const FPlanetSnapshot> Snap = FPlanetBakePipeline::MakeDefault().Bake(Config, Report);
	if (!TestTrue(TEXT("烘焙成功"), Snap.IsValid())) { return false; }

	// 生成器默认 DebugColor 是 Lithology，这里显式关掉；剖切面不选图层，切面应保留地质编码。
	UPlanetGeoGenerator* Gen = NewObject<UPlanetGeoGenerator>();
	Gen->DebugColor = EPlanetGeoDebugColor::None;
	Gen->Cutaway = EPlanetGeoCutaway::Half;
	Gen->SetSnapshot(Snap);
	const TVoxelSharedRef<FVoxelGeneratorInstance> Base = Gen->GetInstance();
	const FPlanetGeoGeneratorInstance& Inst = static_cast<const FPlanetGeoGeneratorInstance&>(*Base);

	// Half 切掉 +X 半球：取 +X 方向地表点，既在切区内，U1 又是满遮罩。
	const FVector3f Dir = FVector3f(1.f, 0.f, 0.f);
	const double Surface = PlanetShapeEval::SurfaceRadius(*Snap, Dir);
	const FVector3d P = FVector3d(Dir) * Surface;
	const FGeoPackedMaterial Expected = GeoMaterialCodec::Encode(PlanetMaterialEval::Sample(*Snap, P));

	const FVoxelMaterial M = Inst.GetMaterialImpl(
		v_flt(P.X), v_flt(P.Y), v_flt(P.Z), 0, FVoxelItemStack::Empty);
	TestEqual(TEXT("切面 R = 岩性编号（不是调试色）"), int32(M.GetR()), int32(Expected.R));
	TestEqual(TEXT("切面 G = 矿物编号"), int32(M.GetG()), int32(Expected.G));
	TestEqual(TEXT("切面 B = 品位"), int32(M.GetB()), int32(Expected.B));
	TestEqual(TEXT("切面 A = 线索"), int32(M.GetA()), int32(Expected.A));
	TestEqual(TEXT("地表点 U1 = 255"), int32(M.GetU1()), 255);
	return true;
}

// 打包后 bKeepDebugFields=false：Biome 层仍注册但 DebugFields 为空。此时必须退回父材质，
// 否则全 NoData 灰立方体会以 BiomeCubeBlend=1 盖满整星（还白烘一次 512²×6）。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoMaterialPackagedNoBiomeFieldTest,
	"StellarEra.Planet.Geo.Material.PackagedNoBiomeField",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoMaterialPackagedNoBiomeFieldTest::RunTest(const FString& Parameters)
{
	AVoxelPlanetActor* Planet = NewObject<AVoxelPlanetActor>(GetTransientPackage());
	if (!TestNotNull(TEXT("行星 Actor"), Planet)) { return false; }
	Planet->GeoRecipe = NewObject<UPlanetRecipe>(GetTransientPackage());
	if (!TestNotNull(TEXT("配方"), Planet->GeoRecipe.Get())) { return false; }

	TSharedPtr<FPlanetSnapshot> Snap = MakeShared<FPlanetSnapshot>();
	Snap->R0 = 1000.0;
	Snap->MinSurfaceRadius = Snap->MaxSurfaceRadius = Snap->R0;
	FGeoMineralInfo Mineral;
	Mineral.Name = TEXT("矿1");
	Mineral.Color = FColor::Red;
	Snap->Minerals.Add(Mineral);
	// 刻意不填 DebugFields：这正是打包后快照的形状。
	Snap->FinalizeHash();
	Planet->SetGeoSnapshotForTest(Snap);

	UMaterial* Base = NewObject<UMaterial>(GetTransientPackage());
	Planet->VoxelMaterial = Base;

	TestFalse(TEXT("没有 Biome 场时不装 MID"), Planet->ApplyGeoDebugOverlayForTest());
	TestTrue(TEXT("VoxelMaterial 保持父材质"), Planet->VoxelMaterial == Base);
	TestNull(TEXT("没有留下 MID"), Planet->BiomeMaterial.Get());
	TestNull(TEXT("跳过调色板，不建 LUT"), Planet->GeoPaletteTexture.Get());
	return true;
}

#endif
