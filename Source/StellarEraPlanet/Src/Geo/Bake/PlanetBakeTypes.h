// 阶段之间的数据总线：类型化结构体 + 位掩码声明，不用 FName 字典（拼错键要在编译期暴露）。
#pragma once
#include "CoreMinimal.h"
#include "GeoField2D.h"
#include "GeoMineral.h"
#include "GeoPlates.h"
#include "GeoPrimitive.h"
#include "GeoProfile.h"
#include "GeoRng.h"
#include "PlanetRecipeConfig.h"

enum class EPlanetField : uint32
{
	None        = 0,
	Radial      = 1 << 0,
	MohoWobble  = 1 << 1,
	Primitives  = 1 << 2,
	Plates      = 1 << 3,   // PlateId / BoundaryType / BoundaryDistance / CrustAge
	Continental = 1 << 4,   // ContinentalMask / ContinentalGrad / CrustType / CrustThickness
	Elevation   = 1 << 5,   // Elevation
};
ENUM_CLASS_FLAGS(EPlanetField);

struct FPlanetBakeFields
{
	FGeoProfile Radial;
	FGeoField2D MohoWobble;
	TArray<FGeoPrimitive> Primitives;
	TArray<FGeoMineralOverlay> MineralOverlays;

	// 阶段 2 的宏观场一律 MacroRes（= FaceRes/4）；只有 Elevation 是全分辨率，它进快照。
	FGeoPlateFrame PlateFrame;
	FGeoField2D PlateId, BoundaryType, BoundaryDistance, CrustAge;
	FGeoField2D ContinentalMask, ContinentalGrad, CrustType, CrustThickness;
	FGeoField2D Elevation;
	/** 立体角加权的 E>0 面积占比。 */
	float LandAreaFrac = 0.f;
	/** 低于它算洋壳：取陆架底部，与 CrustType 的分界同源。 */
	float CrustSplitElevation = 0.f;
	float MinElevation = 0.f, MaxElevation = 0.f;
};

struct FPlanetBakeReport
{
	TArray<FString> Errors;
	TArray<TPair<FName, double>> StageSeconds;
	bool HasErrors() const { return Errors.Num() > 0; }
};

struct FPlanetBakeContext
{
	const FPlanetRecipeConfig& Config;
	FPlanetBakeFields& Fields;
	/** 已按 (Seed, StageIndex) 派生；阶段内再按用途 Derive，不同用途互不干扰。 */
	FGeoRng Rng;
	FPlanetBakeReport& Report;
};
