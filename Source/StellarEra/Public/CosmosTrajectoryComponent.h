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

	//////////////////////////////////////////////////////////////////////////
	// 预测窗口

	/**
	 * 预测多长，单位模拟年。0 = 自动。
	 * 自动值 = 1.05 × 该天体参与的最短轨道周期，再钳到
	 * MaxAutoPredictionPeriods × 系统最短周期；算不出时回退 1.0 年。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory",
		meta = (ClampMin = "0.0", DisplayName = "预测窗口（年）"))
	double PredictionYears = 0.0;

	/**
	 * 自动预测窗口的上限，单位是**系统最短周期**的倍数。0 = 不设限。只钳自动那条路。
	 * 拐点在 0.5 附近，再往上误差按几十倍跳（三体近距离交会指数放大），默认 0.5。
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

private:
	/**
	 * 实际的预测窗口（年）：
	 *     手填优先
	 *     → 1.05 × 估算周期（**只解析一次**）
	 *     → 再钳到「MaxAutoPredictionPeriods × 系统最短周期」
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
