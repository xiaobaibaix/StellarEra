// 把调试图层画成图：展开地图、过球心剖面、PNG、星球叠加用的立方体贴图。查看器、命令、星球共用这一份。
#pragma once
#include "CoreMinimal.h"
#include "PlanetDebugLayer.h"
#include "PlanetSnapshot.h"

class UTextureCube;

namespace PlanetGeoDebugRender
{
	/** 剖面所在平面，都过球心。 */
	enum class ESectionPlane : uint8 { XZ, YZ, XY };

	/** 地表图层取地表点；地下图层取地表下 2 体素（展开地图上看「刚挖开」的那一层）。 */
	STELLARERAPLANET_API FVector3d SurfacePoint(const FPlanetSnapshot& S, const FVector3f& Dir, EGeoDebugDomain Domain);

	/** 等经纬展开：x 从经度 −180° 到 +180°，y 从北极到南极。 */
	STELLARERAPLANET_API FVector3f MapPixelToDir(int32 X, int32 Y, int32 W, int32 H);
	STELLARERAPLANET_API void RenderMap(const FPlanetSnapshot& S, const FGeoDebugLayer& Layer, int32 W, int32 H,
		TArray<FColor>& OutColors, TArray<float>* OutValues = nullptr);

	/** 剖面半宽（体素）：比最高地表多 2%，留一圈太空当背景。 */
	STELLARERAPLANET_API double SectionHalfExtent(const FPlanetSnapshot& S);
	STELLARERAPLANET_API FVector3d SectionPixelToPos(const FPlanetSnapshot& S, int32 X, int32 Y, int32 Size, ESectionPlane Plane);
	/** 星球外是太空色、值为无数据；地表图层在剖面上按方向取地表值（整根柱子同色）。 */
	STELLARERAPLANET_API void RenderSection(const FPlanetSnapshot& S, const FGeoDebugLayer& Layer, int32 Size, ESectionPlane Plane,
		TArray<FColor>& OutColors, TArray<float>* OutValues = nullptr);

	STELLARERAPLANET_API bool SavePng(const FString& Path, int32 W, int32 H, const TArray<FColor>& Colors);

	/** 立方体贴图，Outer 是瞬态包：挂到体素材质上的贴图 Outer 不能是行星（会让切图泄漏崩溃）。 */
	STELLARERAPLANET_API UTextureCube* BakeCube(const FPlanetSnapshot& S, const FGeoDebugLayer& Layer, int32 Resolution);

	/** 每个图层写 Stage<阶段>_<Id>_map.png 与 _section.png，返回写成功的文件数。 */
	STELLARERAPLANET_API int32 ExportAll(const FPlanetSnapshot& S, const TArray<FGeoDebugLayer>& Layers,
		const FString& Directory, int32 MapWidth);
}
