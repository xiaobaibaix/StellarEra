// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "AlwaysLocalVoxelInvokerComponent.h"

bool UAlwaysLocalVoxelInvokerComponent::IsLocalInvoker_Implementation() const
{
	// 见头文件：本地与否只决定「要不要参与 LOD」，与 Possess 无关。
	return true;
}

bool UAlwaysLocalVoxelInvokerWithPredictionComponent::IsLocalInvoker_Implementation() const
{
	// 同上。预测版与简单版是父子类，覆写各写一份，不共用实现。
	return true;
}
