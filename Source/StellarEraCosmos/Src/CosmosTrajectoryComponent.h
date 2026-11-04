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
class UStaticMeshComponent;
class UMaterialInstanceDynamic;

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
class STELLARERACOSMOS_API UCosmosTrajectoryComponent : public UActorComponent
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
	// 引力合力箭头（实体：圆柱杆 + 圆锥头）
	//
	// 与上面那些**线**画出来的箭头是两回事：这一根是给**操控中的飞船**看的
	// 常驻指示物，用两个 UStaticMeshComponent 拼出来，走 M_PlanetGhost（Unlit +
	// 半透明），背光面也看得见。它只在「本地玩家正操控这艘船、且不在星图里」
	// 时显示 —— 与 bShowOutsideStarMap 解耦，那个开关会把预测线和速度箭头
	// 一起拖进游戏画面，这里不要。

	/** 操控飞船时是否显示引力合力箭头。默认开。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory|NetForceArrow")
	bool bDrawNetForceArrow = true;

	/**
	 * 箭头总长（厘米）。**固定长度**，只表达方向。
	 *
	 * 不按合力大小缩放：本关卡的引力加速度都在 10~20 厘米/秒² 量级，深空与
	 * 贴地差不了几倍，但一旦按比例画，滑行时箭头就缩成一点，丢了「一直在」
	 * 的意义。强弱交给颜色。
	 *
	 * 注意尾端钉在 Actor 原点时，这 20 米整根埋在舰体里（船体包围盒
	 * ±27.5 × ±41.3 × ±10.0 米，而 Hull 的轴心就是船体几何中心），实测
	 * PIE 截图上橙色像素为 0。要沿合力方向戳出船壳，长度得超过 41.3 米。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory|NetForceArrow",
		meta = (ClampMin = "1.0"))
	double NetForceArrowLengthCm = 2000.0;

	/** 杆半径（厘米）。引擎圆柱底半径是 50 厘米，这里按实际半径填。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory|NetForceArrow",
		meta = (ClampMin = "0.1"))
	double NetForceArrowShaftRadiusCm = 40.0;

	/** 锥头底半径（厘米）。比杆粗一圈才看得出是箭头。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory|NetForceArrow",
		meta = (ClampMin = "0.1"))
	double NetForceArrowHeadRadiusCm = 100.0;

	/** 锥头占总长的比例。夹在 [0.05, 0.95]。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory|NetForceArrow",
		meta = (ClampMin = "0.05", ClampMax = "0.95"))
	double NetForceArrowHeadFraction = 0.3;

	/**
	 * 起点沿机身 up 的偏移（厘米）。0 = 直接从飞船 Actor 原点长出来。
	 *
	 * 默认 0：尾端钉在 Actor 原点，箭头看起来是从船体里伸出来的。调大会把
	 * 整根箭头抬到船体上方，代价是尾端与船之间露出一段悬空的空隙。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory|NetForceArrow",
		meta = (ClampMin = "0.0"))
	double NetForceArrowOffsetCm = 0.0;

	/** 合力最弱时的颜色（冷）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory|NetForceArrow")
	FLinearColor NetForceArrowColdColor = FLinearColor(0.15f, 0.55f, 1.0f);

	/** 合力最强时的颜色（暖）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory|NetForceArrow")
	FLinearColor NetForceArrowHotColor = FLinearColor(1.0f, 0.3f, 0.1f);

	/**
	 * 颜色映射的下端：低于它恒为纯冷色（厘米/秒²）。
	 *
	 * 与 HotAt 一起构成**对数**刻度。必须是对数：本关卡里引力合力的实际跨度
	 * 从深空的个位数厘米/秒² 到贴近行星时的 **27600**（HUD 那一栏 0.28 km/s²
	 * 就是同一个数），线性刻度要么全程饱和、要么深空永远是冷色。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory|NetForceArrow",
		meta = (ClampMin = "0.0001"))
	double NetForceArrowColdAtCmPerS2 = 0.1;

	/**
	 * 颜色映射的上端：达到它（及以上）恒为纯暖色（厘米/秒²）。
	 *
	 * 默认 30000 ≈ 本关卡贴近行星时的量级（实测 27600），所以「被行星拽住」
	 * 就是满暖色；行星际空间里会明显偏冷。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory|NetForceArrow",
		meta = (ClampMin = "0.001"))
	double NetForceArrowHotAtCmPerS2 = 30000.0;

	/** 箭头的不透明度。M_PlanetGhost 是半透明材质，1 = 实心。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory|NetForceArrow",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float NetForceArrowOpacity = 1.0f;

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
	 * **有限**预测窗口，单位模拟年。0 = 自动。
	 *
	 * 只要相对主导天体是椭圆（ε < 0 且 e < 0.99），就先尝试**闭合轨道**：在主导
	 * 天体参考系里一直积到相对位置转满一圈、回到起点附近，首尾相接，画成闭环。
	 * 闭合优先于本值 —— 闭环长度由实测闭合时刻决定，一圈就是一整圈，**不受**
	 * 本值与 MaxAutoPredictionPeriods 限制。
	 *
	 * 只有密切轨道非椭圆、偏心率过高、或转满一圈仍回不到起点附近时，才退回有限
	 * 窗口：0 = 1.05 × 该天体参与的最短周期（算不出时 1.0 年）；> 0 = 手填值。
	 * 有限回退受 MaxAutoPredictionPeriods 约束。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory",
		meta = (ClampMin = "0.0", DisplayName = "预测窗口（年）"))
	double PredictionYears = 0.0;

	/**
	 * **有限**预测窗口的上限，单位是**该天体自身最短轨道周期**的倍数。0 = 不设限。
	 *
	 * 闭合轨道那条路**不读它**：闭环长度由实测闭合时刻决定，一圈就是一整圈。
	 * 它约束的只有「闭合失败后的有限回退窗口」（手填值或自动估算值）。
	 * 属性名里的 Auto 是历史名 —— 序列化名保留不改。
	 *
	 * 为什么有限窗口要钳：预测副本按「窗口 / 采样数」定固定步长，窗口越长步长越粗；
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

	/**
	 * 引力合力箭头的颜色：按加速度大小在冷暖之间**对数**插值。
	 *
	 *     α = (ln|a| − ln ColdAt) / (ln HotAt − ln ColdAt)，夹在 [0, 1]
	 *
	 * 对数不是美化：本关卡的量级跨 5 个数量级（行星际 ~10，贴地 ~27600），
	 * 线性刻度会让箭头在哪里都一样红。
	 *
	 * |a| ≤ ColdAt → 冷色；≥ HotAt → 暖色。ColdAt ≤ 0、HotAt ≤ ColdAt，或任一
	 * 参数非有限时返回冷色 —— 宁可不表达强弱，也不要算出 NaN 染黑整根箭头。
	 * 纯函数，自动化测试直接调用。
	 */
	static FLinearColor NetForceArrowColor(double MagnitudeCmPerS2,
		double ColdAtCmPerS2, double HotAtCmPerS2,
		const FLinearColor& Cold, const FLinearColor& Hot);

	/**
	 * 把「一根圆柱杆 + 一个圆锥头」的变换算出来。
	 *
	 * 两个引擎基础形状都是 100 厘米高、底半径 50 厘米、沿 +Z 朝上，所以缩放是
	 * 「半径 / 50」「长度 / 100」，朝向用 MakeFromZ。杆从 Anchor 起、到总长的
	 * (1 − HeadFraction) 处结束；锥头接在后面，两者各自以中点定位。
	 *
	 * 输入非法（长度 / 半径非正、方向近零、非有限）返回 false 并把两个变换置为
	 * 单位变换。纯函数，自动化测试直接调用。
	 */
	static bool ComputeNetForceArrowTransforms(
		const FVector& Anchor, const FVector& Direction, double TotalLengthCm,
		double ShaftRadiusCm, double HeadRadiusCm, double HeadFraction,
		FTransform& OutShaft, FTransform& OutHead);

