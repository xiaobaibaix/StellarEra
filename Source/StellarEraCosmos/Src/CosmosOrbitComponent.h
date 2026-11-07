// Copyright Voxel Plugin SAS. All Rights Reserved.
// 把任意 Actor 变成 N 体系统里的一个天体。

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CosmosOrbitComponent.generated.h"

/**
 * 天体组件：挂在任意 Actor 上，它就参与 N 体引力。做成组件而不是 Actor 基类，
 * 体素行星挂上即是天体、不必继承。组件**不推进物理**、不 Tick，只存参数：
 * 注册/推进/写回全在 UCosmosSimulationSubsystem（N 体必须有唯一编排者），渲染参数在
 * UCosmosTrajectoryComponent。初态：位置 = 关卡摆放，速度 = InitialVelocityKmPerSecond。
 */
UCLASS(ClassGroup = (Cosmos), meta = (BlueprintSpawnableComponent))
class STELLARERACOSMOS_API UCosmosOrbitComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCosmosOrbitComponent();

	/**
	 * 质量，单位**地球质量 M⊕**（1 M☉ = 332946.0837 M⊕），默认 1 = 地球。
	 * 质量决定「谁绕谁」：中心天体必须是系统里最大的那个，否则行星会飞出去。
	 * 必须 > 0；<= 0（含 NaN）注册时跳过并报错。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Body",
		meta = (DisplayName = "质量（地球质量 M⊕）"))
	double MassEarth = 1.0;

	/**
	 * 是否作为**引力源**吸引其他天体。关掉后仍被别人吸引、照常接触，但作为引力源的质量
	 * 按 0 计（测试粒子），典型用途是玩家飞船。**接触解算不受影响**，仍按真实 MassEarth
	 * 分配让路。与 MassEarth、初速一样**注册时读一次**，PIE 中途改要重开才生效。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Body")
	bool bExertsGravity = true;

	/**
	 * 初速度，单位 **km/s（物理速度）**，初速度的唯一来源。留零即静止。
	 * 圆轨道：v[AU/年] = 2π√(M[M☉]/r[AU])，再乘 4.7404704635 换成 km/s。
	 * **物理速度 ≠ HUD 世界速度**：后者 = 1 AU/年 × CentimetersPerAu × TimeScale（默认
	 * 3330 cm/s），两个量都正确。速度箭头画的是求解器的**当前**速度，不是这一项。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Body",
		meta = (DisplayName = "初速度（km/s，物理速度）"))
	FVector InitialVelocityKmPerSecond = FVector::ZeroVector;

	// ── 旧字段：只保留序列化与 PostLoad 迁移，代码不再消费，迁移后清零 ──

	UPROPERTY(BlueprintReadWrite, Category = "Cosmos|Legacy")
	double MassSolar = 0.0;

	UPROPERTY(BlueprintReadWrite, Category = "Cosmos|Legacy")
	FVector InitialVelocity = FVector::ZeroVector;

	/**
	 * 是否每帧把求解器算出的位置写回所属 Actor。假 = Actor 待在原地、求解器里照样漂，
	 * 只适合纯装饰的远处天体。它**不决定初始状态**：初态始终是「摆放位置 + 初速度」。
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
