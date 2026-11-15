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
	int32 Count = 400;
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
	/** 矿体埋深下限（体素，从地表往下）。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "0"))
	float DepthMinVoxels = 10.f;
	/** 矿体埋深上限（体素）。0 = 一直到地壳底：在整个地壳体积里按体积均匀撒。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "0"))
	float DepthMaxVoxels = 0.f;

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
	int32 Chains = 200;
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
	/** 埋深下限（体素，从海底往下）。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "0"))
	float DepthMinVoxels = 5.f;
	/** 埋深上限（体素）。0 = 一直到地壳底：在整个地壳体积里按体积均匀撒。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "0"))
	float DepthMaxVoxels = 0.f;

	virtual TSharedPtr<IGeoMineralRule> MakeRule() const override;
};

/** 蛇绿岩铬铁矿：陆-陆碰撞缝合带里成串的豆荚状透镜体。运行于阶段 3 之后。 */
UCLASS(meta = (DisplayName = "蛇绿岩铬铁矿（阶段 3）"))
class STELLARERAPLANET_API UGeoChromiteScatterer : public UGeoMineralScatterer
{
	GENERATED_BODY()

public:
	UGeoChromiteScatterer() { Mineral = TEXT("Chromite"); }

	/** 全球矿串条数。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "0", ClampMax = "4096"))
	int32 Chains = 80;
	/** 每串最少几个豆荚体。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "1", ClampMax = "32"))
	int32 PodMin = 3;
	/** 每串最多几个豆荚体（走出缝合带会提前截断）。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "1", ClampMax = "32"))
	int32 PodMax = 8;
	/** 相邻豆荚体间距（体素）。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "1"))
	float SpacingVoxels = 150.f;
	/** 豆荚体长半径（体素）。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "1"))
	float PodRadiusVoxels = 40.f;
	/** 豆荚体厚度（体素）。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "1"))
	float PodThicknessVoxels = 15.f;
	/** 埋深下限（体素，从地表往下）。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "0"))
	float DepthMinVoxels = 5.f;
	/** 埋深上限（体素）。0 = 一直到地壳底。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "0"))
	float DepthMaxVoxels = 0.f;

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

/** 浅成低温金银：活跃火山锥顶下方的脉群，火山竖向矿柱的中间一层。运行于阶段 4 之后。 */
UCLASS(meta = (DisplayName = "浅成低温金银脉（阶段 4）"))
class STELLARERAPLANET_API UGeoEpithermalScatterer : public UGeoMineralScatterer
{
	GENERATED_BODY()

public:
	UGeoEpithermalScatterer() { Mineral = TEXT("EpithermalAuAg"); }

	/** 脉体中心深度下限（占 R，从地表往下）。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "0", ClampMax = "0.2"))
	float DepthMinRatio = 0.004f;
	/** 脉体中心深度上限（占 R）：每条脉在这段区间里各取一个深度。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "0", ClampMax = "0.2"))
	float DepthMaxRatio = 0.012f;
	/** 每座火山的脉体条数。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "1", ClampMax = "16"))
	int32 VeinCount = 3;
	/** 单条脉水平半径（占 R）。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "0.0005", ClampMax = "0.05"))
	float VeinRadiusRatio = 0.004f;
	/** 单条脉半厚（占 R），贴着球面躺平的那个方向。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "0.0002", ClampMax = "0.02"))
	float VeinThicknessRatio = 0.0015f;
	/** 整簇脉体的水平总宽 = 该倍数 × 火口半径。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "0", ClampMax = "16"))
	float SpreadRatio = 2.f;

	virtual TSharedPtr<IGeoMineralRule> MakeRule() const override;
};

/** 斑岩铜：闪长岩岩株顶面下方的浸染体，火山竖向矿柱最深的一层。运行于阶段 4b 之后。 */
UCLASS(meta = (DisplayName = "斑岩铜浸染体（阶段 4）"))
class STELLARERAPLANET_API UGeoPorphyryScatterer : public UGeoMineralScatterer
{
	GENERATED_BODY()

public:
	UGeoPorphyryScatterer() { Mineral = TEXT("PorphyryCu"); }

