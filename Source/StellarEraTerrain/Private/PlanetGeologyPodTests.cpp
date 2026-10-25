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

	// 0) 矿囊路径的几何幅度必须真的能生效：MaxReachDepth 不设时 SampleGeometryDelta
	//    会在 Depth > 0 直接早退，GeometryAmplitude 变成死参数。这两条断言是那个修复的护栏。
	TestEqual(TEXT("MaxReachDepth 等于行星半径"),
		Field->GetMaxReachDepth(), float(PlanetRadiusVoxels), 1e-3f);

	const FPlanetGeologyPod* InnermostPod = nullptr;
	for (int32 ElementIndex = 0; ElementIndex < 2; ++ElementIndex)
	{
		for (const FPlanetGeologyPod& Pod : Field->GetPods(ElementIndex))
		{
			if (!InnermostPod || Pod.Center.Size() < InnermostPod->Center.Size())
			{
				InnermostPod = &Pod;
			}
		}
	}
	TestTrue(TEXT("夹具里存在明显在球内的矿囊"),
		InnermostPod && InnermostPod->Center.Size() < 0.99);
	if (InnermostPod && InnermostPod->Center.Size() < 0.99)
	{
		const FVector UnitDir = InnermostPod->Center.GetSafeNormal();
		const double Depth = (1.0 - InnermostPod->Center.Size()) * PlanetRadiusVoxels;
		TestTrue(*FString::Printf(TEXT("矿囊中心处几何幅度 > 0（深度 %.1f）"),
			Depth), Field->SampleGeometryDelta(UnitDir, Depth) > 0.f);
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

	// 1) 球外：索引入口的契约是恒 0。暴力版没有球外早退，两版在这里本来就不同，
	//    所以只钉索引版的契约，不做相等比较。
	int32 OutsideNonZero = 0;
	FRandomStream OutsideRng(13579);
	for (int32 SampleIndex = 0; SampleIndex < 200; ++SampleIndex)
	{
		const FVector Outside = OutsideRng.GetUnitVector() * 1.2;
		for (int32 ElementIndex = 0; ElementIndex < 3; ++ElementIndex)
		{
			if (Field->SampleElementNormAtNormalized(ElementIndex, Outside) != 0.f)
			{
				++OutsideNonZero;
			}
		}
	}
	TestEqual(TEXT("球外点索引版恒为 0"), OutsideNonZero, 0);

	// 2) 矿囊边缘：d = r * (1 ± 1e-3)，索引版与暴力版必须一致。
	//    只取仍在单位球内的探针：球外点两版本就有意不同（见上一条契约）。
	int32 EdgeMismatches = 0;
	int32 EdgeChecked = 0;
	FRandomStream EdgeRng(24680);
	for (int32 ElementIndex = 0; ElementIndex < 3; ++ElementIndex)
	{
		for (const FPlanetGeologyPod& Pod : Field->GetPods(ElementIndex))
		{
			for (int32 DirIndex = 0; DirIndex < 6; ++DirIndex)
			{
				const FVector Direction = EdgeRng.GetUnitVector();
				for (const float Sign : {-1.f, 1.f})
				{
					const FVector Probe =
						Pod.Center + Direction * (Pod.Radius * (1.f + Sign * 1e-3f));
					if (Probe.SizeSquared() > 1.0 + 1e-4)
					{
						continue;
					}
					++EdgeChecked;
					const float Indexed = Field->SampleElementNormAtNormalized(ElementIndex, Probe);
					const float Brute = Field->SampleElementNormBruteForce(ElementIndex, Probe);
					if (!FMath::IsNearlyEqual(Indexed, Brute, 1e-5f))
					{
						++EdgeMismatches;
					}
				}
			}
		}
	}
	TestTrue(TEXT("至少检查到矿囊边缘点"), EdgeChecked > 0);
	TestEqual(TEXT("矿囊边缘索引版 == 暴力版"), EdgeMismatches, 0);

	// 3) 重叠区：在高密度夹具里找一对相交矿囊，再在连线中点附近比对两版。
	UPlanetGeologyPreset* DensePreset = MakePreset({
		MakeElement(TEXT("ov"), 120, 0.18f, 4242, 0.0f)});
	const auto DenseField = MakeField(*DensePreset);
	const TArray<FPlanetGeologyPod>& DensePods = DenseField->GetPods(0);

	const FPlanetGeologyPod* PodA = nullptr;
	const FPlanetGeologyPod* PodB = nullptr;
	for (int32 IndexA = 0; IndexA < DensePods.Num() && !PodA; ++IndexA)
	{
		for (int32 IndexB = IndexA + 1; IndexB < DensePods.Num(); ++IndexB)
		{
			if (FVector::Dist(DensePods[IndexA].Center, DensePods[IndexB].Center)
				< DensePods[IndexA].Radius + DensePods[IndexB].Radius)
			{
				PodA = &DensePods[IndexA];
				PodB = &DensePods[IndexB];
				break;
			}
		}
	}
	TestTrue(TEXT("夹具里存在相交矿囊对"), PodA != nullptr && PodB != nullptr);
	if (PodA && PodB)
	{
		int32 OverlapMismatches = 0;
		FRandomStream OverlapRng(112233);
		const FVector Midpoint = 0.5 * (PodA->Center + PodB->Center);
		for (int32 SampleIndex = 0; SampleIndex < 100; ++SampleIndex)
		{
			const FVector Probe(
				Midpoint.X + OverlapRng.FRandRange(-1.f, 1.f) * PodA->Radius * 0.25f,
				Midpoint.Y + OverlapRng.FRandRange(-1.f, 1.f) * PodA->Radius * 0.25f,
				Midpoint.Z + OverlapRng.FRandRange(-1.f, 1.f) * PodA->Radius * 0.25f);
			if (Probe.SizeSquared() > 1.0 + 1e-4)
			{
				continue;
			}
			const float Indexed = DenseField->SampleElementNormAtNormalized(0, Probe);
			const float Brute = DenseField->SampleElementNormBruteForce(0, Probe);
			if (!FMath::IsNearlyEqual(Indexed, Brute, 1e-5f))
			{
				++OverlapMismatches;
			}
		}
		TestEqual(TEXT("重叠区索引版 == 暴力版"), OverlapMismatches, 0);
	}

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

	// 单矿囊夹具：周围没有第二个囊，径向截断的断言才不会被「落在邻囊里」旁路掉。
	FPlanetGeologyElementParams Params = MakeElement(TEXT("a"), 1, 0.06f, 55, 0.0f);
	Params.ShapeDetail = 3.0f;
	UPlanetGeologyPreset* Preset = MakePreset({Params});
	const auto Field = MakeField(*Preset);

	const TArray<FPlanetGeologyPod>& Pods = Field->GetPods(0);
	if (!TestEqual(TEXT("矿囊数"), Pods.Num(), 1))
	{
		return false;
	}
	const FPlanetGeologyPod& Pod = Pods[0];

	// 球外 5% 处场值必须精确为 0：这是闭网格的前提。
	// 朝球心探，避免探针落到单位球外、被球外早退「代为」返 0（那样断言是空转）；
	// 真落到球外就跳过。
	const FVector Inward = Pod.Center.IsNearlyZero()
		? FVector(-1.f, 0.f, 0.f)
		: (-Pod.Center).GetSafeNormal();
	const FVector Outside = Pod.Center + Inward * (Pod.Radius * 1.05f);
	if (Outside.SizeSquared() <= 1.0 + 1e-4)
	{
		TestEqual(TEXT("矿囊外场值为 0"), Field->SampleElementNormAtNormalized(0, Outside), 0.f);
	}

	// 囊内既不是实心球，也不是全空：噪声必须把形状打碎。
	// 同样跳过球外采样点：它们恒 0，只会把占比往下拉，不代表形状。
	int32 InsideSolid = 0;
	int32 InsideVeined = 0;
	FRandomStream Rng(777);
	for (int32 SampleIndex = 0; SampleIndex < 2000; ++SampleIndex)
	{
		const FVector Offset(
			Rng.FRandRange(-1.f, 1.f) * Pod.Radius,
			Rng.FRandRange(-1.f, 1.f) * Pod.Radius,
			Rng.FRandRange(-1.f, 1.f) * Pod.Radius);
		if (Offset.SizeSquared() > Pod.Radius * Pod.Radius)
		{
			continue;
		}
		const FVector Probe = Pod.Center + Offset;
		if (Probe.SizeSquared() > 1.0 + 1e-4)
		{
			continue;
		}
		++InsideSolid;
		if (Field->SampleElementNormAtNormalized(0, Probe) > 0.3f)
		{
			++InsideVeined;
		}
	}

	const double Ratio = InsideSolid > 0 ? double(InsideVeined) / double(InsideSolid) : 0.0;
	TestTrue(*FString::Printf(TEXT("囊内高值点占比 %.2f 应在 (0.05, 0.95)"), Ratio),
		Ratio > 0.05 && Ratio < 0.95);
	return true;
}

#endif
