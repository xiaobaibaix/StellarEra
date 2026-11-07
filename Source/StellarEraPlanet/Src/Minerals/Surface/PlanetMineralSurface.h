// Copyright Voxel Plugin SAS. All Rights Reserved.
// 行星内部矿体的等值面构建：纯函数，只产数据，不碰世界、不建组件、不读资产。

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"

class FPlanetGeologyField;

/** 一个元素的矿体网格。顶点相对行星球心，单位厘米；法线由视图 Actor 转 FDynamicMesh3 时按顶点计算。 */
struct FPlanetMineralMesh
{
	TArray<FVector3f> VerticesCm;
	/** 每 3 个 int32 一个三角形，索引进 VerticesCm。 */
	TArray<int32> Triangles;
	/** 场值高于等值线的角点数，只进日志（诊断「这个元素为什么没面」）。 */
	int32 OccupiedCells = 0;

	bool IsEmpty() const { return Triangles.Num() == 0; }
};

namespace PlanetMineralSurface
{
	/** 一次构建的输入。半径 ≤ 0、体素 ≤ 0 或 CellsPerFeature < 4 时 Build 直接给空结果。 */
	struct FBuildParams
	{
		/** 基准球面半径（厘米），与 AVoxelPlanetActor::GetSurfaceRadiusCm 一致。 */
		float SurfaceRadiusCm = 0.f;
		/** 仅保留作合法性校验（≤ 0 即拒绝）；矿囊取面不再经过体素深度。 */
		float VoxelSizeCm = 0.f;
		/**
		 * 一个特征尺度（片层=全厚、脉=管直径、浸染=粒径、囊=囊直径）横向切多少格（4..32）。
		 * 跟随生成器特征尺度，与行星直径无关 —— 换大行星时相对尺寸与视图清晰度都不变。
		 */
		int32 CellsPerFeature = 12;
		/** 等值线，调用方传 Field.GetAppearance().ColorThreshold。 */
		float IsoValue = 0.f;

		/**
		 * 只给测试用：关掉稀疏块剪枝，盒内每个角点都求值。
		 * 剪枝只少算、不改值，故两条路径网格必须逐顶点相同（SparseMatchesDense 拿它对当）。
		 */
		bool bSampleEveryCorner = false;
	};

	struct FBuildResult
	{
		/** 下标 = 运行期元素下标（GetSampledElementDefinitions 顺序）；空元素留空网格。 */
		TArray<FPlanetMineralMesh> MeshesByElement;
		/**
		 * 每个元素的簇数，下标与 MeshesByElement 对齐（无矿囊为 0）。分簇按元素做，值为该元素自身簇数。
		 * 诊断用：同元素两个 AABB 相交的矿囊必须并成一簇，否则重叠处会留两张互穿的面。
		 */
		TArray<int32> ClustersByElement;
		/** 实际求过场的角点数（元素 × 簇累加），只进日志；稀疏剪枝后小于栅格盒角点数。 */
		int32 GridPoints = 0;
		/** 每个元素求值的角点数，下标与 MeshesByElement 对齐。诊断「谁在吃预算」。 */
		TArray<int32> CornersByElement;
		/** 求场值的累计 CPU 秒。按（元素 × 簇）并行，所以可以大于 BuildSeconds。 */
		double SampleSeconds = 0.0;
		/** 等值面抽取（FMarchingCubes）的累计 CPU 秒。 */
		double MarchSeconds = 0.0;
		/** 各（元素 × 簇）任务墙钟累计秒。JobSeconds / BuildSeconds ≈ 实际并行核数：接近 1 说明被单个大任务卡死。 */
		double JobSeconds = 0.0;
		/** （元素 × 簇）任务数，即并行单元数。 */
		int32 PairCount = 0;
		/** 撞到单任务 8'000'000 角点硬上限的任务数。非 0 = 几何被降采样（格边放大、细节丢失），降 CellsPerFeature 也救不回。 */
		int32 CappedPairs = 0;
		double BuildSeconds = 0.0;
	};

	/** 组件缓存的键：任一项变化都要重算。 */
	struct FCacheKey
	{
		TWeakObjectPtr<const UObject> Planet;
		float SurfaceRadiusCm = 0.f;
		/** 行星 Actor 缩放。S ≠ 1 时厘米换算本来就错，进键里至少不会静默复用旧网格。 */
		FVector Scale = FVector::ZeroVector;
		int32 CellsPerFeature = 0;

		bool operator==(const FCacheKey& Other) const
		{
			// 半径以厘米计：容差 0.01 cm（0.1 mm）之内视为同一颗行星。
			return Planet == Other.Planet
				&& FMath::IsNearlyEqual(SurfaceRadiusCm, Other.SurfaceRadiusCm, 0.01f)
				&& Scale.Equals(Other.Scale, 0.01)
				&& CellsPerFeature == Other.CellsPerFeature;
		}
		bool operator!=(const FCacheKey& Other) const { return !(*this == Other); }
	};

	/**
	 * 按 AABB 相交把区域分簇（空间哈希 + 每格 sweep-and-prune）。OutClusterRoots[i] 为簇代表（簇内最小区域下标），
	 * 使两次构建的簇顺序由数据而非 TMap 哈希顺序决定。不能把「同格」当相交：小区域可同格却相距很远，会合出大簇。
	 * 全零尺寸时格边为 0，直接返回（各自成簇），防空间哈希除零；与「AABB 相交即同簇」在零尺寸盒上故意不一致。
	 */
	STELLARERAPLANET_API void ClusterRegionBounds(const TArray<FBox>& BoundsCm,
		TArray<int32>& OutClusterRoots);

	/**
	 * 逐元素抽等值面。Out 先清空；参数非法或场为空时给空结果，不报错。
	 * 按元素分簇（跨元素分簇会让一个元素的包围盒背别人的空域，把任务顶到角点上限）；每簇独立局部栅格，
	 * 格边 = 2 × 簇内最小特征半径 / CellsPerFeature，按 8³ 稀疏块只对相交块求值（只省时间、不改网格）。
	 * 每轴 ≤ 512 格、单任务 ≤ 8'000'000 角点时放大格边而不丢几何（记入 CappedPairs）；盒子外扩一格防开洞，顶点径向夹到 R。
	 */
	STELLARERAPLANET_API void Build(const FPlanetGeologyField& Field, const FBuildParams& Params,
		FBuildResult& Out);
}
