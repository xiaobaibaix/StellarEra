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

namespace GeoMineralRules
{
	/** 矿物图层（地下 / 地表矿化），图例来自快照的矿物调色板。不属于某一阶段，Stage = 0。 */
	STELLARERAPLANET_API void DescribeDebugLayers(TArray<FGeoDebugLayer>& Out);
	/** 地表切平面上的局部坐标系：Z = Up，X 按走向角转。 */
	STELLARERAPLANET_API FQuat4f TangentFrame(const FVector3f& Up, float StrikeRadians);
}
