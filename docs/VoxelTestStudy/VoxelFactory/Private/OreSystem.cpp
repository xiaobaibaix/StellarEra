// 矿脉系统 —— 实现。

#include "OreSystem.h"

#include "FastNoise/VoxelFastNoise.h"
#include "FastNoise/VoxelFastNoise.inl"

///////////////////////////////////////////////////////////////////////////////
// 配置访问

const FOreDistribution* FOrePlanetConfig::GetDistribution(EOreType Ore) const
{
	switch (Ore)
	{
	case EOreType::Iron:      return &Iron;
	case EOreType::Copper:    return &Copper;
	case EOreType::Silicon:   return &Silicon;
	case EOreType::Titanium:  return &Titanium;
	case EOreType::Uranium:   return &Uranium;
	case EOreType::Water:     return &Water;
	default:                  return nullptr;
	}
}

void FOrePlanetConfig::GetAvailableOres(TArray<EOreType>& OutOres) const
{
	OutOres.Reset();

	static const EOreType AllOres[] = {
		EOreType::Iron, EOreType::Copper, EOreType::Silicon,
		EOreType::Titanium, EOreType::Uranium, EOreType::Water
	};

	for (EOreType Ore : AllOres)
	{
		const FOreDistribution* Dist = GetDistribution(Ore);
		if (Dist && Dist->bPresent && Dist->GlobalAbundance > 0.f)
		{
			OutOres.Add(Ore);
		}
	}
}

///////////////////////////////////////////////////////////////////////////////
// 初始化

void FOreSystem::Init(const FOrePlanetConfig& InConfig)
{
	Config = InConfig;

	SpatialNoise = MakeShared<FVoxelFastNoise>();
	// 用星球种子，让每颗星球的矿脉布局完全不同
	SpatialNoise->SetSeed(Config.PlanetSeed);
	SpatialNoise->SetFractalOctavesAndGain(3, 0.5f);
}

///////////////////////////////////////////////////////////////////////////////
// 深度剖面

float FOreSystem::ComputeDepthProfile(const FOreDistribution& Dist, double DepthVoxels)
{
	// DepthVoxels 是「相对基准半径的偏移」：
	//   0 = 基准半径（大致是海平面）
	//   负数 = 在地下
	//
	// 矿层用高斯分布建模：PeakDepth 处浓度最高，
	// 向上向下按 LayerThickness 衰减。
	if (Dist.LayerThickness <= 1.0)
	{
		return 0.f;
	}

	const double Delta = DepthVoxels - Dist.PeakDepth;
	const double Normalized = Delta / Dist.LayerThickness;

	// 高斯衰减：|x| < 2 时显著，> 3 时基本为 0
	const double Gaussian = FMath::Exp(-Normalized * Normalized);

	// 水有特殊情况：它主要在浅层与地表，不遵守高斯分布
	if (Dist.GlobalAbundance > 0.f)
	{
		// 简单处理：如果矿层很浅（PeakDepth > -20），
		// 在地表也有相当浓度 —— 水就是这样
		const double SurfaceBoost = FMath::Clamp(1.0 + Dist.PeakDepth / 100.0, 0.0, 1.0);
		return float(FMath::Clamp(Gaussian + SurfaceBoost * (1.0 - Gaussian) * 0.3, 0.0, 1.0));
	}

	return float(Gaussian);
}

///////////////////////////////////////////////////////////////////////////////
// 采样

float FOreSystem::SampleRichness(const FVector& VoxelPosition, EOreType Ore) const
{
	float Richness = 0.f;
	SampleRichness(VoxelPosition, Ore, Richness);
	return Richness;
}

