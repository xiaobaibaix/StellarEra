// Copyright Voxel Plugin SAS. All Rights Reserved.
// 噪声地形模块：球面起伏 + 海平面 + 按高度与纬度分色。

#include "VoxelPlanetNoiseTerrain.h"

#include "VoxelMaterialBuilder.h"
#include "Math/RandomStream.h"
#include "PlanetGeologyField.h"

namespace
{
	// 极小的采样点偏移，避免返回恰好 0 的密度；区间查询必须用同一球心。
	constexpr double PlanetCenterEpsilon = 0.001;

	// 解析输出包络。写成常量而不是两处手抄 ±1：错了会静默出洞或整星卡死。
	constexpr double MinNormalizedOffset = -1.0;
	constexpr double MaxNormalizedOffset = 1.0;

	// 密度除数，与纯球模块一致，否则法线梯度变、光照观感跳。
	constexpr double DensityScale = 5.0;

	// 不用 FMath::Lerp：避开 FLinearColor 上的重载歧义，要最朴素的逐通道插值。
	FLinearColor MixColor(const FLinearColor& From, const FLinearColor& To, float Alpha)
	{
		return From + (To - From) * Alpha;
	}

	/** 材质打包的唯一出口。本期 = RGB 顶点色；换 MultiIndex 只改这里。 */
	FVoxelMaterial MakeVoxelMaterial(const FLinearColor& FinalColor)
	{
		FVoxelMaterialBuilder Builder;
		Builder.SetMaterialConfig(EVoxelMaterialConfig::RGB);
		Builder.SetColor(FinalColor);
		return Builder.Build();
	}
}

float ComputeRawPlanetOffset(const FPlanetNoiseParams& Params, const FVector& UnitDir)
{
	// 采在单位方向上而不是 UV 上：球面因此天然无缝，也没有极点奇点。
	const FVector SamplePos = UnitDir * Params.Frequency + Params.SeedOffset;

	float Sum = 0.f;
	float LayerAmplitude = 1.f;
	float AmplitudeSum = 0.f;
	float LayerFrequency = 1.f;

	for (int32 Index = 0; Index < Params.Octaves; ++Index)
	{
		Sum += FMath::PerlinNoise3D(SamplePos * LayerFrequency) * LayerAmplitude;
		AmplitudeSum += LayerAmplitude;
		LayerAmplitude *= Params.Gain;
		LayerFrequency *= Params.Lacunarity;
	}

	// 除以权重和把结果夹回 [-1,1]，是区间正确性的全部依据：每层 Perlin 在 [-1,1]，
	// 权重和恰为 AmplitudeSum。旧版的 FMath::Max(Noise, SeaLevel) 削平已删 ——
	// 海平面以下必须是真实海底，否则水面之下一片平。
	return (AmplitudeSum > 0.f) ? Sum / AmplitudeSum : 0.f;
}

///////////////////////////////////////////////////////////////////////////////

FPlanetNoiseParams UVoxelPlanetNoiseTerrain::MakeParams() const
{
	FPlanetNoiseParams Params;
	Params.Amplitude = Amplitude;
	Params.Frequency = Frequency;
	Params.Octaves = FMath::Clamp(Octaves, 1, 8);
	Params.Lacunarity = Lacunarity;
	Params.Gain = Gain;
	Params.SeaLevel = SeaLevel;

	Params.WaterDepthRange = WaterDepthRange;
	Params.SandToGrassHeight = SandToGrassHeight;
	Params.GrassToRockHeight = GrassToRockHeight;
	Params.PolarStart = PolarStart;

	Params.DeepWaterColor = DeepWaterColor;
	Params.ShallowWaterColor = ShallowWaterColor;
	Params.SandColor = SandColor;
	Params.GrassColor = GrassColor;
	Params.RockColor = RockColor;
	Params.SnowColor = SnowColor;

	// Perlin 无种子，靠挪采样起点换地貌。用 FRandomStream 而非 FMath::Rand：
	// 后者是全局状态，同一种子会跑出不同星球，地形不可复现。
	FRandomStream Stream(Seed);
	Params.SeedOffset = FVector(
		Stream.FRandRange(-1000.f, 1000.f),
		Stream.FRandRange(-1000.f, 1000.f),
		Stream.FRandRange(-1000.f, 1000.f));

	return Params;
}

