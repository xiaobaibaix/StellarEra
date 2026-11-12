// 确定性哈希与随机流。烘焙期一切随机都从这里派生，禁止 FMath::Rand（全局状态，跨线程不确定）。
#pragma once
#include "CoreMinimal.h"

namespace GeoHash
{
	/** splitmix64 终结器：雪崩好、无状态。 */
	FORCEINLINE uint64 Mix64(uint64 X)
	{
		X += 0x9E3779B97F4A7C15ull;
		X = (X ^ (X >> 30)) * 0xBF58476D1CE4E5B9ull;
		X = (X ^ (X >> 27)) * 0x94D049BB133111EBull;
		return X ^ (X >> 31);
	}
	FORCEINLINE uint64 Combine(uint64 A, uint64 B)
	{
		return Mix64(A ^ (B + 0x9E3779B97F4A7C15ull + (A << 6) + (A >> 2)));
	}
	/** 按位哈希 float：-0 与 +0 不同是可接受的，烘焙产物里两者不会混用。 */
	FORCEINLINE uint64 Float(float V)
	{
		uint32 Bits;
		FMemory::Memcpy(&Bits, &V, sizeof(Bits));
		return Mix64(Bits);
	}
	/** FNV-1a，编译期把用途名换成 64 位标签。 */
	constexpr uint64 Str(const char* S)
	{
		uint64 H = 1469598103934665603ull;
		while (*S) { H = (H ^ uint8(*S++)) * 1099511628211ull; }
		return H;
	}
}

struct FGeoRng
{
	uint64 State = 0;

	static FGeoRng Derive(uint64 Seed, uint32 Stage, uint64 Purpose)
	{
		FGeoRng R;
		R.State = GeoHash::Combine(GeoHash::Combine(GeoHash::Mix64(Seed), Stage), Purpose);
		return R;
	}
	uint64 NextU64() { State += 0x9E3779B97F4A7C15ull; return GeoHash::Mix64(State); }
	/** 取高 24 位：float 尾数只有 24 位，多取会出现 1.0。 */
	float NextFloat() { return float(NextU64() >> 40) * (1.f / 16777216.f); }
	float Range(float Lo, float Hi) { return Lo + (Hi - Lo) * NextFloat(); }
};
