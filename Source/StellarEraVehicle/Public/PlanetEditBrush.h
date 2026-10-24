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
 * 用裸 FIntVector 而不是插件的 FVoxelIntBox：本头在 StellarEraVehicle 的公开
 * 目录里，而 Voxel 是该模块的私有依赖。转换成 FVoxelIntBox 的活放在 .cpp。
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
	constexpr int32 MaxStarMapSizeVoxels = 128;

	/** 夹到 [MinSizeVoxels, MaxSizeVoxels]。 */
	STELLARERAVEHICLE_API int32 ClampSize(int32 SizeVoxels);

	/**
	 * 夹到 [MinSize, MaxSize]。
	 *
	 * 下限被抬到至少 1（尺寸 0 会造出空盒）；MinSize > MaxSize 时以下限为准，
	 * 而不是像 FMath::Clamp 那样断言 —— 这两个值是可配置项，写反了不该在
	 * 运行时崩。
	 */
	STELLARERAVEHICLE_API int32 ClampSize(int32 SizeVoxels, int32 MinSize, int32 MaxSize);

	/** 球半径 / 方块半边长，单位厘米。两者同式：Size * VoxelSize / 2。 */
	STELLARERAVEHICLE_API float GetHalfExtentCm(int32 SizeVoxels, float VoxelSizeCm);

	/** 区间版：先按 [MinSize, MaxSize] 夹尺寸，再算半边长。 */
	STELLARERAVEHICLE_API float GetHalfExtentCm(int32 SizeVoxels, float VoxelSizeCm,
		int32 MinSize, int32 MaxSize);

	/**
	 * 编辑中心。
	 *
	 * 挖：就是命中点，笔刷半埋在表面里。
	 * 放：沿命中法线外移一个半边长，让球 / 方块与地表相切而不是半埋。
	 */
	STELLARERAVEHICLE_API FVector GetEditCenterWorld(const FVector& HitLocation, const FVector& HitNormal,
		int32 SizeVoxels, float VoxelSizeCm, bool bPlace);

	/** 区间版：放的偏移量与笔刷半边长一致（用同一套 Min/Max 夹）。 */
	STELLARERAVEHICLE_API FVector GetEditCenterWorld(const FVector& HitLocation, const FVector& HitNormal,
		int32 SizeVoxels, float VoxelSizeCm, bool bPlace, int32 MinSize, int32 MaxSize);

	/**
	 * 把「行星体素空间里的浮点中心」换成整数盒。
	 *
	 * 尺寸 2 必须得到 2 格宽：Low = (Size-1)/2 = 0，High = Size-1-Low = 1。
	 * 用 ±Size/2 会得到 3 格——本盒的 Min 与 Max **两端都含**。
	 * 转成 FVoxelIntBox 时注意它的 Max 是开区间，要 `Max + 1` 才能保持格数。
	 */
	STELLARERAVEHICLE_API FPlanetEditBox MakeBox(const FVector& LocalCenterVoxels, int32 SizeVoxels);

	/** 区间版：盒宽 = 夹到 [MinSize, MaxSize] 之后的尺寸，每轴都是 Width 格。 */
	STELLARERAVEHICLE_API FPlanetEditBox MakeBox(const FVector& LocalCenterVoxels, int32 SizeVoxels,
		int32 MinSize, int32 MaxSize);

	/**
	 * 把两端都含的 FPlanetEditBox 换成右端开区间。
	 * FVoxelIntBox 的 Max 是开区间（"Exclusive"）：直接透传会让方块少一格。
	 */
	STELLARERAVEHICLE_API FPlanetEditVoxelBox MakeVoxelBox(const FPlanetEditBox& Box);

	/** 球体预览每条大圆的分段数。 */
	constexpr int32 PreviewSegmentsPerCircle = 32;

	/**
	 * 球体笔刷预览：过球心的三个正交大圆（XY / XZ / YZ）。
	 *
	 * **世界空间**：Center 与 RadiusCm 都直接是调用方算好的世界坐标，
	 * 球体对旋转不敏感，不需要行星变换。
	 */
	STELLARERAVEHICLE_API void BuildSpherePreviewSegments(const FVector& Center, float RadiusCm,
		TArray<FPlanetEditSegment>& OutSegments);

	/**
	 * 正方体笔刷预览：盒子 Min..MaxExclusive 的 12 条棱。
	 *
	 * 输出是**行星体素空间**坐标（单位：体素）；调用方负责用
	 * AVoxelWorld::LocalToGlobalFloat 换到世界空间。边界必须来自真正的
	 * MakeVoxelBox 结果，预览才会与挖/放的格子逐格一致。
	 */
	STELLARERAVEHICLE_API void BuildBoxPreviewSegments(const FIntVector& MinVoxels,
		const FIntVector& MaxExclusiveVoxels, TArray<FPlanetEditSegment>& OutSegments);
}
