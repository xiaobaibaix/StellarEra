// 3A / 3B / 7 的成矿规律：地表毯层、次生富集、铁帽。都是「贴着地表十几格的薄层」，
// 所以产出覆层（Overlay）而不是深部原语。
#pragma once
#include "CoreMinimal.h"
#include "GeoMineralRule.h"

/** 毯层挂在哪张场上。3A 的沉积矿看盖层岩性，7 的风化矿看土壤类型。 */
enum class EGeoBlanketHost : uint8
{
	StrataLithology,   // 盖层岩性 == HostValue（16 砂岩 / 17 页岩 / 18 石灰岩）
	Soil,              // 土壤类型 == HostValue（1 红土 / 2 高岭 / 3 温带 / 4 干旱）
	Metamorphic,       // 变质级 >= MinGrade
	Fault,             // 断层带强度 >= MinGrade
	Marble,            // 盖层是石灰岩且变质级超过 Threshold
};

struct FGeoBlanketParams
{
	EGeoBlanketHost Host = EGeoBlanketHost::Soil;
	int32 HostValue = 1;
	float MinGrade = 0.f;
	float Threshold = 0.55f;
	float Chance = 0.6f;
	float DepthVoxels = 3.f;
	uint64 HashParams() const;
};

/** 毯层：达标处按概率取一片薄皮。铝土、红土镍、高岭土、蒸发岩、煤、铀都走这条。 */
class FGeoBlanketRule final : public IGeoMineralRule
{
public:
	FGeoBlanketParams P;
	virtual uint32 GetStage() const override { return Stage; }
	virtual const TCHAR* GetRuleName() const override { return TEXT("地表毯层"); }
	virtual void Scatter(FGeoScatterContext& Ctx, FGeoScatterOutput& Out) const override;
	virtual uint64 HashParams() const override;
	uint32 Stage = 8;
};

/** 次生富集：宿主矿体上方、潜水面附近的一条水平富集毯，水平范围不出宿主。 */
class FGeoSupergeneRule final : public IGeoMineralRule
{
public:
	FName HostMineral;
	float DepthBelowSurface = 20.f;   // 富集带顶面离地表多深（体素）
	float ThicknessVoxels = 12.f;
	float ProfileDepthVoxels = 8.f;   // 覆层显示厚度
	float HostMargin = 1.1f;          // 水平范围相对宿主的放大
	uint64 HashParams() const override;
	virtual uint32 GetStage() const override { return Stage; }
	virtual const TCHAR* GetRuleName() const override { return TEXT("次生富集"); }
	virtual void Scatter(FGeoScatterContext& Ctx, FGeoScatterOutput& Out) const override;
	uint32 Stage = 8;
};

/** 铁帽：硫化物矿体露头处的地表铁染。没剥蚀到矿体就没有——线索必须对应得上矿体。 */
class FGeoGossanRule final : public IGeoMineralRule
{
public:
	FName HostMineral;
	float MaxDepthVoxels = 6.f;   // 宿主顶面离地表不超过它才算露头
	float CapDepthVoxels = 2.f;
	float RadiusScale = 1.15f;
	uint64 HashParams() const override;
	virtual uint32 GetStage() const override { return Stage; }
	virtual const TCHAR* GetRuleName() const override { return TEXT("铁帽"); }
	virtual void Scatter(FGeoScatterContext& Ctx, FGeoScatterOutput& Out) const override;
	uint32 Stage = 8;
};

/** 矽卡岩：酸性/中酸性侵入体与石灰岩接触带上的一圈交代矿。 */
class FGeoSkarnRule final : public IGeoMineralRule
{
public:
	float RadiusScale = 1.15f;      // 相对侵入体长轴的接触带半径
	float ThicknessVoxels = 24.f;   // 接触带厚度
	uint64 HashParams() const override;
	virtual uint32 GetStage() const override { return Stage; }
	virtual const TCHAR* GetRuleName() const override { return TEXT("矽卡岩"); }
	virtual void Scatter(FGeoScatterContext& Ctx, FGeoScatterOutput& Out) const override;
	uint32 Stage = 4;
};

namespace GeoSurfaceDeposit
{
	/** 在 Ctx.Config.Minerals 里按中文名找编号；找不到返回 0（规则什么都不撒）。 */
	uint8 FindMineralByName(const FGeoScatterContext& Ctx, const FName& Name);
}
