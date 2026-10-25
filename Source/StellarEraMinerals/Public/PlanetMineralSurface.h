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
	/** 一次构建的输入。半径为 0、体素为 0 或格数 < 8 时 Build 直接给空结果。 */
	struct FBuildParams
	{
		/** 基准球面半径（厘米），与 AVoxelPlanetActor::GetSurfaceRadiusCm 一致。 */
		float SurfaceRadiusCm = 0.f;
		/** 一个体素多少厘米：地质深度以体素计，换回厘米要用它。 */
		float VoxelSizeCm = 0.f;
		/** 每轴格数（角点数 = GridResolution + 1）。40 首进约 0.1–0.3 秒。 */
		int32 GridResolution = 40;
		/** 等值线，调用方传 Field.GetAppearance().ColorThreshold。 */
		float IsoValue = 0.f;
	};

	struct FBuildResult
	{
		/** 下标 = 运行期元素下标（GetSampledElementDefinitions 顺序）；空元素留空网格。 */
		TArray<FPlanetMineralMesh> MeshesByElement;
		/** 实际求过噪声的角点数（球外点不算），只进日志。 */
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
		int32 GridResolution = 0;

		bool operator==(const FCacheKey& Other) const
		{
			// 半径以厘米计：容差 0.01 cm（0.1 mm）之内视为同一颗行星。
			return Planet == Other.Planet
				&& FMath::IsNearlyEqual(SurfaceRadiusCm, Other.SurfaceRadiusCm, 0.01f)
				&& Scale.Equals(Other.Scale, 0.01)
				&& GridResolution == Other.GridResolution;
		}
		bool operator!=(const FCacheKey& Other) const { return !(*this == Other); }
	};

	/**
	 * 逐元素抽等值面。Out 先被清空；参数非法或场为空时返回空结果，不报错。
	 *
	 * 只有球外（深度 < 0）的角点按 0 处理、不花噪声求值；球内所有深度都要求值
	 * （矿囊可以贴到球心，没有「太深」的早退）。
	 * 顶点会被径向夹到球内（`|v| <= R`）：球外场值为 0，等值线插值会把顶点落到
	 * 球外，而 spec §7 禁止顶点出球（观察体的半透明壳就画在 R 上）。
	 * 各元素场独立取面：两种元素同时高于门限的地方，两块矿体会互相穿插（共生矿脉），
	 * 不做 winner-take-all —— 布尔场做 MC 会退回方块状边缘。
	 */
	STELLARERAMINERALS_API void Build(const FPlanetGeologyField& Field, const FBuildParams& Params,
		FBuildResult& Out);
}
