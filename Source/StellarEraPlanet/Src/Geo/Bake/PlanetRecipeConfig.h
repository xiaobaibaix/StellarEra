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
	bool bMoho = true;                    // 关掉时莫霍面按正球面处理（该场填 0）
};

/** 单块板块的运动覆盖：按板块编号（查看器「板块编号」图层可读）单独指定速度 / 方向。 */
struct FPlanetPlateOverride
{
	int32 Plate = 0;
	bool bSpeed = false;
	float Speed = 5.f;
	bool bDirection = false;
	float DirectionDeg = 0.f;   // 板块中心处的运动方位角：0 = 北，90 = 东（从星球外看顺时针）
};

/** 阶段 2 的尺度一律是体素：按 00-pipeline.md §3.0 的物理尺寸，不再写 k·R（R=40000 下 clamp 全撞上限）。 */
struct FPlanetPlateParams
{
	int32 PlateCount = 7;                 // 3–15；> 15 处处是山，< 3 出不了三节点
	float PlateWeightMin = 0.6f;          // 加权 Voronoi 的权重区间，乘性才出得了巨型板块
	float PlateWeightMax = 1.8f;          // 权重上限：越大最大板块越大
	float WarpAmp = 0.5f;                 // 边界大尺度弯折幅度（方向单位 ≈ 弧度）：不扭 Voronoi 边界是完美圆弧
	float WarpFrequency = 2.0f;           // 大尺度弯折的基频（单位球上）
	int32 WarpOctaves = 5;                // 弯折层数：层数越多，凸角凹湾越多
	float DetailWarpAmp = 0.08f;          // 边界小尺度锯齿幅度
	float DetailWarpFrequency = 10.f;     // 小尺度锯齿频率
	float AgeScaleRatio = 0.25f;          // 洋壳年龄随离边界距离增长的尺度（× 弦距）
	float BoundaryBandRatio = 0.02f;      // BoundaryType 只在距边界 2%R 内非零
	float SpeedMin = 1.f;                 // 板块速度下限（相对单位，可理解为 cm/年）
	float SpeedMax = 10.f;                // 板块速度上限
	float TransformShearRatio = 1.5f;     // 剪切 / 挤压超过它就判为转换断层
	TArray<FPlanetPlateOverride> Overrides;
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
	float TrenchSedimentFill = 0.7f;      // 海沟沉积充填：0 不填，1 近岸几乎填平（远洋段不受影响）
	float TrenchSlump = 0.5f;             // 海沟两壁坍塌平滑强度：0 不塌，1 最缓
	float TrenchWedge = 0.5f;             // 陆侧增生楔隆起强度
	float SpeedWidthEffect = 0.6f;        // 挤压速度对山带宽度的影响：快 → 窄，慢 → 宽
	float SpeedHeightEffect = 0.6f;       // 挤压速度对山高 / 海沟深的影响：快 → 高，慢 → 低
	float MidOceanRidgeHeightRatio = 0.006f; // 洋中脊隆起（R=40000 时 240 体素），始终在水下
	float MidOceanRidgeWidthRatio = 0.08f;   // 洋中脊宽度（方向单位）
	float RiftDepthRatio = 0.004f;        // 大陆裂谷地堑下陷（160 体素），谷底不低于海平面
	float RiftWidthRatio = 0.015f;        // 裂谷宽度
	float OldOrogenHeightRatio = 0.008f;  // 古老造山带高度（320 体素），低矮圆缓
	float OldOrogenDensity = 3.f;         // 古老造山带主脊疏密：越大主脊间距越小
	float OldOrogenWidthRatio = 0.045f;   // 主脊到山脚的辐射距离（方向单位，R=40000 时约 1800 体素）
	float OldOrogenSpurStrength = 0.6f;   // 支脉强度：0 只有主脊 + 平滑山坡，1 山坡全是垂直主脊伸出的支脉
	float OldOrogenCoverage = 0.4f;       // 保留多少条主脊：0 没有，1 全部
	float OldOrogenGapFraction = 0.3f;    // 沿走向断开 / 压成垭口的比例
	float OldOrogenWidthVariation = 0.5f; // 宽窄收放幅度：0 等宽，1 最窄处只剩三成
};

