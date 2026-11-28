#pragma once

#include "CoreMinimal.h"

class UStarBodyComponent;

/** 一颗照亮本行星的光源快照：通常来自 UStarBodyComponent；没有恒星时回退到关卡第一盏 ADirectionalLight（Star 空、Radiance 白）。 */
struct FPlanetSun
{
	/** 单位向量：行星 → 光源。 */
	FVector Direction = FVector::ForwardVector;

	/** 颜色 × 相对亮度。 */
	FLinearColor Radiance = FLinearColor::White;

	/** 回退太阳时为 nullptr。 */
	UStarBodyComponent* Star = nullptr;
};
