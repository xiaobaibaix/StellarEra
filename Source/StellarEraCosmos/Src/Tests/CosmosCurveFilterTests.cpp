// Copyright Voxel Plugin SAS. All Rights Reserved.
// 轨迹滤波纯函数：闭合误差分摊、平滑、按角度重采样、帧间混合。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "CosmosCurveFilter.h"
#include "Algo/Reverse.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** 半径 R 的圆，N 个不重复点 + 尾点；Phase 是起点角度，Gap 让末点偏离起点。 */
	TArray<FVector3d> MakeCircle(int32 N, double R, double Phase = 0.0, const FVector3d& Gap = FVector3d::ZeroVector)
	{
		TArray<FVector3d> Out;
		for (int32 i = 0; i <= N; ++i)
		{
			const double A = Phase + 2.0 * PI * double(i) / double(N);
			Out.Add(FVector3d(R * FMath::Cos(A), R * FMath::Sin(A), 0.0));
		}
		Out.Last() += Gap;
		return Out;
	}

	double MaxTurnAngle(const TArray<FVector3d>& P)
	{
		double MaxAngle = 0.0;
		for (int32 i = 1; i + 1 < P.Num(); ++i)
		{
			const FVector3d A = (P[i] - P[i - 1]).GetSafeNormal();
			const FVector3d B = (P[i + 1] - P[i]).GetSafeNormal();
			MaxAngle = FMath::Max(MaxAngle, FMath::Acos(FMath::Clamp(FVector3d::DotProduct(A, B), -1.0, 1.0)));
		}
		return MaxAngle;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraCurveFilterTest,
	"StellarEra.Cosmos.CurveFilter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraCurveFilterTest::RunTest(const FString& Parameters)
{
	// ── 闭合误差分摊：首尾逐位相等，每点偏移 ≤ |Δ| ──
	{
		const FVector3d Gap(0.0, 3.0, 0.0);
		const TArray<FVector3d> Original = MakeCircle(64, 10.0, 0.0, Gap);
		TArray<FVector3d> Points = Original;
		CosmosCurveFilter::DistributeClosureError(Points);
		TestTrue(TEXT("分摊后首尾逐位相等"), Points.Last() == Points[0]);
		double MaxShift = 0.0;
		for (int32 i = 0; i < Points.Num(); ++i)
		{
			MaxShift = FMath::Max(MaxShift, (Points[i] - Original[i]).Size());
		}
		// 用实测误差而不是 Gap：UE 的 PI 是 float 常量，2π 处的圆本身就差 ~1.7e-6。
		const double ActualError = (Original.Last() - Original[0]).Size();
		TestTrue(TEXT("每点偏移不超过闭合误差"), MaxShift <= ActualError + 1.0e-9);
		TestTrue(TEXT("首点不动"), Points[0] == Original[0]);
	}

	// ── 闭环平滑：保持闭合、磨掉接缝折角 ──
	{
		TArray<FVector3d> Points = MakeCircle(64, 10.0);
		Points[0] += FVector3d(1.5, 0.0, 0.0);   // 接缝处一个尖角
		Points.Last() = Points[0];
		const double Before = MaxTurnAngle(Points);
		CosmosCurveFilter::SmoothClosed(Points, 4);
		TestTrue(TEXT("平滑后仍首尾逐位相等"), Points.Last() == Points[0]);
		// 接缝处的转角要跨首尾看：把首点之后的点接到尾部再量。
		TArray<FVector3d> Wrapped = Points;
		Wrapped.Add(Points[1]);
		Wrapped.Insert(Points[Points.Num() - 2], 0);
		TestTrue(TEXT("最大转角明显变小"), MaxTurnAngle(Wrapped) < 0.5 * Before);
	}

	// ── 开放平滑：首末点不动 ──
	{
		TArray<FVector3d> Points;
		for (int32 i = 0; i < 20; ++i)
		{
			Points.Add(FVector3d(double(i), (i % 2) ? 1.0 : -1.0, 0.0));   // 锯齿
		}
		const FVector3d First = Points[0];
		const FVector3d Last = Points.Last();
		CosmosCurveFilter::SmoothOpen(Points, 3);
		TestTrue(TEXT("开放平滑首点不动"), Points[0] == First);
		TestTrue(TEXT("开放平滑末点不动"), Points.Last() == Last);
		TestTrue(TEXT("锯齿被压平"), FMath::Abs(Points[10].Y) < 0.5);
	}

	// ── 按角度重采样：起点相位不同的两条同一圆，重采样后逐点对应 ──
	{
		TArray<FVector3d> A, B;
		FVector3d NormalA, NormalB;
		TestTrue(TEXT("重采样 A 成功"),
			CosmosCurveFilter::ResampleClosedByAngle(MakeCircle(200, 10.0, 0.3), 128, A, NormalA));
		TestTrue(TEXT("重采样 B 成功"),
			CosmosCurveFilter::ResampleClosedByAngle(MakeCircle(200, 10.0, 2.1), 128, B, NormalB));
		TestEqual(TEXT("点数 = NumOut + 尾点"), A.Num(), 129);
		TestTrue(TEXT("重采样结果首尾相等"), A.Last() == A[0]);
		double MaxDiff = 0.0;
		for (int32 i = 0; i < A.Num(); ++i)
		{
			MaxDiff = FMath::Max(MaxDiff, (A[i] - B[i]).Size());
		}
		TestTrue(TEXT("不同起点的同一圆逐点对应"), MaxDiff < 0.05);
		TestTrue(TEXT("法向一致"), FVector3d::DotProduct(NormalA, NormalB) > 0.999);

		// 逆行（顺时针）也要能重采样。
		TArray<FVector3d> Reversed = MakeCircle(200, 10.0);
		Algo::Reverse(Reversed);
		TArray<FVector3d> R;
		FVector3d NormalR;
		TestTrue(TEXT("逆行圆也能重采样"),
			CosmosCurveFilter::ResampleClosedByAngle(Reversed, 64, R, NormalR));
		TestTrue(TEXT("逆行法向朝 -Z"), NormalR.Z < -0.99);
	}

	// ── 帧间混合与系数 ──
	{
		TArray<FVector3d> Current = { FVector3d(0.0), FVector3d(0.0) };
		const TArray<FVector3d> Target = { FVector3d(10.0), FVector3d(-10.0) };
		TestTrue(TEXT("点数相同可混合"), CosmosCurveFilter::BlendToward(Current, Target, 0.25));
		TestTrue(TEXT("混合按比例走"), Current[0].Equals(FVector3d(2.5), 1.0e-9));

		TArray<FVector3d> Mismatch = { FVector3d(0.0) };
		TestFalse(TEXT("点数不同拒绝混合"), CosmosCurveFilter::BlendToward(Mismatch, Target, 0.5));

		TestEqual(TEXT("Tau=0 不平滑"), CosmosCurveFilter::SmoothingAlpha(0.016, 0.0), 1.0);
		// 两帧 dt 与一帧 2dt 的累计效果相同 —— 与帧率无关。
		const double A1 = CosmosCurveFilter::SmoothingAlpha(0.016, 0.2);
		const double A2 = CosmosCurveFilter::SmoothingAlpha(0.032, 0.2);
		TestEqual(TEXT("系数与帧率无关"), 1.0 - (1.0 - A1) * (1.0 - A1), A2, 1.0e-12);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
