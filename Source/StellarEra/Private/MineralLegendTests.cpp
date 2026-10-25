// 矿物观察图例的纯函数测试：布局、命中、文本、空表。
// 只断言关系（行在面板内、行与行相邻不重叠），不写死像素数。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "StarMapUISystem.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace MineralLegendTest
{
	constexpr int32 EntryCount = 3;

	void BuildLayout(FStarMapPanelRect& OutPanel, TArray<FStarMapPanelRect>& OutRows,
		int32 Entries = EntryCount)
	{
		UStarMapUISystem::ComputeMineralLegendLayout(
			FVector2D(1920.f, 1080.f), Entries, OutPanel, OutRows);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMineralLegendLayoutTest,
	"StellarEra.StarMap.MineralLegend.Layout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMineralLegendLayoutTest::RunTest(const FString& Parameters)
{
	FStarMapPanelRect Panel;
	TArray<FStarMapPanelRect> Rows;
	MineralLegendTest::BuildLayout(Panel, Rows);

	TestEqual(TEXT("行矩形数量 = 元素数"), Rows.Num(), MineralLegendTest::EntryCount);
	TestEqual(TEXT("面板高度与公式一致"), Panel.H,
		MineralLegendLayout::PanelHeightPx(MineralLegendTest::EntryCount));
	TestEqual(TEXT("面板宽度与常量一致"), Panel.W, MineralLegendLayout::PanelWidthPx);
	TestEqual(TEXT("贴左边距"), Panel.X, MineralLegendLayout::MarginPx);
	TestEqual(TEXT("贴上边距"), Panel.Y, MineralLegendLayout::MarginPx);

	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		const FStarMapPanelRect& Row = Rows[Index];
		TestTrue(*FString::Printf(TEXT("第 %d 行在面板内（横向）"), Index),
			Row.X >= Panel.X && Row.X + Row.W <= Panel.X + Panel.W + 0.01f);
		TestTrue(*FString::Printf(TEXT("第 %d 行在面板内（纵向）"), Index),
			Row.Y >= Panel.Y && Row.Y + Row.H <= Panel.Y + Panel.H + 0.01f);
		TestEqual(*FString::Printf(TEXT("第 %d 行高度"), Index),
			Row.H, MineralLegendLayout::RowSlotHeight);
	}

	TestTrue(TEXT("第一行不与第二行重叠"), Rows[0].Y + Rows[0].H <= Rows[1].Y + 0.01f);
	TestTrue(TEXT("第一行与第二行之间没有空隙"),
		FMath::IsNearlyEqual(Rows[0].Y + Rows[0].H, Rows[1].Y, 0.01f));

	// 空元素表也要有一行占位，面板高度按 N = 0 算。
	FStarMapPanelRect EmptyPanel;
	TArray<FStarMapPanelRect> EmptyRows;
	MineralLegendTest::BuildLayout(EmptyPanel, EmptyRows, 0);
	TestEqual(TEXT("空表仍有一行占位"), EmptyRows.Num(), 1);
	TestEqual(TEXT("空表面板高度"), EmptyPanel.H, MineralLegendLayout::PanelHeightPx(0));
	TestTrue(TEXT("空表比 3 行矮"), EmptyPanel.H < Panel.H);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMineralLegendHitTestTest,
	"StellarEra.StarMap.MineralLegend.HitTest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMineralLegendHitTestTest::RunTest(const FString& Parameters)
{
	FStarMapPanelRect Panel;
	TArray<FStarMapPanelRect> Rows;
	MineralLegendTest::BuildLayout(Panel, Rows);

	const FVector2D RowCenter(Rows[1].X + Rows[1].W * 0.5f, Rows[1].Y + Rows[1].H * 0.5f);
	TestEqual(TEXT("行中心命中该行"),
		UStarMapUISystem::HitTestMineralLegendRow(Rows, RowCenter), 1);

	const FVector2D FirstRowCenter(Rows[0].X + Rows[0].W * 0.5f, Rows[0].Y + Rows[0].H * 0.5f);
	TestEqual(TEXT("第一行中心命中第一行"),
		UStarMapUISystem::HitTestMineralLegendRow(Rows, FirstRowCenter), 0);

	// 面板右侧之外：不能命中任何行。
	TestEqual(TEXT("面板外不命中"),
		UStarMapUISystem::HitTestMineralLegendRow(Rows,
			FVector2D(Panel.X + Panel.W + 5.f, RowCenter.Y)), INDEX_NONE);

	// 两行的接缝（上行的下沿 / 下行的上沿）：留了 HitInsetY 缩进，哪一行都不算。
	const FVector2D Seam(RowCenter.X, Rows[1].Y);
	TestEqual(TEXT("行接缝不命中"),
		UStarMapUISystem::HitTestMineralLegendRow(Rows, Seam), INDEX_NONE);

	TestEqual(TEXT("空矩形表返回 INDEX_NONE"),
		UStarMapUISystem::HitTestMineralLegendRow(
			TArray<FStarMapPanelRect>(), RowCenter), INDEX_NONE);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMineralLegendFormatTest,
	"StellarEra.StarMap.MineralLegend.Format",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMineralLegendFormatTest::RunTest(const FString& Parameters)
{
	TArray<FMineralLegendEntry> Entries;
	{
		FMineralLegendEntry& Entry = Entries.AddDefaulted_GetRef();
		Entry.DisplayName = TEXT("铁");
		Entry.Color = FLinearColor(0.6f, 0.5f, 0.4f);
		Entry.Abundance = 0.42f;
		Entry.bVisible = true;
	}
	{
		FMineralLegendEntry& Entry = Entries.AddDefaulted_GetRef();
		Entry.DisplayName = TEXT("铜");
		Entry.Color = FLinearColor(0.7f, 0.4f, 0.2f);
		Entry.Abundance = 0.25f;
		Entry.bVisible = false;
	}
	{
		FMineralLegendEntry& Entry = Entries.AddDefaulted_GetRef();
		Entry.DisplayName = TEXT("金");
		Entry.Color = FLinearColor(1.f, 0.8f, 0.1f);
		Entry.Abundance = 0.05f;
		Entry.bVisible = true;
	}

	const TArray<FString> Lines =
		UStarMapUISystem::FormatMineralLegendLines(TEXT("地球"), Entries);

	TestEqual(TEXT("行数 = 标题 + 元素 + 提示"), Lines.Num(), Entries.Num() + 2);
	TestTrue(TEXT("标题含行星名"), Lines[0].Contains(TEXT("地球")));
	TestTrue(TEXT("可见行标「开」"), Lines[1].Contains(TEXT("开")));
	TestTrue(TEXT("隐藏行标「关」"), Lines[2].Contains(TEXT("关")));
	TestTrue(TEXT("百分比按 0..1 换算"), Lines[1].Contains(TEXT("42%")));
	TestTrue(TEXT("提示含 H 退出"), Lines.Last().Contains(TEXT("H 退出")));
	TestTrue(TEXT("提示含数字键"),
		Lines.Last().Contains(TEXT("数字键")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMineralLegendEmptyTest,
	"StellarEra.StarMap.MineralLegend.Empty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMineralLegendEmptyTest::RunTest(const FString& Parameters)
{
	const TArray<FString> Lines =
		UStarMapUISystem::FormatMineralLegendLines(TEXT("空星"), TArray<FMineralLegendEntry>());

	TestEqual(TEXT("只有标题 / 占位 / 提示"), Lines.Num(), 3);
	TestTrue(TEXT("占位行"), Lines[1].Contains(TEXT("无矿物元素")));
	TestTrue(TEXT("提示行还在"), Lines.Last().Contains(TEXT("H 退出")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMineralLegendSpacingTest,
	"StellarEra.StarMap.MineralLegend.Spacing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMineralLegendSpacingTest::RunTest(const FString& Parameters)
{
	// 断言的是「高度预算与排布同源」：提示分隔线不得压到最后一行的命中带，
	// 提示文字不得越出面板底边的内边距。提示区块的落点全部取自
	// MineralLegendLayout::Hint*，测试不另写一份公式。
	// 三个 N 都跑：1 是占位边界、3 是常规、9 是长表，防「只在某个 N 恰好对上」。
	const int32 EntryCounts[] = { 1, 3, 9 };
	for (const int32 EntryCount : EntryCounts)
	{
		FStarMapPanelRect Panel;
		TArray<FStarMapPanelRect> Rows;
		MineralLegendTest::BuildLayout(Panel, Rows, EntryCount);

		TestEqual(*FString::Printf(TEXT("N=%d 行矩形数量"), EntryCount),
			Rows.Num(), EntryCount);
		TestEqual(*FString::Printf(TEXT("N=%d 面板高度与公式一致"), EntryCount),
			Panel.H, MineralLegendLayout::PanelHeightPx(EntryCount));

		const float LastRowBottom = Rows.Last().Y + Rows.Last().H;
		const float SeparatorY = MineralLegendLayout::HintSeparatorY(Panel);
		TestTrue(*FString::Printf(TEXT("N=%d 提示分隔线不低于最后一行下沿 + 间距"), EntryCount),
			SeparatorY >= LastRowBottom + MineralLegendLayout::SeparatorSpacing - 0.01f);

		const float HintBottom = MineralLegendLayout::HintTextTopY(Panel)
			+ MineralLegendLayout::HintLineHeight;
		TestTrue(*FString::Printf(TEXT("N=%d 提示文字底部不越出面板底边 - 内边距"), EntryCount),
			HintBottom <= Panel.Y + Panel.H - MineralLegendLayout::Padding + 0.01f);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
