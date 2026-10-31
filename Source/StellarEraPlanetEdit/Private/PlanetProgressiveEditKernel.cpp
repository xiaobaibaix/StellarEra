#include "PlanetProgressiveEditKernel.h"

#include "VoxelUtilities/VoxelMathUtilities.h"

void PlanetProgressiveKernel::GetSphereThresholds(
	const float Radius, float& OutSquaredRadiusPlus2, float& OutSquaredRadiusMinus2)
{
	// 与 SphereEdit 开头那两行同式，顺序也别改：FMath::Max(R - 2, 0) 先把
	// 小于 2 的半径夹到 0，否则小笔刷会得到一个「负数的平方」，把内圈
	// 判断翻成恒不成立。
	OutSquaredRadiusPlus2 = FMath::Square(Radius + 2);
	OutSquaredRadiusMinus2 = FMath::Square(FMath::Max(Radius - 2, 0.f));
}

FVoxelValue PlanetProgressiveKernel::EvaluateSphereValue(
	const float Radius,
	const float SquaredRadiusPlus2,
	const float SquaredRadiusMinus2,
	const float SquaredDistance,
	const FVoxelValue CurrentValue,
	const bool bPlace)
{
	if (SquaredDistance > SquaredRadiusPlus2)
	{
		return CurrentValue;
	}

	if (SquaredDistance <= SquaredRadiusMinus2)
	{
		return bPlace ? FVoxelValue::Full() : FVoxelValue::Empty();
	}

	const float Distance = FMath::Sqrt(SquaredDistance);
	// 与插件同一个表达式：clamp 到 ±2 再除 2，落到 [-1, 1]；
	// 填取反（越靠内越「实」）。
	const FVoxelValue NewValue{
		FMath::Clamp(Radius - Distance, -2.f, 2.f) / 2 * (bPlace ? -1.f : 1.f) };

	// MergeAsset(Value, NewValue, !bAdd)：挖 = max，填 = min。
	return FVoxelUtilities::MergeAsset(CurrentValue, NewValue, !bPlace);
}

FVoxelValue PlanetProgressiveKernel::EvaluateBoxValue(
	const FIntVector& Voxel,
	const FIntVector& BoxMin,
	const FIntVector& BoxMaxExclusive,
	const FVoxelValue CurrentValue,
	const bool bPlace)
{
	const bool bOnShell =
		Voxel.X == BoxMin.X || Voxel.X == BoxMaxExclusive.X - 1 ||
		Voxel.Y == BoxMin.Y || Voxel.Y == BoxMaxExclusive.Y - 1 ||
		Voxel.Z == BoxMin.Z || Voxel.Z == BoxMaxExclusive.Z - 1;

	if (bOnShell)
	{
		// 原式：`(bAdd && Value.IsEmpty()) || (!bAdd && !Value.IsEmpty())`。
		// 只有「空实翻转」的那一面才置 0，另一面保持原值 —— 这是插件给方块
		// 边缘留的表面层，照抄即可，不要自己加判定。
		if ((bPlace && CurrentValue.IsEmpty()) || (!bPlace && !CurrentValue.IsEmpty()))
		{
			return FVoxelValue(0.f);
		}
		return CurrentValue;
	}

	return bPlace ? FVoxelValue::Full() : FVoxelValue::Empty();
}

float PlanetProgressiveKernel::SquaredDistanceTo(
	const FVector& CenterVoxels, const int32 X, const int32 Y, const int32 Z)
{
	// 与插件同式：FVector 是 double，最后一步落到 float。
	const float SquaredDistance =
		FVector(X - CenterVoxels.X, Y - CenterVoxels.Y, Z - CenterVoxels.Z).SizeSquared();
	return SquaredDistance;
}

FVoxelValue PlanetProgressiveKernel::EvaluateOps(
	const FPlanetEditOp* const Ops, const int32 NumOps,
	const int32 X, const int32 Y, const int32 Z,
	FVoxelValue CurrentValue, const bool bPlace)
{
	if (Ops == nullptr)
	{
		return CurrentValue;
	}

	for (int32 Index = 0; Index < NumOps; ++Index)
	{
		const FPlanetEditOp& Op = Ops[Index];
		if (Op.Shape == EPlanetBrushShape::Sphere)
		{
			CurrentValue = EvaluateSphereValue(
				Op.RadiusVoxels, Op.SquaredRadiusPlus2, Op.SquaredRadiusMinus2,
				SquaredDistanceTo(Op.CenterVoxels, X, Y, Z), CurrentValue, bPlace);
		}
		else
		{
			// 方块 op 只评估自己包围盒内的格子。合并后新块会落在旧 op 的盒外，
			// 而 EvaluateBoxValue 对盒外格子返回 Full/Empty（插件原实现只在
			// Bounds 内迭代，盒外格子在它的语义里根本不该被碰）。
			const FPlanetEditVoxelBox& Bounds = Op.Bounds;
			if (X < Bounds.Min.X || X >= Bounds.MaxExclusive.X
				|| Y < Bounds.Min.Y || Y >= Bounds.MaxExclusive.Y
				|| Z < Bounds.Min.Z || Z >= Bounds.MaxExclusive.Z)
			{
				continue;
			}

			CurrentValue = EvaluateBoxValue(
				FIntVector(X, Y, Z), Op.Bounds.Min, Op.Bounds.MaxExclusive,
				CurrentValue, bPlace);
		}
	}

	return CurrentValue;
}
