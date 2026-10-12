// Copyright Voxel Plugin SAS. All Rights Reserved.
// Wraps UVoxelPlanetGenerator into a placeable Actor.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VoxelPlanetActor.generated.h"

class AVoxelWorld;
class UVoxelPlanetGenerator;

/**
 * A ready-to-place planet Actor.
 *
 * Dropping this in a level creates a planet whose centre is at the actor's
 * location. The standard gizmo works: moving translates the planet, uniform
 * scaling changes its radius.
 *
 * Non-uniform scale is intentionally ignored, because it would shear the voxel
 * grid and turn the planet into a lopsided mess.
 */
UCLASS(Blueprintable)
class AVoxelPlanetActor : public AActor
{
	GENERATED_BODY()

public:
	AVoxelPlanetActor();

	//~ Begin AActor Interface
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	//~ End AActor Interface

	/** Push current settings and transform into the generator, then rebuild. */
	UFUNCTION(BlueprintCallable, Category = "Planet")
	void RebuildPlanet();

	//////////////////////////////////////////////////////////////////////////
	// Base

	/** Radius in voxels, before actor scale. 1 voxel = VoxelSize cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet", meta = (ClampMin = "16"))
	float Radius = 1000.f;

	/** Cached generator settings, exposed for per-instance tweaking. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
	int32 Seed = 1337;

	//////////////////////////////////////////////////////////////////////////
	// Continents

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer 1|Continents")
	bool bUseContinents = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer 1|Continents")
	float ContinentFrequency = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer 1|Continents")
	float ContinentHeight = 60.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer 1|Continents", meta = (ClampMin = "1", ClampMax = "8"))
	int32 ContinentOctaves = 5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer 1|Continents", meta = (ClampMin = "0", ClampMax = "4"))
	float ContinentContrast = 1.5f;

	//////////////////////////////////////////////////////////////////////////
	// Mountains

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer 2|Mountains")
	bool bUseMountains = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer 2|Mountains")
	float MountainFrequency = 6.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer 2|Mountains")
	float MountainHeight = 25.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer 2|Mountains", meta = (ClampMin = "1", ClampMax = "4"))
	float MountainSharpness = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer 2|Mountains", meta = (ClampMin = "0", ClampMax = "1"))
	float MountainLandBias = 0.7f;

	//////////////////////////////////////////////////////////////////////////
	// Detail and ravines

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer 3|Detail")
	bool bUseDetail = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer 3|Detail")
	float DetailFrequency = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer 3|Detail")
	float DetailHeight = 6.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer 4|Ravines")
	bool bUseRavines = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer 4|Ravines")
	float RavineFrequency = 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer 4|Ravines")
	float RavineDepth = 15.f;

	//////////////////////////////////////////////////////////////////////////
	// Caves and shell

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Caves", meta = (ClampMin = "0", ClampMax = "1"))
	float CaveAmount = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Caves")
	float CaveFrequency = 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shell", meta = (ClampMin = "0"))
	float ShellThickness = 0.f;

	//////////////////////////////////////////////////////////////////////////
	// Surface

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface", meta = (ClampMin = "0"))
	float SeaLevel = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface")
	FLinearColor DeepWaterColor = FLinearColor(0.02f, 0.06f, 0.25f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface")
	FLinearColor ShoreColor = FLinearColor(0.75f, 0.68f, 0.45f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface")
	FLinearColor LowlandColor = FLinearColor(0.18f, 0.42f, 0.16f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface")
	FLinearColor HighlandColor = FLinearColor(0.42f, 0.40f, 0.22f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface")
	FLinearColor PeakColor = FLinearColor(0.92f, 0.92f, 0.95f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface")
	FLinearColor CoreColor = FLinearColor(0.22f, 0.10f, 0.05f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface", meta = (ClampMin = "0"))
	float SnowLine = 0.f;

	//////////////////////////////////////////////////////////////////////////
	// Scale and performance

	/** Size of one voxel in cm. Must match the Voxel World's VoxelSize. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Scale", meta = (ClampMin = "0.0001"))
	float VoxelSize = 100.f;

	/** Render distance of the owning Voxel World, in chunks. 1 chunk = 32 voxels. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Performance", meta = (ClampMin = "1"))
	int32 RenderDistanceChunks = 64;

	/** Higher LODs sample every 2^LOD voxels. Raise for big planets. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Performance", meta = (ClampMin = "0", ClampMax = "10"))
	int32 MaxLOD = 5;

	/** The generator instance currently driving this planet. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Planet")
	TObjectPtr<UVoxelPlanetGenerator> Generator;

	/** The proxy Voxel World that renders this planet. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Planet")
	TObjectPtr<AVoxelWorld> VoxelWorld;

private:
	/** Radius in voxels including uniform actor scale. */
	float GetScaledRadiusVoxels() const;

	/** Planet centre in voxel units, derived from the actor location. */
	FVector GetCenterInVoxels() const;
};