struct FPlanetVolcanoParams
{
	bool bVolcanoes = true;               // 整段火山开关；关掉时只跳过放置，高程场照常生成
	bool bArcVolcanoes = true;            // 俯冲弧层状火山
	bool bHotspots = true;                // 热点盾状火山链
	bool bFloodBasalt = true;             // 大陆裂谷溢流玄武岩台地
	bool bCalderas = true;                // 大火山顶部塌陷成破火山口
	bool bCinderCones = true;             // 活火山周围的火山渣锥
	float StratoBaseRatio = 0.024f;       // 层状火山底半径（方向单位，R=40000 时约 960 体素）
	float StratoHeightRatio = 0.012f;     // 层状火山高（480 体素）
	float StratoCraterRatio = 0.004f;     // 火口半径
	float CraterDepthRatio = 0.002f;      // 火口深（80 体素）
	float ArcSpacingRatio = 0.04f;        // 沿弧相邻火山间距
	float ArcOffsetRatio = 0.03f;         // 火山弧离板块边界的距离（上盘一侧）
	float ArcActiveFraction = 0.5f;       // 弧火山活跃比例（挤得越快越活跃）
	float ShieldBaseRatio = 0.04f;        // 盾状火山底半径
	float ShieldHeightRatio = 0.008f;     // 盾状火山高
	int32 HotspotCount = 4;               // 地幔柱热点数
	float HotspotSpacingRatio = 0.06f;    // 热点链上相邻火山间距
	int32 HotspotChainLength = 6;         // 每条链最多几座
	float FloodBasaltChance = 0.3f;       // 每段裂谷出现溢流玄武岩的概率
	float FloodBasaltSizeRatio = 0.06f;   // 溢流玄武岩台地半宽
	float FloodBasaltHeightRatio = 0.003f;// 台地高
	float CalderaFraction = 0.15f;        // 大火山中塌成破火山口的比例
	float CalderaRadiusRatio = 0.016f;    // 破火山口半径（不超过底半径的六成）
	float CalderaDepthRatio = 0.008f;     // 破火山口深
	int32 CinderPerVolcano = 4;           // 每座活火山周围的渣锥个数
	float CinderBaseRatio = 0.006f;       // 渣锥底半径
	float CinderHeightRatio = 0.002f;     // 渣锥高
};

/** 阶段 4b 地下侵入体：尺度同样是占半径的比例，深度 = 比例 × Radius 体素。 */
struct FPlanetIntrusionParams
{
	bool bIntrusions = true;              // 整段岩体开关；关掉时蚀变阈值照写
	bool bBatholiths = true;              // 岩基：弧火山段下方的大花岗岩体
	bool bStocks = true;                  // 岩株：每座弧火山正下方的小侵入体（4c 斑岩铜宿主）
	bool bDikes = true;                   // 岩墙：裂谷处近垂直的薄板
	bool bSills = true;                   // 岩床：溢流玄武岩台地与克拉通下的近水平薄板
	bool bLayeredIntrusions = true;       // 层状侵入体：古老克拉通的叠层（4c 铬铂宿主）
	bool bKimberlites = true;             // 金伯利岩管（4c 金刚石宿主）
	bool bAlteration = true;              // 岩体周围的蚀变晕（只写材质线索）
	float BatholithLongRatio = 0.08f;     // 岩基长半径（占 R）
	float BatholithShortRatio = 0.04f;    // 岩基短半径
	float BatholithThickRatio = 0.03f;    // 岩基竖直半径
	float BatholithTopDepthRatio = 0.05f; // 岩基顶深
	int32 BatholithMinVolcanoes = 3;      // 段内至少几座弧火山才长岩基
	float StockRadiusRatio = 0.008f;      // 岩株半径
	float StockTopDepthRatio = 0.03f;     // 岩株顶深
	int32 DikePerRift = 2;                // 每条裂谷的岩墙数
	float DikeThicknessRatio = 0.002f;    // 岩墙厚（半径）
	float DikeLengthRatio = 0.02f;        // 岩墙半高
	float DikeTopDepthRatio = 0.02f;      // 岩墙顶深
	float SillRadiusRatio = 0.05f;        // 岩床半径
	float SillThicknessRatio = 0.002f;    // 岩床厚（半）
	float SillTopDepthRatio = 0.02f;      // 岩床顶深
	float CratonSillDensity = 0.4f;       // 克拉通内岩床撒布密度
	int32 LayeredCount = 4;               // 层状侵入体每组的层数
	float LayeredRadiusRatio = 0.03f;     // 单层半径
	float LayeredThicknessRatio = 0.0025f;// 单层半厚
	float LayeredGapRatio = 0.004f;       // 层间距
	float LayeredTopDepthRatio = 0.06f;   // 整组顶深
	float KimberliteRadiusRatio = 0.002f; // 岩管半径
	float KimberliteHalfHeightRatio = 0.01f; // 岩管半高
	float KimberliteTopDepthRatio = 0.004f;  // 岩管顶深
	int32 KimberliteMax = 8;              // 全局最多几根
	bool bCarbonatites = true;            // 大陆裂谷的碳酸岩体（4c 稀土的宿主）
	float CarbonatiteRadiusRatio = 0.006f;     // 碳酸岩半径
	float CarbonatiteTopDepthRatio = 0.01f;    // 碳酸岩顶深
	float CarbonatiteChance = 0.35f;           // 每段裂谷出现概率
	float CarbonatiteSpacingRatio = 0.12f;     // 候选去重间距
	float AlterHaloScale = 2.2f;          // 岩株晕半径倍数（岩基固定 1.25）
	float AlterKeratiticBand = 0.3f;      // 钾化外边界（归一化距离）
	float AlterSericiticBand = 0.55f;     // 绢英岩化外边界
	float AlterArgillicBand = 0.8f;       // 泥化外边界
};

