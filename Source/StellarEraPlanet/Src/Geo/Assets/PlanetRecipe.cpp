// 游戏线程把 UObject 字段拷成 POD，烘焙期不再碰 UObject。
#include "PlanetRecipe.h"

FPlanetRecipeConfig UPlanetRecipe::ToConfig() const
{
	check(IsInGameThread());
	FPlanetRecipeConfig C;
	C.Radius = double(Radius);
	C.VoxelSizeCm = VoxelSizeCm;
	// 面板是 int32；经 uint32 扩宽，负种子也得到稳定的 64 位值。
	C.Seed = uint64(uint32(Seed));
	C.FaceRes = FaceRes;
	C.Sphere.CrustRatio = CrustRatio;
	C.Sphere.UpperMantleRatio = UpperMantleRatio;
	C.Sphere.TransitionRatio = TransitionRatio;
	C.Sphere.MantleOuterRatio = MantleOuterRatio;
	C.Sphere.OuterCoreRatio = OuterCoreRatio;
	C.Sphere.MohoWobbleRatio = MohoWobbleRatio;
	C.Sphere.WobbleOctaves = WobbleOctaves;
	// 只覆盖暴露项，其余形状常数保持 FPlanetContinentParams 默认。
	C.Plates.PlateCount = PlateCount;
	C.Continents.FrameDegree = FrameDegree;
	C.Continents.LandFrac = LandFrac;
	C.Continents.CoastRoughness = CoastRoughness;
	C.Continents.ShelfFrac = ShelfFrac;
	C.Continents.LandHeightVoxels = LandHeightVoxels;
	C.Continents.OceanDepthVoxels = OceanDepthVoxels;
	return C;
}
