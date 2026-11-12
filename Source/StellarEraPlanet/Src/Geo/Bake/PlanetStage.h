// 只在烘焙期存在。无状态：同一个阶段对象可以连续烘焙多颗星，不许在成员里攒结果。
#pragma once
#include "CoreMinimal.h"
#include "PlanetBakeTypes.h"

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
};
