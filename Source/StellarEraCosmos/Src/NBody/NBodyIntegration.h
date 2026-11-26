// NBodyIntegration.h —— N 体受力模型之外的积分器：位置/速度/姿态推进。

#pragma once

#include "CoreMinimal.h"

class FNBodySimulator;

namespace NBodyIntegration
{
	/**
	 * Velocity Verlet 一步：位置半步 → 算力 → 速度全步 + 位置半步 → 刚体姿态。
	 * `bPhysicsDriven` 的体跳过线速度/位置（位移归外部物理），姿态仍在这里积分。
	 * 运行时由子系统每子步调用；轨迹预测对副本调用。
	 */
	void StepVelocityVerlet(FNBodySimulator& Simulator, double Dt);
}
