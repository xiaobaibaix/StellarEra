// Copyright Voxel Plugin SAS. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VoxelComponents/VoxelInvokerComponent.h"
#include "AlwaysLocalVoxelInvokerComponent.generated.h"

/**
 * 永远算作「本地调用器」的体素调用器。
 *
 * 插件的 UVoxelInvokerComponentBase::IsLocalInvoker_Implementation
 * （VoxelInvokerComponent.cpp:21）只认 `Cast<APawn>(GetOwner())->IsLocallyControlled()`：
 * 一个 Pawn 只要没被本地控制器 Possess，它的调用器就会被
 * FVoxelDefaultLODManager::UpdateInvokers（VoxelDefaultLODManager.cpp:218）
 * 用 `bUseForLOD &= IsLocalInvoker()` 清零。
 *
 * 这对我们是错的。进星图 = Possess(星图相机)，飞船和角色的调用器会在同一帧一起
 * 掉出 LOD；下一次 LOD 更新发现 `bUseForLOD changed`（同文件 :235）就重建整棵
 * 八叉树。这时若行星还在生成、chunk 任务还在飞，这次全量重排会踩中 VoxelFree 的
 * chunk 簿记（VoxelDefaultRenderer.cpp:1249 / 846 / 302），进出星图各报 ensure。
 *
 * 所以这里把 IsLocalInvoker 固定为 true：调用器「归不归本地玩家控制」与它「该不该
 * 参与 LOD」是两件事 —— 单机下飞船和角色本来就永远属于本地玩家，星图相机就是玩家
 * 当下的视点。Possess 换人不再改变任何 bUseForLOD，进出星图不再触发全量 LOD 更新。
 *
 * 代价是没被控制时调用器仍然计数：飞船停在行星上时保留自己的 LOD/碰撞气泡（本来
 * 也应该有，否则它就缺一块脚下的地形），角色进星图时脚下 200 米的精细块不回收
 * （回来时不用重新生成）。都是想要的方向。
 */
UCLASS()
class STELLARERAPLANET_API UAlwaysLocalVoxelInvokerComponent : public UVoxelSimpleInvokerComponent
{
	GENERATED_BODY()

public:
	//~ Begin UVoxelInvokerComponentBase Interface
	virtual bool IsLocalInvoker_Implementation() const override;
	//~ End UVoxelInvokerComponentBase Interface
};

/**
 * 带速度预测的那一档，语义与 UAlwaysLocalVoxelInvokerComponent 相同。
 *
 * 飞船用的是 UVoxelInvokerWithPredictionComponent，它与 UVoxelSimpleInvokerComponent
 * 是父子关系，但 IsLocalInvoker 的覆写不能共用 —— 两个 UCLASS 各继承各的。
 */
UCLASS()
class STELLARERAPLANET_API UAlwaysLocalVoxelInvokerWithPredictionComponent
	: public UVoxelInvokerWithPredictionComponent
{
	GENERATED_BODY()

public:
	//~ Begin UVoxelInvokerComponentBase Interface
	virtual bool IsLocalInvoker_Implementation() const override;
	//~ End UVoxelInvokerComponentBase Interface
};
