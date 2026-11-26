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

/**
 * 星图 rig：挂在相机 Pawn 上，进图后接管该 Pawn 的相机。
 * 位置在 CalcView 现算（子系统在 TickObjects 写回位置后才调 CalcCamera），所以不 Tick。
 */
UCLASS(ClassGroup = (StellarEra), meta = (BlueprintSpawnableComponent))
class STELLARERAPAWNS_API UStarMapRigComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UStarMapRigComponent();

	//~ Begin UActorComponent Interface
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End UActorComponent Interface

	/** 源 Pawn（飞船 / 角色）：编辑组件、退场相机、调用器搬运都从它取。 */
	void SetSourcePawn(APawn* InSourcePawn);
	APawn* GetSourcePawn() const { return SourcePawn.Get(); }

	/** 由控制器在 Possess 相机 Pawn 前调用；ViewLocation / ViewRotation 是进图前的视点。 */
	void EnterFrom(APawn* InSourcePawn, AActor* Focus,
		const FVector& ViewLocation, const FRotator& ViewRotation);

	void Exit();
	bool IsExiting() const { return bExiting; }

	/** 球心距离最近的天体；列表为空或全失效返回 nullptr。 */
	static AActor* FindNearestBody(const FVector& FromLocation, const TArray<AActor*>& Bodies);

	bool IsActive() const { return bActive; }

	/** 相机 Pawn 的 CalcCamera 在激活 / 退场混合时调用；OutResult 已由 Super 填好 FOV / 后期。 */
	void CalcView(float DeltaSeconds, FMinimalViewInfo& OutResult);

	void OrbitLook(float DeltaYaw, float DeltaPitch);
	void AimLook(float DeltaYaw, float DeltaPitch);
	void AddZoom(float Value);

	/** 左键拾取天体的像素容差。星图输入组件与 HUD 悬停查询共用。 */
	float GetSelectionPixelRadius() const { return SelectionPixelRadius; }

	/** 拉远上限（到焦点球心，厘米）。控制器与测试用；参数本身仍是 EditAnywhere。 */
	double GetMaxOrbitDistanceCm() const { return MaxOrbitDistanceCm; }

	/** 绕行灵敏度倍率上限，见 OrbitLookScale。 */
	double GetMaxOrbitLookScale() const { return MaxOrbitLookScale; }

	/** 源 Pawn（飞船 / 角色）上的行星编辑组件，缺省回退 owner。 */
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
	/** Possess 前后把宿主 Pawn 挂到焦点天体上；Focus 失效时不动。 */
	void AttachOwnerToFocus(AActor* Focus);

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

	/** 源 Pawn：相机 Pawn 被 Possess 后，物理与编辑仍归它。 */
	TWeakObjectPtr<APawn> SourcePawn;

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

	/** 拉远上限。地球量级（半径 6.4e8 cm）下 1e9 只到 1.57R，3R 聚焦会被它夹住。 */
	UPROPERTY(EditAnywhere, Category = "StarMap")
	double MaxOrbitDistanceCm = 1.0e13;

	UPROPERTY(EditAnywhere, Category = "StarMap", meta = (ClampMin = "0.01"))
	float SmoothFocusSeconds = 0.8f;

	UPROPERTY(EditAnywhere, Category = "StarMap", meta = (ClampMin = "0.0"))
	float ExitBlendSeconds = 0.5f;
};
