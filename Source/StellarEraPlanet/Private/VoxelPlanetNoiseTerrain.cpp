// Copyright Voxel Plugin SAS. All Rights Reserved.
// 噪声地形模块：球面起伏 + 海平面 + 按高度与纬度分色。

#include "VoxelPlanetNoiseTerrain.h"

#include "VoxelMaterialBuilder.h"
#include "Math/RandomStream.h"
#include "PlanetGeologyField.h"

namespace
{
	// 与纯球模块一致：值查询给采样点加的极小偏移，避免返回恰好为 0 的密度值
	// （那会让网格化器出问题）。区间查询必须用同一个球心。
	constexpr double PlanetCenterEpsilon = 0.001;

	// 与纯球模块一致：密度除以的常数。两边必须相同，否则换地形时着色法线的
	// 梯度会变、光照观感会跳。
	constexpr double DensityScale = 5.0;

	// 两色线性混合。不用 FMath::Lerp 是为了避开模板在 FLinearColor 上的重载
	// 歧义 —— 这里要的就是最朴素的逐通道线性插值，不要 HSV 那种绕色相的插值。
	FLinearColor MixColor(const FLinearColor& From, const FLinearColor& To, float Alpha)
	{
		return From + (To - From) * Alpha;
	}

	/**
	 * 材质打包的唯一出口。本期 = RGB 顶点色；未来换 MultiIndex 只改这里，
	 * 地形与地质场的代码都不动。
	 */
	FVoxelMaterial MakeVoxelMaterial(const FPlanetGeologySample& Sample, const FLinearColor& FinalColor)
	{
		// 本期不用 Sample：元素颜色已经折算进 FinalColor。参数留着是为了
		// 将来接真实矿物材质时能直接拿到纯度与混合权重，不必再改调用点。
		(void)Sample;

		FVoxelMaterialBuilder Builder;
		Builder.SetMaterialConfig(EVoxelMaterialConfig::RGB);
		Builder.SetColor(FinalColor);
		return Builder.Build();
	}
}

float ComputePlanetOffset(const FPlanetNoiseParams& Params, const FVector& UnitDir)
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

	// 除以权重和，把结果夹回 [-1, 1]。
	//
	// 这一步是**区间正确性的全部依据**：每层 Perlin 的输出都在 [-1, 1]，权重和
	// 恰为 AmplitudeSum，所以归一化后的值必然在范围内。实例的 GetValueRangeImpl
	// 直接把 1 当作位移上界用，不再复核 —— 改这里必须同步改那里。
	const float Noise = (AmplitudeSum > 0.f) ? Sum / AmplitudeSum : 0.f;

	// 海平面：低于它的地形被削平。Max 同时把位移的下界抬到 SeaLevel，
	// 区间那边同样直接用这个值。
	return FMath::Max(Noise, Params.SeaLevel);
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

	// Perlin 没有种子参数，靠挪动采样起点换地貌。
	//
	// 用 FRandomStream 而不是 FMath::Rand：后者是全局状态，同一个种子在不同时候
	// 跑出不同的星球，地形就不可复现了。
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

	// 与密度场调用同一个 ComputePlanetOffset 和同一个 SampleGeometryDelta。
	// 角色脚下的地面和眼睛看到的地面必须是同一个面。
	const FPlanetNoiseParams Params = MakeParams();
	const double BaseSurface =
		double(Context.Radius) + double(Params.Amplitude) * double(ComputePlanetOffset(Params, Direction));

	double GeometryDelta = 0.0;
	if (Context.Geology.IsValid())
	{
		// 真实地表满足 R_s = Base + delta(R - R_s)。用不动点迭代一次，
		// 把 delta 自身的 1~2 体素偏移也收进来。
		const double Depth0 = double(Context.Radius) - BaseSurface;
		GeometryDelta = double(Context.Geology->SampleGeometryDelta(Direction, Depth0));
		GeometryDelta = double(Context.Geology->SampleGeometryDelta(
			Direction, Depth0 - GeometryDelta));
	}

	return float(BaseSurface + GeometryDelta);
}