TVoxelSharedRef<FVoxelGeneratorInstance> UVoxelPlanetNoiseTerrain::GetInstance(const FVoxelPlanetTerrainContext& Context)
{
	return MakeVoxelShared<FVoxelPlanetNoiseTerrainInstance>(Context, MakeParams());
}

float UVoxelPlanetNoiseTerrain::GetSurfaceRadiusVoxels(const FVoxelPlanetTerrainContext& Context, const FVector& UnitDir) const
{
	const FVector Direction = UnitDir.GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		return Context.Radius;
	}

	// 与密度场调用同一函数：角色脚下与眼睛看到的地面必须是同一个面。
	const FPlanetNoiseParams Params = MakeParams();
	const double BaseSurface =
		double(Context.Radius) + double(Params.Amplitude) * double(ComputeRawPlanetOffset(Params, Direction));

	// 河床也算地表：角色站在河里而非浮在水面（本期不做浮力），水面是独立的一张壳。
	double CarveDepth = 0.0;
	if (Context.Hydrology.IsValid())
	{
		CarveDepth = double(Context.Hydrology->Sample(Direction).CarveDepth);
	}

	double GeometryDelta = 0.0;
	if (Context.Geology.IsValid())
	{
		// 真实地表满足 R_s = Base − Carve + delta(R − R_s)，迭代一次。深度必须以
		// **下切后**地表为基准，否则脚下与眼睛最多差 MaxCarveDepth 个体素。
		const double Depth0 = double(Context.Radius) - (BaseSurface - CarveDepth);
		GeometryDelta = double(Context.Geology->SampleGeometryDelta(Direction, Depth0));
		GeometryDelta = double(Context.Geology->SampleGeometryDelta(
			Direction, Depth0 - GeometryDelta));
	}

	return float(BaseSurface - CarveDepth + GeometryDelta);
}

float UVoxelPlanetNoiseTerrain::GetMinSurfaceRadiusVoxels(const FVoxelPlanetTerrainContext& Context) const
{
	// 与 FVoxelPlanetNoiseTerrainInstance::MinSurfaceRadius 同一包络：海平面与地质外凸只会抬高地表。
	const double CarveDepth = Context.Hydrology.IsValid() ? double(Context.Hydrology->GetMaxCarveDepth()) : 0.0;
	return float(double(Context.Radius) + double(MakeParams().Amplitude) * MinNormalizedOffset - CarveDepth);
}

float UVoxelPlanetNoiseTerrain::GetRawOffset(const FVector& UnitDir) const
{
	const FVector Direction = UnitDir.GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		return 0.f;
	}

	// 每次重建 Params（512 分辨率约 157 万次调用，总量几十毫秒）。**不要**加
	// mutable 缓存：烘焙在 ParallelFor 里并发调用，缓存就是数据竞争。
	return ComputeRawPlanetOffset(MakeParams(), Direction);
}

bool UVoxelPlanetNoiseTerrain::GetWaterConfig(FPlanetWaterConfig& OutConfig) const
{
	// 先填满再返回：返回 false 时调用方也拿得到一份合法配置，不必分支初始化。
	FPlanetWaterConfig Config;
	Config.Params.SeaLevel = SeaLevel;
	Config.Params.Amplitude = Amplitude;
	Config.Params.MinRiverFlow = MinRiverFlow;
	Config.Params.MaxCarveDepth = MaxCarveDepth;
	Config.Params.RiverWidthScale = RiverWidthScale;
	Config.Params.Resolution = HydrologyResolution;

	Config.ShallowColor = ShallowWaterColor;
	Config.DeepColor = DeepWaterColor;

	OutConfig = Config;
	return bEnableWater;
}

///////////////////////////////////////////////////////////////////////////////

