// Copyright Voxel Plugin SAS. All Rights Reserved.
// 街机式太空飞船。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "VoxelViewpointInvokers.h"
#include "SpaceshipPawn.generated.h"

class APlanetCharacter;
class AVoxelPlanetActor;
class UCameraComponent;
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
	/**
	 * 速度大小（**世界 km/s**，**惯性系 / 绝对速度**）。取自求解器写回的速度，
	 * 再除以 1e5。
	 *
	 * 世界速度 ≠ 编辑器里填的物理速度：物理 1 AU/年 = 4.74047 km/s，
	 * 世界 1 AU/年 = CentimetersPerAu × TimeScale 厘米/秒 = 0.0333 km/s（默认）。
	 *
	 * 贴着一颗公转中的行星静止时，这个值不是 0，而是行星自己的速度 —— 要看
	 * 「相对地表多快」请读 RelativeSpeedKmPerSecond。
	 */
	float SpeedKmPerSecond = 0.f;

	/**
	 * 相对参考行星（控制飞船的那颗，见 PlanetActor）的速度大小（**世界 km/s**）。
	 *
	 * 大气随行星平移、行星自身不自转，所以大气阻力与「是不是停在地表上」都该看
	 * 这个值，而不是绝对速度。没有参考行星时等于 SpeedKmPerSecond。
	 */
	float RelativeSpeedKmPerSecond = 0.f;

	/** 本帧是否有参考行星。没有时 HUD 的「相对速度」显示占位符。 */
	bool bHasReferencePlanet = false;

	/**
	 * 实际生效的速度上限（**世界 km/s**）：阻尼极速（= 推力 × τ × 加速倍率）与
	 * **当前生效的**硬上限（按 Shift 时为 BoostMaxSpeedKmPerSecond，否则为
	 * MaxSpeedKmPerSecond）里较小且非 0 的那个，HUD 直接印它。
	 *
	 * 0 表示**没有上限**（巡航开着、硬上限也设成了 0），不是「不能动」。
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

	/**
	 * 当前所在处的大气密度比（海平面 = 1，壳外 = 0）。没有参考行星时也为 0。
	 * 显示层据此区分「在真空里」与「在大气里」。
	 */
	float AtmosphereDensityRatio = 0.f;

	/** 当前大气阻力减速度的大小（**世界 km/s²**）。0 = 无阻力（真空或关掉了）。 */
	float AtmosphereDragKmPerS2 = 0.f;

	/**
	 * 当前模式下能给出的最大推力（**世界 km/s²**，按住 Shift 时已乘 BoostMultiplier）。
	 *
	 * 这是**标称值**，不是本帧实际输出：速度上限会削掉沿速分量、大气阻力另算。
	 * 显示层拿它和当地重力比较 —— 重力大于它时，飞船在物理上抬不起来。
	 */
	float MaxThrustAccelerationKmPerS2 = 0.f;
};

/**
 * 街机式太空飞船。
 *
 * **位移完全归 N 体求解器**：本类不自己搬自己，只把「这一帧想施加多少推力加速度」
 * 暴露出去（GetThrustAccelerationWorldCmPerS2 / GetDampingTime），由
 * UCosmosSimulationSubsystem 每帧下来读走、换算成 AU/年² 写进它那个 FCosmosBodyState。
 * 位置与速度都由求解器写回（SetActorLocation + SetCosmosVelocity）。
 *
 * 力只能是这个方向：依赖是 **Cosmos → Spaceship**（子系统要 Cast 到 ASpaceshipPawn
 * 读推力与阻尼），所以 Spaceship 不能反向依赖 Cosmos —— 既不能 include
 * CosmosOrbitComponent.h，也不能调那个子系统。
 *
 * 鼠标给的是角速度冲量而不是直接设朝向，松手后指数衰减 —— 这是"开飞机"的惯性感
 * 来源，也是《戴森球计划》伊卡洛斯的做法。姿态是**运动学**的：它只决定推力朝哪，
 * 不参与积分。
 */
UCLASS()
class STELLARERASPACESHIP_API ASpaceshipPawn : public APawn, public IVoxelViewpointInvokers
{
	GENERATED_BODY()

public:
	ASpaceshipPawn();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	//~ Begin IVoxelViewpointInvokers Interface
	virtual void SetViewpointInvokersActive(bool bActive) override;
	//~ End IVoxelViewpointInvokers Interface

