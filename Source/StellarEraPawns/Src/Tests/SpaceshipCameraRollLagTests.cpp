// 相机横滚的三条契约。
// 一是追的速率：追得太慢就是「滚转开头画面跟不上、松手反甩」，上界由 MaxRollRate /
// CameraRollFollowSpeed 决定，这里按实际 CDO 参数数值积分。
// 二是施加方式：横滚要把**整条机位**（视线 + 相机位置）一起绕机身 X 轴拧，只拧视线会让
// 飞船在屏幕上绕圈 —— 相机位置仍由摇臂带着公转，视线却不跟。
// 三是口径：横滚增量必须走四元数 twist，欧拉 Roll 在俯仰越过 ±90° 时会跳 180°。

#include "CoreMinimal.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Misc/AutomationTest.h"
#include "SpaceshipPawn.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	UClass* LoadShipClass()
	{
		return LoadClass<ASpaceshipPawn>(
			nullptr, TEXT("/Game/Code/Pawns/BP_SpaceshipPawn.BP_SpaceshipPawn_C"));
	}

	float ViewAngleDeg(const FVector& A, const FVector& B)
	{
		return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(A | B, -1.f, 1.f)));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraSpaceshipCameraRollLagTest,
	"StellarEra.Spaceship.CameraRollLag",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraSpaceshipCameraRollLagTest::RunTest(const FString& Parameters)
{
	UClass* ShipClass = LoadShipClass();
	if (!TestNotNull(TEXT("BP_SpaceshipPawn_C 可加载"), ShipClass))
	{
		return false;
	}

	const ASpaceshipPawn* CDO = Cast<ASpaceshipPawn>(ShipClass->GetDefaultObject());
	if (!TestNotNull(TEXT("BP_SpaceshipPawn CDO"), CDO))
	{
		return false;
	}

	const auto GetFloat = [CDO](const TCHAR* Name) -> float
	{
		const FFloatProperty* Prop = FindFProperty<FFloatProperty>(CDO->GetClass(), Name);
		return Prop ? Prop->GetPropertyValue_InContainer(CDO) : -1.f;
	};

	const float MaxRollRate = GetFloat(TEXT("MaxRollRate"));
	const float FollowSpeed = GetFloat(TEXT("CameraRollFollowSpeed"));
	if (!TestTrue(TEXT("MaxRollRate / CameraRollFollowSpeed 可读"),
		MaxRollRate > 0.f && FollowSpeed > 0.f))
	{
		return false;
	}

	// 持续压杆 1 秒后的稳态滞后：机身滚得最狠时画面欠了多少度。滞后为负 = 视线落后机身。
	const float Dt = 1.f / 60.f;
	const float FullRateStep = MaxRollRate * Dt;
	float Lag = 0.f;
	for (int32 i = 0; i < 60; ++i)
	{
		Lag = ASpaceshipPawn::StepCameraRollLagDegrees(Lag, FullRateStep, FollowSpeed, Dt);
	}
	TestTrue(FString::Printf(TEXT("满滚转稳态滞后 %.2f° 不超过 5°"), FMath::Abs(Lag)),
		FMath::Abs(Lag) <= 5.f);
	TestTrue(TEXT("滞后方向为负（视线落后机身）"), Lag < 0.f);

	// 松手后滞后归零：超过 ~0.25 秒就是能看见的「反甩」。
	int32 FramesToSettle = 0;
	for (; FramesToSettle < 120 && FMath::Abs(Lag) > 0.5f; ++FramesToSettle)
	{
		Lag = ASpaceshipPawn::StepCameraRollLagDegrees(Lag, 0.f, FollowSpeed, Dt);
	}
	TestTrue(FString::Printf(TEXT("松手后 %.3f s 归零，不超过 0.25 s"), FramesToSettle * Dt),
		FramesToSettle * Dt <= 0.25f);

	// 符号契约：滞后必须让视线**欠**一截，而不是冲过头。机身滚 10°、滞后取该帧值 → 机位只转了 10°+滞后。
	Lag = ASpaceshipPawn::StepCameraRollLagDegrees(0.f, 10.f, FollowSpeed, Dt);
	TestTrue(TEXT("滞后在 0..-10 之间"), Lag > -10.f && Lag < 0.f);
	const FQuat BaseRotation = FQuat::Identity;
	const FQuat ViewAtZero = ASpaceshipPawn::BuildCameraViewRotation(FQuat::Identity, BaseRotation, 0.f);
	const FQuat ViewAtTen = ASpaceshipPawn::BuildCameraViewRotation(
		FRotator(0.f, 0.f, 10.f).Quaternion(), BaseRotation, Lag);
	const float LaggedRelativeRoll = FMath::RadiansToDegrees(float((ViewAtZero.Inverse() * ViewAtTen).GetAngle()));
	TestTrue(FString::Printf(TEXT("机身滚 10° → 机位转 %.2f°（应为 10 + %.2f）"), LaggedRelativeRoll, Lag),
		FMath::IsNearlyEqual(LaggedRelativeRoll, 10.f + Lag, 0.5f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraSpaceshipRollKeepsViewTest,
	"StellarEra.Spaceship.RollKeepsViewDirection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraSpaceshipRollKeepsViewTest::RunTest(const FString& Parameters)
{
	UClass* ShipClass = LoadShipClass();
	if (!TestNotNull(TEXT("BP_SpaceshipPawn_C 可加载"), ShipClass))
	{
		return false;
	}

	const ASpaceshipPawn* CDO = Cast<ASpaceshipPawn>(ShipClass->GetDefaultObject());
	const UCameraComponent* Camera = CDO ? CDO->FindComponentByClass<UCameraComponent>() : nullptr;
	if (!TestNotNull(TEXT("相机组件"), Camera))
	{
		return false;
	}

	// 机位是刚性的：眼睛固连在机身局部系里，绕机身 X 轴跟机身一起公转。摇臂必须继承横滚，
	// 否则眼睛不跟着转、画面却转了，飞船就在屏幕上前后左右乱飘。
	const USpringArmComponent* Arm = CDO->FindComponentByClass<USpringArmComponent>();
	if (!TestNotNull(TEXT("摇臂组件"), Arm))
	{
		return false;
	}
	TestTrue(TEXT("摇臂继承横滚"), Arm->bInheritRoll);

	// 眼睛在机身局部系里的位置：摇臂往后伸一段 + 抬升偏移 + 相机自己的相对位置。
	const FVector EyeLocal = Arm->GetRelativeRotation().RotateVector(
		FVector(-Arm->TargetArmLength, 0.f, 0.f) + Arm->SocketOffset + Camera->GetRelativeLocation());
	if (!TestTrue(FString::Printf(TEXT("眼睛在机身局部系里 %s"), *EyeLocal.ToCompactString()),
		!EyeLocal.IsNearlyZero()))
	{
		return false;
	}

	const FQuat BaseRotation = Camera->GetRelativeRotation().Quaternion();
	const FVector Forward = FVector::ForwardVector;
	const FRotator HullPose(-11.f, 137.f, 0.f);

	// 机身滚到 RollDeg 时的「视线方向」与「相机→飞船」方向的夹角。视线怎么滚都不该改变它。
	const auto ShipAngleAt = [&](float RollDeg)
	{
		const FQuat Hull = FRotator(HullPose.Pitch, HullPose.Yaw, RollDeg).Quaternion();
		const FVector Eye = Hull.RotateVector(EyeLocal);
		const FVector View = ASpaceshipPawn::BuildCameraViewRotation(Hull, BaseRotation, 0.f).RotateVector(Forward);
		return ViewAngleDeg(View, (FVector::ZeroVector - Eye).GetSafeNormal());
	};

	const float AngleAtZero = ShipAngleAt(0.f);
	float WorstSwing = 0.f;
	float WorstOldSwing = 0.f;
	for (int32 RollDeg = -180; RollDeg <= 180; RollDeg += 15)
	{
		WorstSwing = FMath::Max(WorstSwing, FMath::Abs(ShipAngleAt(float(RollDeg)) - AngleAtZero));

		// 老链路：视线只绕自身 X 轴补 roll（右乘），眼睛却仍随机身公转。两条轴差一个蓝图俯角，
		// 飞船就绕着屏幕中心甩出去 —— 实测滚 115° 时偏 39°。
		const FQuat FreeView = FRotator(HullPose.Pitch, HullPose.Yaw, 0.f).Quaternion() * BaseRotation
			* FQuat(Forward, FMath::DegreesToRadians(-float(RollDeg)));
		const FVector OldEye = FRotator(HullPose.Pitch, HullPose.Yaw, float(RollDeg)).Quaternion()
			.RotateVector(EyeLocal);
		WorstOldSwing = FMath::Max(WorstOldSwing, FMath::Abs(
			ViewAngleDeg(FreeView.RotateVector(Forward), (FVector::ZeroVector - OldEye).GetSafeNormal()) - AngleAtZero));
	}

	TestTrue(FString::Printf(TEXT("飞船相对视线的偏角 %.2f° 随滚转不变（最大摆动 %.4f°）"),
		AngleAtZero, WorstSwing),
		WorstSwing <= 0.01f);
	// 反例必须成立，否则上面那条是空转。
	TestTrue(FString::Printf(TEXT("只绕视线轴补 roll 时该甩 ≥15°（实测 %.1f°）"), WorstOldSwing),
		WorstOldSwing >= 15.f);

	// 画面确实跟着机身滚：滚 40° 前后，机位只差一个「绕视线方向 40°」的旋转。
	const auto ViewAt = [&](float BodyRoll)
	{
		return ASpaceshipPawn::BuildCameraViewRotation(
			FRotator(HullPose.Pitch, HullPose.Yaw, BodyRoll).Quaternion(), BaseRotation, 0.f);
	};
	const FQuat ViewAtZero = ViewAt(0.f);
	const FQuat ViewAtForty = ViewAt(40.f);
	const float RelativeRoll = FMath::RadiansToDegrees(float((ViewAtZero.Inverse() * ViewAtForty).GetAngle()));
	TestTrue(FString::Printf(TEXT("机身滚 40° → 机位相对旋转 %.2f°"), RelativeRoll),
		FMath::IsNearlyEqual(RelativeRoll, 40.f, 0.5f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraSpaceshipRollAcrossVerticalTest,
	"StellarEra.Spaceship.RollAcrossVertical",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraSpaceshipRollAcrossVerticalTest::RunTest(const FString& Parameters)
{
	// 符号契约：机身滚 +40°（FRotator 语义）→ 增量 +40°。
	TestTrue(TEXT("纯横滚 +40° 的增量是 +40°"),
		FMath::IsNearlyEqual(
			ASpaceshipPawn::ExtractRollDeltaDegrees(
				FRotator::ZeroRotator.Quaternion(), FRotator(0.f, 0.f, 40.f).Quaternion()),
			40.f, 0.01f));

	// 机身横滚恒为 30°，俯仰连续翻越垂直（筋斗/过顶）。每 0.5° 采样一次。
	// 四元数 twist 必须连续；旧的欧拉口径在同一次翻越里会跳 180°。
	const float BodyRoll = 30.f;
	const auto PoseAt = [BodyRoll](float Pitch)
	{
		return FRotator(Pitch, 0.f, BodyRoll).Quaternion();
	};

	FQuat Previous = PoseAt(85.f);
	float MaximumRollStep = 0.f;
	float MaximumEulerStep = 0.f;
	float PreviousEulerRoll = Previous.Rotator().Roll;
	for (float Pitch = 85.5f; Pitch <= 95.01f; Pitch += 0.5f)
	{
		const FQuat Current = PoseAt(Pitch);
		MaximumRollStep = FMath::Max(MaximumRollStep,
			FMath::Abs(ASpaceshipPawn::ExtractRollDeltaDegrees(Previous, Current)));

		const float EulerRoll = Current.Rotator().Roll;
		MaximumEulerStep = FMath::Max(MaximumEulerStep,
			FMath::Abs(FRotator::NormalizeAxis(EulerRoll - PreviousEulerRoll)));
		PreviousEulerRoll = EulerRoll;
		Previous = Current;
	}

	TestTrue(FString::Printf(TEXT("过顶时单步横滚增量 %.3f° 保持连续（≤1°）"), MaximumRollStep),
		MaximumRollStep <= 1.f);
	// 反例必须成立，否则上面那条是空转。
	TestTrue(FString::Printf(TEXT("欧拉 Roll 在同一次过顶里跳 %.1f°（应 ≥140°）"), MaximumEulerStep),
		MaximumEulerStep >= 140.f);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
