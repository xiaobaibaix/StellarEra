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
 * 组件本身不做任何事：它只存参数。注册、推进、写回位置全在
 * UCosmosSimulationSubsystem 里 —— N 体是耦合系统（A 受 B 的力，B 也受 A 的力），
 * 必须有唯一的编排者，各天体分别 tick 会读到彼此的中间状态。
 *
 * ── 初始状态从哪来 ────────────────────────────────────────────
 * **组件不生成初始状态，只读取它。** 一个天体的初态就两项：
 *     位置 = Actor 在关卡里摆的位置（GetActorLocation，组件不碰它）
 *     速度 = InitialVelocity
 * 想在哪儿放星球就在视口里拖到哪儿，想让它飞多快就填 InitialVelocity。
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
	 * 质量，单位 M☉（太阳质量）。默认值是地球质量。
	 *
	 * 行星之间的引力相对恒星可以忽略，但质量仍是决定「谁绕谁」的唯一依据 ——
	 * 中心天体必须是这里面最大的那个，否则行星会直接飞出去。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Body")
	double MassSolar = 3.0034893e-6;

	/**
	 * 初速度，游戏单位 AU/年。**天体初速度的唯一来源。**
	 *
	 * 留零即静止，天体只受引力作用。方向与大小都照填，没有任何推导或修正。
	 *
	 * 单位换算：1 AU/年 = 1e6 厘米 / 1 模拟年；TimeScale = 0.00333 时约 3330
	 * 厘米/秒（33 米/秒）。所以地球的 2π ≈ 6.283 AU/年 在世界里是 209 米/秒。
	 * 这个换算同时受 CentimetersPerAu 与 TimeScale 影响 —— 改标定值会改观感速度，
	 * 但不会改变轨道形状（求解器内部全程是 AU / AU每年 / M☉ 的双精度）。
	 *
	 * 圆轨道速度由开普勒第三定律给：v = √(GM/r) = 2π√(M/r)，M 是中心天体质量
	 * （M☉）、r 是到中心天体的距离（AU）。例如距 1 M☉ 0.718 AU 的圆轨道是
	 * 2π/√0.718 ≈ 7.41 AU/年，方向与「天体 → 中心天体」的连线垂直。
	 *
	 * **注意**：速度矢量（bDrawVelocityArrow）画的是求解器里的**当前**速度，
	 * 不是这一项 —— 这一项只决定开局。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Body")
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
	 * 注意这个开关**不决定初始状态**：不论真假，初态都是「摆放位置 + InitialVelocity」。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Body")
	bool bSimulated = true;

	/** 求解器里的索引。运行时由子系统填写，不要手改。 */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Cosmos")
	int32 SimulatorIndex = INDEX_NONE;

	//////////////////////////////////////////////////////////////////////////
	// 轨迹显示
	//
	// 这些参数放在 Orbit 组件上而不是轨迹组件上：轨迹组件是运行时 NewObject
	// 出来的，编辑器里调不到它的属性。Orbit 组件每个天体上都已经有了，参数放
	// 这儿才能逐天体在详情面板里调（颜色有取色器），且零编辑器侧手工操作。

	/**
	 * 是否画这个天体的**预测折线**（浅色那条）。默认开。
	 *
	 * 关掉省下的是**整段预测重算** —— 那是开销的大头：每帧把整个系统复制一份，
	 * 再按预测窗口走 255 步。为假时连 PredictTrajectoryWorld 都不会被调用。
	 *
	 * 它与 bDrawTrail 完全独立，谁也不否决谁；两个都关才是「这条天体什么线都不画」。
	 *
	 * 注意默认它开着，但轨迹**默认仍然看不见** —— 可见性还要过
	 * bShowOutsideStarMap 或星图模式那一关。见下面那条属性。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory")
	bool bDrawPrediction = true;

	/**
	 * 是否画这个天体的**拖尾**（深色那条，真实走过的路径）。默认开。
	 *
	 * 它只门控「画」—— 拖尾采样**不**受它影响，一直在跑（拖尾是真实历史，
	 * 采样无条件进行，成本有界：保留点数 ≤ TrailSamples + 1 = 513）。
	 * 所以关掉再打开立刻就是完整拖尾，不用等它从零长起来。
	 *
	 * 与 bDrawPrediction 完全独立：想只看真实走过的路径、不要预测，就留它、关掉
	 * bDrawPrediction。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory")
	bool bDrawTrail = true;

	/**
	 * 是否在**星图模式之外**也画这个天体的轨迹。默认关。
	 *
	 * 默认关 = 轨迹只在星图模式里显示。星图模式本身（含按 M 进出的开关）见
	 * AStellarEraPlayerController::ToggleStarMap。
	 * 想在普通游戏画面里也看到就把它勾上 —— 这是逐天体开关，与 bDrawPrediction /
	 * bDrawTrail / bDrawVelocityArrow 都是「与」的关系。
	 *
	 * 放在 Orbit 组件上而不是轨迹组件上：轨迹组件是**哑渲染器**，它自己的头注释
	 * 写着「不存任何面向用户的参数，参数全从兄弟 UCosmosOrbitComponent 上读」。
	 * 这条是面向用户的，同理。
	 *
	 * ⚠ 它同样只门控「画」。拖尾采样在两种模式下都一直跑 —— 这正是「一进星图
	 * 就是完整拖尾、不用等它从零长起来」的前提。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory")
	bool bShowOutsideStarMap = false;

	/** 预测轨迹的颜色。浅色 —— 与深色的拖尾形成对比。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory")
	FColor PredictionColor = FColor(90, 170, 255);

	/** 已走过的拖尾颜色。深色。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory")
	FColor TrailColor = FColor(20, 50, 110);

	/** 速度矢量的颜色。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory")
	FColor VelocityArrowColor = FColor(255, 210, 80);

	/**
	 * 预测多长，单位模拟年。0 = 自动。
	 *
	 * 自动值 = 1.05 × 该天体参与的最短轨道周期 —— 比一圈多一点，于是画出来的
	 * 椭圆首尾相接、看起来是闭合的。算不出周期时回退到 1.0 年。
	 *
	 * **自动值只在第一次真的算出周期时解析一次，之后一直用那个值。** 每帧重算的话
	 * 窗口会跟着当前间距跑：周期 ∝ 间距^1.5，天体一分开窗口就变大、画出来的线跟着
	 * 变长，最近邻一换（min 那一项换人）还会跳一下。手填这一条不受缓存影响 ——
	 * 它每帧都重新判断，随时改了随时生效。
	 *
	 * 解析出的自动值还要再过一关：下面 MaxAutoPredictionPeriods 那个上限（**不进缓存**）。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory", meta = (ClampMin = "0.0"))
	double PredictionYears = 0.0;

	/**
	 * 自动预测窗口的上限，单位是**系统最短周期**的倍数。0 = 不设限。**只钳自动那条路。**
	 *
	 * 为什么需要它：自动值 = 1.05 × 该天体估算出的周期，而估算拿的是**当前间距**
	 * （周期 ∝ 间距^1.5）—— 天体一分开这个值就涨，且没有上界。窗口一长，预测就
	 * 不再可信，原因在预测本身是一次**粗积分**：
	 *
	 *     预测把整个系统复制一份，用固定步长 窗口 / (PredictionSamples − 1) = 窗口/255
	 *     走完窗口，而模拟用的是 P / StepsPerOrbit = P/20000。窗口取 P 时两者差 78 倍。
	 *
	 * 误差**不是**随窗口平滑增长的幂律 —— 不要拿幂律去估它。本关卡实测（离线复刻引擎
	 * 的积分器：同款 Velocity Verlet、同款步长、**当时的软化长度 1e-5**；到 t = 0.83
	 * 年的能量漂移 3.9e-07 与引擎自己的一致，参考解把步长再缩一半后位移变化 < 0.24 km）：
	 * 「窗口/P」对「画出的折线比真值长多少」——
	 *
	 *     0.17 → 0.0%    0.33 → 0.0%    0.50 → +1.4%
	 *     0.66 → +98%    0.83 → +341%   1.05 → −23%（已跳到别的支，不是收敛）
	 *     1.38 → +1012%
	 *
	 * ⚠ **这张表是在软化长度还是 1e-5 时测的** —— 那会儿本关卡会在 t ≈ 0.84 年炸掉，
	 * 而表里 0.83 / 1.05 / 1.38 三行已经贴着甚至越过了那个时刻。后来
	 * UCosmosSimulationSubsystem::SofteningLength 的默认值改成了 0.02，交会变软，
	 * **这张表没有在新默认值下重测**。所以 0.5 这个上限是「按旧系统定的」，
	 * 不是「按现在这个系统验证过的」。
	 *
	 * 拐点在 0.5 附近，再往上按几十倍跳 —— 三体有近距离交会，误差在那儿被**指数**
	 * 放大，不是按窗口的幂累积。窗口/P = 1.38 时画出的线是真值的 11 倍长，末端离
	 * 天体真正会到的地方 2700 km（关卡本身才 92 km）。那正是「一条又长又直、
	 * 每帧还不一样的线」。
	 *
	 * 所以默认 0.5：实测误差 ≤ 1.4%，画出的线长约 100 km，与关卡同量级。
	 * **代价是**：窗口只有半圈，画出的椭圆不再首尾相接 —— 那半圈本来也不可信。
	 *
	 * 手填的 PredictionYears 不受它管：那是人明确要的，钳它只会让人以为没生效。
	 * 上限也不进缓存 —— 与手填那条一样，运行期改了立刻生效。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory", meta = (ClampMin = "0.0"))
	double MaxAutoPredictionPeriods = 0.5;

	/**
	 * 拖尾回溯多长，单位模拟年。0 = 自动（= 预测窗口的 1/4）。
	 *
	 * 拖尾按**模拟时间**采样并裁剪，不按帧 —— 按帧的话覆盖时长会随帧率漂移。
	 * 这个窗口决定**覆盖时长**，点的密度另由标称采样间隔决定（论证见 spec §6）。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory", meta = (ClampMin = "0.0"))
	double TrailYears = 0.0;

	/**
	 * 是否画这个天体的**速度矢量**（跟着它走的那根箭头）。默认开。
	 *
	 * 锚在天体**当前**位置，方向与长度读求解器**当前**速度 —— 所以它跟着天体走、
	 * 随实际速度变长变短。**不是** InitialVelocity 的可视化：那个值在详情面板里
	 * 本来就能看，画出来只是重复信息；而当前速度没有别的地方能看到。
	 *
	 * 静止天体不画 —— 速度为零时没有矢量可画。这在关卡里是常态：UniverseMap
	 * 中间那颗初速就是 0。
	 *
	 * 速度暴涨时箭头会很长，**刻意不钳制**：近距离交会时那正是要看的信号。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory")
	bool bDrawVelocityArrow = true;

	/**
	 * 速度矢量的长度系数：每 1 AU/年 画多少厘米。
	 *
	 * 这**不是**物理速度的世界长度。把 AU/年 直接当 AU 长度按世界尺度画，
	 * 箭头会比轨道半径还长一个数量级；而按本项目的世界尺度（1 AU = CentimetersPerAu = 1e6 厘米）换算，1 AU/年 ≈ 0.032 厘米/秒，
	 * 即一秒钟才走 0.032 厘米 —— 又小到看不见。两个极端都不能用，所以箭头长度
	 * 是「速度大小 × 这个系数」的示意图，只保证相对长短正确。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory")
	double VelocityArrowScaleCm = 10000.0;

	//~ Begin UActorComponent Interface
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End UActorComponent Interface

	/** 创建并注册轨迹渲染组件。只调一次。 */
	void CreateTrajectoryComponent();
};