	/** 主推力输入（-1 = 制动，+1 = 前进）。 */
	void SetThrust(float Value);

	/** 横滚输入（-1 / +1）。 */
	void SetRoll(float Value);

	/** 升降输入（-1 = 降，+1 = 升）。 */
	void SetVertical(float Value);

	/** 鼠标输入（度）。以冲量形式累加进角速度，松手后衰减。 */
	void Look(float DeltaYaw, float DeltaPitch);

	/**
	 * 落地自由视角的激活条件：已落地、且没有按 W（正向推力）或 Space（上升）。
	 *
	 * 输入约定见 StellarEraPlayerController：W / Space 给 +1，S / Ctrl 给 -1。
	 * 所以负值只是制动 / 下降，**不**退出自由视角 —— 只有起飞方向的输入才把
	 * 视角还给飞船。纯函数，测试直接调它。
	 */
	static bool ShouldUseFreeLook(bool bLanded, float InThrustInput, float InVerticalInput);

	/**
	 * 自由视角的鼠标累加：偏航归一化到 (-180, 180]，俯仰夹在 [MinPitch, MaxPitch]。
	 *
	 * 与 PlanetCharacter 的环绕相机同一套口径。纯函数，测试直接调它。
	 */
	static void ApplyFreeLookLookInput(
		float DeltaYaw, float DeltaPitch, float MinPitch, float MaxPitch,
		float& InOutYaw, float& InOutPitch);

	/**
	 * 升降推力的"上"方向：有参考行星时取**行星球心 → 飞船中心**的径向，
	 * 否则退回机身本地 up。纯函数，测试直接调它。
	 *
	 * 船心与星心重合（退化）时径向为零，同样退回本地 up。
	 */
	static FVector ResolveVerticalThrustUp(
		const FVector& ShipLocation,
		const FVector& PlanetLocation,
		bool bHasPlanet,
		const FVector& BodyUp);

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

	/**
	 * 落地检测球的实际半径（厘米）：球心在机身中心，球面覆盖「船体沿重力方向
	 * 的最低点到机身中心的距离 + LandingProbeDistance」。
	 *
	 * 单点收口，测试摆参照物时直接用这个值，别在测试里重复算一遍船体深度。
	 */
	float GetLandingQueryRadiusCm() const;

	/** 角色是否近到可以登船。 */
	UFUNCTION(BlueprintPure, Category = "Spaceship")
	bool CanBoardFrom(const FVector& CharacterLocation) const;

	/**
	 * 找离 FromLocation 最近的飞船；找不到返回 nullptr。
	 *
	 * 放在飞船侧而不是角色侧：飞船模块已经依赖角色模块（持有 APlanetCharacter*），
	 * 角色模块若再 include 飞船模块就会成环。调用方是 AStellarEraPlayerController。
	 */
	static ASpaceshipPawn* FindNearestTo(const UWorld* World, const FVector& FromLocation);

	/** 记录登船的角色；传 nullptr 表示离船。 */
	void SetBoardedCharacter(APlanetCharacter* Character);

	/** 当前在船上的角色；没有则为 nullptr。 */
	APlanetCharacter* GetBoardedCharacter() const { return BoardedCharacter; }

	/**
	 * 接触半径（厘米）—— 求解器的**兜底**接触形状：没有凸包探针时才用。
	 *
	 * 返回 ContactFallbackRadiusCm（默认 300 厘米）。**不能返回 0**：半径 0 的
	 * 点质量在求解器里会让飞船的**中心**被钳在地表上，机身下半截埋进地形里。
	 *
	 * 正常情况下求解器用的是 BuildContactProbes 产出的凸包探针（见
	 * GetContactProbesLocalCm）；这个半径只在网格没导 Simple Collision 时生效。
	 */
	float GetContactRadiusCm() const;

	/**
	 * 从 Hull 当前的网格算出接触探针（Actor 本地坐标，厘米）。
	 *
	 * 取网格 Simple Collision 里每个凸包的顶点，按「凸包元素变换 × Hull 相对变换」
	 * 搬到 Actor 本地，再按 10 cm 网格去重。不写成员 —— 测试可以直接调它。
	 *
	 * @return false 表示 Hull 没有网格或网格没有 Simple Collision。
	 */
	bool BuildContactProbes(TArray<FVector>& OutProbes) const;

