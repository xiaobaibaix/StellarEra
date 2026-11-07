#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PlanetSkyModel.h"
#include "PlanetAtmosphereParams.h"
#include "VoxelPlanetActor.h"
#include "PlanetSkyLightComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

// 断言写法说明：浮点一律显式带容差；颜色用 Equals(容差) 而不是 ==。
//
// 这里**不断言色相**（天更蓝/日落更红）。模型照抄壳着色器，没有太阳路径透射项，
// 色相是否如预期要跑起来看，断言它只会得到一条没意义的红。色相验证在 Task 4 的 PIE 里。

namespace
{
	constexpr float TestSurfaceRadiusCm = 100000.0f;   // 1 km
	constexpr float TestShellRadiusCm = 109000.0f;     // 厚度比 0.09
	constexpr float TestRayleighScaleHeightRatio = 0.008f;
	constexpr float TestMieScaleHeightRatio = 0.0015f;

	const FLinearColor TestStarColor(0.008f, 0.010f, 0.020f, 1.0f);

	/** 观察点放在球面正上方（+Z），当地天顶就是 +Z，方向断言好写。 */
	PlanetSky::FModelInput MakeInput()
	{
		PlanetSky::FModelInput Input;
		Input.PlanetCenter = FVector::ZeroVector;
		Input.ObserverLocation = FVector(0.0f, 0.0f, TestSurfaceRadiusCm);
		Input.SurfaceRadiusCm = TestSurfaceRadiusCm;
		Input.ShellRadiusCm = TestShellRadiusCm;
		Input.Scattering = PlanetAtmosphere::ComputeShellScatteringParams(
			TestShellRadiusCm, TestRayleighScaleHeightRatio, TestMieScaleHeightRatio);
		Input.CloudCoverage = 0.0f;
		Input.SpaceSkyRadiance = TestStarColor;
		return Input;
	}

	/** 方位角 0° = +X，仰角 0° = 水平面。 */
	PlanetSky::FSun MakeSun(float ElevationDeg, float AzimuthDeg, const FLinearColor& Color)
	{
		const float El = FMath::DegreesToRadians(ElevationDeg);
		const float Az = FMath::DegreesToRadians(AzimuthDeg);
		PlanetSky::FSun Sun;
		Sun.Direction = FVector(FMath::Cos(El) * FMath::Cos(Az),
			FMath::Cos(El) * FMath::Sin(Az), FMath::Sin(El));
		Sun.Radiance = Color;
		return Sun;
	}
}

// 面序：Resolution = 1 时每个面的中心像素正好是轴方向。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraSkyFaceCentersTest,
	"StellarEra.Planet.SkyLight.FaceCenters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraSkyFaceCentersTest::RunTest(const FString& Parameters)
{
	const FVector Expected[6] = {
		FVector::ForwardVector,
		FVector::BackwardVector,
		FVector::RightVector,
		FVector::LeftVector,
		FVector::UpVector,
		FVector::DownVector,
	};
	for (int32 Face = 0; Face < 6; ++Face)
	{
		const FVector Actual = PlanetSky::CubeFaceDirection(Face, 0, 0, 1);
		TestTrue(FString::Printf(TEXT("face %d center"), Face), Actual.Equals(Expected[Face], 1e-5f));
	}
	return true;
}

// 非轴像素的符号约定：这一条锁的是 GetCubemapVector 里那些不容易看出来的负号。
// Resolution 4、像素 (0,0)：U = V = ((0.5)/4)*2-1 = -0.75。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraSkyFaceCornerTest,
	"StellarEra.Planet.SkyLight.FaceCorner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraSkyFaceCornerTest::RunTest(const FString& Parameters)
{
	// face 0 = +X: (1, -V, -U) = (1, 0.75, 0.75)
	const FVector Expected = FVector(1.0f, 0.75f, 0.75f).GetSafeNormal();
	TestTrue(TEXT("face 0 pixel (0,0)"),
		PlanetSky::CubeFaceDirection(0, 0, 0, 4).Equals(Expected, 1e-4f));

	// face 1 = -X: (-1, -V, U) = (-1, 0.75, -0.75)
	const FVector Expected1 = FVector(-1.0f, 0.75f, -0.75f).GetSafeNormal();
	TestTrue(TEXT("face 1 pixel (0,0)"),
		PlanetSky::CubeFaceDirection(1, 0, 0, 4).Equals(Expected1, 1e-4f));

	// face 5 = -Z: (-U, -V, -1) = (0.75, 0.75, -1)
	const FVector Expected5 = FVector(0.75f, 0.75f, -1.0f).GetSafeNormal();
	TestTrue(TEXT("face 5 pixel (0,0)"),
		PlanetSky::CubeFaceDirection(5, 0, 0, 4).Equals(Expected5, 1e-4f));
	return true;
}

