// 各阶段的内置成矿规律与矿物调试图层。
#pragma once
#include "CoreMinimal.h"
#include "GeoMineralRule.h"
#include "PlanetDebugLayer.h"

/** 条带状铁建造：古老陆壳内部，顺地层躺平的扁平层状体。 */
struct FGeoBifParams
{
	int32 Count = 400;
	float AgeMin = 0.8f;
	float ContinentalMin = 0.9f;
	float LengthVoxels = 300.f;
	float WidthVoxels = 120.f;
	float ThicknessVoxels = 20.f;
	float DepthMinVoxels = 10.f;
	float DepthMaxVoxels = 0.f;   // ≤ 0 = 到地壳底
};

class STELLARERAPLANET_API FGeoBifRule final : public IGeoMineralRule
{
public:
	FGeoBifParams P;
	virtual uint32 GetStage() const override { return 2; }
	virtual const TCHAR* GetRuleName() const override { return TEXT("条带状铁矿"); }
	virtual void Scatter(FGeoScatterContext& Ctx, FGeoScatterOutput& Out) const override;
	virtual uint64 HashParams() const override;
};

/** 块状硫化物 VMS：离散板块边界（洋中脊）的海底下，成串的透镜体。 */
struct FGeoVmsParams
{
	int32 Chains = 200;
	int32 LensMin = 3;
	int32 LensMax = 6;
	float SpacingVoxels = 200.f;
	float LensRadiusVoxels = 50.f;
	float LensThicknessVoxels = 20.f;
	float DepthMinVoxels = 5.f;
	float DepthMaxVoxels = 0.f;   // ≤ 0 = 到地壳底
};

class STELLARERAPLANET_API FGeoVmsRule final : public IGeoMineralRule
{
public:
	FGeoVmsParams P;
	virtual uint32 GetStage() const override { return 2; }
	virtual const TCHAR* GetRuleName() const override { return TEXT("块状硫化物"); }
	virtual void Scatter(FGeoScatterContext& Ctx, FGeoScatterOutput& Out) const override;
	virtual uint64 HashParams() const override;
};

/** 锰结核：古老洋壳、远离大陆的海底表层，斑块状覆层。 */
struct FGeoNoduleParams
{
	float AgeMin = 0.7f;
	float ContinentalMax = 0.02f;
	float Coverage = 0.5f;
	float PatchFrequency = 8.f;
	float DepthVoxels = 2.f;
};

class STELLARERAPLANET_API FGeoNoduleRule final : public IGeoMineralRule
{
public:
	FGeoNoduleParams P;
	virtual uint32 GetStage() const override { return 2; }
	virtual const TCHAR* GetRuleName() const override { return TEXT("锰结核"); }
	virtual void Scatter(FGeoScatterContext& Ctx, FGeoScatterOutput& Out) const override;
	virtual uint64 HashParams() const override;
};

/** 蛇绿岩铬铁矿：陆-陆碰撞缝合带（被推上陆的古洋壳碎片）里成串的豆荚状透镜体。 */
struct FGeoChromiteParams
{
	int32 Chains = 80;
	int32 PodMin = 3;
	int32 PodMax = 8;
	float SpacingVoxels = 150.f;
	float PodRadiusVoxels = 40.f;
	float PodThicknessVoxels = 15.f;
	float DepthMinVoxels = 5.f;
	float DepthMaxVoxels = 0.f;   // ≤ 0 = 到地壳底
};

class STELLARERAPLANET_API FGeoChromiteRule final : public IGeoMineralRule
{
public:
	FGeoChromiteParams P;
	virtual uint32 GetStage() const override { return 3; }
	virtual const TCHAR* GetRuleName() const override { return TEXT("蛇绿岩铬铁矿"); }
	virtual void Scatter(FGeoScatterContext& Ctx, FGeoScatterOutput& Out) const override;
	virtual uint64 HashParams() const override;
};

/** 硫磺结壳：活跃火山火口内的地表覆层（覆层，不是矿体）。运行于阶段 4 之后。 */
struct FGeoSulfurParams
{
	float RadiusRatio = 0.008f;   // 结壳半径下限（占 R）：火口比它小时用它
	float DepthVoxels = 40.f;     // 结壳厚度
};