	/**
	 * 重建 ContactProbesLocalCm。Hull 网格或变换变化后调用；OnConstruction 与
	 * BeginPlay 会自动调用，测试也可以手动调。
	 */
	void RebuildContactProbes();

	/**
	 * 接触探针（Actor 本地，厘米）。空数组 = 没有可用的凸包，求解器退回 300 cm 球。
	 * 由 BeginPlay / OnConstruction 在套用 HullMesh 之后重建。
	 */
	const TArray<FVector>& GetContactProbesLocalCm() const { return ContactProbesLocalCm; }

	/**
	 * 本帧要施加在自己那个 N 体上的推力加速度（**厘米/秒²**，世界坐标）。零 = 无推力。
	 *
	 * 输入单位是 ThrustAccelerationKmPerS2（km/s²），这里乘 1e5 换成厘米/秒²。
	 * 由 UCosmosSimulationSubsystem 每帧下来读走（依赖方向见类注释）。DeltaSeconds
	 * 是**调用方那一帧的帧时长**，只被制动用到 —— 制动要按「本帧满推力能改变多少
	 * 速度」收敛，必须知道这一帧有多长。用调用方的值而不是自己 Tick 里抄一份存起来：
	 * 两个 Tick 谁先谁后不保证，抄的那份在帧率突变时会让制动过冲一整帧。
	 *
	 * 方向 = 机头 × max(ThrustInput, 0) + 升降 up × VerticalInput，合成向量夹到
	 * 长度 1 —— 与 UFloatingPawnMovement 原来的 GetClampedToMaxSize(1) 同规则：
	 * 斜着同时按 W 和 Space 不该比单按一个多出 41% 的推力。再乘
	 * ThrustAccelerationKmPerS2 × 1e5，Shift 时乘 BoostMultiplier。
	 *
	 * 升降 up 不是机身本地 up：有参考行星时取**行星球心 → 飞船中心**的径向，
	 * 这样贴地时无论机身横滚 / 俯仰成什么样，Space 都是沿当地垂直离开球面；
	 * 太空中（PlanetActor 为空）才退回机身本地 up。
	 *
	 * ThrustInput < 0（S）不产生机头方向的推力，而是**制动**：方向取速度的反方向
	 * （与机头无关），大小按「本帧满推力能改变的速度」收敛 ——
	 *
	 *     |a| = A · min(1, |v| / (A·Δt))      A = 推力（厘米/秒²，Shift 时乘倍率）
	 *
	 * 于是本帧的减速量 = min(A·Δt, |v|) ≤ |v|：一帧内恰好停住，**绝不会把速度推
	 * 过头翻成反向**。刻度必须是 A·Δt 而不是某个固定速度：外力是整帧恒定的
	 * （求解器只在帧首读一次），帧率越低 A·Δt 越大，固定刻度会先过冲、再被逐帧
	 * 修正成来回翻符号的抖动 —— 3 fps（编辑器不在前台时的实际帧率）下直接发散。
	 *
	 * 制动只削速度、不抗重力：低速时它给不出托住飞船的力，悬停仍归 W / 升降键。
	 *
	 * 速度上限只衰减**沿速度方向的正向**推力：f = 1 − (v/vmax)²，v ≥ vmax 时
	 * f = 0，所以速度是渐近逼近上限而不是撞墙。上限按 Shift 分两档（按住用
	 * BoostMaxSpeedKmPerSecond，松开用 MaxSpeedKmPerSecond），衰减规则相同。
	 * 横向推力与制动不打折 —— 否则到顶后既转不了向也刹不住车，在重力井里连
	 * 抬头抗重力的力都拿不到。
	 *
	 * 返回的是**合力**：推力 / 制动之后再叠加大气阻力（见
	 * GetAtmosphereDragAccelerationWorldCmPerS2）。阻力与输入无关，推力为 0、
	 * 巡航开着时照样生效；它也**不**受速度上限衰减。
	 */
	FVector GetThrustAccelerationWorldCmPerS2(float DeltaSeconds) const;

	/**
	 * 速度上限的衰减系数：f = 1 − (v/vmax)²，夹在 [0, 1]。
	 *
	 * @param SpeedCmPerSecond    当前速度（世界厘米/秒，与 GetVelocity 同刻度）
	 * @param MaxSpeedCmPerSecond 速度上限（世界厘米/秒）；≤ 0 表示不限，恒返回 1
	 */
	static float ComputeSpeedLimitTaper(double SpeedCmPerSecond, double MaxSpeedCmPerSecond);

