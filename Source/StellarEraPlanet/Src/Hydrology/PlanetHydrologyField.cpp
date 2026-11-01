// 水文场的构造与采样。

#include "PlanetHydrologyField.h"

#include "PlanetBiomeCubemap.h"
#include "PlanetHydrologyGrid.h"

TSharedRef<FPlanetHydrologyField> FPlanetHydrologyField::Create(
	int32 InResolution,
	float InSeaLevelRadius,
	float InMaxCarveDepth,
	TArray<float>&& InWaterLevel,
	TArray<float>&& InCarveDepth,
	TArray<float>&& InRiverStrength,
	TArray<FPlanetLakeBasin>&& InLakes,
	TArray<FPlanetRiverPolyline>&& InRivers)
{
	TSharedRef<FPlanetHydrologyField> Field = MakeShared<FPlanetHydrologyField>();
	Field->Resolution = FMath::Max(InResolution, 0);
	Field->SeaLevelRadius = InSeaLevelRadius;
	Field->MaxCarveDepth = InMaxCarveDepth;
	Field->WaterLevel = MoveTemp(InWaterLevel);
	Field->CarveDepth = MoveTemp(InCarveDepth);
	Field->RiverStrength = MoveTemp(InRiverStrength);
	Field->Lakes = MoveTemp(InLakes);
	Field->Rivers = MoveTemp(InRivers);
	return Field;
}

bool FPlanetHydrologyField::IsValid() const
{
	return Resolution > 0 && WaterLevel.Num() == PlanetHydrologyGrid::CellCount(Resolution);
}

FPlanetHydrologyField::FSample FPlanetHydrologyField::Sample(const FVector& UnitDir) const
{
	FSample Out;
	if (!IsValid())
	{
		return Out;
	}

	int32 Face = 0;
	float U = 0.f;
	float V = 0.f;
	PlanetBiomeCubemap::DirectionToFaceUV(UnitDir, Face, U, V);

	// U ∈ [0,1] ⇒ 连续格坐标 ∈ [0.5, N+0.5]，于是 X0 ∈ [0, N]、X1 = X0+1 ≤ N+1：
	// 双线性**永远**落在栅格内，不需要任何边界分支（见 PlanetHydrologyGrid 的推导）。
	const float GridX = PlanetHydrologyGrid::UToGrid(U, Resolution);
	const float GridY = PlanetHydrologyGrid::UToGrid(V, Resolution);
	const int32 X0 = FMath::Clamp(FMath::FloorToInt(GridX), 0, Resolution);
	const int32 Y0 = FMath::Clamp(FMath::FloorToInt(GridY), 0, Resolution);
	const int32 X1 = X0 + 1;
	const int32 Y1 = Y0 + 1;
	const float Tx = FMath::Clamp(GridX - float(X0), 0.f, 1.f);
	const float Ty = FMath::Clamp(GridY - float(Y0), 0.f, 1.f);

	const int32 Size = PlanetHydrologyGrid::PaddedSize(Resolution);
	const int32 Base = Face * Size * Size;
	const auto Bilinear = [&](const TArray<float>& In) -> float
	{
		const float V00 = In[Base + Y0 * Size + X0];
		const float V10 = In[Base + Y0 * Size + X1];
		const float V01 = In[Base + Y1 * Size + X0];
		const float V11 = In[Base + Y1 * Size + X1];
		return FMath::Lerp(FMath::Lerp(V00, V10, Tx), FMath::Lerp(V01, V11, Tx), Ty);
	};

	Out.WaterLevelRadius = Bilinear(WaterLevel);
	Out.CarveDepth = Bilinear(CarveDepth);
	Out.RiverStrength = Bilinear(RiverStrength);
	return Out;
}