private:
	/**
	 * **有限**预测窗口（年）。闭合轨道成功时不会走到这里 —— 闭环长度由实测
	 * 闭合时刻决定，一圈就是一整圈。
	 *
	 * 有限回退的取值：
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

	/**
	 * 更新实体合力箭头（圆柱杆 + 圆锥头）的位置、朝向与颜色。
	 *
	 * 只在「本地玩家正操控这艘飞船、且不在星图里」时被调用；条件不满足时由
	 * 调用方隐藏。数据源是求解器里的**纯引力**（与星图那根橙箭头的同一个量），
	 * 不含推力 —— 推力比引力大六个数量级，混进去箭头就只剩机头方向。
	 */
	void UpdateNetForceArrow(const UCosmosSimulationSubsystem& Subsystem);

	/**
	 * 惰性创建两个 UStaticMeshComponent。Outer 是飞船 Actor，跟着船一起销毁；
	 * 只给飞船创建，行星永远不会走到这条路径。返回 false = 网格或材质加载失败。
	 */
	bool EnsureNetForceArrowMeshes();

	/** 显示/隐藏两个箭头部件。状态没变时不碰组件，避免每帧触发渲染状态刷新。 */
	void SetNetForceArrowVisible(bool bVisible);

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

	/** 合力箭头的杆（圆柱）与头（圆锥）。惰性创建，Outer 是飞船 Actor。 */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> NetForceArrowShaft;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> NetForceArrowHead;

	/**
	 * 两个部件共用的动态材质实例。
	 *
	 * Outer 给瞬态包而不是组件：指示物不该把资产包拖成脏包，也不该在切图时
	 * 被当成关卡内容收走。本组件持有强引用，生命周期由它兜住。
	 */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> NetForceArrowMID;

	/** 箭头上一次是显示还是隐藏。 */
	bool bNetForceArrowVisible = false;
};