FVoxelPlanetNoiseTerrainInstance::FVoxelPlanetNoiseTerrainInstance(const FVoxelPlanetTerrainContext& Context, const FPlanetNoiseParams& InParams)
	// 宿主指针传空：它只是序列化用的软引用，而本实例从不被序列化。
	: Super(nullptr)
	, Radius(Context.Radius)
	, Center(Context.Center)
	, Params(InParams)
	, Geology(Context.Geology)
	, Hydrology(Context.Hydrology)
	// 与 GetValueRangeImpl 相同的包络，预先算好。下界 = Radius + Amplitude×(−1)
	// − MaxCarveDepth；GeoDelta 只外凸，只影响上界。
	, MinSurfaceRadius(double(Radius)
		+ double(Params.Amplitude) * MinNormalizedOffset
		- (Context.Hydrology.IsValid() ? double(Context.Hydrology->GetMaxCarveDepth()) : 0.0))
	, MaxSurfaceRadius(double(Radius) + double(Params.Amplitude) * MaxNormalizedOffset
		+ (Context.Geology.IsValid() ? double(Context.Geology->GetMaxGeometryDelta()) : 0.0))
{
}

FVector FVoxelPlanetNoiseTerrainInstance::GetUnitDirection(const FVector& RelativePosition) const
{
	const double SizeSquared = RelativePosition.SizeSquared();
	if (SizeSquared < UE_DOUBLE_SMALL_NUMBER)
	{
		// 采样点恰在球心：方向无意义，取恒定方向让值查询与区间查询在退化点仍自洽。
		return FVector::UpVector;
	}
	return RelativePosition / FMath::Sqrt(SizeSquared);
}

v_flt FVoxelPlanetNoiseTerrainInstance::GetValueImpl(v_flt X, v_flt Y, v_flt Z, int32 LOD, const FVoxelItemStack& Items) const
{
	// 与纯球模块相同的 epsilon 约定，球心与区间查询一致。
	const FVector Relative(
		double(X) - Center.X + PlanetCenterEpsilon,
		double(Y) - Center.Y + PlanetCenterEpsilon,
		double(Z) - Center.Z + PlanetCenterEpsilon);

	const double Distance = Relative.Size();

	// 内外早退：地表只在 [MinSurfaceRadius, MaxSurfaceRadius] 带内，带外更深必实体、
	// 更高必空气。只比较一次就跳过 Perlin 与整张地质场 —— 体素工具每个 32³ 叶子整叶
	// 采样，一次 1280 体素挖掘 ≈ 2×10⁹ 次调用。返回值取保守界，±DensityScale 余量
	// 保证存储值与精确计算同为 Full/Empty，洞壁位置零差异。
	if (Distance < MinSurfaceRadius - DensityScale)
	{
		return v_flt((Distance - MaxSurfaceRadius) / DensityScale);
	}
	if (Distance > MaxSurfaceRadius + DensityScale)
	{
		return v_flt((Distance - MinSurfaceRadius) / DensityScale);
	}

	const FVector UnitDir = GetUnitDirection(Relative);
	const double BaseSurface =
		double(Radius) + double(Params.Amplitude) * double(ComputeRawPlanetOffset(Params, UnitDir));

	// 河床下切：只往下挖，不动横向。没有水文场时恒为 0。
	const double CarveDepth = Hydrology.IsValid()
		? double(Hydrology->Sample(UnitDir).CarveDepth)
		: 0.0;

	// 深度以基准球面为 0、向内为正。几何增量只往外推，不挖坑。
	const double GeometryDelta = Geology.IsValid()
		? double(Geology->SampleGeometryDelta(UnitDir, double(Radius) - Distance))
		: 0.0;

	const double Value = (Distance - BaseSurface + CarveDepth - GeometryDelta) / DensityScale;

	// 不夹到 [-1,1]：存储层自己会饱和，但 LOD3 起中心差分采样间距超过 DensityScale
	// 会让法线饱和，退化成轴向色块。
	return v_flt(Value);
}

