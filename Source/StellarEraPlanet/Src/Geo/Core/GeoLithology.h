// 岩性 Id：1–8 按深度递增，9 起是地壳内的侵入体岩性（不分深度）。M2 换成目录资产后这里只留 None。
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
	// 侵入体岩性：只在地壳内有意义，深度剖面里不会出现。
	constexpr uint8 Granite = 9, Diorite = 10, Gabbro = 11, Peridotite = 12, Kimberlite = 13, LayeredMafic = 14;
	constexpr uint8 Carbonatite = 15;
	// 3A 沉积盖层：只出现在地表最上面的覆盖层里。
	constexpr uint8 Sandstone = 16, Shale = 17, Limestone = 18;
	// 3B 变质：石灰岩在变质级足够高时按大理岩出岩性。
	constexpr uint8 Marble = 19;
	constexpr uint8 Count = 20;

	inline const TCHAR* DisplayName(uint8 Id)
	{
		static const TCHAR* Names[Count] = { TEXT("无"), TEXT("地壳"), TEXT("上地幔"), TEXT("过渡带"),
			TEXT("下地幔"), TEXT("外核"), TEXT("内核"), TEXT("陆壳"), TEXT("洋壳"),
			TEXT("花岗岩"), TEXT("闪长岩"), TEXT("辉长岩"), TEXT("橄榄岩"), TEXT("金伯利岩"), TEXT("层状镁铁岩"),
			TEXT("碳酸岩"), TEXT("砂岩"), TEXT("页岩"), TEXT("石灰岩"), TEXT("大理岩") };
		return Id < Count ? Names[Id] : Names[0];
	}

	/** 调试色：M1 在测试星球上肉眼区分圈层用，不是最终美术。 */
	inline FColor DebugColor(uint8 Id)
	{
		static const FColor Colors[Count] = {
			FColor(255, 0, 255), FColor(150, 130, 110), FColor(70, 110, 60), FColor(60, 90, 120),
			FColor(110, 70, 110), FColor(230, 120, 30), FColor(250, 210, 80),
			// 陆壳 / 洋壳都是岩石，调试色与地壳相同；海陆差别只在地表着色上体现。
			FColor(150, 130, 110), FColor(150, 130, 110),
			FColor(190, 120, 140), FColor(150, 140, 150), FColor(90, 100, 110),
			FColor(60, 110, 80), FColor(110, 60, 130), FColor(70, 80, 90),
			FColor(200, 180, 210),
			FColor(220, 190, 130), FColor(110, 120, 100), FColor(210, 210, 200), FColor(235, 235, 240) };
		return Id < Count ? Colors[Id] : Colors[0];
	}
}
