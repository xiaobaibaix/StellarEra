// Copyright Voxel Plugin SAS. All Rights Reserved.
// 街机式太空飞船。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "VoxelViewpointInvokers.h"
#include "VoxelInvokerPredictionReference.h"
#include "CosmosBody.h"
#include "SpaceshipPawn.generated.h"

class APlanetCharacter;
class AVoxelPlanetActor;
class UCameraComponent;
class ULandingDebugSphereComponent;
class USpringArmComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UVoxelInvokerWithPredictionComponent;
class USpaceshipInputComponent;
class UInputComponent;
struct FInputActionValue;

/**
 * 飞船的对外状态快照，供 HUD 等显示层一次取全，飞船内部改动不必跟着改接口。
 * 故意不做成 USTRUCT：只有 C++ 显示层用，不需要反射。
 */
struct FSpaceshipStatus
{
	/**
	 * 速度大小（**世界 km/s**，惯性系）。世界速度 ≠ 编辑器里填的物理速度：
	 * 物理 1 AU/年 = 4.74047 km/s，世界 1 AU/年 ≈ 0.0333 km/s。
	 * 贴着一颗公转中的行星静止时它不为 0，要看相对地表多快请读 RelativeSpeed。
	 */
	float SpeedKmPerSecond = 0.f;

	/**
	 * 相对参考行星（见 PlanetActor）的速度大小（**世界 km/s**）。大气随行星平移、
	 * 行星不自转，所以阻力与「是否停在地表」都看这个值；无参考行星时等于绝对速度。
	 */
	float RelativeSpeedKmPerSecond = 0.f;

	/** 本帧是否有参考行星。没有时 HUD 的「相对速度」显示占位符。 */
	bool bHasReferencePlanet = false;

	/**
	 * 实际生效的速度上限（**世界 km/s**）：阻尼极速与当前硬上限（按 Shift 与否）里
	 * 较小且非 0 的那个。比较对象是相对参考行星的速度；0 表示没有上限，不是不能动。
	 */
	float MaxSpeedKmPerSecond = 0.f;

	/** 主推力输入，-1 ~ +1。 */
	float ThrustInput = 0.f;

	/** Shift 是否按下。 */
	bool bBoosting = false;

	/** 巡航（速度保持）是否开启。 */
	bool bPreserveVelocity = false;

	/** 是否判定为已落地。 */
	bool bLanded = false;

	/** 当前处的大气密度比（海平面 = 1，壳外 / 无参考行星 = 0）。 */
	float AtmosphereDensityRatio = 0.f;

	/** 当前大气阻力减速度的大小（**世界 km/s²**）。0 = 无阻力（真空或关掉了）。 */
	float AtmosphereDragKmPerS2 = 0.f;

	/**
	 * 当前模式下能给出的最大推力（**世界 km/s²**，已含 Shift 倍率）。是标称值而非
	 * 本帧实际输出；显示层拿它和当地重力比，重力更大时飞船抬不起来。
	 */
	float MaxThrustAccelerationKmPerS2 = 0.f;
};

/**
 * 街机式太空飞船。**位移与姿态都归 N 体求解器**：本类只暴露推力加速度、角加速度与
 * 阻尼，由 UCosmosSimulationSubsystem 每帧读走，位置、速度、朝向也由它写回。
 * 本类实现 ICosmosBody，依赖方向是 Pawns → Cosmos，不反向调用子系统。
 *
 * 鼠标给的是角速度增量而非直接设朝向，松手由求解器的角阻尼衰减 —— 这是「开飞机」的
 * 惯性感来源。**不要在本类 AddActorLocalRotation**：两处都写姿态就是互相抹，表现为
 * 机身抖或转不动。
 */
