// 材质资产由代码生成：通道布局随 GeoMaterialCodec 演进，手改 uasset 会静默丢失接线。
#pragma once

#include "CoreMinimal.h"

namespace GeoMaterialBuild
{
	/** `/Game/Code/Planet/Materials/Functions/MF_GeoDecode`（包路径）。 */
	const TCHAR* GetDecodeFunctionPath();

	/** `/Game/Code/Planet/Materials/M_GeoPlanet`（包路径）。 */
	const TCHAR* GetPlanetMaterialPath();

#if WITH_EDITOR
	/** 重建两个资产并落盘；可重复执行（先清空表达式集合）。 */
	bool BuildAll(FString& OutError);
#endif
}
