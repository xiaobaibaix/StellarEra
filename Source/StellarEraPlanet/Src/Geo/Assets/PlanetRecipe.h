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

	FPlanetRecipeConfig ToConfig() const;
};
