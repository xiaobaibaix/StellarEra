// Copyright Voxel Plugin SAS. All Rights Reserved.
// 星图鼠标位移的分派规则：纯枚举 + 纯函数，不碰任何 UObject，因此能住在 Pawns 里。

#pragma once

#include "CoreMinimal.h"

/**
 * 星图里鼠标位移该拿去干什么。
 *
 * - `Orbit`：绕焦点转（相机位置跟着变），见 AStarMapCameraPawn::OrbitLook。
 * - `Aim`：只转视线朝向（相机位置不变），见 AStarMapCameraPawn::AimLook。
 * - `None`：正在编辑 / 连续挖，视角一律不动。
 *
 * 与「哪个键按着」分开建模：右键在普通模式与编辑模式里都归 Aim，左键在编辑
 * 模式里归挖掘（None）—— 一个 bool 表达不了这种三路分派。
 */
enum class EStarMapLookMode : uint8
{
	None,
	Orbit,
	Aim,
};

/**
 * 星图的鼠标位移该拿去哪。
 *
 * 星图的鼠标位移只在按住鼠标（CaptureDuringMouseDown）时才有值，所以
 * 「按的是哪个键」只能靠按住的标志位区分。分派规则（按优先级）：
 *   - 连续挖期间一律 None —— 挖掘点跟着光标走，视角一动就画歪（转头也算，
 *     视线一变，屏幕射线就变了）。
 *   - 右键 → Aim：只转头、不挪相机，任何模式都可用。
 *   - 中键 → Orbit：编辑 / 矿物透视下左键被借走时，绕行改由它承担。
 *   - 左键 → Orbit，但编辑模式（挖掘 / 填充）与矿物透视（图例）下左键已被
 *     借走，只能是 None。
 *   - 都没按 → None。
 *
 * 右键优先于其它键：转头不改变相机位置，同时按住时先满足它更符合直觉。
 * 只决定「拿去哪」；缩放走滚轮，任何状态下都能用。
 */
STELLARERAPAWNS_API EStarMapLookMode ResolveStarMapLookMode(
	bool bRightHeld,
	bool bLeftHeld,
	bool bMiddleHeld,
	bool bEditMode,
	bool bContinuousDigging,
	bool bActiveMineralView);
