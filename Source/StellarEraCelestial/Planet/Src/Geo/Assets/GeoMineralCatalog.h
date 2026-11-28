// 矿物目录：编号、中文名、调试色。撒矿器只按编号引用这里，增删矿物不改代码。
#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GeoMineralCatalog.generated.h"

USTRUCT(BlueprintType)
struct STELLARERACELESTIAL_API FGeoMineralDef
{
	GENERATED_BODY()

	/** 矿物编号（英文，撒矿器按它引用）。改名会让引用它的撒矿器找不到矿物而被跳过。 */
	UPROPERTY(EditAnywhere, Category = "Mineral")
	FName Id;

	/** 显示名（图例、读数里用）。 */
	UPROPERTY(EditAnywhere, Category = "Mineral")
	FString DisplayName;

	/** 调试显示色：查看器、星球调试着色里用，不是最终美术。 */
	UPROPERTY(EditAnywhere, Category = "Mineral")
	FLinearColor DebugColor = FLinearColor::White;
};

UCLASS(BlueprintType)
class STELLARERACELESTIAL_API UGeoMineralCatalog : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** 全部矿物；体素里存的矿物编号 = 这里的下标 + 1（0 = 无矿）。 */
	UPROPERTY(EditAnywhere, Category = "Minerals", meta = (TitleProperty = "Id"))
	TArray<FGeoMineralDef> Minerals;

	/** 找不到返回 INDEX_NONE。 */
	int32 FindIndex(FName Id) const;
};