	/**
	 * 大气密度比 ρ(h)：海平面为 1，按标高指数衰减，壳外为 0。
	 *
	 *     ρ(h) = exp(−max(h, 0) / 标高)
	 *     标高 = 表面半径 × ScaleHeightRatio
	 *     壳顶 h = 表面半径 × AtmosphereHeightRatio，h ≥ 壳顶直接返回 0
	 *
	 * 参数非法（半径 / 标高比 / 壳厚比 ≤ 0）也返回 0 —— 宁可不加阻力，也不要
	 * 除零或 exp 溢出把飞船弹飞。
	 */
	static float ComputeAtmosphereDensityRatio(double AltitudeCm, double SurfaceRadiusCm,
		double ScaleHeightRatio, double AtmosphereHeightRatio);

	/**
	 * 二次大气阻力：a = −D · ρ · (v / 1 km/s)²，方向与速度相反。
	 *
	 * @param VelocityCmPerSecond    飞船速度（世界厘米/秒）
	 * @param DensityRatio           密度比（0 ~ 1，见 ComputeAtmosphereDensityRatio）
	 * @param DragAccelerationKmPerS2 海平面、1 km/s 时的减速度（km/s²）
	 * @return 世界厘米/秒²；速度为零或任一系数为 0 时为零向量
	 */
	static FVector ComputeAtmosphereDragAccelerationWorldCmPerS2(
		const FVector& VelocityCmPerSecond, double DensityRatio,
		double DragAccelerationKmPerS2);

	/** 当前参考行星大气里的密度比。没有参考行星 / 壳外时为 0。 */
	float GetAtmosphereDensityRatio() const;

	/**
	 * 本帧的大气阻力加速度（世界厘米/秒²）。
	 *
	 * 取最近参考行星（PlanetActor）的大气组件：阻力系数、标高比、壳厚比都从
	 * 那里读，所以每颗行星可以单独调。与巡航开关、速度上限都无关。
	 *
	 * 阻力按**相对参考行星**的速度算（GetRelativeVelocity），不是惯性系绝对
	 * 速度：大气随行星平移，贴着一颗公转中的行星静止时不该有迎面风。参考速度由
	 * 子系统每帧经 SetReferencePlanetVelocity 推进来。
	 */
	FVector GetAtmosphereDragAccelerationWorldCmPerS2() const;

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
	 * 由 UCosmosSimulationSubsystem 每帧写入的**参考行星中心速度**（世界厘米/秒）。
	 *
	 * 行星自身不自转，大气跟着行星中心平移，所以大气阻力必须按「飞船相对行星」
	 * 的速度算；相对速度也是 HUD 要显示的第二条速度。方向只能是子系统下来推：
	 * StellarEraSpaceship 不能反向依赖 StellarEraCosmos 去查求解器（会成环）。
	 * 没有参考行星时传零向量，相对速度退化为绝对速度。
	 */
	void SetReferencePlanetVelocity(const FVector& InVelocity) { ReferencePlanetVelocity = InVelocity; }

	/** 相对参考行星的速度（世界厘米/秒）；没有参考行星时等于 GetVelocity()。 */
	FVector GetRelativeVelocity() const { return GetVelocity() - ReferencePlanetVelocity; }

	/** 当前参考行星（控制飞船的那颗，供子系统回查它的求解器速度）；可能为空。 */
	AVoxelPlanetActor* GetPlanetActor() const { return PlanetActor; }

