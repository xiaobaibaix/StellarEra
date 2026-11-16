// 阶段之间的数据总线：类型化结构体 + 位掩码声明，不用 FName 字典（拼错键要在编译期暴露）。
#pragma once
#include "CoreMinimal.h"
#include "GeoField2D.h"
#include "GeoMineral.h"
#include "GeoPlates.h"
#include "GeoPrimitive.h"
#include "GeoProfile.h"
#include "GeoRng.h"
#include "GeoVolcano.h"
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
	Relief      = 1 << 6,   // MountainMask / ReliefDelta / ReliefType
	Volcanoes   = 1 << 7,   // Volcanoes / VolcanoMask / VolcanicType / VolcanicAge / VolcanoActive / VolcanoDelta
	Climate     = 1 << 8,   // Temperature / Precipitation / WindDir / Biome / ClimateLand
	Hydro       = 1 << 9,   // FlowAccum / BasinMask / ErosionDepth / RiverMask
	Exposure    = 1 << 10,  // Primitives 里的 Exposure / RawDepthBefore（原地改写）
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
	/** 边界挤压速率（> 0 汇聚，< 0 离散，单位同板块速度）与所在板块的运动速率。 */
	FGeoField2D Convergence, PlateSpeed;
	FGeoField2D ContinentalMask, ContinentalGrad, CrustType, CrustThickness;
	FGeoField2D Elevation;
	/** 立体角加权的 E>0 面积占比。 */
	float LandAreaFrac = 0.f;
	/** 低于它算洋壳：取陆架底部，与 CrustType 的分界同源。 */
	float CrustSplitElevation = 0.f;
	float MinElevation = 0.f, MaxElevation = 0.f;

	// 阶段 3：宏观场同 MacroRes；Elevation 已被就地改成最终高程。
	FGeoField2D MountainMask, ReliefDelta, ReliefType;
	/** 侵蚀前 / 后地形增量的最大值：侵蚀只削不抬，前者是值域的保守依据。 */
	float ReliefRawMax = 0.f, ReliefDeltaMax = 0.f;

	// 阶段 4：点表进快照；宏观场（MacroRes）只给调试图层。Elevation 已叠上火山。
	TArray<FGeoVolcano> Volcanoes;
	FGeoField2D VolcanoMask, VolcanicType, VolcanicAge, VolcanoActive, VolcanoDelta;
	/** 4b 的蚀变分圈阈值，原样搬进快照：材质求值与烘焙参数必须同源。 */
	float AlterBands[3] = { 0.3f, 0.55f, 0.8f };

	// 阶段 5：气候场一律 ClimateRes；只给阶段 6/7 与调试图层，不进快照。
	FGeoField2D Temperature, Precipitation, WindDir, Biome, ClimateLand;

	// 阶段 6：水系场一律 HydroRes；FlowAccum 按全球（含海洋）面积×降水归一化，值恒小于 1，BasinMask 0 无 / 1 湖盆 / 2 内流盆地。
	FGeoField2D FlowAccum, BasinMask, ErosionDepth, RiverMask;
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
