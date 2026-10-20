// Copyright Voxel Plugin SAS. All Rights Reserved.
//
// 天体轨迹渲染：预测轨迹（浅）、已走过的拖尾（深）、速度矢量、引力矢量。
//
// 这是一个**可挂载组件**：想监控哪颗天体，就把这个组件挂到它的 Actor 上，
// 所有参数（颜色、开关、箭头比例、预测窗口）都在本组件自己的详情面板里调。
//
// 它不决定天体在哪 —— 位置由 UCosmosSimulationSubsystem 每帧写回 Actor，它只读。
// 物理数据（质量、初速、求解器下标）从兄弟 UCosmosOrbitComponent 上拿：本组件
// 负责「怎么画」，Orbit 组件负责「它是什么」。
//
// 运行期读求解器当前状态；编辑器态（不 Play）走离线重建（见 DrawInEditor）。

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/LineBatchComponent.h"
#include "CosmosTrajectoryComponent.generated.h"

class UCosmosOrbitComponent;
class AActor;
class UCosmosSimulationSubsystem;

/**
 * 把一颗天体的轨迹画出来。
 *
 * 挂在任意挂了 UCosmosOrbitComponent 的 Actor 上即可工作 —— 它会自己找到兄弟
 * Orbit 组件拿物理数据（质量、初速、求解器下标）。不挂 Orbit 组件的话，轨迹
 * 组件拿不到任何物理量，只画一条空的预测线（速度为 0、引力为 0）。
 *
 * 三样东西共用一个 ULineBatchComponent：预测折线、拖尾折线、速度/引力箭头。
 * 线用 LifeTime = 0 画上去（永不过期），所以每帧先 Flush 再整批重画。
 *
 * 行批的 outer 是 Actor（不是本组件），组件重建时不会跟着销毁。因此它按
 * 「组件名」固定命名、重注册时复用；OnUnregister 只 Flush 不销毁。否则每次
 * 蓝图重编译/组件重建都会多出一个带旧线的行批，表现为「改了速度，旧预测线
 * 还留在场景里」。
 *
 * tick group 是 TG_PostUpdateWork：必须排在 UCosmosSimulationSubsystem 把位置
 * 写回 Actor **之后**，否则拖尾采到的是上一帧的位置。实际上排错了也只差一帧
 * （约 4 米），肉眼看不出来，但没理由不排对。
 */
