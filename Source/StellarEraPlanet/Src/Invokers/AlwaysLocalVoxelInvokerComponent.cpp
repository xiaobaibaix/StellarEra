// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "AlwaysLocalVoxelInvokerComponent.h"
#include "Engine/World.h"

namespace
{
	/**
	 * 编辑器世界（含蓝图预览）不跑 BeginPlay，也没有 OnPossess —— 视点门控一次都
	 * 到不了这里，而基类 OnRegister（VoxelInvokerComponent.cpp:81）会无条件
	 * EnableInvoker。结果是摆在关卡里的飞船、行星蓝图里内嵌的星图相机在编辑视口
	 * 里也参与 LOD。
	 *
	 * 只关编辑器世界：PIE / Game 是另一个世界、组件重新注册，BeginPlay 与
	 * OnPossess 的门控会按当前视点重新打开。插件对自家 UVoxelInvokerEditorComponent
	 * 用的正是同一手法，方向相反（VoxelWorldEditorControls.cpp:67）。
	 */
	void DisableInvokerInEditorWorld(UVoxelInvokerComponentBase* const Invoker)
	{
		const UWorld* const World = Invoker ? Invoker->GetWorld() : nullptr;
		if (!World ||
			(World->WorldType != EWorldType::Editor && World->WorldType != EWorldType::EditorPreview))
		{
			return;
		}

		// 基类只在 bStartsEnabled 为真时才启用；无条件调用 DisableInvoker 会报
		// "Invoker already disabled"。
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
