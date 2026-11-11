// Copyright Voxel Plugin SAS. All Rights Reserved.
//
// 天体轨迹渲染：预测轨迹（浅）、拖尾（深）、速度/引力矢量。只读位置、不决定位置，
// 物理数据从兄弟 UCosmosOrbitComponent 拿。

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
 * 把一颗天体的轨迹画出来。挂在有 UCosmosOrbitComponent 的 Actor 上；没有 Orbit 时
 * 拿不到物理量，只画一条空预测线。
 *
 * 折线、拖尾、箭头共用一个 ULineBatchComponent，LifeTime = 0 永不过期，每帧 Flush 后重画。
 * 行批按固定名复用，否则蓝图重编译会留下带旧线的行批。tick group 是 TG_PostUpdateWork：
 * 必须在子系统写回位置之后，否则拖尾慢一帧。
 */
UCLASS(ClassGroup = (Cosmos), meta = (BlueprintSpawnableComponent))
class STELLARERACOSMOS_API UCosmosTrajectoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCosmosTrajectoryComponent();

	/**
	 * 颜色朝白色插值 Amount（0 = 原色，1 = 纯白），alpha 不动。星图选中高亮用它 ——
	 * **只改颜色不加粗**：非零线宽会切到世界单位四边形渲染路径。
	 */
	static FColor BrightenTowardWhite(const FColor& In, float Amount);

	/**
	 * 希尔球半径 r_H = a · (m/3M)^(1/3)，长度单位同 DistanceCm。是零速面（泪滴形）的球形近似。
	 * 无效输入返回 false 并置 0。纯函数。
	 */
	static bool ComputeHillRadiusCm(double BodyMass, double PrimaryMass,
		double DistanceCm, double& OutRadiusCm);

	/**
	 * 瞬时引力主导边界：m_i/|P-r_i|² = m_j/|P-r_j|²，空间上是 Apollonius 球（等质量退化
	 * 为过中点的中垂面，OutRadiusCm == 0）。无效输入返回 false 并清零输出。
	 */
	static bool ComputeDominanceBoundary(const FVector3d& PositionA, double MassA,
		const FVector3d& PositionB, double MassB,
		FVector3d& OutCenter, double& OutRadiusCm, FVector3d& OutNormal);

	/** 点 Point 处 i、j 是不是拉得最狠的两颗（并列算通过）。「两人都不输给第三名」等价于「两人就是前两名」。 */
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
	 * 是否在编辑器视口（不 Play）也画。默认关。编辑器里子系统不 Tick、天体没注册，
	 * 走离线重建：用摆放位置 + 初速 + 质量临时建求解器副本。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory")
	bool bShowInEditor = false;

	/** 是否画预测折线（浅色那条）。默认关：每帧整段重积分，星图里多颗天体时很卡。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory")
	bool bDrawPrediction = false;

	/** 是否画拖尾（深色那条）。默认开；编辑器态没有历史，此开关被忽略。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory")
	bool bDrawTrail = true;

	/** 是否画速度矢量（跟着天体走的那根箭头）。默认开。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory")
	bool bDrawVelocityArrow = true;

	/** 是否画引力合力矢量（指向被拉向方向的箭头）。默认开。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory")
	bool bDrawGravityArrow = true;

	/** 是否画希尔球经纬网格。只给自身产生引力且有更重主星的天体画 —— 中心星与飞船被跳过。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory")
	bool bDrawHillSphere = true;

	/**
	 * 是否画瞬时引力主导剖分（任意天体数/质量比都成立的「此刻谁拉得最狠」边界）。
	 * 与希尔球回答不同问题：三星等质量系统里剖分仍有定义，希尔球没有。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory")
	bool bDrawDominancePartition = false;

	/** 是否在星图模式之外也画轨迹。默认关（只在星图里显示）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory")
	bool bShowOutsideStarMap = false;

	//////////////////////////////////////////////////////////////////////////
	// 引力合力箭头（实体：圆柱杆 + 圆锥头）
	//
	// 与线箭头是两回事：给操控中的飞船看的常驻指示物，两个 UStaticMeshComponent 拼成、
	// 走 M_PlanetGhost（Unlit + 半透明）。与 bShowOutsideStarMap 解耦。

	/**
	 * 操控飞船时是否允许显示引力合力箭头。只是总开关：还要求调试视角开着、
	 * 玩家正本地操控这艘船、不在星图里。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory|NetForceArrow")
	bool bDrawNetForceArrow = true;

	/**
	 * 箭头总长（厘米）。固定长度、只表达方向，强弱交给颜色 —— 按大小缩放的话滑行时会缩成一点。
	 * 警告：尾端钉在 Actor 原点时整根埋在舰体里（Hull 包围盒 ±41.3 米，轴心即几何中心）。
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

	/** 起点沿机身 up 的偏移（厘米）。调大能把箭头抬到船体上方，代价是尾端露出空隙。 */
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
	 * 颜色映射下端：低于它恒为纯冷色（厘米/秒²）。与 HotAt 构成**对数**刻度 ——
	 * 引力合力跨 5 个数量级，线性刻度会全程同色。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory|NetForceArrow",
		meta = (ClampMin = "0.0001"))
	double NetForceArrowColdAtCmPerS2 = 0.1;

	/**
	 * 颜色映射上端：达到它恒为纯暖色（厘米/秒²）。默认 30000 ≈ 贴地量级（实测 27600）。
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

	/** 已走过的拖尾颜色。预测线默认关掉后它是星图里唯一的轨道线，要亮到黑底上一眼看见。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory")
	FColor TrailColor = FColor(80, 170, 255);

	/** 拖尾线宽（屏幕像素）。≤ 1 = 引擎 1 像素细线。按相机距离逐段换算成世界宽度，远近同粗。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory", meta = (ClampMin = "0.0"))
	float TrailThicknessPixels = 3.0f;

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
	 * 有限预测窗口（模拟年），0 = 自动。主导天体的密切轨道是椭圆时先尝试闭合轨道
	 * （优先于本值、不受 MaxAutoPredictionPeriods 限制）。闭合失败才退回：
	 * 0 = 1.05 × 该天体最短周期（算不出用 1.0 年），> 0 = 手填值。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory",
		meta = (ClampMin = "0.0", DisplayName = "预测窗口（年）"))
	double PredictionYears = 0.0;

	/**
	 * 有限预测窗口上限（自身最短周期的倍数），0 = 不设限，闭合轨道不读它。属性名里的
	 * Auto 是历史名，别改。必须钳：步长一旦超过自身动力学时标，画出来就是飞出去的直线。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory", meta = (ClampMin = "0.0"))
	double MaxAutoPredictionPeriods = 0.5;

	/** 预测线帧间平滑的时间常数（真实秒）。0 = 不平滑，每帧直接换成新算的线。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory",
		meta = (ClampMin = "0.0", DisplayName = "预测线平滑时间（秒）"))
	float PredictionSmoothingSeconds = 0.2f;

	/** 预测线与拖尾的空间平滑遍数（[1,2,1]/4 核）。0 = 原样画采样点。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory",
		meta = (ClampMin = "0", ClampMax = "32", DisplayName = "曲线平滑遍数"))
	int32 CurveSmoothIterations = 4;

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
	 * 把预测窗口钳到「MaxPeriods × 自身最短周期」，运行期与编辑器预览共用。
	 * RequestedYears ≤ 0 或非有限原样返回；MaxPeriods ≤ 0 或周期 ≤ 0 时无从钳起。
	 */
	static double ClampPredictionHorizonYears(
		double RequestedYears, double OwnPeriodYears, double MaxPeriods);

	/**
	 * 按加速度大小在冷暖之间**对数**插值：α = (ln|a|−ln ColdAt)/(ln HotAt−ln ColdAt)。
	 * 量级跨 5 个数量级，线性会让箭头处处同色。参数非法时返回冷色，绝不算出 NaN。
	 */
	static FLinearColor NetForceArrowColor(double MagnitudeCmPerS2,
		double ColdAtCmPerS2, double HotAtCmPerS2,
		const FLinearColor& Cold, const FLinearColor& Hot);

	/**
	 * 算出「圆柱杆 + 圆锥头」的变换。基础形状 100 cm 高、底半径 50 cm、沿 +Z，缩放为
	 * 「半径/50」「长度/100」，各以中点定位。输入非法返回 false 并置单位变换。
	 */
	static bool ComputeNetForceArrowTransforms(
		const FVector& Anchor, const FVector& Direction, double TotalLengthCm,
		double ShaftRadiusCm, double HeadRadiusCm, double HeadFraction,
		FTransform& OutShaft, FTransform& OutHead);

	/** 实体合力箭头该不该显示：组件总开关、本地操控的飞船、不在星图、调试视角开着，缺一不可。 */
	static bool ShouldShowNetForceArrow(bool bDrawArrow, bool bControlledShip,
		bool bInStarMap, bool bDebugViewMode);

