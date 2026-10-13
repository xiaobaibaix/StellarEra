// Copyright Voxel Plugin SAS. All Rights Reserved.
// 程序化球状行星生成器（纯球体）。

#pragma once

#include "CoreMinimal.h"
#include "VoxelMinimal.h"
#include "VoxelGenerators/VoxelGeneratorHelpers.h"
#include "VoxelPlanetGenerator.generated.h"

class UVoxelPlanetGenerator;

/**
 * 程序化球状行星生成器。
 *
 * 世界是一个有向距离场：value = |P - Center| - Radius。
 * 负值为实体，正值为空，等值面因此是一个严格的球面。
 *
 * 这是纯 C++ 生成器：不使用 Voxel Graph，因此在插件的免费版中也能工作。
 */
UCLASS(Blueprintable)
class UVoxelPlanetGenerator : public UVoxelGenerator
{
	GENERATED_BODY()

public:
	//~ Begin UVoxelGenerator Interface
	virtual TVoxelSharedRef<FVoxelGeneratorInstance> GetInstance() override;
	//~ End UVoxelGenerator Interface

	/** 行星半径（体素数）。世界空间原点位于 Center 处。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet", meta = (ClampMin = "16"))
	float Radius = 1000.f;

	/** 行星球心（体素坐标）。便于在同一个世界里摆放多颗行星。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
	FVector Center = FVector::ZeroVector;

	/** 整颗球的表面颜色，以顶点色传给材质。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
	FLinearColor SurfaceColor = FLinearColor(0.18f, 0.42f, 0.16f);
};

class FVoxelPlanetGeneratorInstance : public TVoxelGeneratorInstanceHelper<FVoxelPlanetGeneratorInstance, UVoxelPlanetGenerator>
{
public:
	using Super = TVoxelGeneratorInstanceHelper<FVoxelPlanetGeneratorInstance, UVoxelPlanetGenerator>;

	explicit FVoxelPlanetGeneratorInstance(UVoxelPlanetGenerator& MyGenerator);

	//~ Begin FVoxelGeneratorInstance Interface
	v_flt GetValueImpl(v_flt X, v_flt Y, v_flt Z, int32 LOD, const FVoxelItemStack& Items) const;
	FVoxelMaterial GetMaterialImpl(v_flt X, v_flt Y, v_flt Z, int32 LOD, const FVoxelItemStack& Items) const;

	TVoxelRange<v_flt> GetValueRangeImpl(const FVoxelIntBox& Bounds, int32 LOD, const FVoxelItemStack& Items) const;

	virtual FVector GetUpVector(v_flt X, v_flt Y, v_flt Z) const override final;
	//~ End FVoxelGeneratorInstance Interface

private:
	const float Radius;
	const FVector Center;
	const FLinearColor SurfaceColor;
};
