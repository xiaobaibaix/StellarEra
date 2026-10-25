// Copyright Voxel Plugin SAS. All Rights Reserved.
// 把行星的「在哪、多大」翻译成地形模块认识的上下文。

#include "VoxelPlanetGenerator.h"

#include "PlanetGeologyField.h"
#include "PlanetGeologyPreset.h"
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
	if (!GeologyPreset)
	{
		return nullptr;
	}

	const double CacheRadius = double(Radius);
	const int32 CacheRevision = GeologyPreset->GetRevision();
	if (GeologyCache.IsValid()
		&& GeologyCachePreset.Get() == GeologyPreset.Get()
		&& GeologyCacheRadius == CacheRadius
		&& GeologyCacheRevision == CacheRevision)
	{
		return GeologyCache;
	}

	GeologyCache = FPlanetGeologyField::BuildFromPreset(*GeologyPreset, CacheRadius);
	GeologyCachePreset = GeologyPreset;
	GeologyCacheRadius = CacheRadius;
	GeologyCacheRevision = CacheRevision;

	if (GeologyCache->IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("行星地质预设 %s 没有任何元素"), *GeologyPreset->GetPathName());
	}

	return GeologyCache;
}

void UVoxelPlanetGenerator::InvalidateGeology()
{
	GeologyCache.Reset();
	GeologyCachePreset.Reset();
	GeologyCacheRadius = 0.0;
	GeologyCacheRevision = 0;
}
