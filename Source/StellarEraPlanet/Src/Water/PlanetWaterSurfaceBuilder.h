// 水文场 → 水面几何（FDynamicMesh3，行星局部空间，单位 cm）。

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "PlanetHydrologyField.h"

/**
 * 水面几何构建器。
 *
 * 三块几何各自独立，因为它们的水位来源不同：
 *   海洋 —— 一张恒定海面的球壳，按「这一格有没有海」裁掉陆地下面的部分
 *   湖泊 —— 每片湖一张球面冠，水位取该湖的 WaterLevelRadius
 *   河流 —— 沿折线的条带，水位逐点取自折线
 *
 * 顶点位置 = 单位方向 × 水位半径(体素) × VoxelSize，即**行星局部空间的厘米**。
 * 调用方（UPlanetWaterComponent）把网格交给 UDynamicMeshComponent，组件挂在
 * 行星 Actor 上，于是水面天然跟着星球走，不需要任何同步代码。
 *
 * 法线直接取径向单位方向：水面就是球面片，径向法线是精确解，比从三角形反算
 * 又快又准（而且河流条带很窄，三角形反算会放大误差）。
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
		 * 海洋球壳每面的格数。格弧长 ≈ (π/2 × 行星半径) / OceanResolution，
		 * 96 在 1 km 半径上约 16 m。规格 §6.1 的 2–4 m 在 10 km 行星上需要
		 * 每边约 4×10³–8×10³ 格（全星约 2–8 亿三角形），当前预算下不可达；保持 96。
		 */
		int32 OceanResolution = 96;

		/**
		 * 地形高度回调：返回**归一化**位移（−1..1，0 = 基准球面）。
		 *
		 * 与 `UVoxelPlanetTerrainModule::GetRawOffset` 同源，水文烘焙用的也是它。
		 * 只有湖泊用得上：湖冠的 2 格余量必须落在地形高过水位的地方（水面埋进
		 * 岸里），否则那圈水面会悬在岸外的空气里。为空 = 不裁剪（旧行为）。
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