FLinearColor ComputePlanetSurfaceColor(
	const FPlanetNoiseParams& Params,
	const FPlanetGeologyField* Geology,
	const FPlanetHydrologyField* Hydrology,
	double Radius,
	const FVector& UnitDir,
	double Distance)
{
	const float Offset = ComputeRawPlanetOffset(Params, UnitDir);

	FLinearColor Color;

	if (Offset < Params.SeaLevel)
	{
		// 海床：浅处沙、越深越接近 DeepWaterColor。**这里不是水本身** —— 水的蓝、
		// 透明度与高光归水面材质；地形只如实记录海底颜色。
		const float Depth = FMath::Clamp(
			(Params.SeaLevel - Offset) / FMath::Max(Params.WaterDepthRange, 1e-4f), 0.f, 1.f);
		Color = MixColor(Params.SandColor, Params.DeepWaterColor, Depth);
	}
	else
	{
		// 陆地：把 [海平面, 1] 映射到 [0, 1]，再按两个阈值分三段。
		const float LandHeight = FMath::Clamp(
			(Offset - Params.SeaLevel) / FMath::Max(1.f - Params.SeaLevel, 1e-4f), 0.f, 1.f);

		if (LandHeight < Params.SandToGrassHeight)
		{
			Color = MixColor(Params.SandColor, Params.GrassColor,
				LandHeight / FMath::Max(Params.SandToGrassHeight, 1e-4f));
		}
		else if (LandHeight < Params.GrassToRockHeight)
		{
			Color = MixColor(Params.GrassColor, Params.RockColor,
				(LandHeight - Params.SandToGrassHeight) / FMath::Max(Params.GrassToRockHeight - Params.SandToGrassHeight, 1e-4f));
		}
		else
		{
			Color = MixColor(Params.RockColor, Params.SnowColor,
				(LandHeight - Params.GrassToRockHeight) / FMath::Max(1.f - Params.GrassToRockHeight, 1e-4f));
		}

		// 极地雪叠在高度配色之上，高山与两极都会变白。
		const float Latitude = FMath::Abs(UnitDir.Z);
		if (Latitude > Params.PolarStart)
		{
			const float SnowBlend = FMath::Clamp(
				(Latitude - Params.PolarStart) / FMath::Max(1.f - Params.PolarStart, 1e-4f), 0.f, 1.f);
			Color = MixColor(Color, Params.SnowColor, SnowBlend);
		}
	}

	// 河床：湿岩/湿沙比周围暗一档，只在陆地上做（海底河道已被深度插值盖住）。
	if (Hydrology && Offset >= Params.SeaLevel)
	{
		const float RiverStrength = Hydrology->Sample(UnitDir).RiverStrength;
		if (RiverStrength > 0.f)
		{
			// 只压暗 RGB、A 保持 1：顶点色与 cubemap 都存 A，半透明会把湿岩画成能看穿的坑。
			FLinearColor RiverbedColor = Color * 0.35f;
			RiverbedColor.A = 1.f;
			Color = MixColor(Color, RiverbedColor, RiverStrength);
		}
	}

	// 矿色**海底也叠**：海底是真实岩层，旧逻辑「必须高于海平面」会让海床变纯沙。
	if (Geology)
	{
		const double Depth = Radius - Distance;
		const FPlanetGeologySample GeologySample = Geology->Sample(UnitDir, Depth);

		const FPlanetGeologyAppearance& Appearance = Geology->GetAppearance();
		// 用最强元素自身强度判定：TotalNorm 会被丰度和稀释，低丰度元素永远够不到阈值。
		if (GeologySample.DominantNorm > Appearance.ColorThreshold)
		{
			const float Presence = FMath::Clamp(
				(GeologySample.DominantNorm - Appearance.ColorThreshold)
					/ FMath::Max(Appearance.FadeRange, 1e-4f),
				0.f, 1.f);
			const float PurityMix = FMath::Clamp(
				(GeologySample.Purity - (1.f - Appearance.PurityForFullColor))
					/ FMath::Max(Appearance.PurityForFullColor, 1e-4f),
				0.f, 1.f);
			Color = MixColor(Color, GeologySample.MineralColor, Presence * PurityMix);
		}
	}

	return Color;
}

