// Copyright Voxel Plugin SAS. All Rights Reserved.
// 矿物透视模式的两张材质：半透明球壳、不透明方块。

#pragma once

#include "CoreMinimal.h"

#if WITH_EDITOR

/**
 * 用 C++ 直接构造 UMaterial 并落盘，不依赖 UnrealEd 的资产创建流程。
 *
 * 与 PlanetAtmosphereMaterialBuilder.cpp 同一套路：新建表达式 → 挂到
 * GetExpressionCollection() → 连 EditorOnlyData 的插槽 → PostEditChange → SavePackage。
 * 改完参数重跑 `StellarEra.BuildMineralMaterials` 即可。
 */
namespace PlanetMineralMaterials
{
	/** `/Game/Materials/M_PlanetGhost`（包路径，不含 `.AssetName`）。 */
	STELLARERAPLANET_API const TCHAR* GetGhostMaterialPath();

	/** `/Game/Materials/M_MineralBlock`（包路径，不含 `.AssetName`）。 */
	STELLARERAPLANET_API const TCHAR* GetBlockMaterialPath();

	/** 重建两张材质并落盘；已存在的同名对象会被复用并清空表达式。 */
	STELLARERAPLANET_API bool Build(FString& OutError);
}

#endif // WITH_EDITOR
