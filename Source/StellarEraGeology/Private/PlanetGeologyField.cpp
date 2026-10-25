// Copyright Voxel Plugin SAS. All Rights Reserved.
// 矿囊采样与地质场构建。

#include "PlanetGeologyField.h"

#include "FastNoise/VoxelFastNoise.inl"
#include "PlanetGeologyPreset.h"
#include "Math/RandomStream.h"

namespace
{
	FIntVector ToPodCell(const FVector& Point, float CellSize)
	{
		return FIntVector(
			FMath::FloorToInt(Point.X / CellSize),
			FMath::FloorToInt(Point.Y / CellSize),
			FMath::FloorToInt(Point.Z / CellSize));
	}

	/** 球内二次衰减（1 - d²/r²）：中心 1，表面 0，囊外 0。 */
	float PodInfluence(const FPlanetGeologyPod& Pod, const FVector& Point)
	{
		const float RadiusSquared = Pod.Radius * Pod.Radius;
		if (RadiusSquared <= 0.f)
		{
			return 0.f;
		}
		const float DistanceSquared = (Point - Pod.Center).SizeSquared();
		if (DistanceSquared >= RadiusSquared)
		{
			return 0.f;
		}
		return 1.f - DistanceSquared / RadiusSquared;
	}

	void PlacePods(FPlanetGeologyElement& Definition, const FPlanetGeologyElementParams& Params)
	{
		const int32 PodCount = FMath::Max(0, Params.PodCount);
		Definition.Pods.Reset();
		Definition.Pods.Reserve(PodCount);
		if (PodCount == 0)
		{
			return;
		}

		FRandomStream Rng(Definition.Seed);
		const double Jitter = FMath::Clamp(double(Params.PodRadiusJitter), 0.0, 1.0);
		const double DepthBias = FMath::Clamp(double(Params.PodDepthBias), 0.0, 0.9);
		const double DepthCenter = FMath::Clamp(double(Params.PodDepthCenter), 0.0, 1.0);
		const double DepthSpread = FMath::Max(double(Params.PodDepthSpread), 0.01);

		for (int32 PodIndex = 0; PodIndex < PodCount; ++PodIndex)
		{
			const double RadiusJitter = 1.0 + Jitter * (2.0 * double(Rng.FRand()) - 1.0);
			const double Radius = FMath::Clamp(
				double(Definition.PodRadius) * RadiusJitter, 1e-3, 0.25);

			// 球面均匀方向；径向距离另抽，两者独立。
			const FVector Direction = Rng.GetUnitVector();

			// ∛U 让**体积**均匀（近地表体积大，矿囊自然更多）。r ∈ [0,1] 是「球心距比例」。
			double RadiusRatio = FMath::Pow(double(Rng.FRand()), 1.0 / 3.0);

			// 作者参数里的 DepthCenter 是「深度」（0 = 地表，1 = 球心），与半径比例
			// 互为补角 —— 带权时必须先换算成 1 - r 再比，否则偏好会朝反方向偏。
			if (DepthBias > 0.0)
			{
				// 带权拒绝采样：权重在 DepthCenter 附近最高，最多试 64 次。
				for (int32 Attempt = 0; Attempt < 64; ++Attempt)
				{
					const double CandidateRadius = FMath::Pow(double(Rng.FRand()), 1.0 / 3.0);
					const double CandidateDepth = 1.0 - CandidateRadius;
					const double Offset = (CandidateDepth - DepthCenter) / DepthSpread;
					const double Weight =
						(1.0 - DepthBias) + DepthBias * FMath::Exp(-Offset * Offset);
					RadiusRatio = CandidateRadius;
					if (double(Rng.FRand()) < Weight)
					{
						break;
					}
				}
			}

			FPlanetGeologyPod& Pod = Definition.Pods.AddDefaulted_GetRef();
			Pod.Radius = float(Radius);
			// **不要乘 (1 - r)**：乘了矿囊就永远够不到半径 1，地形在深度 0 采样时一个矿
			// 都看不到（露头恒为 0，验收 3 必挂）。探出球面的部分场值恒 0，观测视图里
			// 顶点会按 |v| <= R 径向夹回，两边都闭合。
			Pod.Center = Direction * float(RadiusRatio);
		}
	}
}

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
		Definition.Octaves = FMath::Clamp(Params.Octaves, 1, 6);
		Definition.Lacunarity = FMath::Max(Params.Lacunarity, 1.f);
		Definition.Gain = FMath::Clamp(Params.Gain, 0.f, 1.f);
		// Seed 0 = 按 Id 派生：不同元素默认互不相关，且跨进程稳定。
		Definition.Seed = Params.Seed != 0
			? Params.Seed
			: int32(GetTypeHash(Params.Id) & 0x7fffffffu);
		Definition.PodRadius = FMath::Clamp(Params.PodRadius, 0.001f, 0.25f);
		Definition.ShapeDetail = FMath::Max(Params.ShapeDetail, 1.f);
		Definition.VeinThreshold = FMath::Clamp(Params.VeinThreshold, 0.f, 0.95f);

		PlacePods(Definition, Params);

		FElementRuntime& Runtime = Field->Elements.AddDefaulted_GetRef();
		Runtime.Definition = Definition;
		Runtime.Noise.SetSeed(Definition.Seed);
		Runtime.Noise.SetFractalLacunarity(Definition.Lacunarity);
		// FractalBounding 只算一次；octaves / gain 必须与采样时传进
		// GetPerlinFractal_3D 的值完全一致，否则归一化系数会错位。
		Runtime.Noise.SetFractalOctavesAndGain(Definition.Octaves, Definition.Gain);

		Field->TotalAbundance += Definition.Abundance;
	}

	// 矿囊可及整个球体：可及深度就是行星半径。不设的话 SampleGeometryDelta 的
	// Depth > MaxReachDepth 早退对任何球内点都成立，GeometryAmplitude 永远不生效。
	Field->MaxReachDepth = float(FMath::Max(Field->RadiusVoxels, 0.0));

	Field->BuildPodIndex();
	return Field;
}

