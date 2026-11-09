// Copyright Voxel Plugin SAS. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "VoxelViewpointInvokers.h"
#include "StarMapCameraPawn.generated.h"

class APlayerController;
class AVoxelPlanetActor;
class UCameraComponent;
class UCosmosSimulationSubsystem;
class UPlanetEditComponent;
class UPlanetMineralViewComponent;
class UStarMapCameraInputComponent;
class UVoxelSimpleInvokerComponent;

/** 屏幕空间候选：天体 Actor + 投影位置 + 盘面半径。用结构体而非平行数组，避免下标错位。 */
struct FStarMapScreenCandidate
{
	AActor* Actor = nullptr;
	FVector2D ScreenPosition = FVector2D::ZeroVector;

	/**
	 * 屏幕盘面半径（像素），0 = 未算出。拾取容差取 max(固定像素容差, 本值)；
	 * **必须是与视线垂直的半径**，用固定世界轴会被投影压扁。
	 */
	float ScreenRadiusPx = 0.f;
};

/** 相机拉远路径上的挡路球体（球心 + 地表半径）。纯数据，夹取逻辑可无头测。 */
struct FStarMapObstacle
{
	FVector Center = FVector::ZeroVector;
	double RadiusCm = 0.0;
};

struct FPlanetEditTarget;

/**
 * 星图相机：由行星的 Child Actor Component 提供，进场 EnterStarMap、退场 ExitStarMap。
 * **不 tick，位置在 CalcCamera 现算** —— 子系统在 TickObjects 写回位置后才调 CalcCamera，
 * 任何 tick 组都会慢一帧。Pawn 变换不影响相机，只让它在 PIE 里可见可点。
 */
UCLASS()
class STELLARERAPAWNS_API AStarMapCameraPawn : public APawn, public IVoxelViewpointInvokers
{
	GENERATED_BODY()

public:
	AStarMapCameraPawn();

	virtual void BeginPlay() override;

	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	//~ Begin IVoxelViewpointInvokers Interface
	virtual void SetViewpointInvokersActive(bool bActive) override;
	//~ End IVoxelViewpointInvokers Interface

	/** 星图模式的行星编辑组件，由蓝图子类 BP_StarMapCameraPawn 挂载；原生实例没有编辑功能。 */
	UPlanetEditComponent* GetPlanetEditComponent() const;

	// ── 矿物透视 ──

	/**
	 * H 键：进入 / 退出矿物透视，需选中带透视组件的体素行星。退出途径：再按 H、退出星图、
	 * 换 Pawn 或选中变化。@return 失败原因（成功或无操作时为空）。
	 */
	FString ToggleMineralView();

	/** 退出矿物透视（幂等）。状态住在相机上，换 Pawn 时由控制器命令相机执行。 */
	void ExitMineralViewIfActive();

	bool IsMineralViewActive() const { return ActiveMineralView != nullptr; }

	/** 当前激活的矿物透视组件；未激活时为空。控制器用它转发给星图 UI 画图例。 */
	UPlanetMineralViewComponent* GetActiveMineralView() const { return ActiveMineralView; }

	/** 数字键 1..9：切换第 Index 个元素的显示（下标从 0 起；未激活时空操作）。 */
	void ToggleMineralElement(int32 Index);

	/** 数字键 0：全部打开（未激活时空操作）。 */
	void ShowAllMineralElements();

	/** 焦点是行星时相机拉远的倍率（地表半径 × 本值）。控制器按 M 进场聚焦时用。 */
	double GetPlanetFocusRadiusScale() const { return PlanetFocusRadiusScale; }

	/** 拉远上限（到焦点球心，厘米）。 */
	double GetMaxOrbitDistanceCm() const { return MaxOrbitDistanceCm; }

	/** 绕行灵敏度倍率上限，见 OrbitLookScale。 */
	double GetMaxOrbitLookScale() const { return MaxOrbitLookScale; }

	/** 左键拾取天体的像素容差。相机输入组件与 HUD 悬停查询共用。 */
	float GetSelectionPixelRadius() const { return SelectionPixelRadius; }

	/** 进入星图模式时由控制器调用：把相机放到给定位置，并重置为「未选中」。 */
	void EnterFrom(const FVector& InCameraLocation, const FRotator& InCameraRotation);