/** 阶段 5 气候：温度归一化 0–1；风只按纬度分带存方位角。 */
struct FPlanetClimateParams
{
	int32 ClimateRes = 256;          // 气候场每面分辨率，与 FaceRes 解耦
	float AxialTiltDeg = 23.4f;      // 自转轴倾角：只记录，本轮不模拟季节
	float EquatorTemp = 1.f;         // 赤道归一化温度
	float PoleTemp = 0.05f;          // 极点归一化温度
	float LapseRate = 0.35f;         // 高程到 0.08R 时扣掉的温度份额
	float Continentality = 0.15f;    // 大陆度对温度的影响幅度
	float BaseEquator = 0.9f;        // 赤道降水基准
	float BaseDry = 0.15f;           // 30° 干旱带降水基准
	float BaseMid = 0.7f;            // 中纬（45°）降水基准
	float BasePole = 0.15f;          // 极区降水基准
	float OceanFalloff = 0.3f;       // 离海衰减幅度
	float RainShadow = 0.8f;         // 雨影每单位抬升扣掉的水汽份额
	int32 ShadowSteps = 32;          // 逆风回溯步数上限
	int32 ShadowStride = 2;          // 每步跨几个气候 texel
	float PrecipBias = 0.f;          // 全局降水偏移（调参用）
	// 群系阈值：查表顺序固定（见 Stage05::BiomeFor），首个命中即返回。
	float BiomeIceTemp = 0.2f;          // 冰原：温度低于它
	float BiomeDesertPrecip = 0.2f;     // 沙漠：降水低于它
	float BiomeTundraTemp = 0.35f;      // 苔原：温度低于它
	float BiomeForestTemp = 0.55f;      // 针叶林：温度低于它
	float BiomeForestPrecip = 0.35f;    // 针叶林：降水高于它
	float BiomeRainTemp = 0.7f;         // 雨林：温度高于它
	float BiomeRainPrecip = 0.6f;       // 雨林：降水高于它
	float BiomeSavannaTemp = 0.75f;     // 稀树草原：温度高于它
	float BiomeTemperatePrecip = 0.5f;  // 温带林：降水高于它
};

/** 阶段 6 水系与剥蚀：尺寸一律占半径比例；汇流量按「全球面积 × 降水」归一化。 */
struct FPlanetHydroParams
{
	bool bRivers = true;                 // 是否刻河谷
	float RiverThreshold = 0.0008f;      // 成为河道的最小汇流量
	float CarveRatio = 0.002f;           // 下切强度（占 R）
	float CarveMaxRatio = 0.008f;        // 最大下切深度（占 R）
	float ValleyWidthRatio = 0.006f;     // 谷半宽（方向单位，随汇流增大）
	float SlopeRef = 0.02f;              // 坡度参考值（高程差 / 弧长）
	float ErosionRatio = 0.01f;          // 剥蚀强度（占 R）
	float EndorheicPrecip = 0.3f;        // 洼地集水区平均降水低于它即内流盆地
	float FlatSlopeRatio = 1e-6f;        // 填洼后保留的极小坡度
	float MountainErosionWeight = 0.7f;  // 山带对剥蚀的加权
};

