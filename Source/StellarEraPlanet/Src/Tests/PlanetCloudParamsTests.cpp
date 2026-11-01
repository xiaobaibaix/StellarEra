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
	constexpr float SurfaceRadiusCm = 2180000.0f;   // UniverseMap 的大行星
	constexpr float Tolerance = 1.0f;

	const PlanetClouds::FCloudLayerRadii Radii =
		PlanetClouds::ComputeCloudLayerRadiiCm(SurfaceRadiusCm, 0.02f, 0.07f);

	TestEqual(TEXT("云底 = 表面半径 × 0.02"), Radii.BaseCm, 43600.0f, Tolerance);
	TestEqual(TEXT("云顶 = 表面半径 × 0.07"), Radii.TopCm, 152600.0f, Tolerance);

	// 云必须留在壳内，否则会被射线求交直接裁掉，永远看不见。
	const float ShellRadiusCm = PlanetAtmosphere::ComputeShellRadiusCm(SurfaceRadiusCm, 0.09f);
	TestTrue(TEXT("云顶低于大气壳"), Radii.TopCm < ShellRadiusCm);

	// 比例写反（Top < Base）时钳成零厚度，而不是出一个负厚度。
	const PlanetClouds::FCloudLayerRadii Inverted =
		PlanetClouds::ComputeCloudLayerRadiiCm(1000.0f, 0.09f, 0.02f);
	TestTrue(TEXT("Top 被钳到不小于 Base"), Inverted.TopCm >= Inverted.BaseCm);

	// 负比例按 0 处理。
	const PlanetClouds::FCloudLayerRadii Negative =
		PlanetClouds::ComputeCloudLayerRadiiCm(1000.0f, -1.0f, -2.0f);
	TestEqual(TEXT("负比例钳到 0"), Negative.BaseCm, 0.0f, Tolerance);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraCloudParamsCoverageTest,
	"StellarEra.Planet.CloudParams.Coverage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraCloudParamsCoverageTest::RunTest(const FString& Parameters)
{
	// 阈值 = 1 - 覆盖率。噪声值域是 [0,1]，阈值 1 时任何噪声都超不过 → 晴空。
	TestEqual(TEXT("覆盖率 0 → 阈值 1（永远是晴空）"),
		PlanetClouds::CoverageToNoiseThreshold(0.0f), 1.0f, KINDA_SMALL_NUMBER);
	TestEqual(TEXT("覆盖率 1 → 阈值 0（处处是云）"),
		PlanetClouds::CoverageToNoiseThreshold(1.0f), 0.0f, KINDA_SMALL_NUMBER);
	TestEqual(TEXT("覆盖率 0.45 → 阈值 0.55"),
		PlanetClouds::CoverageToNoiseThreshold(0.45f), 0.55f, KINDA_SMALL_NUMBER);
	TestEqual(TEXT("超上界钳制"),
		PlanetClouds::CoverageToNoiseThreshold(2.0f), 0.0f, KINDA_SMALL_NUMBER);
	TestEqual(TEXT("超下界钳制"),
		PlanetClouds::CoverageToNoiseThreshold(-1.0f), 1.0f, KINDA_SMALL_NUMBER);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
