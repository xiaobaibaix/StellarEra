// 烘焙配方：纯 POD，游戏线程从 UPlanetRecipe 拷一次后交给后台任务，烘焙期不再碰 UObject。
#pragma once
#include "CoreMinimal.h"
#include "GeoRng.h"
#include "GeoMineralRule.h"

/** 圈层底界，都是从地表向下的累计深度占 R（附录 01-sphere.md）。 */
struct FPlanetSphereParams
{
	float CrustRatio = 0.25f;             // 地壳底
	float UpperMantleRatio = 0.38f;       // 上地幔底
	float TransitionRatio = 0.42f;        // 过渡带底
	float MantleOuterRatio = 0.55f;       // 下地幔底（核幔边界）
	float OuterCoreRatio = 0.80f;         // 外核底
	/** 莫霍面（地壳 / 地幔分界）起伏幅度占半径比例。必须 < 地壳与上地幔厚度的一半。 */
	float MohoWobbleRatio = 0.01f;
	int32 WobbleOctaves = 3;              // 莫霍面起伏噪声层数
};

/** 阶段 2 的尺度一律是体素：按 00-pipeline.md §3.0 的物理尺寸，不再写 k·R（R=40000 下 clamp 全撞上限）。 */
struct FPlanetPlateParams
{
	int32 PlateCount = 7;                 // 3–15；> 15 处处是山，< 3 出不了三节点
	float PlateWeightMin = 0.6f;          // 加权 Voronoi 的权重区间，乘性才出得了巨型板块
	float PlateWeightMax = 1.8f;          // 权重上限：越大最大板块越大
	float WarpAmp = 0.06f;                // 方向单位（≈弧度）：不做 domain warp 边界是完美圆弧
	float AgeScaleRatio = 0.25f;          // 洋壳年龄随离边界距离增长的尺度（× 弦距）
	float BoundaryBandRatio = 0.02f;      // BoundaryType 只在距边界 2%R 内非零
};

/** 阶段 2 大陆场：全球一张连续场 F，按立体角加权分位数定海陆，再按排名映射高程。 */
struct FPlanetContinentParams
{
	int32 FrameDegree = 4;                // 球谐最高阶：1–2 超大陆、4 类地球、≥6 群岛
	float LandFrac = 0.30f;               // 陆地占表面积比例
	float CoastRoughness = 0.35f;         // 细节层 std / 框架层 std
	float ShelfFrac = 0.15f;              // 陆架占海洋面积
	float LandHeightRatio = 0.006f;      // 最高陆地高出海平面，占半径（R=40000 时 240 体素）
	float OceanDepthRatio = 0.012f;      // 最深洋盆低于海平面，占半径（R=40000 时 480 体素）
	// 以下不暴露：画面实测定的形状常数
	float FrameSpectralDecay = 1.0f;      // 每阶总功率 ∝ l^-decay
	float DetailFrequency = 3.0f;         // 单位球上的基频
	int32 DetailOctaves = 5;              // 海岸细节噪声层数
	float DetailWarpAmp = 0.15f;          // 海岸细节的扭曲幅度：越大半岛越细长
	float LandCurvePower = 1.6f;          // >1：低地多、高地少
	float ShelfDepthRatio = 0.10f;        // 陆架底 = 比例 × OceanDepth
	float SlopeFrac = 0.12f;              // 陆坡占海洋面积
	float AbyssRatio = 0.85f;             // 陆坡底 = 比例 × OceanDepth
};

/** 阶段 3 宏观地形：所有尺度都是占半径的比例，星球放大缩小形状不变。 */
struct FPlanetTerrainParams
{
	float MountainHeightRatio = 0.05f;    // 山脉起伏幅度（R=40000 时 2000 体素）
	float TrenchDepthRatio = 0.025f;      // 海沟在洋底之下再下切（1000 体素）
	float PlainReliefRatio = 0.002f;      // 平原丘陵起伏（80 体素）
	float BeltWidthRatio = 0.06f;         // 造山带宽（方向单位 ≈ 弧度）
	float TrenchWidthRatio = 0.016f;      // 海沟宽
	float RidgeWavelengthRatio = 0.06f;   // 山脊基波长
	float WarpRatio = 0.03f;              // 采样坐标扭曲幅度：山脊弯曲
	int32 ErosionIterations = 5;          // 热力侵蚀遍数
	float ErosionStrength = 0.2f;         // 每遍向邻域均值靠拢的比例
	float TalusAngleDeg = 30.f;           // 只有比它陡的坡才被侵蚀
};

