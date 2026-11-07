// 一个元素一个体素世界。继承 AVoxelWorld（与 AVoxelPlanetActor 同做法），「世界就是本 Actor」，没有子对象层。
// 世界参数在 CreateForElement 里从行星整份复制，两边体素索引一一对应 —— 生成器因此不需要坐标换算。

#pragma once

#include "CoreMinimal.h"
#include "Minerals/View/PlanetMineralVoxelSampling.h"
#include "VoxelWorld.h"
#include "VoxelMineralFieldActor.generated.h"

class UMaterialInstanceDynamic;
class UPlanetMineralVoxelGenerator;

UCLASS()
class STELLARERAPLANET_API AVoxelMineralFieldActor : public AVoxelWorld
{
	GENERATED_BODY()

public:
	AVoxelMineralFieldActor();

	/**
	 * 复制行星的世界参数（VoxelSize / RenderOctreeDepth / WorldSizeInVoxel / 自定义边界 / LOD / Transform，不含 SaveObject）、
	 * 挂生成器并建世界。失败时打 Error 返回 false，调用方应 Destroy 本 Actor。
	 * @param BaseMaterial 创建 MID 用的父材质（M_MineralBlock）。
	 */
	bool CreateForElement(
		const FPlanetMineralVoxelParams& Params,
		const FLinearColor& Color,
		const AVoxelWorld& PlanetWorld,
		UMaterialInterface* BaseMaterial);

	/** 销毁体素世界。重复调用安全。 */
	void DestroyField();

	/** 改颜色。世界已建时只换材质参数，不需要重建。 */
	void SetShellColor(const FLinearColor& Color);

	UMaterialInstanceDynamic* GetFieldMaterial() const { return FieldMaterial; }

	/** 体素世界是否已建。 */
	bool IsFieldCreated() const { return IsCreated(); }

private:
	UPROPERTY(Transient)
	TObjectPtr<UPlanetMineralVoxelGenerator> MineralGenerator;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FieldMaterial;
};
