// Copyright Voxel Plugin SAS. All Rights Reserved.
//
// 天体轨迹渲染：预测轨迹（浅）、已走过的拖尾（深）、速度矢量。
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

	/**
	 * 把颜色朝白色插值 Amount（0 = 原色，1 = 纯白）。alpha 不动。
	 *
	 * 星图里选中天体的高亮就是它 —— **只改颜色，不加粗**。加粗要传非零线宽，
	 * 而行批把「线宽 == 0」走成真正的 1 像素 GPU 线、非零走成世界单位的四边形
	 * （见 .cpp 里 LineThickness 的注释），等于把选中的那条线切到另一条渲染路径。
	 * 那条路径只能人眼验，不值得为一个高亮引入一个只能靠肉眼发现的失败面。
	 */
	static FColor BrightenTowardWhite(const FColor& In, float Amount);

	//~ Begin UActorComponent Interface
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
	//~ End UActorComponent Interface

private:
	/**
	 * 实际的预测窗口（年）：
	 *     手填优先
	 *     → 1.05 × 估算周期（**只解析一次**）
	 *     → 再钳到「MaxAutoPredictionPeriods × 系统最短周期」
	 *     → 都算不出时回退常量。
	 *
	 * 手填与上限都不缓存，每帧重判，运行期改了立刻生效；只有「1.05 × 估算周期」
	 * 那一步的结果被缓存，理由见实现处。上限为什么按最短周期的倍数算、
	 * 而不是按绝对年数，见 UCosmosOrbitComponent::MaxAutoPredictionPeriods。
	 */
	double ResolvePredictionHorizonYears(const UCosmosSimulationSubsystem& Subsystem) const;

	/** 把一段折线追加进 Lines。 */
	void AppendPolyline(TArrayView<const FVector> Points, const FLinearColor& Color);

	/**
	 * 把速度矢量（杆 + 两段箭头）追加进 Lines。
	 *
	 * 锚点是天体**当前**位置，方向与长度读求解器**当前**速度 —— 都不是配置的
	 * InitialVelocity。速度要向子系统要，所以 Subsystem 得传进来。
	 */
	void AppendVelocityArrow(const UCosmosSimulationSubsystem& Subsystem, const FLinearColor& Color);

	/**
	 * 把已经画上去的线清一次（bDrewLastFrame 为真时才真清）。
	 *
	 * 线用 LifeTime = 0 画上去就永不过期，所以「这一帧不画了」必须显式清，
	 * 否则最后一帧的线会永远留在屏幕上。两个显示门控都要用它，抽出来避免重复。
	 */
	void ClearDrawnLines();

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

	/**
	 * 「1.05 × 估算周期」的解析结果（年），以及它是否已经解析过。
	 *
	 * 解析一次就定住 —— 每帧重算的话窗口会跟着当前间距跑（周期 ∝ 间距^1.5），
	 * 线长会变、最近邻一换还会跳。只在**真的算出周期**时才置位
	 * bAutoHorizonResolved，所以注册还没轮到、SimulatorIndex 还是 INDEX_NONE
	 * 的那几帧不会把回退值钉死。
	 *
	 * mutable 是因为 ResolvePredictionHorizonYears 是 const —— 它只读 Orbit 与
	 * 子系统，缓存是它的内部记忆，不改变任何对外可见的状态。
	 */
	mutable double CachedAutoHorizonYears = 0.0;
	mutable bool bAutoHorizonResolved = false;

	/** 复用的中间数组，避免每帧重新分配。 */
	TArray<FVector> PredictedWorld;
	TArray<FBatchedLine> Lines;

	/** 上一帧画过没有 —— 关掉时只清一次，不必每帧 MarkRenderStateDirty。 */
	bool bDrewLastFrame = false;
};
