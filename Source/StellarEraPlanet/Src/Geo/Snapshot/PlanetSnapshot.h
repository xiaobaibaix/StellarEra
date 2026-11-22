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
	/** 原语最深点 / R0。矿与岩体都只在地壳上部，调试图层据此跳过深处的 BVH 查询（不进哈希）。 */
	/** 1 = 未计算、不剔除：手工搭的快照没有走 FinalizeHash 时不能把数据静默藏掉。 */
	float PrimitiveDepthMaxRatio = 1.f;
	bool IsTooDeepForPrimitives(const FVector3d& P) const
	{
		return (R0 - P.Size()) / FMath::Max(R0, 1.0) > double(PrimitiveDepthMaxRatio) + 0.01;
	}
	/** 蚀变四圈的归一化距离内边界：钾化 / 绢英岩化 / 泥化，第四圈到晕边界。 */
	float AlterBands[3] = { 0.3f, 0.55f, 0.8f };
	/** 地表半径的保守上下界，值域与早退都以它为准。 */
	double MinSurfaceRadius = 0.0;
	double MaxSurfaceRadius = 0.0;
	uint64 ContentHash = 0;
	/** 本次烘焙实际跑过的阶段之外的所有阶段（含上游关闭连带跳过的）；查看器据此只列存在的图层。 */
	uint32 DisabledStages = 0;
	bool HasStage(uint32 Stage) const { return Stage == 0 || (DisabledStages & (1u << (Stage - 1))) == 0; }
	/** 只在 bKeepDebugFields 时存在（编辑器调试图层用）：不进 ContentHash、不计入快照预算。 */
	TMap<FName, FGeoField2D> DebugFields;
	/** 阶段 4 火山点表：4b/4c 与运行期按它定位火口、矿柱。 */
	TArray<FGeoVolcano> Volcanoes;
	/** 撒矿器产出的覆层；矿体在 Primitives 里。 */
	TArray<FGeoMineralOverlay> MineralOverlays;
	/** 矿物调色板（编号 − 1 为下标），只供显示。 */
	TArray<FGeoMineralInfo> Minerals;

	// 3A / 3B / 7 的宏观场（MacroRes）：材质求值与调试图层读，Res==0 表示该阶段没跑。
	FGeoField2D StrataThickness, StrataType;
	FGeoField2D FaultMask, FoldField, MetamorphicGrade;
	FGeoField2D WeatheringDepth, SoilType;
	/** 石灰岩转大理岩的变质级门槛：与烘焙参数同源，材质求值不读配置。 */
	float MarbleThreshold = 0.55f;

	FColor MineralColor(uint8 Id) const { return Minerals.IsValidIndex(int32(Id) - 1) ? Minerals[Id - 1].Color : FColor(255, 0, 255); }
	FString MineralName(uint8 Id) const
	{
		return Id == 0 ? FString(TEXT("无矿")) : (Minerals.IsValidIndex(int32(Id) - 1) ? Minerals[Id - 1].Name : FString::Printf(TEXT("矿物 %d"), Id));
	}

	void FinalizeHash();
	SIZE_T GetAllocatedSize() const;
	SIZE_T MineralOverlaysSize() const;
};
