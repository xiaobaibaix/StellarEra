// Copyright Voxel Plugin SAS. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "StarMapCameraPawn.generated.h"

class UCameraComponent;
class UCosmosSimulationSubsystem;

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

	/** 鼠标位移 → 绕焦点转视角。单位是度，来自控制器的 LookSensitivity。 */
	void OrbitLook(float DeltaYaw, float DeltaPitch);

	/** 滚轮 → 按倍率缩放与焦点的距离。Value 每格 ±1。 */
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

	//~ Begin AActor Interface
	virtual void CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult) override;
	//~ End AActor Interface

protected:
	/** 相机，同时是根组件。不挂 SpringArm —— 太空里没有东西要避。 */
	UPROPERTY(VisibleAnywhere, Category = "StarMap")
	TObjectPtr<UCameraComponent> Camera;

	// ── 运行时状态 ──

	/** 从哪个方向看（yaw / pitch）。相机位置 = 焦点 − 这个方向 × 距离。 */
	UPROPERTY(Transient, VisibleAnywhere, Category = "StarMap|State")
	FRotator OrbitRotation = FRotator::ZeroRotator;

	/**
	 * 相机到焦点的距离（厘米）。
	 *
	 * **只在发生过一次选中之后才有意义。** 初始值 0 是哨兵，不影响任何可观测结果：
	 * 未选中时 CalcCamera 不更新 CameraLocation（也就用不到它），而第一次选中时
	 * CalcCamera 会用「当前相机位置 → 焦点」的实际距离（夹到 Min/Max 之间）把它整个覆盖掉。
	 * EnterFrom 不重置它 —— 重置成什么都不影响结果，因为首次选中必然覆盖。
	 *
	 * 因此：不要读它来初始化「进星图时的距离」；也不要指望选中之前调 AddZoom
	 * 有可观测效果（那期间它被夹到 MinOrbitDistanceCm，但没人看）。
	 */
	UPROPERTY(Transient, VisibleAnywhere, Category = "StarMap|State")
	double OrbitDistanceCm = 0.0;

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
	 * 最近距离（厘米）。世界尺度：1 AU = CentimetersPerAu = 1e6 厘米；
	 * 默认体素行星的半径 = Radius 100 体素 × VoxelSize 100 厘米 = 1e4 厘米。
	 * 所以 2e4 = 两颗行星半径，够近但不会钻进地里。
	 */
	UPROPERTY(EditAnywhere, Category = "StarMap")
	double MinOrbitDistanceCm = 2.0e4;

	/** 最远距离（厘米）。5e7 厘米 = 50 AU，够从一颗行星看到整个星系。 */
	UPROPERTY(EditAnywhere, Category = "StarMap")
	double MaxOrbitDistanceCm = 5.0e7;
};
