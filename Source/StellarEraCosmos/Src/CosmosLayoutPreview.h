// 编辑器态映射预览：以当前编辑好的天体布局为初始数据，按编辑器相机做距离压缩。
#pragma once

#include "CoreMinimal.h"

#if WITH_EDITOR
/** 每帧调用：bEnabled=false 时恢复开启前的编辑状态；true 时按相机位置压缩各天体。 */
STELLARERACOSMOS_API void StellarEraCosmosUpdateEditorPreview(bool bEnabled);
#endif
