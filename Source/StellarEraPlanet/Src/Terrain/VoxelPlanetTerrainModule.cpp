// Copyright Voxel Plugin SAS. All Rights Reserved.
// 行星地形模块的抽象基类：地形长什么样归模块，在哪、多大归行星。

#include "VoxelPlanetTerrainModule.h"

#include "VoxelPlanetActor.h"
#include "VoxelPlanetSphereTerrain.h"
#include "UObject/UObjectIterator.h"

TVoxelSharedRef<FVoxelGeneratorInstance> UVoxelPlanetTerrainModule::GetInstance(const FVoxelPlanetTerrainContext& Context)
{
	// 基类兜底返回纯球。用 checkNoEntry 而非 check：编辑器误建空模块不该炸掉关卡。
	checkNoEntry();
	return MakeVoxelShared<FVoxelPlanetSphereTerrainInstance>(Context);
}

float UVoxelPlanetTerrainModule::GetRawOffset(const FVector& UnitDir) const
{
	return 0.f;
}

bool UVoxelPlanetTerrainModule::GetWaterConfig(FPlanetWaterConfig& OutConfig) const
{
	OutConfig = FPlanetWaterConfig();
	return false;
}

#if WITH_EDITOR
void UVoxelPlanetTerrainModule::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// 用 InvalidateAndRebuild 而非 RebuildPlanet：后者有哈希守卫，而地形参数不参与
	// 哈希（变的是资产），守卫会把重建整个吞掉。
	for (TObjectIterator<AVoxelPlanetActor> It; It; ++It)
	{
		// TObjectIterator 会遍历 CDO 与 Archetype；对它们重建会碰没有的世界，必须跳过。
		if (!It->GetWorld() || It->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
		{
			continue;
		}

		if (It->Terrain == this)
		{
			It->InvalidateAndRebuild();
		}
	}
}
#endif