	/** 浸染体中心到岩株顶面的距离下限（占 R）。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "0", ClampMax = "0.2"))
	float DepthMinRatio = 0.012f;
	/** 距离上限（占 R）：PerStock 个浸染体在这段区间里自上而下铺开。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "0", ClampMax = "0.2"))
	float DepthMaxRatio = 0.03f;
	/** 浸染体半径（占 R）：必须 ≤ 中心下移下限，否则浸染体顶面会抬到岩株顶面之上。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "0.001", ClampMax = "0.012"))
	float RadiusRatio = 0.01f;
	/** 每座岩株几个。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "1", ClampMax = "8"))
	int32 PerStock = 1;

	virtual TSharedPtr<IGeoMineralRule> MakeRule() const override;
};

/** 锡钨脉：花岗岩基顶面壳层里沿长轴拉长的细脉。运行于阶段 4b 之后。 */
UCLASS(meta = (DisplayName = "锡钨脉（阶段 4）"))
class STELLARERAPLANET_API UGeoTinTungstenScatterer : public UGeoMineralScatterer
{
	GENERATED_BODY()

public:
	UGeoTinTungstenScatterer() { Mineral = TEXT("TinTungsten"); }

	/** 每座岩基几条脉。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "1", ClampMax = "16"))
	int32 PerBatholith = 4;
	/** 脉长半轴（占 R，沿岩基长轴）。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "0.001", ClampMax = "0.5"))
	float LengthRatio = 0.02f;
	/** 脉横向半轴（占 R）。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "0.0005", ClampMax = "0.1"))
	float RadiusRatio = 0.004f;
	/** 脉半厚（占 R，岩基局部 Z）。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "0.0002", ClampMax = "0.05"))
	float ThicknessRatio = 0.0015f;
	/** 脉中心在岩基局部 z 的比例下限（0 = 中心，1 = 顶面）。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "0", ClampMax = "1"))
	float TopMinRatio = 0.6f;
	/** 比例上限：取 1 时脉心落在岩基顶面，上半截冒出宿主。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "0", ClampMax = "1"))
	float TopMaxRatio = 0.95f;

	virtual TSharedPtr<IGeoMineralRule> MakeRule() const override;
};

/** 伟晶岩锂钽铍：花岗岩基边缘壳层里的团块。运行于阶段 4b 之后。 */
UCLASS(meta = (DisplayName = "伟晶岩锂钽铍（阶段 4）"))
class STELLARERAPLANET_API UGeoPegmatiteScatterer : public UGeoMineralScatterer
{
	GENERATED_BODY()

public:
	UGeoPegmatiteScatterer() { Mineral = TEXT("PegmatiteLi"); }

	/** 每座岩基几个。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "1", ClampMax = "16"))
	int32 PerBatholith = 6;
	/** 团块半径（占 R）。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "0.0005", ClampMax = "0.05"))
	float RadiusRatio = 0.003f;
	/** 团块中心到岩基中心的局部半径比例下限。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "0", ClampMax = "1"))
	float ShellMin = 0.88f;
	/** 比例上限：1 = 团块中心落在岩基表面，半径有一半在宿主外。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "0", ClampMax = "1"))
	float ShellMax = 1.f;

	virtual TSharedPtr<IGeoMineralRule> MakeRule() const override;
};

/** 岩浆铜镍：辉长岩床 / 岩墙底部贴着宿主朝向的扁平硫化物矿体。运行于阶段 4b 之后。 */
UCLASS(meta = (DisplayName = "岩浆铜镍（阶段 4）"))
class STELLARERAPLANET_API UGeoCopperNickelScatterer : public UGeoMineralScatterer
{
	GENERATED_BODY()

public:
	UGeoCopperNickelScatterer() { Mineral = TEXT("CopperNickel"); }

