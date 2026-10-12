// Copyright Voxel Plugin SAS. All Rights Reserved.
// Wraps UVoxelPlanetGenerator into a placeable Actor.

#include "VoxelPlanetActor.h"

#include "VoxelWorld.h"
#include "VoxelWorldCreateInfo.h"
#include "VoxelGenerators/VoxelGeneratorPicker.h"
#include "VoxelPlanetGenerator.h"

AVoxelPlanetActor::AVoxelPlanetActor()
{
	PrimaryActorTick.bCanEverTick = false;

	// The generator is a plain UObject: no transform, no rendering.
	Generator = NewObject<UVoxelPlanetGenerator>(this, TEXT("VoxelPlanetGenerator"));

	// The Voxel World does the actual rendering.
	VoxelWorld = CreateDefaultSubobject<AVoxelWorld>(TEXT("VoxelWorld"));
}

float AVoxelPlanetActor::GetScaledRadiusVoxels() const
{
	// Only uniform scale is honoured. Non-uniform scale would shear the voxel
	// grid, and since the density field lives in voxel space the result would be
	// a distorted mess rather than a scaled planet.
	const float UniformScale = GetActorScale3D().GetAbsMax();
	return Radius * UniformScale;
}

FVector AVoxelPlanetActor::GetCenterInVoxels() const
{
	// The generator works in voxel units, the actor transform is in cm.
	return GetActorLocation() / FMath::Max(VoxelSize, 0.0001f);
}

void AVoxelPlanetActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RebuildPlanet();
}

void AVoxelPlanetActor::BeginPlay()
{
	Super::BeginPlay();
	RebuildPlanet();
}

void AVoxelPlanetActor::RebuildPlanet()
{
	if (!Generator || !VoxelWorld)
	{
		return;
	}

	// --- 1. Push parameters into the generator ---
	Generator->Radius = GetScaledRadiusVoxels();
	Generator->Center = GetCenterInVoxels();
	Generator->Seed = Seed;

	Generator->bUseContinents = bUseContinents;
	Generator->ContinentFrequency = ContinentFrequency;
	Generator->ContinentHeight = ContinentHeight;
	Generator->ContinentOctaves = ContinentOctaves;
	Generator->ContinentContrast = ContinentContrast;

	Generator->bUseMountains = bUseMountains;
	Generator->MountainFrequency = MountainFrequency;
	Generator->MountainHeight = MountainHeight;
	Generator->MountainSharpness = MountainSharpness;
	Generator->MountainLandBias = MountainLandBias;

	Generator->bUseDetail = bUseDetail;
	Generator->DetailFrequency = DetailFrequency;
	Generator->DetailHeight = DetailHeight;

	Generator->bUseRavines = bUseRavines;
	Generator->RavineFrequency = RavineFrequency;
	Generator->RavineDepth = RavineDepth;

	Generator->CaveAmount = CaveAmount;
	Generator->CaveFrequency = CaveFrequency;
	Generator->ShellThickness = ShellThickness;

	Generator->SeaLevel = SeaLevel;
	Generator->DeepWaterColor = DeepWaterColor;
	Generator->ShoreColor = ShoreColor;
	Generator->LowlandColor = LowlandColor;
	Generator->HighlandColor = HighlandColor;
	Generator->PeakColor = PeakColor;
	Generator->CoreColor = CoreColor;
	Generator->SnowLine = SnowLine;

	// --- 2. Configure the Voxel World ---
	// VoxelSize must match or the collision will not line up with the mesh.
	VoxelWorld->VoxelSize = VoxelSize;

	// Type=Object hands over a live instance with the parameters already set.
	// Using Type=Class would mutate the class default object, which is shared
	// globally by every planet in the level.
	VoxelWorld->Generator = FVoxelGeneratorPicker(Generator.Get());

	// --- 3. Recreate so the new generator takes effect ---
	VoxelWorld->CreateWorld(FVoxelWorldCreateInfo());
}
