// Copyright Voxel Plugin SAS. All Rights Reserved.
// 街机式太空飞船。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "SpaceshipPawn.generated.h"

class APlanetCharacter;
class AVoxelPlanetActor;
class UCameraComponent;
class USphereComponent;
class USpringArmComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UVoxelInvokerWithPredictionComponent;

/**
 * 飞船的对外状态快照，供 HUD 之类的显示层读取。
 *
 * 用「一次取全」的快照而不是散落一堆 getter：显示层一次调用就能拿到全部字段，
 * 飞船内部怎么改都不用跟着改接口。
 *
 * 故意不做成 USTRUCT：只有 C++ 显示层用，不需要反射，省掉一次 UHT 处理。
 */
struct FSpaceshipStatus
{
	/** 速度大小（厘米/秒）。取自求解器写回的速度（见 ASpaceshipPawn::GetVelocity）。 */
	float Speed = 0.f;

	/**
	 * 阻尼极速（厘米/秒）= 推力加速度 × 阻尼时间常数。
	 *
	 * 0 表示**没有上限**（巡航开着，阻尼关掉了），不是「不能动」。
	 */
	float MaxSpeed = 0.f;

	/** 主推力输入，-1 ~ +1。 */
	float ThrustInput = 0.f;

	/** Shift 是否按下。 */
	bool bBoosting = false;

	/** 巡航（速度保持）是否开启。 */
	bool bPreserveVelocity = false;

	/** 是否判定为已落地。 */
	bool bLanded = false;
};

/**
 * 街机式太空飞船。
 *
 * **位移完全归 N 体求解器**：本类不自己搬自己，只把「这一帧想施加多少推力加速度」
 * 暴露出去（GetThrustAccelerationWorld / GetDampingTime），由
 * UCosmosSimulationSubsystem 每帧下来读走、换算成 AU/年² 写进它那个 FCosmosBodyState。
 * 位置与速度都由求解器写回（SetActorLocation + SetCosmosVelocity）。
 *
 * 力只能是这个方向：StellarEraVehicle 不能依赖 StellarEra（会成环），所以飞船
 * 既不能 include CosmosOrbitComponent.h，也不能调那个子系统。
 *
 * 鼠标给的是角速度冲量而不是直接设朝向，松手后指数衰减 —— 这是"开飞机"的惯性感
 * 来源，也是《戴森球计划》伊卡洛斯的做法。姿态是**运动学**的：它只决定推力朝哪，
 * 不参与积分。
 */
