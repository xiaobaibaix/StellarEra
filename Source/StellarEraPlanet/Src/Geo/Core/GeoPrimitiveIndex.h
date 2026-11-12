// 原语 BVH。烘焙期建、体素线程查；查询只读、无分配（固定深度栈）。
#pragma once
#include "CoreMinimal.h"
#include "GeoPrimitive.h"

class STELLARERAPLANET_API FGeoPrimitiveIndex
{
public:
	void Build(TArray<FGeoPrimitive> InPrims);
	const TArray<FGeoPrimitive>& GetPrimitives() const { return Prims; }
	bool IsEmpty() const { return Prims.Num() == 0; }

	/** Visit(int32) 收到的是 GetPrimitives() 的原始下标。 */
	template <typename TVisit> void QueryPoint(const FVector3f& Point, TVisit&& Visit) const
	{
		QueryImpl([&Point](const FBox3f& B) { return B.IsInsideOrOn(Point); }, Visit);
	}
	template <typename TVisit> void QueryBox(const FBox3f& Box, TVisit&& Visit) const
	{
		QueryImpl([&Box](const FBox3f& B) { return B.Intersect(Box); }, Visit);
	}

	uint64 Hash() const;
	SIZE_T GetAllocatedSize() const;

private:
	struct FNode
	{
		FBox3f Box = FBox3f(ForceInit);
		int32 Left = -1, Right = -1, First = 0, Count = 0;
	};
	TArray<FGeoPrimitive> Prims;
	TArray<int32> Order;
	TArray<FNode> Nodes;

	int32 BuildNode(int32 First, int32 Count);

	template <typename TTest, typename TVisit> void QueryImpl(TTest&& Test, TVisit&& Visit) const
	{
		if (Nodes.Num() == 0) { return; }
		int32 Stack[64];
		int32 Top = 0;
		Stack[Top++] = 0;
		while (Top > 0)
		{
			const FNode& N = Nodes[Stack[--Top]];
			if (!Test(N.Box)) { continue; }
			if (N.Left < 0)
			{
				for (int32 i = N.First; i < N.First + N.Count; ++i)
				{
					if (Test(Prims[Order[i]].Bounds)) { Visit(Order[i]); }
				}
			}
			else
			{
				Stack[Top++] = N.Left;
				Stack[Top++] = N.Right;
			}
		}
	}
};
