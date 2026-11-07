// Copyright Voxel Plugin SAS. All Rights Reserved.
// 行星编辑笔刷的纯几何：不碰世界，全部可无头测试。

#pragma once

#include "CoreMinimal.h"
#include "PlanetEditBrush.generated.h"

/** 笔刷形状。只有这两种。 */
UENUM(BlueprintType)
enum class EPlanetBrushShape : uint8
{
	Sphere UMETA(DisplayName = "球体"),
	Cube UMETA(DisplayName = "正方体")
};

/**
 * 轴对齐体素盒（裸 FIntVector，避免 FVoxelIntBox 从公开头泄漏；Voxel 是私有依赖）。
 * Min 与 Max 两端都含；FVoxelIntBox 的 Max 不含，换算要 +1。
 */
struct FPlanetEditBox
{
	FIntVector Min = FIntVector::ZeroValue;
	FIntVector Max = FIntVector::ZeroValue;
};

/** 开区间体素盒：Min 含、MaxExclusive 不含。可直接传给 FVoxelIntBox(Min, MaxExclusive)。 */
struct FPlanetEditVoxelBox
{
	FIntVector Min = FIntVector::ZeroValue;
	FIntVector MaxExclusive = FIntVector::ZeroValue;

	/** 每轴格数（MaxExclusive - Min）。 */
	FIntVector GetSize() const { return MaxExclusive - Min; }
};

/** 笔刷预览的一条线。空间由生成函数说明。 */
struct FPlanetEditSegment
{
	FVector Start = FVector::ZeroVector;
	FVector End = FVector::ZeroVector;
};

namespace PlanetEditBrush
{
	/** 角色笔刷的尺寸下限 / 上限（体素数，直径 / 边长）。 */
	constexpr int32 MinSizeVoxels = 1;
	constexpr int32 MaxSizeVoxels = 8;

	/**
	 * 星图笔刷尺寸区间，与角色的 1..8 是两套：角色看脚下几米，星图看整颗星；
	 * 带区间的重载因此存在（1 参 ClampSize 会把 32 悄悄夹成 8）。
	 */
	constexpr int32 MinStarMapSizeVoxels = 8;

	/**
	 * 星图笔刷上限。1280 体素 × 100 cm = 直径 1.28 km 的坑；代价是 Size³ ≈ 2.1e9 格，
	 * 所以上限抬高时必须关掉 ModifiedValues 记录（见 ShouldRecordModifiedValues），否则要吃 40 GB。
	 */
	constexpr int32 MaxStarMapSizeVoxels = 1280;

	/** 按住 Ctrl 时一档走几倍。上限 1280 配一档 10 体素要按 128 下，够不着。 */
	constexpr int32 CoarseStepMultiplier = 10;

	/** 一次改笔刷走多少体素：BaseStepVoxels × (bCoarse ? CoarseStepMultiplier : 1)。 */
	STELLARERAPLANET_API int32 ResolveBrushStep(int32 BaseStepVoxels, bool bCoarse);

	/**
	 * 一次编辑要扫的格子数：包围盒 2·R + 6 体素宽，即 (SizeVoxels + 6)³（球只改到内切球，但扫描和
	 * 记录都按整块算）。
	 */
	STELLARERAPLANET_API int64 EstimateToolCellCount(int32 SizeVoxels);

	/**
	 * 记录缓冲区上限：20 字节/格 → 4e6 格约 80 MB，对应约 152 体素。
	 * 写 4000000 而非 4'000'000，UHT 不认数字分隔符。
	 */
	constexpr int64 MaxRecordedCellCount = 4000000;

	/**
	 * 这次编辑要不要记录 ModifiedValues。插件在 TVoxelDataImpl::Set 里按整个包围盒 AddUninitialized，
	 * 1280 体素要 43 GB；超预算就不记录，改用中心格前后比较判断。
	 */
	STELLARERAPLANET_API bool ShouldRecordModifiedValues(int32 SizeVoxels);

	/** 夹到 [MinSizeVoxels, MaxSizeVoxels]。 */
	STELLARERAPLANET_API int32 ClampSize(int32 SizeVoxels);

	/** 夹到 [MinSize, MaxSize]。下限抬到至少 1（尺寸 0 造空盒）；Min > Max 时以下限为准。 */
	STELLARERAPLANET_API int32 ClampSize(int32 SizeVoxels, int32 MinSize, int32 MaxSize);

	/**
	 * 连续挖节流：DeltaSeconds 累加进 AccumulatorSeconds，≥ IntervalSeconds 返回 true 并清零。一次调用
	 * 最多一次 true（不补挖），负 delta 按 0；Interval 下限 0.01 s 只挡同帧重复调用。
	 */
	STELLARERAPLANET_API bool ShouldDigNow(
		float& AccumulatorSeconds, float DeltaSeconds, float IntervalSeconds);

	/** 球半径 / 方块半边长，单位厘米。两者同式：Size * VoxelSize / 2。 */
	STELLARERAPLANET_API float GetHalfExtentCm(int32 SizeVoxels, float VoxelSizeCm);

	/** 区间版：先按 [MinSize, MaxSize] 夹尺寸，再算半边长。 */
	STELLARERAPLANET_API float GetHalfExtentCm(int32 SizeVoxels, float VoxelSizeCm,
		int32 MinSize, int32 MaxSize);

	/** 编辑中心：挖 = 命中点（笔刷半埋）；放 = 沿命中法线外移一个半边长，让笔刷与地表相切。 */
	STELLARERAPLANET_API FVector GetEditCenterWorld(const FVector& HitLocation, const FVector& HitNormal,
		int32 SizeVoxels, float VoxelSizeCm, bool bPlace);

