// 水文缓存测试用的地形替身：只实现 GetWaterConfig / GetRawOffset，不产生真实地形。
//
// 为什么放在头文件而不是直接写进 *Tests.cpp：UHT 只处理 .h，UCLASS 必须有
// GENERATED_BODY 与 .generated.h；.cpp 里定义的 UCLASS 不会被反射生成，也就
// NewObject 不出来。这个头只被 PlanetHydrologyPlumbingTests.cpp include，
// 不进任何生产路径。

#pragma once

#include "CoreMinimal.h"
#include "VoxelPlanetTerrainModule.h"
#include "PlanetHydrologyTestTerrain.generated.h"

/**
 * 一个「有水」的最小地形模块：GetWaterConfig 恒返回 true，分辨率由测试设小。
 *
 * 它存在的唯一目的是让 RefreshHydrology 走进真正的烘焙分支 —— 用纯球模块或
 * 空指针时那条路在 GetWaterConfig 就早退了，缓存键根本没被比较过。
 */
UCLASS()
class UPlanetHydrologyTestTerrain : public UVoxelPlanetTerrainModule
{
	GENERATED_BODY()

public:
	//~ Begin UVoxelPlanetTerrainModule Interface
	virtual float GetRawOffset(const FVector& UnitDir) const override
	{
		// 确定、无副作用的纯函数：烘焙会在 ParallelFor 里并发调用它。
		return float(FMath::Sin(UnitDir.X * 3.0) * 0.2);
	}

	virtual bool GetWaterConfig(FPlanetWaterConfig& OutConfig) const override
	{
		OutConfig = FPlanetWaterConfig();
		OutConfig.Params.SeaLevel = SeaLevel;
		OutConfig.Params.Amplitude = Amplitude;
		OutConfig.Params.Resolution = Resolution;
		OutConfig.Params.MaxCarveDepth = MaxCarveDepth;
		OutConfig.Params.MinRiverFlow = MinRiverFlow;
		return bEnableWater;
	}
	//~ End UVoxelPlanetTerrainModule Interface

	/** 每面格数；测试里取小值，让烘焙维持在毫秒级。 */
	UPROPERTY()
	int32 Resolution = 16;

	UPROPERTY()
	bool bEnableWater = true;

	/** 把海平面抬到 0 以上，地平线以下才算海；测试只关心缓存，不关心地貌。 */
	UPROPERTY()
	float SeaLevel = -0.2f;

	UPROPERTY()
	float Amplitude = 8.f;

	UPROPERTY()
	float MaxCarveDepth = 4.f;

	UPROPERTY()
	float MinRiverFlow = 24.f;
};