	/**
	 * 覆写：让引擎侧所有问「这艘船多快」的地方拿到求解器给的真值。
	 *
	 * 单点收口，替掉三个散落的改点：IsLanded() 的第一道判断、GetStatus().SpeedKmPerSecond、
	 * 以及体素调用器的预测（UVoxelInvokerWithPredictionComponent::
	 * GetInvokerGlobalPosition 读的是 GetOwner()->GetVelocity()）。删掉移动组件后
	 * 根组件的速度不再有人写，不覆写的话预测会静默失效 —— 高速飞行时地形就追不上了。
	 */
	virtual FVector GetVelocity() const override { return CosmosVelocity; }

protected:
	/**
	 * 凸包探针不可用时的兜底接触半径（厘米）。默认 300。
	 *
	 * 只有 Hull 网格没有 Simple Collision 时才会用到 —— 正常情况下求解器走
	 * BuildContactProbes 的凸包探针。**不要设成 0**：半径 0 的点质量会让飞船中心
	 * 被钳在地表上，机身下半截埋进地形。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spaceship")
	float ContactFallbackRadiusCm = 300.f;

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
	 * 为什么挂在飞船根节点而不是相机上：插件的 UVoxelInvokerAutoCameraComponent
	 * 只是 bUseCameraIfNoInvokersFound 的兜底，且写死玩家索引 0
	 * （VoxelInvokerComponent.cpp:177），多人下不可用。
	 *
	 * 类型是本项目的 UAlwaysLocalVoxelInvokerWithPredictionComponent：插件的
	 * IsLocalInvoker（VoxelInvokerComponent.cpp:21）只认被本地控制器 Possess 的
	 * Pawn，而进星图 = Possess(星图相机) 会把飞船的调用器踢出 LOD
	 * （VoxelDefaultLODManager.cpp:218），强制整棵八叉树重建。覆写成恒真后，
	 * Possess 换人不再影响 LOD，详见 AlwaysLocalVoxelInvokerComponent.h。
	 *
	 * 为什么登船后必须换人扛：登船时角色的调用器由 APlanetCharacter 自己关掉
	 * （PlanetCharacter.cpp 的 bBoarded 分支），不关的话它和飞船会各养一份
	 * 脚下的精细块。没有飞船这一份，整个行星会停在行星调用器给的最粗 LOD 上 ——
	 * 这正是「开飞船时星球不更新 LOD、也不碰撞」的成因。
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Spaceship")
	TObjectPtr<UVoxelInvokerWithPredictionComponent> VoxelInvoker;

	/**
	 * 中间 LOD 环：把「LOD1 细气泡 → LOD8 全星」的 7 级落差拆成三级。
	 *
	 * 只有细气泡时，紧挨着细气泡的就是 8.2 公里的粗块；插件在
	 * WaitingForNewChunks 阶段故意保留旧块直到替代块全部完成
	 * （VoxelDefaultRenderer.cpp:464），再用 2 × ChunksDitheringDuration 淡出，
	 * 于是 8 公里级面片会横穿细地形 —— 高速飞行的「大片白幕」就是这么来的。
	 *
	 * 只做 LOD。碰撞交给细气泡：碰撞细分不看 LODToSet，一路到 Height 0，
	 * 600 米的盒子会请求约 5.5e4 个区块，足以顶爆显存。
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Spaceship")
	TObjectPtr<UVoxelInvokerWithPredictionComponent> VoxelInvokerLOD3;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Spaceship")
	TObjectPtr<UVoxelInvokerWithPredictionComponent> VoxelInvokerLOD5;

	/**
	 * 外壳网格。**网格与变换都归蓝图管**：
	 *
	 *   - 换模型：用 HullMesh 属性（推荐），或直接在子蓝图里覆盖本组件的静态网格体；
	 *   - 摆位置/朝向/缩放：在子蓝图（BP_SpaceshipPawn）的视口里选中 Hull 拖 Gizmo，
	 *     值存在蓝图资产里，重编译不会被清掉；关卡实例上也能单独调。
	 *
	 * 碰撞也归这个组件：构造函数打开它的 QueryAndPhysics，形状取网格的
	 * Simple Collision（凸包）。它同时是 Actor 的**根组件**，因此它的变换就是
	 * 飞船的变换锚点（求解器写回的位置也落在这里）。
	 *
	 * C++ 只在构造期塞一个引擎立方体兜底，让裸放的 ASpaceshipPawn 不至于隐形，
	 * 之后不再写这里的任何变换 —— 一旦在这里写，蓝图里拖出来的值每次重编译都会
	 * 被冲掉，"模型在蓝图里调不动"就是这么来的。
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Spaceship")
	TObjectPtr<UStaticMeshComponent> Hull;

	/**
	 * BuildContactProbes 的结果缓存（Actor 本地，厘米）。
	 * 每帧由子系统读走；为空时求解器退回 GetContactRadiusCm 的球。
	 */
	TArray<FVector> ContactProbesLocalCm;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<USpringArmComponent> SpringArm;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<UCameraComponent> Camera;

