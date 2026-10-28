// Copyright Voxel Plugin SAS. All Rights Reserved.
// 行星内部矿体的等值面构建：纯函数，只产数据，不碰世界、不建组件、不读资产。

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"

class FPlanetGeologyField;

/**
 * 一个元素的矿体网格。顶点相对行星球心，单位厘米。
 *
 * 法线不在数据层：等值面抽取只给顶点与三角形，视图 Actor 转 FDynamicMesh3 时
 * 用 FMeshNormals 按顶点算法线（材质靠 VertexNormalWS 取它）。
 */
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
		 * 一个**特征尺度**（片层=全厚、脉=管直径、浸染=粒径、囊=囊直径）横向切多少格
		 * （4..32）。
		 *
		 * 这个旋钮跟着生成器的特征尺度走，与行星直径无关 —— 换一颗大行星，
		 * 相对尺寸与观测视图清晰度都不变。
		 */
		int32 CellsPerFeature = 12;
		/** 等值线，调用方传 Field.GetAppearance().ColorThreshold。 */
		float IsoValue = 0.f;

		/**
		 * 只给测试用：关掉稀疏块剪枝，盒内**每个**角点都求值。
		 *
		 * 稀疏剪枝只是少算、不改值（块外恒为 0，判据见 Build 的说明），所以两条路径
		 * 的网格必须逐顶点相同 —— `MineralSurface.SparseMatchesDense` 拿它当对照。
		 */
		bool bSampleEveryCorner = false;
	};

	struct FBuildResult
	{
		/** 下标 = 运行期元素下标（GetSampledElementDefinitions 顺序）；空元素留空网格。 */
		TArray<FPlanetMineralMesh> MeshesByElement;
		/**
		 * 每个元素的簇数，下标与 MeshesByElement 一一对齐（没矿囊的元素为 0）。
		 *
		 * 分簇是**按元素**做的（元素之间互不参与求值），所以这里的值就是该元素自己的
		 * 簇数，与其它元素的矿囊无关。
		 *
		 * 诊断用途：同元素的两个 AABB 相交的矿囊必须并成**一**簇 —— 分别建面会在
		 * 重叠处留下两张互穿的面；分簇判据被放松成「永远各建各的」时，值会变成 2。
		 */
		TArray<int32> ClustersByElement;
		/**
		 * 实际求过场的角点数，只进日志：按（元素 × 簇）累加。
		 *
		 * 稀疏剪枝之后它**小于**栅格盒子的角点数 —— 盒子里的空域整块跳过，不再逐点求值。
		 * 与 `OccupiedCells` 不同：那条统计的是场值高于等值线的角点，这条是求值次数。
		 */
		int32 GridPoints = 0;
		/** 每个元素求值的角点数，下标与 MeshesByElement 对齐。诊断「谁在吃预算」。 */
		TArray<int32> CornersByElement;
		/** 求场值的累计 CPU 秒。按（元素 × 簇）并行，所以可以大于 BuildSeconds。 */
		double SampleSeconds = 0.0;
		/** 等值面抽取（FMarchingCubes）的累计 CPU 秒。 */
		double MarchSeconds = 0.0;
		/**
		 * 各（元素 × 簇）任务墙钟的累计秒。
		 *
		 * JobSeconds / BuildSeconds 就是这次构建实际用到的并行核数：接近 1 说明
		 * 并行度被单个大任务卡死（任务粒度太大），远小于核数则是有大任务在拖尾。
		 */
		double JobSeconds = 0.0;
		/** （元素 × 簇）任务数，即并行单元数。 */
		int32 PairCount = 0;
		/**
		 * 撞到单任务 8'000'000 角点硬上限的任务数。
		 *
		 * 非 0 = 这批几何被**降采样**了（格边被放大、细节丢失），也是「构建为什么慢」
		 * 的直观指标：撞满的任务一个都不能少算，降 `CellsPerFeature` 也救不回它们。
		 */
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
	 * 按 AABB 相交把区域分簇（空间哈希 + 每格 sweep-and-prune）。
	 *
	 * OutClusterRoots[i] = 区域 i 所属簇的代表，取**簇内最小区域下标**。
	 * 这样两次构建的簇顺序由数据决定，而不是 TMap 的哈希顺序。
	 *
	 * 不能把「同格」直接当相交：两个小区域可以同格却相距很远，直接 union 会把
	 * 不相交的矿体并成一个大簇，簇盒变大 → 角点数暴涨。
	 *
	 * 退化分支：所有包围盒尺寸全零时格边 `CellSize` 为 0，函数直接返回，等价于
	 * 「各自成簇」。这道早退防的是空间哈希里的除零；它与上面的「AABB 相交即同簇」
	 * 在零尺寸盒上**不一致**（同点零尺寸盒按 `<=` / `>=` 的判据其实相交），是有意为之
	 * —— 没有格边就无法用哈希证明相交，宁可少合。正常生成器不会报出零尺寸区域，
	 * 这条只防退化输入。
	 */
	STELLARERAMINERALS_API void ClusterRegionBounds(const TArray<FBox>& BoundsCm,
		TArray<int32>& OutClusterRoots);

	/**
	 * 逐元素抽等值面。Out 先被清空；参数非法或场为空时返回空结果，不报错。
	 *
	 * **按元素分簇**：元素之间互不参与求值（等值面按 `SampleElementNormAtNormalized`
	 * 逐元素取），所以先按元素分组，再在组内按 AABB 相交分簇（空间哈希 + 并查集）。
	 * 跨元素分簇会让一个元素的包围盒去背别人的空域，实测把每个任务都顶到角点上限。
	 *
	 * **局部栅格**：每簇一套独立栅格，基准格边 = 2 × 簇内最小特征半径 / CellsPerFeature，
	 * 盒子 = 簇包围盒外扩一格。这样分辨率跟着特征尺度走，而不是跟着行星直径走。
	 *
	 * **稀疏块**：栅格按 8³ 格切块，只对与本簇区域包围盒相交的块逐点求值，其余块留 0。
	 * 依据是上面那条零支持域前提（块外恒为 0），所以它只省时间、不改网格。
	 *
	 * 基准格边有两条上限：每轴格数 ≤ 512、单任务总角点 ≤ 8'000'000。触发时
	 * **放大格边**（分辨率下降），不丢几何 —— 跳过簇会让整簇矿体凭空消失。
	 * 触发次数记在 `CappedPairs` 里。
	 *
	 * 外扩一格保证盒子边界上**本簇**矿体的角点场值为 0（每个分布的零支持域
	 * ⊆ 其 Region.Bounds），Marching Cubes 因此对这些矿囊在盒子内封闭 —— 漏了外扩
	 * 就会在面上开洞。
	 * 簇之间只保证 AABB 不相交、不保证相隔一格：同一元素其它簇的矿体若落进本簇
	 * 盒子的一格余量里，其等值面会在盒边被截断。稀疏块用的是**本簇**的区域包围盒，
	 * 对外来簇同样不设防 —— 两处是同一个已知缺口。
	 *
	 * 顶点仍会径向夹到球内（`|v| <= R`）：观察体的半透明壳就画在 R 上。
	 * 各元素场独立取面：两种元素同时高于门限的地方，两块矿体会互相穿插（共生矿囊），
	 * 不做 winner-take-all —— 布尔场做 MC 会退回方块状边缘。
	 */
	STELLARERAMINERALS_API void Build(const FPlanetGeologyField& Field, const FBuildParams& Params,
		FBuildResult& Out);
}
