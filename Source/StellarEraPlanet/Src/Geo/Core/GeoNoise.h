// 确定性值噪声。不用插件的 FastNoise：Core 目录禁止依赖 Voxel，且要能逐位复现。
#pragma once
#include "CoreMinimal.h"
#include "GeoRng.h"

namespace GeoNoise
{
	FORCEINLINE float Lattice(uint64 Seed, int32 X, int32 Y, int32 Z)
	{
		uint64 H = GeoHash::Combine(Seed, uint64(uint32(X)) | (uint64(uint32(Y)) << 32));
		H = GeoHash::Combine(H, uint64(uint32(Z)));
		return float(H >> 40) * (2.f / 16777216.f) - 1.f;
	}
	/** 五次缓动：一阶、二阶导在格点处为 0，法线不出折痕。 */
	FORCEINLINE float Fade(float T) { return T * T * T * (T * (T * 6.f - 15.f) + 10.f); }

	/** ∈ [-1,1]。 */
	FORCEINLINE float Value3(uint64 Seed, const FVector3f& P)
	{
		const int32 X0 = FMath::FloorToInt(P.X), Y0 = FMath::FloorToInt(P.Y), Z0 = FMath::FloorToInt(P.Z);
		const float TX = Fade(P.X - float(X0)), TY = Fade(P.Y - float(Y0)), TZ = Fade(P.Z - float(Z0));
		const float X00 = FMath::Lerp(Lattice(Seed, X0, Y0, Z0),         Lattice(Seed, X0 + 1, Y0, Z0), TX);
		const float X10 = FMath::Lerp(Lattice(Seed, X0, Y0 + 1, Z0),     Lattice(Seed, X0 + 1, Y0 + 1, Z0), TX);
		const float X01 = FMath::Lerp(Lattice(Seed, X0, Y0, Z0 + 1),     Lattice(Seed, X0 + 1, Y0, Z0 + 1), TX);
		const float X11 = FMath::Lerp(Lattice(Seed, X0, Y0 + 1, Z0 + 1), Lattice(Seed, X0 + 1, Y0 + 1, Z0 + 1), TX);
		return FMath::Lerp(FMath::Lerp(X00, X10, TY), FMath::Lerp(X01, X11, TY), TZ);
	}

	/** 按振幅和归一化，∈ [-1,1]；每个 octave 换种子，避免各层格点对齐。 */
	FORCEINLINE float Fbm3(uint64 Seed, FVector3f P, int32 Octaves, float Lacunarity = 2.f, float Gain = 0.5f)
	{
		float Sum = 0.f, Amp = 1.f, Norm = 0.f;
		for (int32 i = 0; i < Octaves; ++i)
		{
			Sum += Amp * Value3(GeoHash::Combine(Seed, uint64(i)), P);
			Norm += Amp;
			Amp *= Gain;
			P *= Lacunarity;
		}
		return Norm > 0.f ? Sum / Norm : 0.f;
	}
}