	/**
	 * 按住 W（不按 Shift）时相机沿机身后方后缩的距离（厘米）。
	 *
	 * 由推力输入驱动（见 Tick 第 5 段）：按住 W 就缩到这个距离，松开平滑收回，与
	 * 当前速度无关。按 S 制动不后缩。0 = 关闭。
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

	/**
	 * 落地自由视角的俯仰下限 / 上限（度）。默认 ±75，与 PlanetCharacter 的环绕相机一致。
	 * 0 = 正前方，负 = 低头看船底，正 = 抬头看天。
	 */
	UPROPERTY(EditAnywhere, Category = "Camera")
	float MinFreeLookPitch = -75.f;

	UPROPERTY(EditAnywhere, Category = "Camera")
	float MaxFreeLookPitch = 75.f;

	/** 退出自由视角后，镜头偏转回到船尾的插值速率（FInterpTo 速率）。越小回位越慢。 */
	UPROPERTY(EditAnywhere, Category = "Camera")
	float FreeLookReturnSpeed = 6.f;

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
	 * 主推力加速度（**km/s²**，世界层）。默认 120 km/s² = 1.2e7 厘米/秒²。
	 *
	 * 这是**游戏手感值**，不是真实飞船的加速度：它必须大于飞船所在处的局部重力
	 * 加速度，否则一步都升不起来。局部重力 = G·M/r²，随所在天体与距离变化，
	 * 要看具体值就开 PIE 看开局日志（质量与位置都在那里）。
	 *
	 * 极速 = 本值 × DampingTime = 120 × 0.01 = 1.2 km/s（**世界速度**，HUD 显示的
	 * 就是它）。世界速度与物理速度不是一回事，见
	 * UCosmosOrbitComponent::InitialVelocityKmPerSecond 的注释。
	 *
	 * 换算是本模块内的一行（× 1e5），因为 StellarEraSpaceship 不能依赖 StellarEraCore。
	 */
	UPROPERTY(EditAnywhere, Category = "Spaceship|Thrust",
		meta = (DisplayName = "推力加速度（km/s²，世界）"))
	float ThrustAccelerationKmPerS2 = 120.0f;

	/** 按 Shift 时乘在 ThrustAccelerationKmPerS2 上的倍数。松开即回到 1。 */
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
	UPROPERTY(EditAnywhere, Category = "Spaceship|Thrust",
		meta = (DisplayName = "阻尼时间（秒）"))
	float DampingTime = 0.01f;

	/**
	 * 速度上限（**世界 km/s**，与 HUD 的速度同一刻度）。0 = 不限。
	 *
	 * **只在没按 Shift 时生效**；按 Shift 时改用 BoostMaxSpeedKmPerSecond。
	 *
	 * 越接近上限，**沿速度方向的正向**推力越小（f = 1 − (v/vmax)²），到顶时 f = 0：
	 * 速度渐近逼近上限，不是撞上一堵墙。横向推力与 S 制动不打折，到顶仍能转向、
	 * 能刹车（见 GetThrustAccelerationWorldCmPerS2）。
	 *
	 * 默认 12 km/s：本关卡飞船所在处的圆轨道速度 7.678 km/s、逃逸速度约
	 * 10.86 km/s，12 给逃逸留约 10% 余量；低于 7.678 就飞不上圆轨道。
	 *
	 * 体素调用器的预测范围是按阻尼极速（1.2 km/s）配的，上限调得越高，高速时
	 * 地形越容易追不上 —— 改这个值要连 PredictionTime / 预测距离一起看。
	 */
	UPROPERTY(EditAnywhere, Category = "Spaceship|Thrust",
		meta = (DisplayName = "速度上限（km/s，世界）", ClampMin = "0"))
	float MaxSpeedKmPerSecond = 12.0f;