// 夜面：太阳在地平线下 45°，朝天的方向必须完全不含太阳项 —— 换任何颜色的太阳，输出逐位相同。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraSkyNightIgnoresSunTest,
	"StellarEra.Planet.SkyLight.NightIgnoresSun",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraSkyNightIgnoresSunTest::RunTest(const FString& Parameters)
{
	PlanetSky::FModelInput RedInput = MakeInput();
	RedInput.Suns.Add(MakeSun(-45.0f, 0.0f, FLinearColor(6.0f, 0.1f, 0.1f, 1.0f)));

	PlanetSky::FModelInput BlueInput = MakeInput();
	BlueInput.Suns.Add(MakeSun(-45.0f, 0.0f, FLinearColor(0.1f, 0.1f, 6.0f, 1.0f)));

	// 只看 +Z 面（仰角 > 40°）。贴近地平线的掠射射线会跑到 20° 以外，那段采样点可能已经
	// 出了行星阴影（壳着色器同样如此），拿它断言「夜面恒无太阳项」会红得莫名其妙。
	bool bAllFinite = true;
	bool bAllNonNegative = true;
	bool bAllBelowStar = true;
	for (int32 Y = 0; Y < 4; ++Y)
	{
		for (int32 X = 0; X < 4; ++X)
		{
			const FVector Dir = PlanetSky::CubeFaceDirection(4, X, Y, 4);
			const FLinearColor Red = PlanetSky::SampleDirection(RedInput, Dir);
			const FLinearColor Blue = PlanetSky::SampleDirection(BlueInput, Dir);

			bAllFinite &= FMath::IsFinite(Red.R) && FMath::IsFinite(Red.G) && FMath::IsFinite(Red.B);
			bAllNonNegative &= Red.R >= 0.0f && Red.G >= 0.0f && Red.B >= 0.0f;
			// 只剩星空乘透射率，不可能比星空本身更亮。
			bAllBelowStar &= Red.R <= TestStarColor.R + 1e-5f
				&& Red.G <= TestStarColor.G + 1e-5f
				&& Red.B <= TestStarColor.B + 1e-5f;

			// 逐位比较：FLinearColor::Equals 用严格「< 容差」，传 0 会恒为 false，
			// 不能拿它判「完全相同」。
			if (Red.R != Blue.R || Red.G != Blue.G || Red.B != Blue.B)
			{
				AddError(FString::Printf(TEXT("夜面输出依赖太阳颜色 @(%d,%d)"), X, Y));
				break;
			}
		}
	}
	TestTrue(TEXT("finite"), bAllFinite);
	TestTrue(TEXT("non-negative"), bAllNonNegative);
	TestTrue(TEXT("below star radiance"), bAllBelowStar);
	return true;
}

// 朝下的方向：射线在球面处就被地面截断，积分为零、也没有星空项。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraSkyGroundIsBlackTest,
	"StellarEra.Planet.SkyLight.GroundIsBlack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraSkyGroundIsBlackTest::RunTest(const FString& Parameters)
{
	PlanetSky::FModelInput Input = MakeInput();
	Input.Suns.Add(MakeSun(90.0f, 0.0f, FLinearColor::White));

	int32 Checked = 0;
	for (int32 Face = 0; Face < 6; ++Face)
	{
		for (int32 Y = 0; Y < 8; ++Y)
		{
			for (int32 X = 0; X < 8; ++X)
			{
				const FVector Dir = PlanetSky::CubeFaceDirection(Face, X, Y, 8);
				if (Dir.Z > -0.5f)
				{
					continue;
				}
				++Checked;
				const FLinearColor Value = PlanetSky::SampleDirection(Input, Dir);
				if (Value.R > 0.0f || Value.G > 0.0f || Value.B > 0.0f)
				{
					AddError(FString::Printf(TEXT("地面方向有颜色 face=%d x=%d y=%d"), Face, X, Y));
				}
			}
		}
	}
	TestTrue(TEXT("至少检查到若干朝下的像素"), Checked > 0);
	return true;
}

// 太阳越高，天顶越亮：θ=0 时米氏前向相位约为 60° 时的千倍，单调性有巨大余量。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraSkySunElevationMonotonicTest,
	"StellarEra.Planet.SkyLight.SunElevationMonotonic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraSkySunElevationMonotonicTest::RunTest(const FString& Parameters)
{
	PlanetSky::FModelInput Noon = MakeInput();
	Noon.Suns.Add(MakeSun(90.0f, 0.0f, FLinearColor::White));

	PlanetSky::FModelInput Mid = MakeInput();
	Mid.Suns.Add(MakeSun(30.0f, 0.0f, FLinearColor::White));

	const float NoonLuma = PlanetSky::SampleDirection(Noon, FVector::UpVector).GetLuminance();
	const float MidLuma = PlanetSky::SampleDirection(Mid, FVector::UpVector).GetLuminance();

	TestTrue(FString::Printf(TEXT("正午(%f) 亮于 30°(%f)"), NoonLuma, MidLuma), NoonLuma > MidLuma);
	TestTrue(FString::Printf(TEXT("正午至少两倍于 30°: %f vs %f"), NoonLuma, MidLuma),
		NoonLuma > MidLuma * 2.0f);
	return true;
}