	/**
	 * 入场锚点：行星地表之上 AltitudeCm 的世界位置。方向为 Center→Toward，重合时退回 +Z；
	 * 高度夹在 [0, 25% 半径]。SurfaceRadiusCm ≤ 0（几何不可信）返回 PlanetCenter。纯函数，可无头测。
	 */
	static FVector ComputeSurfaceAnchorLocation(
		const FVector& PlanetCenter,
		const FVector& TowardWorldLocation,
		double SurfaceRadiusCm,
		double AltitudeCm);

	/** 用自己挂载的行星算入场锚点；没挂在行星上时返回当前 Actor 位置。 */
	FVector ComputeEntryAnchorLocation(const FVector& TowardWorldLocation) const;

	/** 把相机摆回挂载原点：星图期间 Actor 跟着渲染相机走，退出时不搬回会让子 Actor 漂在最后看的位置。 */
	void ResetToAttachOrigin();

	// ── 进出星图的会话流程 ──

	/**
	 * 进入星图：记下 From 为退图返回对象、摆位、通知求解器、聚焦挂载行星，最后 `Possess(this)`。
	 * PC / From 为空或世界没有求解器时返回 false 且不改状态。
	 */
	bool EnterStarMap(APawn* From, APlayerController* PC);

	/** 退出星图：还原透视与编辑、通知求解器，混合结束后 Possess 回进场 Pawn 并搬回挂载原点；混合期间重复调用忽略。 */
	void ExitStarMap();

	/**
	 * 聚焦天体：沿当前视线平滑后退到距焦点 DistanceCm，让飞船连同附近轨道入画。平滑只为
	 * 观感（瞬移会让 LOD 一帧塌多档），只记起点与目标、插值在 CalcCamera。Body 为空无操作。
	 */
	void FocusOn(AActor* Body, double DistanceCm);

	/** 鼠标位移 → 绕焦点转视角（度）。转到角度按 OrbitLookScale 缩放：贴地慢、远处快。 */
	void OrbitLook(float DeltaYaw, float DeltaPitch);

	/**
	 * 右键：只改视线朝向、不动 OrbitRotation（相机位置），偏移由 ClampAimOffset 压回圆锥。
	 * 灵敏度**不乘** OrbitLookScale：转头是纯角度操作，套上「贴地慢」会让近处转不动。
	 */
	void AimLook(float DeltaYaw, float DeltaPitch);

	/**
	 * 把「基准朝向 + 偏移」按**夹角**压回圆锥，roll 恒为 0；逐轴夹会得到正方形边界。
	 * MaxOffsetDeg ≤ 0、偏移含 NaN、方向重合时返回 BaseRotation。纯函数，可无头测。
	 */
	static FRotator ClampAimOffset(
		const FRotator& BaseRotation,
		const FRotator& AimOffset,
		double MaxOffsetDeg);

	/**
	 * 绕行灵敏度倍率 = (离焦点表面高度 / max(焦点表面半径, 5 米)) 夹进 [MinScale, MaxScale]；
	 * 5 米兜底保飞船曲线，常量是为了纯函数可无头测。Distance ≤ 0 或半径 ≤ 0 返回 1。
	 */
	static double OrbitLookScale(
		double DistanceCm,
		double FocusSurfaceRadiusCm,
		double MinScale,
		double MaxScale);

	/** 滚轮 → 按倍率缩放相机到**地表的高度**。Value 每格 ±1，正值为拉近。 */
	void AddZoom(float Value);

	/** 当前选中的天体（每帧从子系统读，不自己存）。 */
	AActor* ResolveSelectedBody() const;

	/**
	 * 从屏幕坐标挑天体：返回最近且距离 ≤ max(PixelRadius, ScreenRadiusPx) 的候选，边界含。
	 * 等距取先出现的，结果依赖 Candidates 顺序（调用方必须给确定的顺序）。纯函数，可无头测。
	 */
	static AActor* PickBodyAtScreenPosition(
		TArrayView<const FStarMapScreenCandidate> Candidates,
		const FVector2D& ScreenPoint,
		float PixelRadius);

	// ── 天体 / 相机解析（纯查询，能无头测）──

	/** 天体和挂载物里有没有星图相机；没有的天体既不会被自动聚焦，也点不中。 */
	static bool HasStarMapCamera(const AActor* Body);

	/** 进图的参考行星（驾驶取飞船的、步行取角色的），只做类型解析，不做距离搜索。 */
	static AVoxelPlanetActor* ResolveReferencePlanet(const APawn* EnteringPawn);

	/** 找 Owner 的 CAC 子 Actor 里的第一台星图相机；CDO 上恒空 —— 机制如此，不是 bug。 */
	static AStarMapCameraPawn* FindChildStarMapCamera(const AActor* PlanetOwner);

