// Copyright Voxel Plugin SAS. All Rights Reserved.
// 程序化球状行星生成器（纯球体）。

#include "VoxelPlanetGenerator.h"
#include "VoxelMaterialBuilder.h"

// 值查询给采样点加的极小偏移（体素数）。区间查询必须用同一个球心，否则
// GetValueRangeImpl 算出的区间会与真实取值差之毫厘。
static constexpr double PlanetCenterEpsilon = 0.001;

TVoxelSharedRef<FVoxelGeneratorInstance> UVoxelPlanetGenerator::GetInstance()
{
	return MakeVoxelShared<FVoxelPlanetGeneratorInstance>(*this);
}

///////////////////////////////////////////////////////////////////////////////

FVoxelPlanetGeneratorInstance::FVoxelPlanetGeneratorInstance(UVoxelPlanetGenerator& MyGenerator)
	: Super(&MyGenerator)
	, Radius(MyGenerator.Radius)
	, Center(MyGenerator.Center)
	, SurfaceColor(MyGenerator.SurfaceColor)
{
}

v_flt FVoxelPlanetGeneratorInstance::GetValueImpl(v_flt X, v_flt Y, v_flt Z, int32 LOD, const FVoxelItemStack& Items) const
{
	// 相对行星球心的偏移；带一个极小的 epsilon，避免返回恰好为 0 的密度值，
	// 那会让网格化器出问题。
	const double PX = double(X) - Center.X + PlanetCenterEpsilon;
	const double PY = double(Y) - Center.Y + PlanetCenterEpsilon;
	const double PZ = double(Z) - Center.Z + PlanetCenterEpsilon;

	const double Distance = FMath::Sqrt(PX * PX + PY * PY + PZ * PZ);

	// 基础有向距离场：球内为负，球外为正，零交叉面即半径为 Radius 的球面。
	//
	// 密度在下游会被夹到 [-1, 1]。除以常数会重新缩放梯度，着色法线正是由
	// 该梯度导出的。除数太小会让梯度饱和成平板着色，太大则法线噪点明显。
	// 5 是官方示例所用的值。
	const double Value = (Distance - double(Radius)) / 5.0;

	return v_flt(FMath::Clamp(Value, -1.0, 1.0));
}

FVoxelMaterial FVoxelPlanetGeneratorInstance::GetMaterialImpl(v_flt X, v_flt Y, v_flt Z, int32 LOD, const FVoxelItemStack& Items) const
{
	// 纯球体没有地貌可分带，整颗星一个颜色。世界采用 RGB 材质配置，
	// 颜色经顶点色传给材质。
	FVoxelMaterialBuilder Builder;
	Builder.SetMaterialConfig(EVoxelMaterialConfig::RGB);
	Builder.SetColor(SurfaceColor);
	return Builder.Build();
}

TVoxelRange<v_flt> FVoxelPlanetGeneratorInstance::GetValueRangeImpl(const FVoxelIntBox& Bounds, int32 LOD, const FVoxelItemStack& Items) const
{
	// 行星性能的关键杠杆。半径 1000 的行星占据边长 2000 的立方体，其中
	// 99% 以上是空的。返回准确的区间，插件就能整块跳过区块、一个体素都
	// 不用碰。
	//
	// 契约：返回的区间必须覆盖 GetValueImpl 在 Bounds 内可能产生的所有值。
	// 估计偏低，行星上就会出洞。
	//
	// 纯球面的区间可以精确算出来，无需保守放大：盒内到球心的距离是连续的，
	// 必然取遍 [最近点, 最远点] 两个距离之间的每一个值，而密度是距离的单调
	// 函数，所以两端各自映射过去就是确切区间。

	const double CX = Center.X - PlanetCenterEpsilon;
	const double CY = Center.Y - PlanetCenterEpsilon;
	const double CZ = Center.Z - PlanetCenterEpsilon;

	// 逐轴求球心投影到盒区间 [Min, Max] 的距离：球心在区间外时是到最近端点
	// 的距离，在区间内时为 0。两个方向的合成即为盒上离球心最近/最远的点。
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

	// 与 GetValueImpl 完全相同的映射，再夹到密度域。
	const double MinValue = FMath::Clamp((MinDistance - double(Radius)) / 5.0, -1.0, 1.0);
	const double MaxValue = FMath::Clamp((MaxDistance - double(Radius)) / 5.0, -1.0, 1.0);

	return TVoxelRange<v_flt>(v_flt(MinValue), v_flt(MaxValue));
}

FVector FVoxelPlanetGeneratorInstance::GetUpVector(v_flt X, v_flt Y, v_flt Z) const
{
	// 在行星上，“上”指向远离地核的方向，而不是世界 Z 轴。放置器用它为
	// 草和岩石定向。返回 FVector::UpVector 会让两极的物体躺倒、赤道的
	// 物体横向戳出。
	const FVector P(double(X) - Center.X, double(Y) - Center.Y, double(Z) - Center.Z);
	if (P.SizeSquared() < UE_DOUBLE_SMALL_NUMBER)
	{
		return FVector::UpVector;
	}
	return P.GetSafeNormal();
}
