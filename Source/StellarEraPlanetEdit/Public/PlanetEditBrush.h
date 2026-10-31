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
 * 轴对齐的体素方块范围。
 *
 * 用裸 FIntVector 而不是插件的 FVoxelIntBox：本头在 StellarEraPlanetEdit 的公开
 * 目录里，而 Voxel 在 StellarEraPlanetEdit.Build.cs 里是 PrivateDependencyModuleNames，
 * 插件的类型不该从这里泄漏出去。转换成 FVoxelIntBox 的活放在 .cpp。
 *
 * **Min 与 Max 两端都含**（宽度 = Max - Min + 1）。注意与 FVoxelIntBox 不同：
 * 后者 Min 含、Max **不含**，换算时要把 Max 加 1 才能保持同样的格子数。
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
	 * 星图笔刷的尺寸区间。
	 *
	 * 与角色的 1..8 是**两套**，不是同一个常量的不同取值：角色贴着地表看的是
	 * 脚下几米，星图看的是整颗星的地貌。带区间的重载是为此存在的 ——
	 * 旧的 1 参 ClampSize 会把 32 悄悄夹成 8。
	 */
	constexpr int32 MinStarMapSizeVoxels = 8;

	/**
	 * 星图笔刷上限。1280 体素 × 100 cm/体素 = 直径 1.28 km 的坑。
	 *
	 * 这个数字不是白给的：体素工具的代价是 Size³，1280 差不多是 128 的一千倍，
	 * 一次落笔要扫 1286³ ≈ 2.1e9 格。所以上限抬高的同时必须关掉 ModifiedValues
	 * 记录（见 ShouldRecordModifiedValues），否则光是记录缓冲区就要 40 GB 以上。
	 */
	constexpr int32 MaxStarMapSizeVoxels = 1280;

	/** 按住 Ctrl 时一档走几倍。上限 1280 配一档 10 体素要按 128 下，够不着。 */
	constexpr int32 CoarseStepMultiplier = 10;

	/** 一次改笔刷走多少体素：BaseStepVoxels × (bCoarse ? CoarseStepMultiplier : 1)。 */
	STELLARERAPLANETEDIT_API int32 ResolveBrushStep(int32 BaseStepVoxels, bool bCoarse);

	/**
	 * 体素工具一次编辑要扫的格子数：包围盒是 2·R + 6 体素宽，即 (SizeVoxels + 6)³。
	 *
	 * 球体真正改到的只是内切球（π/6 ≈ 52%），但工具的包围盒扫描和记录缓冲区
	 * 都按整块算，所以这里保守取整块。
	 */
	STELLARERAPLANETEDIT_API int64 EstimateToolCellCount(int32 SizeVoxels);

	/**
	 * 记录缓冲区最多允许多少格。20 字节/格 → 4e6 格约 80 MB，已经偏大；
	 * 对应尺寸约 152 体素，星图默认 64、角色 1..8 都在里面。
	 *
	 * 写 4000000 而不是 4'000'000：UHT 的数字字面量解析器不认数字分隔符，
	 * 会报 "Unterminated character constant"。
	 */
	constexpr int64 MaxRecordedCellCount = 4000000;

	/**
	 * 这次编辑要不要记录 ModifiedValues。
	 *
	 * 插件在 TVoxelDataImpl::Set 里**按整个包围盒** AddUninitialized，跟实际改到
	 * 几格无关 —— 1280 体素的球要 43 GB。超过预算就只能不记录，改用「中心格
	 * 编辑前后是否相同」判断有没有真的改到东西（见 PlanetBrushApplicator）。
	 */
	STELLARERAPLANETEDIT_API bool ShouldRecordModifiedValues(int32 SizeVoxels);

	/** 夹到 [MinSizeVoxels, MaxSizeVoxels]。 */
	STELLARERAPLANETEDIT_API int32 ClampSize(int32 SizeVoxels);

	/**
	 * 夹到 [MinSize, MaxSize]。
	 *
	 * 下限被抬到至少 1（尺寸 0 会造出空盒）；MinSize > MaxSize 时以下限为准。
	 */
	STELLARERAPLANETEDIT_API int32 ClampSize(int32 SizeVoxels, int32 MinSize, int32 MaxSize);

	/**
	 * 连续挖的节流判定：把 DeltaSeconds 累加进 AccumulatorSeconds，
	 * 达到或超过 IntervalSeconds 时返回 true 并把累加器清零。
	 *
	 * 一次调用最多返回一次 true（不做 while 补偿）：卡顿后不会一帧补挖多下。
	 * 负 DeltaSeconds 按 0 处理，累加器不会因为调用方传错时间而倒退。
	 *
	 * IntervalSeconds 有 0.01 s 的下限：<= 0 与 (0, 0.01) 区间内的值都抬到 0.01。
	 * 这个下限只挡同一帧内间隔 < 10 ms 的重复调用，不足以限制常规帧率 ——
	 * 30 / 60 / 90 FPS 的帧时长（0.033 / 0.017 / 0.011 s）都大于 0.01，
	 * 靠下限兜底照样每帧触发。常规频率必须由调用方传入的 IntervalSeconds 决定。
	 */
	STELLARERAPLANETEDIT_API bool ShouldDigNow(
		float& AccumulatorSeconds, float DeltaSeconds, float IntervalSeconds);

	/** 球半径 / 方块半边长，单位厘米。两者同式：Size * VoxelSize / 2。 */
	STELLARERAPLANETEDIT_API float GetHalfExtentCm(int32 SizeVoxels, float VoxelSizeCm);

	/** 区间版：先按 [MinSize, MaxSize] 夹尺寸，再算半边长。 */
	STELLARERAPLANETEDIT_API float GetHalfExtentCm(int32 SizeVoxels, float VoxelSizeCm,
		int32 MinSize, int32 MaxSize);

	/**
	 * 编辑中心。
	 *
	 * 挖：就是命中点，笔刷半埋在表面里。
	 * 放：沿命中法线外移一个半边长，让球 / 方块与地表相切而不是半埋。
	 */
	STELLARERAPLANETEDIT_API FVector GetEditCenterWorld(const FVector& HitLocation, const FVector& HitNormal,
		int32 SizeVoxels, float VoxelSizeCm, bool bPlace);

	/** 区间版：放的偏移量与笔刷半边长一致（用同一套 Min/Max 夹）。 */
	STELLARERAPLANETEDIT_API FVector GetEditCenterWorld(const FVector& HitLocation, const FVector& HitNormal,
		int32 SizeVoxels, float VoxelSizeCm, bool bPlace, int32 MinSize, int32 MaxSize);

	/**
	 * 把「行星体素空间里的浮点中心」换成整数盒。
	 *
	 * 尺寸 2 必须得到 2 格宽：Low = (Size-1)/2 = 0，High = Size-1-Low = 1。
	 * 用 ±Size/2 会得到 3 格——本盒的 Min 与 Max **两端都含**。
	 * 转成 FVoxelIntBox 时注意它的 Max 是开区间，要 `Max + 1` 才能保持格数。
	 */
	STELLARERAPLANETEDIT_API FPlanetEditBox MakeBox(const FVector& LocalCenterVoxels, int32 SizeVoxels);

	/** 区间版：每轴跨度等于夹到 [MinSize, MaxSize] 之后的 SizeVoxels 格数。 */
	STELLARERAPLANETEDIT_API FPlanetEditBox MakeBox(const FVector& LocalCenterVoxels, int32 SizeVoxels,
		int32 MinSize, int32 MaxSize);

	/**
	 * 把两端都含的 FPlanetEditBox 换成右端开区间。
	 * FVoxelIntBox 的 Max 是开区间（"Exclusive"）：直接透传会让方块少一格。
	 */
	STELLARERAPLANETEDIT_API FPlanetEditVoxelBox MakeVoxelBox(const FPlanetEditBox& Box);

	/**
	 * 体素盒是否有效：三轴都满足 MaxExclusive > Min。
	 *
	 * 判据与 FVoxelIntBox::IsValid() 一致 —— 本类型的默认值就是「无效」，
	 * 用来表示「没有范围」。
	 */
	STELLARERAPLANETEDIT_API bool IsValidVoxelBox(const FPlanetEditVoxelBox& Box);

	/**
	 * 球编辑的完整包围盒（体素空间，右端开区间）。
	 *
	 * 与插件的 FVoxelSphereToolsImpl::GetBounds 逐分量相同：
	 *   Min = FloorToInt(Center - Radius - 3)
	 *   Max = CeilToInt(Center + Radius + 3) + 1
	 *
	 * 那个 ±3 是插件留的余量（SphereEdit 的衰减带最外圈在 Radius + 2，
	 * 再垫一格）。抄错方向很危险：少一格会削掉球最外圈，多一格会多挖。
	 * 有单测直接与插件比对，不写死期望值。
	 *
	 * 按 double 算：插件的 v_flt 是 double（`VOXEL_DOUBLE_PRECISION` 默认 1），
	 * GetBounds 里那两式也在 double 上做。降到 float 会在体素坐标这种大数上
	 * 丢掉小数位，取整后整个盒子偏一格。
	 */
	STELLARERAPLANETEDIT_API FPlanetEditVoxelBox MakeSphereEditVoxelBox(
		const FVector& CenterVoxels, float RadiusVoxels);

	/**
	 * 把体素盒切成边长不超过 ChunkSizeVoxels 的块，按「离盒子中心由近到远」排序。
	 *
	 * 保证：块两两不重叠，并集**恰好**等于输入盒（单测钉住）。这两条是
	 * 「分块写 == 一次写」的充分条件 —— 逐格谓词只看格子自己的坐标、不依赖
	 * 邻格状态，所以按任意划分分别应用再并起来，与一次性应用完全相同。
	 *
	 * 不要求块边界落在 32 的网格上：FVoxelData::ParallelSet 是按
	 * 「叶子与本块的 Overlap」迭代的，跨边界的叶子被两块各处理一部分，结果仍对。
	 *
	 * 排序是为了让挖掘时洞从光标处向外长，而不是从左下角开始逐行填 ——
	 * 逐帧刷新渲染时，这个顺序就是用户看到的样子。
	 */
	STELLARERAPLANETEDIT_API void SplitVoxelBoxIntoChunks(
		const FPlanetEditVoxelBox& Box, int32 ChunkSizeVoxels,
		TArray<FPlanetEditVoxelBox>& OutChunks);

	/**
	 * 两个体素盒的并集。任一无效时返回另一个，两者都无效时返回无效盒。
	 *
	 * 连续挖期间渲染是攒着刷的：每笔工具都不刷渲染，改到的范围在这里累加，
	 * 到点 / 松手时用并集刷一次，同一个 chunk 只重建一遍。
	 */
	STELLARERAPLANETEDIT_API FPlanetEditVoxelBox UnionVoxelBox(
		const FPlanetEditVoxelBox& A, const FPlanetEditVoxelBox& B);

	/**
	 * 两个右端开的体素盒是否有公共格。
	 *
	 * A.MaxExclusive == B.Min 不算相交（没有公共格）—— 合并只对真正重叠的落笔
	 * 有意义，贴边相邻的两笔并起来会凭空多出一片没挖过的范围。
	 */
	STELLARERAPLANETEDIT_API bool VoxelBoxesIntersect(
		const FPlanetEditVoxelBox& A, const FPlanetEditVoxelBox& B);

	/** 球体预览每条纬线的分段数。 */
	constexpr int32 PreviewSegmentsPerCircle = 32;

	/** 球体预览的纬线条数（不含两级）。取奇数，中间那条正好是赤道。 */
	constexpr int32 PreviewParallels = 5;

	/** 球体预览的经线条数（每条是南极到北极的半圆，相邻两条隔 180°/M）。 */
	constexpr int32 PreviewMeridians = 8;

	/** 每条经线的分段数（半圆，所以只有纬线的一半）。 */
	constexpr int32 PreviewSegmentsPerMeridian = 16;

	/**
	 * 让预览线框在屏幕上保持固定像素宽所需的**世界厚度**（厘米）。
	 *
	 * 引擎厚线的 Thickness 是世界单位（bScreenSpace = false 那条路径），投影宽度
	 * 与距离成反比 —— 星图远看整颗星球时线就细成发丝。这里把它反解出来：
	 *
	 *     T_world = P_px × 2 × d × tan(FOV/2) / ViewportWidth_px
	 *
	 * FOV 是**水平** FOV（与 APlayerCameraManager::GetFOVAngle 同口径）。用水平而不是
	 * 垂直，是因为世界厚线的四边形在相机 X/Y 两个轴上都按同一个厚度撑开，按水平
	 * 算出来的像素宽在两个轴上也一致。
	 *
	 * 任一参数非法（距离 ≤ 0、FOV 不在 (0, 180)、视口宽 ≤ 0、像素宽 ≤ 0）时返回
	 * FallbackThicknessCm：没有相机信息时也不该画出一条 0 宽的线。
	 */
	STELLARERAPLANETEDIT_API float PreviewLineThicknessWorld(
		float DistanceCm, float HorizontalFovDegrees, float ViewportWidthPx,
		float ThicknessPx, float FallbackThicknessCm);

	/**
	 * 球体笔刷预览：经纬线球（5 条纬线 + 8 条经线），极轴 = Axis。
	 *
	 * 原来画的是世界 X/Y/Z 轴上的三个大圆，相机一转就有两个侧对镜头、退化成两条
	 * 斜穿的直线，看着像「两个圆交叉」。改成经纬球并让极轴对准**地表法线**：
	 * 中间那条纬线正好是赤道，躺在脚下的地面上，相机怎么转都还是个规整的球。
	 * 纬线条数取奇数就是为了这条赤道。
	 *
	 * **世界空间**：Center / Axis / RadiusCm 都是调用方算好的世界量。Axis 为零时
	 * 退回世界 Z 轴。
	 */
	STELLARERAPLANETEDIT_API void BuildSpherePreviewSegments(const FVector& Center,
		const FVector& Axis, float RadiusCm, TArray<FPlanetEditSegment>& OutSegments);

	/**
	 * 正方体笔刷预览：盒子 Min..MaxExclusive 的 12 条棱。
	 *
	 * 输出是**行星体素空间**坐标（单位：体素）；调用方负责用
	 * AVoxelWorld::LocalToGlobalFloat 换到世界空间。边界必须来自真正的
	 * MakeVoxelBox 结果，预览才会与挖/放的格子逐格一致。
	 */
	STELLARERAPLANETEDIT_API void BuildBoxPreviewSegments(const FIntVector& MinVoxels,
		const FIntVector& MaxExclusiveVoxels, TArray<FPlanetEditSegment>& OutSegments);
}
