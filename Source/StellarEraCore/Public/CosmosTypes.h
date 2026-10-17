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
	 * 本项目的世界按「1 AU = 10 公里」标定，见 UCosmosSimulationSubsystem::CentimetersPerAu
	 * （配置项，默认 1e6；求解器侧对应公开的 FNBodySimulator::GetCentimetersPerAu()）。
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
