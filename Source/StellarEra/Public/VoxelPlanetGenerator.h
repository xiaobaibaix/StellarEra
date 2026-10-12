// Copyright Voxel Plugin SAS. All Rights Reserved.
// Procedural spherical planet terrain generator.

#pragma once

#include "CoreMinimal.h"
#include "VoxelMinimal.h"
#include "FastNoise/VoxelFastNoise.h"
#include "VoxelGenerators/VoxelGeneratorHelpers.h"
#include "VoxelPlanetGenerator.generated.h"

class UVoxelPlanetGenerator;

/**
 * Procedural planet terrain generator.
 *
 * The world is a signed distance field: value = |P - Center| - SurfaceRadius.
 * Negative = solid, positive = empty.
 *
 * SurfaceRadius is built by stacking four noise layers sampled on the UNIT
 * sphere (i.e. on the normalized direction vector), which is what makes the
 * terrain wrap around the planet without seams or polar pinching.
 *
 * This is a pure C++ generator: it does not use Voxel Graphs, so it works in
 * the free version of the plugin.
 */
UCLASS(Blueprintable)
class UVoxelPlanetGenerator : public UVoxelGenerator
{
	GENERATED_BODY()

public:
	//~ Begin UVoxelGenerator Interface
	virtual TVoxelSharedRef<FVoxelGeneratorInstance> GetInstance() override;
	//~ End UVoxelGenerator Interface

	/** Planet radius in voxels. World space origin sits at Center. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet", meta = (ClampMin = "16"))
	float Radius = 1000.f;

	/** Planet centre in voxel units. Lets you place several planets in one world. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
	FVector Center = FVector::ZeroVector;

	//////////////////////////////////////////////////////////////////////////
	// Layer 1- Continents

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer 1|Continents")
	bool bUseContinents = true;

	/** Low frequency: decides where landmasses are. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer 1|Continents")
	float ContinentFrequency = 2.f;

	/** Peak elevation of continents, in voxels. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer 1|Continents")
	float ContinentHeight = 60.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer 1|Continents", meta = (ClampMin = "1", ClampMax = "8"))
	int32 ContinentOctaves = 5;

	/**
	 * Pushes the continent noise towards extremes: negative values become deep
	 * ocean basins, positive values become tall continents. 0 = flat, 1 = hard.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer 1|Continents", meta = (ClampMin = "0", ClampMax = "4"))
	float ContinentContrast = 1.5f;

	//////////////////////////////////////////////////////////////////////////
	// Layer 2- Mountains

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer 2|Mountains")
	bool bUseMountains = true;

	/** Mid frequency: mountain ranges. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer 2|Mountains")
	float MountainFrequency = 6.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer 2|Mountains")
	float MountainHeight = 25.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer 2|Mountains", meta = (ClampMin = "1", ClampMax = "4"))
	float MountainSharpness = 2.f;

	/** Mountains only grow where continents are high, like real orogeny. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer 2|Mountains", meta = (ClampMin = "0", ClampMax = "1"))
	float MountainLandBias = 0.7f;

	//////////////////////////////////////////////////////////////////////////
	// Layer 3- Surface detail

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer 3|Detail")
	bool bUseDetail = true;

	/** High frequency: small scale roughness. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer 3|Detail")
	float DetailFrequency = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer 3|Detail")
	float DetailHeight = 6.f;

	//////////////////////////////////////////////////////////////////////////
	// Layer 4- Ravines

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer 4|Ravines")
	bool bUseRavines = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer 4|Ravines")
	float RavineFrequency = 3.f;

	/** Ravines only carve downwards, they never build mountains. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer 4|Ravines")
	float RavineDepth = 15.f;

	//////////////////////////////////////////////////////////////////////////
	// Caves and shell

	/** Carve spherical caves. 0 disables. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Caves", meta = (ClampMin = "0", ClampMax = "1"))
	float CaveAmount = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Caves")
	float CaveFrequency = 3.f;

	/** Shell thickness. Above 0 the interior is hollow. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shell", meta = (ClampMin = "0"))
	float ShellThickness = 0.f;

	//////////////////////////////////////////////////////////////////////////
	// Surface

	/** Sea level depth in voxels. Surfaces below Radius - SeaLevel get water. */
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

	/** Elevation at which snow starts, measured from Radius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface", meta = (ClampMin = "0"))
	float SnowLine = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
	int32 Seed = 1337;
};

class FVoxelPlanetGeneratorInstance : public TVoxelGeneratorInstanceHelper<FVoxelPlanetGeneratorInstance, UVoxelPlanetGenerator>
{
public:
	using Super = TVoxelGeneratorInstanceHelper<FVoxelPlanetGeneratorInstance, UVoxelPlanetGenerator>;

	explicit FVoxelPlanetGeneratorInstance(UVoxelPlanetGenerator& MyGenerator);

	//~ Begin FVoxelGeneratorInstance Interface
	virtual void Init(const FVoxelGeneratorInit& InitStruct) override;

	v_flt GetValueImpl(v_flt X, v_flt Y, v_flt Z, int32 LOD, const FVoxelItemStack& Items) const;
	FVoxelMaterial GetMaterialImpl(v_flt X, v_flt Y, v_flt Z, int32 LOD, const FVoxelItemStack& Items) const;

	TVoxelRange<v_flt> GetValueRangeImpl(const FVoxelIntBox& Bounds, int32 LOD, const FVoxelItemStack& Items) const;

	virtual FVector GetUpVector(v_flt X, v_flt Y, v_flt Z) const override final;
	//~ End FVoxelGeneratorInstance Interface

private:
	/**
	 * Builds the terrain elevation at a unit-sphere direction.
	 * NX/NY/NZ must already be normalized. Returns the radius offset from Radius.
	 */
	double GetElevationAt(double NX, double NY, double NZ, double CrustRadius, double Distance) const;

	/** Surface radius at a unit-sphere direction. Used by both value and material. */
	double GetSurfaceRadius(double NX, double NY, double NZ) const;

	const float Radius;
	const FVector Center;

	const bool bUseContinents;
	const float ContinentFrequency;
	const float ContinentHeight;
	const int32 ContinentOctaves;
	const float ContinentContrast;

	const bool bUseMountains;
	const float MountainFrequency;
	const float MountainHeight;
	const float MountainSharpness;
	const float MountainLandBias;

	const bool bUseDetail;
	const float DetailFrequency;
	const float DetailHeight;

	const bool bUseRavines;
	const float RavineFrequency;
	const float RavineDepth;

	const float CaveAmount;
	const float CaveFrequency;
	const float ShellThickness;
	const float SeaLevel;

	const FLinearColor DeepWaterColor;
	const FLinearColor ShoreColor;
	const FLinearColor LowlandColor;
	const FLinearColor HighlandColor;
	const FLinearColor PeakColor;
	const FLinearColor CoreColor;
	const float SnowLine;

	const int32 Seed;

	FVoxelFastNoise ContinentNoise;
	FVoxelFastNoise MountainNoise;
	FVoxelFastNoise DetailNoise;
	FVoxelFastNoise RavineNoise;
	FVoxelFastNoise CaveNoise;
};
