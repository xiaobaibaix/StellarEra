// Copyright Voxel Plugin SAS. All Rights Reserved.
// 街机式太空飞船。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "SpaceshipPawn.generated.h"

class APlanetCharacter;
class AVoxelPlanetActor;
class UCameraComponent;
class UFloatingPawnMovement;
class USphereComponent;
class USpringArmComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UVoxelInvokerWithPredictionComponent;

/**
 * 街机式太空飞船。
 *
 * 位移交给 UFloatingPawnMovement（自带扫掠、加速度、阻尼），手感写在 Tick 里：
 * 鼠标给的是角速度冲量而不是直接设朝向，松手后指数衰减——这是“开飞机”的
 * 惯性感来源，也是《戴森球计划》伊卡洛斯的做法。
 */
UCLASS()
class ASpaceshipPawn : public APawn
{
	GENERATED_BODY()

public:
	ASpaceshipPawn();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** 主推力输入（-1 = 反推，+1 = 前进）。 */
	void SetThrust(float Value);

	/** 横滚输入（-1 / +1）。 */
	void SetRoll(float Value);

	/** 升降输入（-1 = 降，+1 = 升）。 */
	void SetVertical(float Value);

	/** 鼠标输入（度）。以冲量形式累加进角速度，松手后衰减。 */
	void Look(float DeltaYaw, float DeltaPitch);

	/** 速度低于阈值且机身下方命中地面时为真。 */
	UFUNCTION(BlueprintPure, Category = "Spaceship")
	bool IsLanded() const;

	/** 角色是否近到可以登船。 */
	UFUNCTION(BlueprintPure, Category = "Spaceship")
	bool CanBoardFrom(const FVector& CharacterLocation) const;

	/** 记录登船的角色；传 nullptr 表示离船。 */
	void SetBoardedCharacter(APlanetCharacter* Character);

	/** 当前在船上的角色；没有则为 nullptr。 */
	APlanetCharacter* GetBoardedCharacter() const { return BoardedCharacter; }

protected:
	/** 碰撞球：既是根组件也是 UFloatingPawnMovement 扫掠用的形状。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Spaceship")
	TObjectPtr<USphereComponent> Collision;

	/**
	 * 体素调用器：行星的 LOD 与碰撞都靠它驱动，没有它飞船就是瞎的。
	 *
	 * 插件的调用器用一个位置服务三件事（见 VoxelInvokerComponent.h）：
	 *   - LOD：ShouldSubdivideByDistance 把与 LODBounds 相交的节点一路细分到
	 *     LODToSet（VoxelRenderOctree.cpp:626）。
	 *   - 碰撞：ShouldSubdivideByOthers 把 CollisionsBounds 内的节点细分到
	 *     Height 0，只有 Height 0 的区块才带碰撞体（同文件 :479）。
	 *   - 导航：本项目不用。
	 *
	 * 为什么挂在飞船根节点而不是相机上：IsLocalInvoker
	 * （VoxelInvokerComponent.cpp:21）只认 Pawn 的 IsLocallyControlled()，
	 * UVoxelInvokerAutoCameraComponent 那条路是 bUseCameraIfNoInvokersFound 的
	 * 兜底，且写死玩家索引 0（同文件 :177），多人下不可用。
	 *
	 * 为什么登船后必须换人扛：Possess(Ship) 会把 Controller 从角色转到飞船，
	 * 角色的调用器随即不再是 local invoker，VoxelDefaultLODManager.cpp:218 会把它
	 * 的 bUseForLOD 强制置假。没有这一份，整个行星就一直停在行星调用器给的最粗
	 * LOD 上 —— 这正是「开飞船时星球不更新 LOD、也不碰撞」的成因。
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Spaceship")
	TObjectPtr<UVoxelInvokerWithPredictionComponent> VoxelInvoker;

	/**
	 * 外壳网格。**网格与变换都归蓝图管**：
	 *
	 *   - 换模型：用 HullMesh 属性（推荐），或直接在子蓝图里覆盖本组件的静态网格体；
	 *   - 摆位置/朝向/缩放：在子蓝图（BP_SpaceshipPawn）的视口里选中 Hull 拖 Gizmo，
	 *     值存在蓝图资产里，重编译不会被清掉；关卡实例上也能单独调。
	 *
	 * C++ 只在构造期塞一个引擎立方体兜底，让裸放的 ASpaceshipPawn 不至于隐形，
	 * 之后不再写这里的任何变换 —— 一旦在这里写，蓝图里拖出来的值每次重编译都会
	 * 被冲掉，"模型在蓝图里调不动"就是这么来的。
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Spaceship")
	TObjectPtr<UStaticMeshComponent> Hull;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Spaceship")
	TObjectPtr<UFloatingPawnMovement> FloatingMovement;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<USpringArmComponent> SpringArm;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<UCameraComponent> Camera;

	/** 偏航角速度上限（度/秒）。 */
	UPROPERTY(EditAnywhere, Category = "Spaceship|Handling")
	float MaxYawRate = 90.f;