void FPlanetGeologyField::BuildPodIndex()
{
	PodIndex.Reset();
	PodIndexCellSize = 0.f;

	float MaxRadius = 0.f;
	for (const FElementRuntime& Runtime : Elements)
	{
		for (const FPlanetGeologyPod& Pod : Runtime.Definition.Pods)
		{
			MaxRadius = FMath::Max(MaxRadius, Pod.Radius);
		}
	}
	if (MaxRadius <= 0.f)
	{
		return;
	}

	PodIndexCellSize = MaxRadius;
	for (int32 ElementIndex = 0; ElementIndex < Elements.Num(); ++ElementIndex)
	{
		const TArray<FPlanetGeologyPod>& Pods = Elements[ElementIndex].Definition.Pods;
		for (int32 PodCursor = 0; PodCursor < Pods.Num(); ++PodCursor)
		{
			const FPlanetGeologyPod& Pod = Pods[PodCursor];
			const FIntVector MinCell = ToPodCell(Pod.Center - FVector(Pod.Radius), PodIndexCellSize);
			const FIntVector MaxCell = ToPodCell(Pod.Center + FVector(Pod.Radius), PodIndexCellSize);
			for (int32 X = MinCell.X; X <= MaxCell.X; ++X)
			{
				for (int32 Y = MinCell.Y; Y <= MaxCell.Y; ++Y)
				{
					for (int32 Z = MinCell.Z; Z <= MaxCell.Z; ++Z)
					{
						FPlanetGeologyPodRef Ref;
						Ref.ElementIndex = ElementIndex;
						Ref.PodIndex = PodCursor;
						PodIndex.FindOrAdd(FIntVector(X, Y, Z)).Add(Ref);
					}
				}
			}
		}
	}
}

