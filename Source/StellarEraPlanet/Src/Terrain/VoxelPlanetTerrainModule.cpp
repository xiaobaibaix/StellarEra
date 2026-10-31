// Copyright Voxel Plugin SAS. All Rights Reserved.
// 行星地形模块的抽象基类：地形长什么样归模块，在哪、多大归行星。

#include "VoxelPlanetTerrainModule.h"

#include "VoxelPlanetActor.h"
#include "VoxelPlanetSphereTerrain.h"
#include "UObject/UObjectIterator.h"

TVoxelSharedRef<FVoxelGeneratorInstance> UVoxelPlanetTerrainModule::GetInstance(const FVoxelPlanetTerrainContext& Context)
{
	// 基类兜底：返回纯球。
	//
	// 走得到这里说明有子类没重写 GetInstance。用 checkNoEntry 而不是 check ——
	// 编辑器里误建一个空模块不该炸掉整个关卡，给一颗光滑球是更有用的失败方式。
	checkNoEntry();
	return MakeVoxelShared<FVoxelPlanetSphereTerrainInstance>(Context);
}

#if WITH_EDITOR
void UVoxelPlanetTerrainModule::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// 遍历所有已加载世界里的行星，把引用本模块的重建一遍。
	//
	// 用 InvalidateAndRebuild 而不是直接调 RebuildPlanet：后者有一道「参数哈希
	// 没变就跳过」的守卫，而地形参数并不参与那个哈希（变的是资产，不是行星自己的
	// 属性），守卫会把这次重建整个吞掉。
	for (TObjectIterator<AVoxelPlanetActor> It; It; ++It)
	{
		// TObjectIterator 默认连类默认对象一起遍历。对 CDO 调重建会去碰它没有的
		// 世界，必须跳过。Archetype 同理 —— 它不是关卡里真实存在的那颗行星。
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
