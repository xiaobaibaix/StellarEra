// Copyright Voxel Plugin SAS. All Rights Reserved.
// 分布生成器框架：噪声范围与确定性。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Math/RandomStream.h"
#include "PlanetGeologyDistribution.h"
#include "PlanetGeologyPreset.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PlanetGeologyDistributionTest
{
	UPlanetGeologyPreset* MakePreset()
	{
		return NewObject<UPlanetGeologyPreset>();
	}

	FPlanetGeologyElementParams& AddElement(UPlanetGeologyPreset& Preset, FName Id)
	{
		FPlanetGeologyElementParams& Element = Preset.Elements.AddDefaulted_GetRef();
		Element.Id = Id;
		Element.DisplayName = Id.ToString();
		// 显式生成器路径：不让旧 PodCount 触发运行期兜底。
		Element.PodCount = 0;
		return Element;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetGeologyDistributionNoiseTest,
	"StellarEra.Planet.Geology.Distribution.Noise",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetGeologyDistributionNoiseTest::RunTest(const FString& Parameters)
{
	FPlanetGeologyDistributionNoise Noise;
	Noise.Init(1234, 0.05f, 3.f, 2, 2.f, 0.5f, 0.f);

	FRandomStream Rng(7);
	const FVector P(Rng.FRandRange(-1.f, 1.f), Rng.FRandRange(-1.f, 1.f),
		Rng.FRandRange(-1.f, 1.f));

	const float A = Noise.EvaluateVein(P);
	const float B = Noise.EvaluateVein(P);
	TestEqual(TEXT("同参数同点确定"), A, B);
	TestTrue(*FString::Printf(TEXT("范围 [0,1]（%.4f）"), A), A >= 0.f && A <= 1.f);

	// 片层拿原始噪声做形变，必须独立于阈值可读。入参是噪声空间坐标，
	// 所以这里自己乘一个任意缩放（7.5），与 EvaluateVein 内部的折频无关。
	const FVector NoiseSpacePoint = P * 7.5f;
	const float Raw = Noise.EvaluateAt(NoiseSpacePoint);
	TestTrue(*FString::Printf(TEXT("EvaluateAt 范围 [0,1]（%.4f）"), Raw),
		Raw >= 0.f && Raw <= 1.f);
	TestEqual(TEXT("EvaluateAt 确定"), Noise.EvaluateAt(NoiseSpacePoint), Raw);

	FPlanetGeologyDistributionNoise Thresholded;
	Thresholded.Init(1234, 0.05f, 3.f, 2, 2.f, 0.5f, 0.9f);
	TestEqual(TEXT("阈值不改 EvaluateAt"), Thresholded.EvaluateAt(NoiseSpacePoint), Raw);
	TestTrue(TEXT("阈值只压低 EvaluateVein"), Thresholded.EvaluateVein(P) <= A);

	FPlanetGeologyDistributionNoise Other;
	Other.Init(4321, 0.05f, 3.f, 2, 2.f, 0.5f, 0.f);
	int32 Differences = 0;
	for (int32 Index = 0; Index < 64; ++Index)
	{
		const FVector Sample(Rng.FRandRange(-1.f, 1.f), Rng.FRandRange(-1.f, 1.f),
			Rng.FRandRange(-1.f, 1.f));
		if (!FMath::IsNearlyEqual(Noise.EvaluateVein(Sample), Other.EvaluateVein(Sample), 1e-6f))
		{
			++Differences;
		}
	}
	TestTrue(*FString::Printf(TEXT("换种子至少一个采样点不同（%d/64）"), Differences),
		Differences > 0);

	FPlanetGeologyDistributionNoise Invalid;
	Invalid.Init(1, 0.f, 3.f, 2, 2.f, 0.5f, 0.f);
	TestEqual(TEXT("特征半径为 0 时恒 0"), Invalid.EvaluateVein(P), 0.f);

	return true;
}

#endif
