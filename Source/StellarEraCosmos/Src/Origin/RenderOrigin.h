// 渲染原点：世界坐标 = 求解器位置换算 − 锚点世界位置，让天体始终待在世界原点附近。

#pragma once

#include "CoreMinimal.h"
#include "CosmosTypes.h"

class FNBodySimulator;

struct STELLARERACOSMOS_API FRenderOrigin
{
	/** 锚点天体在求解器里的下标；INDEX_NONE = 不重定位。 */
	int32 AnchorIndex = INDEX_NONE;

	/** 锚点的世界位置（厘米）。原点每帧跟随它，锚点因此看起来不动。 */
	FVector AnchorWorld = FVector::ZeroVector;

	/** 锚点当前的求解器位置（AU）。每帧由 Apply 写入。 */
	FVector3d OriginGameUnits = FVector3d::ZeroVector;

	/** 注册后选锚点：有地表的天体里质量最大的；纯质点系统保持 INDEX_NONE。 */
	void SelectAnchor(FNBodySimulator& Simulator);

	/** 把原点写进求解器；AnchorPositionOverride 用于插值后的锚点位置。 */
	void Apply(FNBodySimulator& Simulator, const FVector3d* AnchorPositionOverride = nullptr) const;

	/** 设置原点对：求解器单位位置 + 对应世界位置。 */
	void SetOrigin(const FVector3d& InGameUnits, const FVector& InWorld);

	/** 求解器位置 → 世界厘米。bGameUnits=false 时不做 AU/厘米标定（SI 单位 1:1）。 */
	FVector ToWorldLocation(const FVector3d& GameUnitPosition, double CmPerAu, bool bGameUnits) const;

	/** 世界厘米 → 求解器位置，与 ToWorldLocation 互逆。 */
	FVector3d ToGameUnitPosition(const FVector& WorldLocation, double CmPerAu, bool bGameUnits) const;
};
