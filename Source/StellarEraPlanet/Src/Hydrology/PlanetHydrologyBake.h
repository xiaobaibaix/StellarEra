// 水文烘焙：归一化高度场 → 填洼 → 汇流 → 下切 → 湖泊与河流特征。

#pragma once

#include "CoreMinimal.h"
#include "PlanetHydrologyField.h"
#include "PlanetHydrologyTypes.h"

/**
 * 水文烘焙的唯一入口。刻意做成自由函数而不是 UObject：只做一次纯计算，输入是
 * 回调 + 参数 + 半径，不认识任何地形子类，可在后台线程或测试里直接调用。
 */
namespace FPlanetHydrologyBake
{
	/** 返回**归一化**位移（−1..1，0 = 基准球面）。与地形模块的 GetRawOffset 同源。 */
	using FHeightFn = TFunctionRef<float(const FVector& UnitDir)>;

	/**
	 * @param HeightFn 每格调用一次（并行），必须线程安全且无副作用
	 * @param Radius   基准球面半径（体素）
	 * @return 场；参数非法（Resolution < 4）时返回 nullptr
	 */
	STELLARERAPLANET_API TSharedPtr<FPlanetHydrologyField> Bake(
		FHeightFn HeightFn,
		const FPlanetHydrologyParams& Params,
		double Radius);
}