	/** 俯仰角速度上限（度/秒）。 */
	UPROPERTY(EditAnywhere, Category = "Spaceship|Handling")
	float MaxPitchRate = 70.f;

	/** 横滚角速度（度/秒），由 A/D 直接驱动，不带惯性。 */
	UPROPERTY(EditAnywhere, Category = "Spaceship|Handling")
	float MaxRollRate = 120.f;

	/** 鼠标位移到角速度的换算系数（度/秒 每度）。越大越灵敏。 */
	UPROPERTY(EditAnywhere, Category = "Spaceship|Handling")
	float MouseImpulseScale = 6.f;

	/** 角速度衰减速率（FInterpTo 速率）。越小惯性越久。 */
	UPROPERTY(EditAnywhere, Category = "Spaceship|Handling")
	float AngularDamping = 3.5f;

	/** 自动回正速率（度/秒）。只回正横滚，不动玩家给的俯仰。0 = 关闭。 */
	UPROPERTY(EditAnywhere, Category = "Spaceship|Handling")
	float AutoLevelSpeed = 45.f;

	/**
	 * 自动回正的重力参考行星。BeginPlay 时自动查找最近的一颗；找不到（纯太空关卡）
	 * 时自动回正退回世界 Z。软依赖：缺了它飞船照常飞。
	 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Spaceship")
	TObjectPtr<AVoxelPlanetActor> PlanetActor;

	/** 查找行星时的最大距离（厘米）。默认 1000 公里，够覆盖任意单颗行星。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spaceship")
	float PlanetSearchRadius = 1.0e8f;

	/** 判定“已落地”的速度阈值（厘米/秒）。 */
	UPROPERTY(EditAnywhere, Category = "Spaceship|Landing")
	float LandingSpeedThreshold = 200.f;

	/** 落地射线的长度（厘米）。 */
	UPROPERTY(EditAnywhere, Category = "Spaceship|Landing")
	float LandingProbeDistance = 600.f;

	/** 登船触发半径（厘米）。 */
	UPROPERTY(EditAnywhere, Category = "Spaceship|Boarding")
	float BoardingRadius = 600.f;

	/**
	 * 船体模型。留空 = C++ 不介入网格体：蓝图里覆盖的模型、或构造期塞进去的
	 * 引擎立方体都保持原样。
	 *
	 * 这是「具体表现」，默认留给子蓝图 BP_SpaceshipPawn 在 Class Defaults 里指定，
	 * C++ 不硬编码任何项目资产路径 —— 换模型不必重新编译。推荐走这个属性而不是
	 * 直接覆盖 Hull 组件的静态网格体：组件覆盖记在 UInheritableComponentHandler 里，
	 * 原生类一重编译有被重新生成掉、模型无声退回占位立方体的风险。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spaceship|Mesh")
	TObjectPtr<UStaticMesh> HullMesh;

private:
	/**
	 * 把 HullMesh 套用到 Hull 的**网格体**上，不碰任何变换。
	 *
	 * 位置/朝向/缩放故意留给蓝图：在蓝图视口里选中 Hull 拖 Gizmo 即可，值存在
	 * 蓝图资产里。如果这里写 RelativeLocation / Rotation / Scale，每次重编译
	 * （OnConstruction 会重跑）都会把手拖出来的值冲回去 —— 表现就是"模型在蓝图
	 * 里怎么都调不动"。
	 *
	 * 必须在 OnConstruction 与 BeginPlay 里调用，不能只在构造函数里做：
	 * 蓝图 CDO 构造时会先跑一遍 C++ 构造函数，之后才加载蓝图里存的值，
	 * 构造期读到的永远是 C++ 默认值 —— 在蓝图 Class Defaults 里改 HullMesh
	 * 就会静默失效。APlanetCharacter 的 WalkSpeed 出于同样的原因在 BeginPlay
	 * 里再套用一次（见 PlanetCharacter.cpp）。
	 */
	void ApplyHullMesh();

	float ThrustInput = 0.f;
	float RollInput = 0.f;
	float VerticalInput = 0.f;
	float YawRate = 0.f;
	float PitchRate = 0.f;

	/** 在关卡里找离飞船最近的一颗行星；找不到返回 nullptr。 */
	AVoxelPlanetActor* FindNearestPlanet() const;

	/**
	 * 自动回正的目标“上”：有行星时取该行星的径向外侧（飞船停在球面任何位置都
	 * 贴合当地水平面），太空中退回世界 Z。
	 */
	FVector GetAutoLevelUp() const;

	/** 当前在船上的角色。 */
	UPROPERTY(Transient)
	TObjectPtr<APlanetCharacter> BoardedCharacter;
};
