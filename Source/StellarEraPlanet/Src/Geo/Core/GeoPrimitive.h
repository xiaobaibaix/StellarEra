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
	}
	return FGeoRange::Point(0.f);
}
