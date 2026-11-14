// 只在烘焙期存在。无状态：同一个阶段对象可以连续烘焙多颗星，不许在成员里攒结果。
#pragma once
#include "CoreMinimal.h"
#include "PlanetBakeTypes.h"
#include "PlanetDebugLayer.h"

class IPlanetStage
{
public:
	virtual ~IPlanetStage() = default;
	virtual FName GetName() const = 0;
	/** 执行顺序；也是派生随机流的阶段号，改它会改变整颗星。 */
	virtual uint32 GetStageIndex() const = 0;
	virtual EPlanetField GetReads() const = 0;
	virtual EPlanetField GetWrites() const = 0;
	virtual void Bake(FPlanetBakeContext& Ctx) const = 0;
	/** 声明本阶段的调试图层：查看器、星球叠加、剖切、导出都读这张表。新阶段只需覆写它。 */
	virtual void DescribeDebugLayers(TArray<FGeoDebugLayer>& Out) const {}
	/** bKeepDebugFields 时把要看的中间场交给快照；全部阶段跑完后、快照冻结前调用。 */
	virtual void ExportDebugFields(const FPlanetBakeFields& Fields, TMap<FName, FGeoField2D>& Out) const {}
};