///////////////////////////////////////////////////////////////////////////////

FVoxelPlanetNoiseTerrainInstance::FVoxelPlanetNoiseTerrainInstance(const FVoxelPlanetTerrainContext& Context, const FPlanetNoiseParams& InParams)
	// 宿主指针传空：它只是序列化用的软引用，而本实例从不被序列化。
	: Super(nullptr)
	, Radius(Context.Radius)
	, Center(Context.Center)
	, Params(InParams)
	, Geology(Context.Geology)
{
}

FVector FVoxelPlanetNoiseTerrainInstance::GetUnitDirection(const FVector& RelativePosition) const
{
	const double SizeSquared = RelativePosition.SizeSquared();
	if (SizeSquared < UE_DOUBLE_SMALL_NUMBER)
	{
		// 采样点恰好落在球心：方向无意义，任取一个。返回恒定方向而不是随机值，
		// 是为了让值查询与区间查询在退化点上仍然自洽。
		return FVector::UpVector;
	}
	return RelativePosition / FMath::Sqrt(SizeSquared);
}

v_flt FVoxelPlanetNoiseTerrainInstance::GetValueImpl(v_flt X, v_flt Y, v_flt Z, int32 LOD, const FVoxelItemStack& Items) const
{
	// 与纯球模块相同的 epsilon 约定。距离量到的是 (Center - epsilon)，与区间
	// 查询里的球心一致。
	const FVector Relative(
		double(X) - Center.X + PlanetCenterEpsilon,
		double(Y) - Center.Y + PlanetCenterEpsilon,
		double(Z) - Center.Z + PlanetCenterEpsilon);

	const double Distance = Relative.Size();
	const FVector UnitDir = GetUnitDirection(Relative);
	const double BaseSurface =
		double(Radius) + double(Params.Amplitude) * double(ComputePlanetOffset(Params, UnitDir));

	// 深度以基准球面为 0、向内为正。几何增量只往外推，不挖坑。
	const double GeometryDelta = Geology.IsValid()
		? double(Geology->SampleGeometryDelta(UnitDir, double(Radius) - Distance))
		: 0.0;

	const double Value = (Distance - BaseSurface - GeometryDelta) / DensityScale;

	// 不夹到 [-1, 1]。存储层（FVoxelValue）本来就会按 ±1 饱和，表面抽取只看
	// 符号，夹不夹对几何没有影响；但 LOD > 0 的着色法线是插件用**生成器浮点值**
	// 做中心差分算出来的，采样间距 = 2^LOD 体素（VoxelMesher.cpp 的 Step）。
	// 先夹到 ±1 的话，间距一旦明显超过 DensityScale（LOD 3 起），6 个采样就会
	// 全部饱和，法线只剩 ±1 分量，退化成 26 个轴向对角方向之一 —— 粗 LOD 的
	// 大块明暗色块就是这么来的。
	return v_flt(Value);
}

