// 火山点表条目。烘焙期由阶段 4 产出，进快照供 4b/4c 定位；POD，按值比较。
#pragma once
#include "CoreMinimal.h"
#include "GeoRng.h"

enum class EGeoVolcanoKind : uint8 { None = 0, Strato = 1, Shield, FloodBasalt, Caldera, CinderCone };

struct FGeoVolcano
{
	FVector3f Dir = FVector3f::UpVector;
	EGeoVolcanoKind Kind = EGeoVolcanoKind::None;
	float BaseRadius = 0.f;     // 方向单位（单位球弦长）
	float Height = 0.f;         // 体素，从 BaseElevation 起算
	float CraterRadius = 0.f;   // 方向单位；0 = 无火口
	float CraterDepth = 0.f;    // 体素
	float BaseElevation = 0.f;  // 阶段 3 在火山中心的高程（体素）
	float Age = 0.f;            // 0 新 … 1 老
	bool bActive = false;
	int32 Plate = -1;
	int32 OtherPlate = -1;      // 弧火山：俯冲下去的那块板块；其它为 -1
	int32 Chain = -1;           // 热点链编号；非链上为 -1（破火山口据此决定是盾状还是锥状剖面）
	int32 ChainIndex = -1;      // 链上序号，0 = 热点正上方

	uint64 Hash() const
	{
		const float Fs[10] = { Dir.X, Dir.Y, Dir.Z, BaseRadius, Height, CraterRadius, CraterDepth, BaseElevation, Age, bActive ? 1.f : 0.f };
		uint64 H = GeoHash::Combine(uint64(Kind), uint64(uint32(Plate)) | (uint64(uint32(Chain)) << 32));
		H = GeoHash::Combine(H, uint64(uint32(ChainIndex)) | (uint64(uint32(OtherPlate)) << 32));
		for (const float V : Fs) { H = GeoHash::Combine(H, GeoHash::Float(V)); }
		return H;
	}
	bool operator==(const FGeoVolcano& O) const { return Hash() == O.Hash(); }
};
