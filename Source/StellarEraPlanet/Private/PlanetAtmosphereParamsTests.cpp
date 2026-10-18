#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PlanetAtmosphereParams.h"
#include "Components/SkyAtmosphereComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

// 断言写法说明：指针一律用 TestTrue/TestFalse 比较。TestEqual 的泛型重载要走
// LexToString，而指针没有可靠的 ToString —— 会编不过或打出没用的东西。
// 浮点一律带容差。

// 这一组锁死"我们的常量 == 引擎的默认值"。
// 引擎升级改了默认值，这里会红，而不是悄悄让所有缩放算错。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraAtmosphereEarthDefaultsTest,
	"StellarEra.Planet.AtmosphereParams.EarthDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraAtmosphereEarthDefaultsTest::RunTest(const FString& Parameters)
{
	USkyAtmosphereComponent* EngineDefaults = NewObject<USkyAtmosphereComponent>();

	TestEqual(TEXT("BottomRadius"), PlanetAtmosphere::EarthBottomRadiusKm,
		EngineDefaults->BottomRadius, 0.01f);
	TestEqual(TEXT("AtmosphereHeight"), PlanetAtmosphere::EarthAtmosphereHeightKm,
		EngineDefaults->AtmosphereHeight, 0.01f);
	TestEqual(TEXT("RayleighScatteringScale"), PlanetAtmosphere::EarthRayleighScatteringScale,
		EngineDefaults->RayleighScatteringScale, 1e-6f);
	TestEqual(TEXT("MieScatteringScale"), PlanetAtmosphere::EarthMieScatteringScale,
		EngineDefaults->MieScatteringScale, 1e-6f);
	TestEqual(TEXT("MieAbsorptionScale"), PlanetAtmosphere::EarthMieAbsorptionScale,
		EngineDefaults->MieAbsorptionScale, 1e-6f);
	TestEqual(TEXT("RayleighExponentialDistribution"), PlanetAtmosphere::EarthRayleighScaleHeightKm,
		EngineDefaults->RayleighExponentialDistribution, 1e-6f);
	TestEqual(TEXT("MieExponentialDistribution"), PlanetAtmosphere::EarthMieScaleHeightKm,
		EngineDefaults->MieExponentialDistribution, 1e-6f);

	// 引擎把 RayleightScatteringRaw 按 B 通道归一化后存进 RayleighScattering，
	// 把原始值存进 RayleighScatteringScale。我们记的是归一化后的颜色。
	TestEqual(TEXT("RayleighScattering.R"), PlanetAtmosphere::EarthRayleighScatteringColor.X,
		EngineDefaults->RayleighScattering.R, 1e-4f);
	TestEqual(TEXT("RayleighScattering.G"), PlanetAtmosphere::EarthRayleighScatteringColor.Y,
		EngineDefaults->RayleighScattering.G, 1e-4f);
	TestEqual(TEXT("RayleighScattering.B"), PlanetAtmosphere::EarthRayleighScatteringColor.Z,
		EngineDefaults->RayleighScattering.B, 1e-4f);

	return true;
}

// 壳半径：几何 + 观感厚度比。用 UniverseMap 的实际尺度（0.75 km 半径）。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraAtmosphereShellRadiusTest,
	"StellarEra.Planet.AtmosphereParams.ShellRadius",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraAtmosphereShellRadiusTest::RunTest(const FString& Parameters)
{
	// 75000 cm 半径（0.75 km），厚度比 0.05 → 78750 cm
	TestEqual(TEXT("0.75km planet, ratio 0.05"),
		PlanetAtmosphere::ComputeShellRadiusCm(75000.0f, 0.05f), 78750.0f, 0.5f);

	// 厚度比为 0 时壳与地面重合
	TestEqual(TEXT("ratio 0 collapses to surface"),
		PlanetAtmosphere::ComputeShellRadiusCm(75000.0f, 0.0f), 75000.0f, 0.5f);

	// 线性：厚度比减半，增量减半
	TestEqual(TEXT("ratio scales linearly"),
		PlanetAtmosphere::ComputeShellRadiusCm(75000.0f, 0.025f), 75000.0f * 1.025f, 0.5f);

	return true;
}

