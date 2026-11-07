// Copyright Voxel Plugin SAS. All Rights Reserved.
// 纯球地形模块：等值面是一个严格的球面。

#include "VoxelPlanetSphereTerrain.h"
#include "VoxelMaterialBuilder.h"

// 值查询给采样点加的极小偏移（体素）；区间查询必须用同一个球心。
static constexpr double SpherePlanetCenterEpsilon = 0.001;

// 密度除数。噪声地形用同一个值 —— 两边梯度必须一致，否则换地形时光照观感会跳。
static constexpr double SphereDensityScale = 5.0;

TVoxelSharedRef<FVoxelGeneratorInstance> UVoxelPlanetSphereTerrain::GetInstance(const FVoxelPlanetTerrainContext& Context)
{
	return MakeVoxelShared<FVoxelPlanetSphereTerrainInstance>(Context);
}

float UVoxelPlanetSphereTerrain::GetSurfaceRadiusVoxels(const FVoxelPlanetTerrainContext& Context, const FVector& UnitDir) const
{
	// 光滑球没有起伏，地表半径就是基准半径 —— 方向参数用不上。
	return Context.Radius;
}

///////////////////////////////////////////////////////////////////////////////

FVoxelPlanetSphereTerrainInstance::FVoxelPlanetSphereTerrainInstance(const FVoxelPlanetTerrainContext& Context)
	// 宿主指针传空：只用于序列化，本实例从不被序列化（见头文件）。
	: Super(nullptr)
	, Radius(Context.Radius)
	, Center(Context.Center)
	, SurfaceColor(Context.BaseColor)
{
}

v_flt FVoxelPlanetSphereTerrainInstance::GetValueImpl(v_flt X, v_flt Y, v_flt Z, int32 LOD, const FVoxelItemStack& Items) const
{
	// 相对球心的偏移带极小 epsilon，避免返回恰好 0 的密度，那会让网格化器出问题。
	const double PX = double(X) - Center.X + SpherePlanetCenterEpsilon;
	const double PY = double(Y) - Center.Y + SpherePlanetCenterEpsilon;
	const double PZ = double(Z) - Center.Z + SpherePlanetCenterEpsilon;

	const double Distance = FMath::Sqrt(PX * PX + PY * PY + PZ * PZ);

	// 球内有向距离场，除以常数重缩放梯度（着色法线由梯度导出）。**不能**夹到
	// [-1,1]：LOD3 起中心差分间距超过 5 体素会饱和，法线退化成轴向，粗 LOD 出现
	// 明暗色块。存储层 FVoxelValue 自己会按 ±1 饱和，表面抽取只看符号。
	const double Value = (Distance - double(Radius)) / SphereDensityScale;

	return v_flt(Value);
}

FVoxelMaterial FVoxelPlanetSphereTerrainInstance::GetMaterialImpl(v_flt X, v_flt Y, v_flt Z, int32 LOD, const FVoxelItemStack& Items) const
{
	// 纯球没有地貌分带，整颗星一个颜色，经顶点色传给材质。
	FVoxelMaterialBuilder Builder;
	Builder.SetMaterialConfig(EVoxelMaterialConfig::RGB);
	Builder.SetColor(SurfaceColor);
	return Builder.Build();
}

TVoxelRange<v_flt> FVoxelPlanetSphereTerrainInstance::GetValueRangeImpl(const FVoxelIntBox& Bounds, int32 LOD, const FVoxelItemStack& Items) const
{
	// 行星性能的关键杠杆：半径 1000 的立方体 99% 是空的，准确区间让插件整块跳过。
	// 契约要求覆盖 GetValueImpl 的所有取值，估低会出洞。纯球面距离连续、密度单调，
	// 两端各自映射即精确区间。

	const double CX = Center.X - SpherePlanetCenterEpsilon;
	const double CY = Center.Y - SpherePlanetCenterEpsilon;
	const double CZ = Center.Z - SpherePlanetCenterEpsilon;

	// 逐轴求球心到盒区间的最近/最远距离，合成为盒上离球心最近/最远的点。
	const auto NearestDelta = [](double Min, double Max, double C) -> double
	{
		if (C < Min) { return Min - C; }
		if (C > Max) { return C - Max; }
		return 0.0;
	};
	const auto FarthestDelta = [](double Min, double Max, double C) -> double
	{
		return FMath::Max(FMath::Abs(Min - C), FMath::Abs(Max - C));
	};

	const double NearX = NearestDelta(Bounds.Min.X, Bounds.Max.X, CX);
	const double NearY = NearestDelta(Bounds.Min.Y, Bounds.Max.Y, CY);
	const double NearZ = NearestDelta(Bounds.Min.Z, Bounds.Max.Z, CZ);
	const double MinDistance = FMath::Sqrt(NearX * NearX + NearY * NearY + NearZ * NearZ);

	const double FarX = FarthestDelta(Bounds.Min.X, Bounds.Max.X, CX);
	const double FarY = FarthestDelta(Bounds.Min.Y, Bounds.Max.Y, CY);
	const double FarZ = FarthestDelta(Bounds.Min.Z, Bounds.Max.Z, CZ);
	const double MaxDistance = FMath::Sqrt(FarX * FarX + FarY * FarY + FarZ * FarZ);

	// 与 GetValueImpl 相同的映射；不夹 ±1，下游换算成 FVoxelValue 时自己饱和。
	const double MinValue = (MinDistance - double(Radius)) / SphereDensityScale;
	const double MaxValue = (MaxDistance - double(Radius)) / SphereDensityScale;

	return TVoxelRange<v_flt>(v_flt(MinValue), v_flt(MaxValue));
}

FVector FVoxelPlanetSphereTerrainInstance::GetUpVector(v_flt X, v_flt Y, v_flt Z) const
{
	// 行星上「上」指向远离地核，不是世界 Z；否则两极物体躺倒、赤道物体横向戳出。
	const FVector P(double(X) - Center.X, double(Y) - Center.Y, double(Z) - Center.Z);
	if (P.SizeSquared() < UE_DOUBLE_SMALL_NUMBER)
	{
		return FVector::UpVector;
	}
	return P.GetSafeNormal();
}
