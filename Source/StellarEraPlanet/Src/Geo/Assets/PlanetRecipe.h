// 一颗星的地质配方资产，作者唯一入口。烘焙前在游戏线程 ToConfig() 拷成 POD。
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
	/** 星球半径（体素）。UniverseMap 的行星是 40000。 */
	UPROPERTY(EditAnywhere, Category = "Planet", meta = (ClampMin = "256"))
	float Radius = 40000.f;

	/** 默认 50 与 FPlanetRecipeConfig 一致：Task 1 在 UniverseMap 量得 50 cm。 */
	UPROPERTY(EditAnywhere, Category = "Planet", meta = (ClampMin = "1"))
	float VoxelSizeCm = 50.f;

	UPROPERTY(EditAnywhere, Category = "Planet")
	int32 Seed = 1337;

	/** 宏观球面场每面分辨率。 */
	UPROPERTY(EditAnywhere, Category = "Planet", meta = (ClampMin = "32", ClampMax = "2048"))
	int32 FaceRes = 1024;

	/** 圈层底界：从地表向下的累计深度占 R，必须严格递增（阶段 1 校验）。 */
	UPROPERTY(EditAnywhere, Category = "Stage1|Layers", meta = (ClampMin = "0.01", ClampMax = "0.99"))
	float CrustRatio = 0.25f;
	UPROPERTY(EditAnywhere, Category = "Stage1|Layers", meta = (ClampMin = "0.01", ClampMax = "0.99"))
	float UpperMantleRatio = 0.38f;
	UPROPERTY(EditAnywhere, Category = "Stage1|Layers", meta = (ClampMin = "0.01", ClampMax = "0.99"))
	float TransitionRatio = 0.42f;
	UPROPERTY(EditAnywhere, Category = "Stage1|Layers", meta = (ClampMin = "0.01", ClampMax = "0.99"))
	float MantleOuterRatio = 0.55f;
	UPROPERTY(EditAnywhere, Category = "Stage1|Layers", meta = (ClampMin = "0.01", ClampMax = "0.99"))
	float OuterCoreRatio = 0.80f;

	UPROPERTY(EditAnywhere, Category = "Stage1|Moho", meta = (ClampMin = "0", ClampMax = "0.1"))
	float MohoWobbleRatio = 0.01f;
	UPROPERTY(EditAnywhere, Category = "Stage1|Moho", meta = (ClampMin = "1", ClampMax = "6"))
	int32 WobbleOctaves = 3;

	// 六个平铺 UPROPERTY 而非 FPlanetPlateParams 的 USTRUCT 镜像：USTRUCT 会把 CoreUObject
	// 拖进 Geo/Bake（该头只用 CoreMinimal.h + GeoRng.h），check_architecture 会判层次违规。
	/** 板块数，3–15；> 15 处处是山，< 3 出不了三节点。 */
	UPROPERTY(EditAnywhere, Category = "Stage2|Plates", meta = (ClampMin = "3", ClampMax = "15"))
	int32 PlateCount = 7;

	/** 目标陆地面积占比，无量纲 0–1；二分反解 α 收敛到它。 */
	UPROPERTY(EditAnywhere, Category = "Stage2|Plates", meta = (ClampMin = "0.05", ClampMax = "0.95"))
	float TargetLandFrac = 0.30f;

	/** 陆台高出海平面的高度，体素。 */
	UPROPERTY(EditAnywhere, Category = "Stage2|Plates", meta = (ClampMin = "0"))
	float LandTableVoxels = 240.f;

	/** 洋盆低于海平面的深度，体素。 */
	UPROPERTY(EditAnywhere, Category = "Stage2|Plates", meta = (ClampMin = "0"))
	float OceanBasinVoxels = 480.f;

	/** 板块风格低频起伏幅度，体素。 */
	UPROPERTY(EditAnywhere, Category = "Stage2|Plates", meta = (ClampMin = "0"))
	float StyleAmpVoxels = 120.f;

	/** 大陆坡水平宽度，体素。 */
	UPROPERTY(EditAnywhere, Category = "Stage2|Plates", meta = (ClampMin = "1"))
	float SlopeWidthVoxels = 240.f;

	FPlanetRecipeConfig ToConfig() const;
};
