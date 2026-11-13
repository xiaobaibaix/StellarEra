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

/** 阶段 2 的尺度一律是体素：按 00-pipeline.md §3.0 的物理尺寸，不再写 k·R（R=40000 下 clamp 全撞上限）。 */
struct FPlanetPlateParams
{
	int32 PlateCount = 7;                 // 3–15；> 15 处处是山，< 3 出不了三节点
	float PlateWeightMin = 0.6f;          // 加权 Voronoi 的权重区间，乘性才出得了巨型板块
	float PlateWeightMax = 1.8f;
	float WarpAmp = 0.06f;                // 方向单位（≈弧度）：不做 domain warp 边界是完美圆弧
	float ContinentalCoreMin = 2.f;
	float ContinentalCoreMax = 4.f;
	float MixedCoreMin = 1.f;
	float MixedCoreMax = 2.f;
	float MicroCoreChance = 0.15f;        // 大洋型板块出现微陆核的概率
	float CoreRadiusRatio = 0.12f;        // × R，再乘二分反解出的 α
	float CoreMinSpacingRatio = 0.25f;    // × R，核间最小中心角
	float SigmaRatio = 0.55f;             // σ = SigmaRatio · α · r
	float AlphaMin = 0.5f;
	float AlphaMax = 6.0f;                // 附录写的 2.0 在 R=40000 下达不到 30% 陆地占比
	float TargetLandFrac = 0.30f;
	float LandTableVoxels = 240.f;        // A_up
	float OceanBasinVoxels = 480.f;       // A_down
	float SlopeWidthVoxels = 240.f;       // w_s
	float StyleAmpVoxels = 120.f;
	float StyleNoiseWavelengths = 1.5f;   // 每半球波长数，与 R 无关
	float AgeScaleRatio = 0.25f;
	float BoundaryBandRatio = 0.02f;      // BoundaryType 只在距边界 2%R 内非零
};

/** 阶段 2 大陆场：全球一张连续场 F，按立体角加权分位数定海陆，再按排名映射高程。 */
struct FPlanetContinentParams
{
	int32 FrameDegree = 4;                // 球谐最高阶：1–2 超大陆、4 类地球、≥6 群岛
	float LandFrac = 0.30f;
	float CoastRoughness = 0.35f;         // 细节层 std / 框架层 std
	float ShelfFrac = 0.15f;              // 陆架占海洋面积
	float LandHeightVoxels = 240.f;
	float OceanDepthVoxels = 480.f;
	// 以下不暴露：画面实测定的形状常数
	float FrameSpectralDecay = 1.0f;      // 每阶总功率 ∝ l^-decay
	float DetailFrequency = 3.0f;         // 单位球上的基频
	int32 DetailOctaves = 5;
	float DetailWarpAmp = 0.15f;
	float LandCurvePower = 1.6f;          // >1：低地多、高地少
	float ShelfDepthRatio = 0.10f;        // 陆架底 = 比例 × OceanDepth
	float SlopeFrac = 0.12f;              // 陆坡占海洋面积
	float AbyssRatio = 0.85f;             // 陆坡底 = 比例 × OceanDepth
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
	FPlanetPlateParams Plates;
	FPlanetContinentParams Continents;

	uint64 Hash() const
	{
		uint64 H = GeoHash::Combine(GeoHash::Float(float(Radius)), GeoHash::Float(VoxelSizeCm));
		H = GeoHash::Combine(H, Seed);
		H = GeoHash::Combine(H, uint64(FaceRes));
		const float S[6] = { Sphere.CrustRatio, Sphere.UpperMantleRatio, Sphere.TransitionRatio,
			Sphere.MantleOuterRatio, Sphere.OuterCoreRatio, Sphere.MohoWobbleRatio };
		for (const float V : S) { H = GeoHash::Combine(H, GeoHash::Float(V)); }
		H = GeoHash::Combine(H, uint64(Sphere.WobbleOctaves));
		H = GeoHash::Combine(H, uint64(Plates.PlateCount));
		// AlphaMin/AlphaMax 也在内：改搜索区间会改大陆形状。
		const float P[21] = { Plates.PlateWeightMin, Plates.PlateWeightMax, Plates.WarpAmp,
			Plates.ContinentalCoreMin, Plates.ContinentalCoreMax, Plates.MixedCoreMin, Plates.MixedCoreMax,
			Plates.MicroCoreChance, Plates.CoreRadiusRatio, Plates.CoreMinSpacingRatio, Plates.SigmaRatio,
			Plates.AlphaMin, Plates.AlphaMax, Plates.TargetLandFrac, Plates.LandTableVoxels,
			Plates.OceanBasinVoxels, Plates.SlopeWidthVoxels, Plates.StyleAmpVoxels,
			Plates.StyleNoiseWavelengths, Plates.AgeScaleRatio, Plates.BoundaryBandRatio };
		for (const float V : P) { H = GeoHash::Combine(H, GeoHash::Float(V)); }
		return H;
	}
};
