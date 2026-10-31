// Copyright Voxel Plugin SAS. All Rights Reserved.
// 编辑模式下贴在行星表面的经纬网格。

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/TimerHandle.h"
#include "PlanetSurfaceGridComponent.generated.h"

class AVoxelPlanetActor;
class ULineBatchComponent;

/**
 * 采样式表面网格。
 *
 * 不能用解析地形半径：GetSurfaceRadiusCmAtDirection 读不到体素编辑，
 * 挖出的洞和加上的凸起在它眼里不存在。这里以解析半径为初值，沿半径方向
 * 用 FVoxelData::GetValue 找符号翻转，因此网格跟着真实体素表面走。
 *
 * 采样与缓存都在**体素空间**（VoxelSize 无关）；绘制时每帧乘 VoxelSize、
 * 再乘行星变换。行星在 N 体里每帧移动，所以缓存不能是世界坐标。
 */
UCLASS(ClassGroup = (StellarEra), meta = (BlueprintSpawnableComponent))
class STELLARERAPLANET_API UPlanetSurfaceGridComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPlanetSurfaceGridComponent();

	//~ Begin UActorComponent Interface
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
	//~ End UActorComponent Interface

	/** 显示 / 隐藏。显示时立即重建一次缓存。 */
	void SetGridVisible(bool bVisible);
	bool IsGridVisible() const { return bGridVisible; }

	/** 体素被编辑后调用：按 RebuildDebounceSeconds 防抖重建缓存。 */
	void NotifyVoxelEdited();

	/** 经纬线步长（度）。 */
	UPROPERTY(EditAnywhere, Category = "Planet|EditGrid",
		meta = (ClampMin = "1.0", ClampMax = "90.0"))
	float GridStepDegrees = 5.f;

	/** 沿半径方向的额外外移（厘米），避免线被地表网格 z-fighting 吃掉。 */
	UPROPERTY(EditAnywhere, Category = "Planet|EditGrid")
	float SurfaceOffsetCm = 3.f;

	/** 从解析半径向外 / 向内最多扫多少步（每步 2 体素）。 */
	UPROPERTY(EditAnywhere, Category = "Planet|EditGrid", meta = (ClampMin = "1"))
	int32 MaxSearchSteps = 32;

	/** 编辑后重建网格的防抖时间（秒）。 */
	UPROPERTY(EditAnywhere, Category = "Planet|EditGrid", meta = (ClampMin = "0.0"))
	float RebuildDebounceSeconds = 0.3f;

	UPROPERTY(EditAnywhere, Category = "Planet|EditGrid")
	FLinearColor GridColor = FLinearColor(1.f, 0.85f, 0.30f, 0.35f);

	UPROPERTY(EditAnywhere, Category = "Planet|EditGrid", meta = (ClampMin = "0.1"))
	float LineThickness = 1.5f;

private:
	AVoxelPlanetActor* GetPlanet() const;
	void EnsureLineBatch();
	void RebuildCache();
	void DrawCachedLines();

	UPROPERTY(Transient)
	TObjectPtr<ULineBatchComponent> LineBatch = nullptr;

	FTimerHandle RebuildTimer;

	/**
	 * 每个顶点的单位方向，**体素空间**。
	 *
	 * 不变量：这些方向只在行星 Actor 无旋转时与世界方向等价。缓存、采样与
	 * GetSurfaceRadiusCmAtDirection 的调用都依赖这一点；若行星将来会自转，
	 * 需要在边界处做世界 ↔ 体素方向变换。
	 */
	TArray<FVector> CachedDirections;

	/** 每个顶点在体素空间中的位置（体素单位）。与 CachedDirections 等长。 */
	TArray<FVector> CachedVoxelPositions;

	/** 线段两端在 CachedDirections 里的下标。 */
	TArray<FIntPoint> CachedSegments;

	bool bGridVisible = false;
};
