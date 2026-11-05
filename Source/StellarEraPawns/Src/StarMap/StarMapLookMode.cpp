// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "StarMapLookMode.h"

EStarMapLookMode ResolveStarMapLookMode(
	const bool bRightHeld,
	const bool bLeftHeld,
	const bool bMiddleHeld,
	const bool bEditMode,
	const bool bContinuousDigging,
	const bool bActiveMineralView)
{
	// 连续挖期间一个键都不放行：挖掘点跟着光标走，视角一动路径就画歪了。
	// 转头也算 —— 视线一变，屏幕射线跟着变，挖掘点一样会漂。
	if (bContinuousDigging)
	{
		return EStarMapLookMode::None;
	}

	// 右键优先：它只转头、不挪相机，与同时按住的其它键不冲突。
	if (bRightHeld)
	{
		return EStarMapLookMode::Aim;
	}

	// 中键绕行不看模式：编辑 / 矿物透视下左键被借走时，它就是唯一的绕行键。
	if (bMiddleHeld)
	{
		return EStarMapLookMode::Orbit;
	}

	// 左键在编辑模式（挖掘 / 填充）与矿物透视（图例点击）里已经被借走，
	// 这两种状态下按下左键必须什么都不做。
	if (bLeftHeld && !bEditMode && !bActiveMineralView)
	{
		return EStarMapLookMode::Orbit;
	}

	return EStarMapLookMode::None;
}
