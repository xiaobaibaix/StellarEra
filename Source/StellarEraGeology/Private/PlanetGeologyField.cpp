// Copyright Voxel Plugin SAS. All Rights Reserved.
// 矿囊采样与地质场构建。

#include "PlanetGeologyField.h"

#include "PlanetGeologyPreset.h"
#include "PlanetGeologySpherePodsDistribution.h"

TSharedPtr<const FPlanetGeologyField> FPlanetGeologyField::BuildFromPreset(
	const UPlanetGeologyPreset& Preset, const double PlanetRadiusVoxels)
{
	check(IsInGameThread());

	TSharedPtr<FPlanetGeologyField> Field = MakeShared<FPlanetGeologyField>();
	Field->RadiusVoxels = FMath::Max(PlanetRadiusVoxels, 0.0);
	Field->Appearance = Preset.Appearance;
	// 与旧 JSON 校验器同界：绕过 Details（Python/MCP、手改资产）的写入也不能把地表推出世界。
	Field->GeometryAmplitude = FMath::Clamp(Preset.GeometryAmplitude, 0.f, 64.f);

	Field->Elements.Reserve(Preset.Elements.Num());
	for (const FPlanetGeologyElementParams& Params : Preset.Elements)
	{
		FPlanetGeologyElement Definition;
		Definition.Id = Params.Id.ToString();
		Definition.DisplayName = Params.DisplayName.IsEmpty()
			? Params.Id.ToString()
			: Params.DisplayName;
		Definition.Color = Params.Color;
		Definition.Abundance = FMath::Clamp(Params.Abundance, 0.f, 1.f);

		for (int32 DistributionIndex = 0;
			DistributionIndex < Params.Distributions.Num(); ++DistributionIndex)
		{
			const UPlanetGeologyDistribution* Distribution =
				Params.Distributions[DistributionIndex];
			if (!Distribution)
			{
				continue;
			}

			FPlanetGeologyDistributionBuildContext Context;
			Context.ElementId = Params.Id;
			Context.DistributionIndex = DistributionIndex;

			TSharedPtr<const FPlanetGeologyDistributionRuntime> Runtime =
				Distribution->BuildRuntime(Context);
			if (!Runtime.IsValid() || Runtime->IsEmpty())
			{
				continue;
			}

			TArray<FPlanetGeologyRegion> Regions;
			Runtime->EnumerateRegions(Regions);
			UE_LOG(LogTemp, Log, TEXT("元素=%s 生成器=%s 基元=%d 区域=%d %s"),
				*Params.Id.ToString(), Runtime->GetDebugName(),
				Runtime->NumPrimitives(), Regions.Num(), *Runtime->GetDebugSummary());

			Definition.Distributions.Add(MoveTemp(Runtime));
		}

		// 遗留兜底：旧资产没有 Distributions 但有 PodCount。种子必须是旧公式，
		// 用新派生规则会让旧行星的矿囊全部换位置。
		if (Definition.Distributions.Num() == 0 && Params.PodCount > 0)
		{
			const int32 LegacySeed = Params.Seed != 0
				? Params.Seed
				: int32(GetTypeHash(Params.Id) & 0x7fffffffu);
			Definition.Distributions.Add(
				UPlanetGeologySpherePodsDistribution::MakeRuntimeFromLegacyParams(
					Params.PodCount, Params.PodRadius, Params.PodRadiusJitter,
					Params.PodDepthBias, Params.PodDepthCenter, Params.PodDepthSpread,
					LegacySeed, Params.ShapeDetail, Params.Octaves, Params.Lacunarity,
					Params.Gain, Params.VeinThreshold));
			UE_LOG(LogTemp, Log,
				TEXT("元素=%s 生成器=SpherePods(遗留兜底) 基元=%d 区域=%d"),
				*Params.Id.ToString(), Params.PodCount, Params.PodCount);
		}

		Field->TotalAbundance += Definition.Abundance;
		Field->Elements.Add(MoveTemp(Definition));
	}

	// 矿囊可及整个球体：可及深度就是行星半径。不设的话 SampleGeometryDelta 的
	// Depth > MaxReachDepth 早退对任何球内点都成立，GeometryAmplitude 永远不生效。
	Field->MaxReachDepth = float(FMath::Max(Field->RadiusVoxels, 0.0));

	return Field;
}

