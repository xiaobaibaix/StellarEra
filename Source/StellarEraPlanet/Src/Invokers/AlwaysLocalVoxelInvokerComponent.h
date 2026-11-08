// Copyright Voxel Plugin SAS. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VoxelComponents/VoxelInvokerComponent.h"
#include "AlwaysLocalVoxelInvokerComponent.generated.h"

/**
 * 永远算作「本地调用器」的体素调用器。插件默认绑 IsLocallyControlled，进星图
 * Possess 相机时飞船/角色的调用器会掉出 LOD，触发整棵八叉树重建并报 VoxelFree
 * 的 ensure。这里固定返回 true：「归谁控制」与「该不该参与 LOD」是两件事。
 */
UCLASS()
class STELLARERAPLANET_API UAlwaysLocalVoxelInvokerComponent : public UVoxelSimpleInvokerComponent
{
	GENERATED_BODY()

public:
	//~ Begin UVoxelInvokerComponentBase Interface
	virtual bool IsLocalInvoker_Implementation() const override;
	virtual void OnRegister() override;
	//~ End UVoxelInvokerComponentBase Interface
};

/**
 * 带速度预测的那一档，语义同上。两个 UCLASS 是父子关系但各继承各的，
 * IsLocalInvoker 覆写不能共用。
 */
UCLASS()
class STELLARERAPLANET_API UAlwaysLocalVoxelInvokerWithPredictionComponent
	: public UVoxelInvokerWithPredictionComponent
{
	GENERATED_BODY()

public:
	//~ Begin UVoxelInvokerComponentBase Interface
	virtual bool IsLocalInvoker_Implementation() const override;
	virtual void OnRegister() override;
	//~ End UVoxelInvokerComponentBase Interface

protected:
	/**
	 * 预测点改用相对参考系的速度（见 IVoxelInvokerPredictionReference），不再直接吃
	 * GetOwner()->GetVelocity() 的绝对速度 —— 那里面含着行星公转的几 km/s。
	 */
	virtual FVector GetInvokerGlobalPosition_Implementation() const override;
};
