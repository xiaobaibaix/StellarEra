// 星球材质求值器 —— 实现。

#include "VoxelPlanetMaterializer.h"

void FVoxelPlanetMaterializer::Init(const FVoxelPlanetSeaSettings& InSea,
	const FVoxelPlanetLandPalette& InLand,
	const FVoxelPlanetShellSettings& InShell,
	const FVoxelPlanetContinentLayer& InContinent)
{
	Sea = InSea;
	Land = InLand;
	Shell = InShell;
	Continent = InContinent;
}

double FVoxelPlanetMaterializer::GetMaxLandHeight() const
{
	// 保守取大陆层的最大抬升。色带只需要相对关系，不需要绝对精确。
	return FMath::Max(Continent.Height, 1.0);
}

FLinearColor FVoxelPlanetMaterializer::GetSurfaceColor(double SurfaceRadius, double BaseRadius) const
{
	//-----------------------------------------------------------------------
	// 水域：岸滩 → 深水
	//-----------------------------------------------------------------------
	if (Sea.bEnabled && Sea.Depth > 0.0)
	{
		const double SeaRadius = BaseRadius - Sea.Depth;
		if (SurfaceRadius < SeaRadius)
		{
			const double Depth = FMath::Clamp((SeaRadius - SurfaceRadius) / Sea.Depth, 0.0, 1.0);
			// Pow(0.6) 让近岸浅水颜色停留更久，视觉上更自然
			return FLinearColor::LerpUsingHSV(Sea.ShoreColor, Sea.DeepColor, FMath::Pow(float(Depth), 0.6f));
		}
	}

	//-----------------------------------------------------------------------
	// 陆地：低地 → 高地
	//-----------------------------------------------------------------------
	const double LandHeight = FMath::Max(SurfaceRadius - BaseRadius, 0.0);
	const double LandMax = GetMaxLandHeight();
	const float LandT = float(FMath::Clamp(LandHeight / LandMax, 0.0, 1.0));

	// Pow(0.7) 让低地占据更宽的过渡带，高地更集中
	FLinearColor Color = FLinearColor::LerpUsingHSV(Land.LowlandColor, Land.HighlandColor, FMath::Pow(LandT, 0.7f));

	//-----------------------------------------------------------------------
	// 雪线以上覆写为雪顶
	//-----------------------------------------------------------------------
	if (Land.SnowLine > 0.0 && SurfaceRadius > BaseRadius + Land.SnowLine)
	{
		const float SnowT = float(FMath::Clamp(
			(SurfaceRadius - (BaseRadius + Land.SnowLine)) / Land.SnowLine, 0.0, 1.0));
		Color = FLinearColor::LerpUsingHSV(Color, Land.SnowColor, SnowT);
	}

	return Color;
}