float FPlanetGeologyField::EvaluateElementNorm(const int32 ElementIndex,
	const FVector& NormalizedPoint) const
{
	const FPlanetGeologyElement& Element = Elements[ElementIndex];
	float Norm = 0.f;
	for (const TSharedPtr<const FPlanetGeologyDistributionRuntime>& Distribution
		: Element.Distributions)
	{
		Norm = FMath::Max(Norm, Distribution->EvaluateNormalized(NormalizedPoint));
		if (Norm >= 1.f)
		{
			break;
		}
	}
	return Norm;
}

float FPlanetGeologyField::SampleElementNormAtNormalized(const int32 ElementIndex,
	const FVector& NormalizedPoint) const
{
	if (!Elements.IsValidIndex(ElementIndex))
	{
		return 0.f;
	}
	if (NormalizedPoint.SizeSquared() > 1.f + 1e-4f)
	{
		return 0.f; // 球外
	}
	return EvaluateElementNorm(ElementIndex, NormalizedPoint);
}

float FPlanetGeologyField::SampleElementNormBruteForce(const int32 ElementIndex,
	const FVector& NormalizedPoint) const
{
	if (!Elements.IsValidIndex(ElementIndex))
	{
		return 0.f;
	}
	float Norm = 0.f;
	for (const TSharedPtr<const FPlanetGeologyDistributionRuntime>& Distribution
		: Elements[ElementIndex].Distributions)
	{
		Norm = FMath::Max(Norm, Distribution->EvaluateBruteForce(NormalizedPoint));
	}
	if (Norm <= 0.f)
	{
		return 0.f;
	}
	return Norm;
}

const TArray<FPlanetGeologyPod>& FPlanetGeologyField::GetPods(const int32 ElementIndex) const
{
	static const TArray<FPlanetGeologyPod> Empty;
	if (!Elements.IsValidIndex(ElementIndex))
	{
		return Empty;
	}
	for (const TSharedPtr<const FPlanetGeologyDistributionRuntime>& Distribution
		: Elements[ElementIndex].Distributions)
	{
		const TArray<FPlanetGeologyPod>& Pods = Distribution->GetSpherePods();
		if (Pods.Num() > 0)
		{
			return Pods;
		}
	}
	return Empty;
}

void FPlanetGeologyField::EnumerateRegions(const int32 ElementIndex,
	TArray<FPlanetGeologyRegion>& Out) const
{
	Out.Reset();
	if (!Elements.IsValidIndex(ElementIndex))
	{
		return;
	}
	for (const TSharedPtr<const FPlanetGeologyDistributionRuntime>& Distribution
		: Elements[ElementIndex].Distributions)
	{
		Distribution->EnumerateRegions(Out);
	}
}

bool FPlanetGeologyField::IsEmpty() const
{
	if (Elements.Num() == 0)
	{
		return true;
	}
	for (const FPlanetGeologyElement& Element : Elements)
	{
		for (const TSharedPtr<const FPlanetGeologyDistributionRuntime>& Distribution
			: Element.Distributions)
		{
			if (Distribution.IsValid() && !Distribution->IsEmpty())
			{
				return false;
			}
		}
	}
	return true;
}

FPlanetGeologySample FPlanetGeologyField::Sample(const FVector& UnitDir, double Depth) const
{
	FPlanetGeologySample Result;
	if (Elements.Num() == 0)
	{
		return Result;
	}

	FAccumulation Accumulation;
	FLinearColor WeightedColor = FLinearColor::Black;
	Accumulate(UnitDir, Depth, true, Accumulation, WeightedColor);

	// DominantNorm / DominantIndex 只依赖各元素 Norm，与丰度无关：即使全元素
	// Abundance = 0（退化配置）也要给出，否则会与 SampleElementNorms 的契约分叉
	//（观察体照长、地形照凸，而 Sample 却说「没有矿」）。
	Result.DominantNorm = FMath::Clamp(float(Accumulation.DominantNorm), 0.f, 1.f);
	Result.DominantIndex = Accumulation.DominantIndex;

	if (TotalAbundance <= 0.f || Accumulation.Total <= UE_DOUBLE_KINDA_SMALL_NUMBER)
	{
		// 没有颜色权重（丰度和为 0）：TotalNorm / Purity / 颜色留默认值。
		return Result;
	}

	Result.TotalNorm = FMath::Clamp(
		float(Accumulation.Total / double(TotalAbundance)), 0.f, 1.f);
	Result.Purity = FMath::Clamp(
		float(Accumulation.MaxRaw / Accumulation.Total), 0.f, 1.f);
	Result.MineralColor = WeightedColor / float(Accumulation.Total);
	// 累加器初值 A = 1，除法会把 alpha 也除掉；MineralColor 是纯颜色，A 恒为 1。
	Result.MineralColor.A = 1.f;
	return Result;
}

