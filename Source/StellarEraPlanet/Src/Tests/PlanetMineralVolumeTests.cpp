// 矿量区域采样的纯函数测试：并集去重、边界条件、谓词过滤。
//
// 这里全是**数值**断言，不是「没崩就算过」：I3 修的就是「区域 AABB 重叠导致
// 重复计数」，只有数字对得上才算真的修好。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PlanetMineralVolume.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FPlanetMineralRegionBox MakeVolumeTestBox(
		double MinX, double MinY, double MinZ,
		double MaxX, double MaxY, double MaxZ)
	{
		FPlanetMineralRegionBox Box;
		Box.Min = FVector3d(MinX, MinY, MinZ);
		Box.Max = FVector3d(MaxX, MaxY, MaxZ);
		return Box;
	}

	/** 恒真的谓词：只数格子，不关心矿。 */
	bool AlwaysSolid(const FVector3d& Point)
	{
		return true;
	}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralVolumeSingleBoxTest,
	"StellarEra.Planet.MineralView.VolumeSingleBox",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralVolumeSingleBoxTest::RunTest(const FString& Parameters)
{
	// 单个 [0,10]³ 的盒子，步长 1：每个轴 11 个采样点（含两端）。
	TArray<FPlanetMineralRegionBox> Boxes;
	Boxes.Add(MakeVolumeTestBox(0., 0., 0., 10., 10., 10.));

	TestEqual(TEXT("单盒 11³ = 1331"),
		CountMergedBoxSamples(Boxes, 1.0, AlwaysSolid), int64(1331));

	// 谓词只看得到一半：X < 5 → 5 个 X 采样点。
	const int64 Half = CountMergedBoxSamples(Boxes, 1.0,
		[](const FVector3d& Point) { return Point.X < 5.0; });
	TestEqual(TEXT("谓词 X < 5 时 5 × 11 × 11 = 605"), Half, int64(605));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralVolumeDedupTest,
	"StellarEra.Planet.MineralView.VolumeDedup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralVolumeDedupTest::RunTest(const FString& Parameters)
{
	// 两个完全相同的盒子：重叠区只能数一遍。逐盒各扫一遍的实现会给出 2662。
	TArray<FPlanetMineralRegionBox> Duplicate;
	Duplicate.Add(MakeVolumeTestBox(0., 0., 0., 10., 10., 10.));
	Duplicate.Add(MakeVolumeTestBox(0., 0., 0., 10., 10., 10.));

	TestEqual(TEXT("两个相同的盒子仍是 1331，不是 2662"),
		CountMergedBoxSamples(Duplicate, 1.0, AlwaysSolid), int64(1331));

	// 重叠但不相同：[0,10]³ 与 [5,15]³，交集 [5,10]³ = 6³ = 216。
	TArray<FPlanetMineralRegionBox> Overlapping;
	Overlapping.Add(MakeVolumeTestBox(0., 0., 0., 10., 10., 10.));
	Overlapping.Add(MakeVolumeTestBox(5., 5., 5., 15., 15., 15.));

	TestEqual(TEXT("并集 = 1331 + 1331 − 216 = 2446"),
		CountMergedBoxSamples(Overlapping, 1.0, AlwaysSolid), int64(2446));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralVolumeEdgeCasesTest,
	"StellarEra.Planet.MineralView.VolumeEdgeCases",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralVolumeEdgeCasesTest::RunTest(const FString& Parameters)
{
	TArray<FPlanetMineralRegionBox> Boxes;
	Boxes.Add(MakeVolumeTestBox(0., 0., 0., 10., 10., 10.));

	TestEqual(TEXT("空盒子表 → 0"),
		CountMergedBoxSamples({}, 1.0, AlwaysSolid), int64(0));
	TestEqual(TEXT("步长 0 → 0"),
		CountMergedBoxSamples(Boxes, 0.0, AlwaysSolid), int64(0));
	TestEqual(TEXT("负步长 → 0"),
		CountMergedBoxSamples(Boxes, -1.0, AlwaysSolid), int64(0));

	// Min > Max：不是盒子，跳过而不是拿它去撑并集。
	TArray<FPlanetMineralRegionBox> Invalid;
	Invalid.Add(MakeVolumeTestBox(10., 10., 10., 0., 0., 0.));
	TestEqual(TEXT("唯一一个盒子无效 → 0"),
		CountMergedBoxSamples(Invalid, 1.0, AlwaysSolid), int64(0));

	// 一真一假：假的那个（Min > Max）既不计入，也不能把并集撑大。
	TArray<FPlanetMineralRegionBox> Mixed;
	Mixed.Add(MakeVolumeTestBox(0., 0., 0., 10., 10., 10.));
	Mixed.Add(MakeVolumeTestBox(200., 200., 200., 100., 100., 100.));
	TestEqual(TEXT("一真一假 → 等于只有真盒子时的 1331"),
		CountMergedBoxSamples(Mixed, 1.0, AlwaysSolid), int64(1331));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralVolumePredicateTest,
	"StellarEra.Planet.MineralView.VolumePredicate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralVolumePredicateTest::RunTest(const FString& Parameters)
{
	// Z ≤ 5：6 层（0..5），X/Y 各 11 个采样点。
	TArray<FPlanetMineralRegionBox> Boxes;
	Boxes.Add(MakeVolumeTestBox(0., 0., 0., 10., 10., 10.));

	TestEqual(TEXT("谓词 Z ≤ 5 时 11 × 11 × 6 = 726"),
		CountMergedBoxSamples(Boxes, 1.0,
			[](const FVector3d& Point) { return Point.Z <= 5.0; }), int64(726));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
