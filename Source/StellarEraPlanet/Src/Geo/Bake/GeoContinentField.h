// 阶段 2 大陆场的纯函数：球谐框架、warp 细节、立体角加权 CDF、面积–高度曲线。只在烘焙期用。
#pragma once
#include "CoreMinimal.h"
#include "GeoField2D.h"
#include "PlanetRecipeConfig.h"

namespace GeoContinentField
{
	/** 实球谐系数，下标 l*l + l + m；l=0 恒为 0（平移量会被分位数阈值吸收）。 */
	struct FFrame
	{
		int32 Degree = 0;
		TArray<double> Coeffs;
	};

	STELLARERAPLANET_API FFrame BuildFrame(const FPlanetRecipeConfig& Config);
	STELLARERAPLANET_API float EvalFrame(const FFrame& Frame, const FVector3f& Dir);

	STELLARERAPLANET_API uint64 DetailSeed(const FPlanetRecipeConfig& Config);
	STELLARERAPLANET_API float EvalDetail(uint64 Seed, const FPlanetContinentParams& P, const FVector3f& Dir);

	/** cubemap texel 的相对立体角 (1+u²+v²)^-1.5：不加权时面角 texel 被多算约 5 倍面积。 */
	STELLARERAPLANET_API double TexelWeight(const FGeoField2D& Field, int32 X, int32 Y);

	struct STELLARERAPLANET_API FWeightedCdf
	{
		float Min = 0.f, Max = 0.f;
		TArray<double> Cum;   // 归一化累计权重，末项为 1
		/** 场值 → 加权面积排名 ∈ [0,1]。 */
		float Rank(float V) const;
	};
	STELLARERAPLANET_API FWeightedCdf BuildCdf(const FGeoField2D& Field);

	/** 排名 → 高程（体素，相对 R0）；Rank = 1−LandFrac 处恰为 0，全程单调不减。 */
	STELLARERAPLANET_API float Elevation(float Rank, const FPlanetContinentParams& P, double Radius);

	/** à-trous 十字模糊，每遍偏移 Offsets[i] 个 texel；每个 texel 保持原来的海陆符号，海岸线不动。 */
	STELLARERAPLANET_API void SmoothKeepSign(FGeoField2D& Field, TConstArrayView<int32> Offsets, bool bSingleThread);
}
