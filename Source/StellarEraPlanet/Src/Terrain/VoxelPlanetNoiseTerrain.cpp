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

	// ComputeRawPlanetOffset 的解析输出包络。写成常量而不是在两处手抄 ±1：
	// 这个值错了，地表会静默出洞（估小）或整颗星卡死（估大）。
	constexpr double MinNormalizedOffset = -1.0;
	constexpr double MaxNormalizedOffset = 1.0;

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

	// 除以权重和，把结果夹回 [-1, 1]。
	//
	// 这一步是**区间正确性的全部依据**：每层 Perlin 的输出都在 [-1, 1]，权重和
	// 恰为 AmplitudeSum，所以归一化后的值必然在范围内。实例的区间查询直接把
	// ±1 当作位移包络用，不再复核 —— 改这里必须同步改那里。
	//
	// 旧版本末尾还有一道 FMath::Max(Noise, SeaLevel)（海平面削平）。删掉它是
	// 水体功能的前提：海平面以下必须是真实起伏的海底，否则水面之下一片平。
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

	// 与密度场调用同一个 ComputeRawPlanetOffset 和同一个 SampleGeometryDelta。
	// 角色脚下的地面和眼睛看到的地面必须是同一个面。
	const FPlanetNoiseParams Params = MakeParams();
	const double BaseSurface =
		double(Context.Radius) + double(Params.Amplitude) * double(ComputeRawPlanetOffset(Params, Direction));

	// 河床也算地表：角色站在河里（视觉上没过脚踝），而不是浮在水面上 ——
	// 本期不做浮力。水面几何是独立的一张壳，不参与碰撞。
	double CarveDepth = 0.0;
	if (Context.Hydrology.IsValid())
	{
		CarveDepth = double(Context.Hydrology->Sample(Direction).CarveDepth);
	}

	double GeometryDelta = 0.0;
	if (Context.Geology.IsValid())
	{
		// 真实地表满足 R_s = Base − Carve + delta(R − R_s)。用不动点迭代一次，
		// 把 delta 自身的 1~2 体素偏移也收进来。
		//
		// 深度必须以**下切后**的地表为基准：矿脉与河道重叠时，漏掉 CarveDepth
		// 会让脚下与眼睛看到的地面差最多 MaxCarveDepth 个体素。
		const double Depth0 = double(Context.Radius) - (BaseSurface - CarveDepth);
		GeometryDelta = double(Context.Geology->SampleGeometryDelta(Direction, Depth0));
		GeometryDelta = double(Context.Geology->SampleGeometryDelta(
			Direction, Depth0 - GeometryDelta));
	}

	return float(BaseSurface - CarveDepth + GeometryDelta);
}

