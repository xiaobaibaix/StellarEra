// Copyright Voxel Plugin SAS. All Rights Reserved.
// 把行星的「在哪、多大」翻译成地形模块认识的上下文。

#include "VoxelPlanetGenerator.h"

#include "HAL/PlatformTime.h"
#include "PlanetGeologyField.h"
#include "PlanetGeologyPreset.h"
#include "PlanetHydrologyBake.h"
#include "PlanetHydrologyGrid.h"
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
	Context.Hydrology = GetHydrology();
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
		UE_LOG(LogTemp, Warning, TEXT("行星地质预设 %s 没有任何矿囊"), *GeologyPreset->GetPathName());
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

TSharedPtr<const FPlanetHydrologyField> UVoxelPlanetGenerator::GetHydrology() const
{
	return HydrologyCache;
}

void UVoxelPlanetGenerator::RefreshHydrology()
{
	UVoxelPlanetTerrainModule* EffectiveTerrain = Terrain;
	FPlanetWaterConfig Config;
	if (!EffectiveTerrain || !EffectiveTerrain->GetWaterConfig(Config))
	{
		InvalidateHydrology();
		return;
	}

	const double CacheRadius = double(Radius);
	if (HydrologyCache.IsValid()
		&& HydrologyCacheTerrain.Get() == EffectiveTerrain
		&& HydrologyCacheRadius == CacheRadius
		&& HydrologyCacheParams == Config.Params)
	{
		return;
	}

	HydrologyCacheTerrain = EffectiveTerrain;
	HydrologyCacheRadius = CacheRadius;
	HydrologyCacheParams = Config.Params;

	// HeightFn 在烘焙里被并行调用：GetRawOffset 是 const、无副作用，可以安全共享。
	// 但**必须是 const 调用** —— 任何内部缓存（比如「上一次的噪声参数」）都会
	// 变成数据竞争。
	const double StartSeconds = FPlatformTime::Seconds();
	HydrologyCache = FPlanetHydrologyBake::Bake(
		[EffectiveTerrain](const FVector& UnitDir)
		{
			return EffectiveTerrain->GetRawOffset(UnitDir);
		},
		Config.Params,
		CacheRadius);
	const double ElapsedSeconds = FPlatformTime::Seconds() - StartSeconds;

	if (!HydrologyCache.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("行星水文烘焙失败：分辨率 %d"), Config.Params.Resolution);
		return;
	}

	UE_LOG(LogTemp, Log,
		TEXT("行星水文烘焙完成：%d 面 × %d² 格，%d 片湖，%d 条河，耗时 %.2f s"),
		PlanetHydrologyGrid::FaceCount,
		Config.Params.Resolution,
		HydrologyCache->GetLakes().Num(),
		HydrologyCache->GetRivers().Num(),
		ElapsedSeconds);
}

void UVoxelPlanetGenerator::InvalidateHydrology()
{
	HydrologyCache.Reset();
	HydrologyCacheTerrain.Reset();
	HydrologyCacheRadius = 0.0;
	HydrologyCacheParams = FPlanetHydrologyParams();
}
