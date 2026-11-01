// 矿物等值面的采样核心。

#include "PlanetMineralVoxelSampling.h"

// MAX_VOXELVALUE：夹值用到，避免把 int16 的量程抄成一个字面数。
#include "VoxelValue.h"

bool PlanetMineralVoxel::IsInsideAnyRegion(
	const TArray<FBox>& RegionBoxes, const FVector& NormalizedPoint)
{
	for (const FBox& Box : RegionBoxes)
	{
		if (Box.IsValid && Box.IsInsideOrOn(NormalizedPoint))
		{
			return true;
		}
	}
	return false;
}

double PlanetMineralVoxel::EvaluateVoxel(
	const FPlanetMineralVoxelParams& Params, const FIntVector& VoxelIndex)
{
	if (!Params.SampleElementNorm || Params.RadiusVoxels <= 0.0)
	{
		return AirValue;
	}

	// ① 区域早退
	const FVector Normalized(
		double(VoxelIndex.X) / Params.RadiusVoxels,
		double(VoxelIndex.Y) / Params.RadiusVoxels,
		double(VoxelIndex.Z) / Params.RadiusVoxels);

	if (!IsInsideAnyRegion(Params.RegionBoxes, Normalized))
	{
		return AirValue;
	}

	// ② 密度门：行星被挖空了，矿就不在了
	if (Params.IsSolidAtPlanetVoxel && !Params.IsSolidAtPlanetVoxel(VoxelIndex))
	{
		return AirValue;
	}

	// ③ 地质场 + 屏阈
	const double Norm = double(Params.SampleElementNorm(Normalized));

	// FadeRange 是资产上可编辑的值，0 会让除法爆掉。钳到 1e-4 而不是跳过：
	// 跳过会把「阈值以上实心、以下空气」变成一个零宽的硬边。
	const double Fade = FMath::Max(Params.FadeRange, 1e-4);

	// 负 = 实心。Norm 越大越负 = 越是矿。
	//
	// 夹到 FVoxelValue 的量程是纵深防御，不是在挡当前就会发生的溢出：
	// 按现在 Fade 的 1e-4 下限与 |Iso - Norm| ≤ 1，量级上限是 1e4，插件那边
	// 也先把 v_flt 夹到 ±10 再换算。夹子只防以后有人放宽 Fade 下限。
	const double Value = (Params.IsoValue - Norm) / Fade;
	return FMath::Clamp(Value, -double(FVoxelValue::MAX_VOXELVALUE), double(FVoxelValue::MAX_VOXELVALUE));
}
