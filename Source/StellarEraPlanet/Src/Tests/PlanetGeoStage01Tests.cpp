// 阶段 1 是「挖到哪一层」的唯一来源：层序照配方、起伏有界、非法比例拒绝烘焙。
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PlanetBakePipeline.h"
#include "PlanetMaterialEval.h"
#include "GeoLithology.h"
#include "GeoRng.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage01Test, "StellarEra.Planet.Geo.Stage01",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage01Test::RunTest(const FString& Parameters)
{
	FPlanetRecipeConfig Config;
	Config.FaceRes = 256;
	FPlanetBakeReport Report;
	const TSharedPtr<const FPlanetSnapshot> Snap = FPlanetBakePipeline::MakeDefault().Bake(Config, Report);
	if (!TestTrue(TEXT("默认配方烘焙成功"), Snap.IsValid())) { return false; }
	TestEqual(TEXT("6 层"), Snap->Radial.Layers.Num(), 6);
	TestEqual(TEXT("起伏场分辨率 = FaceRes/4"), Snap->MohoWobble.Res, 64);

	const float Mid[6] = { 0.12f, 0.31f, 0.40f, 0.48f, 0.67f, 0.90f };
	const uint8 Expect[6] = { GeoLithology::Crust, GeoLithology::UpperMantle, GeoLithology::Transition,
		GeoLithology::LowerMantle, GeoLithology::OuterCore, GeoLithology::InnerCore };
	FGeoRng Rng = FGeoRng::Derive(9, 0, 0);
	bool bLayers = true;
	float MinW = 1.f, MaxW = -1.f;
	for (int32 i = 0; i < 100; ++i)
	{
		const float Z = Rng.Range(-1.f, 1.f), Phi = Rng.Range(0.f, 2.f * PI), S = FMath::Sqrt(1.f - Z * Z);
		const FVector3d Dir(S * FMath::Cos(Phi), S * FMath::Sin(Phi), Z);
		// 地壳层不再按深度分岩性，按海平面分（阶段 2）。
		const uint8 ExpectedCrust = Snap->Elevation.Res > 0
			&& Snap->Elevation.Sample(FVector3f(Dir)) < Snap->CrustSplitElevation
			? GeoLithology::OceanicCrust : GeoLithology::ContinentalCrust;
		for (int32 L = 0; L < 6; ++L)
		{
			const uint8 Want = L == 0 ? ExpectedCrust : Expect[L];
			bLayers &= PlanetMaterialEval::Sample(*Snap, Dir * (Config.Radius * (1.0 - Mid[L]))).Lithology == Want;
		}
		const float W = Snap->MohoWobble.Sample(FVector3f(Dir));
		MinW = FMath::Min(MinW, W);
		MaxW = FMath::Max(MaxW, W);
	}
	TestTrue(TEXT("100 个虚拟钻孔层序都符合配方"), bLayers);
	TestTrue(FString::Printf(TEXT("起伏场 ∈ [-1,1] 且确有起伏 (Min=%f Max=%f)"), MinW, MaxW),
		MinW >= -1.f && MaxW <= 1.f && MaxW - MinW > 0.2f);

	FPlanetRecipeConfig Single = Config;
	Single.bSingleThreadBake = true;
	FPlanetBakeReport R2;
	const TSharedPtr<const FPlanetSnapshot> SnapSingle = FPlanetBakePipeline::MakeDefault().Bake(Single, R2);
	TestTrue(TEXT("单线程与并行快照哈希一致"), SnapSingle && SnapSingle->ContentHash == Snap->ContentHash);

	FPlanetRecipeConfig Bad = Config;
	Bad.Sphere.UpperMantleRatio = 0.2f;
	FPlanetBakeReport R3;
	TestFalse(TEXT("圈层比例不递增 → 拒绝烘焙"), FPlanetBakePipeline::MakeDefault().Bake(Bad, R3).IsValid());
	TestTrue(TEXT("报告里有原因"), R3.HasErrors());

	FPlanetRecipeConfig TooWobbly = Config;
	TooWobbly.Sphere.MohoWobbleRatio = 0.1f;   // 上地幔厚 0.13，半厚 0.065
	FPlanetBakeReport R4;
	TestFalse(TEXT("起伏超过相邻层半厚 → 拒绝烘焙"), FPlanetBakePipeline::MakeDefault().Bake(TooWobbly, R4).IsValid());

	// 关掉莫霍面：层序照旧，只有起伏归零。
	FPlanetRecipeConfig NoMoho = Config;
	NoMoho.Sphere.bMoho = false;
	NoMoho.Sphere.MohoWobbleRatio = 0.f;
	FPlanetBakeReport R5;
	const TSharedPtr<const FPlanetSnapshot> SnapNoMoho = FPlanetBakePipeline::MakeDefault().Bake(NoMoho, R5);
	TestTrue(TEXT("关莫霍面仍能烘焙"), SnapNoMoho.IsValid());
	if (SnapNoMoho.IsValid())
	{
		TestEqual(TEXT("层数不变"), SnapNoMoho->Radial.Layers.Num(), 6);
		TestEqual(TEXT("地壳层起伏归零"), SnapNoMoho->Radial.Layers[0].WobbleRatio, 0.f);
		FGeoRng RngFlat = FGeoRng::Derive(11, 1, 0);
		float MaxAbs = 0.f;
		for (int32 i = 0; i < 64; ++i)
		{
			const float Z = RngFlat.Range(-1.f, 1.f), Phi = RngFlat.Range(0.f, 2.f * PI), S = FMath::Sqrt(1.f - Z * Z);
			MaxAbs = FMath::Max(MaxAbs, FMath::Abs(SnapNoMoho->MohoWobble.Sample(
				FVector3f(FVector3d(S * FMath::Cos(Phi), S * FMath::Sin(Phi), Z)))));
		}
		TestTrue(FString::Printf(TEXT("起伏场全 0 (MaxAbs=%f)"), MaxAbs), MaxAbs == 0.f);
	}
	return true;
}

#endif