class STELLARERAPLANET_API FGeoSulfurRule final : public IGeoMineralRule
{
public:
	FGeoSulfurParams P;
	virtual uint32 GetStage() const override { return 4; }
	virtual const TCHAR* GetRuleName() const override { return TEXT("Sulfur"); }
	virtual void Scatter(FGeoScatterContext& Ctx, FGeoScatterOutput& Out) const override;
	virtual uint64 HashParams() const override;
};

/** 浅成低温金银：活跃火山锥顶下方的脉群，火山竖向矿柱的中间一层。运行于阶段 4 之后。 */
struct FGeoEpithermalParams
{
	float DepthMinRatio = 0.004f;        // 脉体中心深度下限（占 R）
	float DepthMaxRatio = 0.012f;        // 脉体中心深度上限；每条脉在这段区间里取一个深度
	int32 VeinCount = 3;                 // 每座火山的脉体条数
	float VeinRadiusRatio = 0.004f;      // 单条脉水平半径
	float VeinThicknessRatio = 0.0015f;  // 单条脉半厚（局部 Z，贴着球面躺平）
	float SpreadRatio = 2.0f;            // 整簇脉体的水平总宽 = 该倍数 × 火口半径
};

class STELLARERAPLANET_API FGeoEpithermalRule final : public IGeoMineralRule
{
public:
	FGeoEpithermalParams P;
	virtual uint32 GetStage() const override { return 4; }
	virtual const TCHAR* GetRuleName() const override { return TEXT("Epithermal"); }
	virtual void Scatter(FGeoScatterContext& Ctx, FGeoScatterOutput& Out) const override;
	virtual uint64 HashParams() const override;
};

/** 斑岩铜：闪长岩岩株顶面下方的浸染体，火山竖向矿柱最深的一层。运行于阶段 4b 之后。 */
struct FGeoPorphyryParams
{
	float DepthMinRatio = 0.012f;  // 浸染体中心到岩株顶面的距离下限（占 R）
	float DepthMaxRatio = 0.03f;   // 上限；PerStock 个浸染体在这段区间里自上而下铺开
	float RadiusRatio = 0.01f;     // 浸染体半径
	int32 PerStock = 1;            // 每座岩株几个
};

class STELLARERAPLANET_API FGeoPorphyryRule final : public IGeoMineralRule
{
public:
	FGeoPorphyryParams P;
	virtual uint32 GetStage() const override { return 4; }
	virtual const TCHAR* GetRuleName() const override { return TEXT("Porphyry"); }
	virtual void Scatter(FGeoScatterContext& Ctx, FGeoScatterOutput& Out) const override;
	virtual uint64 HashParams() const override;
};

/** 锡钨：花岗岩基顶面壳层里沿长轴拉长的细脉。运行于阶段 4b 之后。 */
struct FGeoTinTungstenParams
{
	int32 PerBatholith = 4;         // 每座岩基几条
	float LengthRatio = 0.02f;      // 脉长半轴（占 R，沿宿主局部 X）
	float RadiusRatio = 0.004f;     // 脉横向半轴
	float ThicknessRatio = 0.0015f; // 脉半厚（局部 Z）
	float TopMinRatio = 0.6f;       // 中心在宿主局部 z 的比例下限
	float TopMaxRatio = 0.95f;      // 上限：脉顶压在岩基顶面之下，否则整株被包住
};

class STELLARERAPLANET_API FGeoTinTungstenRule final : public IGeoMineralRule
{
public:
	FGeoTinTungstenParams P;
	virtual uint32 GetStage() const override { return 4; }
	virtual const TCHAR* GetRuleName() const override { return TEXT("TinTungsten"); }
	virtual void Scatter(FGeoScatterContext& Ctx, FGeoScatterOutput& Out) const override;
	virtual uint64 HashParams() const override;
};

/** 伟晶岩锂钽铍：花岗岩基边缘壳层里的团块。运行于阶段 4b 之后。 */
struct FGeoPegmatiteParams
{
	int32 PerBatholith = 6;      // 每座岩基几个
	float RadiusRatio = 0.003f;  // 团块半径（占 R）
	float ShellMin = 0.88f;      // 到宿主中心的局部半径比例下限
	float ShellMax = 1.f;        // 上限：贴着岩基外缘
};

