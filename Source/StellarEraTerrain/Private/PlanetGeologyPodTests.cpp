// Copyright Voxel Plugin SAS. All Rights Reserved.
// 矿囊模型的自动化测试：覆盖、重叠、索引一致性、丰度不变性、形状。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Math/RandomStream.h"
#include "PlanetGeologyField.h"
#include "PlanetGeologyPreset.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PlanetGeologyPodTest
{
	constexpr double PlanetRadiusVoxels = 40000.0;

	FPlanetGeologyElementParams MakeElement(FName Id, int32 PodCount, float PodRadius,
		int32 Seed, float VeinThreshold = 0.0f)
	{
		FPlanetGeologyElementParams Params;
		Params.Id = Id;
		Params.DisplayName = Id.ToString();
		Params.PodCount = PodCount;
		Params.PodRadius = PodRadius;
		Params.Seed = Seed;
		Params.VeinThreshold = VeinThreshold;
		return Params;
	}

	UPlanetGeologyPreset* MakePreset(const TArray<FPlanetGeologyElementParams>& InElements)
	{
		UPlanetGeologyPreset* Preset = NewObject<UPlanetGeologyPreset>();
		Preset->Elements = InElements;
		return Preset;
	}

	TSharedPtr<const FPlanetGeologyField> MakeField(const UPlanetGeologyPreset& Preset)
	{
		return FPlanetGeologyField::BuildFromPreset(Preset, PlanetRadiusVoxels);
	}

	/** 球内均匀随机点（归一化）。 */
	FVector RandomPoint(FRandomStream& Rng)
	{
		for (int32 Attempt = 0; Attempt < 64; ++Attempt)
		{
			const FVector P(Rng.FRandRange(-1.f, 1.f), Rng.FRandRange(-1.f, 1.f),
				Rng.FRandRange(-1.f, 1.f));
			if (P.SizeSquared() <= 1.f)
			{
				return P;
			}
		}
		return FVector::ZeroVector;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetGeologyPodCoverageTest,
	"StellarEra.Planet.Geology.PodCoverage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetGeologyPodCoverageTest::RunTest(const FString& Parameters)
{
	using namespace PlanetGeologyPodTest;

	// 60 囊 / r=0.08：相交与露头都够多，固定种子下不会偶发为 0。
	UPlanetGeologyPreset* Preset = MakePreset({
		MakeElement(TEXT("iron"), 60, 0.08f, 101),
		MakeElement(TEXT("copper"), 60, 0.08f, 202)});
	const auto Field = MakeField(*Preset);
	if (!TestTrue(TEXT("场非空"), Field.IsValid() && !Field->IsEmpty()))
	{
		return false;
	}

	// 1) 矿囊中心半径必须铺满整个球体 —— 这是「不分层」的直接证据。
	for (int32 ElementIndex = 0; ElementIndex < 2; ++ElementIndex)
	{
		const TArray<FPlanetGeologyPod>& Pods = Field->GetPods(ElementIndex);
		TestEqual(*FString::Printf(TEXT("元素 %d 矿囊数"), ElementIndex), Pods.Num(), 60);

		float MinRadius = 2.f;
		float MaxRadius = -1.f;
		bool bOutcrop = false;
		for (const FPlanetGeologyPod& Pod : Pods)
		{
			MinRadius = FMath::Min(MinRadius, Pod.Center.Size());
			MaxRadius = FMath::Max(MaxRadius, Pod.Center.Size());
			// 探出基准球面 = 地形上能被看到。没这一条，观测视图再漂亮地形也是纯色。
			bOutcrop |= Pod.Center.Size() + Pod.Radius > 1.f;
		}
		TestTrue(*FString::Printf(TEXT("元素 %d 有矿囊贴近地表（最近 %.3f）"),
			ElementIndex, MinRadius), MinRadius < 0.5f);
		TestTrue(*FString::Printf(TEXT("元素 %d 有矿囊接近球心（最远 %.3f）"),
			ElementIndex, MaxRadius), MaxRadius > 0.5f);
		TestTrue(*FString::Printf(TEXT("元素 %d 至少一个矿囊探出球面（露头）"), ElementIndex),
			bOutcrop);
	}

	// 2) 采样点的深度覆盖：正样本必须散落在过半深度区间里。
	const int32 DepthBinCount = 20;
	TArray<bool> BinOccupied;
	BinOccupied.Init(false, DepthBinCount * 2);

	FRandomStream Rng(12345);
	const int32 SampleCount = 100000;
	for (int32 SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
	{
		const FVector P = RandomPoint(Rng);
		const double RadiusRatio = double(P.Size());
		const int32 Bin = FMath::Clamp(
			int32((1.0 - RadiusRatio) * DepthBinCount), 0, DepthBinCount - 1);
		for (int32 ElementIndex = 0; ElementIndex < 2; ++ElementIndex)
		{
			if (Field->SampleElementNormAtNormalized(ElementIndex, P) > 0.05f)
			{
				BinOccupied[ElementIndex * DepthBinCount + Bin] = true;
			}
		}
	}

	for (int32 ElementIndex = 0; ElementIndex < 2; ++ElementIndex)
	{
		int32 OccupiedBins = 0;
		for (int32 Bin = 0; Bin < DepthBinCount; ++Bin)
		{
			OccupiedBins += BinOccupied[ElementIndex * DepthBinCount + Bin] ? 1 : 0;
		}
		TestTrue(*FString::Printf(TEXT("元素 %d 覆盖 %d/%d 个深度区间（需 >= 12）"),
			ElementIndex, OccupiedBins, DepthBinCount), OccupiedBins >= 12);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetGeologyPodOverlapTest,
	"StellarEra.Planet.Geology.PodOverlap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetGeologyPodOverlapTest::RunTest(const FString& Parameters)
{
	using namespace PlanetGeologyPodTest;

	// 故意用更大的矿囊：相交概率 ∝ (r_a + r_b)³，默认参数下只有个位数的对。
	UPlanetGeologyPreset* Preset = MakePreset({
		MakeElement(TEXT("a"), 60, 0.09f, 11),
		MakeElement(TEXT("b"), 60, 0.09f, 22)});
	const auto Field = MakeField(*Preset);

	int32 OverlappingPairs = 0;
	for (const FPlanetGeologyPod& PodA : Field->GetPods(0))
	{
		for (const FPlanetGeologyPod& PodB : Field->GetPods(1))
		{
			if (FVector::Dist(PodA.Center, PodB.Center) < PodA.Radius + PodB.Radius)
			{
				++OverlappingPairs;
			}
		}
	}

	TestTrue(*FString::Printf(TEXT("至少一对不同元素的矿囊相交（实测 %d 对）"), OverlappingPairs),
		OverlappingPairs > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetGeologySpatialIndexTest,
	"StellarEra.Planet.Geology.SpatialIndex",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetGeologySpatialIndexTest::RunTest(const FString& Parameters)
{
	using namespace PlanetGeologyPodTest;

	UPlanetGeologyPreset* Preset = MakePreset({
		MakeElement(TEXT("a"), 30, 0.06f, 7, 0.3f),
		MakeElement(TEXT("b"), 30, 0.04f, 8, 0.3f),
		MakeElement(TEXT("c"), 10, 0.02f, 9, 0.3f)});
	const auto Field = MakeField(*Preset);

	FRandomStream Rng(98765);
	int32 Mismatches = 0;
	for (int32 SampleIndex = 0; SampleIndex < 2000; ++SampleIndex)
	{
		const FVector P = RandomPoint(Rng);
		for (int32 ElementIndex = 0; ElementIndex < 3; ++ElementIndex)
		{
			const float Indexed = Field->SampleElementNormAtNormalized(ElementIndex, P);
			const float Brute = Field->SampleElementNormBruteForce(ElementIndex, P);
			if (!FMath::IsNearlyEqual(Indexed, Brute, 1e-6f))
			{
				++Mismatches;
			}
		}
	}

	TestEqual(TEXT("索引查询与暴力遍历逐点一致"), Mismatches, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetGeologyAbundanceInvarianceTest,
	"StellarEra.Planet.Geology.AbundanceGeometryInvariance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetGeologyAbundanceInvarianceTest::RunTest(const FString& Parameters)
{
	using namespace PlanetGeologyPodTest;

	FPlanetGeologyElementParams Low = MakeElement(TEXT("a"), 20, 0.05f, 31, 0.2f);
	Low.Abundance = 0.2f;
	FPlanetGeologyElementParams High = Low;
	High.Abundance = 1.0f;

	UPlanetGeologyPreset* PresetLow = MakePreset({Low});
	UPlanetGeologyPreset* PresetHigh = MakePreset({High});
	const auto FieldLow = MakeField(*PresetLow);
	const auto FieldHigh = MakeField(*PresetHigh);

	FRandomStream Rng(4242);
	int32 Mismatches = 0;
	for (int32 SampleIndex = 0; SampleIndex < 1000; ++SampleIndex)
	{
		const FVector P = RandomPoint(Rng);
		if (!FMath::IsNearlyEqual(FieldLow->SampleElementNormAtNormalized(0, P),
			FieldHigh->SampleElementNormAtNormalized(0, P), 1e-6f))
		{
			++Mismatches;
		}
	}

	TestEqual(TEXT("只改 Abundance 不改逐点几何"), Mismatches, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetGeologyPodShapeTest,
	"StellarEra.Planet.Geology.PodShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetGeologyPodShapeTest::RunTest(const FString& Parameters)
{
	using namespace PlanetGeologyPodTest;

	FPlanetGeologyElementParams Params = MakeElement(TEXT("a"), 8, 0.06f, 55, 0.0f);
	Params.ShapeDetail = 3.0f;
	UPlanetGeologyPreset* Preset = MakePreset({Params});
	const auto Field = MakeField(*Preset);

	const TArray<FPlanetGeologyPod>& Pods = Field->GetPods(0);
	if (!TestEqual(TEXT("矿囊数"), Pods.Num(), 8))
	{
		return false;
	}

	int32 InsideSolid = 0;
	int32 InsideVeined = 0;
	FRandomStream Rng(777);
	for (const FPlanetGeologyPod& Pod : Pods)
	{
		// 球外 5% 处场值必须精确为 0：这是闭网格的前提。
		const FVector Outside = Pod.Center + FVector(Pod.Radius * 1.05f, 0.f, 0.f);
		TestEqual(TEXT("矿囊外场值为 0"), Field->SampleElementNormAtNormalized(0, Outside), 0.f);

		// 囊内既不是实心球，也不是全空：噪声必须把形状打碎。
		for (int32 SampleIndex = 0; SampleIndex < 200; ++SampleIndex)
		{
			const FVector Offset(
				Rng.FRandRange(-1.f, 1.f) * Pod.Radius,
				Rng.FRandRange(-1.f, 1.f) * Pod.Radius,
				Rng.FRandRange(-1.f, 1.f) * Pod.Radius);
			if (Offset.SizeSquared() > Pod.Radius * Pod.Radius)
			{
				continue;
			}
			++InsideSolid;
			if (Field->SampleElementNormAtNormalized(0, Pod.Center + Offset) > 0.3f)
			{
				++InsideVeined;
			}
		}
	}

	const double Ratio = InsideSolid > 0 ? double(InsideVeined) / double(InsideSolid) : 0.0;
	TestTrue(*FString::Printf(TEXT("囊内高值点占比 %.2f 应在 (0.05, 0.95)"), Ratio),
		Ratio > 0.05 && Ratio < 0.95);
	return true;
}

#endif