UCLASS()
class STELLARERAVEHICLE_API ASpaceshipPawn : public APawn
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

	/** 加速键（Shift）。按下时推力加速度乘 BoostMultiplier。 */
	void SetBoost(bool bValue);

	/**
	 * 滚轮缩放相机：拉近 / 拉远。
	 *
	 * Value 每格滚轮为 ±1，正（滚轮上滚）= 拉近 = 摇臂变短。调用方直接把
	 * MouseWheelAxis 的值传进来即可，不需要取反。
	 *
	 * 累加的是**偏移**而不是直接改臂长：摇臂长度每帧由 Tick 第 5 段重算
	 * （基准臂长 + 缩放 + 后缩），直接写 TargetArmLength 会在下一帧被冲掉。
	 */
	void AddZoom(float Value);

	/**
	 * 巡航开关（Tab）。翻转 bPreserveVelocity。
	 *
	 * 开：关掉阻尼，速度不再自动衰减 —— 按 W 加到稳、按一下 Tab 就能松开 W 继续飞。
	 * 关：有阻尼，松手后速度按 τ 衰减停下。
	 *
	 * **只关阻尼，推力照给**：开着巡航按住 W 才能一路加速到圆轨道速度
	 * （阻尼极速 1.2 km/s 只有圆轨道速度 7.678 km/s 的 0.156 倍）。
	 *
	 * 只翻转、不接收目标值：状态归飞船自己管，输入侧不需要知道当前是开是关。
	 */
	void TogglePreserveVelocity();

	/** 巡航（速度保持）是否开启。 */
	bool IsPreserveVelocity() const { return bPreserveVelocity; }

	/** 取一份状态快照，供 HUD 之类的显示层读取。 */
	FSpaceshipStatus GetStatus() const;

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

	/**
	 * 接触半径（厘米）—— N 体求解器把飞船当球体解接触时用的那个半径。
	 *
	 * 取碰撞球的缩放后半径（默认 300 厘米）。**不能返回 0**：半径 0 的点质量
	 * 在求解器里会让飞船的**中心**被钳在地表上，机身下半截埋进地形里。
	 *
	 * 读的是 Collision 而不是 Hull：Collision 本来就是「飞船占多大地方」的那个
	 * 形状（它同时是根组件），Hull 是纯外观（CollisionEnabled 为 NoCollision）。
	 */
	float GetContactRadiusCm() const;

	/**
	 * 本帧要施加在自己那个 N 体上的推力加速度（厘米/秒²，世界坐标）。零 = 无推力。
	 *
	 * 由 UCosmosSimulationSubsystem 每帧下来读走（依赖方向见类注释）。
	 *
	 * 方向 = 机头 × ThrustInput + 机身上 × VerticalInput，合成向量夹到长度 1 ——
	 * 与 UFloatingPawnMovement 原来的 GetClampedToMaxSize(1) 同规则：斜着同时按
	 * W 和 Space 不该比单按一个多出 41% 的推力。再乘 ThrustAcceleration，
	 * Shift 时乘 BoostMultiplier。
	 */
	FVector GetThrustAccelerationWorld() const;

	/**
	 * 阻尼时间常数（秒）。0 = 不阻尼。
	 *
	 * 巡航（bPreserveVelocity）开着就是 0 —— 「巡航 = 只关阻尼，推力照给」：
	 * 关掉阻尼才有办法一路加速到圆轨道速度（阻尼极速只有它的 0.156 倍，见 spec §4）。
	 * 有阻尼时速度收敛到「推力 × 本值」，这就是极速的来源。
	 */
	float GetDampingTime() const;

	/**
	 * 由 UCosmosSimulationSubsystem 在写回位置时一并写入的**求解器速度**
	 * （厘米/秒，世界坐标）。本类不自己算速度 —— 位移归求解器，速度也就该由它给。
	 */
	void SetCosmosVelocity(const FVector& InVelocity) { CosmosVelocity = InVelocity; }

	/**
	 * 覆写：让引擎侧所有问「这艘船多快」的地方拿到求解器给的真值。
	 *
	 * 单点收口，替掉三个散落的改点：IsLanded() 的第一道判断、GetStatus().Speed、
	 * 以及体素调用器的预测（UVoxelInvokerWithPredictionComponent::
	 * GetInvokerGlobalPosition 读的是 GetOwner()->GetVelocity()）。删掉移动组件后
	 * 根组件的速度不再有人写，不覆写的话预测会静默失效 —— 高速飞行时地形就追不上了。
	 */
	virtual FVector GetVelocity() const override { return CosmosVelocity; }

