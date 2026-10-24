// Copyright Voxel Plugin SAS. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "StarMapCameraPawn.generated.h"

class AVoxelPlanetActor;
class UCameraComponent;
class UCosmosSimulationSubsystem;
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

/**
 * 星图模式的相机。进入星图时由 AStellarEraPlayerController 创建并 Possess。
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
class AStarMapCameraPawn : public APawn
{
	GENERATED_BODY()

public:
	AStarMapCameraPawn();

	/** 进入星图模式时由控制器调用：把相机原地钉住，并重置为「未选中」。 */
	void EnterFrom(const FVector& InCameraLocation, const FRotator& InCameraRotation);

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

	/** 鼠标位移 → 绕焦点转视角。单位是度，来自控制器的 LookSensitivity。 */
	void OrbitLook(float DeltaYaw, float DeltaPitch);

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

	/** 相机，同时是根组件。不挂 SpringArm —— 太空里没有东西要避。 */
	UPROPERTY(VisibleAnywhere, Category = "StarMap")
	TObjectPtr<UCameraComponent> Camera;

	/**
	 * 体素调用器：三档 LOD，配置与飞船（ASpaceshipPawn）同一套。
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
	 * 三档只做 LOD；碰撞只开在细气泡上（碰撞细分路径不读 LODToSet，锁死
	 * Height 0，开大了会顶爆显存，详见 SpaceshipPawn.cpp 构造函数的注释）。
	 */
	UPROPERTY(VisibleAnywhere, Category = "StarMap|Voxel")
	TObjectPtr<UVoxelSimpleInvokerComponent> VoxelInvoker;

	UPROPERTY(VisibleAnywhere, Category = "StarMap|Voxel")
	TObjectPtr<UVoxelSimpleInvokerComponent> VoxelInvokerLOD3;

	UPROPERTY(VisibleAnywhere, Category = "StarMap|Voxel")
	TObjectPtr<UVoxelSimpleInvokerComponent> VoxelInvokerLOD5;

	// ── 运行时状态 ──

	/** 从哪个方向看（yaw / pitch）。相机位置 = 焦点 − 这个方向 × 距离。 */
	UPROPERTY(Transient, VisibleAnywhere, Category = "StarMap|State")
	FRotator OrbitRotation = FRotator::ZeroRotator;

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
};
