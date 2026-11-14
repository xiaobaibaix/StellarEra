#include "PlanetDebugLayer.h"
#include "PlanetSnapshot.h"

FVector2f FGeoDebugLayer::GetRange(const FPlanetSnapshot& S) const
{
	return Range ? Range(S) : FVector2f(Min, Max);
}

FColor FGeoDebugLayer::Ramp(float T)
{
	static const FLinearColor Stops[5] = {
		FLinearColor(0.27f, 0.00f, 0.33f), FLinearColor(0.23f, 0.32f, 0.55f), FLinearColor(0.13f, 0.57f, 0.55f),
		FLinearColor(0.37f, 0.79f, 0.38f), FLinearColor(0.99f, 0.91f, 0.14f) };
	const float X = FMath::Clamp(T, 0.f, 1.f) * 4.f;
	const int32 I = FMath::Min(int32(X), 3);
	return FMath::Lerp(Stops[I], Stops[I + 1], X - float(I)).ToFColor(false);
}

FColor FGeoDebugLayer::ColorOf(const FPlanetSnapshot& S, float V) const
{
	if (FMath::IsNaN(V)) { return NoDataColor(); }
	if (Colorize) { return Colorize(S, V); }
	if (Kind == EGeoDebugKind::Categorical)
	{
		const int32 I = FMath::RoundToInt(V);
		return Legend.IsValidIndex(I) ? Legend[I].Color : NoDataColor();
	}
	const FVector2f R = GetRange(S);
	return Ramp(R.Y > R.X ? (V - R.X) / (R.Y - R.X) : 0.5f);
}

TArray<FGeoDebugLegendEntry> FGeoDebugLayer::GetLegend(const FPlanetSnapshot& S) const
{
	return DynamicLegend ? DynamicLegend(S) : Legend;
}

FString FGeoDebugLayer::FormatValue(const FPlanetSnapshot& S, float V) const
{
	if (FMath::IsNaN(V)) { return TEXT("无数据"); }
	if (Kind == EGeoDebugKind::Categorical)
	{
		const int32 I = FMath::RoundToInt(V);
		const TArray<FGeoDebugLegendEntry> L = GetLegend(S);
		return L.IsValidIndex(I) ? L[I].Label : FString::Printf(TEXT("%d"), I);
	}
	return Unit.IsEmpty() ? FString::Printf(TEXT("%.3f"), V) : FString::Printf(TEXT("%.1f %s"), V, *Unit);
}
