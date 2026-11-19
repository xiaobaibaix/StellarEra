// Copyright Voxel Plugin SAS. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "StarMapRigComponent.generated.h"

class APlayerController;
class AVoxelPlanetActor;
class UPlanetEditComponent;
class USceneComponent;
class UVoxelSimpleInvokerComponent;
struct FMinimalViewInfo;
struct FPlanetEditTarget;

/** 屏幕空间候选：天体 Actor + 投影位置 + 盘面半径。用结构体而非平行数组，避免下标错位。 */
struct FStarMapScreenCandidate
{
	AActor* Actor = nullptr;
	FVector2D ScreenPosition = FVector2D::ZeroVector;

	/** 屏幕盘面半径（像素），0 = 未算出。拾取容差取 max(固定像素容差, 本值)。 */
	float ScreenRadiusPx = 0.f;
};

/** 相机拉远路径上的挡路球体（球心 + 地表半径）。纯数据，夹取逻辑可无头测。 */
struct FStarMapObstacle
{
	FVector Center = FVector::ZeroVector;
	double RadiusCm = 0.0;
};

/**
 * 星图 rig：挂在飞船 / 角色身上，进图后接管该 Pawn 的相机。
 * 位置在 CalcView 现算（子系统在 TickObjects 写回位置后才调 CalcCamera），所以不 Tick。
 */
UCLASS(ClassGroup = (StellarEra), meta = (BlueprintSpawnableComponent))
class STELLARERAPAWNS_API UStarMapRigComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UStarMapRigComponent();

	//~ Begin UActorComponent Interface
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End UActorComponent Interface

	/** M 键：未激活则进图，激活则退图；退图过渡期间重复调用忽略。 */
	void Toggle(APlayerController* PC);

	bool IsActive() const { return bActive; }

	/** owner Pawn 的 CalcCamera 在 IsActive() 时调用；OutResult 已由 Super 填好 FOV / 后期。 */
	void CalcView(float DeltaSeconds, FMinimalViewInfo& OutResult);

	void OrbitLook(float DeltaYaw, float DeltaPitch);
	void AimLook(float DeltaYaw, float DeltaPitch);
	void AddZoom(float Value);

	/** 左键拾取天体的像素容差。星图输入组件与 HUD 悬停查询共用。 */
	float GetSelectionPixelRadius() const { return SelectionPixelRadius; }

	/** owner 上的行星编辑组件；飞船 / 角色都可能是蓝图挂的。 */
	UPlanetEditComponent* GetPlanetEditComponent() const;

	// ── 纯函数（可无头测，签名与旧相机完全一致）──

	static FRotator ClampAimOffset(const FRotator& BaseRotation, const FRotator& AimOffset, double MaxOffsetDeg);
	static double OrbitLookScale(double DistanceCm, double FocusSurfaceRadiusCm, double MinScale, double MaxScale);
	static double SilhouetteRadiusScale(double CameraDistanceCm, double BodyRadiusCm);
	static double ZoomAltitudeCm(double AltitudeCm, double StepFactor, float Value, double MinAltitudeCm, double MaxAltitudeCm);
	static double RaySphereFirstHitCm(const FVector& RayOrigin, const FVector& UnitDir, const FVector& SphereCenter, double SphereRadiusCm);
	static double LimitDistanceByObstaclesCm(
		const FVector& FocusLocation, const FVector& RayDirection,
		TArrayView<const FStarMapObstacle> Obstacles,
		double MinAltitudeCm, double MinDistanceCm, double CurrentMaxCm);
	static AActor* PickBodyAtScreenPosition(
		TArrayView<const FStarMapScreenCandidate> Candidates,
		const FVector2D& ScreenPoint, float PixelRadius);

