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

/**
 * 一个屏幕空间候选：某个天体的 Actor 和它投影到屏幕上的位置。
 *
 * 单独拎成一个结构体而不是用两个平行数组（Actors + ScreenPositions）：平行数组
 * 靠下标约定对齐，结构体靠类型对齐。下标错位是编译器看不出来的错。
 */
struct FStarMapScreenCandidate
{
	AActor* Actor = nullptr;
	FVector2D ScreenPosition = FVector2D::ZeroVector;

	/**
	 * 该天体在屏幕上的**盘面半径**（像素）。0 表示「没算 / 算不出来」。
	 *
	 * 拾取容差取 max(固定像素容差, 这个值)：远处的天体只有几个像素，靠固定容差才好点；
	 * 近处的盘面有好几百像素，只按固定容差就点不中盘面（点到盘面上却选不中）。
	 *
	 * **必须是「与视线垂直」的半径**：调用方把「中心」与「中心 + 视线右向 × 世界半径」
	 * 都投到屏幕，取两点的像素距离。用固定的世界轴会算出被投影压扁的假半径。
	 */
	float ScreenRadiusPx = 0.f;
};

/**
 * 星图里挡在相机拉远路径上的球体：一颗行星的球心与当前方向上的地表半径。
 *
 * 单独拎成结构体是为了让「不穿行星」的夹取能被无头测试：算夹取的函数只吃
 * 位置与半径，不碰世界、不碰体素查询。
 */
struct FStarMapObstacle
{
	FVector Center = FVector::ZeroVector;
	double RadiusCm = 0.0;
};

struct FPlanetEditTarget;

