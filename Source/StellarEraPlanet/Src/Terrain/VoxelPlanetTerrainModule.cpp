// Copyright Voxel Plugin SAS. All Rights Reserved.
// 行星地形模块的抽象基类：地形长什么样归模块，在哪、多大归行星。

#include "VoxelPlanetTerrainModule.h"

#include "VoxelGenerators/VoxelEmptyGenerator.h"

TVoxelSharedRef<FVoxelGeneratorInstance> UVoxelPlanetTerrainModule::GetInstance(const FVoxelPlanetTerrainContext& /*Context*/)
{
	// 基类没有地形；用 checkNoEntry 而非 check：编辑器误建空模块不该炸掉关卡。
	checkNoEntry();
	return MakeVoxelShared<FVoxelEmptyGeneratorInstance>();
}

float UVoxelPlanetTerrainModule::GetMinSurfaceRadiusVoxels(const FVoxelPlanetTerrainContext& Context) const
{
	return Context.Radius;
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