float FOreSystem::SampleRichness(const FVector& VoxelPosition, EOreType Ore, float& OutRichness) const
{
	OutRichness = 0.f;

	const FOreDistribution* Dist = Config.GetDistribution(Ore);
	if (!Dist || !Dist->bPresent || Dist->GlobalAbundance <= 0.f)
	{
		return 0.f;
	}

	// ① 深度剖面
	const double DistanceFromCenter = double(VoxelPosition.Size());
	const double Depth = DistanceFromCenter - Config.PlanetRadius;
	const float DepthFactor = ComputeDepthProfile(*Dist, Depth);

	if (DepthFactor <= 0.001f)
	{
		return 0.f;   // 这个深度没有这种矿
	}

	// ② 空间噪声 —— 让矿脉成块而不是均匀分布
	//
	// 关键：噪声要在「单位球面 + 深度」上采样，
	// 否则同一半径上的浓度会完全一样，变成一层均匀的壳。
	float Spatial = 1.0f;
	if (SpatialNoise.IsValid() && Dist->SpatialVariance > 0.001f)
	{
		// 归一化方向，让噪声主要在球面上变化
		const double Dist3 = double(VoxelPosition.Size());
		FVector N = Dist3 > 1.0 ? VoxelPosition / Dist3 : FVector::UpVector;

		// 加一个深度维度作为 Z 分量，让不同深度的矿脉错开
		const FVector SamplePoint(
			N.X, N.Y, N.Z + float(Depth) / FMath::Max(Dist->LayerThickness, 1.0) * 0.3);

		// 噪声返回 [-1, 1]，映射到 [0, 1]
		const double Raw = SpatialNoise->GetPerlinFractal_3D(
			SamplePoint.X, SamplePoint.Y, SamplePoint.Z,
			Dist->SpatialFrequency, 3);

		const float Normalized = float((Raw + 1.0) * 0.5);

		// 用 Variance 在「均匀」和「完全由噪声决定」之间插值
		Spatial = FMath::Lerp(1.0f, Normalized, Dist->SpatialVariance);
	}

	// ③ 综合
	const float Richness = Dist->GlobalAbundance * DepthFactor * Spatial;

	// 阈值：低于它就没有矿。这是「矿脉」而非「均匀撒」的关键。
	OutRichness = (Richness >= Dist->Threshold)
		? FMath::Clamp((Richness - Dist->Threshold) / FMath::Max(1.f - Dist->Threshold, 0.01f), 0.f, 1.f)
		: 0.f;

	return OutRichness;
}

void FOreSystem::SampleAllOres(const FVector& VoxelPosition, TMap<EOreType, float>& OutOres) const
{
	OutOres.Reset();

	TArray<EOreType> Available;
	Config.GetAvailableOres(Available);

	for (EOreType Ore : Available)
	{
		const float Richness = SampleRichness(VoxelPosition, Ore);
		if (Richness > 0.f)
		{
			OutOres.Add(Ore, Richness);
		}
	}
}

EOreType FOreSystem::GetDominantOre(const FVector& VoxelPosition, float& OutRichness) const
{
	TMap<EOreType, float> All;
	SampleAllOres(VoxelPosition, All);

	OutRichness = 0.f;
	EOreType Best = EOreType::None;

	for (const TPair<EOreType, float>& Pair : All)
	{
		if (Pair.Value > OutRichness)
		{
			OutRichness = Pair.Value;
			Best = Pair.Key;
		}
	}

	return Best;
}

///////////////////////////////////////////////////////////////////////////////
// 开采计算

float FOreSystem::GetDigDifficulty(const FOreDistribution& Dist, double DepthVoxels)
{
	// 难度 = 硬度 × 深度系数
	//
	// 深度系数增长很快：浅层几乎不影响，深层显著变难。
	// 这让「挖到深层」本身成为一个目标。
	const double Depth = FMath::Max(-DepthVoxels, 0.0);
	const double DepthFactor = 1.0 + Depth * 0.0008 + Depth * Depth * 0.0000035;

	return float(FMath::Max(Dist.Hardness, 0.1f) * DepthFactor);
}

float FOreSystem::GetYield(const FOreDistribution& Dist, float Richness)
{
	// 产量 = 基础产量 × (0.5 + 浓度)
	//
	// 即使浓度很低（刚过阈值），也能产出 50% 基础量——
	// 玩家不会觉得「挖了半天什么都没拿到」。
	return Dist.BaseYield * (0.5f + FMath::Clamp(Richness, 0.f, 1.f) * 0.5f);
}
