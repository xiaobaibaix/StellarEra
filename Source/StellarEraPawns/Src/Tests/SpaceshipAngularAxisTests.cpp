// Copyright Voxel Plugin SAS. All Rights Reserved.
//
// 锁定「机体系角速度 → 世界系角加速度」的轴符号与旧路径一致。
// 旧路径是 AddActorLocalRotation(FRotator(Pitch, Yaw, Roll))，即 q ← q ⊗ FRotator(...).Quaternion()；
// 新路径是 q ← exp(½·ω_world·dt) ⊗ q 且 ω_world = q.RotateVector(ω_local)。
//
// ⚠ UE 的 FRotator 转四元数是绕 −X(Roll)、−Y(Pitch)、+Z(Yaw)：用标准轴直接积分会翻掉俯仰与横滚。
// 角度必须给小 30°：小角度下反向也只差 cos(θ)≈1，测不出符号。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

namespace
{
	FQuat OldPathRotate(const FQuat& Body, const FRotator& Delta)
	{
		return (Body * Delta.Quaternion()).GetNormalized();
	}

	/** 世界系左乘：ω_world = Body·ω_local，ω_local 已按四元数标准轴给出。 */
	FQuat NewPathRotate(const FQuat& Body, const FVector& LocalAxis, float AngleRad)
	{
		return (FQuat(Body.RotateVector(LocalAxis), AngleRad) * Body).GetNormalized();
	}

	float QuatDot(const FQuat& A, const FQuat& B)
	{
		return float(A.X * B.X + A.Y * B.Y + A.Z * B.Z + A.W * B.W);
	}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraSpaceshipAngularAxisConventionTest,
	"StellarEra.Spaceship.AngularAxisConvention",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraSpaceshipAngularAxisConventionTest::RunTest(const FString& Parameters)
{
	// 非平凡朝向：三条轴都不能靠「恰好对齐世界轴」蒙对。
	const FQuat Body = FRotator(20.f, 35.f, -10.f).Quaternion();
	const float AngleDeg = 30.f;
	const float AngleRad = FMath::DegreesToRadians(AngleDeg);

	struct FAxis
	{
		const TCHAR* Name;
		FRotator FrDelta;
		FVector QuatLocalAxis;
	};

	const FAxis Axes[] = {
		{ TEXT("横滚 Roll"), FRotator(0.f, 0.f, AngleDeg), FVector(-1.0, 0.0, 0.0) },
		{ TEXT("俯仰 Pitch"), FRotator(AngleDeg, 0.f, 0.f), FVector(0.0, -1.0, 0.0) },
		{ TEXT("偏航 Yaw"), FRotator(0.f, AngleDeg, 0.f), FVector(0.0, 0.0, 1.0) },
	};

	for (const FAxis& Axis : Axes)
	{
		const FQuat Old = OldPathRotate(Body, Axis.FrDelta);
		const FQuat Right = NewPathRotate(Body, Axis.QuatLocalAxis, AngleRad);
		const FQuat Wrong = NewPathRotate(Body, -Axis.QuatLocalAxis, AngleRad);

		const float DotRight = QuatDot(Old, Right);
		const float DotWrong = QuatDot(Old, Wrong);

		AddInfo(FString::Printf(TEXT("%s：正轴 dot=%.6f，反轴 dot=%.6f"),
			Axis.Name, DotRight, DotWrong));

		TestTrue(FString::Printf(TEXT("%s 与旧路径同向"), Axis.Name), DotRight > 0.9999f);
		// 判据自身必须能分辨反向：30° 下反向只有 cos30°≈0.866。
		TestTrue(FString::Printf(TEXT("%s 反轴必须明显不同（防判据失效）"), Axis.Name),
			DotWrong < 0.99f);
	}

	return true;
}
