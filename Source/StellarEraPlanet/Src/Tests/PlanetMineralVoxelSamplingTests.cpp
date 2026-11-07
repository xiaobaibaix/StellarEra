// 采样纯函数的单元测试：早退顺序、密度门、阈值映射。
// 全部用注入的回调，不建体素世界、不读资产。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Minerals/View/PlanetMineralVoxelSampling.h"
#include "VoxelValue.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** 一个恒定返回 Norm 的采样回调，并记录被调了几次。 */
	struct FConstantNormSampler
	{
		float Norm = 0.f;
		mutable int32 Calls = 0;

		FPlanetMineralVoxelParams MakeParams(double IsoValue = 0.22, double FadeRange = 0.15) const
		{
			FPlanetMineralVoxelParams Params;
			Params.RadiusVoxels = 100.0;
			Params.IsoValue = IsoValue;
			Params.FadeRange = FadeRange;
			Params.RegionBoxes.Add(FBox(FVector(-10.0), FVector(10.0)));
			Params.SampleElementNorm = [this](const FVector&) -> float
			{
				++Calls;
				return Norm;
			};
			Params.IsSolidAtPlanetVoxel = [](const FIntVector&) { return true; };
			return Params;
		}
	};
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralVoxelRegionEarlyOutTest,
	"StellarEra.Planet.MineralView.VoxelSample.RegionEarlyOut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralVoxelRegionEarlyOutTest::RunTest(const FString& Parameters)
{
	FConstantNormSampler Sampler;
	Sampler.Norm = 1.0f; // 就算地质场说这里是纯矿
	const FPlanetMineralVoxelParams Params = Sampler.MakeParams();

	// 区域盒是归一化空间的 [-10, 10]，半径 100 体素 → 体素 [-1000, 1000]。
	TestTrue(TEXT("盒内：Norm 1 > 阈值 → 负值（实心）"),
		PlanetMineralVoxel::EvaluateVoxel(Params, FIntVector(0, 0, 0)) < 0.0);
	TestTrue(TEXT("盒内确实调了地质场"), Sampler.Calls > 0);

	Sampler.Calls = 0;
	const double Outside = PlanetMineralVoxel::EvaluateVoxel(
		Params, FIntVector(5000, 0, 0)); // 归一化 50，盒外
	TestEqual(TEXT("盒外恒为空气"), Outside, PlanetMineralVoxel::AirValue);
	TestEqual(TEXT("盒外不调地质场"), Sampler.Calls, 0);

	FPlanetMineralVoxelParams NoRegions = Params;
	NoRegions.RegionBoxes.Reset();
	TestEqual(TEXT("空区域表恒为空气"),
		PlanetMineralVoxel::EvaluateVoxel(NoRegions, FIntVector(0, 0, 0)),
		PlanetMineralVoxel::AirValue);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralVoxelDensityGateTest,
	"StellarEra.Planet.MineralView.VoxelSample.DensityGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralVoxelDensityGateTest::RunTest(const FString& Parameters)
{
	FConstantNormSampler Sampler;
	Sampler.Norm = 1.0f;
	FPlanetMineralVoxelParams Params = Sampler.MakeParams();

	// 行星在那里是空气（被挖空）→ 矿不存在，且地质场根本不用调。
	Params.IsSolidAtPlanetVoxel = [](const FIntVector&) { return false; };
	Sampler.Calls = 0;
	TestEqual(TEXT("行星空气 → 矿也是空气"),
		PlanetMineralVoxel::EvaluateVoxel(Params, FIntVector(0, 0, 0)),
		PlanetMineralVoxel::AirValue);
	TestEqual(TEXT("密度门早退，不调地质场"), Sampler.Calls, 0);

	Params.IsSolidAtPlanetVoxel = [](const FIntVector&) { return true; };
	TestTrue(TEXT("行星实心 → 有矿"),
		PlanetMineralVoxel::EvaluateVoxel(Params, FIntVector(0, 0, 0)) < 0.0);

	// 回调未绑定 → 视作实心（不因为漏绑就整颗星没矿）
	Params.IsSolidAtPlanetVoxel = nullptr;
	TestTrue(TEXT("未绑密度回调时视作实心"),
		PlanetMineralVoxel::EvaluateVoxel(Params, FIntVector(0, 0, 0)) < 0.0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralVoxelThresholdTest,
	"StellarEra.Planet.MineralView.VoxelSample.Threshold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralVoxelThresholdTest::RunTest(const FString& Parameters)
{
	FConstantNormSampler Sampler;
	const FPlanetMineralVoxelParams Params = Sampler.MakeParams(/*IsoValue=*/0.22, /*FadeRange=*/0.15);

	// 容差取 1e-6 而不是 1e-9：SampleElementNorm 返回 float，0.22f 与双精度 0.22
	// 差 1.2e-9，除以 FadeRange 0.15 后放大到 ~8e-9，1e-9 会误判。
	Sampler.Norm = 0.22f;
	TestEqual(TEXT("Norm == 阈值 → 0（等值面）"),
		PlanetMineralVoxel::EvaluateVoxel(Params, FIntVector(0, 0, 0)), 0.0, 1e-6);

	Sampler.Norm = 0.30f;
	const double Above = PlanetMineralVoxel::EvaluateVoxel(Params, FIntVector(0, 0, 0));
	TestTrue(TEXT("Norm 高于阈值 → 负（实心）"), Above < 0.0);
	TestEqual(TEXT("高于阈值的量按 FadeRange 缩放"),
		Above, (0.22 - 0.30) / 0.15, 1e-6);

	Sampler.Norm = 0.10f;
	TestTrue(TEXT("Norm 低于阈值 → 正（空气）"),
		PlanetMineralVoxel::EvaluateVoxel(Params, FIntVector(0, 0, 0)) > 0.0);

	// FadeRange 是资产上可编辑的值，0 会让除法爆掉
	FPlanetMineralVoxelParams ZeroFade = Params;
	ZeroFade.FadeRange = 0.0;
	Sampler.Norm = 0.30f;
	const double Guarded = PlanetMineralVoxel::EvaluateVoxel(ZeroFade, FIntVector(0, 0, 0));
	TestTrue(TEXT("FadeRange 0 时结果仍然有限"), FMath::IsFinite(Guarded));
	TestTrue(TEXT("FadeRange 0 时符号仍然正确"), Guarded < 0.0);

	// Fade 下限 1e-4 与 |Iso - Norm| ≤ 1 一起给出的可达边界就是 ±1e4。
	// 这里钉的是这个边界本身，不是 int16 夹子（夹子按当前 Fade 下限取不到）。
	FPlanetMineralVoxelParams Bounds = Params;
	Bounds.FadeRange = 0.0;
	Bounds.IsoValue = 0.0;
	Sampler.Norm = 1.0f;
	TestEqual(TEXT("Fade 下限 + 最大差值 → -1e4"),
		PlanetMineralVoxel::EvaluateVoxel(Bounds, FIntVector(0, 0, 0)), -10000.0, 1e-6);

	FPlanetMineralVoxelParams NoSampler = Params;
	NoSampler.SampleElementNorm = nullptr;
	TestEqual(TEXT("没有采样回调时恒为空气"),
		PlanetMineralVoxel::EvaluateVoxel(NoSampler, FIntVector(0, 0, 0)),
		PlanetMineralVoxel::AirValue);

	FPlanetMineralVoxelParams NoRadius = Params;
	NoRadius.RadiusVoxels = 0.0;
	TestEqual(TEXT("RadiusVoxels 0 时恒为空气"),
		PlanetMineralVoxel::EvaluateVoxel(NoRadius, FIntVector(0, 0, 0)),
		PlanetMineralVoxel::AirValue);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralVoxelRegionBoxTest,
	"StellarEra.Planet.MineralView.VoxelSample.RegionBox",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralVoxelRegionBoxTest::RunTest(const FString& Parameters)
{
	TArray<FBox> Boxes;
	Boxes.Add(FBox(FVector(-1.0, -0.5, -0.5), FVector(0.0, 0.5, 0.5)));
	Boxes.Add(FBox(FVector(0.5, -0.5, -0.5), FVector(1.0, 0.5, 0.5)));

	TestTrue(TEXT("第一个盒内"),
		PlanetMineralVoxel::IsInsideAnyRegion(Boxes, FVector(-0.5, 0.0, 0.0)));
	TestTrue(TEXT("第二个盒内"),
		PlanetMineralVoxel::IsInsideAnyRegion(Boxes, FVector(0.75, 0.0, 0.0)));
	TestFalse(TEXT("两个盒之间的空隙"),
		PlanetMineralVoxel::IsInsideAnyRegion(Boxes, FVector(0.25, 0.0, 0.0)));

	TArray<FBox> NoBoxes;
	TestFalse(TEXT("空盒数组恒为假"),
		PlanetMineralVoxel::IsInsideAnyRegion(NoBoxes, FVector(0.0, 0.0, 0.0)));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
