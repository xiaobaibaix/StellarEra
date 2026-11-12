// M1 的岩性 Id，按深度递增编号；M2 换成目录资产后这里只留 None。
#pragma once
#include "CoreMinimal.h"

namespace GeoLithology
{
	constexpr uint8 None = 0;
	constexpr uint8 Crust = 1;
	constexpr uint8 UpperMantle = 2;
	constexpr uint8 Transition = 3;
	constexpr uint8 LowerMantle = 4;
	constexpr uint8 OuterCore = 5;
	constexpr uint8 InnerCore = 6;
	// 地壳按海平面分陆壳 / 洋壳：两套不同的成矿背景，深度比只看厚度分不出来。
	constexpr uint8 ContinentalCrust = 7;
	constexpr uint8 OceanicCrust = 8;
	constexpr uint8 Count = 9;

	/** 调试色：M1 在测试星球上肉眼区分圈层用，不是最终美术。 */
	inline FColor DebugColor(uint8 Id)
	{
		static const FColor Colors[Count] = {
			FColor(255, 0, 255), FColor(150, 130, 110), FColor(70, 110, 60), FColor(60, 90, 120),
			FColor(110, 70, 110), FColor(230, 120, 30), FColor(250, 210, 80),
			FColor(90, 120, 80), FColor(40, 70, 130) };
		return Id < Count ? Colors[Id] : Colors[0];
	}
}
