// NBodyIntegration.cpp —— 见头文件。

#include "NBodyIntegration.h"
#include "NBodySimulator.h"

namespace NBodyIntegration
{
	void StepVelocityVerlet(FNBodySimulator& Simulator, double Dt)
	{
		const int32 N = Simulator.GetNumBodies();
		if (N == 0 || Dt <= 0.0)
		{
			return;
		}

		// 位置半步。
		for (int32 i = 0; i < N; ++i)
		{
			FCosmosBodyState& Body = Simulator.GetBodyMutable(i);
			// 物理驱动的体位置归 Chaos；这里只保留它的引力贡献（ComputeAccelerations 照跑）。
			if (Body.bPhysicsDriven)
			{
				continue;
			}
			Body.Position += Body.Velocity * (Dt * 0.5);
		}

		Simulator.ComputeAccelerations();

		// 速度全步 + 位置半步。
		for (int32 i = 0; i < N; ++i)
		{
			FCosmosBodyState& Body = Simulator.GetBodyMutable(i);
			if (Body.bPhysicsDriven)
			{
				continue;
			}
			Body.Velocity += Body.Acceleration * Dt;
			Body.Position += Body.Velocity * (Dt * 0.5);
		}

		// 姿态：ω += α·dt，ω 乘角阻尼，q ← exp(½·ω·dt)⊗q。
		// 角速度用显式欧拉：线速度上 Verlet 是因为有引力这个平滑加速度场，
		// 角加速度只有外力矩，且一帧最多 200 个子步，一阶足够。
		for (int32 i = 0; i < N; ++i)
		{
			FCosmosBodyState& Body = Simulator.GetBodyMutable(i);
			if (!Body.bRigidBody)
			{
				continue;
			}

			Body.AngularVelocity += Body.ExternalAngularAcceleration * Dt;

			// 乘法形式，k·dt ≥ 1 时直接归零。
			if (Body.AngularDamping > 0.0)
			{
				const double Decay = 1.0 - Body.AngularDamping * Dt;
				Body.AngularVelocity *= Decay > 0.0 ? Decay : 0.0;
			}

			// 每步归一化，否则几千步后 Actor 的变换矩阵会慢慢非正交，表现为缓慢缩放。
			const double Speed = Body.AngularVelocity.Size();
			if (Speed > 0.0)
			{
				const FVector3d Axis = Body.AngularVelocity / Speed;
				const FQuat4d Delta(Axis, Speed * Dt);
				Body.Orientation = (Delta * Body.Orientation).GetNormalized();
			}
		}

		Simulator.AdvanceSimulatedTime(Dt);
	}
}
