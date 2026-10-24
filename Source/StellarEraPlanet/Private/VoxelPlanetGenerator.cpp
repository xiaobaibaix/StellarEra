// Copyright Voxel Plugin SAS. All Rights Reserved.
// 把行星的「在哪、多大」翻译成地形模块认识的上下文。

#include "VoxelPlanetGenerator.h"

#include "PlanetGeologyField.h"
#include "VoxelPlanetTerrainModule.h"
#include "VoxelPlanetSphereTerrain.h"

TVoxelSharedRef<FVoxelGeneratorInstance> UVoxelPlanetGenerator::GetInstance()
{
	UVoxelPlanetTerrainModule* EffectiveTerrain = Terrain;
	if (!EffectiveTerrain)
	{
		// 没挂地形 = 光滑球。这里造一个纯球模块来代理，而不是写一条独立分支：
		// 两条路走同一段代码，就不存在「只在缺模块时才执行、平时没人测」的逻辑。
		if (!FallbackTerrain)
		{
			FallbackTerrain = NewObject<UVoxelPlanetSphereTerrain>(
				this, TEXT("VoxelPlanetFallbackTerrain"));
		}
		EffectiveTerrain = FallbackTerrain;
	}

	// 行星的「在哪、多大」与地质场在这里、也只在这里与模块的「长什么样」交汇。
	return EffectiveTerrain->GetInstance(MakeTerrainContext());
}

FVoxelPlanetTerrainContext UVoxelPlanetGenerator::MakeTerrainContext()
{
	FVoxelPlanetTerrainContext Context;
	Context.Radius = Radius;
	Context.Center = Center;
	Context.BaseColor = SurfaceColor;
	Context.Geology = GetGeology();
	return Context;
}

TSharedPtr<const FPlanetGeologyField> UVoxelPlanetGenerator::GetGeology()
{
	if (GeologyPreset.IsEmpty())
	{
		return nullptr;
	}

	if (bGeologyCacheValid && GeologyCachePreset == GeologyPreset)
	{
		return GeologyCache;
	}

	FPlanetGeologyLoadReport Report;
	GeologyCache = FPlanetGeologyField::LoadFromFiles(GeologyPreset, Report);
	GeologyCachePreset = GeologyPreset;
	bGeologyCacheValid = true;

	for (const FString& Error : Report.Errors)
	{
		UE_LOG(LogTemp, Error, TEXT("行星地质 '%s': %s"), *GeologyPreset, *Error);
	}
	for (const FString& Warning : Report.Warnings)
	{
		UE_LOG(LogTemp, Warning, TEXT("行星地质 '%s': %s"), *GeologyPreset, *Warning);
	}

	return GeologyCache;
}

void UVoxelPlanetGenerator::InvalidateGeology()
{
	GeologyCache.Reset();
	GeologyCachePreset.Reset();
	bGeologyCacheValid = false;
}