/** 阶段 6b 出露结算：把 6a 的剥蚀深度换算成原语的径向上抬量。 */
struct FPlanetExposureParams
{
	bool bExposure = true;       // 是否做出露结算
	float LiftScale = 1.0f;      // 抬升量占剥蚀深度的比例（1 = 完全抬升）
	float MaxLiftRatio = 0.02f;  // 单个原语最大抬升（占 R），防止极端剥蚀把矿整体顶到地表
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
	/** 调试用阶段开关（位 = 阶段号 − 1）。被禁用的阶段整段跳过，后续阶段若读它的场会在校验里报错。 */
	uint32 DisabledStages = 0;
	FPlanetSphereParams Sphere;
	FPlanetPlateParams Plates;
	FPlanetContinentParams Continents;
	FPlanetTerrainParams Terrain;
	FPlanetVolcanoParams Volcano;
	FPlanetIntrusionParams Intrusion;
	FPlanetClimateParams Climate;
	FPlanetHydroParams Hydro;
	FPlanetExposureParams Exposure;
	/** 撒矿规则（来自配方的撒矿器列表，已过滤掉停用项）。规则不可变，后台线程只读。 */
	TArray<TSharedPtr<const IGeoMineralRule>> MineralRules;
	/** 矿物调色板，只供显示，不进哈希。 */
	TArray<FGeoMineralInfo> Minerals;