FVoxelMaterial FVoxelPlanetNoiseTerrainInstance::GetMaterialImpl(v_flt X, v_flt Y, v_flt Z, int32 LOD, const FVoxelItemStack& Items) const
{
	const FVector Relative(
		double(X) - Center.X + PlanetCenterEpsilon,
		double(Y) - Center.Y + PlanetCenterEpsilon,
		double(Z) - Center.Z + PlanetCenterEpsilon);

	const FVector UnitDir = GetUnitDirection(Relative);
	const float Offset = ComputePlanetOffset(Params, UnitDir);

	FLinearColor Color;

	// 海平面判定带一点容差：正好等于海平面的体素算水面而不是陆地，否则海岸线
	// 上会出现一圈闪烁的沙色。
	if (Offset <= Params.SeaLevel + UE_KINDA_SMALL_NUMBER)
	{
		// 越往下越深，由浅水过渡到深水。
		const float Depth = FMath::Clamp(
			(Params.SeaLevel - Offset) / FMath::Max(Params.WaterDepthRange, 1e-4f), 0.f, 1.f);
		Color = MixColor(Params.ShallowWaterColor, Params.DeepWaterColor, Depth);
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

		// 极地雪：|Z| 越接近 1 越靠两极。叠在高度配色**之上**，所以高山和两极
		// 都会变白 —— 这正是真实行星的样子。
		const float Latitude = FMath::Abs(UnitDir.Z);
		if (Latitude > Params.PolarStart)
		{
			const float SnowBlend = FMath::Clamp(
				(Latitude - Params.PolarStart) / FMath::Max(1.f - Params.PolarStart, 1e-4f), 0.f, 1.f);
			Color = MixColor(Color, Params.SnowColor, SnowBlend);
		}
	}

	FPlanetGeologySample GeologySample;
	if (Geology.IsValid() && Offset > Params.SeaLevel + UE_KINDA_SMALL_NUMBER)
	{
		const double Distance = Relative.Size();
		const double Depth = double(Radius) - Distance;
		GeologySample = Geology->Sample(UnitDir, Depth);

		const FPlanetGeologyAppearance& Appearance = Geology->GetAppearance();
		// 用最强元素自身的强度判定显现：TotalNorm 会被「丰度和」稀释，
		// 铀/钛这类低丰度元素即使在自己的矿脉里也永远够不到阈值。
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

	return MakeVoxelMaterial(GeologySample, Color);
}

TVoxelRange<v_flt> FVoxelPlanetNoiseTerrainInstance::GetValueRangeImpl(const FVoxelIntBox& Bounds, int32 LOD, const FVoxelItemStack& Items) const
{
	// 最近/最远距离的算法与纯球模块逐字相同：密度仍然是「距离减去地表半径」，
	// 只是地表半径现在随方向变。
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

	// 位移的确定包络。它由 ComputePlanetOffset 的两条性质直接给出，不是估的：
	//   上界 1 —— 归一化除以了权重和，而每层 Perlin 都在 [-1, 1]。
	//   下界 SeaLevel —— 末尾那道 Max 就是它。
	//
	// 这个区间是保守的（真实 fbm 极少同时触到两端），但保守是**安全**的方向：
	// 估高了只是多算一些区块，估低了地表会出洞。想要更紧就调小 Amplitude。
	const double MaxOffset = 1.0;
	const double MinOffset = FMath::Min(double(Params.SeaLevel), MaxOffset);

	// 最小密度 ← 最近的距离 + 最大的地表半径；最大密度 ← 最远的距离 + 最小的。
	//
	// 与 GetValueImpl 一样不夹到 ±1：契约要求区间**覆盖**取值，而取值已不再
	// 夹断。下游把它换算成 FVoxelValue 时会自己饱和，只消费端点的符号，所以
	// 这里放宽不影响空/实区块的判定。
	// 地质增量只增大表面半径，因此只压缩最小密度那一侧。
	// 漏掉这一项会在矿脉处出洞，而且是静默的 —— RangeCoverage 测试兜底。
	const double MaxGeometryDelta =
		Geology.IsValid() ? double(Geology->GetMaxGeometryDelta()) : 0.0;

	const double MinValue =
		(MinDistance - double(Radius) - double(Params.Amplitude) * MaxOffset - MaxGeometryDelta)
		/ DensityScale;
	const double MaxValue =
		(MaxDistance - double(Radius) - double(Params.Amplitude) * MinOffset) / DensityScale;

	return TVoxelRange<v_flt>(v_flt(MinValue), v_flt(MaxValue));
}

FVector FVoxelPlanetNoiseTerrainInstance::GetUpVector(v_flt X, v_flt Y, v_flt Z) const
{
	// 地形是「球面沿径向的起伏」，所以「上」仍然是径向 —— 这一点与纯球模块
	// 完全相同，有山有谷并不改变重力方向。
	const FVector P(double(X) - Center.X, double(Y) - Center.Y, double(Z) - Center.Z);
	if (P.SizeSquared() < UE_DOUBLE_SMALL_NUMBER)
	{
		return FVector::UpVector;
	}
	return P.GetSafeNormal();
}
