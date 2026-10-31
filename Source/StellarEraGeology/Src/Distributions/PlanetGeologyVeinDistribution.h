// Copyright Voxel Plugin SAS. All Rights Reserved.
// 脉状分布：折线骨架 + 胶囊，穿切地层。

#pragma once

#include "CoreMinimal.h"
#include "PlanetGeologyDistribution.h"
#include "PlanetGeologyVeinDistribution.generated.h"

/**
 * 脉状 / 折线骨架。
 *
 * 一条脉按 `SegmentLengthRatio × 半径` 拆成短段，每段一个胶囊。拆段是刻意的：
 * 单段包围半径因此稳定在 (0.5 × 段长 + r) 量级，观测视图的稀疏网格格边不会被
 * 一条长脉拉大（spec §3.2）。
 */
UCLASS(DisplayName = "脉状（穿切）", EditInlineNew)
class STELLARERAGEOLOGY_API UPlanetGeologyVeinDistribution : public UPlanetGeologyDistribution
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Veins", meta = (ClampMin = "0", ClampMax = "64"))
	int32 VeinCount = 4;

	/** 单条总长度（占行星基准半径）。这是**目标**：起点越浅、方向越朝外，实际越短。 */
	UPROPERTY(EditAnywhere, Category = "Veins", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float VeinLength = 0.35f;

	/** 管半径。 */
	UPROPERTY(EditAnywhere, Category = "Veins", meta = (ClampMin = "0.002", ClampMax = "0.05"))
	float VeinRadius = 0.008f;

	/** 逐段半径抖动。 */
	UPROPERTY(EditAnywhere, Category = "Veins", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RadiusJitter = 0.4f;

	/** 段长 / 管半径。越大段越少、弯曲越粗放。 */
	UPROPERTY(EditAnywhere, Category = "Veins", meta = (ClampMin = "1.0", ClampMax = "4.0"))
	float SegmentLengthRatio = 2.5f;

	/** 每段方向偏折（度）。 */
	UPROPERTY(EditAnywhere, Category = "Veins", meta = (ClampMin = "0.0", ClampMax = "45.0"))
	float DirectionJitterDeg = 8.0f;

	/** 每条主脉的分叉数。分叉长度 = 主脉的一半。 */
	UPROPERTY(EditAnywhere, Category = "Veins", meta = (ClampMin = "0", ClampMax = "8"))
	int32 BranchCount = 1;

	/** 起点深度偏好强度。 */
	UPROPERTY(EditAnywhere, Category = "Veins|Depth", meta = (ClampMin = "0.0", ClampMax = "0.9"))
	float DepthBias = 0.5f;

	/** 起点偏好深度（0 = 地表，1 = 球心）。 */
	UPROPERTY(EditAnywhere, Category = "Veins|Depth", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DepthCenter = 0.4f;

	UPROPERTY(EditAnywhere, Category = "Veins|Depth", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float DepthSpread = 0.12f;

	virtual TSharedPtr<const FPlanetGeologyDistributionRuntime> BuildRuntime(
		const FPlanetGeologyDistributionBuildContext& Context) const override;
};
