// 烘焙流水线：按阶段号排序、校验读写、顺序执行、冻结成快照。全程不碰 UObject，可在后台线程调用。
#pragma once
#include "CoreMinimal.h"
#include "PlanetBakeTypes.h"
#include "PlanetSnapshot.h"

class IPlanetStage;

class STELLARERAPLANET_API FPlanetBakePipeline
{
public:
	FPlanetBakePipeline();
	~FPlanetBakePipeline();
	FPlanetBakePipeline(FPlanetBakePipeline&&);
	FPlanetBakePipeline& operator=(FPlanetBakePipeline&&);

	void AddStage(TUniquePtr<IPlanetStage> Stage);
	bool Validate(FPlanetBakeReport& Report) const;
	/** 校验失败或任一阶段写了 Error 时返回空。 */
	TSharedPtr<const FPlanetSnapshot> Bake(const FPlanetRecipeConfig& Config, FPlanetBakeReport& Report) const;

	/** 项目默认的阶段集合（M1 只有阶段 1）。 */
	static FPlanetBakePipeline MakeDefault();

private:
	TArray<TUniquePtr<IPlanetStage>> Stages;
	TArray<const IPlanetStage*> Sorted() const;
};
