// Copyright Voxel Plugin SAS. All Rights Reserved.
//
// 天体轨迹渲染：预测轨迹（浅）、已走过的拖尾（深）、初始速度矢量。
//
// 这是一个**哑渲染器**：它不存任何面向用户的参数，参数全从兄弟
// UCosmosOrbitComponent 上读（那边在编辑器详情面板里能调、有取色器）。
// 它也不决定天体在哪 —— 位置由 UCosmosSimulationSubsystem 每帧写回 Actor，
// 它只读。

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/LineBatchComponent.h"
#include "CosmosTrajectoryComponent.generated.h"

class UCosmosOrbitComponent;
class UCosmosSimulationSubsystem;

/**
 * 把一颗天体的轨迹画出来。
 *
 * 由 UCosmosOrbitComponent::BeginPlay 用 NewObject + RegisterComponent 创建 ——
 * 于是不需要动蓝图、不需要手工加组件。
 *
 * 三样东西共用一个 ULineBatchComponent：预测折线、拖尾折线、速度箭头。
 * 线用 LifeTime = 0 画上去（永不过期），所以每帧先 Flush 再整批重画。
 *
 * tick group 是 TG_PostUpdateWork：必须排在 UCosmosSimulationSubsystem 把位置
 * 写回 Actor **之后**，否则拖尾采到的是上一帧的位置。实际上排错了也只差一帧
 * （约 4 米），肉眼看不出来，但没理由不排对。
 */
UCLASS(ClassGroup = (Cosmos))
class UCosmosTrajectoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCosmosTrajectoryComponent();

	/** 由 UCosmosOrbitComponent::BeginPlay 调用。InOrbit 必须非空。 */
	void Initialize(UCosmosOrbitComponent* InOrbit);

	//~ Begin UActorComponent Interface
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
	//~ End UActorComponent Interface

private:
	/** 实际的预测窗口（年）：手填优先 → 1.05 × 估算周期 → 回退常量。 */
	double ResolvePredictionHorizonYears(const UCosmosSimulationSubsystem& Subsystem) const;

	/** 把一段折线追加进 Lines。 */
	void AppendPolyline(TArrayView<const FVector> Points, const FLinearColor& Color);

	/** 把速度箭头（杆 + 两段箭头）追加进 Lines。 */
	void AppendVelocityArrow(const FLinearColor& Color);

	/** 参数来源。每帧重读，所以运行期改属性立刻生效。 */
	UPROPERTY(Transient)
	TObjectPtr<UCosmosOrbitComponent> Orbit;

	/** 画线用。自己造、自己注册。 */
	UPROPERTY(Transient)
	TObjectPtr<ULineBatchComponent> LineBatch;

	/**
	 * 拖尾的历史位置（世界坐标）。
	 *
	 * 用「先进先出」而不是环形缓冲：下标 0 是最旧的点、末尾是最新的点，直接顺着
	 * 画就是时间顺序，不需要任何索引换算。RemoveAt(0) 的 O(n) 搬迁只发生在**采样那一刻**
	 * （不是每帧），而采样率受模拟时间门限 TrailInterval 限制 —— 上限 = TimeScale /
	 * TrailInterval，与帧率无关。所以本关卡最坏也就每秒几 MB 的量级，完全不值得为它写一套取模索引。
	 *
	 * TrailTimes 是与它一一对应的采样时刻（模拟年）—— **两个数组只在一起改**，
	 * 长度恒等。裁剪按时间而不按点数（理由见 TrailSamples 的注释）。
	 */
	TArray<FVector> Trail;

	/** 与 Trail 一一对应的采样时刻（模拟年）。按时间裁剪要用它。 */
	TArray<double> TrailTimes;

	/** 上次采样时的模拟时间（年）。 */
	double LastTrailSampleTime = 0.0;

	/** 复用的中间数组，避免每帧重新分配。 */
	TArray<FVector> PredictedWorld;
	TArray<FBatchedLine> Lines;

	/** 上一帧画过没有 —— 关掉时只清一次，不必每帧 MarkRenderStateDirty。 */
	bool bDrewLastFrame = false;
};