protected:
	/** 碰撞球：根组件，也是飞船「占多大地方」的那个形状（接触半径取自它）。 */
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

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<USpringArmComponent> SpringArm;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<UCameraComponent> Camera;

	/**
	 * 按住 W（不按 Shift）时相机沿机身后方后缩的距离（厘米）。
	 *
	 * 由推力输入驱动（见 Tick 第 5 段）：按住 W 就缩到这个距离，松开平滑收回，与
	 * 当前速度无关。按 S 反推不后缩。0 = 关闭。
	 *
	 * 名字里的 AtFullThrust 是历史遗留（当时由加速度驱动），改 UPROPERTY 名有让关卡
	 * 实例里已设的值丢失的风险，故保留。
	 */
	UPROPERTY(EditAnywhere, Category = "Camera")
	float CameraPullbackAtFullThrust = 350.f;

	/**
	 * 按住 Shift 时后缩距离的**倍数**（在 CameraPullbackAtFullThrust 基础上乘）。
	 * 用倍数而不是绝对增量，"Shift 缩得更远" 这个关系就不会因为改基准值而失效。
	 */
	UPROPERTY(EditAnywhere, Category = "Camera")
	float BoostPullbackMultiplier = 2.f;

	/** 相机后缩/回位的平滑速率（FInterpTo 速率）。越小越拖沓。 */
	UPROPERTY(EditAnywhere, Category = "Camera")
	float CameraPullbackInterpSpeed = 4.f;

	/** 滚轮每格的缩放距离（厘米）。越大每格拉得越远。 */
	UPROPERTY(EditAnywhere, Category = "Camera")
	float ZoomStep = 300.f;

	/**
	 * 相机最近距离（厘米）。滚轮拉到头的下限。
	 *
	 * 是**绝对臂长**而不是相对基准的偏移：这样改基准臂长（蓝图或关卡实例覆盖
	 * TargetArmLength）时，"最近能拉到多近"不会跟着漂。
	 */
	UPROPERTY(EditAnywhere, Category = "Camera")
	float MinArmLength = 500.f;

	/** 相机最远距离（厘米）。滚轮推到底的上限。 */
	UPROPERTY(EditAnywhere, Category = "Camera")
	float MaxArmLength = 6000.f;

	/** 缩放平滑速率（FInterpTo 速率）。越小越拖沓。 */
	UPROPERTY(EditAnywhere, Category = "Camera")
	float ZoomInterpSpeed = 8.f;

	/**
	 * 相机横滚的跟随速率（FInterpTo 速率）。越小，按 A/D 时相机越"迟钝"。
	 *
	 * 持续压杆时相机落后机身的稳态角度 = MaxRollRate / 本值（默认 120 / 8 = 15°）。
	 * 点一下 A/D 则几乎不跟（机身冲得快，lag 还没衰减完）。松手后按 1/本值 秒的
	 * 时间常数补齐，最终与机身**完全一致** —— 不像死区那样会留下永久偏差。
	 *
	 * 设 0 = 关闭滞后，相机与机身刚性绑定（改动前的行为）。
	 */
	UPROPERTY(EditAnywhere, Category = "Camera")
	float CameraRollFollowSpeed = 8.f;

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

	/**
	 * 主推力加速度（厘米/秒²）。**本轮唯一被调大的量。**
	 *
	 * 下限被当地重力锁死：本关卡中心天体 100 M☉、飞船在 r = 0.07425 AU，
	 * 表面重力 = G·M/r² = 716,089 AU/年² = 7.94e6 cm/s²。推力必须大于它，否则
	 * 飞船一步都升不起来 —— 这跟怎么接线无关，是物理。改动前飞船的最大推力是
	 * 4000 + 12000 = 16000 cm/s²，只有重力的 0.20 %。
	 *
	 * 1.2e7 = 1.51 × 表面重力，留 51% 净余量。
	 *
	 * 极速 = 本值 × DampingTime = 1.2e7 × 0.01 = 1.2e5 cm/s = 1.2 km/s，只有该处
	 * 圆轨道速度（7.678e5 cm/s）的 0.156 倍 —— 光按 W 飞不出行星，只能悬停、
	 * 爬升、绕圈。要进轨道得开巡航（关阻尼）持续加速。
	 */
	UPROPERTY(EditAnywhere, Category = "Spaceship|Thrust")
	float ThrustAcceleration = 1.2e7f;

	/** 按 Shift 时乘在 ThrustAcceleration 上的倍数。松开即回到 1。 */
	UPROPERTY(EditAnywhere, Category = "Spaceship|Thrust")
	float BoostMultiplier = 3.0f;

	/**
	 * 阻尼时间常数 τ（秒）：速度按 a_damp = −v/τ 衰减，收敛到「推力 × τ」。
	 *
	 * 单位是秒，而求解器的时间单位是模拟年 —— 换算在
	 * UCosmosSimulationSubsystem::ApplyControlledBodyForces 里做（除以 TimeScale）。
	 *
	 * 0.01 秒是**在「有惯性感」和「世界跟得上」之间取的折中**：τ 再大极速就上千
	 * km/s，体素调用器的范围要跟着涨到几十公里。代价是响应很硬 —— 约 3τ = 30 毫秒
	 * 到极速（60 fps 下两帧）。旧的「2 秒加到 80 m/s」是旧尺度（飞船不参与引力）
	 * 的产物，回不去了。
	 */
	UPROPERTY(EditAnywhere, Category = "Spaceship|Thrust")
	float DampingTime = 0.01f;

	/**
	 * 巡航开关。开 = 关掉阻尼（速度保持）；关 = 有阻尼，松手就按 τ 衰减停下。
	 *
	 * **只关阻尼，推力照给** —— 开着巡航按住 W 才能一路加速到圆轨道速度。
	 *
	 * 关掉阻尼之后飞船走纯引力测地线：松手不减速，但**行星引力照样作用**，
	 * 所以「停」不下来，只能悬停或绕圈。
	 *
	 * 运行时按 Tab 切换（见 TogglePreserveVelocity），细节面板里改的是**初始状态**，
	 * 每帧生效，PIE 里也能直接勾。
	 *
	 * 刹车按 S：反向推力会先把速度减到零再反向，不需要额外的刹车键。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spaceship|Thrust")
	bool bPreserveVelocity = false;

	/**
	 * 自动回正速率（度/秒）。只回正横滚，不动玩家给的俯仰。0 = 关闭。
	 *
	 * 默认 0（关闭）：横滚完全归玩家，"转了就转了"。曾经是 45 —— 那个值会和按住的
	 * A/D 抢方向盘（MaxRollRate 120 被顶掉 45，净滚转只剩 75°/s），松手后还会自动
	 * 把机身拉回水平，玩家压出来的角度留不住。
	 *
	 * 关掉之后 PlanetActor / PlanetReevaluateInterval 那套参考行星的机制会空转
	 * （GetAutoLevelUp() 只被这里用）。留着不动：代价是每 PlanetReevaluateInterval
	 * 秒一次遍历几颗 Actor，可忽略；把本属性调回正数即可整体复活。
	 *
	 * 代价：贴着行星飞大圆时曲面往下走，机身不会自己跟，需要玩家压杆维持高度。
	 */
	UPROPERTY(EditAnywhere, Category = "Spaceship|Handling")
	float AutoLevelSpeed = 0.f;

	/**
	 * 自动回正的重力参考行星。BeginPlay 时自动查找最近的一颗；找不到（纯太空关卡）
	 * 时自动回正退回世界 Z。软依赖：缺了它飞船照常飞。
	 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Spaceship")
	TObjectPtr<AVoxelPlanetActor> PlanetActor;

	/** 查找行星时的最大距离（厘米）。默认 1000 公里，够覆盖任意单颗行星。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spaceship")
	float PlanetSearchRadius = 1.0e8f;

	/**
	 * 重新评估「最近的行星」的间隔（秒）。默认 0.5。设 0 关闭。
	 *
	 * 行星在动，最近的一颗会换人。不重评的话，飞离母星之后自动回正会一直
	 * 对齐到已经远去的旧星，横滚再也拉不回来。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spaceship")
	float PlanetReevaluateInterval = 0.5f;

	/** 判定“已落地”的速度阈值（厘米/秒）。 */
	UPROPERTY(EditAnywhere, Category = "Spaceship|Landing")
	float LandingSpeedThreshold = 200.f;

	/**
	 * 落地检测球的半径（厘米）。球心在机身中心，与机身朝向无关。
	 *
	 * 曾经是一条沿机身"下"方的射线。自动回正关掉后机身可以停在任何横滚角，射线
	 * 会指偏、永远打不到地面，人就被卡在船里下不来（IsLanded 恒为假，按 E 无效）。
	 * 换成全向球后，滚成什么样都能判定落地。
	 *
	 * 取舍：球是全向的，贴着悬崖侧面慢速悬停也会判为已落地。本判定唯一的用途是
	 * 「能不能按 E 下船」，从几百厘米高处落到地面可以接受。
	 *
	 * 名字里的 Distance 是历史遗留（当时是射线长度），改 UPROPERTY 名有让关卡
	 * 实例里已设的值丢失的风险，故保留。
	 */
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

	/** 加速键（Shift）当前是否按下。 */
	bool bBoosting = false;

	/**
	 * 求解器每帧写回的当前速度（厘米/秒，世界坐标）。
	 *
	 * 由 UCosmosSimulationSubsystem 在写回位置时一并写入，本类只读不改。
	 * 初值零 —— 第一帧写回之前 GetVelocity() 返回零向量，IsLanded() 因此按
	 * 「速度为零」判断，与旧行为一致。
	 */
	FVector CosmosVelocity = FVector::ZeroVector;

	/** 距离下次重新评估最近行星还剩多少秒。 */
	float PlanetReevaluateTimer = 0.f;

	/**
	 * SpringArm 的基准臂长，BeginPlay 时从组件上抄下来。
	 *
	 * 不在 Tick 里写死 1800：蓝图或关卡实例可能改过 TargetArmLength，抄一次再
	 * 叠加后缩量，才不会被本类覆盖掉。
	 */
	float BaseArmLength = 0.f;

	/** 当前相机后缩量（厘米），平滑后的值。 */
	float CameraPullback = 0.f;

	/** 滚轮缩放的**目标**偏移（厘米），正 = 相机更远。滚轮改的是它。 */
	float TargetZoomOffset = 0.f;

	/** 平滑后的缩放偏移（厘米）。与 CameraPullback 一起加在基准臂长之上。 */
	float ZoomOffset = 0.f;

	/** 相机横滚当前落后机身的角度（度）。正 = 相机比机身少滚了这么多。 */
	float CameraRollLag = 0.f;

	/**
	 * 上一帧的机身朝向，用来差分出"本帧滚了多少"。
	 *
	 * 必须在 BeginPlay 里初始化：若留到第一次 Tick 才赋值，差分就从单位四元数
	 * 起算，开局摆位那一次旋转会被当成一次巨大的横滚。
	 */
	FQuat PreviousRotation = FQuat::Identity;

	/**
	 * 相机的基准相对朝向（蓝图可能给相机设过）。横滚偏移叠加在它上面，
	 * 而不是直接覆盖 —— 否则会把蓝图里调好的相机朝向冲掉。
	 * BeginPlay 时从组件上抄下来。
	 */
	FQuat CameraBaseRotation = FQuat::Identity;

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