private:
	/**
	 * 有限预测窗口（年），闭合成功时不会走到这里。回退顺序：手填 → 1.05 × 估算周期
	 * （解析一次并缓存）→ 钳到 MaxAutoPredictionPeriods × 自身最短周期 → 常量。
	 */
	double ResolvePredictionHorizonYears(const UCosmosSimulationSubsystem& Subsystem) const;

	/** 把一段折线追加进 Lines。 */
	void AppendPolyline(TArrayView<const FVector> Points, const FLinearColor& Color, float ThicknessPixels = 0.0f);

	/** 速度箭头（运行期，读求解器当前速度）。 */
	void AppendVelocityArrow(const UCosmosSimulationSubsystem& Subsystem, const FLinearColor& Color);

	/** 引力箭头（运行期，读求解器当前纯引力）。 */
	void AppendGravityArrow(const UCosmosSimulationSubsystem& Subsystem, const FLinearColor& Color);

	/**
	 * 更新实体合力箭头的位置、朝向与颜色。数据源是求解器的**纯引力**，不含推力 ——
	 * 推力大六个数量级，混进去就只剩机头方向。
	 */
	void UpdateNetForceArrow(const UCosmosSimulationSubsystem& Subsystem);

	/** 惰性创建两个 UStaticMeshComponent（Outer 是飞船 Actor）。false = 网格或材质加载失败。 */
	bool EnsureNetForceArrowMeshes();

	/** 显示/隐藏两个箭头部件。状态没变时不碰组件，避免每帧触发渲染状态刷新。 */
	void SetNetForceArrowVisible(bool bVisible);

	/** 找对本天体引力最强的另一颗（max M/d²），算希尔半径。不产生引力的天体返回 false。 */
	bool ResolveHillSphereRadiusCm(double& OutRadiusCm);

	/** 以 Center 为球心、RadiusCm 为半径，把经纬网格追加进 Lines。 */
	void AppendHillSphere(const FVector& Center, double RadiusCm, const FLinearColor& Color);

	/**
	 * 这一对的剖分是否由本组件画：双方都开时较重者画、等重按 Actor 名去重；
	 * 对方没开时本组件负责（避免开了开关却什么都画不出）。
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

	/** 把已经画上去的线清一次（bDrewLastFrame 为真时才真清）。 */
	void ClearDrawnLines();

	/**
	 * 运行期预测：闭环（带滞回）或有限窗口，经空间平滑 + 帧间指数平滑后写进 PredictedWorld。
	 * 闭环在主导天体相对系里按角度重采样，开放线在自身相对系里按时间对齐，两帧才逐点对应。
	 */
	void UpdateFilteredPrediction(const UCosmosSimulationSubsystem& Subsystem, int32 SimulatorIndex,
		double HorizonYears, const FVector& SelfLocation, float DeltaTime);

	/** 丢掉帧间平滑状态（关预测 / 换天体时），下次直接用新算的线。 */
	void ResetPredictionFilter();

	/** 编辑器态（不 Play）的绘制：预测折线 + 速度/引力箭头。离线重建求解器，无拖尾。 */
	void DrawInEditor();

	/** 找到兄弟 UCosmosOrbitComponent（缓存）。找不到返回 nullptr。 */
	UCosmosOrbitComponent* GetOrbitComponent();

	/** 确保行批存在且已注册。按固定名字查找/复用，避免重注册时留下带旧线的孤立行批。 */
	ULineBatchComponent* EnsureLineBatch();

	/**
	 * 清掉本 Actor 上遗留的旧行批（老版本按默认名、outer = Actor）。只在「本 Actor
	 * 只有一个轨迹组件」时执行 —— 多个组件无法凭名字判断归属。
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

	/** 帧间平滑状态：闭环存主导天体相对坐标，开放线存自身相对坐标。 */
	TArray<FVector3d> RawPrediction;
	TArray<FVector3d> ResampledPrediction;
	TArray<FVector3d> FilteredClosed;
	FVector3d FilteredClosedNormal = FVector3d::ZeroVector;
	TArray<FVector3d> FilteredOpen;
	TArray<FVector> TrailDrawPoints;

	/** 上一帧是否画成闭环。闭合判定的进出阈值不同，免得在整圈与半截之间逐帧来回跳。 */
	bool bClosedOrbitLatched = false;

	/** 上一帧画过没有 —— 关掉时只清一次。 */
	bool bDrewLastFrame = false;

	/** 合力箭头的杆（圆柱）与头（圆锥）。惰性创建，Outer 是飞船 Actor。 */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> NetForceArrowShaft;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> NetForceArrowHead;

	/** 两个部件共用的 MID。Outer 给瞬态包而不是组件：否则会拖脏资产包，切图时还可能被当成关卡内容收走。 */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> NetForceArrowMID;

	/** 箭头上一次是显示还是隐藏。 */
	bool bNetForceArrowVisible = false;
};
