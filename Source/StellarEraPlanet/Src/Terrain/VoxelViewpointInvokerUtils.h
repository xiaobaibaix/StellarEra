// 体素调用器的幂等开关 —— IVoxelViewpointInvokers 的配套工具。
//
// 从原 StellarEraTerrain/Public/VoxelViewpointInvokers.h 拆出来的一半：
// 接口本身（只声明 SetViewpointInvokersActive，不碰任何 Voxel 类型）搬到了
// Core/Public/Interfaces/VoxelViewpointInvokers.h，那里是三个 Pawn 与控制器
// 都能看见的位置；而这里的 SetEnabled 要用 UVoxelInvokerComponentBase，
// 只有依赖 Voxel 插件的模块才放得下，于是留在本模块。
//
// 文件名与接口头不同（多一个 Utils），是为了避免两个模块出现同名头导致
// include 解析到错误的那一份。

#pragma once

#include "CoreMinimal.h"

class UVoxelInvokerComponentBase;

namespace VoxelViewpointInvokers
{
	/**
	 * 幂等地把调用器切到 bEnabled。
	 *
	 * 插件的 EnableInvoker / DisableInvoker 在状态已经对时会往 Message Log 写
	 * "Invoker already enabled/disabled"（VoxelInvokerComponent.cpp:41 / :57）。
	 * 换视点是低频事件但会反复传同一个值，所以必须先比对再动手。
	 * 未注册的组件直接跳过 —— 注册状态由 OnRegister / OnUnregister 管。
	 */
	STELLARERAPLANET_API void SetEnabled(UVoxelInvokerComponentBase* Invoker, bool bEnabled);
}
