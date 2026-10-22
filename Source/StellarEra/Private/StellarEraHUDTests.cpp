// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "StellarEraHUD.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraHUDFrameRateTest,
	"StellarEra.UI.HUD.FrameRateFormat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraHUDFrameRateTest::RunTest(const FString& Parameters)
{
	// 格式化：59.94 fps 应显示 59.9 FPS 与 16.7 ms。
	const FString FastLine = AStellarEraHUD::FormatFrameRateLine(59.94f);
	TestTrue(TEXT("含一位小数的 FPS"), FastLine.Contains(TEXT("59.9 FPS")));
	TestTrue(TEXT("含帧时间"), FastLine.Contains(TEXT("16.7 ms")));

	// 无效帧率（尚无样本 / 负值 / 非有限值）显示占位符，不能打印 nan/inf。
	TestTrue(TEXT("零帧率显示占位符"), AStellarEraHUD::FormatFrameRateLine(0.f).Contains(TEXT("--")));
	TestTrue(TEXT("负帧率显示占位符"), AStellarEraHUD::FormatFrameRateLine(-1.f).Contains(TEXT("--")));
	TestTrue(TEXT("非有限帧率显示占位符"),
		AStellarEraHUD::FormatFrameRateLine(std::numeric_limits<float>::infinity()).Contains(TEXT("--")));

	// 平滑：第一个样本直接用瞬时帧率，不被历史值拖慢。
	TestTrue(TEXT("首个样本取瞬时值"),
		FMath::IsNearlyEqual(AStellarEraHUD::UpdateSmoothedFps(0.f, 1.f / 60.f, 0.5f), 60.f, 0.01f));

	// 平滑：从 60 fps 起持续收到 30 fps 的样本，应单调下降并收敛到 30。
	float Smoothed = 60.f;
	bool bMonotonicDown = true;
	for (int32 Index = 0; Index < 300; ++Index)
	{
		const float Next = AStellarEraHUD::UpdateSmoothedFps(Smoothed, 1.f / 30.f, 0.5f);
		bMonotonicDown = bMonotonicDown && Next <= Smoothed + KINDA_SMALL_NUMBER;
		Smoothed = Next;
	}
	TestTrue(TEXT("平滑值单调下降"), bMonotonicDown);
	TestTrue(TEXT("收敛到 30 fps"), FMath::IsNearlyEqual(Smoothed, 30.f, 0.05f));

	// 零/负 DeltaSeconds（暂停、时间回绕）不改变已有值。
	TestEqual(TEXT("零 dt 保持原值"),
		AStellarEraHUD::UpdateSmoothedFps(45.f, 0.f, 0.5f), 45.f);
	TestEqual(TEXT("负 dt 保持原值"),
		AStellarEraHUD::UpdateSmoothedFps(45.f, -0.016f, 0.5f), 45.f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraHUDGravityTest,
	"StellarEra.UI.HUD.GravityFormat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraHUDGravityTest::RunTest(const FString& Parameters)
{
	// 换算出来的数值要同时给出 km/s² 与 m/s²：前者与推力同单位便于比较，
	// 后者便于和现实重力（9.8 m/s²）对感觉。
	const FString HeavyLine = AStellarEraHUD::FormatGravityLine(43.776f, 12.5f);
	TestTrue(TEXT("含 km/s² 读数"), HeavyLine.Contains(TEXT("43.78 km/s²")));
	TestTrue(TEXT("含 m/s² 读数"), HeavyLine.Contains(TEXT("43776 m/s²")));
	TestTrue(TEXT("重力大于推力时告警"), HeavyLine.Contains(TEXT("超出推力")));

	// 重力小于推力：不告警，否则这个标记就没有信息量。
	const FString LightLine = AStellarEraHUD::FormatGravityLine(10.944f, 12.5f);
	TestFalse(TEXT("重力小于推力不告警"), LightLine.Contains(TEXT("超出推力")));

	// 恰好相等也算「能抬起来」（净加速度为零，加速度谈不上），不标超限。
	TestFalse(TEXT("相等不告警"),
		AStellarEraHUD::FormatGravityLine(12.5f, 12.5f).Contains(TEXT("超出推力")));

	// 未知重力（子系统未注册 / 没有求解器索引）显示占位符，不能印 nan/inf。
	TestTrue(TEXT("负值显示占位符"),
		AStellarEraHUD::FormatGravityLine(-1.f, 12.5f).Contains(TEXT("--")));
	TestTrue(TEXT("非有限值显示占位符"),
		AStellarEraHUD::FormatGravityLine(std::numeric_limits<float>::infinity(), 12.5f).Contains(TEXT("--")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraHUDSpeedLinesTest,
	"StellarEra.UI.HUD.SpeedLines",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraHUDSpeedLinesTest::RunTest(const FString& Parameters)
{
	FString Absolute;
	FString Relative;

	// 有上限：绝对速度印「当前 / 上限」；相对速度单独一行，不带上限
	// （上限目前作用在绝对速度上，印在相对速度旁边会误导）。
	AStellarEraHUD::FormatSpeedLines(1.665f, 0.f, 1.5f, true, Absolute, Relative);
	TestTrue(TEXT("绝对速度标明绝对"), Absolute.Contains(TEXT("绝对速度")));
	TestTrue(TEXT("绝对速度含当前值与上限"), Absolute.Contains(TEXT("1.66 / 1.50 km/s")));
	TestTrue(TEXT("相对速度标明相对"), Relative.Contains(TEXT("相对速度")));
	TestTrue(TEXT("相对速度含读数"), Relative.Contains(TEXT("0.00 km/s")));
	// 注意：km/s 自带斜杠，判「有没有上限」只能找 " / " 这个格式。
	TestFalse(TEXT("相对速度不带上限"), Relative.Contains(TEXT(" / ")));

	// 无上限（0 = 什么上限都没有）时不能印「x / 0」。
	AStellarEraHUD::FormatSpeedLines(1.665f, 1.2f, 0.f, true, Absolute, Relative);
	TestTrue(TEXT("无上限时印无上限"), Absolute.Contains(TEXT("无上限")));
	TestFalse(TEXT("无上限时不印上限格式"), Absolute.Contains(TEXT(" / ")));
	TestTrue(TEXT("相对速度 1.2"), Relative.Contains(TEXT("1.20 km/s")));

	// 没有参考行星（纯太空）：相对速度印占位符，不假装有个参考系。
	AStellarEraHUD::FormatSpeedLines(1.f, 1.f, 0.f, false, Absolute, Relative);
	TestTrue(TEXT("无参考行星时相对速度印占位符"), Relative.Contains(TEXT("--")));
	TestTrue(TEXT("无参考行星时提及无参考行星"), Relative.Contains(TEXT("无参考行星")));

	// 非有限值不能印 nan/inf。
	AStellarEraHUD::FormatSpeedLines(std::numeric_limits<float>::infinity(), 0.f, 1.5f,
		true, Absolute, Relative);
	TestFalse(TEXT("绝对速度非有限值不印 inf"), Absolute.Contains(TEXT("inf")));
	TestTrue(TEXT("绝对速度非有限值印占位符"), Absolute.Contains(TEXT("--")));

	return true;
}

#endif
