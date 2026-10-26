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
	};

	struct FBuildResult
	{
		/** 下标 = 运行期元素下标（GetSampledElementDefinitions 顺序）；空元素留空网格。 */
		TArray<FPlanetMineralMesh> MeshesByElement;
		/**
		 * 每个元素**参与**的簇数，下标与 MeshesByElement 一一对齐（没矿囊的元素为 0）。
		 *
		 * 一个簇可以同时含多个元素的矿囊；这样的簇对该元素各计一次。所以这个数组
		 * 之和 ≥ 实际簇数，只有单元素场才相等。
		 *
		 * 诊断用途：同元素的两个 AABB 相交的矿囊必须并成**一**簇 —— 分别建面会在
		 * 重叠处留下两张互穿的面；分簇判据被放松成「永远各建各的」时，值会变成 2。
		 */
		TArray<int32> ClustersByElement;
		/**
		 * 实际求过场的角点数，只进日志：按（簇 × 元素）累加。
		 * 含球外角点与簇内其他元素占用过的角点，**会重复计数**。
		 */
		int32 GridPoints = 0;
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
	 */
	STELLARERAMINERALS_API void ClusterRegionBounds(const TArray<FBox>& BoundsCm,
		TArray<int32>& OutClusterRoots);

	/**
	 * 逐元素抽等值面。Out 先被清空；参数非法或场为空时返回空结果，不报错。
	 *
	 * **局部栅格**：按区域的空间邻近关系分簇（未外扩的 AABB 相交即同簇，空间哈希 + 并查集），
	 * 每簇一套独立栅格，基准格边 = 2 × 簇内最小特征半径 / CellsPerFeature，盒子 = 簇包围盒
	 * 外扩一格。这样分辨率跟着矿囊走，而不是跟着行星直径走。
	 *
	 * 基准格边有两条上限：每轴格数 ≤ 512、单簇单元素总角点 ≤ 8'000'000。触发时
	 * **放大格边**（分辨率下降），不丢几何 —— 跳过簇会让整簇矿囊凭空消失。
	 *
	 * 外扩一格保证盒子边界上**本簇**矿体的角点场值为 0（每个分布的零支持域
	 * ⊆ 其 Region.Bounds），Marching Cubes 因此对这些矿囊在盒子内封闭 —— 漏了外扩
	 * 就会在面上开洞。
	 * 簇之间只保证 AABB 不相交、不保证相隔一格：同一元素其它簇的矿囊若落进本簇
	 * 盒子的一格余量里，其等值面会在盒边被截断。
	 *
	 * 顶点仍会径向夹到球内（`|v| <= R`）：观察体的半透明壳就画在 R 上。
	 * 各元素场独立取面：两种元素同时高于门限的地方，两块矿体会互相穿插（共生矿囊），
	 * 不做 winner-take-all —— 布尔场做 MC 会退回方块状边缘。
	 */
	STELLARERAMINERALS_API void Build(const FPlanetGeologyField& Field, const FBuildParams& Params,
		FBuildResult& Out);
}
