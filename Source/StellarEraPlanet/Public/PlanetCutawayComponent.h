// Copyright Voxel Plugin SAS. All Rights Reserved.
// 行星矿物剖面：切面状态、剖面纹理、圆盘、旋转环、全局裁剪参数。

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "PlanetCutawayMath.h"
#include "PlanetGeologyField.h"
#include "PlanetCutawayComponent.generated.h"

class AVoxelPlanetActor;
class ULineBatchComponent;
class UMaterialInstanceDynamic;
class UMaterialParameterCollection;
class UStaticMesh;
class UStaticMeshComponent;
class UTexture2D;

/**
 * 一颗行星的剖面视图。观察性质：不改体素、不碰碰撞、不写存档。
 *
 * 生命周期：Activate 时加载资产并创建圆盘/圆环，Deactivate 时把全局 MPC 的
 * Active 写 0、隐藏子组件、恢复大气；EndPlay 兜底 Deactivate 并销毁子对象。
 *
 * 每帧 Tick 做三件事：按当前相机重算 ClipNormal、同步圆盘与圆环、限频重渲染预览。
 */
UCLASS(ClassGroup = (StellarEra), meta = (BlueprintSpawnableComponent))
class STELLARERAPLANET_API UPlanetCutawayComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UPlanetCutawayComponent();

	//~ Begin UActorComponent Interface
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
	//~ End UActorComponent Interface

	/** 基类的 Activate(bool) 仍然可用：本类的同名重载只是多了一种入口。 */
	using USceneComponent::Activate;

	/** 进入剖面。法线是「平面法线」，组件会用相机位置把它翻到相机侧。 */
	bool Activate(AVoxelPlanetActor* InPlanet, const FVector& InitialPlaneNormal);

	/** 退出剖面并恢复全部视觉。重复调用安全。 */
	void Deactivate();

	bool IsActive() const { return bActive; }

	/** 绕世界轴旋转切面；DeltaDegrees 为正 = 右手方向。 */
	void RotatePlane(ESectionRing Ring, float DeltaDegrees);

	/** 立即渲染一帧剖面：Resolution ≥ FinalResolution 用 Final，否则用 Preview。 */
	void RequestSectionRender(int32 Resolution);

	/** 输出三根圆环的世界折线（各 128 点）；未激活时输出空数组。 */
	void GetRingWorldPoints(TArray<FSectionRingWorldPolyline>& OutRings) const;

	AVoxelPlanetActor* GetCutawayPlanet() const { return Planet.Get(); }
	TSharedPtr<const FPlanetGeologyField> GetGeologyField() const { return CachedGeology; }

	UPROPERTY(EditAnywhere, Category = "Planet|Cutaway", meta = (ClampMin = "16"))
	int32 FinalResolution = 512;

	UPROPERTY(EditAnywhere, Category = "Planet|Cutaway", meta = (ClampMin = "16"))
	int32 PreviewResolution = 96;

	/** 圆环半径 / 剖面半径。 */
	UPROPERTY(EditAnywhere, Category = "Planet|Cutaway")
	float RingRadiusScale = 1.15f;

	/** 圆盘沿 ClipNormal 向相机侧的偏移（厘米），避免与体素表面共面闪烁。 */
	UPROPERTY(EditAnywhere, Category = "Planet|Cutaway")
	float DiscOffsetCm = 2.f;

	/** 剖面半径 / 基准表面半径（spec §3.1）。 */
	static constexpr float SectionRadiusScale = 1.05f;

private:
	bool EnsureViewAssets();
	void ReleaseViewAssets();

	/** 把球心/ClipNormal/Active 与内球半径写进两个 MPC。 */
	void ApplyClipMask();
	void UpdateDiscTransform();
	void UpdateRingGeometry();
	void RenderSection(int32 Resolution);
	void UploadSection(UTexture2D* Target, TArray<FColor>& Buffer, int32 Resolution);

	float ComputeSectionRadiusCm() const;
	FVector GetCameraLocationOrDefault() const;

	bool bActive = false;
	TWeakObjectPtr<AVoxelPlanetActor> Planet;
	TSharedPtr<const FPlanetGeologyField> CachedGeology;

	FQuat PlaneOrientation = FQuat::Identity;
	FVector ClipNormal = FVector::UpVector;
	FVector AxisU = FVector::ForwardVector;
	FVector AxisV = FVector::RightVector;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> SectionMesh;

	UPROPERTY(Transient)
	TObjectPtr<ULineBatchComponent> SectionRings;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SectionMaterialInstance;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> FinalTexture;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> PreviewTexture;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialParameterCollection> CutawayOriginMpc;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialParameterCollection> CutawayNormalMpc;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> SectionPlaneMesh;

	/**
	 * UpdateTextureRegions 是异步的：像素缓冲必须活到渲染线程读完。
	 * Final（512²）与 Preview（96²）各两份轮换 —— 不能共用同一个数组，
	 * 也不能在下一份写入时复用上一份上传还在读的内存。
	 */
	TArray<FColor> FinalPixels[2];
	TArray<FColor> PreviewPixels[2];
	int32 FinalPixelsIndex = 0;
	int32 PreviewPixelsIndex = 0;

	float SecondsSincePreviewRender = 0.f;
	float PendingRotationDegrees = 0.f;

	bool bSavedAtmosphereEnabled = true;
	bool bSavedAtmosphereVisible = true;

	/** 运行期改分辨率属性时只警告一次：纹理沿用创建时的尺寸，不逐帧重建。 */
	bool bWarnedResolutionMismatch = false;

	static constexpr int32 RingSegments = 128;
};