private:
	void Enter(APlayerController* PC);
	void Exit();

	/** 焦点行星：飞船取最近行星，角色取脚下那颗；都没有返回空（不进图）。 */
	AVoxelPlanetActor* ResolveFocusPlanet() const;

	/** 当前选中的天体（每帧从子系统读，不自己存）。 */
	AActor* ResolveSelectedBody() const;

	/** 焦点在「焦点 → 相机」方向的表面半径：体素行星查真实地表，非体素用碰撞半径。 */
	double ResolveFocusSurfaceRadiusCm(AActor* Focus) const;

	/** 当前视线下相机到焦点距离的合法区间；**每帧重算**（行星在动）。 */
	void ComputeOrbitDistanceLimitsCm(AActor* Focus, double& OutMinCm, double& OutMaxCm) const;

	/** 屏幕射线 → 最近被击中的体素行星表面点。 */
	bool ResolveEditTargetByPlanets(const FVector& RayOrigin, const FVector& RayDirection,
		float MaxDistanceCm, FPlanetEditTarget& OutTarget) const;

	FVector ResolveOwnerCameraLocation() const;
	FRotator ResolveOwnerCameraRotation() const;

	void BeginInvokerFollow();
	void UpdateInvokerFollow();
	void EndInvokerFollow();

	void ApplyStarMapEditSettings();
	void RestoreEditSettings();

	/** 焦点天体；进场与换选中都用它。 */
	void FocusOn(AActor* Body, double DistanceCm);

	bool bActive = false;
	bool bExiting = false;
	float ExitElapsedSeconds = 0.f;

	// ── 运行时状态（原相机字段，原样搬）──
	FRotator OrbitRotation = FRotator::ZeroRotator;
	FRotator AimOffset = FRotator::ZeroRotator;
	double OrbitDistanceCm = 0.0;
	bool bSmoothFocusActive = false;
	double SmoothFocusStartCm = 0.0;
	double SmoothFocusTargetCm = 0.0;
	float SmoothFocusElapsedSeconds = 0.f;
	FVector CameraLocation = FVector::ZeroVector;
	FRotator LastViewRotation = FRotator::ZeroRotator;
	TWeakObjectPtr<AActor> LastSeenSelected;

	// ── 退图过渡 ──
	FVector ExitStartLocation = FVector::ZeroVector;
	FRotator ExitStartRotation = FRotator::ZeroRotator;

	// ── 体素 invoker 跟随（只搬 bUseForCollisions == false 的）──
	struct FInvokerSnapshot
	{
		USceneComponent* Component = nullptr;
		FTransform RelativeTransform = FTransform::Identity;
	};
	TArray<FInvokerSnapshot> FollowedInvokers;

	// ── 编辑组件参数快照 ──
	struct FEditSettingsSnapshot
	{
		bool bValid = false;
		int32 MinSize = 1;
		int32 MaxSize = 8;
		int32 Size = 1;
		int32 Step = 1;
		float MaxDistanceCm = 1.0e5f;
		bool bMultiThreaded = false;
		FString PlaceHint;
	};
	FEditSettingsSnapshot SavedEditSettings;

	// ── 可调参数（原相机 UPROPERTY，原样搬）──
	UPROPERTY(EditAnywhere, Category = "StarMap")
	double ZoomStepFactor = 1.15;

	UPROPERTY(EditAnywhere, Category = "StarMap", meta = (ClampMin = "0.0"))
	double PlanetFocusRadiusScale = 3.0;

	UPROPERTY(EditAnywhere, Category = "StarMap", meta = (ClampMin = "0.0"))
	float SelectionPixelRadius = 20.f;

	UPROPERTY(EditAnywhere, Category = "StarMap", meta = (ClampMin = "0.0"))
	double MinOrbitLookScale = 0.05;

	UPROPERTY(EditAnywhere, Category = "StarMap", meta = (ClampMin = "1.0"))
	double MaxOrbitLookScale = 2.0;

	UPROPERTY(EditAnywhere, Category = "StarMap", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	double MaxAimOffsetDeg = 90.0;

	UPROPERTY(EditAnywhere, Category = "StarMap")
	double MinSurfaceAltitudeCm = 5.0e3;

	UPROPERTY(EditAnywhere, Category = "StarMap")
	double MinFocusDistanceCm = 5.0e2;

	UPROPERTY(EditAnywhere, Category = "StarMap")
	double MaxOrbitDistanceCm = 1.0e9;

	UPROPERTY(EditAnywhere, Category = "StarMap", meta = (ClampMin = "0.01"))
	float SmoothFocusSeconds = 0.8f;

	UPROPERTY(EditAnywhere, Category = "StarMap", meta = (ClampMin = "0.0"))
	float ExitBlendSeconds = 0.5f;
};
