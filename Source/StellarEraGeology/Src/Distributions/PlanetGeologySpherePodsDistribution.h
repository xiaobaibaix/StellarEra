// Copyright Voxel Plugin SAS. All Rights Reserved.
// 囊状分布：最朴素的球团，旧矿囊模型的直接搬迁，也是旧资产的迁移目标。

#pragma once

#include "CoreMinimal.h"
#include "PlanetGeologyDistribution.h"
#include "PlanetGeologySpherePodsDistribution.generated.h"

/**
 * 球团分布。
 *
 * 参数名保留 Pod 前缀（spec §4.4「参数与语义完全沿用」）：迁移时逐字段照抄旧值，
 * 名字一改就又多一个漂移点。
 */
UCLASS(DisplayName = "囊状（球团）", EditInlineNew)
class STELLARERAGEOLOGY_API UPlanetGeologySpherePodsDistribution
	: public UPlanetGeologyDistribution
{
	GENERATED_BODY()

public:
	/** 撒多少个矿囊。0 = 这个生成器不贡献任何矿。 */
	UPROPERTY(EditAnywhere, Category = "Pods", meta = (ClampMin = "0", ClampMax = "512"))
	int32 PodCount = 16;

	/** 矿囊名义半径（占行星基准半径的比例）。重叠对数按 PodCount² × (2r)³ 增长。 */
	UPROPERTY(EditAnywhere, Category = "Pods", meta = (ClampMin = "0.001", ClampMax = "0.25"))
	float PodRadius = 0.05f;

	/** 每囊半径的随机抖动幅度（±比例）。 */
	UPROPERTY(EditAnywhere, Category = "Pods", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PodRadiusJitter = 0.5f;

	/** 深度偏好强度。0 = 全深度体积均匀。 */
	UPROPERTY(EditAnywhere, Category = "Pods|Depth", meta = (ClampMin = "0.0", ClampMax = "0.9"))
	float PodDepthBias = 0.0f;

	/** 偏好深度（0 = 地表，1 = 球心）。 */
	UPROPERTY(EditAnywhere, Category = "Pods|Depth", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PodDepthCenter = 0.25f;

	/** 偏好深度的高斯宽度。 */
	UPROPERTY(EditAnywhere, Category = "Pods|Depth", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float PodDepthSpread = 0.15f;

	virtual TSharedPtr<const FPlanetGeologyDistributionRuntime> BuildRuntime(
		const FPlanetGeologyDistributionBuildContext& Context) const override;

	/**
	 * 用旧元素字段直接造运行期，**不建 UObject**。
	 *
	 * 供 BuildFromPreset 的遗留兜底使用：种子由调用方按旧公式
	 * `Seed != 0 ? Seed : Hash(Id) & 0x7fffffff` 算好后传入。
	 * 这里是「旧行星形状逐位不变」的唯一实现，迁移与兜底都收敛到它。
	 */
	static TSharedPtr<const FPlanetGeologyDistributionRuntime> MakeRuntimeFromLegacyParams(
		int32 InPodCount, float InPodRadius, float InPodRadiusJitter,
		float InPodDepthBias, float InPodDepthCenter, float InPodDepthSpread,
		int32 InSeed, float InShapeDetail, int32 InOctaves, float InLacunarity,
		float InGain, float InVeinThreshold);
};
