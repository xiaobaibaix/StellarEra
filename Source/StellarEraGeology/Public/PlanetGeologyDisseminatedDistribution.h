// Copyright Voxel Plugin SAS. All Rights Reserved.
// 浸染 / 稀疏散布：大量小颗粒，单颗不成片，整体是"星星点点"。

#pragma once

#include "CoreMinimal.h"
#include "PlanetGeologyDistribution.h"
#include "PlanetGeologyDisseminatedDistribution.generated.h"

/**
 * 浸染状分布。
 *
 * 成本随 GrainCount 线性涨；每粒在观测视图里的角点数 ∝ 粒径²，
 * 所以想更密先加 GrainRadius，再加数量。
 */
UCLASS(DisplayName = "浸染（散布）", EditInlineNew)
class STELLARERAGEOLOGY_API UPlanetGeologyDisseminatedDistribution
	: public UPlanetGeologyDistribution
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Grains", meta = (ClampMin = "0", ClampMax = "8192"))
	int32 GrainCount = 512;

	/** 颗粒半径（占行星基准半径）。 */
	UPROPERTY(EditAnywhere, Category = "Grains", meta = (ClampMin = "0.0005", ClampMax = "0.02"))
	float GrainRadius = 0.002f;

	UPROPERTY(EditAnywhere, Category = "Grains", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RadiusJitter = 0.5f;

	UPROPERTY(EditAnywhere, Category = "Grains|Depth", meta = (ClampMin = "0.0", ClampMax = "0.9"))
	float DepthBias = 0.5f;

	UPROPERTY(EditAnywhere, Category = "Grains|Depth", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DepthCenter = 0.15f;

	UPROPERTY(EditAnywhere, Category = "Grains|Depth", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float DepthSpread = 0.08f;

	virtual TSharedPtr<const FPlanetGeologyDistributionRuntime> BuildRuntime(
		const FPlanetGeologyDistributionBuildContext& Context) const override;
};
