// Copyright Voxel Plugin SAS. All Rights Reserved.
// 纯球地形模块：等值面是一个严格的球面。

#include "VoxelPlanetSphereTerrain.h"
#include "VoxelMaterialBuilder.h"

// 值查询给采样点加的极小偏移（体素数）。区间查询必须用同一个球心，否则
// GetValueRangeImpl 算出的区间会与真实取值差之毫厘。
static constexpr double PlanetCenterEpsilon = 0.001;

// 密度除以的常数。噪声地形模块用的是同一个值 —— 两边的梯度必须一致，
// 否则换地形时着色法线会变、光照观感会跳。
static constexpr double DensityScale = 5.0;

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
	// 宿主指针传空：它只是序列化用的软引用，而本实例从不被序列化。
	// 详见头文件里 FVoxelPlanetSphereTerrainInstance 的说明。
	: Super(nullptr)
	, Radius(Context.Radius)
	, Center(Context.Center)
	, SurfaceColor(Context.BaseColor)
{
}

v_flt FVoxelPlanetSphereTerrainInstance::GetValueImpl(v_flt X, v_flt Y, v_flt Z, int32 LOD, const FVoxelItemStack& Items) const
{
	// 相对行星球心的偏移；带一个极小的 epsilon，避免返回恰好为 0 的密度值，
	// 那会让网格化器出问题。
	const double PX = double(X) - Center.X + PlanetCenterEpsilon;
	const double PY = double(Y) - Center.Y + PlanetCenterEpsilon;
	const double PZ = double(Z) - Center.Z + PlanetCenterEpsilon;

	const double Distance = FMath::Sqrt(PX * PX + PY * PY + PZ * PZ);

	// 基础有向距离场：球内为负，球外为正，零交叉面即半径为 Radius 的球面。
	//
	// 除以常数会重新缩放梯度，着色法线正是由该梯度导出的。除数太小会让法线
	// 噪点明显；取 5 让密度在离地表 5 体素处到达 ±1。
	//
	// 但**不能**再把结果夹到 [-1, 1]：LOD > 0 的着色法线是插件对生成器浮点值
	// 做中心差分算的，采样间距是 2^LOD 体素。夹住之后，间距一超过 5 体素
	// （LOD 3 起），6 个采样就全都饱和，法线只剩 ±1 分量，退化成 26 个轴向
	// 对角方向之一 —— 表现为粗 LOD 的大块明暗色块。存储层（FVoxelValue）自己
	// 会按 ±1 饱和，表面抽取只看符号，所以不夹不影响几何。
	const double Value = (Distance - double(Radius)) / DensityScale;

	return v_flt(Value);
}

FVoxelMaterial FVoxelPlanetSphereTerrainInstance::GetMaterialImpl(v_flt X, v_flt Y, v_flt Z, int32 LOD, const FVoxelItemStack& Items) const
{
	// 纯球体没有地貌可分带，整颗星一个颜色。世界采用 RGB 材质配置，
	// 颜色经顶点色传给材质。
	FVoxelMaterialBuilder Builder;
	Builder.SetMaterialConfig(EVoxelMaterialConfig::RGB);
	Builder.SetColor(SurfaceColor);
	return Builder.Build();
}

TVoxelRange<v_flt> FVoxelPlanetSphereTerrainInstance::GetValueRangeImpl(const FVoxelIntBox& Bounds, int32 LOD, const FVoxelItemStack& Items) const
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

	// 与 GetValueImpl 完全相同的映射。不夹到 ±1：契约要求区间覆盖取值，而
	// 取值已不再夹断；下游换算成 FVoxelValue 时会自己饱和，只消费端点符号。
	const double MinValue = (MinDistance - double(Radius)) / DensityScale;
	const double MaxValue = (MaxDistance - double(Radius)) / DensityScale;

	return TVoxelRange<v_flt>(v_flt(MinValue), v_flt(MaxValue));
}

FVector FVoxelPlanetSphereTerrainInstance::GetUpVector(v_flt X, v_flt Y, v_flt Z) const
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
