// Copyright Voxel Plugin SAS. All Rights Reserved.
// 分布生成器框架：噪声范围与确定性。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Math/RandomStream.h"
#include "PlanetGeologyDistribution.h"
#include "PlanetGeologyField.h"
#include "PlanetGeologyLensDistribution.h"
#include "PlanetGeologyPreset.h"
#include "PlanetGeologySpherePodsDistribution.h"

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

namespace PlanetGeologyDistributionTest
{
	UPlanetGeologySpherePodsDistribution* AddSpherePods(UPlanetGeologyPreset& Preset,
		FPlanetGeologyElementParams& Element, int32 PodCount, float PodRadius, int32 Seed)
	{
		UPlanetGeologySpherePodsDistribution* Distribution =
			NewObject<UPlanetGeologySpherePodsDistribution>(&Preset);
		Distribution->PodCount = PodCount;
		Distribution->PodRadius = PodRadius;
		Distribution->Seed = Seed;
		// 刻意不设 bSeedExplicit：Seed=0 必须走派生，否则 SeedDerivation 的前两段
		// 会两条都锁死成种子 0，囊表相同，测试变成永真。
		Element.Distributions.Add(Distribution);
		return Distribution;
	}

	TSharedPtr<const FPlanetGeologyField> MakeField(const UPlanetGeologyPreset& Preset)
	{
		return FPlanetGeologyField::BuildFromPreset(Preset, 40000.0);
	}

	UPlanetGeologyPreset* MakeSingleElementPreset(const TCHAR* Id, bool bAddA, bool bAddB)
	{
		UPlanetGeologyPreset* Preset = MakePreset();
		FPlanetGeologyElementParams& Element = AddElement(*Preset, Id);
		if (bAddA)
		{
			AddSpherePods(*Preset, Element, 8, 0.06f, 111);
		}
		if (bAddB)
		{
			AddSpherePods(*Preset, Element, 5, 0.09f, 222);
		}
		return Preset;
	}

