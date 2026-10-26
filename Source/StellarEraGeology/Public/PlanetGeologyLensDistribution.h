// Copyright Voxel Plugin SAS. All Rights Reserved.
// 片层 / 透镜分布：沿球面切平面铺开的薄椭球，角度上成片、厚度上成层。

#pragma once

#include "CoreMinimal.h"
#include "PlanetGeologyDistribution.h"
#include "PlanetGeologyLensDistribution.generated.h"

/**
 * 片层 / 透镜。
 *
 * 一片 = 一个有向椭球（三轴半长 A / B / C，C 是径向半厚）。宽而薄是刻意的：
 * A / C 可达 20 倍以上。基元尺度一致性（spec §3.2）约束的是**片与片之间**，
 * 不是单片的长宽比。
 */
UCLASS(DisplayName = "片层（透镜）", EditInlineNew)
class STELLARERAGEOLOGY_API UPlanetGeologyLensDistribution : public UPlanetGeologyDistribution
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Seams", meta = (ClampMin = "0", ClampMax = "256"))
	int32 SeamCount = 24;

	/** 长轴半径（占行星基准半径）。 */
	UPROPERTY(EditAnywhere, Category = "Seams", meta = (ClampMin = "0.01", ClampMax = "0.5"))
	float SeamLength = 0.12f;

	/** 宽轴半径。 */
	UPROPERTY(EditAnywhere, Category = "Seams", meta = (ClampMin = "0.01", ClampMax = "0.5"))
	float SeamWidth = 0.06f;

	/** 径向半厚。观测视图角点与它成平方反比：减半角点涨 4 倍。 */
	UPROPERTY(EditAnywhere, Category = "Seams", meta = (ClampMin = "0.0005", ClampMax = "0.05"))
	float SeamThickness = 0.008f;

	/** 三轴各自的抖动幅度（±比例）。 */
	UPROPERTY(EditAnywhere, Category = "Seams", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SizeJitter = 0.35f;

	/** 深度偏好强度。0 = 全深度体积均匀。 */
	UPROPERTY(EditAnywhere, Category = "Seams|Depth", meta = (ClampMin = "0.0", ClampMax = "0.9"))
	float DepthBias = 0.6f;

	/** 偏好深度（0 = 地表，1 = 球心）。 */
	UPROPERTY(EditAnywhere, Category = "Seams|Depth", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DepthCenter = 0.12f;

	UPROPERTY(EditAnywhere, Category = "Seams|Depth", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float DepthSpread = 0.06f;

	/** 相对球面切平面的随机倾斜（度）。 */
	UPROPERTY(EditAnywhere, Category = "Seams", meta = (ClampMin = "0.0", ClampMax = "45.0"))
	float TiltJitterDeg = 5.0f;

	/** 径向域扭曲强度，单位 = 厚度。0 = 关闭（默认）。 */
	UPROPERTY(EditAnywhere, Category = "Seams|Warp", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float WarpAmplitude = 0.0f;

	/** 扭曲噪声频率，单位 = 厚度的倒数。 */
	UPROPERTY(EditAnywhere, Category = "Seams|Warp", meta = (ClampMin = "0.5", ClampMax = "8.0"))
	float WarpScale = 2.0f;

	virtual TSharedPtr<const FPlanetGeologyDistributionRuntime> BuildRuntime(
		const FPlanetGeologyDistributionBuildContext& Context) const override;
};
