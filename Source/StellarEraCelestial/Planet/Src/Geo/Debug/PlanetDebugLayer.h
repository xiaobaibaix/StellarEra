// 地质调试图层描述：各阶段自己声明，查看器 / 星球叠加 / 剖切 / 导出只消费这张表，加阶段不改查看器。
#pragma once
#include "CoreMinimal.h"
#include <limits>

struct FPlanetSnapshot;

/** 地表图层按方向取值（在地表点采样）；地下图层按三维位置取值（剖面、剖切面用）。 */
enum class EGeoDebugDomain : uint8 { Surface, Volume };
/** 分类图层的值是整数编号，按图例上色；连续图层按色带或自定义函数上色。 */
enum class EGeoDebugKind : uint8 { Categorical, Continuous };

struct FGeoDebugLegendEntry
{
	FString Label;
	FColor Color;
};

struct STELLARERACELESTIAL_API FGeoDebugLayer
{
	FName Id;
	FString DisplayName;
	uint32 Stage = 0;
	EGeoDebugDomain Domain = EGeoDebugDomain::Surface;
	EGeoDebugKind Kind = EGeoDebugKind::Continuous;
	/** 连续图层的色带范围；Range 非空时以它为准（值域随星球变的图层用）。 */
	float Min = 0.f, Max = 1.f;
	FString Unit;
	/** 分类图层：下标即取值。 */
	TArray<FGeoDebugLegendEntry> Legend;
	/** 位置单位是体素、球心为原点。数据缺失返回 NoData()，不许崩；会被多线程同时调用。 */
	TFunction<float(const FPlanetSnapshot&, const FVector3d&)> Sample;
	TFunction<FColor(const FPlanetSnapshot&, float)> Colorize;
	TFunction<FVector2f(const FPlanetSnapshot&)> Range;
	/** 分类随星球变的图层（矿物）用它代替 Legend。 */
	TFunction<TArray<FGeoDebugLegendEntry>(const FPlanetSnapshot&)> DynamicLegend;
	/** 3D 视图里默认隐藏的分类（例如「无矿」，隐藏后只剩矿体）。 */
	TArray<int32> DefaultHidden;
	/** 薄小目标（矿体）光线步进会漏：给出光线 O + tD 进入它们的 (t, 值)，3D 视图解析叠加。 */
	TFunction<void(const FPlanetSnapshot&, const FVector3d&, const FVector3d&, TArray<TPair<double, float>>&)> RayHits;

	FVector2f GetRange(const FPlanetSnapshot& S) const;
	FColor ColorOf(const FPlanetSnapshot& S, float V) const;
	TArray<FGeoDebugLegendEntry> GetLegend(const FPlanetSnapshot& S) const;
	FString FormatValue(const FPlanetSnapshot& S, float V) const;

	static float NoData() { return std::numeric_limits<float>::quiet_NaN(); }
	static FColor NoDataColor() { return FColor(60, 60, 60, 255); }
	/** 默认连续色带（深紫 → 蓝 → 青 → 绿 → 黄），T ∈ [0,1]。 */
	static FColor Ramp(float T);
};
