// 水文域的纯数据：参数、湖泊、河流折线。没有 UObject，可跨线程，可被测试直接构造。

#pragma once

#include "CoreMinimal.h"

/**
 * 水文烘焙的参数快照。
 *
 * 由地形模块的 GetWaterConfig 打包（见 VoxelPlanetNoiseTerrain），烘焙层不认识
 * 任何地形子类 —— 它只接受「高度回调 + 这份参数 + 半径」。
 */
struct FPlanetHydrologyParams
{
	/** 海平面（归一化，与地形模块的 SeaLevel 同源）。只在种子判据里用到。 */
	float SeaLevel = -0.35f;

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

	/** 角半径（弧度）：湖面覆盖的球冠半角，已含 1 格余量。 */
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