UCLASS()
class STELLARERAPAWNS_API ASpaceshipPawn
	: public APawn
	, public IVoxelViewpointInvokers
	, public ICosmosBody
	, public IVoxelInvokerPredictionReference
{
	GENERATED_BODY()

public:
	ASpaceshipPawn();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/**
	 * 视线朝向在这里用**当帧**摇臂世界朝向重算：相机组件的相对旋转是本类 Tick 写的，那时机身
	 * 还是上一帧的；相机更新排在求解器写回之后，用组件那份会慢一帧。
	 */
	virtual void CalcCamera(float DeltaSeconds, struct FMinimalViewInfo& OutResult) override;

	/**
	 * 相机视线朝向：摇臂世界朝向 × 绕其 X 轴的滞后角 × 蓝图朝向。即**整个机位**随机身刚性转，
	 * 只让横滚慢半拍（滞后 0 = 完全刚性）。滞后是相对量，不用绝对欧拉 Roll —— 俯仰过 ±90°
	 * 时 `GetActorRotation().Roll` 会跳 180°，拿它当目标画面会整圈翻转。
	 */
	static FQuat BuildCameraViewRotation(const FQuat& ArmWorldRotation, const FQuat& InBaseRotation, float RollLagDegrees);

	/**
	 * 两次机身朝向之间绕机身 X 轴的转角，度，FRotator 的 Roll 语义。用四元数 twist 而不是
	 * 欧拉角：欧拉分解在俯仰 ±90° 处不连续。纯函数。
	 */
	static float ExtractRollDeltaDegrees(const FQuat& PreviousHullRotation, const FQuat& CurrentHullRotation);

	/**
	 * 推进一帧的横滚滞后：`Lag ← (Lag − RollDelta) · exp(−FollowSpeed·dt)`，返回新滞后角
	 * （度，负 = 视线落后于机身）。FollowSpeed ≤ 0 为刚性；单帧增量夹在 ±45° 防甩飞。纯函数。
	 */
	static float StepCameraRollLagDegrees(float LagDegrees, float RollDeltaDegrees,
		float FollowSpeed, float DeltaSeconds);

	/**
	 * 绑飞船自己的输入。引擎只在**本地 Possess 这个 Pawn** 时调用，动作表从控制器
	 * （`IStellarEraInputSource`）取。
	 */
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	/** 键鼠输入。回调与输入状态都在组件里，本类只负责转发绑定。 */
	UPROPERTY(VisibleAnywhere, Category = "Input")
	TObjectPtr<USpaceshipInputComponent> PlayerInput;

	//~ Begin IVoxelViewpointInvokers Interface
	virtual void SetViewpointInvokersActive(bool bActive) override;
	//~ End IVoxelViewpointInvokers Interface

	/** 主推力输入（-1 = 制动，+1 = 前进）。 */
	void SetThrust(float Value);

	/** 横滚输入（-1 / +1）。 */
	void SetRoll(float Value);

	/** 升降输入（-1 = 降，+1 = 升）。 */
	void SetVertical(float Value);

	/**
	 * 鼠标输入（度）。累加成待发角速度增量（机体系，度/秒），由
	 * GetAngularAccelerationWorldRadPerS2() 每帧取走，衰减归求解器的角阻尼。
	 */
	void Look(float DeltaYaw, float DeltaPitch);

	/**
	 * 本帧要施加的世界系角加速度（rad/s²）。**有副作用**：读走并清零待发冲量，
	 * 一帧只能调一次（UCosmosSimulationSubsystem::ApplyControlledBodyForces）。
	 * 自由视角（落地且没给推力）时返回零，同时丢弃待发冲量。
	 */
	FVector GetAngularAccelerationWorldRadPerS2(float DeltaSeconds) override;

	/**
	 * 求解器当前的实际角速度（世界系 rad/s），由子系统在**施加本帧角加速度之前**写入。
	 * 横滚到顶的衰减要读它：它描述这一帧开始时船已经在怎么转。
	 */
	void SetCosmosAngularVelocity(const FVector& InWorldRadPerSecond) override
	{
		CosmosAngularVelocity = InWorldRadPerSecond;
	}

	/** 角阻尼（1/秒），子系统换算成 1/模拟年后写进求解器。 */
	float GetAngularDampingPerSecond() const override { return AngularDamping; }

	/**
	 * 纯函数：转速增量 + 横滚输入 → 世界系角加速度（rad/s²）。
	 * 拆出来是为能无头测试 —— 测试不建 Actor，只喂四元数与系数。
	 */
	static FVector ComputeAngularAccelerationWorldRadPerS2(
		const FQuat& BodyRotation,
		const FVector& PendingImpulseLocalDegPerSec,
		const FVector& ActualAngularVelocityWorld,
		float InRollInput,
		float InRollAngularAccelerationDegPerS2,
		float InMaxRollRateDegPerSec,
		float DeltaSeconds);

	/**
	 * 落地自由视角的激活条件：已落地、且没有按 W（正向推力）或 Space（上升）。
	 * 负值只是制动 / 下降，**不**退出自由视角。纯函数，测试直接调它。
	 */
	static bool ShouldUseFreeLook(bool bLanded, float InThrustInput, float InVerticalInput);

	/**
	 * 自由视角的鼠标累加：偏航归一化到 (-180, 180]，俯仰夹在 [MinPitch, MaxPitch]。
	 * 与 PlanetCharacter 的环绕相机同一套口径。纯函数。
	 */
	static void ApplyFreeLookLookInput(
		float DeltaYaw, float DeltaPitch, float MinPitch, float MaxPitch,
		float& InOutYaw, float& InOutPitch);

	/**
	 * 升降推力的"上"方向：有参考行星时取**行星球心 → 飞船中心**的径向，否则退回
	 * 机身本地 up（船心与星心重合时同样退回）。纯函数，测试直接调它。
	 */
	static FVector ResolveVerticalThrustUp(
		const FVector& ShipLocation,
		const FVector& PlanetLocation,
		bool bHasPlanet,
		const FVector& BodyUp);

	/** 加速键（Shift）。按下时推力加速度乘 BoostMultiplier。 */
	void SetBoost(bool bValue);

	/**
	 * 滚轮缩放相机：正（滚轮上滚）= 拉近，调用方直接传 MouseWheelAxis 即可。
	 * 累加的是**偏移**而非直接改臂长 —— 臂长每帧由 Tick 重算，直接写会被冲掉。
	 */
	void AddZoom(float Value);

	/**
	 * 巡航开关（Tab）。开 = 关掉阻尼、速度不再衰减，但**推力照给**（否则到不了圆
	 * 轨道速度）；关 = 有阻尼，松手按 τ 停下。只翻转不接收目标值。
	 */
	void TogglePreserveVelocity();

	/** 巡航（速度保持）是否开启。 */
	bool IsPreserveVelocity() const { return bPreserveVelocity; }

	/**
	 * 切换飞船调试视角（B 键）：船心画落地检测球的线框（命中地面变绿、悬空橙色，
	 * 与按 E 下船同一个球），并允许引力合力箭头显示。状态跟着飞船走。
	 */
	void ToggleDebugViewMode();

	/** 调试视角是否开着。 */
	bool IsDebugViewMode() const override { return bDebugViewMode; }

	/** 取一份状态快照，供 HUD 之类的显示层读取。 */
	FSpaceshipStatus GetStatus() const;

	/**
	 * 相对参考行星的速度低于阈值、且机身检测球命中地面时为真。速度门必须比相对
	 * 速度：停在一颗公转中的行星上时，绝对速度就是行星的轨道速度（约 7.3 km/s）。
	 */
	UFUNCTION(BlueprintPure, Category = "Spaceship")
	bool IsLanded() const;

	/**
	 * 落地检测球半径（厘米）：**运行期恒定**，就是 LandingQueryRadiusCm。
	 * 测试摆参照物时直接用这个值。
	 */
	float GetLandingQueryRadiusCm() const;

	/**
	 * 登船射程（厘米）：与下船用同一套距离 —— 落地检测球半径与下船落脚点取较大者。
	 * 两者不能各写一个数，见 CanBoardFrom。
	 */
	float GetBoardingRadiusCm() const;

	/** 角色是否近到可以登船。 */
	UFUNCTION(BlueprintPure, Category = "Spaceship")
	bool CanBoardFrom(const FVector& CharacterLocation) const;

	/**
	 * 找离 FromLocation 最近的飞船；找不到返回 nullptr。放在飞船侧是因为角色模块
	 * 若再 include 飞船模块就会成环。
	 */
	static ASpaceshipPawn* FindNearestTo(const UWorld* World, const FVector& FromLocation);

	/** 记录登船的角色；传 nullptr 表示离船。 */
	void SetBoardedCharacter(APlanetCharacter* Character);

	/** 当前在船上的角色；没有则为 nullptr。 */
	APlanetCharacter* GetBoardedCharacter() const { return BoardedCharacter; }

	/**
	 * 让角色登船：挂到船下、隐藏、关碰撞，并记成舱里的角色。**不 Possess** ——
	 * 换控制对象归控制器。附着用 KeepWorldTransform，与 SetBoarded 先后无关。
	 */
	void Board(APlanetCharacter* Character);

	/**
	 * 让 Character 离船：摘下、显示、摆到离船落脚点并贴回球面。传入的角色也可以
	 * 还没登船（只做摆位）。不 Possess；Character 为空时无动作。
	 */
	void DisembarkCrew(APlanetCharacter* Character);

	/**
	 * 离船落脚点（世界坐标）：船右侧、探针 AABB 半宽之外再让 CrewExitClearanceCm。
	 * 半宽现算而不是写死距离，飞船横滚后也不会把角色摆进船体里。
	 */
	FVector GetCrewExitLocation() const;

	/**
	 * 接触半径（厘米），求解器的**兜底**形状：正常用 BuildContactProbes 的凸包探针，
	 * 只有 Hull 网格没导 Simple Collision 时才用它（默认 300）。**不能返回 0**：
	 * 半径 0 的点质量会让飞船中心被钳在地表上，机身下半截埋进地形。
	 */
	float GetContactRadiusCm() const override;

	/**
	 * 从 Hull 网格的 Simple Collision 凸包顶点算出接触探针（Actor 本地坐标，厘米），
	 * 按 10 cm 网格去重。不写成员，测试可直接调。@return false = 网格没有 Simple Collision。
	 */
	bool BuildContactProbes(TArray<FVector>& OutProbes) const;

	/** 重建 ContactProbesLocalCm；Hull 网格或变换变化后调用，构造期与 BeginPlay 会自动调。 */
	void RebuildContactProbes();

	/** 接触探针（Actor 本地，厘米）。空数组 = 没有可用凸包，求解器退回 300 cm 球。 */
	const TArray<FVector>& GetContactProbesLocalCm() const override { return ContactProbesLocalCm; }

	/**
	 * 本帧的推力加速度（**厘米/秒²**，世界坐标），已叠加制动与大气阻力。
	 * 推力方向 = 机头 × max(ThrustInput, 0) + 升降 up × VerticalInput，合成向量夹到
	 * 长度 1；S 键是沿**相对参考行星**速度反向的**制动**（无参考行星时即绝对速度），
	 * 刻度 A·Δt 而非固定速度，保证一帧内恰好停住、绝不反向。速度上限只衰减沿速度方向
	 * 的正向推力。DeltaSeconds 必须用调用方那帧的帧时长。
	 */
	FVector GetThrustAccelerationWorldCmPerS2(float DeltaSeconds) const override;

	/**
	 * 速度上限的衰减系数：f = 1 − (v/vmax)²，夹在 [0, 1]；上限 ≤ 0 表示不限，恒返回 1。
	 * 两个参数都是世界厘米/秒（与 GetVelocity 同刻度）。
	 */
	static float ComputeSpeedLimitTaper(double SpeedCmPerSecond, double MaxSpeedCmPerSecond);

	/**
	 * 大气密度比 ρ(h) = exp(−max(h, 0) / 标高)，海平面为 1、壳外为 0；标高 = 表面半径 ×
	 * ScaleHeightRatio。参数非法也返回 0 —— 宁可不加阻力，也不要除零或 exp 溢出。
	 * bAtmosphereEnabled = false 时恒返回 0（整颗行星没大气）。
	 */
	static float ComputeAtmosphereDensityRatio(double AltitudeCm, double SurfaceRadiusCm,
		double ScaleHeightRatio, double AtmosphereHeightRatio,
		bool bAtmosphereEnabled = true);

	/**
	 * 二次大气阻力：a = −D · ρ · (v / 1 km/s)²，方向与速度相反；返回世界厘米/秒²，
	 * 速度为零或任一系数为 0 时为零向量。
	 */
	static FVector ComputeAtmosphereDragAccelerationWorldCmPerS2(
		const FVector& VelocityCmPerSecond, double DensityRatio,
		double DragAccelerationKmPerS2);

	/** 当前参考行星大气里的密度比。没有参考行星 / 壳外 / 行星大气总开关关闭时为 0。 */
	float GetAtmosphereDensityRatio() const;

	/**
	 * 本帧的大气阻力加速度（世界厘米/秒²）。系数从参考行星的大气组件读，与巡航
	 * 开关、速度上限无关。按**相对参考行星**的速度算 —— 大气随行星平移，贴着一颗
	 * 公转中的行星静止时不该有迎面风。
	 */
	FVector GetAtmosphereDragAccelerationWorldCmPerS2() const;

	/**
	 * 阻尼时间常数（秒），0 = 不阻尼。巡航开着就是 0（只关阻尼、推力照给）；
	 * 有阻尼时速度收敛到「推力 × 本值」，这就是极速的来源。
	 */
	float GetDampingTime() const override;

	/** 由 UCosmosSimulationSubsystem 在写回位置时一并写入的**求解器速度**（厘米/秒）。 */
	void SetCosmosVelocity(const FVector& InVelocity) override { CosmosVelocity = InVelocity; }

	/**
	 * 由 UCosmosSimulationSubsystem 每帧写入的**参考行星中心速度**（世界厘米/秒）。
	 * 行星不自转、大气跟着行星平移，阻力与相对速度都以它当参考系；无参考行星时为零。
	 */
	void SetReferencePlanetVelocity(const FVector& InVelocity) override { ReferencePlanetVelocity = InVelocity; }

	/** 相对参考行星的速度（世界厘米/秒）；没有参考行星时等于 GetVelocity()。 */
	FVector GetRelativeVelocity() const { return GetVelocity() - ReferencePlanetVelocity; }

	/** 当前参考行星（控制飞船的那颗，供子系统回查它的求解器速度）；可能为空。 */
	AVoxelPlanetActor* GetPlanetActor() const override { return PlanetActor; }

	/**
	 * 覆写：让引擎侧所有问「这艘船多快」的地方拿到求解器给的真值。
	 * 体素调用器的预测**不**走这里（绝对速度会带出公转），见下一行。
	 */
	virtual FVector GetVelocity() const override { return CosmosVelocity; }

	/**
	 * 体素调用器预测速度：相对参考行星。用绝对速度的话，落地静止（相对 0）时预测点
	 * 仍会顺着公转方向漂出 PredictionTime × 6 km/s ≈ 181 米，气泡看着不在船心。
	 */
	FVector GetPredictionReferenceVelocity() const override { return GetRelativeVelocity(); }

protected:
	/**
	 * 凸包探针不可用时的兜底接触半径（厘米）。**不要设成 0**：半径 0 的点质量会让
	 * 飞船中心被钳在地表上，机身下半截埋进地形。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spaceship")
	float ContactFallbackRadiusCm = 300.f;

	/**
	 * 体素调用器：行星的 LOD 与碰撞都靠它驱动，没有它飞船就是瞎的。挂在飞船根节点
	 * 而不是相机上（插件的 auto-camera 兜底写死玩家索引 0，多人不可用）。类型是本项目的
	 * AlwaysLocal 子类：插件只认被本地 Possess 的 Pawn，进星图换人会把调用器踢出 LOD
	 * 并重建整棵八叉树。登船后必须由它接管，否则角色调用器一关，行星就停在最粗 LOD。
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Spaceship")
	TObjectPtr<UVoxelInvokerWithPredictionComponent> VoxelInvoker;

	/**
	 * 中间 LOD 环：把「LOD1 细气泡 → LOD8 全星」的 7 级落差拆成三级，否则紧挨细气泡的
	 * 8 公里粗块会在高速飞行时横穿地形（「大片白幕」）。只做 LOD：碰撞交给细气泡，
	 * 碰撞细分不看 LODToSet，600 米盒子会请求约 5.5e4 个区块，足以顶爆显存。
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Spaceship")
	TObjectPtr<UVoxelInvokerWithPredictionComponent> VoxelInvokerLOD3;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Spaceship")
	TObjectPtr<UVoxelInvokerWithPredictionComponent> VoxelInvokerLOD5;

	/**
	 * 1 公里外的过渡环：叶块 1.02 公里，补 LOD5（512 米）与行星 MaxLOD 之间的那一档。
	 * 没有它 1 公里处一步跨 3 级，块边界在地表上是笔直硬接缝。范围 ±10 公里是为了
	 * 盖住 20 公里半径星的地平线（约 6.4 公里）。只做 LOD，碰撞照旧交给细气泡。
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Spaceship")
	TObjectPtr<UVoxelInvokerWithPredictionComponent> VoxelInvokerLOD6;

	/**
	 * 外壳网格。**网格与变换都归蓝图管**：换模型走 HullMesh，摆位置/朝向在子蓝图
	 * 视口里拖 Gizmo。它同时是根组件和碰撞体（形状取网格 Simple Collision）。C++ 只在
	 * 构造期塞一个立方体兜底，之后不写任何变换 —— 写了会冲掉蓝图里调好的值。
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Spaceship")
	TObjectPtr<UStaticMeshComponent> Hull;

	/** BuildContactProbes 的结果缓存（Actor 本地，厘米）；为空时求解器退回兜底球。 */
	TArray<FVector> ContactProbesLocalCm;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<USpringArmComponent> SpringArm;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<UCameraComponent> Camera;

	/**
	 * B 键调试视角的落地检测球（线框）。单独做成组件是为了 tick 顺序：它跑在
	 * TG_PostUpdateWork，即求解器写回位置之后；放在本 Actor 的 Tick 里会落后一帧。
	 */
	UPROPERTY(Transient, VisibleAnywhere, Category = "Spaceship|Debug")
	TObjectPtr<ULandingDebugSphereComponent> LandingDebugSphere;

	/**
	 * 按住 W（不按 Shift）时相机沿机身后方后缩的距离（厘米），与当前速度无关；
	 * 0 = 关闭。名字里的 AtFullThrust 是历史遗留，改名会丢关卡实例里已设的值。
	 */
	UPROPERTY(EditAnywhere, Category = "Camera")
	float CameraPullbackAtFullThrust = 350.f;

	/** 按住 Shift 时后缩距离的**倍数**（乘在 CameraPullbackAtFullThrust 上）。 */
	UPROPERTY(EditAnywhere, Category = "Camera")
	float BoostPullbackMultiplier = 2.f;

	/** 相机后缩/回位的平滑速率（FInterpTo 速率）。越小越拖沓。 */
	UPROPERTY(EditAnywhere, Category = "Camera")
	float CameraPullbackInterpSpeed = 4.f;

	/** 滚轮每格的缩放距离（厘米）。越大每格拉得越远。 */
	UPROPERTY(EditAnywhere, Category = "Camera")
	float ZoomStep = 300.f;

	/**
	 * 相机最近距离（厘米）。是**绝对臂长**而非相对基准的偏移 —— 改基准臂长时
	 * 「最近能拉多近」不会跟着漂。
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
	 * 横滚滞后的一阶滤波速率：稳态滞后 ≈ MaxRollRate / 本值（24 → 约 4°），同时把求解器
	 * 定步长造成的逐格转动抹平（一帧 4~11 个子步，每步 0.33°）。设 0 = 与机身刚性绑定，代价是逐格抖。
	 */
	UPROPERTY(EditAnywhere, Category = "Camera")
	float CameraRollFollowSpeed = 24.f;

	/**
	 * 落地自由视角的俯仰下限 / 上限（度），默认 ±75，与 PlanetCharacter 的环绕相机一致。
	 * 0 = 正前方，负 = 低头，正 = 抬头。
	 */
	UPROPERTY(EditAnywhere, Category = "Camera")
	float MinFreeLookPitch = -75.f;

	UPROPERTY(EditAnywhere, Category = "Camera")
	float MaxFreeLookPitch = 75.f;

	/** 退出自由视角后，镜头偏转回到船尾的插值速率（FInterpTo 速率）。越小回位越慢。 */
	UPROPERTY(EditAnywhere, Category = "Camera")
	float FreeLookReturnSpeed = 6.f;

	/** 目标偏航角速度上限（度/秒）。鼠标累加的转速目标夹在这里。 */
	UPROPERTY(EditAnywhere, Category = "Spaceship|Handling")
	float MaxYawRate = 90.f;

	/** 目标俯仰角速度上限（度/秒）。鼠标累加的转速目标夹在这里。 */
	UPROPERTY(EditAnywhere, Category = "Spaceship|Handling")
	float MaxPitchRate = 70.f;

	/** 横滚角速度上限（度/秒）。A/D 给的是持续角加速度，本值是「到顶」的刻度。 */
	UPROPERTY(EditAnywhere, Category = "Spaceship|Handling")
	float MaxRollRate = 120.f;

	/**
	 * 横滚角加速度（度/秒²）。求解器持有实际角速度，输入只能通过加速度进得去。
	 * 默认 1500 = MaxRollRate ÷ 0.08 秒。带阻尼时稳态到不了上限：同向 taper 给出
	 * A/(A/Max + 角阻尼) = 93.75°/s（78%）；反打不受 taper 衰减。
	 */
	UPROPERTY(EditAnywhere, Category = "Spaceship|Handling")
	float RollAngularAcceleration = 1500.f;

	/** 鼠标位移到角速度的换算系数（度/秒 每度）。越大越灵敏。 */
	UPROPERTY(EditAnywhere, Category = "Spaceship|Handling")
	float MouseImpulseScale = 6.f;

	/** 角阻尼（**1/秒**），子系统换算成 1/模拟年后写进求解器。越大惯性越短。 */
	UPROPERTY(EditAnywhere, Category = "Spaceship|Handling")
	float AngularDamping = 3.5f;

	/**
	 * 主推力加速度（**km/s²**，世界层），默认 120 km/s² = 1.2e7 厘米/秒²。是游戏手感值，
	 * 必须大于当地重力（G·M/r²），否则升不起来。极速 = 本值 × DampingTime = 1.2 km/s
	 * （世界速度，HUD 显示的就是它）。换算只有本类内一行 ×1e5，不走 CosmosUnits。
	 */
	UPROPERTY(EditAnywhere, Category = "Spaceship|Thrust",
		meta = (DisplayName = "推力加速度（km/s²，世界）"))
	float ThrustAccelerationKmPerS2 = 120.0f;

	/** 按 Shift 时乘在 ThrustAccelerationKmPerS2 上的倍数。松开即回到 1。 */
	UPROPERTY(EditAnywhere, Category = "Spaceship|Thrust")
	float BoostMultiplier = 3.0f;

	/**
	 * 阻尼时间常数 τ（秒）：a_damp = −v/τ，速度收敛到「推力 × τ」。求解器时间单位是
	 * 模拟年，换算在 UCosmosSimulationSubsystem::ApplyControlledBodyForces 做。0.01 是
	 * 「有惯性感」与「世界跟得上」的折中 —— τ 再大极速上千 km/s，体素范围要跟着涨。
	 */
	UPROPERTY(EditAnywhere, Category = "Spaceship|Thrust",
		meta = (DisplayName = "阻尼时间（秒）"))
	float DampingTime = 0.01f;

	/**
	 * 速度上限（**世界 km/s**，0 = 不限），只在没按 Shift 时生效；比的是**相对参考行星**
	 * 的速度（绝对速度含着行星轨道速度，会把正向推力整根削掉）。接近上限时只衰减沿
	 * 速度方向的正向推力（f = 1 − (v/vmax)²），横向推力与制动不打折。默认 12：本关
	 * 圆轨道速度 7.678 km/s、逃逸约 10.86 km/s。调高要连体素预测范围一起看。
	 */
	UPROPERTY(EditAnywhere, Category = "Spaceship|Thrust",
		meta = (DisplayName = "速度上限（km/s，世界）", ClampMin = "0"))
	float MaxSpeedKmPerSecond = 12.0f;

	/**
	 * Shift 加速时的速度上限（**世界 km/s**，0 = 不限），与上面相互独立、衰减规则相同。
	 * 默认 24（W 的 2 倍）：巡航关着时 Shift 的阻尼极速只有 3.6 km/s，本值不起作用；
	 * 巡航开着才靠它让 Shift 比 W 快。调高后高速时体素地形更追不上。
	 */
	UPROPERTY(EditAnywhere, Category = "Spaceship|Thrust",
		meta = (DisplayName = "加速速度上限（km/s，世界）", ClampMin = "0"))
	float BoostMaxSpeedKmPerSecond = 24.0f;

	/**
	 * 巡航开关。开 = 关掉阻尼（速度保持，但**推力照给**，否则到不了圆轨道速度）；
	 * 关 = 有阻尼，松手按 τ 衰减停下。关掉阻尼后走纯引力测地线，行星引力照样作用，
	 * 所以只能悬停或绕圈，停不下来。细节面板改的是初始状态，运行时按 Tab 切换。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spaceship|Thrust")
	bool bPreserveVelocity = false;

	/**
	 * B 键调试视角开关：显示落地检测球与引力合力箭头。Transient：只是运行期状态，
	 * 不写进关卡，否则编辑器里开一次会污染每次 PIE。
	 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Spaceship|Debug")
	bool bDebugViewMode = false;

	/**
	 * 姿态归求解器后，本类里那段运动学自动回正已删除，本属性**暂时没有消费者**（留着的开关，默认 0）。
	 * GetAutoLevelUp() 现在也没有消费者了，留着给将来的回正逻辑。
	 */
	UPROPERTY(EditAnywhere, Category = "Spaceship|Handling")
	float AutoLevelSpeed = 0.f;

	/**
	 * 参考行星（控制飞船的那颗）。BeginPlay 时找最近一颗，找不到则自动回正退回世界 Z、
	 * 相对速度退化为绝对速度；软依赖，缺了它飞船照常飞。它同时喂给自动回正、
	 * 升降推力方向、大气密度/阻力与参考速度。
	 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Spaceship")
	TObjectPtr<AVoxelPlanetActor> PlanetActor;

	/** 查找行星时的最大距离（厘米）。默认 1000 公里，够覆盖任意单颗行星。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spaceship")
	float PlanetSearchRadius = 1.0e8f;

	/**
	 * 重新评估「最近的行星」的间隔（秒），默认 0.5，0 = 关闭。行星在动、最近的一颗
	 * 会换人；不重评的话飞离母星后自动回正会一直对齐到旧星。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spaceship")
	float PlanetReevaluateInterval = 0.5f;

	/**
	 * 体素调用器的高速门控：速度高于此值（世界 km/s）时只关掉细气泡的**碰撞**，
	 * 三档 LOD 保持开启 —— 碰撞走 Height 0 细分、代价远高于 LOD，关 LOD 会让高速下
	 * 身边只剩轨道 LOD、星球变糊（2026-10-21 实机反馈）。默认 0.6，与 Resume 构成回差。
	 * 0 = 关闭门控。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spaceship|Voxel",
		meta = (DisplayName = "关碰撞 invoker 速度阈值（km/s，世界）", ClampMin = "0"))
	float VoxelInvokerSuspendSpeedKmPerSecond = 0.6f;

	/** 速度低于此值（世界 km/s）时重开细气泡碰撞。必须小于上面的阈值，两者之差就是回差。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spaceship|Voxel",
		meta = (DisplayName = "开碰撞 invoker 速度阈值（km/s，世界）", ClampMin = "0"))
	float VoxelInvokerResumeSpeedKmPerSecond = 0.2f;

	/**
	 * 两次启停之间的最小间隔（秒）。切换 bUseForCollisions 会让插件重建整棵八叉树，
	 * 阈值附近抖动时反复重建会把省下的开销加倍还回去。回差之外再加一道时间闸。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spaceship|Voxel",
		meta = (ClampMin = "0"))
	float VoxelInvokerToggleCooldownSeconds = 0.5f;

	/**
	 * 每帧扫过的距离（相对行星）超过此值就关掉细气泡**碰撞**。碰撞走 Height 0 全分辨
	 * 率细分，范围只有 30 m；一帧走 30 m 以上时整圈每帧重建，且根本来不及挡住任何东西。
	 * 按距离而不是按速度判断：同样 0.37 km/s，60 FPS 下每帧 6 m 并不贵。0 = 关闭。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spaceship|Voxel",
		meta = (DisplayName = "关碰撞 invoker 每帧位移阈值（cm）", ClampMin = "0"))
	float VoxelInvokerCollisionSweepCm = 3000.f;

	/**
	 * 最细那一档 LOD（LOD1，叶块 32 m）的每帧位移上限，单位「叶块边长」。LOD 重建量
	 * ∝ 每帧位移 ÷ 叶块面积：一帧走过好几个叶块时，细档每帧被整体重做一遍，只贡献卡顿
	 * 不贡献细节。超了就关掉它回落 LOD3，位移降到 1/3 以下再开回来。0 = 关闭。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spaceship|Voxel",
		meta = (DisplayName = "细档每帧位移上限（叶块边长倍数）", ClampMin = "0"))
	float VoxelInvokerFineLODSweepLeafWidths = 3.f;

	/**
	 * 「已落地」的速度阈值（厘米/秒），比的是**相对参考行星**的速度 —— 绝对速度
	 * 就是行星的轨道速度（本关卡约 7.3 km/s），任何合理阈值都会让它恒为假。
	 */
	UPROPERTY(EditAnywhere, Category = "Spaceship|Landing")
	float LandingSpeedThreshold = 200.f;

	/**
	 * 落地检测球半径（厘米）。**恒定值，不随机身姿态变化** —— 早先按「机身中心到船底
	 * 沿重力方向的距离」现算，机身一滚球就跟着胀缩，调试球看着像在呼吸。
	 * 默认 4200 ≈ 本资源翼展半径（网格 ±4133 × ±2755 × ±996），机身横滚到任何姿态
	 * 球面都够得到地面。代价是机身中心离地 42 m 内就判「落地」（自由视角 + 按 E 下船），
	 * 对 83 m 翼展的船来说仍算贴地；换模型要跟着改。
	 */
	UPROPERTY(EditAnywhere, Category = "Spaceship|Landing")
	float LandingQueryRadiusCm = 4200.f;

	/** 离船落脚点在船体水平半宽之外再让开多少厘米。半宽取自接触探针的本地 AABB。 */
	UPROPERTY(EditAnywhere, Category = "Spaceship|Boarding")
	float CrewExitClearanceCm = 250.f;

	/** 离船后额外抬高（厘米）。在球面吸附**之后**生效，避免人卡在船边的地形棱里。 */
	UPROPERTY(EditAnywhere, Category = "Spaceship|Boarding")
	float CrewExitLiftCm = 30.f;

	/**
	 * 船体模型。留空 = C++ 不介入：蓝图里覆盖的模型、或构造期塞的引擎立方体保持原样。
	 * 推荐走这个属性而不是直接覆盖 Hull 组件的静态网格体 —— 组件覆盖记在
	 * UInheritableComponentHandler 里，原生类重编译有模型无声退回占位立方体的风险。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spaceship|Mesh")
	TObjectPtr<UStaticMesh> HullMesh;

private:
	/**
	 * 把 HullMesh 套用到 Hull 的**网格体**上，不碰任何变换 —— 位置/朝向/缩放留给蓝图，
	 * 在这里写会在每次重编译时冲掉手拖出来的值。必须在 OnConstruction 与 BeginPlay 调：
	 * 构造期读到的是 C++ 默认值，蓝图 Class Defaults 里改的 HullMesh 会静默失效。
	 */
	void ApplyHullMesh();

	/**
	 * 只开关细气泡的碰撞通道（bUseForCollisions），LOD 保持原样。插件会在下次 LOD
	 * 更新时发现标志变化并重建八叉树，不需要手动 RefreshAllVoxelInvokers。
	 */
	void SetVoxelInvokerCollisionEnabled(bool bEnabled);

	/**
	 * 只开关最细那一档的 LOD（bUseForLOD），碰撞与其余三档保持原样。关掉后身边从
	 * LOD3 起，行星不会塌到轨道 LOD。
	 */
	void SetVoxelInvokerFineLODEnabled(bool bEnabled);

	/** 三档调用器一起开关 —— 只留高档近处会糊，只留低档就没有碰撞气泡。 */
	void ApplyViewpointInvokerState();

	/** 本 Pawn 是否当前视点。初值 false —— OnRegister 会自动启用，BeginPlay 要按它关掉。 */
	bool bIsViewpoint = false;

	float ThrustInput = 0.f;
	float RollInput = 0.f;
	float VerticalInput = 0.f;

	/**
	 * 待发角速度增量（机体系，度/秒）：Y = 俯仰、Z = 偏航。
	 * Look() 累加，GetAngularAccelerationWorldRadPerS2() 读一次并清零。
	 */
	FVector PendingAngularImpulseLocalDeg = FVector::ZeroVector;

	/** 子系统写回的实际角速度（世界系 rad/s），只用于横滚到顶的衰减。 */
	FVector CosmosAngularVelocity = FVector::ZeroVector;

	/** 加速键（Shift）当前是否按下。 */
	bool bBoosting = false;

	/**
	 * 求解器每帧写回的当前速度（厘米/秒，世界坐标），本类只读不改。初值零 ——
	 * 第一帧写回前 GetVelocity() 返回零向量。
	 */
	FVector CosmosVelocity = FVector::ZeroVector;

	/** 参考行星中心速度（厘米/秒）。大气随行星平移，阻力与相对速度都用它当参考系。 */
	FVector ReferencePlanetVelocity = FVector::ZeroVector;

	/** 距离下次重新评估最近行星还剩多少秒。 */
	float PlanetReevaluateTimer = 0.f;

	/** 细气泡的碰撞通道是否因高速被主动关掉（LOD 三档始终开着）。 */
	bool bVoxelInvokerCollisionSuspended = false;

	/** 最细那一档 LOD 是否因每帧位移过大被关掉（只剩 LOD3/5/6）。 */
	bool bVoxelInvokerFineLODSuspended = false;

	/** 距下次允许切换调用器还剩多少秒（回差之外的时间闸）。 */
	float VoxelInvokerToggleTimer = 0.f;

	/**
	 * 上一帧采样，用来量**行星参考系下**的速度。门控必须按相对速度（行星每秒走几十
	 * 公里）；这里用两个 Actor 的逐帧位移相减，免得每帧去查求解器。
	 */
	FVector LastVoxelInvokerShipLocation = FVector::ZeroVector;
	FVector LastVoxelInvokerPlanetLocation = FVector::ZeroVector;
	TObjectPtr<AVoxelPlanetActor> LastVoxelInvokerPlanet;
	bool bHasVoxelInvokerSpeedSample = false;

	/**
	 * SpringArm 的基准臂长，BeginPlay 时从组件上抄下来。蓝图或关卡实例可能改过
	 * TargetArmLength，抄一次再叠加后缩量才不会被本类覆盖掉。
	 */
	float BaseArmLength = 0.f;

	/** 当前相机后缩量（厘米），平滑后的值。 */
	float CameraPullback = 0.f;

	/** 滚轮缩放的**目标**偏移（厘米），正 = 相机更远。滚轮改的是它。 */
	float TargetZoomOffset = 0.f;

	/** 平滑后的缩放偏移（厘米）。与 CameraPullback 一起加在基准臂长之上。 */
	float ZoomOffset = 0.f;

	/**
	 * 当前横滚滞后（度，负 = 视线落后机身），由 CalcCamera 按帧间四元数增量积分。是**相对量**，
	 * 不存绝对角：绝对欧拉 Roll 在俯仰 ±90° 处跳 180°，会让画面整圈翻转。
	 */
	float CameraRollLagDegrees = 0.f;

	/**
	 * 上一次横滚采样（CalcCamera 里，TickObjects 写回之后）的机身朝向与时间。间隔超过
	 * 0.5 s（切星图、卡顿）直接对齐而不是补一个巨大滞后；同一帧多次 CalcCamera 只采样一次。
	 */
	FQuat LastCameraRollHullRotation = FQuat::Identity;
	double LastCameraRollSampleTime = 0.0;
	uint64 LastCameraRollSampleFrame = 0;
	bool bHasCameraRollSample = false;

	/**
	 * 相机的基准相对朝向（蓝图设的）。横滚是把它一起拧，不是覆盖 —— 否则会把蓝图里
	 * 调好的朝向冲掉。BeginPlay 时从组件上抄下来。
	 */
	FQuat CameraBaseRotation = FQuat::Identity;

	/**
	 * SpringArm 的基准相对朝向，BeginPlay 时从组件上抄下来。自由视角的偏移叠加在
	 * 它上面，避免冲掉蓝图里调好的摇臂朝向。
	 */
	FQuat SpringArmBaseRotation = FQuat::Identity;

	/**
	 * 落地自由视角是否激活，每帧 Tick 开头由 ShouldUseFreeLook 重算。激活时鼠标改去累加
	 * FreeLookYaw/Pitch、不再给飞船角速度；已经转起来的角速度由求解器的角阻尼衰减。
	 */
	bool bFreeLookActive = false;

	/** 自由视角的偏航偏移（度，相对船尾正后方）。退出后平滑回 0。 */
	float FreeLookYaw = 0.f;

	/** 自由视角的俯仰偏移（度）。退出后平滑回 0。 */
	float FreeLookPitch = 0.f;

	/** 在关卡里找离飞船最近的一颗行星；找不到返回 nullptr。 */
	AVoxelPlanetActor* FindNearestPlanet() const;

	/** 自动回正的目标"上"：有行星时取径向外侧，太空中退回世界 Z。 */
	FVector GetAutoLevelUp() const;

	/** CalcCamera 每帧一次：按机身朝向增量推进 CameraRollLagDegrees。 */
	void UpdateCameraRollLag(const FQuat& HullRotation, float DeltaSeconds);

	/** 当前在船上的角色。 */
	UPROPERTY(Transient)
	TObjectPtr<APlanetCharacter> BoardedCharacter;
};