	/**
	 * 找直接 Attach 到 Owner 的第一台星图相机（CAC 子 Actor 也会 Attach）。不递归，
	 * 避免飞船 / 采矿平台上的相机被误认成行星相机。
	 */
	static AStarMapCameraPawn* FindAttachedStarMapCamera(const AActor* PlanetOwner);

	/** 先认 CAC 再退回附加关系；都没有或深空解析不出行星时返回空，调用方**不进入星图**。 */
	static AStarMapCameraPawn* ResolvePlanetStarMapCamera(const APawn* EnteringPawn);

	/**
	 * 球体屏幕轮廓半径的放大系数 `1/cos α`（`sin α = R/d`）：可见轮廓由切线决定，
	 * d = 2R 处系数 1.1547。CameraDistanceCm ≤ BodyRadiusCm 返回 **0**，不返回无穷大。
	 */
	static double SilhouetteRadiusScale(double CameraDistanceCm, double BodyRadiusCm);

	/**
	 * 滚轮缩放：在**地表高度**上乘倍率再夹进 [Min, Max]（按中心距离缩放会让贴地一格穿球）。
	 * Value > 0 = 拉近，负高度按 0 处理；Max < Min 以下限为准。纯函数，可无头测。
	 */
	static double ZoomAltitudeCm(
		double AltitudeCm,
		double StepFactor,
		float Value,
		double MinAltitudeCm,
		double MaxAltitudeCm);

	/** 射线-球首次交点距离；没命中、球在背后或起点在球内返回 **-1**（给 0 会把缩放锁死）。 */
	static double RaySphereFirstHitCm(
		const FVector& RayOrigin,
		const FVector& UnitDir,
		const FVector& SphereCenter,
		double SphereRadiusCm);

	/** 用视线上的行星压低拉远上限：停在每颗行星穿入点之前 MinAltitudeCm，且不低于 MinDistanceCm。 */
	static double LimitDistanceByObstaclesCm(
		const FVector& FocusLocation,
		const FVector& RayDirection,
		TArrayView<const FStarMapObstacle> Obstacles,
		double MinAltitudeCm,
		double MinDistanceCm,
		double CurrentMaxCm);

	//~ Begin AActor Interface
	virtual void CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult) override;
	//~ End AActor Interface

