// 一颗星的地质配方资产：只放「地质怎么长」。半径归行星、体素尺寸归体素世界，这里不再存。
#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PlanetRecipeConfig.h"
#include "PlanetRecipe.generated.h"

class UGeoMineralCatalog;
class UGeoMineralScatterer;

UCLASS(BlueprintType)
class STELLARERAPLANET_API UPlanetRecipe : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** 随机种子：换一个数就是另一套大陆布局与圈层起伏，其它参数含义不变。 */
	UPROPERTY(EditAnywhere, Category = "Planet")
	int32 Seed = 1337;

	/** 烘焙贴图每个立方体面的分辨率。越大海岸越细、烘焙越慢；半径 40000 时 1024 一格约 60 体素。 */
	UPROPERTY(EditAnywhere, Category = "Planet", meta = (ClampMin = "32", ClampMax = "2048"))
	int32 FaceRes = 1024;

	// 阶段 1：圈层。下面五项是「从地表往下的累计深度 ÷ 半径」，必须一项比一项大。
	/** 地壳底界深度占半径比例。 */
	UPROPERTY(EditAnywhere, Category = "Stage1|Layers", meta = (ClampMin = "0.01", ClampMax = "0.99"))
	float CrustRatio = 0.25f;
	/** 上地幔底界深度占半径比例。 */
	UPROPERTY(EditAnywhere, Category = "Stage1|Layers", meta = (ClampMin = "0.01", ClampMax = "0.99"))
	float UpperMantleRatio = 0.38f;
	/** 地幔过渡带底界深度占半径比例。 */
	UPROPERTY(EditAnywhere, Category = "Stage1|Layers", meta = (ClampMin = "0.01", ClampMax = "0.99"))
	float TransitionRatio = 0.42f;
	/** 下地幔底界（核幔边界）深度占半径比例。 */
	UPROPERTY(EditAnywhere, Category = "Stage1|Layers", meta = (ClampMin = "0.01", ClampMax = "0.99"))
	float MantleOuterRatio = 0.55f;
	/** 外核底界深度占半径比例；再往里是内核。 */
	UPROPERTY(EditAnywhere, Category = "Stage1|Layers", meta = (ClampMin = "0.01", ClampMax = "0.99"))
	float OuterCoreRatio = 0.80f;

	/** 莫霍面（地壳 / 地幔分界）起伏幅度占半径比例；0 = 正球面。 */
	UPROPERTY(EditAnywhere, Category = "Stage1|Moho", meta = (ClampMin = "0", ClampMax = "0.1"))
	float MohoWobbleRatio = 0.01f;
	/** 莫霍面起伏的噪声层数：越多细节越碎。 */
	UPROPERTY(EditAnywhere, Category = "Stage1|Moho", meta = (ClampMin = "1", ClampMax = "6"))
	int32 WobbleOctaves = 3;

	/** 板块数量（3–15）。只决定板块边界位置，给阶段 3 造山用；不影响大陆形状。 */
	UPROPERTY(EditAnywhere, Category = "Stage2|Plates", meta = (ClampMin = "3", ClampMax = "15"))
	int32 PlateCount = 7;

	// 平铺而非 USTRUCT 镜像：USTRUCT 会把 CoreUObject 拖进 Geo/Bake，check_architecture 判层次违规。
	/** 大陆框架阶数：越大陆块越多越小。1–2 超大陆，4 类地球，6 以上配高海岸破碎度出群岛。 */
	UPROPERTY(EditAnywhere, Category = "Stage2|Continents", meta = (ClampMin = "1", ClampMax = "12"))
	int32 FrameDegree = 4;

	/** 陆地占星球表面积的比例（0.3 = 三成陆地七成海）。 */
	UPROPERTY(EditAnywhere, Category = "Stage2|Continents", meta = (ClampMin = "0.05", ClampMax = "0.95"))
	float LandFrac = 0.30f;

	/** 海岸破碎度：越大半岛、海湾、近海小岛越多；0 = 只有平滑的大轮廓。 */
	UPROPERTY(EditAnywhere, Category = "Stage2|Continents", meta = (ClampMin = "0", ClampMax = "3"))
	float CoastRoughness = 0.35f;

	/** 陆架浅海占海洋面积的比例：越大离岸越远才变深。 */
	UPROPERTY(EditAnywhere, Category = "Stage2|Continents", meta = (ClampMin = "0", ClampMax = "0.9"))
	float ShelfFrac = 0.15f;

	/** 最高陆地高出海平面多少，占半径比例（R=40000 时 0.006 ≈ 240 体素）。星球变大变小时形状不变。 */
	UPROPERTY(EditAnywhere, Category = "Stage2|Continents", meta = (ClampMin = "0", ClampMax = "0.1"))
	float LandHeightRatio = 0.006f;

	/** 最深洋盆低于海平面多少，占半径比例（R=40000 时 0.012 ≈ 480 体素）。 */
	UPROPERTY(EditAnywhere, Category = "Stage2|Continents", meta = (ClampMin = "0.0001", ClampMax = "0.1"))
	float OceanDepthRatio = 0.012f;

	// 阶段 3：宏观地形。全部占半径比例，括号里是 R=40000 时的体素数。
	/** 山脉起伏幅度：山顶 ≈ 陆台 + 0.8 × 它（0.05 ≈ 2000 体素）。 */
	UPROPERTY(EditAnywhere, Category = "Stage3|Terrain", meta = (ClampMin = "0", ClampMax = "0.2"))
	float MountainHeightRatio = 0.05f;

	/** 海沟在洋底之下再往下切多深（0.025 ≈ 1000 体素）。只在俯冲边界的洋侧出现。 */
	UPROPERTY(EditAnywhere, Category = "Stage3|Terrain", meta = (ClampMin = "0", ClampMax = "0.2"))
	float TrenchDepthRatio = 0.025f;

	/** 山带以外的平原丘陵起伏（0.002 ≈ 80 体素）。0 = 平原是平的。 */
	UPROPERTY(EditAnywhere, Category = "Stage3|Terrain", meta = (ClampMin = "0", ClampMax = "0.05"))
	float PlainReliefRatio = 0.002f;

	/** 造山带宽度（0.06 ≈ 2400 体素）。越大山脉越宽、覆盖面积越大。 */
	UPROPERTY(EditAnywhere, Category = "Stage3|Terrain", meta = (ClampMin = "0.002", ClampMax = "0.3"))
	float BeltWidthRatio = 0.06f;

	/** 山脊间距（0.06 ≈ 2400 体素）。越小山脊越密越碎。 */
	UPROPERTY(EditAnywhere, Category = "Stage3|Terrain", meta = (ClampMin = "0.005", ClampMax = "0.5"))
	float RidgeWavelengthRatio = 0.06f;

	/** 热力侵蚀遍数：越多山越圆、坡越缓。0 = 不侵蚀。 */
	UPROPERTY(EditAnywhere, Category = "Stage3|Terrain", meta = (ClampMin = "0", ClampMax = "50"))
	int32 ErosionIterations = 5;

	/** 侵蚀强度：每遍陡坡向周围平均高度靠拢的比例。 */
	UPROPERTY(EditAnywhere, Category = "Stage3|Terrain", meta = (ClampMin = "0", ClampMax = "1"))
	float ErosionStrength = 0.2f;

	/** 休止角（度）：比它陡的坡才会被侵蚀削平。 */
	UPROPERTY(EditAnywhere, Category = "Stage3|Terrain", meta = (ClampMin = "5", ClampMax = "80"))
	float TalusAngleDeg = 30.f;

	/** 矿物目录：编号、中文名、调试色。撒矿器只引用这里的编号。 */
	UPROPERTY(EditAnywhere, Category = "Minerals")
	TObjectPtr<UGeoMineralCatalog> MineralCatalog;

	/** 撒矿器列表：一项一种成矿规律，可增删、排序、复制；同一种可叠加多份（用种子偏移错开）。 */
	UPROPERTY(EditAnywhere, Instanced, Category = "Minerals")
	TArray<TObjectPtr<UGeoMineralScatterer>> MineralScatterers;

	/** 拷成烘焙用的 POD。半径由行星给（行星是半径的唯一真源），配方本身不存。 */
	FPlanetRecipeConfig ToConfig(double RadiusVoxels) const;
};