	/**
	 * Shift 加速时的速度上限（**世界 km/s**）。0 = 不限。
	 *
	 * 与 MaxSpeedKmPerSecond 相互独立：按住 Shift 时只认本值，松开后只认前者。
	 * 衰减规则完全相同 —— f = 1 − (v/vmax)²，只削沿速度方向的正向推力，横向推力
	 * 和 S 制动不打折；大气阻力同样不受上限影响。
	 *
	 * 默认 24（W 上限的 2 倍）：巡航关着时 Shift 的阻尼极速只有 3.6 km/s，本值
	 * 不起作用；巡航开着（阻尼关掉）时才靠它让 Shift 比 W 飞得更快。
	 *
	 * 注意体素调用器的预测范围按阻尼极速配，本值调高后高速时地形更追不上。
	 */
	UPROPERTY(EditAnywhere, Category = "Spaceship|Thrust",
		meta = (DisplayName = "加速速度上限（km/s，世界）", ClampMin = "0"))
	float BoostMaxSpeedKmPerSecond = 24.0f;

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
	 * 制动按 S：沿速度反方向把速度削到零，不倒车（见 GetThrustAccelerationWorldCmPerS2）。
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
	 * 关掉之后自动回正不再跑，但 PlanetActor / PlanetReevaluateInterval 那套
	 * 参考行星机制并未空转：升降推力的"上"也取自这颗行星（见
	 * GetThrustAccelerationWorldCmPerS2）。代价是每 PlanetReevaluateInterval
	 * 秒一次遍历几颗 Actor，可忽略；把本属性调回正数即可恢复自动回正。
	 *
	 * 代价：贴着行星飞大圆时曲面往下走，机身不会自己跟，需要玩家压杆维持高度。
	 */
	UPROPERTY(EditAnywhere, Category = "Spaceship|Handling")
	float AutoLevelSpeed = 0.f;

	/**
	 * 参考行星（控制飞船的那颗）。BeginPlay 时自动查找最近的一颗；找不到（纯太空
	 * 关卡）时自动回正退回世界 Z、相对速度退化为绝对速度。软依赖：缺了它飞船照常飞。
	 *
	 * 它同时喂给：自动回正（AutoLevelSpeed > 0 时）、升降推力方向、大气密度/阻力，
	 * 以及子系统每帧写回的参考速度（SetReferencePlanetVelocity）。
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

	/**
	 * 体素调用器的高速门控：速度高于此值（世界 km/s）时，只关掉细气泡的
	 * **碰撞**（bUseForCollisions = false），三档 LOD 全部保持开启。
	 *
	 * 为什么只关碰撞：两条路径的代价完全不同 ——
	 *   - 碰撞走 ShouldSubdivideByOthers → IsInvokerInRange，强制 Height 0
	 *     （32 m 块，32³ 采样），盒子扫过的每一块都是最细的；
	 *   - LOD 只细到 LODToSet（细气泡是 LOD1，64 m 块），块数少 8 倍。
	 * 13 km/s 时（每 0.1 秒扫过 1.3 km），±30 m 碰撞盒 ≈ 1.4e2 块 / 4.7e6
	 * 采样；三档 LOD 合计 ≈ 5e6 采样，量级相同。关掉 LOD 会让高速下身边
	 * 只剩行星球心调用器的轨道 LOD（本项目约 LOD 8），星球变糊 —— 2026-10-21
	 * 实机反馈，所以 LOD 不能关。
	 *
	 * 默认 0.6：低于阻尼极速（1.2 km/s）的一半，只有落地/悬停才需要最细碰撞。
	 * 与 VoxelInvokerResumeSpeedKmPerSecond 构成回差，避免在阈值上反复重建。
	 * 0 = 关闭门控（碰撞始终开着）。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spaceship|Voxel",
		meta = (DisplayName = "关碰撞 invoker 速度阈值（km/s，世界）", ClampMin = "0"))
	float VoxelInvokerSuspendSpeedKmPerSecond = 0.6f;

	/**
	 * 速度低于此值（世界 km/s）时重新打开细气泡的碰撞。必须小于上面的阈值，
	 * 两者之差就是回差。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spaceship|Voxel",
		meta = (DisplayName = "开碰撞 invoker 速度阈值（km/s，世界）", ClampMin = "0"))
	float VoxelInvokerResumeSpeedKmPerSecond = 0.2f;

	/**
	 * 两次启停之间的最小间隔（秒）。默认 0.5。
	 *
	 * 切换 bUseForCollisions 会让插件在下一次 LOD 更新（≤ MinDelayBetweenLODUpdates，
	 * 默认 0.1 秒）重建整棵八叉树；在阈值附近抖动时反复重建会把省下来的开销
	 * 加倍还回去。回差之外再加一道时间闸。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spaceship|Voxel",
		meta = (ClampMin = "0"))
	float VoxelInvokerToggleCooldownSeconds = 0.5f;

	/** 判定“已落地”的速度阈值（厘米/秒）。 */
	UPROPERTY(EditAnywhere, Category = "Spaceship|Landing")
	float LandingSpeedThreshold = 200.f;