FVoxelMaterial FVoxelPlanetNoiseTerrainInstance::GetMaterialImpl(v_flt X, v_flt Y, v_flt Z, int32 LOD, const FVoxelItemStack& Items) const
{
	const FVector Relative(
		double(X) - Center.X + PlanetCenterEpsilon,
		double(Y) - Center.Y + PlanetCenterEpsilon,
		double(Z) - Center.Z + PlanetCenterEpsilon);

	const FVector UnitDir = GetUnitDirection(Relative);

	return MakeVoxelMaterial(ComputePlanetSurfaceColor(
		Params, Geology.Get(), Hydrology.Get(), double(Radius), UnitDir, Relative.Size()));
}

TVoxelRange<v_flt> FVoxelPlanetNoiseTerrainInstance::GetValueRangeImpl(const FVoxelIntBox& Bounds, int32 LOD, const FVoxelItemStack& Items) const
{
	// 最近/最远距离算法与纯球模块逐字相同；密度仍是距离减去随方向变的地表半径。
	const double CX = Center.X - PlanetCenterEpsilon;
	const double CY = Center.Y - PlanetCenterEpsilon;
	const double CZ = Center.Z - PlanetCenterEpsilon;

	const auto NearestDelta = [](double Min, double Max, double C) -> double
	{
		if (C < Min) { return Min - C; }
		if (C > Max) { return C - Max; }
		return 0.0;
	};
	const auto FarthestDelta = [](double Min, double Max, double C) -> double
	{
		return FMath::Max(FMath::Abs(Min - C), FMath::Abs(Max - C));
	};

	const double NearX = NearestDelta(Bounds.Min.X, Bounds.Max.X, CX);
	const double NearY = NearestDelta(Bounds.Min.Y, Bounds.Max.Y, CY);
	const double NearZ = NearestDelta(Bounds.Min.Z, Bounds.Max.Z, CZ);
	const double MinDistance = FMath::Sqrt(NearX * NearX + NearY * NearY + NearZ * NearZ);

	const double FarX = FarthestDelta(Bounds.Min.X, Bounds.Max.X, CX);
	const double FarY = FarthestDelta(Bounds.Min.Y, Bounds.Max.Y, CY);
	const double FarZ = FarthestDelta(Bounds.Min.Z, Bounds.Max.Z, CZ);
	const double MaxDistance = FMath::Sqrt(FarX * FarX + FarY * FarY + FarZ * FarZ);

	// 位移的确定包络 ±1。保守是安全方向：估高只多算区块，估低地表出洞。
	const double MaxOffset = MaxNormalizedOffset;
	const double MinOffset = MinNormalizedOffset;

	// 最小密度 = 最近距离 − 最大地表半径；最大密度 = 最远距离 − 最小地表半径。
	// 最大地表半径 = Radius + Amplitude×MaxOffset + 最大地质增量（矿脉只外凸）；
	// 最小 = Radius + Amplitude×MinOffset − 最大下切量（河床只下挖）。两项各压一侧，
	// 方向**不能写反** —— 写反不会崩，只会静默出洞或冒实体块，由 Terrain 用例兜底。
	const double MaxGeometryDelta =
		Geology.IsValid() ? double(Geology->GetMaxGeometryDelta()) : 0.0;
	const double MaxCarveDepth =
		Hydrology.IsValid() ? double(Hydrology->GetMaxCarveDepth()) : 0.0;

	const double MinValue =
		(MinDistance - double(Radius) - double(Params.Amplitude) * MaxOffset
			- MaxGeometryDelta) / DensityScale;
	const double MaxValue =
		(MaxDistance - double(Radius) - double(Params.Amplitude) * MinOffset
			+ MaxCarveDepth) / DensityScale;

	return TVoxelRange<v_flt>(v_flt(MinValue), v_flt(MaxValue));
}

FVector FVoxelPlanetNoiseTerrainInstance::GetUpVector(v_flt X, v_flt Y, v_flt Z) const
{
	// 地形是球面沿径向的起伏，「上」仍是径向；有山有谷不改变重力方向。
	const FVector P(double(X) - Center.X, double(Y) - Center.Y, double(Z) - Center.Z);
	if (P.SizeSquared() < UE_DOUBLE_SMALL_NUMBER)
	{
		return FVector::UpVector;
	}
	return P.GetSafeNormal();
}
