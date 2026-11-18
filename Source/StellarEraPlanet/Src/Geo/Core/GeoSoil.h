// 阶段 7 的土壤类型：风化壳按气候与母岩分档，撒矿规则与调试图层共用这张表。
#pragma once
#include "CoreMinimal.h"

namespace GeoSoil
{
	constexpr uint8 None = 0;        // 没有风化壳（沙漠 / 陡坡 / 冰盖）
	constexpr uint8 Laterite = 1;    // 红土：湿热，铝土矿与红土镍的母床
	constexpr uint8 Kaolin = 2;      // 高岭土：花岗岩在温湿条件下的风化产物
	constexpr uint8 Temperate = 3;   // 温带土壤：普通风化壳
	constexpr uint8 Arid = 4;        // 干旱壳：极薄，只留盐壳
	constexpr uint8 Count = 5;

	inline const TCHAR* DisplayName(uint8 Id)
	{
		static const TCHAR* Names[Count] = { TEXT("无风化壳"), TEXT("红土"), TEXT("高岭土"), TEXT("温带土壤"), TEXT("干旱壳") };
		return Id < Count ? Names[Id] : Names[0];
	}

	inline FColor DebugColor(uint8 Id)
	{
		static const FColor Colors[Count] = {
			FColor(70, 70, 70), FColor(170, 60, 30), FColor(220, 215, 200), FColor(90, 130, 70), FColor(200, 180, 120) };
		return Id < Count ? Colors[Id] : Colors[0];
	}
}
