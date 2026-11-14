// 一颗星的地质配方资产：只放「地质怎么长」。半径归行星、体素尺寸归体素世界，这里不再存。
#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PlanetRecipeConfig.h"
#include "PlanetRecipe.generated.h"

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

	/** 最高陆地高出海平面多少，单位体素。 */
	UPROPERTY(EditAnywhere, Category = "Stage2|Continents", meta = (ClampMin = "0"))
	float LandHeightVoxels = 240.f;

	/** 最深洋盆低于海平面多少，单位体素。 */
	UPROPERTY(EditAnywhere, Category = "Stage2|Continents", meta = (ClampMin = "1"))
	float OceanDepthVoxels = 480.f;

	/** 拷成烘焙用的 POD。半径由行星给（行星是半径的唯一真源），配方本身不存。 */
	FPlanetRecipeConfig ToConfig(double RadiusVoxels) const;
};
