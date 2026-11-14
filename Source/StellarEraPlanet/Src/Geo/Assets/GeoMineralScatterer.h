// 撒矿器：一种成矿规律一个子类，挂在配方的撒矿器列表里，可增删、排序、复制、叠加多份。
// 游戏线程上 MakeRule() 拷成不可变的烘焙规则，后台线程只碰规则不碰 UObject。
#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GeoMineralRule.h"
#include "GeoMineralScatterer.generated.h"

UCLASS(Abstract, EditInlineNew, DefaultToInstanced, CollapseCategories, BlueprintType)
class STELLARERAPLANET_API UGeoMineralScatterer : public UObject
{
	GENERATED_BODY()

public:
	/** 关掉 = 这一项不撒矿（保留参数，方便对比开关前后）。 */
	UPROPERTY(EditAnywhere, Category = "Scatter")
	bool bEnabled = true;

	/** 撒哪种矿：下拉框来自配方的矿物目录。目录里找不到时这一项被跳过。 */
	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (GetOptions = "GetMineralOptions"))
	FName Mineral;

	/** 种子偏移：同一种撒矿器叠两份时改它，两份才不会撒在同一处。 */
	UPROPERTY(EditAnywhere, Category = "Scatter")
	int32 SeedOffset = 0;

	/** 品位下限（1–255，越大越富）。 */
	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (ClampMin = "1", ClampMax = "255"))
	int32 GradeMin = 120;

	/** 品位上限（1–255）。 */
	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (ClampMin = "1", ClampMax = "255"))
	int32 GradeMax = 230;

	UFUNCTION()
	TArray<FName> GetMineralOptions() const;

	/** 子类把自己的参数拷进规则；矿物编号与公共参数由配方统一填。 */
	virtual TSharedPtr<IGeoMineralRule> MakeRule() const PURE_VIRTUAL(UGeoMineralScatterer::MakeRule, return nullptr;);
};

/** 条带状铁矿：古老陆壳内部、顺地层躺平的扁平层状体。运行于阶段 2 之后。 */
UCLASS(meta = (DisplayName = "条带状铁矿（阶段 2）"))
class STELLARERAPLANET_API UGeoBifScatterer : public UGeoMineralScatterer
{
	GENERATED_BODY()

public:
	UGeoBifScatterer() { Mineral = TEXT("BIF"); }

	/** 全球矿体个数。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "0", ClampMax = "4096"))
	int32 Count = 40;
	/** 只撒在地壳年龄（0 新 … 1 老）高于它的陆壳上。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "0", ClampMax = "1"))
	float AgeMin = 0.8f;
	/** 只撒在大陆度高于它的地方：越接近 1 越靠大陆内部。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "0", ClampMax = "1"))
	float ContinentalMin = 0.9f;
	/** 矿体长度（体素，沿走向）。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "1"))
	float LengthVoxels = 300.f;
	/** 矿体宽度（体素，垂直走向）。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "1"))
	float WidthVoxels = 120.f;
	/** 矿体厚度（体素，竖直方向）。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "1"))
	float ThicknessVoxels = 20.f;
	/** 矿体顶面埋深下限（体素，从地表往下）。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "0"))
	float DepthMinVoxels = 10.f;
	/** 矿体顶面埋深上限（体素）。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "0"))
	float DepthMaxVoxels = 200.f;

	virtual TSharedPtr<IGeoMineralRule> MakeRule() const override;
};

/** 块状硫化物：洋中脊（离散板块边界）海底下成串的透镜体。运行于阶段 2 之后。 */
UCLASS(meta = (DisplayName = "块状硫化物 VMS（阶段 2）"))
class STELLARERAPLANET_API UGeoVmsScatterer : public UGeoMineralScatterer
{
	GENERATED_BODY()

public:
	UGeoVmsScatterer() { Mineral = TEXT("VMS"); }

	/** 全球矿串条数。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "0", ClampMax = "4096"))
	int32 Chains = 30;
	/** 每串最少几个透镜体。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "1", ClampMax = "32"))
	int32 LensMin = 3;
	/** 每串最多几个透镜体（走出洋中脊会提前截断）。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "1", ClampMax = "32"))
	int32 LensMax = 6;
	/** 相邻透镜体间距（体素）。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "1"))
	float SpacingVoxels = 200.f;
	/** 透镜体半径（体素）。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "1"))
	float LensRadiusVoxels = 50.f;
	/** 透镜体厚度（体素）。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "1"))
	float LensThicknessVoxels = 20.f;
	/** 顶面埋深下限（体素，从海底往下）。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "0"))
	float DepthMinVoxels = 5.f;
	/** 顶面埋深上限（体素）。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "0"))
	float DepthMaxVoxels = 60.f;

	virtual TSharedPtr<IGeoMineralRule> MakeRule() const override;
};

/** 锰结核：古老洋壳、远离大陆的海底表层斑块（覆层，不是矿体）。运行于阶段 2 之后。 */
UCLASS(meta = (DisplayName = "锰结核（阶段 2）"))
class STELLARERAPLANET_API UGeoNoduleScatterer : public UGeoMineralScatterer
{
	GENERATED_BODY()

public:
	UGeoNoduleScatterer() { Mineral = TEXT("MnNodule"); }

	/** 只撒在地壳年龄高于它的洋壳上。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "0", ClampMax = "1"))
	float AgeMin = 0.7f;
	/** 只撒在大陆度低于它的地方：越小离大陆越远。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "0", ClampMax = "1"))
	float ContinentalMax = 0.02f;
	/** 符合条件的洋底里大约多少比例有结核（近似值）。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "0", ClampMax = "1"))
	float Coverage = 0.5f;
	/** 斑块细碎程度：越大斑块越小越多。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "0.5", ClampMax = "64"))
	float PatchFrequency = 8.f;
	/** 结核层厚度（体素，从海底往下）。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "0.5"))
	float DepthVoxels = 2.f;

	virtual TSharedPtr<IGeoMineralRule> MakeRule() const override;
};
