// 把一个元素的矿物场接进 VoxelFree。

#include "PlanetMineralVoxelGenerator.h"

#include "VoxelMaterialBuilder.h"

TVoxelSharedRef<FVoxelGeneratorInstance> UPlanetMineralVoxelGenerator::GetInstance()
{
	return MakeVoxelShared<FPlanetMineralVoxelInstance>(Params, Color);
}

void UPlanetMineralVoxelGenerator::Configure(
	const FPlanetMineralVoxelParams& InParams, const FLinearColor& InColor)
{
	Params = InParams;
	Color = InColor;
}

///////////////////////////////////////////////////////////////////////////////

FPlanetMineralVoxelInstance::FPlanetMineralVoxelInstance(
	const FPlanetMineralVoxelParams& InParams, const FLinearColor& InColor)
	// 宿主指针传空：它只是序列化用的软引用，而本实例从不被序列化（FVoxelGeneratorPicker 每次建世界都重新调 GetInstance）。
	: Super(nullptr)
	, Params(InParams)
	, Color(InColor)
{
	// 区域盒从归一化空间换到体素索引空间。逐个保存，不取并集（理由见头文件）。
	RegionBoxesVoxels.Reserve(Params.RegionBoxes.Num());
	for (const FBox& Box : Params.RegionBoxes)
	{
		if (!Box.IsValid)
		{
			continue;
		}
		RegionBoxesVoxels.Emplace(
			FVector3d(Box.Min) * Params.RadiusVoxels,
			FVector3d(Box.Max) * Params.RadiusVoxels);
	}
}

v_flt FPlanetMineralVoxelInstance::GetValueImpl(
	v_flt X, v_flt Y, v_flt Z, int32 LOD, const FVoxelItemStack& Items) const
{
	// 插件用浮点坐标调用，判据定义在整数体素索引上。取整而非截断：负坐标截断会往 0 靠，球心附近错半格。
	const FIntVector Index(
		FMath::RoundToInt(double(X)),
		FMath::RoundToInt(double(Y)),
		FMath::RoundToInt(double(Z)));

	return v_flt(PlanetMineralVoxel::EvaluateVoxel(Params, Index));
}

FVoxelMaterial FPlanetMineralVoxelInstance::GetMaterialImpl(
	v_flt X, v_flt Y, v_flt Z, int32 LOD, const FVoxelItemStack& Items) const
{
	// 一个世界只有一种颜色。世界用 RGB 材质配置，颜色经顶点色传给材质。
	FVoxelMaterialBuilder Builder;
	Builder.SetMaterialConfig(EVoxelMaterialConfig::RGB);
	Builder.SetColor(Color);
	return Builder.Build();
}

TVoxelRange<v_flt> FPlanetMineralVoxelInstance::GetValueRangeImpl(
	const FVoxelIntBox& Bounds, int32 LOD, const FVoxelItemStack& Items) const
{
	// 纯空气块：插件整块跳过，一个体素都不求 —— 矿物体素世界的性能全靠这一条。逐盒求交，与 EvaluateVoxel 的 IsInsideAnyRegion 等价
	// （并集 AABB 会把整片壳层都算成相交，杠杆就没了）。
	const FBox3d Bounds3d(FVector3d(Bounds.Min), FVector3d(Bounds.Max));
	const bool bIntersectsAnyRegion = RegionBoxesVoxels.ContainsByPredicate(
		[&Bounds3d](const FBox3d& RegionBox)
		{
			return RegionBox.Intersect(Bounds3d);
		});
	if (!bIntersectsAnyRegion)
	{
		return TVoxelRange<v_flt>(v_flt(PlanetMineralVoxel::AirValue));
	}

	// 与区域相交的块给保守但紧的区间，契约要求覆盖块内所有取值，估窄了矿上会出洞：
	// ① 区域内矿 Norm ∈ [0,1] → (Iso - Norm) / Fade ∈ [(Iso-1)/Fade, Iso/Fade]；② 区域外空气恒为 AirValue。
	// 上界必须同时盖住 Iso/Fade 与 AirValue（ColorThreshold 0.05、FadeRange 0.12 时 Iso/Fade 只有 0.4167，光用它会把 1.0 排除在外）；
	// 下界 (Iso-1)/Fade ≤ 0 < AirValue，本来就盖住了。
	const double Fade = FMath::Max(Params.FadeRange, 1e-4);
	const double MaxValue = FMath::Max(
		Params.IsoValue / Fade, double(PlanetMineralVoxel::AirValue));
	const double MinValue = (Params.IsoValue - 1.0) / Fade;
	return TVoxelRange<v_flt>(v_flt(MinValue), v_flt(MaxValue));
}

FVector FPlanetMineralVoxelInstance::GetUpVector(v_flt X, v_flt Y, v_flt Z) const
{
	// 与行星体素索引空间同一套坐标（中心恒为原点），「上」是行星径向而非世界 Z 轴；原点处无方向可言，退回 Z 轴
	// （GetSafeNormal 长度为 0 时返回零向量，拿它定向会让物体塌掉）。写成 = FVector(...) 而非 FVector P(...)，否则会被解析成函数声明。
	const FVector P = FVector(double(X), double(Y), double(Z));
	if (P.SizeSquared() < UE_DOUBLE_SMALL_NUMBER)
	{
		return FVector::UpVector;
	}
	return P.GetSafeNormal();
}
