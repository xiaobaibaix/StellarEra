// 体素调用器的幂等开关 —— IVoxelViewpointInvokers 的配套工具。
// 接口本身（不碰任何 Voxel 类型）在 VoxelViewpointInvokers.h；这里因为要用
// UVoxelInvokerComponentBase，只能留在依赖 Voxel 插件的 Planet。

#pragma once

#include "CoreMinimal.h"

class UVoxelInvokerComponentBase;

namespace VoxelViewpointInvokers
{
	/**
	 * 幂等地把调用器切到 bEnabled。插件在状态已对时仍会写
	 * "Invoker already enabled/disabled"，而换视点会反复传同一个值，故先比对。
	 * 未注册的组件直接跳过。
	 */
	STELLARERAPLANET_API void SetEnabled(UVoxelInvokerComponentBase* Invoker, bool bEnabled);
}
