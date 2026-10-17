// 宇宙相机 —— 戴森球计划式的自由观察相机。
//
// 设计目标：拖着看整个星系，而不是控制一个人物。
// 因此不做角色控制器，不做重力，只做「自由飞行观察者」。
//
// 操作：
//   W / S      前进 / 后退
//   A / D      左移 / 右移
//   Q / E      下降 / 上升
//   鼠标右键拖动  转动视角
//   鼠标滚轮     调整移动速度档位
//   Shift       临时加速
//   Ctrl        临时减速
//   F           聚焦最近的行星（居中）
//   Tab         循环切换下一颗天体
//
// 速度是「档位」而非连续值，这点和戴森球计划一致：
// 你需要能瞬间从一颗星球挪到邻星，中间没有意义的中间档。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "CosmosCameraPawn.generated.h"

/**
 * 自由飞行相机 Pawn。
 *
 * 用 Pawn 而不是纯 CameraActor，是为了能直接挂在 PlayerController 下
 * 并自动获得输入焦点。
 */
UCLASS(Blueprintable)
class VOXELCOSMOS_API ACosmosCameraPawn : public APawn
{
	GENERATED_BODY()

public:
	ACosmosCameraPawn();

	//~ Begin APawn Interface
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void BeginPlay() override;
	//~ End APawn Interface

	//////////////////////////////////////////////////////////////////////
	// 移动

	/** 移动速度档位索引。0 = 最慢，MaxSpeedSteps-1 = 最快。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Movement", meta = (ClampMin = "0"))
	int32 SpeedStep = 6;

	/** 速度档位表，单位 AU/秒。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Movement")
	TArray<float> SpeedSteps;

	/** 视角转动灵敏度，单位度/像素。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Look")
	float LookSensitivity = 0.12f;

	/** 阻尼系数。0 = 立即停止，越大滑行越远。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Movement", meta = (ClampMin = "0.0", ClampMax = "0.99"))
	float Damping = 0.85f;

	/** Shift 加速倍率。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Movement", meta = (ClampMin = "1.0"))
	float BoostMultiplier = 8.f;

	/** Ctrl 减速倍率。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Movement", meta = (ClampMin = "0.01"))
	float PrecisionMultiplier = 0.15f;

	/** 单次滚轮调整的档位数。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Movement")
	int32 SpeedStepPerScroll = 1;

	//////////////////////////////////////////////////////////////////////
	// 聚焦

	/** 聚焦目标。设置后相机会在其附近就位。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Focus")
	TWeakObjectPtr<AActor> FocusTarget;

	/** 聚焦时相机与目标的距离，单位 AU。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Focus", meta = (ClampMin = "0.0001"))
	double FocusDistanceAu = 0.5;

	/** 每秒扫掠的最大角速度，度/秒。用于自动对准。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Focus")
	float AutoAlignDegreesPerSecond = 45.f;

	//////////////////////////////////////////////////////////////////////
	// 查询

	/** 当前实际速度档位值，单位 AU/秒。 */
	UFUNCTION(BlueprintPure, Category = "Camera")
	float GetCurrentSpeedAu() const;

	/** 把视线对准某个 Actor。 */
	UFUNCTION(BlueprintCallable, Category = "Camera")
	void LookAtActor(AActor* Target);

	/** 跳到目标附近，保持当前朝向。 */
	UFUNCTION(BlueprintCallable, Category = "Camera")
	void SnapToActor(AActor* Target);

	/** 找最近的行星并聚焦。 */
	UFUNCTION(BlueprintCallable, Category = "Camera")
	void FocusNearestBody();

	/** 聚焦列表中的下一颗天体。 */
	UFUNCTION(BlueprintCallable, Category = "Camera")
	void CycleNextFocusTarget();

	/** 注册一个可聚焦天体。GameMode 在 BeginPlay 时调用。 */
	UFUNCTION(BlueprintCallable, Category = "Camera")
	void RegisterFocusTarget(AActor* Actor);

private:
	/** 输入回调。 */
	void MoveForward(float Value);
	void MoveRight(float Value);
	void MoveUp(float Value);
	void LookAtMouse(float X, float Y);
	void OnScrollUp();
	void OnScrollDown();
	void OnBoostPressed();
	void OnBoostReleased();
	void OnPrecisionPressed();
	void OnPrecisionReleased();

	/** 焦点目标列表。 */
	UPROPERTY()
	TArray<TWeakObjectPtr<AActor>> FocusTargets;

	/** 当前索引，供 CycleNextFocusTarget 使用。 */
	int32 FocusIndex = 0;

	/** 速度倍率状态（Shift / Ctrl）。 */
	float CurrentSpeedMultiplier = 1.f;

	/** 平滑后的实际移动速度，用于阻尼。 */
	FVector SmoothedVelocity = FVector::ZeroVector;
};
