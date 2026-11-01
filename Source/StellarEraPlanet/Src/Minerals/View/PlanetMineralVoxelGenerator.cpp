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
	// 宿主指针传空：它只是序列化用的软引用，而本实例从不被序列化
	// （FVoxelGeneratorPicker 每次建世界都重新调 GetInstance）。
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
	// 插件用浮点坐标调用，但我们的判据定义在整数体素索引上。
	// 取整而不是截断：负坐标下截断会往 0 靠，让球心附近错半格。
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
	// 纯空气块：插件整块跳过，一个体素都不求。矿物体素世界的性能全靠这一条。
	//
	// 逐盒求交，与 EvaluateVoxel 的 IsInsideAnyRegion 等价：区域散开时并集 AABB
	// 会把整片壳层都算成「相交」，性能杠杆就没了。
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

	// 与区域相交的块：给一个保守但紧的区间。契约要求它必须覆盖块内可能出现的
	// 所有取值，估窄了矿上会出洞。
	//
	// 块里同时有两种体素：
	//   ① 区域内的矿：Norm ∈ [0, 1] → (Iso - Norm) / Fade ∈ [(Iso - 1) / Fade, Iso / Fade]
	//   ② 区域外的空气：恒为 AirValue（EvaluateVoxel 的第一条早退）
	// 所以上界必须**同时**盖住 Iso / Fade 和 AirValue —— 主预设 ColorThreshold = 0.05、
	// FadeRange = 0.12 时 Iso / Fade 只有 0.4167，光用它当上界会把 1.0 排除在区间外。
	//
	// 下界不用同样处理：Iso ≤ 1 时 (Iso - 1) / Fade ≤ 0 < AirValue，本来就盖住了。
	const double Fade = FMath::Max(Params.FadeRange, 1e-4);
	const double MaxValue = FMath::Max(
		Params.IsoValue / Fade, double(PlanetMineralVoxel::AirValue));
	const double MinValue = (Params.IsoValue - 1.0) / Fade;
	return TVoxelRange<v_flt>(v_flt(MinValue), v_flt(MaxValue));
}

FVector FPlanetMineralVoxelInstance::GetUpVector(v_flt X, v_flt Y, v_flt Z) const
{
	// 与行星体素索引空间同一套坐标（行星中心恒为原点，见
	// PlanetMineralVoxelSampling.h 的坐标约定），所以「上」就是行星径向，
	// 而不是世界 Z 轴 —— 与 FVoxelPlanetSphereTerrainInstance 同一个理由。
	//
	// 原点处没有方向可言，退回世界 Z 轴。GetSafeNormal 在长度为 0 时返回零向量，
	// 拿零向量去定向会让物体整个塌掉。
	// 写成 = FVector(...) 而不是 FVector P(...)：后者在参数形如 double(X) 时会
	// 被解析成函数声明（most vexing parse），C2228「.SizeSquared 左边必须有类/结构」。
	const FVector P = FVector(double(X), double(Y), double(Z));
	if (P.SizeSquared() < UE_DOUBLE_SMALL_NUMBER)
	{
		return FVector::UpVector;
	}
	return P.GetSafeNormal();
}
