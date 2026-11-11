// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "CosmosCurveFilter.h"

namespace
{
	/** 把角度差折到 (−π, π]。 */
	double WrapAngle(double Angle)
	{
		Angle = FMath::Fmod(Angle + PI, 2.0 * PI);
		if (Angle <= 0.0)
		{
			Angle += 2.0 * PI;
		}
		return Angle - PI;
	}
}

void CosmosCurveFilter::DistributeClosureError(TArray<FVector3d>& Points)
{
	const int32 Num = Points.Num();
	if (Num < 2)
	{
		return;
	}

	const FVector3d Error = Points.Last() - Points[0];
	const double InvLast = 1.0 / double(Num - 1);
	for (int32 Index = 1; Index < Num; ++Index)
	{
		Points[Index] -= Error * (double(Index) * InvLast);
	}
	// 浮点上 P_last − Δ 不一定逐位等于 P_0，直接写回。
	Points.Last() = Points[0];
}

void CosmosCurveFilter::SmoothClosed(TArray<FVector3d>& Points, int32 Iterations)
{
	const int32 Unique = Points.Num() - 1;
	if (Unique < 3 || Iterations <= 0)
	{
		return;
	}

	TArray<FVector3d> Temp;
	Temp.SetNumUninitialized(Unique);
	for (int32 Pass = 0; Pass < Iterations; ++Pass)
	{
		for (int32 Index = 0; Index < Unique; ++Index)
		{
			const FVector3d& Prev = Points[(Index + Unique - 1) % Unique];
			const FVector3d& Next = Points[(Index + 1) % Unique];
			Temp[Index] = 0.25 * Prev + 0.5 * Points[Index] + 0.25 * Next;
		}
		FMemory::Memcpy(Points.GetData(), Temp.GetData(), sizeof(FVector3d) * Unique);
	}
	Points.Last() = Points[0];
}

void CosmosCurveFilter::SmoothOpen(TArray<FVector3d>& Points, int32 Iterations)
{
	const int32 Num = Points.Num();
	if (Num < 3 || Iterations <= 0)
	{
		return;
	}

	TArray<FVector3d> Temp = Points;
	for (int32 Pass = 0; Pass < Iterations; ++Pass)
	{
		for (int32 Index = 1; Index < Num - 1; ++Index)
		{
			Temp[Index] = 0.25 * Points[Index - 1] + 0.5 * Points[Index] + 0.25 * Points[Index + 1];
		}
		Swap(Points, Temp);
	}
	// 迭代奇数次时最后一轮写在 Temp 上又被换回来，首末点两边都没动过，这里不用补。
}

bool CosmosCurveFilter::ResampleClosedByAngle(const TArray<FVector3d>& Loop, int32 NumOut,
	TArray<FVector3d>& Out, FVector3d& OutNormal)
{
	Out.Reset();
	OutNormal = FVector3d::ZeroVector;

	const int32 Unique = Loop.Num() - 1;
	if (Unique < 3 || NumOut < 3)
	{
		return false;
	}

	FVector3d Normal = FVector3d::ZeroVector;
	for (int32 Index = 0; Index < Unique; ++Index)
	{
		Normal += FVector3d::CrossProduct(Loop[Index], Loop[Index + 1]);
	}
	if (!Normal.Normalize(1.0e-30))
	{
		return false;
	}

	FVector3d Axis = FVector3d::XAxisVector - FVector3d::DotProduct(FVector3d::XAxisVector, Normal) * Normal;
	if (Axis.SizeSquared() < 0.01)
	{
		Axis = FVector3d::YAxisVector - FVector3d::DotProduct(FVector3d::YAxisVector, Normal) * Normal;
	}
	Axis.Normalize();
	const FVector3d Side = FVector3d::CrossProduct(Normal, Axis);

	auto AngleOf = [&Axis, &Side](const FVector3d& P)
	{
		return FMath::Atan2(FVector3d::DotProduct(P, Side), FVector3d::DotProduct(P, Axis));
	};

	// 展开角：沿运动方向必须严格递增，总共恰好 2π。
	TArray<double> Angles;
	Angles.SetNumUninitialized(Unique + 1);
	double Previous = AngleOf(Loop[0]);
	Angles[0] = Previous < 0.0 ? Previous + 2.0 * PI : Previous;
	for (int32 Index = 1; Index <= Unique; ++Index)
	{
		const double Current = AngleOf(Loop[Index]);
		const double Delta = WrapAngle(Current - Previous);
		if (!(Delta > 0.0))
		{
			return false;
		}
		Angles[Index] = Angles[Index - 1] + Delta;
		Previous = Current;
	}
	if (FMath::Abs(Angles[Unique] - Angles[0] - 2.0 * PI) > 0.5)
	{
		return false;
	}

	Out.Reserve(NumOut + 1);
	for (int32 Sample = 0; Sample < NumOut; ++Sample)
	{
		double Target = 2.0 * PI * double(Sample) / double(NumOut);
		if (Target < Angles[0])
		{
			Target += 2.0 * PI;
		}

		// Angles 单调，二分找第一个 > Target 的点。
		int32 Low = 1;
		int32 High = Unique;
		while (Low < High)
		{
			const int32 Mid = (Low + High) / 2;
			if (Angles[Mid] > Target)
			{
				High = Mid;
			}
			else
			{
				Low = Mid + 1;
			}
		}
		const double Span = Angles[Low] - Angles[Low - 1];
		const double Alpha = Span > 0.0 ? FMath::Clamp((Target - Angles[Low - 1]) / Span, 0.0, 1.0) : 0.0;
		Out.Add(FMath::Lerp(Loop[Low - 1], Loop[Low], Alpha));
	}

	const FVector3d First = Out[0];
	Out.Add(First);
	OutNormal = Normal;
	return true;
}

bool CosmosCurveFilter::BlendToward(TArray<FVector3d>& Current, const TArray<FVector3d>& Target,
	double Alpha)
{
	if (Current.Num() != Target.Num() || Current.Num() == 0)
	{
		return false;
	}

	Alpha = FMath::Clamp(Alpha, 0.0, 1.0);
	for (int32 Index = 0; Index < Current.Num(); ++Index)
	{
		Current[Index] += (Target[Index] - Current[Index]) * Alpha;
	}
	return true;
}

double CosmosCurveFilter::SmoothingAlpha(double DeltaSeconds, double TauSeconds)
{
	if (!(TauSeconds > 0.0))
	{
		return 1.0;
	}
	return 1.0 - FMath::Exp(-FMath::Max(DeltaSeconds, 0.0) / TauSeconds);
}
