// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetSectionImage.h"

void FPlanetSectionImage::Render(
	const FPlanetSectionImageParams& Params,
	TFunctionRef<float(const FVector& UnitDir)> SurfaceRadiusCmFn,
	TFunctionRef<FPlanetGeologySample(const FVector& UnitDir, double DepthVoxels)> SampleFn,
	TArray<FColor>& OutPixels)
{
	const int32 Res = FMath::Max(Params.Resolution, 1);
	OutPixels.Reset();
	OutPixels.SetNumUninitialized(Res * Res);

	const float SectionRadiusCm = FMath::Max(Params.SectionRadiusCm, KINDA_SMALL_NUMBER);
	const float VoxelSize = FMath::Max(Params.VoxelSize, KINDA_SMALL_NUMBER);

	// 一个像素覆盖多长（厘米）：整条边长 2R 摊到 Res 个像素。
	const float PixelSizeCm = 2.f * SectionRadiusCm / float(Res);
	constexpr float FeatherPixels = 1.5f;

	// 深度变暗：DepthShade = 1 − 0.4 × saturate(DepthVoxels / BaseRadiusVoxels)。
	const float DepthShadeScale =
		Params.BaseRadiusVoxels > KINDA_SMALL_NUMBER ? 1.f / Params.BaseRadiusVoxels : 0.f;
	const float PresenceScale = 1.f / FMath::Max(Params.Appearance.FadeRange, 1e-4f);
	const float PurityScale = 1.f / FMath::Max(Params.Appearance.PurityForFullColor, 1e-4f);

	const FVector AxisU = Params.AxisU.GetSafeNormal();
	const FVector AxisV = Params.AxisV.GetSafeNormal();

	for (int32 Y = 0; Y < Res; ++Y)
	{
		// 图像第 0 行对应 v = −1（spec §3.1 的像素中心公式）。
		const float VCoord = (float(Y) + 0.5f) / float(Res) * 2.f - 1.f;

		for (int32 X = 0; X < Res; ++X)
		{
			const float UCoord = (float(X) + 0.5f) / float(Res) * 2.f - 1.f;

			const FVector Offset = AxisU * UCoord + AxisV * VCoord;
			const float OffsetLength = float(Offset.Size());
			const FVector Dir = OffsetLength > KINDA_SMALL_NUMBER
				? Offset / OffsetLength
				: AxisU;

			const float PlaneRadiusCm = OffsetLength * SectionRadiusCm;
			const float SurfaceRadiusCm = SurfaceRadiusCmFn(Dir);
			const float ClipRadiusCm = FMath::Min(SectionRadiusCm, SurfaceRadiusCm);

			// 越过 min(剖面半径, 地表半径) 的部分向外线性淡出 1.5px。
			const float OverPx = (PlaneRadiusCm - ClipRadiusCm) / PixelSizeCm;
			const float Alpha = FMath::Clamp(1.f - OverPx / FeatherPixels, 0.f, 1.f);

			FColor& Pixel = OutPixels[X + Y * Res];
			if (Alpha <= 0.f)
			{
				Pixel = FColor(0, 0, 0, 0);
				continue;
			}

			const double DepthVoxels = double(SurfaceRadiusCm - PlaneRadiusCm) / double(VoxelSize);
			const FPlanetGeologySample Sample = SampleFn(Dir, DepthVoxels);

			const float Presence = FMath::Clamp(
				(Sample.DominantNorm - Params.Appearance.ColorThreshold) * PresenceScale, 0.f, 1.f);
			const float PurityMix = FMath::Clamp(Sample.Purity * PurityScale, 0.f, 1.f);
			const float DepthShade = 1.f - 0.4f * FMath::Clamp(
				float(DepthVoxels) * DepthShadeScale, 0.f, 1.f);

			const FLinearColor Base = Params.SurfaceColor * 0.25f * DepthShade;
			// 按 HSV 混合（测试钉住的口径）：暗底色与矿物色明度差大，直插会发灰。
			const FLinearColor Color =
				FLinearColor::LerpUsingHSV(Base, Sample.MineralColor, Presence * PurityMix);

			Pixel = Color.ToFColor(true);
			Pixel.A = uint8(FMath::RoundToInt(Alpha * 255.f));
		}
	}
}
