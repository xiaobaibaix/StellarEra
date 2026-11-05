// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "CosmosTrajectoryComponent.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// 回归测试：操控飞船时那根实体引力合力箭头。
//
// 这里只测纯函数部分 —— 颜色映射与「圆柱 + 圆锥」两个部件的变换。
// 真正的显示门控（本地玩家操控 + 不在星图）要在 PIE 里验：编辑器世界的
// TickComponent 走的是离线预览分支，碰不到这条路径。

namespace
{
	const FLinearColor TestCold(0.0f, 0.0f, 1.0f);
	const FLinearColor TestHot(1.0f, 0.0f, 0.0f);

	bool ColorsClose(const FLinearColor& A, const FLinearColor& B, float Tolerance = 1.0e-4f)
	{
		return FMath::IsNearlyEqual(A.R, B.R, Tolerance)
			&& FMath::IsNearlyEqual(A.G, B.G, Tolerance)
			&& FMath::IsNearlyEqual(A.B, B.B, Tolerance);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraNetForceArrowColorTest,
	"StellarEra.Trajectory.NetForceArrowColor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraNetForceArrowColorTest::RunTest(const FString& Parameters)
{
	using Comp = UCosmosTrajectoryComponent;

	constexpr double ColdAt = 0.1;
	constexpr double HotAt = 30000.0;

	// 区间外一律饱和：≤ 下端取冷色，≥ 上端取暖色。
	TestTrue(TEXT("零加速度取冷色"),
		ColorsClose(Comp::NetForceArrowColor(0.0, ColdAt, HotAt, TestCold, TestHot), TestCold));
	TestTrue(TEXT("低于下端取冷色"),
		ColorsClose(Comp::NetForceArrowColor(0.01, ColdAt, HotAt, TestCold, TestHot), TestCold));
	TestTrue(TEXT("恰好下端取冷色"),
		ColorsClose(Comp::NetForceArrowColor(ColdAt, ColdAt, HotAt, TestCold, TestHot), TestCold));
	TestTrue(TEXT("恰好上端取暖色"),
		ColorsClose(Comp::NetForceArrowColor(HotAt, ColdAt, HotAt, TestCold, TestHot), TestHot));
	TestTrue(TEXT("超过上端仍夹在暖色"),
		ColorsClose(Comp::NetForceArrowColor(1.0e6, ColdAt, HotAt, TestCold, TestHot), TestHot));

	// 对数刻度：几何中点（√(ColdAt·HotAt)）才应该是冷暖各一半。
	// 线性刻度下这个值会贴着冷色，正是这段代码要避免的行为。
	const double GeometricMean = FMath::Sqrt(ColdAt * HotAt);
	TestTrue(TEXT("几何中点取中间色"),
		ColorsClose(Comp::NetForceArrowColor(GeometricMean, ColdAt, HotAt, TestCold, TestHot),
			FLinearColor(0.5f, 0.0f, 0.5f)));

	// 单调性：加速度越大，暖色分量只增不减。
	{
		float PreviousAlpha = -1.0f;
		bool bMonotonic = true;
		for (double Magnitude = ColdAt; Magnitude <= HotAt; Magnitude *= 10.0)
		{
			const FLinearColor Color =
				Comp::NetForceArrowColor(Magnitude, ColdAt, HotAt, TestCold, TestHot);
			if (Color.R < PreviousAlpha)
			{
				bMonotonic = false;
			}
			PreviousAlpha = Color.R;
		}
		TestTrue(TEXT("颜色随加速度单调变暖"), bMonotonic);
	}

	// 非法输入一律退回冷色，不能算出 NaN 把整根箭头染黑。
	TestTrue(TEXT("下端为 0 时退回冷色"),
		ColorsClose(Comp::NetForceArrowColor(5.0, 0.0, HotAt, TestCold, TestHot), TestCold));
	TestTrue(TEXT("上端不大于下端时退回冷色"),
		ColorsClose(Comp::NetForceArrowColor(5.0, 10.0, 10.0, TestCold, TestHot), TestCold));
	TestTrue(TEXT("负加速度退回冷色"),
		ColorsClose(Comp::NetForceArrowColor(-5.0, ColdAt, HotAt, TestCold, TestHot), TestCold));

	const double NotANumber = std::numeric_limits<double>::quiet_NaN();
	TestTrue(TEXT("NaN 加速度退回冷色"),
		ColorsClose(Comp::NetForceArrowColor(NotANumber, ColdAt, HotAt, TestCold, TestHot),
			TestCold));
	TestTrue(TEXT("NaN 上端退回冷色"),
		ColorsClose(Comp::NetForceArrowColor(5.0, ColdAt, NotANumber, TestCold, TestHot),
			TestCold));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraNetForceArrowTransformTest,
	"StellarEra.Trajectory.NetForceArrowTransforms",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraNetForceArrowTransformTest::RunTest(const FString& Parameters)
{
	using Comp = UCosmosTrajectoryComponent;

	const FVector Anchor(100.0, 200.0, 300.0);
	const FVector Direction = FVector::ForwardVector;   // +X
	constexpr double TotalLength = 1000.0;
	constexpr double ShaftRadius = 40.0;
	constexpr double HeadRadius = 100.0;
	constexpr double HeadFraction = 0.3;

	FTransform Shaft;
	FTransform Head;
	TestTrue(TEXT("合法输入返回 true"),
		Comp::ComputeNetForceArrowTransforms(Anchor, Direction, TotalLength,
			ShaftRadius, HeadRadius, HeadFraction, Shaft, Head));

	// 杆长 700、头长 300，各自以中点定位。
	const FVector ExpectedShaftCenter = Anchor + Direction * 350.0;
	const FVector ExpectedHeadCenter = Anchor + Direction * 850.0;
	TestTrue(TEXT("杆中心在总长的 35% 处"),
		Shaft.GetLocation().Equals(ExpectedShaftCenter, 1.0e-3));
	TestTrue(TEXT("锥头中心在总长的 85% 处"),
		Head.GetLocation().Equals(ExpectedHeadCenter, 1.0e-3));

	// 引擎基础形状是 100 厘米高、底半径 50 厘米，所以缩放 = 实际 / 基准。
	TestTrue(TEXT("杆的 Z 缩放 = 杆长 / 100"),
		FMath::IsNearlyEqual(Shaft.GetScale3D().Z, 7.0, 1.0e-6));
	TestTrue(TEXT("锥头的 Z 缩放 = 头长 / 100"),
		FMath::IsNearlyEqual(Head.GetScale3D().Z, 3.0, 1.0e-6));
	TestTrue(TEXT("杆的径向缩放 = 杆半径 / 50"),
		FMath::IsNearlyEqual(Shaft.GetScale3D().X, 0.8, 1.0e-6)
		&& FMath::IsNearlyEqual(Shaft.GetScale3D().Y, 0.8, 1.0e-6));
	TestTrue(TEXT("锥头的径向缩放 = 头半径 / 50"),
		FMath::IsNearlyEqual(Head.GetScale3D().X, 2.0, 1.0e-6)
		&& FMath::IsNearlyEqual(Head.GetScale3D().Y, 2.0, 1.0e-6));

	// 两个形状都沿 +Z 建模，旋转后本体的 +Z 必须落在箭头的方向上。
	const FVector ShaftAxis = Shaft.GetRotation().GetAxisZ();
	const FVector HeadAxis = Head.GetRotation().GetAxisZ();
	TestTrue(TEXT("杆朝向对齐合力方向"), ShaftAxis.Equals(Direction, 1.0e-4));
	TestTrue(TEXT("锥头朝向对齐合力方向"), HeadAxis.Equals(Direction, 1.0e-4));

	// 非法输入：返回 false，两个变换都是单位变换，调用方据此隐藏箭头。
	struct FBadCase
	{
		const TCHAR* Name;
		double Length;
		double ShaftR;
		double HeadR;
		FVector Dir;
	};
	const FBadCase BadCases[] = {
		{ TEXT("长度为 0"), 0.0, ShaftRadius, HeadRadius, Direction },
		{ TEXT("长度为负"), -100.0, ShaftRadius, HeadRadius, Direction },
		{ TEXT("杆半径为 0"), TotalLength, 0.0, HeadRadius, Direction },
		{ TEXT("方向为零"), TotalLength, ShaftRadius, HeadRadius, FVector::ZeroVector },
	};

	for (const FBadCase& Case : BadCases)
	{
		FTransform BadShaft = FTransform(FRotator(10.0, 20.0, 30.0), FVector(1.0, 2.0, 3.0));
		FTransform BadHead = BadShaft;
		const bool bOk = Comp::ComputeNetForceArrowTransforms(Anchor, Case.Dir,
			Case.Length, Case.ShaftR, Case.HeadR, HeadFraction, BadShaft, BadHead);

		TestFalse(*FString::Printf(TEXT("%s 应返回 false"), Case.Name), bOk);
		TestTrue(*FString::Printf(TEXT("%s 时变换应被清零"), Case.Name),
			BadShaft.Equals(FTransform::Identity) && BadHead.Equals(FTransform::Identity));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraNetForceArrowGateTest,
	"StellarEra.Trajectory.NetForceArrowGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraNetForceArrowGateTest::RunTest(const FString& Parameters)
{
	using Comp = UCosmosTrajectoryComponent;

	// 四个条件缺一不可：组件总开关、本地操控的飞船、不在星图、调试视角开着。
	TestTrue(TEXT("四项全满足才显示"),
		Comp::ShouldShowNetForceArrow(
			/*bDrawArrow=*/true, /*bControlledShip=*/true,
			/*bInStarMap=*/false, /*bDebugViewMode=*/true));

	TestFalse(TEXT("组件总开关关掉时不显示"),
		Comp::ShouldShowNetForceArrow(false, true, false, true));
	TestFalse(TEXT("Owner 不是本地操控的飞船时不显示"),
		Comp::ShouldShowNetForceArrow(true, false, false, true));
	TestFalse(TEXT("星图里不显示"),
		Comp::ShouldShowNetForceArrow(true, true, true, true));
	TestFalse(TEXT("调试视角没开时不显示（不再一上船就常驻）"),
		Comp::ShouldShowNetForceArrow(true, true, false, false));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
