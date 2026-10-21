// Copyright Voxel Plugin SAS. All Rights Reserved.
// 把任意 Actor 变成 N 体系统里的一个天体。

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CosmosOrbitComponent.generated.h"

/**
 * 天体组件：挂在任意 Actor 上，它就参与 N 体引力。
 *
 * 刻意做成「组件」而不是「Actor 基类」：
 *   - 恒星不需要新类 —— 一个 StaticMeshActor 挂上它就是天体，不必为它写一个
 *     只有网格的恒星 Actor；
 *   - 行星也不需要 —— AVoxelPlanetActor 挂上它就是天体，不必让体素星球去继承
 *     一个 N 体的基类。
 *
 * 组件本身**不推进物理**，也不 Tick：它只存参数。注册、推进、写回位置全在
 * UCosmosSimulationSubsystem 里 —— N 体是耦合系统（A 受 B 的力，B 也受 A 的力），
 * 必须有唯一的编排者。
 *
 * 它只负责**物理**（质量、初速、是否施力、是否模拟）。轨迹的渲染与显示参数在
 * UCosmosTrajectoryComponent 上 —— 想监控哪个天体，就给它的 Actor 挂一个轨迹
 * 组件。
 *
 * ── 初始状态从哪来 ────────────────────────────────────────────
 * **组件不生成初始状态，只读取它。** 一个天体的初态就两项：
 *     位置 = Actor 在关卡里摆的位置（GetActorLocation，组件不碰它）
 *     速度 = InitialVelocityKmPerSecond
 * 想在哪儿放星球就在视口里拖到哪儿，想让它飞多快就填 InitialVelocityKmPerSecond。
 * 没有轨道根数那种中间层，也没有任何「开局把 Actor 搬到别处」的步骤 ——
 * 关卡里摆成什么样，运行后就从什么样开始跑。
 */
UCLASS(ClassGroup = (Cosmos), meta = (BlueprintSpawnableComponent))
class UCosmosOrbitComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCosmosOrbitComponent();

	/**
	 * 质量，单位 **地球质量 M⊕**（1 M⊕ = 3.0034893e-6 M☉）。默认 1 = 地球。
	 *
	 * 行星之间的引力相对恒星可以忽略，但质量仍是决定「谁绕谁」的唯一依据 ——
	 * 中心天体必须是这里面最大的那个，否则行星会直接飞出去。
	 *
	 * 单位换算：1 M☉ = 332946.0837 M⊕。中心恒星填 10000 M☉ 就是
	 * 3.329460837e9 M⊕（求解器内部仍用 M☉）。
	 * 必须 > 0；<= 0（含 NaN）的天体在注册时会被跳过并报错。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Body",
		meta = (DisplayName = "质量（地球质量 M⊕）"))
	double MassEarth = 1.0;

	/**
	 * 是否作为**引力源**吸引其他天体。默认 true。
	 *
	 * 关掉后该天体仍被所有天体吸引（照常受引力、走接触），但它不再吸引别人 ——
	 * 求解器里标记为「测试粒子」（FCosmosBodyState::bTestParticle），它作为引力源
	 * 的质量按 0 计。
	 *
	 * 典型用途是玩家飞船：飞船不该把行星从轨道上拽动。**接触解算不受影响** ——
	 * 它仍按真实 MassEarth 分配让路，撞地行为与开不开这一项无关。
	 *
	 * 与 MassEarth、InitialVelocityKmPerSecond 一样，**注册时读一次**
	 * （BeginPlay 后首个 Tick）。PIE 中途改这一项要重开 PIE 才生效。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Body")
	bool bExertsGravity = true;

	/**
	 * 初速度，单位 **km/s（物理速度）**。**天体初速度的唯一来源。**
	 *
	 * 留零即静止，天体只受引力作用。方向与大小都照填，没有任何推导或修正。
	 *
	 * 物理换算：1 AU/年 = 4.7404704635 km/s，地球的 2π AU/年 ≈ 29.79 km/s。
	 * 圆轨道速度先按求解器单位算：v[AU/年] = 2π√(M[M☉]/r[AU])，再乘
	 * 4.7404704635 换成 km/s。M[M☉] = MassEarth × 3.0034893e-6。
	 *
	 * **物理速度 ≠ HUD 上的世界速度**：1 AU/年 = CentimetersPerAu × TimeScale
	 * 厘米/秒（默认 3330 cm/s）。两边都正确，只是两个不同的量 —— 见 spec §3.2。
	 *
	 * **注意**：速度箭头画的是求解器里的**当前**速度，不是这一项 —— 这一项只决定开局。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Body",
		meta = (DisplayName = "初速度（km/s，物理速度）"))
	FVector InitialVelocityKmPerSecond = FVector::ZeroVector;

	// ── 旧字段：只保留序列化与 PostLoad 迁移，代码不再消费 ──
	// 2026-10-19 之前存下来的资产用它们；迁移后清零。等所有资产保存过一轮、
	// 确认没有蓝图引用后再单独提交删除。

	UPROPERTY(BlueprintReadWrite, Category = "Cosmos|Legacy")
	double MassSolar = 0.0;

	UPROPERTY(BlueprintReadWrite, Category = "Cosmos|Legacy")
	FVector InitialVelocity = FVector::ZeroVector;

	/**
	 * 是否每帧把求解器算出的位置写回所属 Actor。
	 *
	 * 为真：求解器是位置的主人，Actor 每帧被搬到求解器算出的位置上，两边永远一致。
	 *       想让星球看起来真的在动就用它。
	 * 为假：Actor 待在原地，但求解器里那个天体照样受引力、照样漂 —— 两边会分叉，
	 *       只适合纯装饰的远处天体（不需要看起来在动，也不想付 SetActorLocation
	 *       的代价）。
	 *
	 * 注意这个开关**不决定初始状态**：不论真假，初态都是「摆放位置 + InitialVelocityKmPerSecond」。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Body")
	bool bSimulated = true;

	/** 求解器里的索引。运行时由子系统填写，不要手改。 */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Cosmos")
	int32 SimulatorIndex = INDEX_NONE;

	//~ Begin UActorComponent Interface
	virtual void PostLoad() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End UActorComponent Interface
};
