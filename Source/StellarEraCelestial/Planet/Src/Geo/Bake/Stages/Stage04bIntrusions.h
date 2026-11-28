// 阶段 4b：地下侵入体放置。只产出原语，几何与材质由快照与材质求值消费。
#pragma once
#include "CoreMinimal.h"
#include "PlanetBakeTypes.h"

namespace Stage04b
{
	/** 读 4a 的火山点表与阶段 2/3 的场，把六类岩体连同各自的蚀变晕追加到 Out。 */
	STELLARERACELESTIAL_API void PlaceIntrusions(const FPlanetRecipeConfig& Cfg, const FPlanetBakeFields& F, TArray<FGeoPrimitive>& Out);
}
