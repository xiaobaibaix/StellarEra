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
	// 只覆盖六个可调项，其余 FPlanetPlateParams 字段保持默认。
	C.Plates.PlateCount = PlateCount;
	C.Plates.TargetLandFrac = TargetLandFrac;
	C.Plates.LandTableVoxels = LandTableVoxels;
	C.Plates.OceanBasinVoxels = OceanBasinVoxels;
	C.Plates.StyleAmpVoxels = StyleAmpVoxels;
	C.Plates.SlopeWidthVoxels = SlopeWidthVoxels;
	return C;
}