	bool AreRegionsEqual(const TArray<FPlanetGeologyRegion>& A, const TArray<FPlanetGeologyRegion>& B)
	{
		if (A.Num() != B.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < A.Num(); ++Index)
		{
			if (!A[Index].Bounds.Equals(B[Index].Bounds)
				|| A[Index].CharacteristicRadius != B[Index].CharacteristicRadius)
			{
				return false;
			}
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetGeologyDistributionMaxCompositionTest,
	"StellarEra.Planet.Geology.Distribution.MaxComposition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetGeologyDistributionMaxCompositionTest::RunTest(const FString& Parameters)
{
	using namespace PlanetGeologyDistributionTest;

	UPlanetGeologyPreset* OnlyA = MakeSingleElementPreset(TEXT("iron"), true, false);
	UPlanetGeologyPreset* OnlyB = MakeSingleElementPreset(TEXT("iron"), false, true);
	UPlanetGeologyPreset* Both = MakeSingleElementPreset(TEXT("iron"), true, true);

	const auto FieldA = MakeField(*OnlyA);
	const auto FieldB = MakeField(*OnlyB);
	const auto FieldBoth = MakeField(*Both);
	if (!TestTrue(TEXT("三个场都建成功"),
		FieldA.IsValid() && FieldB.IsValid() && FieldBoth.IsValid()))
	{
		return false;
	}

	FRandomStream Rng(4321);
	int32 Mismatches = 0;
	int32 NonZeroSamples = 0;
	for (int32 SampleIndex = 0; SampleIndex < 3000; ++SampleIndex)
	{
		const FVector P(Rng.FRandRange(-1.f, 1.f), Rng.FRandRange(-1.f, 1.f),
			Rng.FRandRange(-1.f, 1.f));
		if (P.SizeSquared() > 1.f)
		{
			continue;
		}
		const float A = FieldA->SampleElementNormAtNormalized(0, P);
		const float B = FieldB->SampleElementNormAtNormalized(0, P);
		const float BothNorm = FieldBoth->SampleElementNormAtNormalized(0, P);

		if (BothNorm > 0.f)
		{
			++NonZeroSamples;
		}
		if (!FMath::IsNearlyEqual(BothNorm, FMath::Max(A, B), 1e-6f))
		{
			++Mismatches;
		}
		// 两个生成器都为 0 的地方合成后必须还是 0（防止实现写成「加一点底噪」）。
		if (A == 0.f && B == 0.f && BothNorm != 0.f)
		{
			++Mismatches;
		}
	}

	TestEqual(TEXT("合成结果逐点等于两生成器的 max"), Mismatches, 0);
	TestTrue(*FString::Printf(TEXT("夹具确实采到过非零点（%d）"), NonZeroSamples),
		NonZeroSamples > 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetGeologyDistributionSeedTest,
	"StellarEra.Planet.Geology.Distribution.SeedDerivation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetGeologyDistributionSeedTest::RunTest(const FString& Parameters)
{
	using namespace PlanetGeologyDistributionTest;

	// 1) Seed=0：不同元素派生出不同的囊表。
	{
		UPlanetGeologyPreset* Preset = MakePreset();
		FPlanetGeologyElementParams& Alpha = AddElement(*Preset, TEXT("alpha"));
		FPlanetGeologyElementParams& Beta = AddElement(*Preset, TEXT("beta"));
		AddSpherePods(*Preset, Alpha, 6, 0.05f, 0);
		AddSpherePods(*Preset, Beta, 6, 0.05f, 0);

		const auto Field = MakeField(*Preset);
		TArray<FPlanetGeologyRegion> RegionsAlpha;
		TArray<FPlanetGeologyRegion> RegionsBeta;
		Field->EnumerateRegions(0, RegionsAlpha);
		Field->EnumerateRegions(1, RegionsBeta);
		TestEqual(TEXT("两个元素各 6 个区域"), RegionsAlpha.Num(), 6);
		TestTrue(TEXT("不同元素 Seed=0 派生出的囊表不同"),
			!AreRegionsEqual(RegionsAlpha, RegionsBeta));
	}

	// 2) Seed=0：同一元素的两个生成器互不相同（下标参与派生）。
	{
		UPlanetGeologyPreset* Preset = MakePreset();
		FPlanetGeologyElementParams& Element = AddElement(*Preset, TEXT("alpha"));
		AddSpherePods(*Preset, Element, 6, 0.05f, 0);
		AddSpherePods(*Preset, Element, 6, 0.05f, 0);

		const auto Field = MakeField(*Preset);
		TArray<FPlanetGeologyRegion> Regions;
		Field->EnumerateRegions(0, Regions);
		if (!TestEqual(TEXT("两个生成器共 12 个区域"), Regions.Num(), 12))
		{
			return false;
		}
		const TArray<FPlanetGeologyRegion> First(Regions.GetData(), 6);
		const TArray<FPlanetGeologyRegion> Second(Regions.GetData() + 6, 6);
		TestTrue(TEXT("同元素两个生成器 Seed=0 派生出的囊表不同"),
			!AreRegionsEqual(First, Second));
	}

	// 3) 非 0 Seed 被尊重：两个生成器写同一个字面量种子必须逐位相同。
	// 夹具不设 bSeedExplicit —— Seed≠0 本身就走字面值分支，设了只会掩盖这条语义。
	{
		UPlanetGeologyPreset* Preset = MakePreset();
		FPlanetGeologyElementParams& Element = AddElement(*Preset, TEXT("alpha"));
		AddSpherePods(*Preset, Element, 6, 0.05f, 999);
		AddSpherePods(*Preset, Element, 6, 0.05f, 999);

		const auto Field = MakeField(*Preset);
		TArray<FPlanetGeologyRegion> Regions;
		Field->EnumerateRegions(0, Regions);
		if (!TestEqual(TEXT("两个生成器共 12 个区域"), Regions.Num(), 12))
		{
			return false;
		}
		const TArray<FPlanetGeologyRegion> First(Regions.GetData(), 6);
		const TArray<FPlanetGeologyRegion> Second(Regions.GetData() + 6, 6);
		TestTrue(TEXT("同种子两次生成逐位相同"), AreRegionsEqual(First, Second));
	}

	// 4) 跨进程稳定：把派生结果钉成字面量。
	// GetTypeHash(FName) 取的是名字池下标，换个进程就可能变；字符串哈希（FCrc::Strihash）才是确定的。
	// 这条字面量让「悄悄改回 FName 哈希」立刻变成失败。首次实现时先写成 0，
	// 从失败信息读出实际值再填回来 —— 不要反过来先算出值再写测试。
	{
		UPlanetGeologyPreset* Preset = MakePreset();
		UPlanetGeologySpherePodsDistribution* Distribution =
			NewObject<UPlanetGeologySpherePodsDistribution>(Preset);
		Distribution->Seed = 0;
		TestEqual(TEXT("ResolveSeed 对 iron/生成器 3 的派生值是钉住的字面量"),
			Distribution->ResolveSeed(TEXT("iron"), 3), 1033752709);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetGeologyDistributionEmptyTest,
	"StellarEra.Planet.Geology.Distribution.EmptySemantics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetGeologyDistributionEmptyTest::RunTest(const FString& Parameters)
{
	using namespace PlanetGeologyDistributionTest;

	TestTrue(TEXT("没有元素 → 空场"), MakeField(*MakePreset())->IsEmpty());

	{
		UPlanetGeologyPreset* Preset = MakePreset();
		AddElement(*Preset, TEXT("iron")); // AddElement 已把遗留 PodCount 置 0
		TestTrue(TEXT("既无生成器也无遗留矿囊 → 空场"), MakeField(*Preset)->IsEmpty());
	}

	{
		UPlanetGeologyPreset* Preset = MakePreset();
		FPlanetGeologyElementParams& Element = AddElement(*Preset, TEXT("iron"));
		AddSpherePods(*Preset, Element, 0, 0.05f, 1);
		TestTrue(TEXT("唯一生成器基元数为 0 → 空场"), MakeField(*Preset)->IsEmpty());
	}

	{
		UPlanetGeologyPreset* Preset = MakePreset();
		FPlanetGeologyElementParams& Element = AddElement(*Preset, TEXT("iron"));
		AddSpherePods(*Preset, Element, 4, 0.05f, 1);
		TestFalse(TEXT("有基元 → 非空场"), MakeField(*Preset)->IsEmpty());
	}

	return true;
}

namespace PlanetGeologyDistributionTest
{
	UPlanetGeologyLensDistribution* AddLens(UPlanetGeologyPreset& Preset,
		FPlanetGeologyElementParams& Element, float WarpAmplitude = 0.f)
	{
		UPlanetGeologyLensDistribution* Lens =
			NewObject<UPlanetGeologyLensDistribution>(&Preset);
		Lens->SeamCount = 24;
		Lens->SeamLength = 0.14f;
		Lens->SeamWidth = 0.07f;
		Lens->SeamThickness = 0.008f;
		Lens->SizeJitter = 0.35f;
		Lens->DepthBias = 0.9f;
		Lens->DepthCenter = 0.12f;
		Lens->DepthSpread = 0.04f;
		Lens->TiltJitterDeg = 5.f;
		Lens->WarpAmplitude = WarpAmplitude;
		Lens->Seed = 707;
		Lens->bSeedExplicit = true;
		Element.Distributions.Add(Lens);
		return Lens;
	}

	/** 加速路径与暴力路径的逐点一致性。
	 *
	 * 采样范围必须**超出**上报的包围盒：只在盒内采样时，包围盒算小了也发现不了 ——
	 * 盒内每一格索引都登记过，当然一致。所以区域点按 2× extent 采，再沿特征半径补壳层点。
	 * 失配要打出坐标：不说清是哪个点、差多少，两个 0.x 的浮点差根本 loc 不到。
	 */
	int32 CountBruteForceMismatches(FAutomationTestBase& Test, const FPlanetGeologyField& Field,
		int32 ElementIndex, const TArray<FPlanetGeologyRegion>& Regions, FRandomStream& Rng)
	{
		int32 Mismatches = 0;

		auto CheckPoint = [&Mismatches, &Test, &Field, ElementIndex](const FVector& P)
		{
			if (P.SizeSquared() > 1.f)
			{
				return;
			}
			const float Indexed = Field.SampleElementNormAtNormalized(ElementIndex, P);
			const float Brute = Field.SampleElementNormBruteForce(ElementIndex, P);
			if (!FMath::IsNearlyEqual(Indexed, Brute, 1e-6f))
			{
				Test.AddError(FString::Printf(
					TEXT("索引与暴力不一致：索引=%.6f 暴力=%.6f，点 %s"), Indexed, Brute,
					*P.ToString()));
				++Mismatches;
			}
		};

		for (int32 SampleIndex = 0; SampleIndex < 2000; ++SampleIndex)
		{
			CheckPoint(FVector(Rng.FRandRange(-1.f, 1.f), Rng.FRandRange(-1.f, 1.f),
				Rng.FRandRange(-1.f, 1.f)));
		}
		for (const FPlanetGeologyRegion& Region : Regions)
		{
			const FVector Center = Region.Bounds.GetCenter();
			const FVector Extent = Region.Bounds.GetExtent();
			for (int32 SampleIndex = 0; SampleIndex < 40; ++SampleIndex)
			{
				const FVector Offset(Rng.FRandRange(-1.f, 1.f), Rng.FRandRange(-1.f, 1.f),
					Rng.FRandRange(-1.f, 1.f));
				CheckPoint(Center + Offset * Extent * 2.0);
			}
			for (int32 SampleIndex = 0; SampleIndex < 16; ++SampleIndex)
			{
				CheckPoint(Center + Rng.GetUnitVector() * double(Region.CharacteristicRadius));
			}
		}
		return Mismatches;
	}

	/** 包围盒的保守性：暴力路径说有矿的点，必须落在某个区域的包围盒里。
	 *
	 * 这条**不依赖包围盒的算法**，所以公式写反（盒子比真实矿体小）时它立刻红。
	 * 上面那条「索引 == 暴力」在盒子偏小时可能结构性失明：索引只登记盒内格子，
	 * 盒外没有候选可查；只有主动去盒外采样的人才会发现那里本该有矿。
	 */
	int32 CountOreOutsideAllRegions(const FPlanetGeologyField& Field, int32 ElementIndex,
		const TArray<FPlanetGeologyRegion>& Regions, FRandomStream& Rng)
	{
		int32 Violations = 0;
		for (int32 SampleIndex = 0; SampleIndex < 4000; ++SampleIndex)
		{
			const FVector P(Rng.FRandRange(-1.f, 1.f), Rng.FRandRange(-1.f, 1.f),
				Rng.FRandRange(-1.f, 1.f));
			if (P.SizeSquared() > 1.f)
			{
				continue;
			}
			if (Field.SampleElementNormBruteForce(ElementIndex, P) <= 0.f)
			{
				continue;
			}
			bool bCovered = false;
			for (const FPlanetGeologyRegion& Region : Regions)
			{
				if (Region.Bounds.IsInsideOrOn(P))
				{
					bCovered = true;
					break;
				}
			}
			if (!bCovered)
			{
				++Violations;
			}
		}
		// 区域包围盒内部（真实矿体所在）按 2× extent 补采：命中率比整个球体随机高得多，
		// 而且正是盒子偏小时矿体探出盒外的位置。
		for (const FPlanetGeologyRegion& Region : Regions)
		{
			const FVector Center = Region.Bounds.GetCenter();
			const FVector Extent = Region.Bounds.GetExtent();
			for (int32 SampleIndex = 0; SampleIndex < 40; ++SampleIndex)
			{
				const FVector Offset(Rng.FRandRange(-1.f, 1.f), Rng.FRandRange(-1.f, 1.f),
					Rng.FRandRange(-1.f, 1.f));
				const FVector P = Center + Offset * Extent * 2.0;
				if (Field.SampleElementNormBruteForce(ElementIndex, P) <= 0.f)
				{
					continue;
				}
				bool bCovered = false;
				for (const FPlanetGeologyRegion& Other : Regions)
				{
					if (Other.Bounds.IsInsideOrOn(P))
					{
						bCovered = true;
						break;
					}
				}
				if (!bCovered)
				{
					++Violations;
				}
			}
		}
		return Violations;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetGeologyLensBoundsTest,
	"StellarEra.Planet.Geology.Lens.Bounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetGeologyLensBoundsTest::RunTest(const FString& Parameters)
{
	using namespace PlanetGeologyDistributionTest;

	UPlanetGeologyPreset* Preset = MakePreset();
	FPlanetGeologyElementParams& Element = AddElement(*Preset, TEXT("silicon"));
	AddLens(*Preset, Element);

	const auto Field = MakeField(*Preset);
	TArray<FPlanetGeologyRegion> Regions;
	Field->EnumerateRegions(0, Regions);
	if (!TestEqual(TEXT("24 片各一个区域"), Regions.Num(), 24))
	{
		return false;
	}

	int32 InBand = 0;
	for (const FPlanetGeologyRegion& Region : Regions)
	{
		const FVector Center = Region.Bounds.GetCenter();
		TestTrue(TEXT("区域包围盒有效"), Region.Bounds.IsValid != 0);
		// 厚度带 ±35% 抖动，所以判据是「落在抖动区间内」，不是等于标称值。
		TestTrue(*FString::Printf(TEXT("特征半径 = 厚度且落在抖动区间（%.4f）"),
			Region.CharacteristicRadius),
			Region.CharacteristicRadius >= 0.008f * 0.64f
				&& Region.CharacteristicRadius <= 0.008f * 1.36f);
		TestTrue(*FString::Printf(TEXT("片心不越出球体（%.4f）"), Center.Size()),
			Center.Size() <= 1.0 + 1e-4);

		// 作者参数里的深度 0 = 地表、1 = 球心；这里按同一口径回算。
		const double Depth = 1.0 - double(Center.Size());
		if (FMath::Abs(Depth - 0.12) <= 4.0 * 0.04)
		{
			++InBand;
		}
	}
	TestTrue(*FString::Printf(TEXT("多数片心落在偏好深度带（%d/24）"), InBand), InBand >= 18);

	// 两次建场逐位一致（放置全走固定随机流）。
	TArray<FPlanetGeologyRegion> Rebuilt;
	MakeField(*Preset)->EnumerateRegions(0, Rebuilt);
	TestTrue(TEXT("两次建场区域逐位相同"), AreRegionsEqual(Regions, Rebuilt));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetGeologyLensBruteForceTest,
	"StellarEra.Planet.Geology.Lens.BruteForceAgrees",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetGeologyLensBruteForceTest::RunTest(const FString& Parameters)
{
	using namespace PlanetGeologyDistributionTest;

	UPlanetGeologyPreset* Preset = MakePreset();
	FPlanetGeologyElementParams& Element = AddElement(*Preset, TEXT("silicon"));
	AddLens(*Preset, Element);

	const auto Field = MakeField(*Preset);
	TArray<FPlanetGeologyRegion> Regions;
	Field->EnumerateRegions(0, Regions);

	FRandomStream Rng(31415);
	TestEqual(TEXT("索引路径 == 暴力路径"), CountBruteForceMismatches(*this, *Field, 0, Regions, Rng), 0);
	TestEqual(TEXT("包围盒盖住全部真实矿体"), CountOreOutsideAllRegions(*Field, 0, Regions, Rng), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetGeologyLensWarpBruteForceTest,
	"StellarEra.Planet.Geology.Lens.WarpBruteForceAgrees",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetGeologyLensWarpBruteForceTest::RunTest(const FString& Parameters)
{
	using namespace PlanetGeologyDistributionTest;

	// 域扭曲会把点沿局部 Z 挪 WarpAmplitude × 厚度，包围盒必须按放大后的厚度外扩。
	// 不开扭曲时这条路径完全走不到，所以单独来一例。
	UPlanetGeologyPreset* Preset = MakePreset();
	FPlanetGeologyElementParams& Element = AddElement(*Preset, TEXT("silicon"));
	AddLens(*Preset, Element, 1.0f);

	const auto Field = MakeField(*Preset);
	TArray<FPlanetGeologyRegion> Regions;
	Field->EnumerateRegions(0, Regions);

	FRandomStream Rng(27182);
	TestEqual(TEXT("扭曲下索引路径 == 暴力路径"),
		CountBruteForceMismatches(*this, *Field, 0, Regions, Rng), 0);
	TestEqual(TEXT("扭曲下包围盒盖住全部真实矿体"),
		CountOreOutsideAllRegions(*Field, 0, Regions, Rng), 0);
	return true;
}

#endif