	uint64 Hash() const
	{
		uint64 H = GeoHash::Float(float(Radius));
		H = GeoHash::Combine(H, Seed);
		H = GeoHash::Combine(H, uint64(FaceRes));
		H = GeoHash::Combine(H, uint64(DisabledStages));
		const float S[6] = { Sphere.CrustRatio, Sphere.UpperMantleRatio, Sphere.TransitionRatio,
			Sphere.MantleOuterRatio, Sphere.OuterCoreRatio, Sphere.MohoWobbleRatio };
		for (const float V : S) { H = GeoHash::Combine(H, GeoHash::Float(V)); }
		H = GeoHash::Combine(H, uint64(Sphere.WobbleOctaves));
		H = GeoHash::Combine(H, uint64(Sphere.bMoho));
		H = GeoHash::Combine(H, uint64(Plates.PlateCount));
		H = GeoHash::Combine(H, uint64(Plates.WarpOctaves));
		const float P[11] = { Plates.PlateWeightMin, Plates.PlateWeightMax, Plates.WarpAmp,
			Plates.AgeScaleRatio, Plates.BoundaryBandRatio, Plates.WarpFrequency, Plates.DetailWarpAmp, Plates.DetailWarpFrequency,
			Plates.SpeedMin, Plates.SpeedMax, Plates.TransformShearRatio };
		for (const FPlanetPlateOverride& O : Plates.Overrides)
		{
			H = GeoHash::Combine(H, uint64(uint32(O.Plate)) | (uint64(O.bSpeed) << 32) | (uint64(O.bDirection) << 33));
			H = GeoHash::Combine(GeoHash::Combine(H, GeoHash::Float(O.Speed)), GeoHash::Float(O.DirectionDeg));
		}
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
		const float Tr[25] = { T.MountainHeightRatio, T.TrenchDepthRatio, T.PlainReliefRatio, T.BeltWidthRatio, T.TrenchWidthRatio,
			T.RidgeWavelengthRatio, T.WarpRatio, T.ErosionStrength, T.TalusAngleDeg, T.TrenchSedimentFill, T.TrenchSlump, T.TrenchWedge, T.SpeedWidthEffect, T.SpeedHeightEffect,
			T.MidOceanRidgeHeightRatio, T.MidOceanRidgeWidthRatio, T.RiftDepthRatio, T.RiftWidthRatio, T.OldOrogenHeightRatio, T.OldOrogenDensity,
			T.OldOrogenWidthRatio, T.OldOrogenSpurStrength, T.OldOrogenCoverage, T.OldOrogenGapFraction, T.OldOrogenWidthVariation };
		for (const float V : Tr) { H = GeoHash::Combine(H, GeoHash::Float(V)); }
		const FPlanetVolcanoParams& Vp = Volcano;
		H = GeoHash::Combine(H, uint64(Vp.bArcVolcanoes) | (uint64(Vp.bHotspots) << 1) | (uint64(Vp.bFloodBasalt) << 2)
			| (uint64(Vp.bCalderas) << 3) | (uint64(Vp.bCinderCones) << 4) | (uint64(Vp.bVolcanoes) << 5));
		H = GeoHash::Combine(H, uint64(uint32(Vp.HotspotCount)) | (uint64(uint32(Vp.HotspotChainLength)) << 32));
		H = GeoHash::Combine(H, uint64(uint32(Vp.CinderPerVolcano)));
		const float Vo[18] = { Vp.StratoBaseRatio, Vp.StratoHeightRatio, Vp.StratoCraterRatio, Vp.CraterDepthRatio, Vp.ArcSpacingRatio,
			Vp.ArcOffsetRatio, Vp.ArcActiveFraction, Vp.ShieldBaseRatio, Vp.ShieldHeightRatio, Vp.HotspotSpacingRatio, Vp.FloodBasaltChance,
			Vp.FloodBasaltSizeRatio, Vp.FloodBasaltHeightRatio, Vp.CalderaFraction, Vp.CalderaRadiusRatio, Vp.CalderaDepthRatio,
			Vp.CinderBaseRatio, Vp.CinderHeightRatio };
		for (const float V : Vo) { H = GeoHash::Combine(H, GeoHash::Float(V)); }
		const FPlanetIntrusionParams& In = Intrusion;
		H = GeoHash::Combine(H, uint64(In.bBatholiths) | (uint64(In.bStocks) << 1) | (uint64(In.bDikes) << 2)
			| (uint64(In.bSills) << 3) | (uint64(In.bLayeredIntrusions) << 4) | (uint64(In.bKimberlites) << 5)
			| (uint64(In.bAlteration) << 6) | (uint64(In.bCarbonatites) << 7) | (uint64(In.bIntrusions) << 8));
		H = GeoHash::Combine(H, uint64(uint32(In.BatholithMinVolcanoes)) | (uint64(uint32(In.DikePerRift)) << 32));
		H = GeoHash::Combine(H, uint64(uint32(In.LayeredCount)) | (uint64(uint32(In.KimberliteMax)) << 32));
		const float InF[28] = { In.BatholithLongRatio, In.BatholithShortRatio, In.BatholithThickRatio, In.BatholithTopDepthRatio,
			In.StockRadiusRatio, In.StockTopDepthRatio, In.DikeThicknessRatio, In.DikeLengthRatio, In.DikeTopDepthRatio,
			In.SillRadiusRatio, In.SillThicknessRatio, In.SillTopDepthRatio, In.CratonSillDensity,
			In.LayeredRadiusRatio, In.LayeredThicknessRatio, In.LayeredGapRatio, In.LayeredTopDepthRatio,
			In.KimberliteRadiusRatio, In.KimberliteHalfHeightRatio, In.KimberliteTopDepthRatio,
			In.AlterHaloScale, In.AlterKeratiticBand, In.AlterSericiticBand, In.AlterArgillicBand,
			In.CarbonatiteRadiusRatio, In.CarbonatiteTopDepthRatio, In.CarbonatiteChance, In.CarbonatiteSpacingRatio };
		for (const float V : InF) { H = GeoHash::Combine(H, GeoHash::Float(V)); }
		H = GeoHash::Combine(H, uint64(uint32(Climate.ClimateRes))
			| (uint64(uint32(Climate.ShadowSteps)) << 32));
		H = GeoHash::Combine(H, uint64(uint32(Climate.ShadowStride)));
		const float ClF[21] = { Climate.AxialTiltDeg, Climate.EquatorTemp, Climate.PoleTemp, Climate.LapseRate, Climate.Continentality,
			Climate.BaseEquator, Climate.BaseDry, Climate.BaseMid, Climate.BasePole, Climate.OceanFalloff,
			Climate.RainShadow, Climate.PrecipBias,
			Climate.BiomeIceTemp, Climate.BiomeDesertPrecip, Climate.BiomeTundraTemp, Climate.BiomeForestTemp,
			Climate.BiomeForestPrecip, Climate.BiomeRainTemp, Climate.BiomeRainPrecip, Climate.BiomeSavannaTemp,
			Climate.BiomeTemperatePrecip };
		for (const float V : ClF) { H = GeoHash::Combine(H, GeoHash::Float(V)); }
		const float HyF[9] = { Hydro.RiverThreshold, Hydro.CarveRatio, Hydro.CarveMaxRatio, Hydro.ValleyWidthRatio, Hydro.SlopeRef,
			Hydro.ErosionRatio, Hydro.EndorheicPrecip, Hydro.FlatSlopeRatio, Hydro.MountainErosionWeight };
		for (const float V : HyF) { H = GeoHash::Combine(H, GeoHash::Float(V)); }
		H = GeoHash::Combine(H, uint64(Hydro.bRivers));
		H = GeoHash::Combine(H, uint64(Exposure.bExposure));
		H = GeoHash::Combine(H, GeoHash::Combine(GeoHash::Float(Exposure.LiftScale), GeoHash::Float(Exposure.MaxLiftRatio)));
		for (const TSharedPtr<const IGeoMineralRule>& Rule : MineralRules)
		{
			if (Rule) { H = GeoHash::Combine(H, Rule->Hash()); }
		}
		return H;
	}
};
