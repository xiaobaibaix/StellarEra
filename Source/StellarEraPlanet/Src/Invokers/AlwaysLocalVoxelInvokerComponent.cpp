// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "AlwaysLocalVoxelInvokerComponent.h"
#include "Engine/World.h"

namespace
{
	/**
	 * 编辑器世界不跑 BeginPlay / OnPossess，门控到不了这里，而基类 OnRegister 会
	 * 无条件 EnableInvoker —— 摆进关卡的飞船、蓝图内嵌的相机就会在编辑视口参与 LOD。
	 * PIE / Game 是另一个世界、组件重新注册，门控会按视点重新打开。
	 */
	void DisableInvokerInEditorWorld(UVoxelInvokerComponentBase* const Invoker)
	{
		const UWorld* const World = Invoker ? Invoker->GetWorld() : nullptr;
		if (!World ||
			(World->WorldType != EWorldType::Editor && World->WorldType != EWorldType::EditorPreview))
		{
			return;
		}

		// 无条件 DisableInvoker 会报 "Invoker already disabled"。
		if (Invoker->IsInvokerEnabled())
		{
			Invoker->DisableInvoker();
		}
	}
}

bool UAlwaysLocalVoxelInvokerComponent::IsLocalInvoker_Implementation() const
{
	// 见头文件：本地与否只决定「要不要参与 LOD」，与 Possess 无关。
	return true;
}

void UAlwaysLocalVoxelInvokerComponent::OnRegister()
{
	Super::OnRegister();
	DisableInvokerInEditorWorld(this);
}

bool UAlwaysLocalVoxelInvokerWithPredictionComponent::IsLocalInvoker_Implementation() const
{
	// 同上。预测版与简单版是父子类，覆写各写一份，不共用实现。
	return true;
}

void UAlwaysLocalVoxelInvokerWithPredictionComponent::OnRegister()
{
	Super::OnRegister();
	DisableInvokerInEditorWorld(this);
}