float FPlanetGeologyField::SampleGeometryDelta(const FVector& UnitDir, double Depth) const
{
	if (Elements.Num() == 0)
	{
		return 0.f;
	}
	// 矿囊之外没有任何贡献；绝大多数行星内部体积走这一条，省掉一次全元素遍历。
	if (Depth < -double(MaxReachDepth) || Depth > double(MaxReachDepth))
	{
		return 0.f;
	}

	FAccumulation Accumulation;
	FLinearColor UnusedColor = FLinearColor::Black;
	Accumulate(UnitDir, Depth, false, Accumulation, UnusedColor);

	// 取各元素 Norm 的**最大值**，不做丰度加权平均：Abundance 的承诺是「只影响颜色
	// 混合权重与图例占比」，地形外凸量必须与它无关。这样外凸量也仍被
	// GeometryAmplitude 严格限住（≤ GeometryAmplitude），元素再多也不会变强或变弱。
	const double MaxNorm = Accumulation.DominantNorm;
	if (MaxNorm <= 0.0)
	{
		return 0.f;
	}
	return GeometryAmplitude * FMath::Clamp(float(MaxNorm), 0.f, 1.f);
}

void FPlanetGeologyField::SampleElementNorms(const FVector& UnitDir, double Depth,
	TArray<float>& OutNorms) const
{
	OutNorms.Reset();
	OutNorms.SetNumZeroed(Elements.Num());
	if (Elements.Num() == 0)
	{
		return;
	}

	FAccumulation Accumulation;
	FLinearColor UnusedColor = FLinearColor::Black;
	Accumulate(UnitDir, Depth, false, Accumulation, UnusedColor, &OutNorms);
}

void FPlanetGeologyField::Accumulate(const FVector& UnitDir, double Depth, bool bComputeColor,
	FAccumulation& OutAccumulation, FLinearColor& OutWeightedColor,
	TArray<float>* OutElementNorms) const
{
	OutAccumulation = FAccumulation();
	OutWeightedColor = FLinearColor::Black;

	// 矿囊用归一化坐标；半径 0 时退化成方向。
	const FVector NormalizedPoint = RadiusVoxels > 0.0
		? UnitDir * float(1.0 - Depth / RadiusVoxels)
		: UnitDir;

	double WeightedR = 0.0;
	double WeightedG = 0.0;
	double WeightedB = 0.0;

	for (int32 ElementIndex = 0; ElementIndex < Elements.Num(); ++ElementIndex)
	{
		const FPlanetGeologyElement& Definition = Elements[ElementIndex];

		// 单元素求值：矿囊径向衰减 × 噪声稀疏。没有任何矿囊时恒 0。
		double ElementNorm = double(EvaluateElementNorm(ElementIndex, NormalizedPoint));
		double ElementRaw = double(Definition.Abundance) * ElementNorm;

		if (OutElementNorms && OutElementNorms->IsValidIndex(ElementIndex))
		{
			(*OutElementNorms)[ElementIndex] = float(ElementNorm);
		}

		if (ElementNorm <= 0.0)
		{
			continue;
		}

		OutAccumulation.Total += ElementRaw;
		OutAccumulation.MaxRaw = FMath::Max(OutAccumulation.MaxRaw, ElementRaw);
		if (ElementNorm > OutAccumulation.DominantNorm)
		{
			OutAccumulation.DominantNorm = ElementNorm;
			OutAccumulation.DominantIndex = ElementIndex;
		}

		if (bComputeColor && ElementRaw > 0.0)
		{
			WeightedR += ElementRaw * double(Definition.Color.R);
			WeightedG += ElementRaw * double(Definition.Color.G);
			WeightedB += ElementRaw * double(Definition.Color.B);
		}
	}

	OutWeightedColor = FLinearColor(
		float(WeightedR), float(WeightedG), float(WeightedB), 1.f);
}

TArray<FPlanetGeologyElement> FPlanetGeologyField::GetSampledElementDefinitions() const
{
	TArray<FPlanetGeologyElement> Out;
	Out.Reserve(Elements.Num());
	for (const FPlanetGeologyElement& ElementDefinition : Elements)
	{
		Out.Add(ElementDefinition);
	}
	return Out;
}
