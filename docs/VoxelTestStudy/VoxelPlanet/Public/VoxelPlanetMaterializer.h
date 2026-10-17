// 星球材质求值器 —— 按海拔高度分带着色。
//
// 与地形计算分离的理由：
//   地形决定「哪里高」，材质决定「什么颜色」，
//   两者是独立的关注点，可以各自调整而不互相影响。

#pragma once

#include "CoreMinimal.h"
#include "VoxelMinimal.h"
#include "VoxelMaterial.h"
#include "VoxelPlanetTypes.h"

/**
 * 星球材质求值器。
 *
 * 色带顺序（从低到高）：
 *   深水 → 岸滩 → 低地 → 高地 → 雪顶
 * 全部用 LerpUsingHSV 渐变并配合 Pow 做非线性过渡，避免出现色带。
 */
class VOXELPLANET_API FVoxelPlanetMaterializer
{
public:
	void Init(const FVoxelPlanetSeaSettings& InSea,
		const FVoxelPlanetLandPalette& InLand,
		const FVoxelPlanetShellSettings& InShell,
		const FVoxelPlanetContinentLayer& InContinent);

	/**
	 * 按表面海拔与半径的关系取颜色。
	 * SurfaceRadius 是该方向的实际地表半径。
	 */
	FLinearColor GetSurfaceColor(double SurfaceRadius, double BaseRadius) const;

	/** 中空壳层的内壁颜色。 */
	FLinearColor GetCoreColor() const { return Shell.CoreColor; }

	/** 陆地最大可能海拔，色带归一化用。 */
	double GetMaxLandHeight() const;

private:
	FVoxelPlanetSeaSettings Sea;
	FVoxelPlanetLandPalette Land;
	FVoxelPlanetShellSettings Shell;
	FVoxelPlanetContinentLayer Continent;
};
