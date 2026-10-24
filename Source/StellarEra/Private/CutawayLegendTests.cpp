// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "StarMapUISystem.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FStellarEraCutawayLegendFormatTest,
	"StellarEra.StarMap.Cutaway.Legend.Format",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraCutawayLegendFormatTest::RunTest(const FString& Parameters)
{
	const TArray<FCutawayLegendEntry> Entries = {
		{ TEXT("硅"), FLinearColor::Gray, 0.5f },
		{ TEXT("铁"), FLinearColor::Red, 0.25f },
		{ TEXT(""), FLinearColor::Blue, 0.25f }
	};

	const TArray<FString> Lines =
		UStarMapUISystem::FormatCutawayLegendLines(TEXT("地球"), Entries);

	TestEqual(TEXT("标题 + 三行元素 + 提示"), Lines.Num(), 5);
	TestEqual(TEXT("标题带行星名"), Lines[0], FString(TEXT("矿物剖面 — 地球")));
	TestEqual(TEXT("元素行含名称与百分比"), Lines[1], FString(TEXT("硅  50%")));
	TestEqual(TEXT("第二行"), Lines[2], FString(TEXT("铁  25%")));
	TestEqual(TEXT("空名退回问号"), Lines[3], FString(TEXT("?  25%")));
	TestTrue(TEXT("末行是操作提示"), Lines.Last().StartsWith(TEXT("H 退出")));

	// 百分比夹取：负值按 0、超过 1 按 100。
	const TArray<FCutawayLegendEntry> Clamped = {
		{ TEXT("负"), FLinearColor::White, -0.4f },
		{ TEXT("溢"), FLinearColor::White, 1.7f }
	};
	const TArray<FString> ClampedLines =
		UStarMapUISystem::FormatCutawayLegendLines(TEXT("X"), Clamped);
	TestEqual(TEXT("负值夹到 0%"), ClampedLines[1], FString(TEXT("负  0%")));
	TestEqual(TEXT("超过 1 夹到 100%"), ClampedLines[2], FString(TEXT("溢  100%")));

	// 没有元素：仍给一行占位。
	const TArray<FCutawayLegendEntry> NoEntries;
	const TArray<FString> EmptyLines =
		UStarMapUISystem::FormatCutawayLegendLines(TEXT("X"), NoEntries);
	TestEqual(TEXT("空元素表共三行"), EmptyLines.Num(), 3);
	TestEqual(TEXT("占位文本"), EmptyLines[1], FString(TEXT("（无矿物元素）")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
