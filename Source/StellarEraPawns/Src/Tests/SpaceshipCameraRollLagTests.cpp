// 相机横滚的两条契约。
// 一是追的速率：追得太慢就是「滚转开头画面跟不上、松手反甩」，上界由 MaxRollRate /
// CameraRollFollowSpeed 决定，这里按实际 CDO 参数数值积分。
// 二是施加方式：横滚要把**整条机位**（视线 + 相机位置）一起绕机身 X 轴拧，只拧视线会让
// 飞船在屏幕上绕圈 —— 相机位置仍由摇臂带着公转，视线却不跟。

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

	// 与 Tick 同一套积分：最短弧指数逼近机身的 Roll。
	float StepRoll(float Applied, float Target, float DeltaSeconds, float Speed)
	{
		const float Alpha = Speed > 0.f ? 1.f - FMath::Exp(-Speed * DeltaSeconds) : 1.f;
		return Applied + FMath::FindDeltaAngleDegrees(Applied, Target) * Alpha;
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

	// 持续压杆 1 秒后的稳态滞后：机身滚得最狠时画面欠了多少度。
	const float Dt = 1.f / 60.f;
	float Applied = 0.f;
	float Target = 0.f;
	for (int32 i = 0; i < 60; ++i)
	{
		Target += MaxRollRate * Dt;
		Applied = StepRoll(Applied, Target, Dt, FollowSpeed);
	}
	TestTrue(FString::Printf(TEXT("满滚转稳态滞后 %.2f° 不超过 5°"), Target - Applied),
		FMath::Abs(Target - Applied) <= 5.f);

	// 松手后滞后归零：超过 ~0.25 秒就是能看见的「反甩」。
	int32 FramesToSettle = 0;
	for (; FramesToSettle < 120 && FMath::Abs(Target - Applied) > 0.5f; ++FramesToSettle)
	{
		Applied = StepRoll(Applied, Target, Dt, FollowSpeed);
	}
	TestTrue(FString::Printf(TEXT("松手后 %.3f s 归零，不超过 0.25 s"), FramesToSettle * Dt),
		FramesToSettle * Dt <= 0.25f);

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
		const FVector View = ASpaceshipPawn::BuildCameraViewRotation(
			FRotator(HullPose.Pitch, HullPose.Yaw, RollDeg), BaseRotation, RollDeg).RotateVector(Forward);
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
			FRotator(HullPose.Pitch, HullPose.Yaw, BodyRoll), BaseRotation, BodyRoll);
	};
	const FQuat ViewAtZero = ViewAt(0.f);
	const FQuat ViewAtForty = ViewAt(40.f);
	const float RelativeRoll = FMath::RadiansToDegrees(float((ViewAtZero.Inverse() * ViewAtForty).GetAngle()));
	TestTrue(FString::Printf(TEXT("机身滚 40° → 机位相对旋转 %.2f°"), RelativeRoll),
		FMath::IsNearlyEqual(RelativeRoll, 40.f, 0.5f));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
