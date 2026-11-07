#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Atmosphere/PlanetAtmosphereParams.h"
#include "Atmosphere/PlanetCloudParams.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraCloudParamsRadiiTest,
	"StellarEra.Planet.CloudParams.Radii",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraCloudParamsRadiiTest::RunTest(const FString& Parameters)
{
	constexpr float SurfaceRadiusCm = 2000000.0f;   // UniverseMap 里那颗 20 km 行星
	constexpr float Tolerance = 1.0f;

	const PlanetClouds::FCloudLayerRadii Radii =
		PlanetClouds::ComputeCloudLayerRadiiCm(SurfaceRadiusCm, 0.02f, 0.07f);

	// 比例是"高度 / 表面半径"，半径要以行星中心为原点：r × (1 + 比例)。
	TestEqual(TEXT("云底 = 表面半径 × 1.02"), Radii.BaseCm, 2040000.0f, Tolerance);
	TestEqual(TEXT("云顶 = 表面半径 × 1.07"), Radii.TopCm, 2140000.0f, Tolerance);

	// 真正的回归守卫：漏掉 (1 + …) 时云层落到地面以下、HLSL 里密度恒 0 看不见云，
	// 而上面两条若跟着实现一起写错，照样是绿的。
	TestTrue(TEXT("云底在地表之上"), Radii.BaseCm > SurfaceRadiusCm);
	TestTrue(TEXT("云顶在地表之上"), Radii.TopCm > SurfaceRadiusCm);

	// 云必须留在壳内，否则会被射线求交直接裁掉，永远看不见。
	const float ShellRadiusCm = PlanetAtmosphere::ComputeShellRadiusCm(SurfaceRadiusCm, 0.09f);
	TestTrue(TEXT("云顶低于大气壳"), Radii.TopCm < ShellRadiusCm);

	// 比例写反（Top < Base）时钳成零厚度，而不是出一个负厚度。
	const PlanetClouds::FCloudLayerRadii Inverted =
		PlanetClouds::ComputeCloudLayerRadiiCm(1000.0f, 0.09f, 0.02f);
	TestTrue(TEXT("Top 被钳到不小于 Base"), Inverted.TopCm >= Inverted.BaseCm);

	// 负比例按 0 处理 —— 半径退到地表，不是退到行星中心。
	const PlanetClouds::FCloudLayerRadii Negative =
		PlanetClouds::ComputeCloudLayerRadiiCm(1000.0f, -1.0f, -2.0f);
	TestEqual(TEXT("负比例钳到地表"), Negative.BaseCm, 1000.0f, Tolerance);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraCloudParamsCoverageTest,
	"StellarEra.Planet.CloudParams.Coverage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraCloudParamsCoverageTest::RunTest(const FString& Parameters)
{
	// 阈值在噪声的**实测取值区间**上线性映射，不是教科书的 1 - Coverage（后者覆盖率
	// 0.45 落在 +1σ，画面是一层灰雾）。CloudFbm 实测均值 0.497、区间 [0.243, 0.631]。
	TestEqual(TEXT("覆盖率 0 → 阈值 0.63（晴空）"),
		PlanetClouds::CoverageToNoiseThreshold(0.0f),
		PlanetClouds::CloudNoiseThresholdAtZeroCoverage, KINDA_SMALL_NUMBER);
	TestEqual(TEXT("覆盖率 1 → 阈值 0.36（处处是云）"),
		PlanetClouds::CoverageToNoiseThreshold(1.0f),
		PlanetClouds::CloudNoiseThresholdAtFullCoverage, KINDA_SMALL_NUMBER);
	// 0.63 + (0.36 - 0.63) * 0.45 = 0.5085
	TestEqual(TEXT("覆盖率 0.45 → 阈值 0.5085"),
		PlanetClouds::CoverageToNoiseThreshold(0.45f), 0.5085f, 1e-4f);
	// 阈值必须落在噪声的实际区间内，否则覆盖率整段失效：
	// 高于 0.631 连最高的噪声都过不去（永远晴空），低于 0.243 则处处是云。
	TestTrue(TEXT("覆盖率 0 的阈值仍低于噪声上界"),
		PlanetClouds::CloudNoiseThresholdAtZeroCoverage < 0.631f);
	TestTrue(TEXT("覆盖率 1 的阈值高于噪声下界"),
		PlanetClouds::CloudNoiseThresholdAtFullCoverage > 0.243f);
	TestEqual(TEXT("超上界钳制"),
		PlanetClouds::CoverageToNoiseThreshold(2.0f),
		PlanetClouds::CloudNoiseThresholdAtFullCoverage, KINDA_SMALL_NUMBER);
	TestEqual(TEXT("超下界钳制"),
		PlanetClouds::CoverageToNoiseThreshold(-1.0f),
		PlanetClouds::CloudNoiseThresholdAtZeroCoverage, KINDA_SMALL_NUMBER);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
