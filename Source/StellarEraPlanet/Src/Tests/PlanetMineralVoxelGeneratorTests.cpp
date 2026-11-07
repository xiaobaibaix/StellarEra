// 生成器实例的冒烟测试：参数真的进了实例、区间查询会整块判空。
// 不建体素世界 —— 建世界要跑 BeginPlay，而 editor world 里不存在 BeginPlay。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Minerals/View/PlanetMineralVoxelGenerator.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	UPlanetMineralVoxelGenerator* MakeConfiguredGenerator(
		bool& bOutNormCalled,
		double IsoValue = 0.22,
		double FadeRange = 0.15,
		const TArray<FBox>& RegionBoxes = { FBox(FVector(-0.2), FVector(0.2)) })
	{
		UPlanetMineralVoxelGenerator* Generator =
			NewObject<UPlanetMineralVoxelGenerator>(GetTransientPackage());

		FPlanetMineralVoxelParams Params;
		Params.RadiusVoxels = 100.0;
		Params.IsoValue = IsoValue;
		Params.FadeRange = FadeRange;
		Params.RegionBoxes = RegionBoxes;
		Params.SampleElementNorm = [&bOutNormCalled](const FVector&) -> float
		{
			bOutNormCalled = true;
			return 0.5f;
		};
		Params.IsSolidAtPlanetVoxel = [](const FIntVector&) { return true; };

		Generator->Configure(Params, FLinearColor::Red);
		return Generator;
	}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralVoxelGeneratorInstanceTest,
	"StellarEra.Planet.MineralView.VoxelGenerator.Instance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralVoxelGeneratorInstanceTest::RunTest(const FString& Parameters)
{
	bool bNormCalled = false;
	UPlanetMineralVoxelGenerator* Generator = MakeConfiguredGenerator(bNormCalled);
	if (!TestNotNull(TEXT("生成器"), Generator))
	{
		return false;
	}

	const TVoxelSharedRef<FVoxelGeneratorInstance> Instance = Generator->GetInstance();
	// TSharedRef::IsValid() 是 private（SharedPointer.h:634，注释明说「shared
	// reference 必须恒有效」），所以只能经由公开的 Get() 取指针。
	if (!TestNotNull(TEXT("实例有效"), &Instance.Get()))
	{
		return false;
	}

	// 盒内（归一化 0.1 → 体素 10）：走完流程，Norm 0.5 > 0.22 → 负
	//
	// FVoxelItemStack 没有默认构造函数（两个构造函数都要 FVoxelPlaceableItemHolder），
	// 用插件自己的空栈静态实例。
	bNormCalled = false;
	const v_flt Inside = Instance->GetValue(10, 10, 10, /*LOD=*/0, FVoxelItemStack::Empty);
	TestTrue(TEXT("区域内的点是实心"), Inside < 0.0f);
	TestTrue(TEXT("区域内调了采样回调"), bNormCalled);

	bNormCalled = false;
	const v_flt Outside = Instance->GetValue(90, 0, 0, /*LOD=*/0, FVoxelItemStack::Empty);
	TestEqual(TEXT("区域外的点是空气"),
		float(Outside), float(PlanetMineralVoxel::AirValue));
	TestFalse(TEXT("区域外不调采样回调"), bNormCalled);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralVoxelGeneratorRangeTest,
	"StellarEra.Planet.MineralView.VoxelGenerator.Range",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralVoxelGeneratorRangeTest::RunTest(const FString& Parameters)
{
	bool bNormCalled = false;
	UPlanetMineralVoxelGenerator* Generator = MakeConfiguredGenerator(bNormCalled);
	if (!TestNotNull(TEXT("生成器"), Generator))
	{
		return false;
	}
	const TVoxelSharedRef<FVoxelGeneratorInstance> Instance = Generator->GetInstance();

	// 远离区域盒的块：区间必须整段在空气侧，插件才会整块跳过、一个体素都不求。
	// 这是矿物体素世界的性能杠杆 —— 返错区间会让每个块都被完整求值。
	const TVoxelRange<v_flt> FarRange = Instance->GetValueRange(
		FVoxelIntBox(FIntVector(500, 500, 500), FIntVector(540, 540, 540)),
		/*LOD=*/0, FVoxelItemStack::Empty);
	TestTrue(TEXT("远处块的区间下界 > 0"), FarRange.Min > 0.0f);

	// 与区域相交的块：区间必须跨过 0，否则插件会跳过整块、矿就没了。
	const TVoxelRange<v_flt> NearRange = Instance->GetValueRange(
		FVoxelIntBox(FIntVector(-15, -15, -15), FIntVector(15, 15, 15)),
		/*LOD=*/0, FVoxelItemStack::Empty);
	TestTrue(TEXT("区域块的区间跨过 0"),
		NearRange.Min < 0.0f && NearRange.Max > 0.0f);

	// 一个元素没有区域（RegionBoxes 为空）= 这颗星球上没有这种矿。
	// 此时任何块都必须是**单值**纯空气区间，插件才会整块跳过。
	bool bUnusedNormCalled = false;
	UPlanetMineralVoxelGenerator* EmptyGenerator = MakeConfiguredGenerator(
		bUnusedNormCalled, /*IsoValue=*/0.05, /*FadeRange=*/0.12, TArray<FBox>());
	if (!TestNotNull(TEXT("无区域的生成器"), EmptyGenerator))
	{
		return false;
	}
	const TVoxelSharedRef<FVoxelGeneratorInstance> EmptyInstance = EmptyGenerator->GetInstance();
	// 故意用与区域盒相交的那一块 —— 判空只应看 RegionBoxes，不看块的坐标。
	const TVoxelRange<v_flt> EmptyRange = EmptyInstance->GetValueRange(
		FVoxelIntBox(FIntVector(-15, -15, -15), FIntVector(15, 15, 15)),
		/*LOD=*/0, FVoxelItemStack::Empty);
	TestEqual(TEXT("没有区域时区间下界 = 空气值"),
		float(EmptyRange.Min), float(PlanetMineralVoxel::AirValue));
	TestEqual(TEXT("没有区域时区间上界 = 空气值"),
		float(EmptyRange.Max), float(PlanetMineralVoxel::AirValue));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralVoxelGeneratorRangeCoversValuesTest,
	"StellarEra.Planet.MineralView.VoxelGenerator.RangeCoversValues",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralVoxelGeneratorRangeCoversValuesTest::RunTest(const FString& Parameters)
{
	// 契约的暴力验证：块内每个体素的真实取值都必须落在本块报出的区间内。
	// 用主预设的真实参数（ColorThreshold 0.05、FadeRange 0.12），正是上界最容易被估窄
	// 的一支：Iso / Fade = 0.4167 < AirValue，只按「矿」的值域算会漏掉区域外恒返 AirValue 的体素。
	bool bNormCalled = false;
	UPlanetMineralVoxelGenerator* Generator = MakeConfiguredGenerator(
		bNormCalled, /*IsoValue=*/0.05, /*FadeRange=*/0.12);
	if (!TestNotNull(TEXT("生成器"), Generator))
	{
		return false;
	}
	const TVoxelSharedRef<FVoxelGeneratorInstance> Instance = Generator->GetInstance();

	// 区域盒是 [-0.2, 0.2] 归一化 = [-20, 20] 体素。8³ 的块放在 [16, 24]，
	// 于是它一半在盒内（矿）、一半在盒外（空气）—— 两种体素都在同一块里。
	const FVoxelIntBox Block(FIntVector(16, 16, 16), FIntVector(24, 24, 24));
	const TVoxelRange<v_flt> Range = Instance->GetValueRange(
		Block, /*LOD=*/0, FVoxelItemStack::Empty);

	int32 Violations = 0;
	int32 SolidCount = 0;
	int32 AirCount = 0;
	FString FirstViolation;
	for (int32 X = Block.Min.X; X <= Block.Max.X; ++X)
	{
		for (int32 Y = Block.Min.Y; Y <= Block.Max.Y; ++Y)
		{
			for (int32 Z = Block.Min.Z; Z <= Block.Max.Z; ++Z)
			{
				const v_flt Value = Instance->GetValue(
					X, Y, Z, /*LOD=*/0, FVoxelItemStack::Empty);

				if (Value < 0.0f)
				{
					++SolidCount;
				}
				else if (Value == v_flt(PlanetMineralVoxel::AirValue))
				{
					++AirCount;
				}

				// 含端点：区间是闭区间。
				if (Value < Range.Min || Value > Range.Max)
				{
					++Violations;
					if (FirstViolation.IsEmpty())
					{
						FirstViolation = FString::Printf(
							TEXT("首个越界体素 (%d,%d,%d)：value=%f，区间=[%f, %f]"),
							X, Y, Z, double(Value), double(Range.Min), double(Range.Max));
					}
				}
			}
		}
	}

	TestEqual(TEXT("块内每个体素的取值都落在本块报出的区间内（含端点）"),
		Violations, 0);
	if (!FirstViolation.IsEmpty())
	{
		AddError(FirstViolation);
	}

	// 没有这一条，「区间覆盖取值」在块只有一种体素时会退化成恒等式。
	TestTrue(TEXT("这一块里确实既有矿（负值）也有空气（AirValue）"),
		SolidCount > 0 && AirCount > 0);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
