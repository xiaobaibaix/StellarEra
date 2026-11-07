// 烘焙结果的只读快照：每格的水位 / 下切深度 / 河流强度，外加湖泊与河流特征。

#pragma once

#include "CoreMinimal.h"
#include "PlanetHydrologyTypes.h"

/**
 * 水文场。单位一律是体素半径：生成器在体素空间直接比较，水面几何乘 VoxelSize
 * 即本地空间厘米，运行时不需要再乘 Radius。三个栅格同布局、长度为
 * CellCount(Resolution)。构造后不再改变，可安全在后台体素线程共享。
 */
class STELLARERAPLANET_API FPlanetHydrologyField
{
public:
	/** 一个方向的四邻域插值结果。 */
	struct FSample
	{
		/** 水面半径（体素）。陆地上等于地形高度（= 没有水）。 */
		float WaterLevelRadius = 0.f;

		/** 河道下切深度（体素）。非河道为 0。 */
		float CarveDepth = 0.f;

		/** 河流强度 0..1（由汇流量归一化）。非河道为 0。 */
		float RiverStrength = 0.f;
	};

	/** 唯一构造点：烘焙层填好三个栅格与特征列表后调用。 */
	static TSharedRef<FPlanetHydrologyField> Create(
		int32 InResolution,
		float InSeaLevelRadius,
		float InMaxCarveDepth,
		TArray<float>&& InWaterLevel,
		TArray<float>&& InCarveDepth,
		TArray<float>&& InRiverStrength,
		TArray<FPlanetLakeBasin>&& InLakes,
		TArray<FPlanetRiverPolyline>&& InRivers);

	bool IsValid() const;

	/** cubemap 双线性采样。场无效时返回全零（调用方拿到的水位 0 意味着「没有水」）。 */
	FSample Sample(const FVector& UnitDir) const;

	/** 海面半径（体素）= Radius + Amplitude × SeaLevel。 */
	float GetSeaLevelRadius() const { return SeaLevelRadius; }

	/** 给区间查询用的下切包络：地表半径要再减去这么多。 */
	float GetMaxCarveDepth() const { return MaxCarveDepth; }

	int32 GetResolution() const { return Resolution; }

	const TArray<FPlanetLakeBasin>& GetLakes() const { return Lakes; }
	const TArray<FPlanetRiverPolyline>& GetRivers() const { return Rivers; }

	/** 给水面几何构建器用：它要按格遍历海面，而不是按方向采样。 */
	const TArray<float>& GetWaterLevelGrid() const { return WaterLevel; }

private:
	int32 Resolution = 0;
	float SeaLevelRadius = 0.f;
	float MaxCarveDepth = 0.f;
	TArray<float> WaterLevel;
	TArray<float> CarveDepth;
	TArray<float> RiverStrength;
	TArray<FPlanetLakeBasin> Lakes;
	TArray<FPlanetRiverPolyline> Rivers;
};
