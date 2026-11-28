#include "PlanetSkyModel.h"

namespace
{
	// 与 PlanetClouds.ush 的 MIE_G 一致。
	constexpr float MieG = 0.8f;
	constexpr float Pi = 3.14159265358979323846f;

	// 太阳仰角淡出窗口（正弦值），-15° .. +1°，抄自 .ush 的 smoothstep(-0.26, 0.02, ...)。
	constexpr float SunVisFadeStart = -0.26f;
	constexpr float SunVisFadeEnd = 0.02f;

	float RayleighPhase(float CosTheta)
	{
		return 3.0f / (16.0f * Pi) * (1.0f + CosTheta * CosTheta);
	}

	float MiePhase(float CosTheta)
	{
		const float G2 = MieG * MieG;
		const float Denom = FMath::Max(1.0f + G2 - 2.0f * MieG * CosTheta, 1e-4f);
		return (1.0f - G2) / (4.0f * Pi * FMath::Pow(Denom, 1.5f));
	}

	/** 近交点，钳到 ≥0；起点在球内或整个球在身后时返回 0，射线不与球相交返回 -1。 */
	double RaySphereNear(const FVector& Origin, const FVector& Dir,
		const FVector& Center, double Radius)
	{
		const FVector Offset = Origin - Center;
		const double B = FVector::DotProduct(Offset, Dir);
		const double C = FVector::DotProduct(Offset, Offset) - Radius * Radius;
		const double Disc = B * B - C;
		if (Disc < 0.0)
		{
			return -1.0;
		}
		const double Near = -B - FMath::Sqrt(Disc);
		return Near > 0.0 ? Near : 0.0;
	}

	/** 远交点；未命中或整个球在身后返回负值。 */
	double RaySphereFar(const FVector& Origin, const FVector& Dir,
		const FVector& Center, double Radius)
	{
		const FVector Offset = Origin - Center;
		const double B = FVector::DotProduct(Offset, Dir);
		const double C = FVector::DotProduct(Offset, Offset) - Radius * Radius;
		const double Disc = B * B - C;
		if (Disc < 0.0)
		{
			return -1.0;
		}
		const double Far = -B + FMath::Sqrt(Disc);
		return Far > 0.0 ? Far : -1.0;
	}

	/**
	 * 地面交点距离；0 表示射线从地表立刻扎进地下，负值表示前方没有地面。
	 * 观测点正好在地表时近交点也是 0，必须用方向区分「扎进地面」与「贴着地表朝上」——
	 * 壳着色器的相机永远在地表之上，没有这个退化情况。
	 */
	float RayGroundHit(const FVector& Origin, const FVector& Dir,
		const FVector& Center, float Radius)
	{
		const FVector Offset = Origin - Center;
		const float B = FVector::DotProduct(Offset, Dir);
		const float C = FVector::DotProduct(Offset, Offset) - Radius * Radius;
		const float Disc = B * B - C;
		if (Disc < 0.0f)
		{
			return -1.0f;
		}
		// 地表/地下朝内：近交点可能是 0，也可能是浮点噪声级的小正数，统一按 0 处理，
		// 否则会积出一段非零的「地下空气」颜色。
		if (FVector::DotProduct(Offset, Offset) <= Radius * Radius && B < 0.0f)
		{
			return 0.0f;
		}
		const float Near = -B - FMath::Sqrt(Disc);
		return Near > 0.0f ? Near : -1.0f;
	}
}

FVector PlanetSky::CubeFaceDirection(int32 FaceIndex, int32 PixelX, int32 PixelY,
	int32 Resolution)
{
	const float Res = static_cast<float>(FMath::Max(Resolution, 1));
	const float U = ((PixelX + 0.5f) / Res) * 2.0f - 1.0f;
	const float V = ((PixelY + 0.5f) / Res) * 2.0f - 1.0f;

	FVector Dir;
	switch (FaceIndex)
	{
	case 0:  Dir = FVector( 1.0f, -V, -U); break;   // +X
	case 1:  Dir = FVector(-1.0f, -V,  U); break;   // -X
	case 2:  Dir = FVector( U,  1.0f,  V); break;   // +Y
	case 3:  Dir = FVector( U, -1.0f, -V); break;   // -Y
	case 4:  Dir = FVector( U, -V,  1.0f); break;   // +Z
	default: Dir = FVector(-U, -V, -1.0f); break;   // -Z
	}
	return Dir.GetSafeNormal();
}