	/**
	 * 落地判定的**允许间距**（厘米）：船体沿重力方向的最低点到地面的距离小于它，
	 * 才判为已落地。它**不再是检测球半径** —— 球半径由 GetLandingQueryRadiusCm()
	 * 按船体深度现算。
	 *
	 * 检测形状是球（球心在机身中心，与机身横滚无关）。曾经是一条沿机身"下"方的
	 * 射线：自动回正关掉后机身可以停在任何横滚角，射线会指偏、永远打不到地面，
	 * 人就被卡在船里下不来（IsLanded 恒为假，按 E 无效）。换成全向球后没有这个问题。
	 *
	 * 为什么半径不能是常数：求解器的接触形状是 Hull 凸包探针，飞船是**船底着地**，
	 * 机身中心天然比船底高出一整个船体深度（本资源约 1000 cm）。固定 600 cm 的球
	 * 够不到地面，IsLanded() 恒为假 —— 落地后鼠标仍在转飞船、按 E 也下不了船。
	 * 半径改成「船体深度 + 本值」后，本值恢复它字面上的含义：船底离地多远算落地。
	 *
	 * 取舍：球是全向的，贴着悬崖侧面慢速悬停也会判为已落地。本判定唯一的用途是
	 * 「能不能按 E 下船」和「落地后鼠标只转相机」，从几百厘米高处落到地面可以接受。
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

	/**
	 * 只开关细气泡的碰撞通道（bUseForCollisions），LOD 通道保持原样。
	 *
	 * 插件会在下一次 LOD 更新时发现该标志变化并重建八叉树，不需要手动
	 * RefreshAllVoxelInvokers。
	 */
	void SetVoxelInvokerCollisionEnabled(bool bEnabled);

	/** 三档调用器一起开关 —— 只留高档近处会糊，只留低档就没有碰撞气泡。 */
	void ApplyViewpointInvokerState();

	/** 本 Pawn 是否当前视点。初值 false —— OnRegister 会自动启用，BeginPlay 要按它关掉。 */
	bool bIsViewpoint = false;

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

	/**
	 * 参考行星中心速度（厘米/秒，世界坐标）。由子系统每帧写入，本类只读。
	 * 大气随行星平移（行星不自转），阻力与相对速度都用它当参考系。
	 */
	FVector ReferencePlanetVelocity = FVector::ZeroVector;

	/** 距离下次重新评估最近行星还剩多少秒。 */
	float PlanetReevaluateTimer = 0.f;

	/**
	 * 细气泡的碰撞通道当前是否因为高速而被关掉（LOD 三档始终开着）。
	 *
	 * 记录的是「我们主动关的」，用来把高速门控做成对称的。
	 */
	bool bVoxelInvokerCollisionSuspended = false;

	/** 距下次允许切换调用器还剩多少秒（回差之外的时间闸）。 */
	float VoxelInvokerToggleTimer = 0.f;

	/**
	 * 上一帧采样，用来量 **行星参考系下** 的速度。
	 *
	 * 门控必须按相对速度：行星随求解器每秒走几十公里，船相对行星静止时
	 * GetVelocity() 的绝对值并不小。这里用两个 Actor 的逐帧位移相减，
	 * 免得让本模块反向依赖 StellarEraCosmos 去查求解器（会成环）。
	 */
	FVector LastVoxelInvokerShipLocation = FVector::ZeroVector;
	FVector LastVoxelInvokerPlanetLocation = FVector::ZeroVector;
	TObjectPtr<AVoxelPlanetActor> LastVoxelInvokerPlanet;
	bool bHasVoxelInvokerSpeedSample = false;

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

	/**
	 * SpringArm 的基准相对朝向，BeginPlay 时从组件上抄下来。
	 * 自由视角的偏移叠加在它上面，避免冲掉蓝图里调好的摇臂朝向。
	 */
	FQuat SpringArmBaseRotation = FQuat::Identity;

	/**
	 * 落地自由视角是否激活。每帧 Tick 开头由 ShouldUseFreeLook 重算。
	 *
	 * 激活时飞船整体冻结：Look() 不再给角速度，Tick 跳过旋转与自动回正，
	 * 鼠标改去累加 FreeLookYaw / FreeLookPitch。
	 */
	bool bFreeLookActive = false;

	/** 自由视角的偏航偏移（度，相对船尾正后方）。退出后平滑回 0。 */
	float FreeLookYaw = 0.f;

	/** 自由视角的俯仰偏移（度）。退出后平滑回 0。 */
	float FreeLookPitch = 0.f;

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