/**
 * 星图模式的相机。由行星的 Child Actor Component 提供；进图时由进场 Pawn 调用
 * EnterStarMap 接手（Possess 发生在那里），退图由本类的 ExitStarMap 收尾。
 *
 * **相机不 tick，位置在 CalcCamera 里现算。** 这是必需的，不是风格选择：
 * UWorld::Tick 里的顺序是「各 Actor tick 组 → FTickableGameObject::TickObjects
 * （子系统在这里推进求解器、把位置写回 Actor）→ UpdateCameraManager（CalcCamera
 * 在这里被调用）→ TG_PostUpdateWork → TG_LastDemotable」。
 * 任何 tick 组都不落在中间那个窗口里：排在前面的会读到天体上一帧的位置，排在
 * TG_PostUpdateWork 的则相机已经算完了。两条路都差一帧，CalcCamera 零滞后。
 *
 * Pawn 自己的 Actor 变换**不影响相机** —— 它只是为了让这个 Pawn 在 PIE 里
 * 看得见、点得中。
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

	/**
	 * 星图模式的行星编辑组件。**由蓝图子类 BP_StarMapCameraPawn 挂载**
	 * （2026-10-31 从构造函数移出），笔刷区间 / 默认尺寸 / 步长 / 提示文案 /
	 * 最大编辑距离都在它的类默认值里配。原生实例没有编辑功能，调用方按空处理。
	 */
	UPlanetEditComponent* GetPlanetEditComponent() const;

	// ── 矿物透视 ──

	/**
	 * H 键：进入 / 退出矿物透视。
	 *
	 * 进入条件：当前选中的是体素行星、且它带矿物透视组件。失败时不改变状态，
	 * 并把原因返回给输入组件（由它经 HUD 弹提示）。退出条件：再按一次 H、退出
	 * 星图、换 Pawn（控制器在 OnPossess / 退出星图时调用 ExitMineralViewIfActive），
	 * 或选中天体变了（控制器 PlayerTick）。
	 *
	 * @return 失败原因；进入成功、退出成功或本来无操作时为空。
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

	/** 左键拾取天体的像素容差。相机输入组件与 HUD 悬停查询共用。 */
	float GetSelectionPixelRadius() const { return SelectionPixelRadius; }

	/** 进入星图模式时由控制器调用：把相机放到给定位置，并重置为「未选中」。 */
	void EnterFrom(const FVector& InCameraLocation, const FRotator& InCameraRotation);

	/**
	 * 入场锚点：行星地表之上 AltitudeCm 的世界位置。
	 *
	 * 方向 = PlanetCenter → TowardWorldLocation；两者重合时退回 +Z（不产生 NaN）。
	 * 高度夹在 [0, 25% 半径]：蓝图里填多大都不会把相机送进轨道尺度。
	 * SurfaceRadiusCm ≤ 0（拿不到可信几何）返回 PlanetCenter，由调用方按「没有
	 * 行星」处理。纯函数，不碰世界，所以能无头测。
	 */
	static FVector ComputeSurfaceAnchorLocation(
		const FVector& PlanetCenter,
		const FVector& TowardWorldLocation,
		double SurfaceRadiusCm,
		double AltitudeCm);

	/**
	 * 用自己挂载的行星算入场锚点：球心 + 地表半径 + EntryAnchorAltitudeCm。
	 *
	 * 没挂在行星上时返回当前 Actor 位置 —— 入场画面不变。
	 */
	FVector ComputeEntryAnchorLocation(const FVector& TowardWorldLocation) const;

	/**
	 * 把相机摆回挂载原点（行星球心）。
	 *
	 * 星图期间 Actor 每帧跟着渲染相机走（见 CalcCamera 末尾），退出时必须搬回来，
	 * 否则这颗行星的子 Actor 会一直漂在最后看的位置上。没有挂载时
	 * 什么都不做。
	 */
	void ResetToAttachOrigin();

	// ── 进出星图的会话流程 ──

	/**
	 * 进入星图：From 是进场 Pawn，PC 是操控它的玩家控制器。
	 *
	 * 做四件事：记下 From 作为退出时的返回对象、摆好入场位姿（ComputeEntryAnchorLocation）、
	 * 通知求解器进入星图态、把焦点设为挂载行星（FocusOn 拉远），最后 `Possess(this)`。
	 *
	 * **由 Pawn 的输入组件直接调用**，不经过控制器的会话流程 —— 换谁 Possess 由
	 * 调用方决定，本函数只负责「这台相机怎么进场」。PC 为空 / From 为空 / 世界没有
	 * 求解器时返回 false，什么都不改。
	 *
	 * @return 是否真的进了星图。
	 */
	bool EnterStarMap(APawn* From, APlayerController* PC);

	/**
	 * 退出星图：还原矿物透视与编辑模式、通知求解器退出、视点混合结束后
	 * `Possess` 回进场 Pawn，最后把相机搬回挂载原点。
	 *
	 * 返回对象就是 EnterStarMap 记下的那个 —— 相机自己知道是谁把它叫出来的，
	 * 所以退图**不需要问控制器**。混合期间重复调用会被忽略。
	 */
	void ExitStarMap();

	/**
	 * 进入星图模式时聚焦到某个天体：把焦点设为它，并让相机沿当前视线方向
	 * 平滑后退到距焦点 DistanceCm 处。
	 *
	 * 与「进入后手动点选」的区别：手动点选时相机位置不动、只把朝向摆向新焦点
	 * （见 CalcCamera 里 LastSeenSelected 那段）；这里则是**主动拉远**，让飞船
	 * 连同附近轨道一起进入视野 —— 这是按 M 进星图想要的观感。
	 *
	 * **拉远是平滑的、不在本函数内一次跳到位**：直接瞬移会让整颗行星在同一帧里
	 * 从 LOD 1/3/5 塌到 LOD 6/7，画面掉精度（平滑只管观感，不负责躲 ensure ——
	 * 见 bSmoothFocusActive 的说明）。这里只记录起点与目标，插值在 CalcCamera。
	 *
	 * 距离会被夹进「不穿行星」的合法区间（见 ComputeOrbitDistanceLimitsCm）。
	 * Body 为空时什么都不做。
	 */
	void FocusOn(AActor* Body, double DistanceCm);

	/**
	 * 鼠标位移 → 绕焦点转视角。单位是度，来自 UStarMapCameraInputComponent::LookSensitivity。
	 *
	 * 实际转过的角度按 OrbitLookScale 缩放：贴着地表看细节时同样的鼠标位移只转
	 * 一小点，拉到远处看整个星系时转得比基准快。
	 */
	void OrbitLook(float DeltaYaw, float DeltaPitch);

	/**
	 * 鼠标位移 → 改**视线朝向**，不动相机位置（右键）。
	 *
	 * 与 OrbitLook 的区别：OrbitLook 改的是「相机绕焦点的方向」（位置跟着变），
	 * 本函数只把镜头从「正对焦点」偏开一点，`OrbitRotation`（决定位置）一点都不动。
	 * 于是锁定天体之后画面不再永远死盯球心。
	 *
	 * 偏移量不在这里夹取：越拖越大是正常的，真正生效前由 ClampAimOffset 压回
	 * MaxAimOffsetDeg 的圆锥内。这样往回拖时手感是连续的（而不是一到边界就卡住、
	 * 再拖也不动）。
	 *
	 * 灵敏度**不乘** OrbitLookScale：绕行要按距离缩放才能让屏幕上的位移量恒定，
	 * 而转头是纯角度操作 —— 套上「贴地慢」会让近处几乎转不动。任何距离上，
	 * 同样的鼠标位移转过的角度都相同。
	 */
	void AimLook(float DeltaYaw, float DeltaPitch);

	/**
	 * 把「基准朝向 + 偏移」合成后压回圆锥：结果视线与 BaseRotation 方向的夹角
	 * 不超过 MaxOffsetDeg 度。纯函数（不碰世界、不碰成员），所以能无头测。
	 *
	 * 锥是绕**夹角**夹的，不是逐轴夹 Pitch/Yaw —— 逐轴夹出来的边界是球面上一个
	 * 正方形，斜着拖能到 90°√2 ≈ 127°，「半球」的承诺就破了。
	 *
	 * 退化情形一律返回 BaseRotation（roll 归零）：
	 *   - MaxOffsetDeg ≤ 0；
	 *   - AimOffset 含 NaN / 非有限值（绝不把 NaN 写进相机朝向）；
	 *   - 合成结果与基准方向重合。
	 * 结果是 roll 恒为 0 —— 星图相机不翻滚。
	 */
	static FRotator ClampAimOffset(
		const FRotator& BaseRotation,
		const FRotator& AimOffset,
		double MaxOffsetDeg);

	/**
	 * 绕行视角的灵敏度倍率，夹在 [MinScale, MaxScale]：相机离焦点表面越近越小，
	 * 越远越大（可以超过 1）。
	 *
	 * 相机绕的是**焦点中心**，转动只改变视角朝向，而「画面里星球挪了多少」与
	 * 朝向变化成正比、与距离无关。所以手感的目标是：**贴得越近，同样的鼠标位移
	 * 对画面的影响越小；拉得越远，一拖就扫过整个星系**。
	 *
	 * 倍率 = 离焦点表面的高度 / 参考高度，参考高度 = max(焦点表面半径, 5 米)：
	 *   - 行星（半径 1e6 厘米）：2R 处 = 1，3R（整颗入画） = 2，约 9R 起封顶；
	 *   - 贴地 50 米 → 0.005，夹到下限 MinScale；
	 *   - 飞船/角色这类小天体按 5 米兜底，5 米处仍是 0.9 —— 与旧曲线一致，
	 *     否则「贴着飞船看一眼」会变成几十倍灵敏度。
	 *
	 * 5 米兜底与 MinFocusDistanceCm 的默认值同源，但它是函数内的常量而不是读实例：
	 * 本函数必须是纯函数才能无头测。
	 *
	 * DistanceCm ≤ 0（还没选中过焦点，OrbitDistanceCm 还是哨兵）或
	 * FocusSurfaceRadiusCm ≤ 0（没有可信几何）返回 1。
	 * MinScale / MaxScale 先夹进 [0, +∞)，且下限不高于上限：MaxScale < MinScale
	 * 时以下限为准（返回上限值）。
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
	 * 从屏幕坐标里挑一颗天体。纯函数：不碰世界、不碰碰撞，所以能无头测。
	 *
	 * 返回离 ScreenPoint 最近、且距离 ≤ max(PixelRadius, 该候选的 ScreenRadiusPx) 的候选。
	 * 都不在容差内、或候选全为空时返回 nullptr。**边界含**：距离恰好等于容差算命中。
	 *
	 * 等距时取**先出现**的那个，所以结果依赖 Candidates 的顺序 —— 调用方给的
	 * 顺序必须是确定的（本项目里就是 `GetRegisteredBodies` 的返回顺序，
	 * 即 `Bodies` 的当前顺序：`FinalizeRegistration` 之后为质量降序）。
	 */
	static AActor* PickBodyAtScreenPosition(
		TArrayView<const FStarMapScreenCandidate> Candidates,
		const FVector2D& ScreenPoint,
		float PixelRadius);

	// ── 天体 / 相机解析（纯查询，能无头测）──

	/**
	 * 这个天体身上（含它的挂载物）有没有星图相机。
	 *
	 * 星图的聚焦目标**只认带相机的天体**：相机是「照这颗天体的视点」，没有相机的
	 * 天体（飞船）在星图里既不被进场自动聚焦，也点不中。
	 */
	static bool HasStarMapCamera(const AActor* Body);

	/**
	 * 「进图时的参考行星」：进星图该借用谁的星图相机。
	 *
	 * 驾驶中 → 飞船的参考行星（ASpaceshipPawn::GetPlanetActor，子系统每帧维护）；
	 * 步行 → 角色当前所在的行星（APlanetCharacter::PlanetActor）；
	 * 其他 Pawn / 空 → 空。**只做类型解析，不做距离搜索** —— 那是 Pawn 自己的职责。
	 */
	static AVoxelPlanetActor* ResolveReferencePlanet(const APawn* EnteringPawn);

	/**
	 * 在 Owner 的组件里找「行星自带的星图相机」：第一台作为
	 * UChildActorComponent 子 Actor 存在的 AStarMapCameraPawn。
	 *
	 * BP_VoxelPlanet 挂一个 Child Actor Component（子类 BP_StarMapCameraPawn），
	 * 「星球 + 它的星图相机」就成了一个可整体复制、每个实例可单独换类（Details
	 * 里 Child Actor Class 可编辑）的组合 —— 这是 B2 路线（用户选定，2026-11-01）。
	 *
	 * 注意 CAC 的子 Actor 在类默认对象（CDO）里不存在、实例化才创建，所以
	 * 本函数对 CDO 恒返回空 —— 这是 CAC 的机制，不是 bug。
	 */
	static AStarMapCameraPawn* FindChildStarMapCamera(const AActor* PlanetOwner);

	/**
	 * 在 Owner 上找直接 Attach 的第一台 AStarMapCameraPawn。
	 *
	 * 这条路径覆盖「相机是关卡里的独立 Actor、手动 Attach 到星球」的旧摆法
	 * （UniverseMap 里现存的 3 台就是这种）。CAC 的子 Actor 其实也会 Attach 到
	 * 宿主，所以本函数同样能取到 CAC 相机 —— 保留它是为了让 CAC 尚未普及、
	 * 或临时手摆一台相机的关卡也能正常工作。
	 *
	 * 不递归子挂载物，避免飞船 / 角色 / 采矿平台上挂的相机被误认成行星相机。
	 */
	static AStarMapCameraPawn* FindAttachedStarMapCamera(const AActor* PlanetOwner);

	/**
	 * 上面两步的串联：进星图时优先用焦点行星自带的相机。
	 * 先认 CAC（显式声明的那台），没有再退回附加关系（手动摆的那台）；
	 * 都没有、或进图时解析不出行星（深空）→ 返回空，调用方**不进入星图**。
	 */
	static AStarMapCameraPawn* ResolvePlanetStarMapCamera(const APawn* EnteringPawn);

	/**
	 * 球体「屏幕轮廓半径」相对于「投影(中心 + 右向 × 半径)」的放大系数。
	 *
	 * 把「中心 + 右向 × R」投到屏幕，量到的是 `f·R/d`（d = 相机到球心距离）。但球体的
	 * 可见轮廓由**切线**决定，角半径是 `asin(R/d)`，屏幕半径是
	 * `f·tan(asin(R/d)) = f·R/√(d²−R²)`。两者之比 = `1/√(1−(R/d)²)` = `1/cos α`，
	 * 其中 `sin α = R/d`。最近距离（d = 2R）处 α = 30°，系数 = 1.1547 —— 不乘它，
	 * 容差只有真实盘面的 86.6%，盘面外圈一圈点不中。
	 *
	 * `CameraDistanceCm <= BodyRadiusCm`（相机在球内或贴着球面）返回 **0** —— 调用方按
	 * 「算不出来」处理、退回固定像素容差。**不返回无穷大。**
	 */
	static double SilhouetteRadiusScale(double CameraDistanceCm, double BodyRadiusCm);

	/**
	 * 滚轮缩放的纯函数：在**地表高度**上乘倍率，再夹进 [MinAltitudeCm, MaxAltitudeCm]。
	 *
	 * 为什么缩放的是高度而不是到球心的距离：星图焦点的半径从飞船（几十厘米）到
	 * 行星（1e6–2e6 厘米）不等。按中心距离乘同一个倍率时，贴着地表滚一格会直接
	 * 穿进星球，远处滚一格又只挪一点点。按高度缩放让「每格画面的变化」大致恒定：
	 * 贴地一格几十米，远处一格几个 AU。
	 *
	 * Value > 0 = 滚轮上滚 = 拉近（高度变小）。AltitudeCm 为负按 0 处理；
	 * MaxAltitudeCm 小于 MinAltitudeCm 时以下限为准。
	 */
	static double ZoomAltitudeCm(
		double AltitudeCm,
		double StepFactor,
		float Value,
		double MinAltitudeCm,
		double MaxAltitudeCm);

	/**
	 * 射线-球首次交点：从 RayOrigin 沿 UnitDir（内部归一化）出发，返回首次穿入
	 * 球面的距离（厘米）；没命中、或整个球在射线背后返回 **-1**。
	 *
	 * 起点在球内（Entry ≤ 0 < Exit）也返回 -1：那是「焦点本身就在山里」的病态
	 * 情形，强行给个 0 反而会把缩放锁死。
	 */
	static double RaySphereFirstHitCm(
		const FVector& RayOrigin,
		const FVector& UnitDir,
		const FVector& SphereCenter,
		double SphereRadiusCm);

	/**
	 * 用视线上的行星压低「拉远上限」：相机停在每颗行星穿入点之前 MinAltitudeCm 处。
	 *
	 * 返回 min(CurrentMaxCm, 各命中点 − MinAltitudeCm)，且不小于 MinDistanceCm
	 * （MinDistanceCm 由焦点自己的半径决定；两者冲突说明焦点贴在山里，保下限）。
	 * 没命中的行星不产生约束。
	 */
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
	/**
	 * 焦点天体在「焦点 → 相机」方向上的表面半径（厘米）。
	 *
	 * 焦点是体素行星时按该方向查真实地表（山脉会高于基准球面）；飞船/角色这类
	 * 非体素天体退回碰撞半径；都没有则返回 0。
	 */
	double ResolveFocusSurfaceRadiusCm(AActor* Focus) const;

	/**
	 * 当前视线下「相机到焦点距离」的合法区间（厘米）。
	 *
	 * 下限 = 焦点表面半径 + （焦点是行星时用 MinSurfaceAltitudeCm，否则用
	 * MinFocusDistanceCm）—— 绕焦点转时不能钻进焦点，也不能把相机塞进飞船模型。
	 * 上限 = MaxOrbitDistanceCm，再被视线上的其他行星压低：沿「焦点 → 相机」
	 * 做射线-球求交，相机只能停在命中点前 MinSurfaceAltitudeCm 处。焦点是飞船
	 * （进图默认就是飞船）时，这条才是防穿地的主力 —— 飞船自己的半径保护不了
	 * 脚下的行星。
	 *
	 * **每帧都要重算**：行星在动，合法区间也在动。
	 */
	void ComputeOrbitDistanceLimitsCm(AActor* Focus, double& OutMinCm, double& OutMaxCm) const;

	/**
	 * 屏幕射线 → 最近一颗被射线打到的体素行星的表面点。
	 *
	 * 逐颗行星调用 AVoxelPlanetActor::RaycastSurfaceCm，用「当前最近距离」当
	 * 上限剪枝 —— 射线没穿过的行星在解析那一步就返回了，不会付体素行进的开销。
	 * 不是行星的天体（飞船、角色）直接跳过：它们没有可编辑的体素数据。
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
	 * 体素调用器：四档 LOD，配置与飞船（ASpaceshipPawn）同一套。
	 *
	 * 类型是本项目的 UAlwaysLocalVoxelInvokerComponent，不是插件的 Simple 版。
	 * 插件原版只把「被本地控制器 Possess 的 Pawn」算作 local invoker
	 * （VoxelInvokerComponent.cpp:21），而进出星图恰好要换 Possess 对象 —— 用原版的话，
	 * 每次进出星图都会把飞船和角色的调用器一起踢出 LOD，逼插件重建整棵八叉树，
	 * 撞上 chunk 簿记 ensure。详见 AlwaysLocalVoxelInvokerComponent.h。
	 *
	 * 靠近行星时靠它把脚下的体素细分到最精，离开后由行星自己的调用器接手。
	 *
	 * 用 **Simple** 而不是飞船那种 WithPrediction：本 Pawn 不 Tick，位置在
	 * CalcCamera 里现算，`GetVelocity()` 恒为零 —— 预测位移永远是 0，
	 * 挂预测版只是多一层没有作用的计算。
	 *
	 * 四档只做 LOD；碰撞只开在细气泡上（碰撞细分路径不读 LODToSet，锁死
	 * Height 0，开大了会顶爆显存，详见 SpaceshipPawn.cpp 构造函数的注释）。
	 */
	UPROPERTY(VisibleAnywhere, Category = "StarMap|Voxel")
	TObjectPtr<UVoxelSimpleInvokerComponent> VoxelInvoker;

	UPROPERTY(VisibleAnywhere, Category = "StarMap|Voxel")
	TObjectPtr<UVoxelSimpleInvokerComponent> VoxelInvokerLOD3;

	UPROPERTY(VisibleAnywhere, Category = "StarMap|Voxel")
	TObjectPtr<UVoxelSimpleInvokerComponent> VoxelInvokerLOD5;

	/**
	 * 1 公里外的过渡环：补上 LOD5（叶块 512 米）与行星自身 LOD 之间的那一档。
	 *
	 * 没有它，离相机 1 公里处分辨率直接跨 3 级、8 倍（行星 3 是 512 米 → 4.1 公里）。
	 * 八叉树块是轴对齐立方体，边界投到地表上就是一条笔直的接缝；粗的一侧 32³ 个
	 * 采样点跨 4 公里，法线、顶点色、地形起伏全被抹平，看起来像一片悬空的板子
	 * 横穿地形。行星的 MaxLOD 把全星压到 7（叶块 2.05 公里）之后，
	 * 这一环（1.02 公里）就是 512 米与 2.05 公里之间唯一的台阶。
	 *
	 * 范围 ±10 公里而不是 ±3 公里：20 公里半径的星，1 公里高度地平线约 6.4 公里，
	 * 2.5 公里高度约 10.3 公里。环必须盖住整条地平线，否则那道台阶会横在画面中间。
	 */
	UPROPERTY(VisibleAnywhere, Category = "StarMap|Voxel")
	TObjectPtr<UVoxelSimpleInvokerComponent> VoxelInvokerLOD6;

	/** 四档调用器同进同出。 */
	void ApplyViewpointInvokerState();

	/**
	 * 退图的收尾：Possess 回 ReturnPawn（可能已经没了），再把相机搬回挂载原点。
	 * 混合没开、或返回 Pawn 为空时由 ExitStarMap 直接调用。
	 */
	void FinishStarMapExit(APawn* ReturnPawn);

	/** 本 Pawn 是否当前视点。初值 false —— OnRegister 会自动启用，BeginPlay 要按它关掉。 */
	bool bIsViewpoint = false;

	/**
	 * 挂载原点（相对变换），BeginPlay 拍下。
	 *
	 * ResetToAttachOrigin 用它把相机搬回行星球心的设计位置。未挂载时
	 * 保持单位变换，且 ResetToAttachOrigin 直接跳过。
	 */
	FTransform AttachOriginRelativeTransform = FTransform::Identity;

	/**
	 * 把本相机叫出来的那个 Pawn，退图时 Possess 回它。
	 *
	 * **弱引用** —— 它可能在星图期间被销毁（引擎的 FellOutOfWorld 会 Destroy）。
	 * 那时 ExitStarMap 不 Possess，留在相机上 —— 这是个已知的、可达的死角，
	 * 完整成因与三步修法写在 ExitStarMap 里。
	 */
	TWeakObjectPtr<APawn> StarMapEnterer;

	/** 退出混合的定时器；有效且还在跑说明混合没结束，期间的 M 一律忽略。 */
	FTimerHandle StarMapExitTimerHandle;

	// ── 运行时状态 ──

	/** 从哪个方向看（yaw / pitch）。相机位置 = 焦点 − 这个方向 × 距离。 */
	UPROPERTY(Transient, VisibleAnywhere, Category = "StarMap|State")
	FRotator OrbitRotation = FRotator::ZeroRotator;

	/**
	 * 右键拖出来的朝向偏移（yaw / pitch 的累加量）。**只影响朝向，不影响位置**。
	 *
	 * 相机位置仍按 OrbitRotation 算，实际朝向是
	 * ClampAimOffset(OrbitRotation, AimOffset, MaxAimOffsetDeg)。两件事拆开，
	 * 于是「转头」不会让相机绕着球跑，绕球也永远从正对球心的方向起算。
	 *
	 * 归零时机：选中天体变化、FocusOn（按 M 入场）、EnterFrom。滚轮缩放不动它。
	 * 不归零的话，换一颗天体时玩家会带着上一颗行星的偏移去看新目标，第一眼
	 * 就是歪的 —— 而「锁定 = 正对」是这个模式的默认约定。
	 */
	UPROPERTY(Transient, VisibleAnywhere, Category = "StarMap|State")
	FRotator AimOffset = FRotator::ZeroRotator;

	/**
	 * 相机到焦点的距离（厘米）。
	 *
	 * **只在发生过一次选中之后才有意义。** 初始值 0 是哨兵，不影响任何可观测结果：
	 * 未选中时 CalcCamera 不更新 CameraLocation（也就用不到它），而第一次选中时
	 * CalcCamera 会用「当前相机位置 → 焦点」的实际距离（夹进合法区间）把它整个覆盖掉。
	 * EnterFrom 不重置它 —— 重置成什么都不影响结果，因为首次选中必然覆盖。
	 *
	 * 因此：不要读它来初始化「进星图时的距离」；也不要指望选中之前调 AddZoom
	 * 有可观测效果（那期间它被夹到合法下限，但没人看）。
	 */
	UPROPERTY(Transient, VisibleAnywhere, Category = "StarMap|State")
	double OrbitDistanceCm = 0.0;

	/**
	 * 平滑拉远（FocusOn）的状态。FocusOn 只记录起点与目标，CalcCamera 每帧把
	 * OrbitDistanceCm 按 SmoothFocusSeconds 插值过去。
	 *
	 * **平滑是为了观感，不是为了躲 ensure。** 进星图时相机从地表瞬移到焦点外
	 * （默认 0.5 AU），整颗行星会在同一帧里从 LOD 1/3/5 塌到 LOD 6/7，画面会
	 * 突然掉精度；摊到几十帧后每帧的变化量与飞船高速飞行相当，观感自然。
	 *
	 * 曾经以为这种一次性 LOD 切换就是 VoxelDefaultRenderer.cpp:1249 / 846 那几条
	 * ensure 的成因 —— **2026-10-24 用日志否掉了**：ensure 在按 M 后约 113 ms 就报，
	 * 相机几乎没动；退出星图（相机不动）时同样会报一条。真正的触发点是 Possess
	 * 换人导致调用器 `bUseForLOD` 翻转、全量重建，修复见
	 * AlwaysLocalVoxelInvokerComponent.h。平滑保留，但不要再把它当成 ensure 的解药。
	 *
	 * 用户滚轮缩放过、或选中被改掉时，拉远立即作废（见 AddZoom 与 CalcCamera
	 * 的选中变化分支）—— 平滑只是入场的过渡，不该压过玩家输入。
	 */
	bool bSmoothFocusActive = false;

	/** 拉远起点（厘米，已夹进 Min/Max）。与 CalcCamera 反推的实际距离一致。 */
	double SmoothFocusStartCm = 0.0;

	/** 拉远目标（厘米，已夹进 Min/Max）。 */
	double SmoothFocusTargetCm = 0.0;

	/** 拉远已进行的秒数，每帧累加 DeltaTime。 */
	float SmoothFocusElapsedSeconds = 0.f;

	/**
	 * 相机当前的世界位置。
	 *
	 * 选中有效时每帧重算成「焦点 − 视线方向 × 距离」；未选中时**不更新** ——
	 * 这就是「冻结在取消选中那一刻的位置」本身。
	 */
	UPROPERTY(Transient, VisibleAnywhere, Category = "StarMap|State")
	FVector CameraLocation = FVector::ZeroVector;

	/**
	 * 上一帧看到的选中天体，用来检出「选中状态变了」。
	 *
	 * 检出变化时从**当前** CameraLocation 反推 OrbitDistanceCm，并把 OrbitRotation
	 * 重算成「看向新焦点」。
	 *
	 * **位置不跳，朝向会跳。** 距离取 |相机→焦点|、方向取「相机→焦点」的单位向量，
	 * 于是 焦点 − 方向 × 距离 ≡ 原来的 CameraLocation —— 位置代数上严格不变。
	 * 但朝向是被整个替换掉的，只有原本就正对着该天体时才不变；最明显的是 EnterFrom
	 * 之后的第一次选中（从「进星图时的飞船视角」直接摆到对准被选天体）。
	 *
	 * 位置不变与朝向重算是一体的：CameraLocation 由 OrbitRotation 与 OrbitDistanceCm
	 * 共同决定，想保住位置，朝向就只能取「看向焦点」这一个值。唯一的例外是距离被
	 * 夹取时（相机不能钻进星体），那时位置也会跳一次 —— 见 CalcCamera 里的注释。
	 */
	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> LastSeenSelected;

	/** 当前激活的矿物透视组件。行星销毁时 GC 会把它置空。 */
	UPROPERTY(Transient, VisibleAnywhere, Category = "StarMap|State")
	TObjectPtr<UPlanetMineralViewComponent> ActiveMineralView;

	// ── 可调参数 ──

	/**
	 * 每格滚轮的距离倍率。
	 *
	 * **倍率而不是线性步进**，这里和飞船的 AddZoom 不一样，是有意的：飞船的摇臂
	 * 长度只在几十厘米的量级内变，星图的距离要跨好几个数量级（从贴着一颗行星到
	 * 看整个星系），线性步进在 AU 尺度上等于没反应。
	 */
	UPROPERTY(EditAnywhere, Category = "StarMap")
	double ZoomStepFactor = 1.15;

	/**
	 * 焦点是行星时（按 M 进场、按 H 进矿物透视），相机拉远到「地表半径 × 本倍率」。
	 *
	 * 3 倍半径能把整颗行星连同它附近的空间一起框进画面，是这个项目里行星构图的
	 * 默认档。实际距离仍会被「不穿行星」区间夹住。两种进入方式共用这一个值，
	 * 构图才一致 —— 所以它住在相机上，而不是控制器各存一份。
	 */
	UPROPERTY(EditAnywhere, Category = "StarMap", meta = (ClampMin = "0.0"))
	double PlanetFocusRadiusScale = 3.0;

	/**
	 * 左键拾取天体的像素容差。20 像素保证远处的星球也点得中。
	 *
	 * 夹到 ≥ 0：负值在拾取里没有意义，而且**新旧行为不同** —— 旧代码把
	 * `PixelRadius * PixelRadius` 当半径平方用，`-5` 会被当成 `5`；现在逐候选取
	 * `max(PixelRadius, ScreenRadiusPx)`，`-5` 被当成 `0`。这个字段是 EditAnywhere，
	 * 编辑器里能被改成负数，所以在这里挡住。
	 */
	UPROPERTY(EditAnywhere, Category = "StarMap", meta = (ClampMin = "0.0"))
	float SelectionPixelRadius = 20.f;

	/**
	 * 绕行视角的灵敏度下限（倍率），见 OrbitLookScale。
	 *
	 * 贴地表时实际灵敏度 = UStarMapCameraInputComponent::LookSensitivity × 这个值。0.05 = 慢到二十分之一：
	 * 调小更好对细节，但转身要多拖几倍；调大更好转身，但贴地容易「移过」。
	 * 远处的灵敏度由 MaxOrbitLookScale 决定，不受这个值影响。
	 */
	UPROPERTY(EditAnywhere, Category = "StarMap", meta = (ClampMin = "0.0"))
	double MinOrbitLookScale = 0.05;

	/**
	 * 绕行视角的灵敏度上限（倍率），见 OrbitLookScale。
	 *
	 * 拉到看整个星系时实际灵敏度 = UStarMapCameraInputComponent::LookSensitivity × 这个值。8 = 远距离
	 * 一拖就横扫过去；调小更好精确定位远处天体，调大更容易大范围扫视。
	 * 2R–3R（整颗行星入画）那一段的倍率是 1–2，不受这个值影响。
	 */
	UPROPERTY(EditAnywhere, Category = "StarMap", meta = (ClampMin = "1.0"))
	double MaxOrbitLookScale = 8.0;

	/**
	 * 右键转头能离开「正对焦点」的最大角度（度）。
	 *
	 * 90 = 半球：相机停在焦点外某处，镜头可以扫到与「看向球心」垂直的方向，
	 * 但转不到背后（转到背后时绕行位置与画面就完全对不上，玩家会以为相机穿模）。
	 * 调小更像「微调构图」，调大能看到更多侧后方。上限本身也支持 0（锁死正对）。
	 *
	 * 夹取在 ClampAimOffset 里，按**夹角**算，不是逐轴。
	 */
	UPROPERTY(EditAnywhere, Category = "StarMap", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	double MaxAimOffsetDeg = 90.0;

	/**
	 * 相机在地表之上的最小高度（厘米）。
	 *
	 * 滚轮作用在**高度**（相机到球心距离 − 焦点表面半径）上，见 ZoomAltitudeCm。
	 * 5000 = 50 米：贴着地表看细节，又不会把近裁剪面塞进体素；细气泡调用器的
	 * LOD 1 范围是 80 米，这个高度正好落在它里面。
	 *
	 * 同一个值也用作「视线擦过行星」的余量：沿视线拉远时，相机停在行星表面
	 * 前 50 米。
	 *
	 * 注意别再按「行星半径 = 1e4 厘米」写常量：当前关卡的行星半径是 1e6–2e6
	 * 厘米（1–2 AU），任何按中心距离设的固定下限都会让相机扎进星球。
	 */
	UPROPERTY(EditAnywhere, Category = "StarMap")
	double MinSurfaceAltitudeCm = 5.0e3;

	/**
	 * 进星图时相机锚点离地表的距离（厘米）。
	 *
	 * 默认 1e5 = 1 公里：行星半径是 1e6–2e6 厘米的量级，1 公里处地表起伏清晰，
	 * 又不会让入场第一帧埋在体素里。实际值再夹进 [0, 25% 地表半径]，见
	 * ComputeSurfaceAnchorLocation。按星球在 BP_StarMapCameraPawn 里可分别配。
	 */
	UPROPERTY(EditAnywhere, Category = "StarMap", meta = (ClampMin = "0.0"))
	double EntryAnchorAltitudeCm = 1.0e5;

	/**
	 * 焦点不是行星时（飞船/角色），相机到焦点中心的最小距离（厘米）。
	 *
	 * 这一项故意远小于 MinSurfaceAltitudeCm：飞船的「表面」不是地表，5 米已经
	 * 够贴近；用 50 米会让贴着地面停放的飞船一进星图就把相机顶到地下。
	 * 防穿地由视线上的行星约束负责，不靠这个值。
	 */
	UPROPERTY(EditAnywhere, Category = "StarMap")
	double MinFocusDistanceCm = 5.0e2;

	/**
	 * 最远距离（厘米，到焦点球心）。5e7 厘米 = 50 AU，够从一颗行星看到整个星系。
	 *
	 * 这是**全局**上限；视线上有行星时实际上限更小（见
	 * ComputeOrbitDistanceLimitsCm）—— 拉远不能穿过行星。
	 */
	UPROPERTY(EditAnywhere, Category = "StarMap")
	double MaxOrbitDistanceCm = 5.0e7;

	/**
	 * FocusOn 的平滑拉远时长（秒）。0.8 秒足够让 LOD 逐帧退档，又不至于让
	 * 玩家觉得「按了 M 还在等」。
	 *
	 * 距离按**对数**插值而不是线性：LOD 需求按距离的数量级变化，线性插值会把
	 * 时间耗在远处、近处几帧就冲过去，LOD 仍是一帧塌好几档。对数插值让每帧的
	 * 距离倍率恒定，档位平滑地一档一档退。
	 */
	UPROPERTY(EditAnywhere, Category = "StarMap", meta = (ClampMin = "0.01"))
	float SmoothFocusSeconds = 0.8f;

	/**
	 * 退图的视点混合时长（秒）。0 = 直接切回去，不做混合。
	 *
	 * 混合的起点是仍被 Possess 的这台相机（CalcCamera 每帧还在产出画面），终点是
	 * 进场 Pawn 的相机；引擎逐帧插两条 FMinimalViewInfo，位置 / 朝向 / FOV 一起过渡。
	 * 混合走完才 Possess —— Possess 会立刻把视点设成返回 Pawn，提前调用等于把混合掐掉。
	 *
	 * 配置放在相机上而不是控制器上：混合的是**这台相机**，按星球在
	 * BP_StarMapCameraPawn 里可以各配各的。
	 */
	UPROPERTY(EditAnywhere, Category = "StarMap", meta = (ClampMin = "0.0"))
	float ExitBlendSeconds = 0.5f;
};
