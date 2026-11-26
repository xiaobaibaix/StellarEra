// CosmosTypes.h —— 天体物理单位定义与共享类型。

#pragma once

#include "CoreMinimal.h"

///////////////////////////////////////////////////////////////////////////////
// 单位系统
//
// 游戏单位：距离 AU、时间 年、质量 M☉。此单位下引力常数恰好是 4π²，
// 「半径 1 AU、周期 1 年」正好是圆轨道，方便直接填真实天体数据。
///////////////////////////////////////////////////////////////////////////////

namespace CosmosConstants
{
	/** 游戏单位下的引力常数：4π² AU³/(M☉·年²)。 */
	inline constexpr double GravitationalConstant = 39.478417604357434;

	/** SI 单位下的引力常数，单位 cm³/(kg·s²)。星图算表面重力用。 */
	inline constexpr double GravitationalConstant_SI = 6.67430e-5;

	/**
	 * 1 AU = 1.495978707e13 厘米。**真实物理值，不是本项目的世界尺度** —— 世界按
	 * 「1 AU = 10 公里」标定（见 CentimetersPerAu）。直接拿它当世界尺度会超出 float32
	 * 精度（该处 ULP ≈ 18 公里）。
	 */
	inline constexpr double AuToCentimeter = 1.495978707e13;

	/** 1 年 = 3.15576e7 秒。 */
	inline constexpr double YearToSecond = 3.15576e7;

	/** 1 太阳质量 = 1.98892e30 千克。 */
	inline constexpr double SolarMassToKilogram = 1.98892e30;

	/** 1 千米 = 1e5 厘米。HUD 与飞船模块都用它，避免散落魔数。 */
	inline constexpr double CentimetersPerKilometer = 1.0e5;

	/** 1 AU = 1.495978707e8 公里。 */
	inline constexpr double AuToKilometer = AuToCentimeter / CentimetersPerKilometer;
}

namespace CosmosMass
{
	/** 地球质量，单位 M☉。 */
	inline constexpr double EarthMass = 3.0034893e-6;

	/** 木星质量，单位 M☉。 */
	inline constexpr double JupiterMass = 9.545942e-4;

	/** 1 M☉ = 332946.0837433315 M⊕。 */
	inline constexpr double SolarMassInEarthMasses = 1.0 / EarthMass;

	/** 质量：地球质量 → 太阳质量（求解器单位）。 */
	inline double EarthMassToSolarMass(double InMassEarth)
	{
		return InMassEarth * EarthMass;
	}

	/** 质量：太阳质量 → 地球质量（设计层单位）。 */
	inline double SolarMassToEarthMass(double InSolarMass)
	{
		return InSolarMass * SolarMassInEarthMasses;
	}
}

namespace CosmosSpeed
{
	/** 地球轨道速度，单位 AU/年。恰好等于 2π。 */
	inline constexpr double EarthOrbitalSpeed = 6.283185307179586;

	/** 1 AU/年 = 4.740470463533348 km/s（物理速度）。 */
	inline constexpr double KilometerPerSecondPerAuPerYear =
		CosmosConstants::AuToKilometer / CosmosConstants::YearToSecond;

	/** 1 km/s = 0.21094952656969873 AU/年。 */
	inline constexpr double AuPerYearPerKilometerPerSecond =
		1.0 / KilometerPerSecondPerAuPerYear;

	/** 速度：AU/年 → km/s（物理速度）。 */
	inline FVector3d AuPerYearToKilometerPerSecond(const FVector3d& Velocity)
	{
		return Velocity * KilometerPerSecondPerAuPerYear;
	}

	/** 速度：km/s → AU/年（求解器单位）。 */
	inline FVector3d KilometerPerSecondToAuPerYear(const FVector3d& Velocity)
	{
		return Velocity * AuPerYearPerKilometerPerSecond;
	}
}

namespace CosmosWorld
{
	// ── 求解器单位 ↔ 世界厘米/秒（含游戏时钟）──

	/** 速度：求解器 AU/年 → 世界厘米/秒。标定无效时返回 0。 */
	inline double GameVelocityToWorldCmPerSecond(double Velocity, double CmPerAu, double TimeScale)
	{
		const double Scale = CmPerAu * TimeScale;
		return Scale > 0.0 ? Velocity * Scale : 0.0;
	}

	/** 速度：世界厘米/秒 → 求解器 AU/年。标定无效时返回 0。 */
	inline double WorldCmPerSecondToGameVelocity(double Velocity, double CmPerAu, double TimeScale)
	{
		const double Scale = CmPerAu * TimeScale;
		return Scale > 0.0 ? Velocity / Scale : 0.0;
	}

	/** 加速度：求解器 AU/年² → 世界厘米/秒²。标定无效时返回 0。 */
	inline double GameAccelerationToWorldCmPerS2(double Acceleration, double CmPerAu, double TimeScale)
	{
		const double Scale = CmPerAu * TimeScale * TimeScale;
		return Scale > 0.0 ? Acceleration * Scale : 0.0;
	}

	/** 加速度：世界厘米/秒² → 求解器 AU/年²。标定无效时返回 0。 */
	inline double WorldCmPerS2ToGameAcceleration(double Acceleration, double CmPerAu, double TimeScale)
	{
		const double Scale = CmPerAu * TimeScale * TimeScale;
		return Scale > 0.0 ? Acceleration / Scale : 0.0;
	}

	// ── 向量重载：调用点传整条速度/加速度，不再自己乘标定值 ──

	inline FVector3d GameVelocityToWorldCmPerSecond(
		const FVector3d& Velocity, double CmPerAu, double TimeScale)
	{
		const double Scale = CmPerAu * TimeScale;
		return Scale > 0.0 ? Velocity * Scale : FVector3d::ZeroVector;
	}

	inline FVector3d WorldCmPerSecondToGameVelocity(
		const FVector3d& Velocity, double CmPerAu, double TimeScale)
	{
		const double Scale = CmPerAu * TimeScale;
		return Scale > 0.0 ? Velocity / Scale : FVector3d::ZeroVector;
	}

	inline FVector3d GameAccelerationToWorldCmPerS2(
		const FVector3d& Acceleration, double CmPerAu, double TimeScale)
	{
		const double Scale = CmPerAu * TimeScale * TimeScale;
		return Scale > 0.0 ? Acceleration * Scale : FVector3d::ZeroVector;
	}

	inline FVector3d WorldCmPerS2ToGameAcceleration(
		const FVector3d& Acceleration, double CmPerAu, double TimeScale)
	{
		const double Scale = CmPerAu * TimeScale * TimeScale;
		return Scale > 0.0 ? Acceleration / Scale : FVector3d::ZeroVector;
	}
}

//////////////////////////////////////////////////////////////////////////