FLinearColor PlanetSky::SampleDirection(const FModelInput& Input, const FVector& Direction)
{
	const FVector Dir = Direction.GetSafeNormal();
	const FVector Center = Input.PlanetCenter;

	// 观察点可能在壳内也可能在壳外：近交点小于 0 时从观察点起算。
	// 距离用 double：观察点飞到 1 AU 时 float 的 ulp 已接近壳厚，Span 会抵消失准。
	const double ShellEnter = RaySphereNear(Input.ObserverLocation, Dir, Center, Input.ShellRadiusCm);
	const double ShellExit = RaySphereFar(Input.ObserverLocation, Dir, Center, Input.ShellRadiusCm);
	if (ShellExit < 0.0)
	{
		// 射线压根不碰大气：往上看就是星空。BrightnessScale 只校准散射，别乘到星空上。
		// 立方图的 alpha 必须恒为 1，不能把 SpaceSkyRadiance 里配的 alpha 带出去。
		FLinearColor Space = Input.SpaceSkyRadiance;
		Space.A = 1.0f;
		return Space;
	}

	double EndT = ShellExit;
	bool bHitGround = false;
	const float GroundHit = RayGroundHit(Input.ObserverLocation, Dir, Center, Input.SurfaceRadiusCm);
	if (GroundHit >= 0.0f && GroundHit < EndT)
	{
		EndT = GroundHit;
		bHitGround = true;
	}

	const int32 StepCount = FMath::Max(Input.Steps, 1);
	const double Span = EndT - ShellEnter;
	FLinearColor Acc = FLinearColor::Black;
	FLinearColor Transmittance = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);

	if (Span > 0.0)
	{
		const double Ds = Span / static_cast<double>(StepCount);
		const FLinearColor RayleighBeta(Input.Scattering.RayleighBeta.X,
			Input.Scattering.RayleighBeta.Y, Input.Scattering.RayleighBeta.Z, 0.0f);
		const FLinearColor MieBeta(Input.Scattering.MieBeta.X,
			Input.Scattering.MieBeta.Y, Input.Scattering.MieBeta.Z, 0.0f);
		const float RayleighScaleHeight = FMath::Max(Input.Scattering.RayleighScaleHeightCm, 1.0f);
		const float MieScaleHeight = FMath::Max(Input.Scattering.MieScaleHeightCm, 1.0f);

		// 相位只依赖「射线方向 · 太阳方向」，沿整条射线不变，按壳着色器的做法提到循环外。
		TArray<float, TInlineAllocator<8>> RayleighPhases;
		TArray<float, TInlineAllocator<8>> MiePhases;
		RayleighPhases.SetNum(Input.Suns.Num());
		MiePhases.SetNum(Input.Suns.Num());
		for (int32 SunIndex = 0; SunIndex < Input.Suns.Num(); ++SunIndex)
		{
			const float CosTheta = FVector::DotProduct(Dir, Input.Suns[SunIndex].Direction);
			RayleighPhases[SunIndex] = RayleighPhase(CosTheta);
			MiePhases[SunIndex] = MiePhase(CosTheta);
		}

		for (int32 Step = 0; Step < StepCount; ++Step)
		{
			const double T = ShellEnter + (Step + 0.5) * Ds;
			const FVector SamplePosition = Input.ObserverLocation + Dir * T;
			const FVector Offset = SamplePosition - Center;
			const float Radius = Offset.Size();
			const float Height = FMath::Max(Radius - Input.SurfaceRadiusCm, 0.0f);

			const float RayleighDensity = FMath::Exp(-Height / RayleighScaleHeight);
			const float MieDensity = FMath::Exp(-Height / MieScaleHeight);

			FLinearColor Scatter = FLinearColor::Black;
			for (int32 SunIndex = 0; SunIndex < Input.Suns.Num(); ++SunIndex)
			{
				const FSun& Sun = Input.Suns[SunIndex];
				// 阴影判据抄 .ush：采样点相对球心的方向与太阳方向的点积 / 半径。
				const float SinSunElevation =
					FVector::DotProduct(Offset, Sun.Direction) / FMath::Max(Radius, 1.0f);
				const float SunVisibility =
					FMath::SmoothStep(SunVisFadeStart, SunVisFadeEnd, SinSunElevation);
				if (SunVisibility <= 0.0f)
				{
					continue;
				}

				const FLinearColor Term =
					RayleighBeta * RayleighDensity * RayleighPhases[SunIndex]
					+ MieBeta * MieDensity * MiePhases[SunIndex];
				Scatter += FLinearColor(Sun.Radiance.R, Sun.Radiance.G, Sun.Radiance.B, 0.0f)
					* SunVisibility * Term;
			}

			const FLinearColor Extinction = RayleighBeta * RayleighDensity + MieBeta * MieDensity;
			const FLinearColor SegmentTransmittance(
				FMath::Exp(-Extinction.R * Ds),
				FMath::Exp(-Extinction.G * Ds),
				FMath::Exp(-Extinction.B * Ds), 1.0f);

			// 与 .ush 的累加顺序一致：先乘新透射率再更新 Tr，反过来结果不同。
			Acc += Scatter * Transmittance * SegmentTransmittance * Ds;
			Transmittance *= SegmentTransmittance;
		}
	}

	// 壳材质输出 float4(Acc, 1-T)。BLEND_Translucent 下可见颜色 = Acc × (1-T) + 背景 × T，
	// 所以这里也要乘 (1-T)；漏掉这一步，天光会比看得见的天空亮好几倍。
	const float Alpha = FMath::Clamp(
		1.0f - (Transmittance.R + Transmittance.G + Transmittance.B) / 3.0f, 0.0f, 1.0f);
	const FLinearColor SunScale = Input.SunIntensity * Input.BrightnessScale;
	FLinearColor Result = Acc * SunScale * Alpha;

	if (!bHitGround)
	{
		// 逃逸到太空：背景是星空，按 1-alpha 透上来。
		Result += Input.SpaceSkyRadiance * (1.0f - Alpha);
		// 云只有一阶衰减：覆盖率越高天越暗。全阴时朝天方向归零。
		Result *= 1.0f - FMath::Clamp(Input.CloudCoverage, 0.0f, 1.0f);
	}

	Result.A = 1.0f;
	return Result;
}

void PlanetSky::RenderCube(const FModelInput& Input, int32 Resolution,
	TArray<FLinearColor>& OutPixels)
{
	const int32 Res = FMath::Max(Resolution, 1);
	OutPixels.SetNumUninitialized(6 * Res * Res);

	int32 Index = 0;
	for (int32 Face = 0; Face < 6; ++Face)
	{
		for (int32 Y = 0; Y < Res; ++Y)
		{
			for (int32 X = 0; X < Res; ++X)
			{
				OutPixels[Index++] = SampleDirection(Input, CubeFaceDirection(Face, X, Y, Res));
			}
		}
	}
}
