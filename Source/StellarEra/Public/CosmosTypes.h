// CosmosTypes.h —— 天体物理单位定义与共享类型。

#pragma once

#include "CoreMinimal.h"
#include "CosmosTypes.generated.h"

///////////////////////////////////////////////////////////////////////////////
// 单位系统
//
// 默认使用「游戏单位」，数值直观、便于手填：
//   距离 AU   （1 AU = 地球轨道半长轴 ≈ 1.496e11 米）
//   时间 年   （1 年 = 365.25 天）
//   质量 M☉   （1 M☉ = 太阳质量 ≈ 1.989e30 千克）
//
// 在这套单位下，玻尔兹曼常数恰好是 4π² ——
// 也就是「半径 1 AU、周期 1 年」的轨道正好是圆轨道。
// 这让所有真实天体数据的填入变得毫无负担。
///////////////////////////////////////////////////////////////////////////////

namespace CosmosUnits
{
	/** 游戏单位下的引力常数：4π² AU³/(M☉·年²)。 */
	inline constexpr double GravitationalConstant_Game = 39.478417604357434;

	/** SI 单位下的引力常数，单位 cm³/(kg·s²)。 */
	inline constexpr double GravitationalConstant_SI = 6.67430e-5;

	/**
	 * 1 AU = 1.495978707e13 厘米。**这是真实物理值，不是本项目的世界尺度。**
	 * 本项目的世界按「1 AU = 10 公里」标定，见 FNBodySimulator::CentimetersPerAu。
	 * 直接用这个常量会把星球甩出 float32 的精度范围（该处 ULP ≈ 18 公里）。
	 */
	inline constexpr double AuToCentimeter = 1.495978707e13;

	/** 1 年 = 3.15576e7 秒。 */
	inline constexpr double YearToSecond = 3.15576e7;

	/** 1 太阳质量 = 1.98892e30 千克。 */
	inline constexpr double SolarMassToKilogram = 1.98892e30;

	/** 地球质量，单位 M☉。 */
	inline constexpr double EarthMass = 3.0034893e-6;

	/** 木星质量，单位 M☉。 */
	inline constexpr double JupiterMass = 9.545942e-4;

	/** 地球轨道速度，单位 AU/年。恰好等于 2π。 */
	inline constexpr double EarthOrbitalSpeed = 6.283185307179586;
}

///////////////////////////////////////////////////////////////////////////////
// 单位系统选择

UENUM(BlueprintType)
enum class ECosmosUnitSystem : uint8
{
	/** 距离 AU、时间 年、质量 M☉。适合直接填真实天文数据。 */
	Game,
	/** 距离厘米、时间 秒、质量千克。适合真实物理计算。 */
	SI
};

///////////////////////////////////////////////////////////////////////////////
// 积分器类型

UENUM(BlueprintType)
enum class ECosmosIntegrator : uint8
{
	/**
	 * Velocity Verlet（辛积分器）。强烈推荐。
	 * 保持相空间体积，误差有界不累积，轨道可稳定运行数百万年。
	 */
	VelocityVerlet,
	/**
	 * 显式欧拉法。一阶方法，误差同向累积，
	 * 轨道会在可观时间内螺旋坠入中心天体。仅用于对比测试。
	 */
	Euler,
	/** 禁用积分。天体保持初始速度做匀速直线运动。 */
	None
};

///////////////////////////////////////////////////////////////////////////////
// 天体类型

UENUM(BlueprintType)
enum class ECosmosBodyType : uint8
{
	/** 恒星。自身发光，并作为系统的引力锚点。 */
	Star,
	/** 行星。围绕恒星公转。 */
	Planet,
	/** 卫星。围绕行星公转。 */
	Moon,
	/** 纯计算质点。不渲染，只参与引力。 */
	Invisible
};

///////////////////////////////////////////////////////////////////////////////
// 光照模式

UENUM(BlueprintType)
enum class ECosmosLightMode : uint8
{
	/**
	 * 平行光（推荐）。
	 * 每颗恒星为每个受照天体提供一个 DirectionalLight，无距离衰减。
	 * 适合太阳系与多恒星系统，是唯一在 AU 尺度下合理的方案。
	 */
	Directional,
	/**
	 * 点光源。平方反比衰减，仅适合半径小于几 AU 的紧凑系统。
	 */
	Point
};

///////////////////////////////////////////////////////////////////////////////
// 轨道预设

UENUM(BlueprintType)
enum class ECosmosOrbitPreset : uint8
{
	/** 不套用，直接用手填的位置与速度。 */
	Custom,
	/** 圆轨道。给定半长轴，速度自动由开普勒第三定律算出。 */
	Circular,
	/** 椭圆轨道。给定半长轴与偏心率。 */
	Elliptical
};

/**
 * 轨道初始条件。
 *
 * 用「半长轴 + 偏心率」而不是直接填位置速度，
 * 因为这样能保证天体落在正确的轨道上——
 * 直接填位置和速度，一旦两者不匹配就会飞向太阳或者逃逸。
 */
USTRUCT(BlueprintType)
struct FCosmosOrbitInit
{
	GENERATED_BODY()

	/** 轨道预设。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit")
	ECosmosOrbitPreset Preset = ECosmosOrbitPreset::Circular;

	/** 半长轴。AU 对应地球轨道半径，1000 AU 适合外太阳系。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit", meta = (ClampMin = "0.001"))
	double SemiMajorAxis = 1.0;

	/** 偏心率。0 = 正圆，0.2 = 椭圆，0.9 = 细长椭圆。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit", meta = (ClampMin = "0.0", ClampMax = "0.99"))
	double Eccentricity = 0.0;

	/** 轨道倾角，度。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit")
	double InclinationDegrees = 0.0;

	/** 升交点经度，度。决定轨道在黄道面内的旋转方向。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit")
	double AscendingNodeDegrees = 0.0;

	/** 初始相位角，度。0 表示位于轨道最近点（Periapsis）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit")
	double PhaseDegrees = 0.0;

	/**
	 * 直接指定初始速度（游戏单位 AU/年）。
	 * 留零则由轨道参数自动推导。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit")
	FVector OverrideVelocity = FVector::ZeroVector;

	bool bHasVelocityOverride() const { return !OverrideVelocity.IsNearlyZero(); }
};
