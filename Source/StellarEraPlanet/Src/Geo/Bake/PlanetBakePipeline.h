// 烘焙流水线：按阶段号排序、校验读写、顺序执行、冻结成快照。全程不碰 UObject，可在后台线程调用。
#pragma once
#include "CoreMinimal.h"
#include "PlanetBakeTypes.h"
#include "PlanetSnapshot.h"
#include "PlanetDebugLayer.h"

class IPlanetStage;

class STELLARERAPLANET_API FPlanetBakePipeline
{
public:
	FPlanetBakePipeline();
	~FPlanetBakePipeline();
	FPlanetBakePipeline(FPlanetBakePipeline&&);
	FPlanetBakePipeline& operator=(FPlanetBakePipeline&&);

	void AddStage(TUniquePtr<IPlanetStage> Stage);
	bool Validate(FPlanetBakeReport& Report, uint32 DisabledStages = 0) const;
	/** 校验失败或任一阶段写了 Error 时返回空。 */
	TSharedPtr<const FPlanetSnapshot> Bake(const FPlanetRecipeConfig& Config, FPlanetBakeReport& Report) const;

	/** 项目默认的阶段集合（1 圈层 / 2 板块与大陆 / 3 宏观地形）。 */
	static FPlanetBakePipeline MakeDefault();

	/** 按阶段号排序后的执行顺序；测试用它确认默认管线包含哪些阶段。 */
	TArray<const IPlanetStage*> Sorted() const;

	/** 按阶段顺序收集全部调试图层。 */
	TArray<FGeoDebugLayer> DescribeDebugLayers() const;
	/** 默认管线的图层表（首次调用时构建，之后只读，可跨线程用）。 */
	static const TArray<FGeoDebugLayer>& DefaultDebugLayers();
	static const FGeoDebugLayer* FindDefaultDebugLayer(FName Id);

private:
	TArray<TUniquePtr<IPlanetStage>> Stages;
};