UCLASS(ClassGroup = (Cosmos), meta = (BlueprintSpawnableComponent))
class UCosmosTrajectoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCosmosTrajectoryComponent();

	/**
	 * 把颜色朝白色插值 Amount（0 = 原色，1 = 纯白）。alpha 不动。
	 *
	 * 星图里选中天体的高亮就是它 —— **只改颜色，不加粗**。加粗要传非零线宽，
	 * 而行批把「线宽 == 0」走成真正的 1 像素 GPU 线、非零走成世界单位的四边形
	 * （见 .cpp 里 LineThickness 的注释），等于把选中的那条线切到另一条渲染路径。
	 * 那条路径只能人眼验，不值得为一个高亮引入一个只能靠肉眼发现的失败面。
	 */
	static FColor BrightenTowardWhite(const FColor& In, float Amount);

	/**
	 * 希尔球半径：r_H = a · (m / 3M)^(1/3)，长度单位与 DistanceCm 相同。
	 *
	 * 「引力主导范围」的严格解是过 L1 的零速面（泪滴形，朝主星一侧收成尖点）；
	 * 希尔球是它的球形近似，m ≪ M 时两者只差 (m/3M)^(1/3) 的量级。
	 *
	 * 无效输入（非有限、非正、次体不轻于主星）返回 false，并把 OutRadiusCm 置 0。
	 * 纯函数，自动化测试直接调用。
	 */
	static bool ComputeHillRadiusCm(double BodyMass, double PrimaryMass,
		double DistanceCm, double& OutRadiusCm);

	/**
	 * 两颗天体的瞬时引力主导边界：m_i/|P-r_i|² = m_j/|P-r_j|²，
	 * 即 |P-r_i|/|P-r_j| = sqrt(m_i/m_j)。
	 *
	 * 该轨迹是空间的 Apollonius 球：质量不等时是球（OutNormal 为轴方向），
	 * 等质量时退化为过中点的中垂面（OutRadiusCm == 0）。
	 * 不假设 m_i ≪ m_j，也不假设天体共线。
	 *
	 * 无效输入（非有限、非正质量、两点重合）返回 false，输出清零。
	 * 纯函数，自动化测试直接调用。
	 */
	static bool ComputeDominanceBoundary(const FVector3d& PositionA, double MassA,
		const FVector3d& PositionB, double MassB,
		FVector3d& OutCenter, double& OutRadiusCm, FVector3d& OutNormal);

	/**
	 * 点 Point 处，Positions/Masses 里的 i、j 是不是**拉得最狠的两颗**（并列算通过）。
	 *
	 * 只和 PairPull = max(g_i, g_j) 比即可：max(g_i, g_j) = PairPull 是恒等式，
	 * 所以「两人都不输给第三名」等价于「两人就是前两名」。
	 * 允许 1e-9 相对误差，避免采样点落在边界上时抖动。
	 */
	static bool ArePairDominantAt(const FVector3d& Point,
		const TArray<FVector3d>& Positions, const TArray<double>& Masses,
		int32 IndexA, int32 IndexB);

	//~ Begin UActorComponent Interface
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	//~ End UActorComponent Interface

	//////////////////////////////////////////////////////////////////////////
	// 显示开关（逐天体可调，都在本组件详情面板）

	/**
	 * 是否在**编辑器视口**（不 Play）也画这个天体的轨迹。默认关。
	 *
	 * 编辑器里子系统不 Tick、天体也没注册进求解器，所以这条路走的是**离线重建**：
	 * 遍历关卡里所有 Orbit 组件，用「摆放位置 + InitialVelocityKmPerSecond + MassEarth」临时
	 * 建一个求解器副本，拿它做预测、速度箭头与引力箭头。初值即真实初值。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory")
	bool bShowInEditor = false;

	/** 是否画预测折线（浅色那条）。默认开。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory")
	bool bDrawPrediction = true;

	/**
	 * 是否画拖尾（深色那条，真实走过的路径）。默认开。
	 * 拖尾只在运行期有（编辑器态没有历史），编辑器态此开关被忽略。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory")
	bool bDrawTrail = true;

	/** 是否画速度矢量（跟着天体走的那根箭头）。默认开。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory")
	bool bDrawVelocityArrow = true;

	/** 是否画引力合力矢量（指向被拉向方向的箭头）。默认开。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory")
	bool bDrawGravityArrow = true;

	/**
	 * 是否画希尔球（引力主导范围）的经纬网格。默认开。
	 *
	 * 球心在本天体，半径取「对它引力最强的另一颗」算出的希尔半径。只给有资格的
	 * 天体画：自身产生引力，且存在比它更重的主星。中心星与飞船都会被跳过。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory")
	bool bDrawHillSphere = true;

	/**
	 * 是否画**瞬时引力主导剖分**：任意天体数、任意质量比都成立的
	 * 「此刻谁的引力最强」边界。默认关。
	 *
	 * 与希尔球并存、回答不同问题：希尔球回答小质量比下的归属范围，
	 * 剖分回答这一瞬间的拉力归属。三星等质量系统里后者仍有定义，前者没有。
	 * 无界区域按 DominanceClipRadiusCm 裁剪。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory")
	bool bDrawDominancePartition = false;

	/**
	 * 是否在**星图模式之外**也画轨迹。默认关。
	 * 关 = 只在星图模式里显示；开 = 普通游戏画面里也画。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory")
	bool bShowOutsideStarMap = false;

	//////////////////////////////////////////////////////////////////////////
	// 颜色

	/** 预测轨迹的颜色。浅色 —— 与深色的拖尾形成对比。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory")
	FColor PredictionColor = FColor(90, 170, 255);

	/** 已走过的拖尾颜色。深色。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory")
	FColor TrailColor = FColor(20, 50, 110);

	/** 速度矢量的颜色。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory")
	FColor VelocityArrowColor = FColor(255, 210, 80);

	/** 引力矢量的颜色。默认取暖橙，与速度矢量的黄色区分开。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory")
	FColor GravityArrowColor = FColor(255, 120, 60);

	/** 希尔球网格的颜色。默认偏青，与轨迹蓝、箭头黄/橙区分开。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory")
	FColor HillSphereColor = FColor(80, 210, 200);

	/** 瞬时引力主导剖分的网格颜色。默认偏蓝紫，与希尔球青色区分。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory")
	FColor DominanceColor = FColor(150, 190, 255);

	//////////////////////////////////////////////////////////////////////////
	// 预测窗口

	/**
	 * 预测多长，单位模拟年。0 = 自动。
	 * 自动值 = 1.05 × 该天体参与的最短轨道周期；算不出时回退 1.0 年。
	 * 手填与自动最终都受 MaxAutoPredictionPeriods 的上限约束。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory",
		meta = (ClampMin = "0.0", DisplayName = "预测窗口（年）"))
	double PredictionYears = 0.0;

	/**
	 * 预测窗口上限，单位是**该天体自身最短轨道周期**的倍数。0 = 不设限。
	 * **手填与自动两条路都受它限制**（属性名里的 Auto 是历史名 —— 它曾经只钳自动
	 * 那条路，序列化名保留不改）。
	 *
	 * 为什么手填也要钳：预测副本按「窗口 / 采样数」定固定步长，窗口越长步长越粗；
	 * 快天体（飞船穿过中心星只要千分之几年）一旦步长超过它自己的动力学时标，
	 * 画出来的就不是轨道而是一条飞出去的直线。0.5 是误差开始按几十倍跳的拐点附近。
	 *
	 * 上限读**自身**周期而不是全系统最短：一艘悬在中心星上方的飞船会把全系统
	 * 最短周期压到 0.03 年，行星的椭圆就只剩一小段。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory", meta = (ClampMin = "0.0"))
	double MaxAutoPredictionPeriods = 0.5;

	/** 拖尾回溯多长，单位模拟年。0 = 自动（= 预测窗口的 1/4）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory",
		meta = (ClampMin = "0.0", DisplayName = "拖尾时长（年）"))
	double TrailYears = 0.0;

	//////////////////////////////////////////////////////////////////////////
	// 箭头比例

	/** 速度矢量的长度系数：每 1 AU/年 画多少厘米。示意比例，非物理世界长度。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory")
	double VelocityArrowScaleCm = 10000.0;

	/** 引力矢量的长度系数：每 1 AU/年² 画多少厘米。默认比速度系数小得多（引力量级远大于速度）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory")
	double GravityArrowScaleCm = 100.0;

	//////////////////////////////////////////////////////////////////////////
	// 希尔球

	/** 每条纬线/经线的折线段数。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory",
		meta = (ClampMin = "3", ClampMax = "128"))
	int32 HillSphereSegments = 48;

	/** 纬线圈数（不含赤道）。0 = 只画赤道。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory",
		meta = (ClampMin = "0", ClampMax = "16"))
	int32 HillSphereLatitudeRings = 4;

	/** 经线圈数（每条都是过两极的大圆）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory",
		meta = (ClampMin = "3", ClampMax = "32"))
	int32 HillSphereLongitudeRings = 12;

	/** 半径小于这个值（厘米）就不画 —— 算出来小到看不见的球没有意义。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory",
		meta = (ClampMin = "0.0"))
	double MinHillSphereRadiusCm = 100.0;

	//////////////////////////////////////////////////////////////////////////
	// 瞬时引力主导剖分

	/** 以本天体为圆心的裁剪半径（厘米）。最重天体的主导区无界，必须裁。默认 150 AU。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory",
		meta = (ClampMin = "1.0"))
	double DominanceClipRadiusCm = 1.5e8;

	/** 球面边界的经向段数。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory",
		meta = (ClampMin = "3", ClampMax = "128"))
	int32 DominanceSphereSegments = 32;

	/** 球面边界的纬向圈数（不含两极）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory",
		meta = (ClampMin = "1", ClampMax = "32"))
	int32 DominanceSphereRings = 8;

	/** 平面边界（等质量）每个方向的格数。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory",
		meta = (ClampMin = "2", ClampMax = "128"))
	int32 DominancePlaneSegments = 32;

	/** 球面半径小于这个值（厘米）就不画 —— 飞船这种微小主导区没有意义。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory",
		meta = (ClampMin = "0.0"))
	double MinDominanceRadiusCm = 100.0;

	/**
	 * 把请求的预测窗口钳到「MaxPeriods × 该天体自身最短周期」。
	 *
	 * 纯函数，运行期与编辑器预览共用同一份判定：
	 *   - RequestedYears ≤ 0 或非有限 → 原样返回（0 = 自动，交给调用方另算）；
	 *   - MaxPeriods ≤ 0 = 不设限；周期 ≤ 0 / 非有限（单天体、退化配对）也无从钳起。
	 */
	static double ClampPredictionHorizonYears(
		double RequestedYears, double OwnPeriodYears, double MaxPeriods);

