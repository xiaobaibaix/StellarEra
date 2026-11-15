// 冻结产物：不可变、无 UObject，以 TSharedPtr<const> 交给体素线程。只放求值期真的要读的字段。
#pragma once
#include "CoreMinimal.h"
#include "GeoField2D.h"
#include "GeoMineral.h"
#include "GeoPrimitiveIndex.h"
#include "GeoProfile.h"
#include "GeoVolcano.h"

struct STELLARERAPLANET_API FPlanetSnapshot
{
	/** 海平面半径（体素），也是 M1 的地表。 */
	double R0 = 0.0;
	/** 密度值 = 距离差 / DensityScale；1 表示「1 体素对应 1 个单位」。 */
	float DensityScale = 1.f;
	FGeoProfile Radial;
	/** 莫霍面等界面的起伏，∈ [-1,1]，乘各层 WobbleRatio 得到半径比例。 */
	FGeoField2D MohoWobble;
	FGeoPrimitiveIndex Primitives;
	/** 地表高程（体素，相对 R0）；Res==0 表示阶段 2 未运行，地表仍是 R0。 */
	FGeoField2D Elevation;
	/** 低于它算洋壳岩性；取洋盆深度的一半，与烘焙期 CrustType 的分界一致。 */
	float CrustSplitElevation = -240.f;
	/** 地表半径的保守上下界，值域与早退都以它为准。 */
	double MinSurfaceRadius = 0.0;
	double MaxSurfaceRadius = 0.0;
	uint64 ContentHash = 0;
	/** 只在 bKeepDebugFields 时存在（编辑器调试图层用）：不进 ContentHash、不计入快照预算。 */
	TMap<FName, FGeoField2D> DebugFields;
	/** 阶段 4 火山点表：4b/4c 与运行期按它定位火口、矿柱。 */
	TArray<FGeoVolcano> Volcanoes;
	/** 撒矿器产出的覆层；矿体在 Primitives 里。 */
	TArray<FGeoMineralOverlay> MineralOverlays;
	/** 矿物调色板（编号 − 1 为下标），只供显示。 */
	TArray<FGeoMineralInfo> Minerals;

	FColor MineralColor(uint8 Id) const { return Minerals.IsValidIndex(int32(Id) - 1) ? Minerals[Id - 1].Color : FColor(255, 0, 255); }
	FString MineralName(uint8 Id) const
	{
		return Id == 0 ? FString(TEXT("无矿")) : (Minerals.IsValidIndex(int32(Id) - 1) ? Minerals[Id - 1].Name : FString::Printf(TEXT("矿物 %d"), Id));
	}

	void FinalizeHash();
	SIZE_T GetAllocatedSize() const;
	SIZE_T MineralOverlaysSize() const;
};
