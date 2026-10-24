// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "StarMapUISystem.h"
#include "PlanetGeologyField.h"

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

	// 四舍五入而非截断：0.256 → 25.6%，必须印 26%（截断会印 25%）。
	const TArray<FCutawayLegendEntry> Rounding = {
		{ TEXT("半"), FLinearColor::White, 0.256f }
	};
	const TArray<FString> RoundingLines =
		UStarMapUISystem::FormatCutawayLegendLines(TEXT("X"), Rounding);
	TestEqual(TEXT("0.256 四舍五入到 26%"), RoundingLines[1], FString(TEXT("半  26%")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FStellarEraCutawayLegendElementOrderTest,
	"StellarEra.StarMap.Cutaway.Legend.ElementOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraCutawayLegendElementOrderTest::RunTest(const FString& Parameters)
{
	// Elements.json 数组顺序是 B、A、C；行星 JSON 的元素对象按 A、B、C 列出。
	// 图例顺序必须跟元素表数组走（spec §3.3），而不是行星 JSON 的键顺序。
	const TCHAR* ElementsJson = TEXT(R"JSON(
{
  "version": 1,
  "elements": [
    { "id": "b", "displayName": "乙", "color": [0.0, 0.0, 1.0], "noise": { "seed": 11 } },
    { "id": "a", "displayName": "甲", "color": [1.0, 0.0, 0.0], "noise": { "seed": 12 } },
    { "id": "c", "displayName": "丙", "color": [0.0, 1.0, 0.0], "noise": { "seed": 13 } }
  ]
}
)JSON");

	const TCHAR* PlanetJson = TEXT(R"JSON(
{
  "version": 1,
  "elements": {
    "a": { "abundance": 0.2, "peakDepth": 0, "layerThickness": 10 },
    "b": { "abundance": 0.5, "peakDepth": 0, "layerThickness": 10 },
    "c": { "abundance": 0.3, "peakDepth": 0, "layerThickness": 10 }
  }
}
)JSON");

	FPlanetGeologyLoadReport Report;
	const TSharedPtr<const FPlanetGeologyField> Field =
		FPlanetGeologyField::LoadFromString(ElementsJson, PlanetJson, Report);
	TestFalse(TEXT("合法输入不应产生 error"), Report.HasErrors());
	TestTrue(TEXT("顺序测试场有效"), Field.IsValid() && !Field->IsEmpty());
	if (!Field.IsValid())
	{
		return false;
	}

	const TArray<FPlanetGeologyElement> Definitions = Field->GetElementDefinitions();
	TestEqual(TEXT("元素个数"), Definitions.Num(), 3);
	if (Definitions.Num() == 3)
	{
		TestEqual(TEXT("第 1 个 = 元素表首位的 b"),
			Definitions[0].Id, FString(TEXT("b")));
		TestEqual(TEXT("第 2 个 = a"),
			Definitions[1].Id, FString(TEXT("a")));
		TestEqual(TEXT("第 3 个 = c"),
			Definitions[2].Id, FString(TEXT("c")));

		// 顺序变了，丰度不能跟着错位。
		TestTrue(TEXT("b 的丰度 0.5 仍挂在 b 上"),
			FMath::IsNearlyEqual(Definitions[0].Abundance, 0.5f, 1e-4f));
		TestTrue(TEXT("a 的丰度 0.2 仍挂在 a 上"),
			FMath::IsNearlyEqual(Definitions[1].Abundance, 0.2f, 1e-4f));
		TestTrue(TEXT("c 的丰度 0.3 仍挂在 c 上"),
			FMath::IsNearlyEqual(Definitions[2].Abundance, 0.3f, 1e-4f));
	}

	// 单元素场：份额是 1/1，格式化后必须是 100%。
	FPlanetGeologyLoadReport SingleReport;
	const TSharedPtr<const FPlanetGeologyField> SingleField =
		FPlanetGeologyField::LoadFromString(
			TEXT(R"JSON({"version":1,"elements":[{"id":"solo","displayName":"单"}]})JSON"),
			TEXT(R"JSON({"version":1,"elements":{"solo":{"abundance":1.0,"peakDepth":0,"layerThickness":10}}})JSON"),
			SingleReport);
	TestFalse(TEXT("单元素场不应产生 error"), SingleReport.HasErrors());
	TestTrue(TEXT("单元素场有效"), SingleField.IsValid() && !SingleField->IsEmpty());
	if (SingleField.IsValid())
	{
		const TArray<FPlanetGeologyElement> SingleDefinitions =
			SingleField->GetElementDefinitions();
		TestEqual(TEXT("单元素场只有一个元素"), SingleDefinitions.Num(), 1);
		if (SingleDefinitions.Num() == 1)
		{
			const TArray<FCutawayLegendEntry> SingleEntries = {
				{ SingleDefinitions[0].DisplayName, SingleDefinitions[0].Color,
					SingleDefinitions[0].Abundance /
						FMath::Max(SingleField->GetTotalAbundance(), 1e-6f) }
			};
			const TArray<FString> SingleLines =
				UStarMapUISystem::FormatCutawayLegendLines(TEXT("X"), SingleEntries);
			TestEqual(TEXT("单元素 1/1 显示 100%"),
				SingleLines[1], FString(TEXT("单  100%")));
		}
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
