// TrajectoryPrediction —— 轨道线预测：复制一份求解器、清掉外力后纯引力推进。
// 不属于 N 体受力模型，归在 Trajectory/ 下。

#pragma once

#include "CoreMinimal.h"

class FNBodySimulator;

/**
 * 闭合轨道预测结果（相对主导天体坐标）。世界系里主导天体自己在动、绝对路径是摆线，
 * 只有相对坐标才闭合；还原绝对坐标 = RelativePositions + PrimaryPosition。
 */
struct FCosmosClosedOrbitPrediction
{
	/** 相对主导天体的位置（AU）。成功时首尾相接：Last() 与 [0] 逐位相等，闭合误差已沿整圈摊开。 */
	TArray<FVector3d> RelativePositions;

	/** 主导天体在 t=0 的位置（AU）。 */
	FVector3d PrimaryPosition = FVector3d::ZeroVector;

	/** 实测闭合时间（年），不是密切周期 —— 多体扰动下两者可差数倍。 */
	double PeriodYears = 0.0;

	/** 摊开前的闭合误差 ÷ 半长轴。调用方做滞回用。 */
	double ClosureErrorFraction = 0.0;
};

namespace TrajectoryPrediction
{
	/**
	 * 预测天体未来 Horizon 年的轨迹（AU），含首尾共 NumSamples 点，末点恰好落在 Horizon。
	 * 内部复制求解器再推进，不改原对象。索引无效 / Horizon ≤ 0 / NumSamples < 2 时返回 false。
	 */
	bool Predict(const FNBodySimulator& Simulator, int32 BodyIndex, double Horizon, int32 NumSamples,
	             TArray<FVector3d>& OutPositions);

	/**
	 * 预测天体相对主导天体（引力 M/r² 最大且更重的那颗）的闭合轨道（一整圈）。
	 * 密切周期只用来定步长，最多积 3 个周期，取扫满 2π 后离起点最近的点为闭合时刻。
	 * 非椭圆 / e ≥ 0.99 / 找不到主导天体 / 回到起点的距离超过 ReturnToleranceFraction × 半长轴
	 * 时返回 false。成功时闭合误差按时间线性摊到整圈，不堆在最后一段。
	 */
	bool PredictClosedOrbit(const FNBodySimulator& Simulator, int32 BodyIndex, int32 NumSamplesPerOrbit,
	                        FCosmosClosedOrbitPrediction& Out, double ReturnToleranceFraction = 0.3);
}
