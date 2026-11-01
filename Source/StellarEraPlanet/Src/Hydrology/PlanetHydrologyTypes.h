// 水文域的纯数据：参数、湖泊、河流折线。没有 UObject，可跨线程，可被测试直接构造。

#pragma once

#include "CoreMinimal.h"

/**
 * 湖冠角半径里「探进岸里」的余量格数。
 *
 * PlanetHydrologyBake 在最大角距之上加这么多格，写进 FPlanetLakeBasin::AngularRadius；
 * PlanetWaterSurfaceBuilder::BuildLakes 再从 AngularRadius 里减掉同一个数，得到湖的
 * 真实范围。两处必须用同一个常量 —— 改一处会静默错裁余量（水面要么悬空，要么缺一圈）。
 */
constexpr int32 PlanetLakeCapMarginCells = 2;

/**
 * 水文烘焙的参数快照。
 *
 * 由地形模块的 GetWaterConfig 打包（见 VoxelPlanetNoiseTerrain），烘焙层不认识
 * 任何地形子类 —— 它只接受「高度回调 + 这份参数 + 半径」。
 */
struct FPlanetHydrologyParams
{
	/**
	 * 海平面（归一化，与地形模块的 SeaLevel 同源）。只在种子判据里用到。
	 *
	 * 必须与 `FPlanetNoiseParams::SeaLevel` / `UVoxelPlanetNoiseTerrain::SeaLevel`
	 * 的默认值一致。−0.35 是「地形削平阈值」时代的遗留值，当水位用只剩 0.01%
	 * 的连通海洋；0.0 在 DA_Terrain_Earth 的参数下约 45%。
	 */
	float SeaLevel = 0.f;

	/** 归一化位移 → 体素的换算系数（= 地形模块的 Amplitude）。 */
	float Amplitude = 8.f;

	/** 汇流量阈值（格）。越大河网越稀。 */
	float MinRiverFlow = 24.f;

	/** 河道最大下切（体素）。 */
	float MaxCarveDepth = 4.f;

	/** 河宽系数。 */
	float RiverWidthScale = 1.f;

	/** 每面格数。512 是默认；256 预览，1024 更细。 */
	int32 Resolution = 512;

	/**
	 * Priority-Flood 的 ε（体素）。
	 *
	 * 填洼时每往低处溢一层就把水位抬高 ε，湖面因此不是绝对平的一整片，
	 * 而是一条确定的、单调下降的「排水阶梯」—— 平地也能解出唯一流向。
	 *
	 * 取值必须大于 float32 在行星半径量级上的间距（半径 20000 体素时约 2e-3），
	 * 否则 ε 会被浮点吃掉。0.01 在半径 ≤ 65000 体素时安全。
	 */
	float Epsilon = 0.01f;
};

/** 一片湖泊的水面。 */
struct FPlanetLakeBasin
{
	/** 湖心方向（单位向量）。 */
	FVector CentroidDir = FVector::UpVector;

	/** 水面半径（体素，含 Radius）。 */
	float WaterLevelRadius = 0.f;

	/**
	 * 角半径（弧度）：湖面覆盖的球冠半角，= 最大角距 + `PlanetLakeCapMarginCells` 格。
	 *
	 * 那些格是探进岸里的余量：岸外地形高过水位时是安全的，低于水位时水面会悬空，
	 * 由水面几何按地形高度裁掉（PlanetWaterSurfaceBuilder::BuildLakes）。
	 */
	float AngularRadius = 0.f;
};

/** 河流折线上的一个采样点。 */
struct FPlanetRiverPoint
{
	/** 中轴方向（单位向量）。 */
	FVector Dir = FVector::UpVector;

	/** 该点的水面半径（体素）。沿折线单调不增。 */
	float WaterLevelRadius = 0.f;

	/** 河宽（体素）。 */
	float WidthVoxels = 1.f;
};

/** 一条从源头到汇入点的河流。 */
struct FPlanetRiverPolyline
{
	TArray<FPlanetRiverPoint> Points;
};

/**
 * 一颗行星的水体配置：水文参数 + 外观参数。
 *
 * 为什么放在一起：地形模块只暴露一个 GetWaterConfig，拆成两个虚函数会让
 * 「有水文参数但没有外观参数」这种半吊子状态在类型上合法。
 */
struct FPlanetWaterConfig
{
	/** 海平面、分辨率、汇流阈值、下切深度、ε。 */
	FPlanetHydrologyParams Params;

	/** 近岸的浅水色。 */
	FLinearColor ShallowColor = FLinearColor(0.055f, 0.22f, 0.28f);

	/** 深海色。 */
	FLinearColor DeepColor = FLinearColor(0.012f, 0.055f, 0.115f);

	/** 岸边泡沫色。 */
	FLinearColor FoamColor = FLinearColor(0.75f, 0.80f, 0.85f);

	/** 水深小于它开始泛泡沫（体素）。 */
	float FoamDepthVoxels = 0.35f;

	/** 水面的最大不透明度。1 = 完全遮住海底。 */
	float MaxOpacity = 0.85f;
};

/**
 * 精确比较，用于生成器的缓存键。
 *
 * 刻意不用 FMath::IsNearlyEqual：缓存键要的是「是不是同一组参数」，
 * 不是「视觉上是否等价」。近似相等会让「改了一点点海平面」被当成没改。
 */
inline bool operator==(const FPlanetHydrologyParams& A, const FPlanetHydrologyParams& B)
{
	return A.SeaLevel == B.SeaLevel
		&& A.Amplitude == B.Amplitude
		&& A.MinRiverFlow == B.MinRiverFlow
		&& A.MaxCarveDepth == B.MaxCarveDepth
		&& A.RiverWidthScale == B.RiverWidthScale
		&& A.Resolution == B.Resolution
		&& A.Epsilon == B.Epsilon;
}

inline bool operator!=(const FPlanetHydrologyParams& A, const FPlanetHydrologyParams& B)
{
	return !(A == B);
}
