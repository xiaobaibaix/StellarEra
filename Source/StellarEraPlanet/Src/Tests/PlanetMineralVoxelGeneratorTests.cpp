// 生成器实例的冒烟测试：参数真的进了实例、区间查询会整块判空。
//
// 不建体素世界 —— 建世界要跑 BeginPlay，而 editor world 里不存在 BeginPlay。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Minerals/View/PlanetMineralVoxelGenerator.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	UPlanetMineralVoxelGenerator* MakeConfiguredGenerator(bool& bOutNormCalled)
	{
		UPlanetMineralVoxelGenerator* Generator =
			NewObject<UPlanetMineralVoxelGenerator>(GetTransientPackage());

		FPlanetMineralVoxelParams Params;
		Params.RadiusVoxels = 100.0;
		Params.IsoValue = 0.22;
		Params.FadeRange = 0.15;
		Params.RegionBoxes.Add(FBox(FVector(-0.2), FVector(0.2)));
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

	// 盒外（归一化 0.9 → 体素 90）：恒空气
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

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