float UVoxelPlanetNoiseTerrain::GetRawOffset(const FVector& UnitDir) const
{
	const FVector Direction = UnitDir.GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		return 0.f;
	}

	// 每次调用都重建一次 Params（含 3 次 FRandomStream 取样）。512 分辨率下
	// 约 157 万次调用、每次 6 次乘法移位，总量在几十毫秒量级。
	//
	// **不要**为此加一个 mutable 缓存：烘焙在 ParallelFor 里并发调这个函数，
	// 缓存就是一个数据竞争。MakeParams() 每次返回新值反而是线程安全的根据。
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
	// 与 GetValueRangeImpl 完全相同的包络，只是在这里一次算好。
	// 下界 = Radius + Amplitude × (−1) − MaxCarveDepth：海平面削平去掉之后，
	// 位移下界从 SeaLevel 变成 −1（见 ComputeRawPlanetOffset 的注释），
	// 河床还要再往下挖。GeoDelta 只往外推，所以只影响上界。
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

	// ---- 内外早退：挖掘/建造的绝大部分体素落在这里 ----
	//
	// 地表只可能出现在 [MinSurfaceRadius, MaxSurfaceRadius] 这条径向带里，带外
	// 的符号是**确定**的：更深必然是实体，更高必然是空气。这两支只做一次比较
	// 就返回，跳过 Perlin（Octaves 层）和整张地质场（每个元素 × 每个生成器）。
	// 这一步之所以是最大的一笔：体素工具每碰到一个 32³ 叶子就把**整叶**当生成器
	// 采样一遍（FVoxelDataOctreeLeaf::InitForEdit → GetValues），而这条路径没有
	// 任何区间预判；一次 1280 体素的挖掘 ≈ 2×10⁹ 次调用。
	//
	// 返回值刻意取**保守界**而不是真值，两个要求同时满足：
	//   1) 落在 GetValueRangeImpl 报出的区间内（该函数未改，仍是 MinValue/MaxValue
	//      那两式）。实体支返回 (D - MaxSurface)，它是真值的下界且随 D 单调，
	//      带内任一点都落在 [MinValue, MaxValue] 里（推导见 .h 的成员注释）。
	//   2) 存储值**与精确计算完全相同**：两支的触发条件各自留了 DensityScale(5)
	//      的余量，所以真值在实体支必然 ≤ -1、空气支必然 ≥ +1；而 FVoxelValue
	//      是 int8 且按 ±1 饱和（VoxelValue.h），两者存进去都是 Full()/Empty()。
	//      因此这次优化对存下来的体素数据是零差异，不改变洞壁位置。
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

	// 不夹到 [-1, 1]。存储层（FVoxelValue）本来就会按 ±1 饱和，表面抽取只看
	// 符号，夹不夹对几何没有影响；但 LOD > 0 的着色法线是插件用**生成器浮点值**
	// 做中心差分算出来的，采样间距 = 2^LOD 体素（VoxelMesher.cpp 的 Step）。
	// 先夹到 ±1 的话，间距一旦明显超过 DensityScale（LOD 3 起），6 个采样就会
	// 全部饱和，法线只剩 ±1 分量，退化成 26 个轴向对角方向之一 —— 粗 LOD 的
	// 大块明暗色块就是这么来的。
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
		// 海床：浅处是沙，越深越接近 DeepWaterColor。
		//
		// **这里不再是蓝色水面本身** —— 水的蓝色、透明度与高光归水面材质
		// （见 Water/PlanetWaterMaterialBuilder）。地形只如实记录海底的颜色，
		// 于是透过半透明的水看到的是真的沙和真的岩，而不是又一层蓝。
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

	// 河床：湿岩 / 湿沙，比周围地面暗一档。只在陆地上做 —— 海底的河道已经
	// 被上面那段深度插值盖住了。
	if (Hydrology && Offset >= Params.SeaLevel)
	{
		const float RiverStrength = Hydrology->Sample(UnitDir).RiverStrength;
		if (RiverStrength > 0.f)
		{
			// 0.35 是「看得出是河」与「不像铺了柏油」之间的折中。
			// 只压暗 RGB，A 保持 1：顶点色与 cubemap 都会把 A 一起存下来，
			// 让河床顺带变成半透明会把「湿岩」画成一个能看穿的坑。
			FLinearColor RiverbedColor = Color * 0.35f;
			RiverbedColor.A = 1.f;
			Color = MixColor(Color, RiverbedColor, RiverStrength);
		}
	}

	// 矿色：**海底也叠**（本期改动）。海底是真实岩层，透过半透明的水看会自然
	// 混色 —— 旧逻辑「必须高于海平面」会让海床变成一片纯沙。
	if (Geology)
	{
		const double Depth = Radius - Distance;
		const FPlanetGeologySample GeologySample = Geology->Sample(UnitDir, Depth);

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

	// 位移的确定包络，由 ComputeRawPlanetOffset 的归一化直接给出（见上）。
	//
	// 这个区间是保守的（真实 fbm 极少同时触到两端），但保守是**安全**的方向：
	// 估高了只是多算一些区块，估低了地表会出洞。想要更紧就调小 Amplitude。
	const double MaxOffset = MaxNormalizedOffset;
	const double MinOffset = MinNormalizedOffset;

	// 最小密度 ← 最近的距离 − 最大的地表半径；最大密度 ← 最远的距离 − 最小的。
	//
	// 与 GetValueImpl 一样不夹到 ±1：契约要求区间**覆盖**取值，而取值已不再
	// 夹断。下游把它换算成 FVoxelValue 时会自己饱和，只消费端点的符号，所以
	// 这里放宽不影响空/实区块的判定。
	//
	// 最大地表半径 = Radius + Amplitude × MaxOffset + 最大地质增量（矿脉只外凸）；
	// 最小地表半径 = Radius + Amplitude × MinOffset − 最大下切量（河床只往下挖）。
	// 两项各自只压一侧，方向**不能写反** —— 写反的表现是河床出洞或冒出实体块，
	// 而且不会崩，只会静默出错。Terrain.EarlyOut 与 Terrain.HydrologyCarveRange
	// 两条用例兜底（后者在删掉 + MaxCarveDepth 时必须变红）。
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
	// 地形是「球面沿径向的起伏」，所以「上」仍然是径向 —— 这一点与纯球模块
	// 完全相同，有山有谷并不改变重力方向。
	const FVector P(double(X) - Center.X, double(Y) - Center.Y, double(Z) - Center.Z);
	if (P.SizeSquared() < UE_DOUBLE_SMALL_NUMBER)
	{
		return FVector::UpVector;
	}
	return P.GetSafeNormal();
}