protected:
	/** 焦点在「焦点 → 相机」方向的表面半径：体素行星查真实地表，非体素用碰撞半径，都没有返回 0。 */
	double ResolveFocusSurfaceRadiusCm(AActor* Focus) const;

	/**
	 * 当前视线下相机到焦点距离的合法区间：下限保护焦点，上限被视线上的其他行星压低。
	 * **每帧重算**（行星在动）。
	 */
	void ComputeOrbitDistanceLimitsCm(AActor* Focus, double& OutMinCm, double& OutMaxCm) const;

	/**
	 * 屏幕射线 → 最近被击中的体素行星表面点。用「当前最近距离」剪枝，非行星天体直接跳过。
	 */
	bool ResolveEditTargetByPlanets(const FVector& RayOrigin, const FVector& RayDirection,
		float MaxDistanceCm, FPlanetEditTarget& OutTarget) const;

	/** 相机，同时是根组件。不挂 SpringArm —— 太空里没有东西要避。 */
	UPROPERTY(VisibleAnywhere, Category = "StarMap")
	TObjectPtr<UCameraComponent> Camera;

	/** 键鼠输入。回调与输入状态都在组件里，本类只负责转发绑定。 */
	UPROPERTY(VisibleAnywhere, Category = "Input")
	TObjectPtr<UStarMapCameraInputComponent> PlayerInput;

	/**
	 * 体素调用器：四档 LOD，配置与飞船一致。必须是 UAlwaysLocalVoxelInvokerComponent ——
	 * 插件原版只把 Possess 中的 Pawn 当 local invoker，换 Possess 会触发全量重建与 chunk
	 * 簿记 ensure。本 Pawn 不 Tick、`GetVelocity()` 恒零，故用 Simple；碰撞只开在细气泡上。
	 */
	UPROPERTY(VisibleAnywhere, Category = "StarMap|Voxel")
	TObjectPtr<UVoxelSimpleInvokerComponent> VoxelInvoker;

	UPROPERTY(VisibleAnywhere, Category = "StarMap|Voxel")
	TObjectPtr<UVoxelSimpleInvokerComponent> VoxelInvokerLOD3;

	UPROPERTY(VisibleAnywhere, Category = "StarMap|Voxel")
	TObjectPtr<UVoxelSimpleInvokerComponent> VoxelInvokerLOD5;

	/**
	 * 1 公里外的过渡环：补上 LOD5 与行星自身 LOD 之间的台阶，否则边界成一片悬空板子。
	 * ±10 公里是为了盖住整条地平线，否则台阶会横在画面中间。
	 */
	UPROPERTY(VisibleAnywhere, Category = "StarMap|Voxel")
	TObjectPtr<UVoxelSimpleInvokerComponent> VoxelInvokerLOD6;

	void ApplyViewpointInvokerState();

	/** 退图收尾：Possess 回 ReturnPawn（可能已销毁），再把相机搬回挂载原点。 */
	void FinishStarMapExit(APawn* ReturnPawn);

	/** 本 Pawn 是否当前视点。初值 false —— OnRegister 会自动启用，BeginPlay 要按它关掉。 */
	bool bIsViewpoint = false;

	/** 挂载原点（相对变换），BeginPlay 拍下，供 ResetToAttachOrigin 搬回；未挂载时为单位变换。 */
	FTransform AttachOriginRelativeTransform = FTransform::Identity;

	/** 叫出本相机的 Pawn，退图时 Possess 回去。**弱引用** —— 它可能在星图期间被销毁（死角见 ExitStarMap）。 */
	TWeakObjectPtr<APawn> StarMapEnterer;

	/** 退出混合的定时器；有效且还在跑说明混合没结束，期间的 M 一律忽略。 */
	FTimerHandle StarMapExitTimerHandle;

	// ── 运行时状态 ──

	/** 从哪个方向看（yaw / pitch）。相机位置 = 焦点 − 这个方向 × 距离。 */
	UPROPERTY(Transient, VisibleAnywhere, Category = "StarMap|State")
	FRotator OrbitRotation = FRotator::ZeroRotator;

	/**
	 * 右键拖出的朝向偏移，**只影响朝向不影响位置**（位置仍按 OrbitRotation 算）；
	 * 实际朝向见 ClampAimOffset。选中变化、FocusOn、EnterFrom 时归零 —— 「锁定 = 正对」。
	 */
	UPROPERTY(Transient, VisibleAnywhere, Category = "StarMap|State")
	FRotator AimOffset = FRotator::ZeroRotator;

	/**
	 * 相机到焦点的距离。**只在发生过一次选中之后才有意义**：初值 0 是哨兵，首次选中时
	 * CalcCamera 会用实际距离整个覆盖。别用它初始化进图距离，也别指望选中前 AddZoom。
	 */
	UPROPERTY(Transient, VisibleAnywhere, Category = "StarMap|State")
	double OrbitDistanceCm = 0.0;

	/**
	 * 平滑拉远（FocusOn）的状态。平滑**只为观感**：瞬移会让整颗行星一帧内塌多档 LOD；
	 * 它**不是** VoxelDefaultRenderer ensure 的解药（真因是换 Possess 的全量重建）。
	 * 滚轮缩放过或选中被改掉时立即作废。
	 */
	bool bSmoothFocusActive = false;

	/** 拉远起点（厘米，已夹进 Min/Max）。与 CalcCamera 反推的实际距离一致。 */
	double SmoothFocusStartCm = 0.0;

	double SmoothFocusTargetCm = 0.0;

	float SmoothFocusElapsedSeconds = 0.f;

	/**
	 * 相机当前世界位置。选中有效时每帧重算成「焦点 − 视线方向 × 距离」；未选中时
	 * **不更新** —— 即冻结在取消选中那一刻。
	 */
	UPROPERTY(Transient, VisibleAnywhere, Category = "StarMap|State")
	FVector CameraLocation = FVector::ZeroVector;

	/**
	 * 上一帧的选中天体：变化时从**当前** CameraLocation 反推 OrbitDistanceCm 并重算 OrbitRotation。
	 * **位置不跳，朝向会跳**（距离被夹取时例外）。
	 */
	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> LastSeenSelected;

	/** 当前激活的矿物透视组件。行星销毁时 GC 会把它置空。 */
	UPROPERTY(Transient, VisibleAnywhere, Category = "StarMap|State")
	TObjectPtr<UPlanetMineralViewComponent> ActiveMineralView;

	// ── 可调参数 ──

	/** 每格滚轮的距离倍率（与飞船的线性步进不同）：星图距离跨好几个数量级，线性步进在 AU 尺度上等于没反应。 */
	UPROPERTY(EditAnywhere, Category = "StarMap")
	double ZoomStepFactor = 1.15;

	/** 焦点是行星时拉远到「地表半径 × 本倍率」：3 倍能让整颗行星入画，两种进入方式共用；实际距离仍夹。 */
	UPROPERTY(EditAnywhere, Category = "StarMap", meta = (ClampMin = "0.0"))
	double PlanetFocusRadiusScale = 3.0;

	/** 左键拾取的像素容差，20 像素保证远处星球也点得中；负值只会退化成 0，夹到 ≥ 0。 */
	UPROPERTY(EditAnywhere, Category = "StarMap", meta = (ClampMin = "0.0"))
	float SelectionPixelRadius = 20.f;

	/**
	 * 绕行灵敏度下限（倍率），见 OrbitLookScale。实际灵敏度 = LookSensitivity × 本值；
	 * 调小好对细节，调大好转身。
	 */
	UPROPERTY(EditAnywhere, Category = "StarMap", meta = (ClampMin = "0.0"))
	double MinOrbitLookScale = 0.05;

	/**
	 * 绕行灵敏度上限（倍率），见 OrbitLookScale。2 = 在「整颗行星入画」（3R）处封顶，
	 * 再拉远也不加速；旧值 8 配 1.2°/px 在星系尺度上一拖就整圈飞过去。
	 */
	UPROPERTY(EditAnywhere, Category = "StarMap", meta = (ClampMin = "1.0"))
	double MaxOrbitLookScale = 2.0;

	/**
	 * 右键转头离开「正对焦点」的最大角度（度）。90 = 半球：转不到背后，否则绕行位置
	 * 与画面完全对不上，玩家会以为相机穿模。夹取在 ClampAimOffset 里按**夹角**算。
	 */
	UPROPERTY(EditAnywhere, Category = "StarMap", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	double MaxAimOffsetDeg = 90.0;

	/**
	 * 相机在地表之上的最小高度（厘米），也是视线擦过行星的余量。5000 = 50 米，
	 * 落在细气泡调用器 LOD1（80 米）范围内。别再按「行星半径 = 1e4 厘米」写常量，
	 * 当前行星半径是 1e6–2e6 厘米。
	 */
	UPROPERTY(EditAnywhere, Category = "StarMap")
	double MinSurfaceAltitudeCm = 5.0e3;

	/**
	 * 进星图时锚点离地表的距离（厘米）。默认 1 公里：起伏清晰又不至于埋在体素里，
	 * 实际再夹进 [0, 25% 地表半径]；可按星球在 BP_StarMapCameraPawn 里分别配。
	 */
	UPROPERTY(EditAnywhere, Category = "StarMap", meta = (ClampMin = "0.0"))
	double EntryAnchorAltitudeCm = 1.0e5;

	/**
	 * 焦点非行星（飞船 / 角色）时相机到焦点中心的最小距离。故意远小于 MinSurfaceAltitudeCm：
	 * 用 50 米会让停在地面的飞船一进星图就把相机顶到地下。防穿地由视线上的行星约束负责。
	 */
	UPROPERTY(EditAnywhere, Category = "StarMap")
	double MinFocusDistanceCm = 5.0e2;

	/**
	 * 最远距离（到焦点球心），1e9 = 1000 AU。这是**全局**上限；视线上有行星时实际上限更小。
	 * 当前关卡三恒星在 ±200 AU、行星在 +400 AU，系统跨 600 AU —— 50 AU 根本拉不出全貌。
	 */
	UPROPERTY(EditAnywhere, Category = "StarMap")
	double MaxOrbitDistanceCm = 1.0e9;

	/**
	 * FocusOn 的平滑时长（秒）。距离按**对数**插值而不是线性：LOD 需求按数量级变化，
	 * 线性插值会把近处几帧就冲过去、档位仍一帧塌好几。对数让每帧距离倍率恒定。
	 */
	UPROPERTY(EditAnywhere, Category = "StarMap", meta = (ClampMin = "0.01"))
	float SmoothFocusSeconds = 0.8f;

	/**
	 * 退图的视点混合时长，0 = 直接切回。混合走完才 Possess —— 提前调用会掐掉混合。
	 * 放在相机上，按星球可各配各的。
	 */
	UPROPERTY(EditAnywhere, Category = "StarMap", meta = (ClampMin = "0.0"))
	float ExitBlendSeconds = 0.5f;
};
