// 星球地形求值器 —— 实现。
//
// 这里是全部地形数学。四层噪声叠加成半径偏移：
//   ① 大陆：分形Perlin + 对比度  → 海陆分布
//   ② 山脉：只取正半 + 幂锐化 + 陆地偏置 → 山脉成链
//   ③ 细节：高频粗糙度
//   ④ 峡谷：脊状噪声反转 → 向下切割

#include "VoxelPlanetTerrain.h"
#include "FastNoise/VoxelFastNoise.inl"

void FVoxelPlanetTerrain::Init(const FVoxelPlanetContinentLayer& InContinent,
	const FVoxelPlanetMountainLayer& InMountain,
	const FVoxelPlanetDetailLayer& InDetail,
	const FVoxelPlanetRavineLayer& InRavine,
	const FVoxelPlanetCaveSettings& InCave,
	int32 Seed)
{
	Continent = InContinent;
	Mountain = InMountain;
	Detail = InDetail;
	Ravine = InRavine;
	Cave = InCave;
	CaveFrequency = Cave.Frequency;

	// 每层用不同的种子偏移，保证形态互不雷同
	ContinentNoise.SetSeed(Seed);
	MountainNoise.SetSeed(Seed + 1);
	DetailNoise.SetSeed(Seed + 2);
	RavineNoise.SetSeed(Seed + 3);
	CaveNoise.SetSeed(Seed + 4);

	ContinentNoise.SetFractalOctavesAndGain(FMath::Clamp(Continent.Octaves, 1, 8), 0.5f);
	MountainNoise.SetFractalOctavesAndGain(4, 0.5f);
	DetailNoise.SetFractalOctavesAndGain(3, 0.5f);
	RavineNoise.SetFractalOctavesAndGain(2, 0.5f);
	CaveNoise.SetFractalOctavesAndGain(2, 0.5f);
}

double FVoxelPlanetTerrain::GetRadiusOffset(double NX, double NY, double NZ) const
{
	// 输入是单位球面方向向量，输出是相对基准半径的偏移量
	double Offset = 0.0;

	//-----------------------------------------------------------------------
	// ① 大陆：决定海陆分布
	//-----------------------------------------------------------------------
	double ContinentValue = 0.0;
	if (Continent.bEnabled)
	{
		ContinentValue = ContinentNoise.GetPerlinFractal_3D(NX, NY, NZ, Continent.Frequency, Continent.Octaves);

		// 对比度：Perlin 噪声天然聚集在 0 附近，直接用会得到平缓丘陵。
		// 乘一个 >1 的系数把值推开，海洋才够深、陆地才够高。
		ContinentValue = FMath::Clamp(ContinentValue, -1.0, 1.0) * (1.0 + FMath::Max(Continent.Contrast, 0.0));
		ContinentValue = FMath::Clamp(ContinentValue, -1.0, 1.0);
	}
	Offset += ContinentValue * Continent.Height;

	//-----------------------------------------------------------------------
	// ② 山脉：只长在大陆高地
	//-----------------------------------------------------------------------
	if (Mountain.bEnabled)
	{
		const double MountainNoiseValue = MountainNoise.GetPerlinFractal_3D(NX, NY, NZ, Mountain.Frequency, 4);

		// 只取正半：山噪声不能挖出盆地
		double Mask = FMath::Max(MountainNoiseValue, 0.0);

		// 指数锐化：>1 的指数把圆钝丘陵变成狭窄尖峰
		Mask = FMath::Pow(Mask, FMath::Max(Mountain.Sharpness, 1.0));

		// 陆地偏置：真实山脉形成于板块边界，不长在汪洋中央。
		// 加这层约束后山脉会沿大陆边缘成链，而不是均匀铺满全球。
		const double LandFactor = FMath::Clamp(ContinentValue * 0.5 + 0.5, 0.0, 1.0);
		const double Bias = FMath::Lerp(1.0, LandFactor, FMath::Clamp(Mountain.LandBias, 0.0, 1.0));

		Offset += Mask * Bias * Mountain.Height;
	}

	//-----------------------------------------------------------------------
	// ③ 地表细节
	//-----------------------------------------------------------------------
	if (Detail.bEnabled)
	{
		Offset += DetailNoise.GetPerlin_3D(NX, NY, NZ, Detail.Frequency) * Detail.Height;
	}

	//-----------------------------------------------------------------------
	// ④ 峡谷：脊状噪声反转成向下切割
	//-----------------------------------------------------------------------
	// 1 - |n| 在噪声过零处产生尖锐正脊。反转后变成河谷，
	// 而不是在地面上到处长出墙来。
	if (Ravine.bEnabled)
	{
		const double RavineNoiseValue = RavineNoise.GetPerlinFractal_3D(NX, NY, NZ, Ravine.Frequency, 2);
		const double Ridge = 1.0 - FMath::Abs(RavineNoiseValue);

		// 只在最强的脊上切割，避免地表被切得七零八落
		const double Cut = FMath::Clamp((Ridge - 0.6) / 0.4, 0.0, 1.0);
		Offset -= Cut * Ravine.Depth;
	}

	return Offset;
}

double FVoxelPlanetTerrain::GetMaxRadiusOffset() const
{
	// 每一层都被自身振幅约束：
	//   · Continent 被两次 clamp 到 [-1, 1]
	//   · Mountain 是非负值的 clamp 后幂运算，≤ 1
	//   · Detail 是原始 Perlin，约 [-1, 1]
	//   · Ravine 只做减法
	// 相加即得保守上界。GetValueRangeImpl 靠它推导密度区间，算窄了星球会空洞。
	double Max = 0.0;

	if (Continent.bEnabled)
	{
		Max += Continent.Height;
	}
	if (Mountain.bEnabled)
	{
		Max += Mountain.Height;
	}
	if (Detail.bEnabled)
	{
		Max += Detail.Height;
	}
	if (Ravine.bEnabled)
	{
		Max += Ravine.Depth;
	}

	return Max;
}

double FVoxelPlanetTerrain::GetMaxLandHeight() const
{
	// 材质色带归一化用：陆地海拔相对基准半径的最大抬升量
	return GetMaxRadiusOffset();
}
