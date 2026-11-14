#include "GeoPrimitiveIndex.h"
#include "Algo/Sort.h"
#include "GeoRng.h"

void FGeoPrimitiveIndex::Build(TArray<FGeoPrimitive> InPrims)
{
	Prims = MoveTemp(InPrims);
	Order.SetNumUninitialized(Prims.Num());
	for (int32 i = 0; i < Order.Num(); ++i) { Order[i] = i; }
	Nodes.Reset();
	MaxHalfExtent = 0.f;
	for (const FGeoPrimitive& P : Prims) { MaxHalfExtent = FMath::Max(MaxHalfExtent, P.Bounds.GetExtent().GetMax()); }
	if (Prims.Num() > 0) { BuildNode(0, Prims.Num()); }
}

int32 FGeoPrimitiveIndex::BuildNode(int32 First, int32 Count)
{
	const int32 Index = Nodes.AddDefaulted();
	FBox3f Box(ForceInit), Centers(ForceInit);
	for (int32 i = First; i < First + Count; ++i)
	{
		Box += Prims[Order[i]].Bounds;
		Centers += Prims[Order[i]].Bounds.GetCenter();
	}
	Nodes[Index].Box = Box;
	if (Count <= 4)
	{
		Nodes[Index].First = First;
		Nodes[Index].Count = Count;
		return Index;
	}
	const FVector3f Extent = Centers.GetExtent();
	const int32 Axis = (Extent.X >= Extent.Y && Extent.X >= Extent.Z) ? 0 : (Extent.Y >= Extent.Z ? 1 : 2);
	// 平局按原下标排：排序不稳定，不加次序键会让同输入建出不同的树，哈希不一致。
	Algo::Sort(MakeArrayView(Order.GetData() + First, Count), [this, Axis](int32 A, int32 B)
	{
		const float CA = Prims[A].Bounds.GetCenter()[Axis], CB = Prims[B].Bounds.GetCenter()[Axis];
		return CA < CB || (CA == CB && A < B);
	});
	const int32 Half = Count / 2;
	const int32 Left = BuildNode(First, Half);
	const int32 Right = BuildNode(First + Half, Count - Half);
	Nodes[Index].Left = Left;
	Nodes[Index].Right = Right;
	return Index;
}

uint64 FGeoPrimitiveIndex::Hash() const
{
	uint64 H = GeoHash::Combine(uint64(Prims.Num()), uint64(Nodes.Num()));
	for (const FGeoPrimitive& P : Prims)
	{
		H = GeoHash::Combine(H, uint64(P.Kind) | (uint64(P.MineralId) << 8) | (uint64(P.LithologyId) << 16)
			| (uint64(P.Grade) << 24) | (uint64(P.Priority) << 32));
		const float Vals[11] = { P.Center.X, P.Center.Y, P.Center.Z, P.Radii.X, P.Radii.Y, P.Radii.Z, P.Hard,
			P.Rotation.X, P.Rotation.Y, P.Rotation.Z, P.Rotation.W };
		for (const float V : Vals) { H = GeoHash::Combine(H, GeoHash::Float(V)); }
	}
	for (const int32 I : Order) { H = GeoHash::Combine(H, uint64(I)); }
	return H;
}

SIZE_T FGeoPrimitiveIndex::GetAllocatedSize() const
{
	return Prims.GetAllocatedSize() + Order.GetAllocatedSize() + Nodes.GetAllocatedSize();
}
