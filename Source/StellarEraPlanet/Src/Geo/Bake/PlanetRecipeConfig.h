// 烘焙配方：纯 POD，游戏线程从 UPlanetRecipe 拷一次后交给后台任务，烘焙期不再碰 UObject。
#pragma once
#include "CoreMinimal.h"
#include "GeoRng.h"

/** 圈层底界，都是从地表向下的累计深度占 R（附录 01-sphere.md）。 */
struct FPlanetSphereParams
{
	float CrustRatio = 0.25f;
	float UpperMantleRatio = 0.38f;
	float TransitionRatio = 0.42f;
	float MantleOuterRatio = 0.55f;
	float OuterCoreRatio = 0.80f;
	/** 莫霍面起伏幅度（占 R）。必须 < 地壳与上地幔厚度的一半。 */
	float MohoWobbleRatio = 0.01f;
	int32 WobbleOctaves = 3;
};

struct FPlanetRecipeConfig
{
	double Radius = 40000.0;
	float VoxelSizeCm = 50.f;
	uint64 Seed = 1337;
	int32 FaceRes = 1024;
	/** 只给确定性测试用：强制单线程烘焙，与并行结果对拍。 */
	bool bSingleThreadBake = false;
	FPlanetSphereParams Sphere;

	uint64 Hash() const
	{
		uint64 H = GeoHash::Combine(GeoHash::Float(float(Radius)), GeoHash::Float(VoxelSizeCm));
		H = GeoHash::Combine(H, Seed);
		H = GeoHash::Combine(H, uint64(FaceRes));
		const float S[6] = { Sphere.CrustRatio, Sphere.UpperMantleRatio, Sphere.TransitionRatio,
			Sphere.MantleOuterRatio, Sphere.OuterCoreRatio, Sphere.MohoWobbleRatio };
		for (const float V : S) { H = GeoHash::Combine(H, GeoHash::Float(V)); }
		return GeoHash::Combine(H, uint64(Sphere.WobbleOctaves));
	}
};
