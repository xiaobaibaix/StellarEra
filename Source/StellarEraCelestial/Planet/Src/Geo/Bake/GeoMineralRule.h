// 撒矿规则的烘焙侧接口：纯 POD 参数 + 无状态 Scatter，游戏线程从撒矿器资产拷出，后台线程运行。
// 新增一种成矿规律 = 写一个规则类 + 一个撒矿器子类，阶段、管线、查看器都不用改。
#pragma once
#include "CoreMinimal.h"
#include "GeoField2D.h"
#include "GeoMineral.h"
#include "GeoPrimitive.h"
#include "GeoRng.h"

struct FPlanetRecipeConfig;
struct FPlanetBakeFields;

struct FGeoScatterOutput
{
	TArray<FGeoPrimitive> Bodies;
	TArray<FGeoMineralOverlay> Overlays;
};

struct FGeoScatterContext
{
	const FPlanetRecipeConfig& Config;
	const FPlanetBakeFields& Fields;
	FGeoRng Rng;
	TArray<FString>& Errors;
};

class IGeoMineralRule
{
public:
	virtual ~IGeoMineralRule() = default;
	/** 在哪个阶段烘焙完之后运行：决定它能读到哪些场。 */
	virtual uint32 GetStage() const = 0;
	virtual const TCHAR* GetRuleName() const = 0;
	/** 产出的矿体 / 覆层不必填 MineralId，管线统一盖章。 */
	virtual void Scatter(FGeoScatterContext& Ctx, FGeoScatterOutput& Out) const = 0;
	virtual uint64 HashParams() const = 0;

	uint8 MineralId = 0;
	uint32 SeedOffset = 0;
	uint8 GradeMin = 120;
	uint8 GradeMax = 230;

	uint64 Hash() const
	{
		uint64 H = GeoHash::Combine(HashParams(), uint64(GetStage()));
		return GeoHash::Combine(H, uint64(MineralId) | (uint64(SeedOffset) << 8) | (uint64(GradeMin) << 40) | (uint64(GradeMax) << 48));
	}
	uint8 RandomGrade(FGeoRng& Rng) const
	{
		return uint8(FMath::Clamp(FMath::RoundToInt(Rng.Range(float(FMath::Min(GradeMin, GradeMax)), float(FMath::Max(GradeMin, GradeMax)))), 1, 255));
	}
};
