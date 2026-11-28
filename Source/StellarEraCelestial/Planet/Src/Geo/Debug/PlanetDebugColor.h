// 按高程分色：验收大陆形状用，不是最终美术。海陆分界、陆架、高地一眼可辨。
#pragma once
#include "CoreMinimal.h"

namespace PlanetDebugColor
{
	/** E：高程（体素，相对 R0）；ShelfBottom：陆架底部高程（负数）。 */
	inline FColor FromElevation(float E, float OceanDepth, float LandHeight, float ShelfBottom)
	{
		const auto Mix = [](const FLinearColor& A, const FLinearColor& B, float T)
		{
			return FMath::Lerp(A, B, FMath::Clamp(T, 0.f, 1.f)).ToFColor(false);
		};
		static const FLinearColor Abyss(0.02f, 0.05f, 0.25f), Deep(0.05f, 0.15f, 0.45f), Shallow(0.25f, 0.6f, 0.85f);
		static const FLinearColor Sand(0.85f, 0.8f, 0.55f), Plain(0.25f, 0.55f, 0.2f), High(0.5f, 0.35f, 0.2f), Peak(0.9f, 0.9f, 0.9f);
		if (E < ShelfBottom)
		{
			return Mix(Deep, Abyss, (ShelfBottom - E) / FMath::Max(OceanDepth + ShelfBottom, 1.f));
		}
		if (E <= 0.f)
		{
			return Mix(Shallow, Deep, E / FMath::Min(ShelfBottom, -1.f));
		}
		const float T = E / FMath::Max(LandHeight, 1.f);
		if (T < 0.04f) { return Mix(Sand, Plain, T / 0.04f); }
		if (T < 0.6f) { return Mix(Plain, High, (T - 0.04f) / 0.56f); }
		return Mix(High, Peak, (T - 0.6f) / 0.4f);
	}
}
