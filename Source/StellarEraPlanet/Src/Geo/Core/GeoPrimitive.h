// 形态原语：扁平结构体 + switch，进快照、在体素线程上求值，所以不用虚函数。
// M1 只有团块（轴对齐椭球）；其余 6 种原语在用到它的里程碑加 case。
#pragma once
#include "CoreMinimal.h"

struct FGeoRange
{
	float Lo = 0.f;
	float Hi = 0.f;
	static FGeoRange Point(float V) { return { V, V }; }
	bool Contains(float V) const { return V >= Lo && V <= Hi; }
};

enum class EGeoPrimitiveKind : uint8
{
	Blob,
	/** 任意朝向的椭球：层状体、透镜体、脉体都靠它贴着球面躺平或沿走向排开。 */
	OrientedBlob,
};

struct FGeoPrimitive
{
	EGeoPrimitiveKind Kind = EGeoPrimitiveKind::Blob;
	uint8 MineralId = 0;
	uint8 LithologyId = 0;
	uint8 Grade = 0;
	uint8 Priority = 0;
	FVector3f Center = FVector3f::ZeroVector;
	FVector3f Radii = FVector3f::OneVector;
	/** 只对 OrientedBlob 有意义：局部轴 → 世界。 */
	FQuat4f Rotation = FQuat4f::Identity;
	/** 软边宽度占半径比例：0.02–0.05 是硬接触（侵入体），0.2 是浸染状软边。 */
	float Hard = 0.05f;
	FBox3f Bounds = FBox3f(ForceInit);

	static FGeoPrimitive MakeBlob(const FVector3f& InCenter, const FVector3f& InRadii, float InHard)
	{
		FGeoPrimitive P;
		P.Kind = EGeoPrimitiveKind::Blob;
		P.Center = InCenter;
		P.Radii = InRadii;
		P.Hard = InHard;
		P.Bounds = FBox3f(InCenter - InRadii, InCenter + InRadii);
		return P;
	}

	static FGeoPrimitive MakeOrientedBlob(const FVector3f& InCenter, const FVector3f& InRadii, const FQuat4f& InRotation, float InHard)
	{
		FGeoPrimitive P;
		P.Kind = EGeoPrimitiveKind::OrientedBlob;
		P.Center = InCenter;
		P.Radii = InRadii;
		P.Rotation = InRotation.GetNormalized();
		P.Hard = InHard;
		// 旋转椭球的轴对齐包围盒：每个世界轴的半宽 = Σ |局部轴分量| × 对应半径。
		const FVector3f AX = P.Rotation.RotateVector(FVector3f::XAxisVector) * InRadii.X;
		const FVector3f AY = P.Rotation.RotateVector(FVector3f::YAxisVector) * InRadii.Y;
		const FVector3f AZ = P.Rotation.RotateVector(FVector3f::ZAxisVector) * InRadii.Z;
		const FVector3f Ext(FMath::Abs(AX.X) + FMath::Abs(AY.X) + FMath::Abs(AZ.X),
			FMath::Abs(AX.Y) + FMath::Abs(AY.Y) + FMath::Abs(AZ.Y), FMath::Abs(AX.Z) + FMath::Abs(AY.Z) + FMath::Abs(AZ.Z));
		P.Bounds = FBox3f(InCenter - Ext, InCenter + Ext);
		return P;
	}
};

/** 归一化距离 D → 强度：D ≤ 1−Hard 为 1，D ≥ 1 为 0，单调不增（值域推导依赖单调）。 */
FORCEINLINE float GeoBlobFalloff(float D, float Hard)
{
	return 1.f - FMath::SmoothStep(1.f - Hard, 1.f, D);
}

FORCEINLINE float GeoPrimitiveEvaluate(const FGeoPrimitive& P, const FVector3f& X)
{
	switch (P.Kind)
	{
	case EGeoPrimitiveKind::Blob:
		return GeoBlobFalloff(((X - P.Center) / P.Radii).Size(), P.Hard);
	case EGeoPrimitiveKind::OrientedBlob:
		return GeoBlobFalloff((P.Rotation.UnrotateVector(X - P.Center) / P.Radii).Size(), P.Hard);
	}
	return 0.f;
}

FORCEINLINE FGeoRange GeoPrimitiveRange(const FGeoPrimitive& P, const FBox3f& Box)
{
	switch (P.Kind)
	{
	case EGeoPrimitiveKind::Blob:
	{
		// 盒子变到椭球的单位球空间仍是轴对齐盒：最近点取钳位，最远点取离原点远的那一角。
		const FVector3f Lo = (Box.Min - P.Center) / P.Radii;
		const FVector3f Hi = (Box.Max - P.Center) / P.Radii;
		const FVector3f Near(FMath::Clamp(0.f, Lo.X, Hi.X), FMath::Clamp(0.f, Lo.Y, Hi.Y), FMath::Clamp(0.f, Lo.Z, Hi.Z));
		const FVector3f Far(FMath::Max(FMath::Abs(Lo.X), FMath::Abs(Hi.X)),
			FMath::Max(FMath::Abs(Lo.Y), FMath::Abs(Hi.Y)), FMath::Max(FMath::Abs(Lo.Z), FMath::Abs(Hi.Z)));
		return { GeoBlobFalloff(Far.Size(), P.Hard), GeoBlobFalloff(Near.Size(), P.Hard) };
	}
	case EGeoPrimitiveKind::OrientedBlob:
	{
		// 用包围球夹：最近点距 / 最长半轴 ≤ 真归一化距离 ≤ 最远点距 / 最短半轴。保守但不漏。
		const FVector3f Near(FMath::Clamp(P.Center.X, Box.Min.X, Box.Max.X),
			FMath::Clamp(P.Center.Y, Box.Min.Y, Box.Max.Y), FMath::Clamp(P.Center.Z, Box.Min.Z, Box.Max.Z));
		const FVector3f Far(FMath::Max(FMath::Abs(Box.Min.X - P.Center.X), FMath::Abs(Box.Max.X - P.Center.X)),
			FMath::Max(FMath::Abs(Box.Min.Y - P.Center.Y), FMath::Abs(Box.Max.Y - P.Center.Y)),
			FMath::Max(FMath::Abs(Box.Min.Z - P.Center.Z), FMath::Abs(Box.Max.Z - P.Center.Z)));
		return { GeoBlobFalloff(Far.Size() / P.Radii.GetMin(), P.Hard), GeoBlobFalloff((Near - P.Center).Size() / P.Radii.GetMax(), P.Hard) };
	}
	}
	return FGeoRange::Point(0.f);
}
