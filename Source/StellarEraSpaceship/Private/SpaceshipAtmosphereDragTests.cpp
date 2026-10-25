// Copyright Voxel Plugin SAS. All Rights Reserved.
//
// 大气阻力的验收测试。
//
// 纯函数：密度剖面与二次阻力公式。真正挂到行星上的那条路径需要
// AVoxelPlanetActor（构造即建体素世界），不适合在单元测试里生成；那部分在
// UniverseMap PIE 里验证 —— HUD 会直接印出当前绝对/相对速度与阻力减速度。
//
// 相对速度的接入单独验：参考速度只是子系统每帧推进来的一个 FVector，生成一艘
// 裸飞船就能断言绝对值/相对值/状态字段，不需要真的行星。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "SpaceshipPawn.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraAtmosphereDensityTest,
	"StellarEra.Vehicle.AtmosphereDensityProfile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraAtmosphereDensityTest::RunTest(const FString& Parameters)
{
	// 取本关卡大行星的实际比例尺：表面半径 2 AU = 2e6 cm，壳厚 5%，标高 1.5%。
	constexpr double SurfaceRadiusCm = 2.0e6;
	constexpr double ScaleHeightRatio = 0.015;
	constexpr double ShellHeightRatio = 0.05;
	const double ScaleHeightCm = SurfaceRadiusCm * ScaleHeightRatio;
	const double ShellHeightCm = SurfaceRadiusCm * ShellHeightRatio;

	auto Density = [=](double AltitudeCm)
	{
		return ASpaceshipPawn::ComputeAtmosphereDensityRatio(
			AltitudeCm, SurfaceRadiusCm, ScaleHeightRatio, ShellHeightRatio);
	};

	TestTrue(TEXT("海平面密度比 = 1"),
		FMath::IsNearlyEqual(Density(0.0), 1.f, 1.0e-5f));
	TestTrue(TEXT("地面以下按海平面处理（不出现 ρ > 1）"),
		FMath::IsNearlyEqual(Density(-10000.0), 1.f, 1.0e-5f));
	TestTrue(TEXT("一个标高处 = 1/e"),
		FMath::IsNearlyEqual(Density(ScaleHeightCm), 0.3678794f, 1.0e-4f));
	TestTrue(TEXT("壳顶 = 0"),
		FMath::IsNearlyEqual(Density(ShellHeightCm), 0.f));
	TestTrue(TEXT("壳外 = 0"),
		FMath::IsNearlyEqual(Density(ShellHeightCm + 1.0), 0.f));

	// 参数非法时宁可不加阻力，也不能除零 / exp 溢出。
	TestTrue(TEXT("表面半径非法 = 0"),
		FMath::IsNearlyEqual(ASpaceshipPawn::ComputeAtmosphereDensityRatio(
			0.0, 0.0, ScaleHeightRatio, ShellHeightRatio), 0.f));
	TestTrue(TEXT("标高比非法 = 0"),
		FMath::IsNearlyEqual(ASpaceshipPawn::ComputeAtmosphereDensityRatio(
			0.0, SurfaceRadiusCm, 0.0, ShellHeightRatio), 0.f));
	TestTrue(TEXT("壳厚比非法 = 0"),
		FMath::IsNearlyEqual(ASpaceshipPawn::ComputeAtmosphereDensityRatio(
			0.0, SurfaceRadiusCm, ScaleHeightRatio, 0.0), 0.f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraAtmosphereDragTest,
	"StellarEra.Vehicle.AtmosphereDragAcceleration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraAtmosphereDragTest::RunTest(const FString& Parameters)
{
	constexpr double DragKmPerS2 = 10.0;

	// 1 km/s 向下 = 1e5 cm/s；海平面、系数 10 km/s² → 减速度 1e6 cm/s² 向上。
	const FVector Down(0.0, 0.0, -1.0e5);
	const FVector Reference =
		ASpaceshipPawn::ComputeAtmosphereDragAccelerationWorldCmPerS2(
			Down, 1.0, DragKmPerS2);
	TestTrue(TEXT("方向与速度严格相反"),
		Reference.Equals(FVector(0.0, 0.0, 1.0e6), 1.0));

	// 二次阻力：速度翻倍 → 阻力四倍。
	const FVector Fast =
		ASpaceshipPawn::ComputeAtmosphereDragAccelerationWorldCmPerS2(
			Down * 2.0, 1.0, DragKmPerS2);
	TestTrue(TEXT("速度翻倍阻力四倍"),
		Fast.Equals(FVector(0.0, 0.0, 4.0e6), 4.0));

	// 密度折半 → 阻力折半。
	const FVector Thin =
		ASpaceshipPawn::ComputeAtmosphereDragAccelerationWorldCmPerS2(
			Down, 0.5, DragKmPerS2);
	TestTrue(TEXT("密度折半阻力折半"),
		Thin.Equals(FVector(0.0, 0.0, 5.0e5), 1.0));

	// 任一输入为零都必须是零向量：真空里不能凭空出现减速。
	TestTrue(TEXT("速度为零 = 无阻力"),
		ASpaceshipPawn::ComputeAtmosphereDragAccelerationWorldCmPerS2(
			FVector::ZeroVector, 1.0, DragKmPerS2).IsNearlyZero());
	TestTrue(TEXT("密度为零 = 无阻力"),
		ASpaceshipPawn::ComputeAtmosphereDragAccelerationWorldCmPerS2(
			Down, 0.0, DragKmPerS2).IsNearlyZero());
	TestTrue(TEXT("系数为零 = 无阻力"),
		ASpaceshipPawn::ComputeAtmosphereDragAccelerationWorldCmPerS2(
			Down, 1.0, 0.0).IsNearlyZero());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraRelativeVelocityTest,
	"StellarEra.Vehicle.RelativeVelocityToPlanet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraRelativeVelocityTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false);
	if (!TestNotNull(TEXT("测试世界"), World))
	{
		return false;
	}
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Editor);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());

	ON_SCOPE_EXIT
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	};

	ASpaceshipPawn* Ship = World->SpawnActor<ASpaceshipPawn>(
		ASpaceshipPawn::StaticClass(),
		FTransform(FRotator::ZeroRotator, FVector(0.0, 0.0, 1.0e7)));
	if (!TestNotNull(TEXT("能生成飞船"), Ship))
	{
		return false;
	}

	// 外圈行星的世界速度 1.665 km/s = 166500 cm/s。飞船跟着它走时，绝对速度
	// 是 1.665 km/s，相对速度必须是 0 —— 这正是大气阻力该看到的数。
	const FVector PlanetVelocity(1.665e5, 0.0, 0.0);
	Ship->SetCosmosVelocity(PlanetVelocity);
	Ship->SetReferencePlanetVelocity(PlanetVelocity);

	TestTrue(TEXT("随行星一起运动时相对速度为零"),
		Ship->GetRelativeVelocity().IsNearlyZero(1.0));

	FSpaceshipStatus Status = Ship->GetStatus();
	TestTrue(TEXT("绝对速度仍报 1.665 km/s"),
		FMath::IsNearlyEqual(Status.SpeedKmPerSecond, 1.665f, 1.0e-3f));
	TestTrue(TEXT("相对速度报 0"),
		FMath::IsNearlyEqual(Status.RelativeSpeedKmPerSecond, 0.f, 1.0e-3f));

	// 裸飞船没有 PlanetActor：HUD 据此把相对速度印成占位符。
	TestFalse(TEXT("没有行星时 bHasReferencePlanet 为假"), Status.bHasReferencePlanet);

	// 相对速度是矢量差：飞船沿 +Y 相对行星 0.333 km/s。
	Ship->SetCosmosVelocity(PlanetVelocity + FVector(0.0, 3.33e4, 0.0));
	TestTrue(TEXT("相对速度方向正确"),
		Ship->GetRelativeVelocity().Equals(FVector(0.0, 3.33e4, 0.0), 1.0));
	Status = Ship->GetStatus();
	TestTrue(TEXT("相对速度大小 0.333 km/s"),
		FMath::IsNearlyEqual(Status.RelativeSpeedKmPerSecond, 0.333f, 1.0e-3f));

	// 参考速度清零 = 没有参考系：相对速度退化为绝对速度（与改动前一致）。
	Ship->SetReferencePlanetVelocity(FVector::ZeroVector);
	TestTrue(TEXT("没有参考时相对速度等于绝对速度"),
		Ship->GetRelativeVelocity().Equals(Ship->GetVelocity(), 1.0));

	return true;
}

#endif