// 引擎大气：长度 ×s、系数 ×(1/s)。1/s 在这里正好是整数 8480。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraAtmosphereEngineScaleTest,
	"StellarEra.Planet.AtmosphereParams.EngineScale",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraAtmosphereEngineScaleTest::RunTest(const FString& Parameters)
{
	const PlanetAtmosphere::FSkyAtmosphereScaleParams P =
		PlanetAtmosphere::ComputeSkyAtmosphereScaleParams(0.75f, 0.05f);

	// s = 0.75 / 6360 = 1/8480
	TestEqual(TEXT("Scale"), P.Scale, 0.75f / 6360.0f, 1e-9f);

	// 长度按行星半径与厚度比给，不用几何缩放 —— 这是观感厚度，与壳一致
	TestEqual(TEXT("BottomRadiusKm"), P.BottomRadiusKm, 0.75f, 1e-5f);
	TestEqual(TEXT("AtmosphereHeightKm"), P.AtmosphereHeightKm, 0.75f * 0.05f, 1e-5f);

	// 系数 ×(1/s) = ×8480
	TestEqual(TEXT("RayleighScatteringScale"), P.RayleighScatteringScale, 0.0331f * 8480.0f, 1.0f);
	TestEqual(TEXT("MieScatteringScale"), P.MieScatteringScale, 0.003996f * 8480.0f, 0.1f);
	TestEqual(TEXT("MieAbsorptionScale"), P.MieAbsorptionScale, 0.000444f * 8480.0f, 0.01f);

	// 标高是长度，×s
	TestEqual(TEXT("RayleighExponentialDistributionKm"),
		P.RayleighExponentialDistributionKm, 8.0f / 8480.0f, 1e-9f);
	TestEqual(TEXT("MieExponentialDistributionKm"),
		P.MieExponentialDistributionKm, 1.2f / 8480.0f, 1e-9f);

	return true;
}

// 壳散射：垂直光学厚度对齐地球。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraAtmosphereShellScatteringTest,
	"StellarEra.Planet.AtmosphereParams.ShellScattering",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraAtmosphereShellScatteringTest::RunTest(const FString& Parameters)
{
	const float ShellRadiusCm = PlanetAtmosphere::ComputeShellRadiusCm(75000.0f, 0.05f); // 78750
	const PlanetAtmosphere::FShellScatteringParams S =
		PlanetAtmosphere::ComputeShellScatteringParams(ShellRadiusCm, 0.015f, 0.003f);

	// 标高是壳半径的比例
	TestEqual(TEXT("RayleighScaleHeightCm"), S.RayleighScaleHeightCm, 78750.0f * 0.015f, 0.5f);
	TestEqual(TEXT("MieScaleHeightCm"), S.MieScaleHeightCm, 78750.0f * 0.003f, 0.5f);

	// 核心不变式：垂直光学厚度 τ = β × H 等于地球的
	// 地球瑞利 τ（B 通道）= 0.033100 × 8.0 = 0.2648
	const float EarthTauRayleigh = PlanetAtmosphere::EarthRayleighScatteringScale
		* PlanetAtmosphere::EarthRayleighScaleHeightKm;
	TestEqual(TEXT("Rayleigh vertical optical depth matches Earth"),
		S.RayleighBeta.Z * S.RayleighScaleHeightCm, EarthTauRayleigh, 1e-4f);

	// 地球米氏 τ = (0.003996 + 0.000444) × 1.2 = 0.005328
	const float EarthTauMie = (PlanetAtmosphere::EarthMieScatteringScale
		+ PlanetAtmosphere::EarthMieAbsorptionScale) * PlanetAtmosphere::EarthMieScaleHeightKm;
	TestEqual(TEXT("Mie vertical optical depth matches Earth"),
		S.MieBeta.X * S.MieScaleHeightCm, EarthTauMie, 1e-5f);

	// RGB 比例取自地球：B 通道归一化为 1，R/G 按比例
	TestEqual(TEXT("Rayleigh RGB ratio R"), S.RayleighBeta.X / S.RayleighBeta.Z, 0.175287f, 1e-4f);
	TestEqual(TEXT("Rayleigh RGB ratio G"), S.RayleighBeta.Y / S.RayleighBeta.Z, 0.409607f, 1e-4f);

	// 米氏是白的
	TestTrue(TEXT("Mie is white"),
		FMath::IsNearlyEqual(S.MieBeta.X, S.MieBeta.Y, 1e-12f)
		&& FMath::IsNearlyEqual(S.MieBeta.Y, S.MieBeta.Z, 1e-12f));

	// 量级护栏：写错单位（cm/km 混用）会让这两个数差 5 个数量级
	TestTrue(TEXT("RayleighBeta magnitude is 1e-4 /cm"), S.RayleighBeta.Z > 1e-5f && S.RayleighBeta.Z < 1e-3f);
	TestTrue(TEXT("MieBeta magnitude is 1e-5 /cm"), S.MieBeta.X > 1e-6f && S.MieBeta.X < 1e-4f);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