private:
	/**
	 * 实际的预测窗口（年）：
	 *     手填优先
	 *     → 1.05 × 该天体自身的估算周期（**只解析一次**）
	 *     → 再钳到「MaxAutoPredictionPeriods × 该天体自身最短周期」
	 *     → 都算不出时回退常量。
	 *
	 * 手填与上限都不缓存，每帧重判，运行期改了立刻生效；只有「1.05 × 估算周期」
	 * 那一步的结果被缓存。
	 */
	double ResolvePredictionHorizonYears(const UCosmosSimulationSubsystem& Subsystem) const;

	/** 把一段折线追加进 Lines。 */
	void AppendPolyline(TArrayView<const FVector> Points, const FLinearColor& Color);

	/** 速度箭头（运行期，读求解器当前速度）。 */
	void AppendVelocityArrow(const UCosmosSimulationSubsystem& Subsystem, const FLinearColor& Color);

	/** 引力箭头（运行期，读求解器当前纯引力）。 */
	void AppendGravityArrow(const UCosmosSimulationSubsystem& Subsystem, const FLinearColor& Color);

	/** 找对本天体引力最强的另一颗（max M/d²），算希尔半径。不产生引力的天体返回 false。 */
	bool ResolveHillSphereRadiusCm(double& OutRadiusCm);

	/** 以 Center 为球心、RadiusCm 为半径，把经纬网格追加进 Lines。 */
	void AppendHillSphere(const FVector& Center, double RadiusCm, const FLinearColor& Color);

	/**
	 * 这一对本天体与 OtherActor 是否由本组件负责画。
	 *
	 * 规则：对方也有开着的剖分组件时，较重者画；等重按 Actor 名排序去重。
	 * 对方没有开着的组件时本组件负责 —— 避免「只有一个组件开了开关却什么都画不出」。
	 */
	bool ShouldDrawDominancePair(double SelfMassEarth, double OtherMassEarth,
		const AActor& SelfActor, const AActor& OtherActor) const;

	/** 画出本天体参与的、裁剪后的 Apollonius 边界（球面或中垂面）。 */
	void AppendDominancePartition(const FVector& Anchor, const FLinearColor& Color);

	/** 编辑器态速度箭头：读 InitialVelocityKmPerSecond，换算回 AU/年；比例系数仍按 AU/年 定义。 */
	void AppendEditorVelocityArrow(const FVector& Anchor, const FVector3d& Velocity,
		const FLinearColor& Color);

	/** 编辑器态引力箭头（读离线算出的纯引力）。 */
	void AppendEditorGravityArrow(const FVector& Anchor, const FVector3d& Gravity,
		const FLinearColor& Color);

	/**
	 * 把已经画上去的线清一次（bDrewLastFrame 为真时才真清）。
	 */
	void ClearDrawnLines();

	/**
	 * 编辑器态（不 Play）的绘制：预测折线 + 速度箭头 + 引力箭头。
	 * 离线重建求解器，不读子系统。无拖尾。
	 */
	void DrawInEditor();

	/** 找到兄弟 UCosmosOrbitComponent（缓存）。找不到返回 nullptr。 */
	UCosmosOrbitComponent* GetOrbitComponent();

	/**
	 * 确保行批存在且已注册。按固定名字查找/复用，避免组件重注册时不断新建，
	 * 留下带旧线的孤立行批。返回 nullptr 表示没有 Owner 或创建失败。
	 */
	ULineBatchComponent* EnsureLineBatch();

	/**
	 * 清掉本 Actor 上遗留的旧行批（老版本按默认名创建、outer = Actor 的那种）。
	 * 只在「本 Actor 只有一个轨迹组件」时执行 —— 多个轨迹组件各有自己的行批，
	 * 无法凭名字判断归属。
	 */
	void DestroyStaleLineBatches();

	//////////////////////////////////////////////////////////////////////////
	// 运行期状态

	/** 兄弟 Orbit 组件缓存。懒解析，找不到为 null。 */
	UPROPERTY(Transient)
	TObjectPtr<UCosmosOrbitComponent> Orbit;

	/** 画线用。自己造、自己注册。 */
	UPROPERTY(Transient)
	TObjectPtr<ULineBatchComponent> LineBatch;

	/** 拖尾的历史位置（世界坐标），下标 0 最旧、末尾最新。 */
	TArray<FVector> Trail;

	/** 与 Trail 一一对应的采样时刻（模拟年）。 */
	TArray<double> TrailTimes;

	/** 上次采样时的模拟时间（年）。 */
	double LastTrailSampleTime = 0.0;

	/** 「1.05 × 估算周期」的解析结果（年）及是否已解析。 */
	mutable double CachedAutoHorizonYears = 0.0;
	mutable bool bAutoHorizonResolved = false;

	/** 复用的中间数组，避免每帧重新分配。 */
	TArray<FVector> PredictedWorld;
	TArray<FBatchedLine> Lines;

	/** 上一帧画过没有 —— 关掉时只清一次。 */
	bool bDrewLastFrame = false;
};
