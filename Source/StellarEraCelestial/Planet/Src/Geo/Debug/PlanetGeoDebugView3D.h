// 地质图层的 3D 视图：CPU 光线步进快照，可旋转 / 缩放 / 切开 / 隐藏圈层 / 半透明。查看器、导出与测试共用。
#pragma once
#include "CoreMinimal.h"
#include "PlanetDebugLayer.h"
#include "PlanetSnapshot.h"

namespace PlanetGeoDebugView3D
{
	enum class EClip : uint8
	{
		None,
		Half,          // 切掉 +X 半球（与星球剖切同一约定）
		Quarter,       // 切掉 +X、−Y 四分之一
		FacingCamera,  // 垂直视线切开，ClipOffset 决定切面位置
	};

	struct FView
	{
		// 默认从 +X、−Y 方向看：正对被切掉的四分之一，两个切面都朝向相机。
		float YawDeg = -45.f;
		float PitchDeg = 25.f;
		/** 相机到视线中心的距离，单位：星球半径。 */
		float Distance = 3.2f;
		/** 视线中心相对球心的平移，单位：星球半径。 */
		FVector2f Pan = FVector2f::ZeroVector;
		EClip Clip = EClip::Quarter;
		/** FacingCamera 时切面离球心的距离（半径，−1…1；0 过球心，越大切得越少）。 */
		float ClipOffset = 0.f;
		/** 每穿过一个可见层叠多少不透明度；1 = 只看最外面那层，小于 1 能透看里层。 */
		float Opacity = 1.f;
		/** 分类图层：下标即取值，true = 隐藏这一类（隐藏的层当作透明）。 */
		TArray<bool> Hidden;
		/** 沿星球直径的采样数；分界处再二分细化，所以不用很大。 */
		int32 Steps = 192;
		/** > 0 时切面上叠矿物色，矿体按这个倍数放大显示（只影响显示）。 */
		float MineralScale = 0.f;
	};

	/**
	 * Layer 决定内部颜色；SurfaceLayer 非空时，从太空直接打到地表的那一层改用它上色（外看海陆、切面看圈层）。
	 * OutHitPos / OutHitValues 记录每像素第一个可见点（体素坐标，球心为原点）与 Layer 在该点的值；没打中为 NaN。
	 */
	STELLARERACELESTIAL_API void Render(const FPlanetSnapshot& S, const FGeoDebugLayer& Layer, const FGeoDebugLayer* SurfaceLayer,
		const FView& View, int32 W, int32 H, TArray<FColor>& OutColors,
		TArray<FVector3d>* OutHitPos = nullptr, TArray<float>* OutHitValues = nullptr);
}