	/** 区间版：放的偏移量与笔刷半边长一致（用同一套 Min/Max 夹）。 */
	STELLARERAPLANET_API FVector GetEditCenterWorld(const FVector& HitLocation, const FVector& HitNormal,
		int32 SizeVoxels, float VoxelSizeCm, bool bPlace, int32 MinSize, int32 MaxSize);

	/**
	 * 浮点中心换成两端都含的整数盒：Low=(Size-1)/2、High=Size-1-Low，尺寸 2 得 2 格宽。
	 * 转 FVoxelIntBox 时 Max 是开区间，要 +1 才能保持格数。
	 */
	STELLARERAPLANET_API FPlanetEditBox MakeBox(const FVector& LocalCenterVoxels, int32 SizeVoxels);

	/** 区间版：每轴跨度等于夹到 [MinSize, MaxSize] 之后的 SizeVoxels 格数。 */
	STELLARERAPLANET_API FPlanetEditBox MakeBox(const FVector& LocalCenterVoxels, int32 SizeVoxels,
		int32 MinSize, int32 MaxSize);

	/** 两端都含 → 右端开。FVoxelIntBox 的 Max 是开区间，直接透传会让方块少一格。 */
	STELLARERAPLANET_API FPlanetEditVoxelBox MakeVoxelBox(const FPlanetEditBox& Box);

	/**
	 * 体素盒是否有效：三轴都满足 MaxExclusive > Min，与 FVoxelIntBox::IsValid() 一致；
	 * 默认值即「无效」，表示「没有范围」。
	 */
	STELLARERAPLANET_API bool IsValidVoxelBox(const FPlanetEditVoxelBox& Box);

	/**
	 * 球编辑的完整包围盒（体素空间，右端开），与插件 GetBounds 逐分量相同：
	 * Min=FloorToInt(Center−R−3)，Max=CeilToInt(Center+R+3)+1。±3 是插件留的余量，抄错方向会削掉
	 * 最外圈或多挖。按 double 算，降到 float 会在体素坐标大数上偏一格。
	 */
	STELLARERAPLANET_API FPlanetEditVoxelBox MakeSphereEditVoxelBox(
		const FVector& CenterVoxels, float RadiusVoxels);

	/**
	 * 把体素盒切成 ≤ ChunkSizeVoxels 的块，按离盒中心由近到远排序。块不重叠、并集恰好等于输入盒
	 * （单测钉住），这是「分块写 == 一次写」的充分条件；排序让洞从光标处向外长。
	 */
	STELLARERAPLANET_API void SplitVoxelBoxIntoChunks(
		const FPlanetEditVoxelBox& Box, int32 ChunkSizeVoxels,
		TArray<FPlanetEditVoxelBox>& OutChunks);

	/**
	 * 两个体素盒的并集；任一无效返回另一个。连续挖期间渲染攒着刷，改到的范围在这里累加，到点 /
	 * 松手时用并集刷一次，同一 chunk 只重建一遍。
	 */
	STELLARERAPLANET_API FPlanetEditVoxelBox UnionVoxelBox(
		const FPlanetEditVoxelBox& A, const FPlanetEditVoxelBox& B);

	/**
	 * 两个右端开的盒是否有公共格。A.MaxExclusive == B.Min 不算相交 —— 贴边相邻的两笔并起来
	 * 会凭空多出一片没挖过的范围。
	 */
	STELLARERAPLANET_API bool VoxelBoxesIntersect(
		const FPlanetEditVoxelBox& A, const FPlanetEditVoxelBox& B);

	/** 球体预览每条纬线的分段数。 */
	constexpr int32 PreviewSegmentsPerCircle = 32;

	/** 球体预览的纬线条数（不含两级）。取奇数，中间那条正好是赤道。 */
	constexpr int32 PreviewParallels = 5;

	/**
	 * 经线条数（每条是南极到北极的半圆）。方位角必须扫满 360°：写成 180°/M 时全挤在半个球上，
	 * 星图沿法线俯视时一侧经线全没了。
	 */
	constexpr int32 PreviewMeridians = 8;

	/** 每条经线的分段数（半圆，所以只有纬线的一半）。 */
	constexpr int32 PreviewSegmentsPerMeridian = 16;

	/**
	 * 让预览线在屏幕上保持固定像素宽所需的世界厚度（cm）。引擎厚线的 Thickness 是世界单位，投影宽度
	 * 与距离成反比：T = P_px × 2d × tan(FOV/2) / ViewportWidth_px（FOV 取水平，与 GetFOVAngle 同口径）。
	 * 参数非法时返回 FallbackThicknessCm。
	 */
	STELLARERAPLANET_API float PreviewLineThicknessWorld(
		float DistanceCm, float HorizontalFovDegrees, float ViewportWidthPx,
		float ThicknessPx, float FallbackThicknessCm);

	/**
	 * 球体预览：经纬线球（5 条纬线 + 8 条经线），极轴 = Axis。用经纬球而非三个大圆：后者相机一转
	 * 就有两个侧对镜头退化成斜线。纬线取奇数，中间那条正好是贴地赤道。Center / Axis / RadiusCm
	 * 均为世界量，Axis 为零时退回世界 Z。
	 */
	STELLARERAPLANET_API void BuildSpherePreviewSegments(const FVector& Center,
		const FVector& Axis, float RadiusCm, TArray<FPlanetEditSegment>& OutSegments);

	/**
	 * 正方体预览：盒 Min..MaxExclusive 的 12 条棱，输出是行星体素空间，调用方负责换到世界空间；
	 * 边界必须来自真正的 MakeVoxelBox 结果。
	 */
	STELLARERAPLANET_API void BuildBoxPreviewSegments(const FIntVector& MinVoxels,
		const FIntVector& MaxExclusiveVoxels, TArray<FPlanetEditSegment>& OutSegments);
}
