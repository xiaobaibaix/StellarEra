// 体素调用器的幂等开关 —— IVoxelViewpointInvokers 的配套工具。
//
// 从原 StellarEraTerrain/Public/VoxelViewpointInvokers.h 拆出来的一半：
// 接口本身（只声明 SetViewpointInvokersActive，不碰任何 Voxel 类型）在同目录的
// VoxelViewpointInvokers.h；而这里的 SetEnabled 要用 UVoxelInvokerComponentBase，
// 只有依赖 Voxel 插件的模块才放得下，于是始终留在 Planet。
//
// 实现文件叫 VoxelViewpointInvokerUtils.cpp（与本文配对），不叫
// VoxelViewpointInvokers.cpp —— 那个名字已经被接口头占用了，同名会让
// 「.h 与 .cpp 是一对」的直觉失效。

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
