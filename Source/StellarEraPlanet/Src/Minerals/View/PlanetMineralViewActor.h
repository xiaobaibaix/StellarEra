// Copyright Voxel Plugin SAS. All Rights Reserved.
// 矿物透视的观察体：半透明球壳 + 每个元素一层等值面矿体。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PlanetGeologyTypes.h"
#include "PlanetMineralSurface.h"
#include "PlanetMineralViewActor.generated.h"

class AVoxelMineralFieldActor;
class AVoxelWorld;
class UDynamicMeshComponent;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;
struct FPlanetMineralVoxelParams;

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
	 * 按元素建 N 个体素世界。失败时销毁已建的体素世界并返回 false，但**球壳与
	 * bHasBuilt 会留下** —— 本 Actor 从此不可重建，调用方必须 Destroy() 本 Actor。
	 *
	 * ParamsByElement 与 Elements 的下标必须同为**运行期元素下标**
	 * （FPlanetGeologyField::GetSampledElementDefinitions 的顺序）。空槽照样
	 * 建一个空世界 —— 下标才能与图例、数字键对齐。
	 */
	bool BuildVoxelWorlds(
		const TArray<FPlanetMineralVoxelParams>& ParamsByElement,
		const AVoxelWorld& PlanetWorld,
		const FLinearColor& ShellColor,
		float SurfaceRadiusCm,
		const TArray<FPlanetGeologyElement>& Elements);

	/** 销毁所有体素世界。重复调用安全。 */
	void ReleaseAllFields();

	/** 每帧把观察体钉到行星球心，并同步每个体素世界的位置。 */
	void SyncToPlanetLocation(const FVector& PlanetCenterWorld);

	/** 某个元素的体素世界，仅供测试与调试。越界返回 nullptr。 */
	AVoxelMineralFieldActor* GetElementField(int32 Index) const;

	/** 改某个元素的颜色。世界未建时返回 false。 */
	bool SetElementColor(int32 Index, const FLinearColor& Color);

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

	int32 NumElements() const;
	bool IsElementVisible(int32 Index) const;
	void SetElementVisible(int32 Index, bool bVisible);
	void SetAllElementsVisible(bool bVisible);

	/**
	 * 兜底之一：Destroy() 与关卡卸载。UWorld::DestroyActor 会调 Destroyed()
	 * （LevelActor.cpp:926），**编辑器世界与游戏世界都会触发** —— 编辑器预览是
	 * 本功能的主路径，靠的就是这个钩子。
	 */
	virtual void Destroyed() override;

	/**
	 * 兜底之二：无缝切图这类「走 EndPlay 而不走 Destroy」的路径。编辑器世界不跑
	 * BeginPlay，AActor::RouteEndPlay 会按 ActorHasBegunPlay 直接跳过
	 * （Actor.cpp:3221-3231），所以编辑器预览保护不到 —— 那条由 Destroyed() 负责。
	 * 两个钩子都幂等，先后触发是空操作。
	 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

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

	/** 下标 = 运行期元素下标。与 MineralMeshes 是两条互斥路径，同一时刻只有一条非空。 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<AVoxelMineralFieldActor>> ElementFields;

	/** 本观察体自己的根位置（= 行星球心），体素世界逐个同步到这里。 */
	FVector LastPlanetCenterWorld = FVector::ZeroVector;

	/**
	 * Build / BuildVoxelWorlds 已经建过组件与世界。两条路径都不可重入：组件名固定，
	 * 第二次 NewObject 会在同一个 Outer 上撞名（致命错误），体素世界也会重复 spawn。
	 * 所以两个入口都 ensureMsgf 后早退，且两处都不清这个标志 —— 一旦置真就是终态。
	 */
	bool bHasBuilt = false;

	/**
	 * 建半透明球壳。两条渲染路径共用：粘贴两份的话，它们会在 Task 7 删掉
	 * marching cubes 之前一直漂移。
	 *
	 * SphereMesh 与 GhostMaterial 由调用方保证非空（上游已判空并打过日志）。
	 * 动态材质创建失败只打 Warning，不算构建失败 —— 球壳会留在默认材质上。
	 */
	void CreateShell(UStaticMesh* SphereMesh, UMaterialInterface* GhostMaterial,
		const FLinearColor& ShellColor, float SurfaceRadiusCm);
};