float FPlanetGeologyField::EvaluateElementNorm(const int32 ElementIndex,
	const FVector& NormalizedPoint) const
{
	const FElementRuntime& Runtime = Elements[ElementIndex];
	const FPlanetGeologyElement& Definition = Runtime.Definition;
	if (Definition.Pods.Num() == 0 || PodIndexCellSize <= 0.f)
	{
		return 0.f;
	}

	// 1) 空间索引取候选矿囊，多囊取 max。
	float PodFactor = 0.f;
	const FIntVector Cell = ToPodCell(NormalizedPoint, PodIndexCellSize);
	if (const TArray<FPlanetGeologyPodRef>* Refs = PodIndex.Find(Cell))
	{
		for (const FPlanetGeologyPodRef& Ref : *Refs)
		{
			if (Ref.ElementIndex != ElementIndex)
			{
				continue;
			}
			PodFactor = FMath::Max(PodFactor,
				PodInfluence(Definition.Pods[Ref.PodIndex], NormalizedPoint));
			if (PodFactor >= 1.f)
			{
				break;
			}
		}
	}
	if (PodFactor <= 0.f)
	{
		return 0.f; // 不在任何矿囊里：一次 Perlin 都不求
	}

	// 2) 形状噪声。频率折叠进坐标（ShapeDetail 是「每囊直径的周期数」，
	//    直径 = 2r），噪声 API 的频率参数固定传 1。
	// 低于 1 时波长比矿囊直径还长，整囊可能落进噪声谷而消失（meta 可能被旧数据绕过）。
	const double ShapeDetail = FMath::Max(double(Definition.ShapeDetail), 1.0);
	const double NoiseScale =
		ShapeDetail / (2.0 * FMath::Max(double(Definition.PodRadius), 1e-4));
	const double SampleX = double(NormalizedPoint.X) * NoiseScale;
	const double SampleY = double(NormalizedPoint.Y) * NoiseScale;
	const double SampleZ = double(NormalizedPoint.Z) * NoiseScale;

	const double RawNoise = double(Runtime.Noise.GetPerlinFractal_3D(
		v_flt(SampleX), v_flt(SampleY), v_flt(SampleZ), v_flt(1.f), Definition.Octaves));

	// Perlin 输出 [-1,1]，映射到 [0,1] 才是「含量」。
	const double Noise01 = 0.5 * (RawNoise + 1.0);

	// 矿脉门：阈值以下完全为 0，阈值以上线性升到 1。
	const double Threshold = FMath::Clamp(double(Definition.VeinThreshold), 0.0, 0.95);
	const double Vein = Threshold > 0.0
		? FMath::Clamp((Noise01 - Threshold) / (1.0 - Threshold), 0.0, 1.0)
		: Noise01;

	return float(double(PodFactor) * Vein);
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
	const FElementRuntime& Runtime = Elements[ElementIndex];
	const FPlanetGeologyElement& Definition = Runtime.Definition;

	float PodFactor = 0.f;
	for (const FPlanetGeologyPod& Pod : Definition.Pods)
	{
		PodFactor = FMath::Max(PodFactor, PodInfluence(Pod, NormalizedPoint));
	}
	if (PodFactor <= 0.f)
	{
		return 0.f;
	}

	// 低于 1 时波长比矿囊直径还长，整囊可能落进噪声谷而消失（meta 可能被旧数据绕过）。
	const double ShapeDetail = FMath::Max(double(Definition.ShapeDetail), 1.0);
	const double NoiseScale =
		ShapeDetail / (2.0 * FMath::Max(double(Definition.PodRadius), 1e-4));
	const double RawNoise = double(Runtime.Noise.GetPerlinFractal_3D(
		v_flt(double(NormalizedPoint.X) * NoiseScale),
		v_flt(double(NormalizedPoint.Y) * NoiseScale),
		v_flt(double(NormalizedPoint.Z) * NoiseScale),
		v_flt(1.f), Definition.Octaves));
	const double Noise01 = 0.5 * (RawNoise + 1.0);
	const double Threshold = FMath::Clamp(double(Definition.VeinThreshold), 0.0, 0.95);
	const double Vein = Threshold > 0.0
		? FMath::Clamp((Noise01 - Threshold) / (1.0 - Threshold), 0.0, 1.0)
		: Noise01;

	return float(double(PodFactor) * Vein);
}

const TArray<FPlanetGeologyPod>& FPlanetGeologyField::GetPods(const int32 ElementIndex) const
{
	static const TArray<FPlanetGeologyPod> Empty;
	return Elements.IsValidIndex(ElementIndex)
		? Elements[ElementIndex].Definition.Pods
		: Empty;
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
		const FElementRuntime& Runtime = Elements[ElementIndex];
		const FPlanetGeologyElement& Definition = Runtime.Definition;

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
	for (const FElementRuntime& Runtime : Elements)
	{
		Out.Add(Runtime.Definition);
	}
	return Out;
}
