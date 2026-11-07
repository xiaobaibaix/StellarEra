// Copyright Voxel Plugin SAS. All Rights Reserved.
// 矿物透视模式的两张材质：半透明球壳、不透明矿体（等值面 mesh）。

#pragma once

#include "CoreMinimal.h"

/** 矿物透视材质：路径 getter 在 Game/Editor 都可用；只有 Build 需要编辑器（直接构造 UMaterial 并落盘）。 */
namespace PlanetMineralMaterials
{
	/** `/Game/Code/Planet/Materials/M_PlanetGhost`（包路径，不含 `.AssetName`）。 */
	STELLARERAPLANET_API const TCHAR* GetGhostMaterialPath();

	/** `/Game/Code/Planet/Materials/M_MineralBlock`（包路径，不含 `.AssetName`）。 */
	STELLARERAPLANET_API const TCHAR* GetBlockMaterialPath();

#if WITH_EDITOR
	/**
	 * 用 C++ 直接构造 UMaterial 并落盘：新建表达式 → GetExpressionCollection() → 连 EditorOnlyData 插槽 → PostEditChange → SavePackage。
	 * 改完参数重跑 StellarEra.BuildMineralMaterials 即可。仅编辑器可用，Game 目标运行时按上面的路径加载资产。
	 */
	STELLARERAPLANET_API bool Build(FString& OutError);
#endif // WITH_EDITOR
}
