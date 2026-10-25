// Copyright Voxel Plugin SAS. All Rights Reserved.
// 矿物透视的观察体：半透明球壳 + 每个元素一组不透明方块。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PlanetGeologyTypes.h"
#include "PlanetMineralViewActor.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;
struct FPlanetMineralBlock;

/**
 * 观察体。
 *
 * **必须独立 spawn，不能挂在行星 Actor 下面**：进入模式会
 * SetActorHiddenInGame(true) 隐藏行星，而 AActor::IsHidden 会让**所有**子组件的
 * USceneComponent::ShouldRender 返回 false（SceneComponent.cpp:3517），球壳与方块
 * 会跟着一起消失。
 *
 * 方块位置相对本 Actor 原点（= 球心），组件不随行星旋转缩放。
 */
UCLASS()
class STELLARERAPLANET_API APlanetMineralViewActor : public AActor
{
	GENERATED_BODY()

public:
	APlanetMineralViewActor();

	/**
	 * 建球壳、按元素建 ISM 并放方块。
	 *
	 * BlocksByElement 与 Elements 的下标必须同为**运行期元素下标**
	 * （即 FPlanetGeologyField::GetSampledElementDefinitions 的顺序）。
	 * 材质/网格加载失败或元素数与分组数不一致时返回 false 并打 Error。
	 */
	bool Build(const TArray<TArray<FPlanetMineralBlock>>& BlocksByElement,
		const FLinearColor& ShellColor, float SurfaceRadiusCm, float BlockSizeCm,
		const TArray<FPlanetGeologyElement>& Elements);

	int32 NumElements() const { return BlockMeshes.Num(); }
	bool IsElementVisible(int32 Index) const;
	void SetElementVisible(int32 Index, bool bVisible);
	void SetAllElementsVisible(bool bVisible);

	/** 球壳组件，仅供测试与调试。 */
	UStaticMeshComponent* GetShellMesh() const { return ShellMesh; }

private:
	UPROPERTY(VisibleAnywhere, Category = "MineralView")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> ShellMesh;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ShellMaterial;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> BlockMeshes;
};
