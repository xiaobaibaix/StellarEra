// 水文场 → 水面几何（FDynamicMesh3，行星局部空间，单位 cm）。

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "PlanetHydrologyField.h"

/**
 * 水面几何构建器。三块几何水位来源不同：海洋是恒定海面球壳、湖泊每片一张球冠、
 * 河流沿折线条带。顶点 = 单位方向 × 水位半径(体素) × VoxelSize，即行星局部空间
 * 厘米。法线直接取径向单位方向 —— 球面片上它是精确解，比从三角形反算更准
 * （河流条带很窄，反算会放大误差）。
 */
namespace PlanetWaterSurfaceBuilder
{
	struct FBuildParams
	{
		/** 体素边长（cm）。行星局部空间与体素空间的比例。 */
		double VoxelSize = 100.0;

		/** 行星基准半径（体素）。把厘米宽度换算成球面角的依据。 */
		double PlanetRadiusVoxels = 1000.0;

		/**
		 * 海洋球壳每面的格数。96 在 1 km 半径上约 16 m/格；规格的 2–4 m 需要
		 * 2–8 亿三角形，当前预算下不可达。
		 */
		int32 OceanResolution = 96;

		/**
		 * 地形高度回调：返回归一化位移（−1..1，0 = 基准球面），与 GetRawOffset
		 * 同源。只有湖泊用得上：湖冠 2 格余量必须落在地形高过水位处，否则水面会
		 * 悬在岸外的空气里。为空 = 不裁剪。
		 */
		TFunction<float(const FVector&)> TerrainHeightFn;

		/** 归一化位移 → 体素的换算系数（= 地形模块的 Amplitude）。 */
		float TerrainAmplitude = 0.f;
	};

	/** 海洋：半径恒为海面的球壳，陆地内部整格跳过。 */
	STELLARERAPLANET_API void BuildOcean(
		const FPlanetHydrologyField& Field,
		const FBuildParams& Params,
		UE::Geometry::FDynamicMesh3& OutMesh);

	/** 湖泊：每片湖一张球冠，多余的边缘探进地形里（岸线由深度测试给出）。 */
	STELLARERAPLANET_API void BuildLakes(
		const FPlanetHydrologyField& Field,
		const FBuildParams& Params,
		UE::Geometry::FDynamicMesh3& OutMesh);

	/** 河流：沿每条折线的四边形条带。 */
	STELLARERAPLANET_API void BuildRivers(
		const FPlanetHydrologyField& Field,
		const FBuildParams& Params,
		UE::Geometry::FDynamicMesh3& OutMesh);
}
