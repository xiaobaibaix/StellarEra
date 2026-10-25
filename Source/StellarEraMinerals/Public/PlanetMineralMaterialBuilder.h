// Copyright Voxel Plugin SAS. All Rights Reserved.
// 矿物透视模式的两张材质：半透明球壳、不透明矿体（等值面 mesh）。

#pragma once

#include "CoreMinimal.h"

/**
 * 矿物透视材质：路径 getter 在 Game / Editor 目标都可用；
 * 只有 Build 需要编辑器（它直接构造 UMaterial 并落盘）。
 */
namespace PlanetMineralMaterials
{
	/** `/Game/Materials/M_PlanetGhost`（包路径，不含 `.AssetName`）。 */
	STELLARERAMINERALS_API const TCHAR* GetGhostMaterialPath();

	/** `/Game/Materials/M_MineralBlock`（包路径，不含 `.AssetName`）。 */
	STELLARERAMINERALS_API const TCHAR* GetBlockMaterialPath();

#if WITH_EDITOR
	/**
	 * 用 C++ 直接构造 UMaterial 并落盘，不依赖 UnrealEd 的资产创建流程。
	 *
	 * 与 PlanetAtmosphereMaterialBuilder.cpp 同一套路：新建表达式 → 挂到
	 * GetExpressionCollection() → 连 EditorOnlyData 的插槽 → PostEditChange → SavePackage。
	 * 改完参数重跑 `StellarEra.BuildMineralMaterials` 即可。
	 *
	 * 仅编辑器可用：Game 目标不编译实现，运行时请按上面的路径加载资产。
	 */
	STELLARERAMINERALS_API bool Build(FString& OutError);
#endif // WITH_EDITOR
}
