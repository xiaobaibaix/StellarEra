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
};

/**
 * 星图模式的相机。进入星图时由 AStellarEraPlayerController 创建并 Possess —— 该接线由 T4 完成。
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
	 * 返回离 ScreenPoint 最近、且距离 ≤ PixelRadius 的候选。都不在容差内、
	 * 或候选全为空时返回 nullptr。**边界含**：距离恰好等于 PixelRadius 算命中。
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

	/** 相机到焦点的距离（厘米）。 */
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
	 * 检出变化时从**当前** CameraLocation 反推 OrbitRotation / OrbitDistanceCm，
	 * 于是相机只是「开始跟着」新焦点，位置和朝向都不跳（spec §5.2）。
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