// 观察点在壳外朝上看：整条射线不经过大气，输出必须**正好**是星空辐亮度。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraSkyOutsideShellIsStarTest,
	"StellarEra.Planet.SkyLight.OutsideShellIsStar",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraSkyOutsideShellIsStarTest::RunTest(const FString& Parameters)
{
	PlanetSky::FModelInput Input = MakeInput();
	Input.ObserverLocation = FVector(0.0f, 0.0f, TestShellRadiusCm * 3.0f);
	Input.Suns.Add(MakeSun(90.0f, 0.0f, FLinearColor::White));

	const FLinearColor Up = PlanetSky::SampleDirection(Input, FVector::UpVector);
	TestTrue(TEXT("壳外朝上 == 星空"), Up.Equals(TestStarColor, 1e-6f));

	// 朝下穿过整层大气再看地面：必须有散射，不能等于星空。
	const FLinearColor Down = PlanetSky::SampleDirection(Input, FVector::DownVector);
	TestFalse(TEXT("壳外朝下 != 星空"), Down.Equals(TestStarColor, 1e-6f));
	TestTrue(TEXT("壳外朝下非负"), Down.R >= 0.0f && Down.G >= 0.0f && Down.B >= 0.0f);
	return true;
}

// 云只有一阶衰减：覆盖率为 1 时朝天的方向归零。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraSkyCloudsDimSkyTest,
	"StellarEra.Planet.SkyLight.CloudsDimSky",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraSkyCloudsDimSkyTest::RunTest(const FString& Parameters)
{
	PlanetSky::FModelInput Clear = MakeInput();
	Clear.Suns.Add(MakeSun(60.0f, 0.0f, FLinearColor::White));

	PlanetSky::FModelInput Overcast = Clear;
	Overcast.CloudCoverage = 1.0f;

	const float ClearLuma = PlanetSky::SampleDirection(Clear, FVector::UpVector).GetLuminance();
	const float OvercastLuma = PlanetSky::SampleDirection(Overcast, FVector::UpVector).GetLuminance();

	TestTrue(TEXT("晴天有光"), ClearLuma > 0.0f);
	TestTrue(TEXT("全阴更暗"), OvercastLuma < ClearLuma);
	TestTrue(TEXT("全阴时朝天的方向归零"), OvercastLuma == 0.0f);
	return true;
}

// 整张立方图：像素数、无 NaN、非负、alpha 恒为 1（喂给 UTextureCube 之前不能有脏值）。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraSkyCubeShapeTest,
	"StellarEra.Planet.SkyLight.CubeShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraSkyCubeShapeTest::RunTest(const FString& Parameters)
{
	PlanetSky::FModelInput Input = MakeInput();
	Input.Suns.Add(MakeSun(72.0f, 30.0f, FLinearColor(1.0f, 0.9f, 0.8f, 1.0f)));

	TArray<FLinearColor> Pixels;
	PlanetSky::RenderCube(Input, 8, Pixels);

	TestEqual(TEXT("pixel count"), Pixels.Num(), 6 * 8 * 8);
	bool bAllFinite = true;
	bool bAllNonNegative = true;
	bool bAlphaOne = true;
	for (const FLinearColor& Pixel : Pixels)
	{
		bAllFinite &= FMath::IsFinite(Pixel.R) && FMath::IsFinite(Pixel.G)
			&& FMath::IsFinite(Pixel.B);
		bAllNonNegative &= Pixel.R >= 0.0f && Pixel.G >= 0.0f && Pixel.B >= 0.0f;
		bAlphaOne &= FMath::IsNearlyEqual(Pixel.A, 1.0f, 1e-6f);
	}
	TestTrue(TEXT("finite"), bAllFinite);
	TestTrue(TEXT("non-negative"), bAllNonNegative);
	TestTrue(TEXT("alpha == 1"), bAlphaOne);
	return true;
}

// 天光是骨架：行星 CDO 上必须自带，不能靠蓝图补。蓝图 SCS 组件不在 CDO 上，这条能区分两种做法。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraSkyPlanetHasComponentTest,
	"StellarEra.Planet.SkyLight.PlanetHasComponent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraSkyPlanetHasComponentTest::RunTest(const FString& Parameters)
{
	const AVoxelPlanetActor* Planet = GetDefault<AVoxelPlanetActor>();
	TestNotNull(TEXT("行星 CDO"), Planet);
	if (!Planet)
	{
		return false;
	}
	const UPlanetSkyLightComponent* Component =
		Planet->FindComponentByClass<UPlanetSkyLightComponent>();
	TestNotNull(TEXT("PlanetSkyLight"), Component);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
