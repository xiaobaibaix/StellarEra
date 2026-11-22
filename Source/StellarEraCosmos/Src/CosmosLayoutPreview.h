// 编辑器态映射布局预览：把天体按 FUniverseMapping 摆到物理尺度下的初始位置与尺寸，不跑物理。
#pragma once

#include "CoreMinimal.h"

#if WITH_EDITOR
/** 立即按映射摆一次；bVerbose 时逐天体打日志。CVar StellarEra.Cosmos.EditorPreview 会在编辑器里周期调用它。 */
STELLARERACOSMOS_API void StellarEraCosmosApplyMappedLayout(bool bVerbose);
#endif
