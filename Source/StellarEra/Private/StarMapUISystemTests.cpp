// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "CosmosTypes.h"
#include "StarMapUISystem.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraStarMapUISurfaceGravityTest,
	"StellarEra.StarMap.UI.SurfaceGravity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraStarMapUISurfaceGravityTest::RunTest(const FString& Parameters)
{
	const double EarthGravity = UStarMapUISystem::ComputeSurfaceGravityMps2(1.0, 6371.0);
	TestTrue(TEXT("地球表面重力约 9.822 m/s²"),
		FMath::IsNearlyEqual(EarthGravity, 9.822, 0.05));

	const double DoubleMass = UStarMapUISystem::ComputeSurfaceGravityMps2(2.0, 6371.0);
	TestTrue(TEXT("质量翻倍，重力翻倍"),
		FMath::IsNearlyEqual(DoubleMass, EarthGravity * 2.0, 1.0e-9));

	TestEqual(TEXT("质量为 0 返回 0"),
		UStarMapUISystem::ComputeSurfaceGravityMps2(0.0, 6371.0), 0.0);
	TestEqual(TEXT("半径为 0 返回 0"),
		UStarMapUISystem::ComputeSurfaceGravityMps2(1.0, 0.0), 0.0);
	TestEqual(TEXT("质量为 NaN 返回 0"),
		UStarMapUISystem::ComputeSurfaceGravityMps2(
			std::numeric_limits<double>::quiet_NaN(), 6371.0), 0.0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraStarMapUIFormattingTest,
	"StellarEra.StarMap.UI.Formatting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraStarMapUIFormattingTest::RunTest(const FString& Parameters)
{
	FStarMapBodyInfo Info;
	Info.Name = TEXT("地球");
	Info.MassEarth = 1.0;
	Info.MassSolar = CosmosUnits::EarthMassToSolarMass(1.0);
	Info.PositionAu = FVector(0.8732, 0.0, 0.0);
	Info.VelocityKmPerSecond = FVector(29.790, 0.0, 0.0);
	Info.OrbitalPeriodYears = 1.0;
	Info.RadiusKm = 6371.0;
	Info.SurfaceGravityMps2 = UStarMapUISystem::ComputeSurfaceGravityMps2(1.0, 6371.0);
	Info.SimulatorIndex = 0;
	Info.bSimulated = true;
	Info.bExertsGravity = true;

	const TArray<FString> Lines = UStarMapUISystem::FormatBodyInfoLines(Info);
	const FString Joined = FString::Join(Lines, TEXT("\n"));

	TestTrue(TEXT("第一行是名字"), Lines.Num() > 0 && Lines[0] == TEXT("地球"));
	TestTrue(TEXT("含质量 M⊕"), Joined.Contains(TEXT("1 M⊕")));
	TestTrue(TEXT("含质量 M☉"), Joined.Contains(TEXT("3.003e-06 M☉")));
	TestTrue(TEXT("含位置 AU"), Joined.Contains(TEXT("(0.8732, 0.0000, 0.0000) AU")));
	TestTrue(TEXT("含速度"), Joined.Contains(TEXT("29.790 km/s")));
	TestTrue(TEXT("含轨道周期"), Joined.Contains(TEXT("1.000 年")));
	TestTrue(TEXT("含半径"), Joined.Contains(TEXT("6371.0 km")));
	// 不写死 9.822：地球参数按当前 G / M⊕ 常数算出来是 9.824，
	// 用同一个值生成期望串，测试只锁「格式与数值来源一致」。
	TestTrue(TEXT("含表面引力"), Joined.Contains(FString::Printf(
		TEXT("%.3f m/s²"), Info.SurfaceGravityMps2)));
	TestTrue(TEXT("含 g 倍数"), Joined.Contains(FString::Printf(
		TEXT("%.3f g"), Info.SurfaceGravityMps2 / 9.80665)));
	TestTrue(TEXT("含 Simulator 下标"), Joined.Contains(TEXT("#0")));
	TestTrue(TEXT("含模拟与引力源"), Joined.Contains(TEXT("模拟")) &&
		Joined.Contains(TEXT("引力源")));

	FStarMapBodyInfo Unknown = Info;
	Unknown.OrbitalPeriodYears = 0.0;
	Unknown.RadiusKm = 0.0;
	Unknown.SurfaceGravityMps2 = 0.0;
	Unknown.SimulatorIndex = INDEX_NONE;
	Unknown.bSimulated = false;
	Unknown.bExertsGravity = false;

	const FString UnknownJoined =
		FString::Join(UStarMapUISystem::FormatBodyInfoLines(Unknown), TEXT("\n"));
	TestTrue(TEXT("周期无效显示未知"), UnknownJoined.Contains(TEXT("未知")));
	TestTrue(TEXT("半径无效显示破折号"), UnknownJoined.Contains(TEXT("半径        —")));
	TestTrue(TEXT("引力无效显示破折号"), UnknownJoined.Contains(TEXT("表面引力    —")));
	TestTrue(TEXT("未注册显示提示"), UnknownJoined.Contains(TEXT("#-1（未注册）")));
	TestTrue(TEXT("模拟关闭显示否"), UnknownJoined.Contains(TEXT("模拟        否")));
	TestTrue(TEXT("引力源关闭显示否"), UnknownJoined.Contains(TEXT("引力源    否")));

	return true;
}

#endif