struct FPlanetRecipeConfig
{
	/** 半径（体素），由行星填；烘焙全程以体素为单位，与体素厘米尺寸无关。 */
	double Radius = 40000.0;
	/** 随机种子。 */
	uint64 Seed = 1337;
	/** 烘焙贴图每面分辨率。 */
	int32 FaceRes = 1024;
	/** 只给确定性测试用：强制单线程烘焙，与并行结果对拍。 */
	bool bSingleThreadBake = false;
	/** 编辑器调试图层用：快照额外保留阶段中间场（约 12 MB）。不进哈希，运行时恒为 false。 */
	bool bKeepDebugFields = false;
	FPlanetSphereParams Sphere;
	FPlanetPlateParams Plates;
	FPlanetContinentParams Continents;
	FPlanetTerrainParams Terrain;
	/** 撒矿规则（来自配方的撒矿器列表，已过滤掉停用项）。规则不可变，后台线程只读。 */
	TArray<TSharedPtr<const IGeoMineralRule>> MineralRules;
	/** 矿物调色板，只供显示，不进哈希。 */
	TArray<FGeoMineralInfo> Minerals;

	uint64 Hash() const
	{
		uint64 H = GeoHash::Float(float(Radius));
		H = GeoHash::Combine(H, Seed);
		H = GeoHash::Combine(H, uint64(FaceRes));
		const float S[6] = { Sphere.CrustRatio, Sphere.UpperMantleRatio, Sphere.TransitionRatio,
			Sphere.MantleOuterRatio, Sphere.OuterCoreRatio, Sphere.MohoWobbleRatio };
		for (const float V : S) { H = GeoHash::Combine(H, GeoHash::Float(V)); }
		H = GeoHash::Combine(H, uint64(Sphere.WobbleOctaves));
		H = GeoHash::Combine(H, uint64(Plates.PlateCount));
		const float P[5] = { Plates.PlateWeightMin, Plates.PlateWeightMax, Plates.WarpAmp,
			Plates.AgeScaleRatio, Plates.BoundaryBandRatio };
		for (const float V : P) { H = GeoHash::Combine(H, GeoHash::Float(V)); }
		const FPlanetContinentParams& K = Continents;
		H = GeoHash::Combine(H, uint64(K.FrameDegree));
		H = GeoHash::Combine(H, uint64(K.DetailOctaves));
		const float C[12] = { K.LandFrac, K.CoastRoughness, K.ShelfFrac, K.LandHeightRatio, K.OceanDepthRatio,
			K.FrameSpectralDecay, K.DetailFrequency, K.DetailWarpAmp, K.LandCurvePower, K.ShelfDepthRatio,
			K.SlopeFrac, K.AbyssRatio };
		for (const float V : C) { H = GeoHash::Combine(H, GeoHash::Float(V)); }
		const FPlanetTerrainParams& T = Terrain;
		H = GeoHash::Combine(H, uint64(T.ErosionIterations));
		const float Tr[9] = { T.MountainHeightRatio, T.TrenchDepthRatio, T.PlainReliefRatio, T.BeltWidthRatio, T.TrenchWidthRatio,
			T.RidgeWavelengthRatio, T.WarpRatio, T.ErosionStrength, T.TalusAngleDeg };
		for (const float V : Tr) { H = GeoHash::Combine(H, GeoHash::Float(V)); }
		for (const TSharedPtr<const IGeoMineralRule>& Rule : MineralRules)
		{
			if (Rule) { H = GeoHash::Combine(H, Rule->Hash()); }
		}
		return H;
	}
};
