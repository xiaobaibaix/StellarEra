// Copyright Voxel Plugin SAS. All Rights Reserved.
// 矿物透视的观察体：半透明球壳 + 每个元素一层等值面矿体。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PlanetGeologyTypes.h"
#include "PlanetMineralSurface.h"
#include "PlanetMineralViewActor.generated.h"

class UDynamicMeshComponent;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;

/**
 * 观察体。
 *
 * **必须独立 spawn，不能挂在行星 Actor 下面**：进入模式会
 * SetActorHiddenInGame(true) 隐藏行星，而 AActor::IsHidden 会让**所有**子组件的
 * USceneComponent::ShouldRender 返回 false（SceneComponent.cpp:3517），球壳与矿体
 * 会跟着一起消失。
 *
 * 矿体顶点相对本 Actor 原点（= 球心），组件不随行星旋转缩放。
 */
UCLASS()
class STELLARERAPLANET_API APlanetMineralViewActor : public AActor
{
	GENERATED_BODY()

public:
	APlanetMineralViewActor();

	/**
	 * 建球壳、按元素建动态网格组件。
	 *
	 * MeshesByElement 与 Elements 的下标必须同为**运行期元素下标**
	 * （即 FPlanetGeologyField::GetSampledElementDefinitions 的顺序）。
	 * 空网格照样占一个组件槽，下标才能与图例、数字键对齐。
	 * 网格/材质加载失败或两者长度不一致时返回 false 并打 Error。
	 */
	bool Build(const TArray<FPlanetMineralMesh>& MeshesByElement, const FLinearColor& ShellColor,
		float SurfaceRadiusCm, const TArray<FPlanetGeologyElement>& Elements);

	int32 NumElements() const { return MineralMeshes.Num(); }
	bool IsElementVisible(int32 Index) const;
	void SetElementVisible(int32 Index, bool bVisible);
	void SetAllElementsVisible(bool bVisible);

	/** 球壳组件，仅供测试与调试。 */
	UStaticMeshComponent* GetShellMesh() const { return ShellMesh; }

	/** 某个元素的矿体组件，仅供测试与调试。越界返回 nullptr。 */
	UDynamicMeshComponent* GetElementMesh(int32 Index) const;

private:
	UPROPERTY(VisibleAnywhere, Category = "MineralView")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> ShellMesh;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ShellMaterial;

	/** 下标 = 运行期元素下标；空元素对应一个没有三角形的组件。 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UDynamicMeshComponent>> MineralMeshes;
};
