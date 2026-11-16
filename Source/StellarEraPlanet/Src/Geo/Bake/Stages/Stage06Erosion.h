// 阶段 6：水系与剥蚀。本文件当前只含填洼、流向、内流盆地、汇流累积；刻谷与剥蚀深度见后续任务。
#pragma once
#include "CoreMinimal.h"
#include "PlanetStage.h"

class STELLARERAPLANET_API FStage06Erosion final : public IPlanetStage
{
public:
	virtual FName GetName() const override { return TEXT("Stage06Erosion"); }
	virtual uint32 GetStageIndex() const override { return 6; }
	virtual EPlanetField GetReads() const override { return EPlanetField::Elevation | EPlanetField::Relief | EPlanetField::Climate; }
	virtual EPlanetField GetWrites() const override { return EPlanetField::Hydro; }
	virtual void Bake(FPlanetBakeContext& Ctx) const override;
};

namespace Stage06
{
	/** 宏观水系网格：格子编号 (Face*Res+Y)*Res+X；Nbr 每格 8 个邻居编号（跨面已换算）。 */
	struct FHydroGrid
	{
		int32 Res = 0;
		TArray<int32> Nbr;        // 8 * N
		TArray<float> Area;       // 立体角权重，和为 1
		TArray<FVector3f> Dir;    // 格心方向
		int32 Num() const { return 6 * Res * Res; }
	};
	STELLARERAPLANET_API void BuildGrid(int32 Res, FHydroGrid& Out);

	/** 流向求解结果；只在烘焙期存在，测试直接读它来核对网络性质。 */
	struct FHydroFlow
	{
		TArray<double> Filled;         // 填洼后高程（体素）
		TArray<int32> Down;            // 下游格；海洋 / 内流盆地出口 / 无下游为 -1
		TArray<int32> DownPreCut;      // 切断内流出口之前的下游格，只给测试核对分类用
		TArray<uint8> Basin;           // 0 无 / 1 湖盆 / 2 内流盆地
		TArray<int32> BasinId;         // 洼地编号；不在洼地为 -1
		TArray<TArray<int32>> Exits;   // 每个洼地的溢出格（内流盆地的 Down 已被切断）
		TArray<float> BasinMeanPrecip; // 每个洼地集水区的面积加权平均降水
		TArray<double> Acc;            // 未归一化汇流：Σ 面积×降水
		double TotalAreaPrecip = 0.0;  // 全球 Σ 面积×降水
	};
	/** E 为宏观网格高程（体素），Precip 同网格 0–1。 */
	STELLARERAPLANET_API void SolveFlow(const FHydroGrid& Grid, const TArray<float>& E, const TArray<float>& Precip,
		float Radius, float FlatSlopeRatio, float EndorheicPrecip, FHydroFlow& Out);
}