class STELLARERAPLANET_API FGeoPegmatiteRule final : public IGeoMineralRule
{
public:
	FGeoPegmatiteParams P;
	virtual uint32 GetStage() const override { return 4; }
	virtual const TCHAR* GetRuleName() const override { return TEXT("Pegmatite"); }
	virtual void Scatter(FGeoScatterContext& Ctx, FGeoScatterOutput& Out) const override;
	virtual uint64 HashParams() const override;
};

/** 岩浆铜镍：辉长岩床 / 岩墙底部、贴着宿主朝向的扁平硫化物矿体。运行于阶段 4b 之后。 */
struct FGeoCopperNickelParams
{
	int32 PerHost = 1;            // 每个辉长岩宿主几个
	float WidthScale = 0.7f;      // 水平半轴 = 该倍数 × 宿主水平半轴
	float ThicknessScale = 0.3f;  // 半厚 = 该倍数 × 宿主竖直半轴
};

class STELLARERAPLANET_API FGeoCopperNickelRule final : public IGeoMineralRule
{
public:
	FGeoCopperNickelParams P;
	virtual uint32 GetStage() const override { return 4; }
	virtual const TCHAR* GetRuleName() const override { return TEXT("CopperNickel"); }
	virtual void Scatter(FGeoScatterContext& Ctx, FGeoScatterOutput& Out) const override;
	virtual uint64 HashParams() const override;
};

/** 层状铬铂：层状镁铁岩每一片薄板内、与宿主同中心同朝向的薄层。运行于阶段 4b 之后。 */
struct FGeoChromiumPGEParams
{
	int32 PerSlab = 1;            // 每片薄板几层
	float ThicknessScale = 0.4f;  // 半厚 = 该倍数 × 宿主竖直半轴
};

class STELLARERAPLANET_API FGeoChromiumPGERule final : public IGeoMineralRule
{
public:
	FGeoChromiumPGEParams P;
	virtual uint32 GetStage() const override { return 4; }
	virtual const TCHAR* GetRuleName() const override { return TEXT("ChromiumPGE"); }
	virtual void Scatter(FGeoScatterContext& Ctx, FGeoScatterOutput& Out) const override;
	virtual uint64 HashParams() const override;
};

/** 金刚石：金伯利岩管内的含矿小团块；只有一部分岩管含矿。运行于阶段 4b 之后。 */
struct FGeoDiamondParams
{
	int32 PerPipe = 1;            // 每根含矿岩管几颗
	float RadiusRatio = 0.0012f;  // 团块半径（占 R）
	float FertileChance = 0.15f;  // 每根岩管含矿的概率
};

class STELLARERAPLANET_API FGeoDiamondRule final : public IGeoMineralRule
{
public:
	FGeoDiamondParams P;
	virtual uint32 GetStage() const override { return 4; }
	virtual const TCHAR* GetRuleName() const override { return TEXT("Diamond"); }
	virtual void Scatter(FGeoScatterContext& Ctx, FGeoScatterOutput& Out) const override;
	virtual uint64 HashParams() const override;
};

/** 碳酸岩稀土：碳酸岩体中心、比宿主小一圈的团块。运行于阶段 4b 之后。 */
struct FGeoRareEarthParams
{
	int32 PerBody = 1;        // 每个碳酸岩体几颗
	float RadiusScale = 0.6f; // 团块半径 = 该倍数 × 宿主半径
};

class STELLARERAPLANET_API FGeoRareEarthRule final : public IGeoMineralRule
{
public:
	FGeoRareEarthParams P;
	virtual uint32 GetStage() const override { return 4; }
	virtual const TCHAR* GetRuleName() const override { return TEXT("RareEarth"); }
	virtual void Scatter(FGeoScatterContext& Ctx, FGeoScatterOutput& Out) const override;
	virtual uint64 HashParams() const override;
};

namespace GeoMineralRules
{
	/** 矿物图层（地下 / 地表矿化），图例来自快照的矿物调色板。不属于某一阶段，Stage = 0。 */
	STELLARERAPLANET_API void DescribeDebugLayers(TArray<FGeoDebugLayer>& Out);
	/** 地表切平面上的局部坐标系：Z = Up，X 按走向角转。 */
	STELLARERAPLANET_API FQuat4f TangentFrame(const FVector3f& Up, float StrikeRadians);
}
