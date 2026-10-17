// 星球地形求值器 —— 纯计算，不依赖 Voxel 插件接口。
//
// 职责：给定单位球面上的方向，算出该方向的地表半径。
// 不碰 UVoxelGenerator / FVoxelGeneratorInstance 任何东西，
// 因此可以脱离引擎单独写单元测试。

#pragma once

#include "CoreMinimal.h"
#include "VoxelMinimal.h"
#include "FastNoise/VoxelFastNoise.h"
#include "VoxelPlanetTypes.h"

/**
 * 星球地形求值器。
 *
 * 所有噪声都在「单位球面」上采样（即传入的方向向量已归一化）。
 * 这是整个星球地形最关键的一点：直接用世界坐标会让噪声的有效频率
 * 随半径变化，导致两极涂抹、赤道拉伸。归一化后地表自然包裹球体、无缝。
 *
 * 线程安全：SetSeed 只在初始化阶段调用，之后所有查询函数都是 const 只读。
 */
class VOXELPLANET_API FVoxelPlanetTerrain
{
public:
	/** 用配置初始化五层噪声。必须在任何查询之前调用。 */
	void Init(const FVoxelPlanetContinentLayer& InContinent,
		const FVoxelPlanetMountainLayer& InMountain,
		const FVoxelPlanetDetailLayer& InDetail,
		const FVoxelPlanetRavineLayer& InRavine,
		const FVoxelPlanetCaveSettings& InCave,
		int32 Seed);

	/**
	 * 计算单位球面某个方向上的地表半径。
	 * NX/NY/NZ 必须已经归一化。返回 Radius 之外的偏移量，可正可负。
	 */
	double GetRadiusOffset(double NX, double NY, double NZ) const;

	/**
	 * 地表半径可能的最大偏移绝对值。
	 * GetValueRangeImpl 用它推导保守的密度区间 —— 这是星球唯一的性能命门。
	 */
	double GetMaxRadiusOffset() const;

	/** 洞穴的脊状密度场，返回值越大越接近空腔。仅在Amount > 0 时有意义。 */
	double GetCaveField(double NX, double NY, double NZ) const { return CaveNoise.GetPerlin_3D(NX, NY, NZ, CaveFrequency); }

	/** 各层配置只读引用，供材质等外部逻辑复用。 */
	const FVoxelPlanetContinentLayer& GetContinent() const { return Continent; }
	const FVoxelPlanetDetailLayer& GetDetail() const { return Detail; }

	/** 陆地最大可能海拔，用于材质色带归一化。 */
	double GetMaxLandHeight() const;

private:
	FVoxelPlanetContinentLayer Continent;
	FVoxelPlanetMountainLayer Mountain;
	FVoxelPlanetDetailLayer Detail;
	FVoxelPlanetRavineLayer Ravine;
	FVoxelPlanetCaveSettings Cave;

	float CaveFrequency = 3.f;

	// 每层用不同的种子偏移，保证形态互不雷同
	FVoxelFastNoise ContinentNoise;
	FVoxelFastNoise MountainNoise;
	FVoxelFastNoise DetailNoise;
	FVoxelFastNoise RavineNoise;
	FVoxelFastNoise CaveNoise;
};