	/** 每个辉长岩宿主几个矿体。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "1", ClampMax = "8"))
	int32 PerHost = 1;
	/** 水平半轴 = 该倍数 × 宿主水平半轴：宿主越宽矿体越大。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "0.05", ClampMax = "1"))
	float WidthScale = 0.7f;
	/** 半厚 = 该倍数 × 宿主竖直半轴；> 0.5 时矿体底戳出宿主底（< −1）、顶越过宿主中线（> 0）。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "0.05", ClampMax = "0.5"))
	float ThicknessScale = 0.3f;

	virtual TSharedPtr<IGeoMineralRule> MakeRule() const override;
};

/** 层状铬铂：层状镁铁岩每片薄板内、与宿主同中心同朝向的薄层。运行于阶段 4b 之后。 */
UCLASS(meta = (DisplayName = "层状铬铂（阶段 4）"))
class STELLARERAPLANET_API UGeoChromiumPGEScatterer : public UGeoMineralScatterer
{
	GENERATED_BODY()

public:
	UGeoChromiumPGEScatterer() { Mineral = TEXT("ChromiumPGE"); }

	/** 每片薄板几层。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "1", ClampMax = "8"))
	int32 PerSlab = 1;
	/** 半厚 = 该倍数 × 宿主竖直半轴；≤ 0.5 才是夹在薄板里的一层，取 1 就成了宿主副本。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "0.05", ClampMax = "0.5"))
	float ThicknessScale = 0.4f;

	virtual TSharedPtr<IGeoMineralRule> MakeRule() const override;
};

/** 金刚石：金伯利岩管内的含矿小团块，只有一部分岩管含矿。运行于阶段 4b 之后。 */
UCLASS(meta = (DisplayName = "金刚石（阶段 4）"))
class STELLARERAPLANET_API UGeoDiamondScatterer : public UGeoMineralScatterer
{
	GENERATED_BODY()

public:
	UGeoDiamondScatterer() { Mineral = TEXT("Diamond"); }

	/** 每根含矿岩管几颗。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "1", ClampMax = "8"))
	int32 PerPipe = 1;
	/** 团块半径（占 R）：别超过岩管半径（默认 0.002R），否则会戳出管壁。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "0.0002", ClampMax = "0.002"))
	float RadiusRatio = 0.0012f;
	/** 每根岩管含矿的概率：越小越稀有（默认 0.15）。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "0", ClampMax = "1"))
	float FertileChance = 0.15f;

	virtual TSharedPtr<IGeoMineralRule> MakeRule() const override;
};

/** 碳酸岩稀土：碳酸岩体中心、比宿主小一圈的团块。运行于阶段 4b 之后。 */
UCLASS(meta = (DisplayName = "碳酸岩稀土（阶段 4）"))
class STELLARERAPLANET_API UGeoRareEarthScatterer : public UGeoMineralScatterer
{
	GENERATED_BODY()

public:
	UGeoRareEarthScatterer() { Mineral = TEXT("RareEarth"); }

	/** 每个碳酸岩体几颗。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "1", ClampMax = "8"))
	int32 PerBody = 1;
	/** 团块半径 = 该倍数 × 宿主半径：必须 ≤ 1，否则矿体冒出碳酸岩。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "0.05", ClampMax = "1"))
	float RadiusScale = 0.6f;

	virtual TSharedPtr<IGeoMineralRule> MakeRule() const override;
};

/** 硫磺结壳：活跃火山火口内的地表覆层（覆层，不是矿体）。运行于阶段 4 之后。 */
UCLASS(meta = (DisplayName = "硫磺结壳（阶段 4）"))
class STELLARERAPLANET_API UGeoSulfurScatterer : public UGeoMineralScatterer
{
	GENERATED_BODY()

public:
	UGeoSulfurScatterer() { Mineral = TEXT("Sulfur"); }

	/** 结壳半径下限（占 R）：火口半径比它小时按它算，否则遮罩上不足一格。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "0.001", ClampMax = "0.1"))
	float RadiusRatio = 0.008f;
	/** 结壳厚度（体素，从地表往下）。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "2", ClampMax = "400"))
	float DepthVoxels = 40.f;

	virtual TSharedPtr<IGeoMineralRule> MakeRule() const override;
};
