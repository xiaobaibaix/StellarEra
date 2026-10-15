// Copyright Voxel Plugin SAS. All Rights Reserved.
// 把行星的「在哪、多大」翻译成地形模块认识的上下文。

#include "VoxelPlanetGenerator.h"

#include "VoxelPlanetTerrainModule.h"
#include "VoxelPlanetSphereTerrain.h"

TVoxelSharedRef<FVoxelGeneratorInstance> UVoxelPlanetGenerator::GetInstance()
{
	// 行星的「在哪、多大」在这里、也只在这里与模块的「长什么样」交汇。
	//
	// 模块拿到的是纯值，不是本对象：它因此可以挂在任意位置、任意尺寸的行星上，
	// 同一个资产能被多颗行星共享而不互相污染。
	FVoxelPlanetTerrainContext Context;
	Context.Radius = Radius;
	Context.Center = Center;
	Context.BaseColor = SurfaceColor;

	UVoxelPlanetTerrainModule* EffectiveTerrain = Terrain;
	if (!EffectiveTerrain)
	{
		// 没挂地形 = 光滑球。这里造一个纯球模块来代理，而不是写一条独立分支：
		// 两条路走同一段代码，就不存在「只在缺模块时才执行、平时没人测」的逻辑。
		if (!FallbackTerrain)
		{
			FallbackTerrain = NewObject<UVoxelPlanetSphereTerrain>(this, TEXT("VoxelPlanetFallbackTerrain"));
		}
		EffectiveTerrain = FallbackTerrain;
	}

	return EffectiveTerrain->GetInstance(Context);
}
