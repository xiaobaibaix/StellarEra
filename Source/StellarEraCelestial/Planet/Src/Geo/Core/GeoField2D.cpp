#include "GeoField2D.h"
#include "Async/ParallelFor.h"
#include "GeoRng.h"

void FGeoField2D::Init(int32 InRes)
{
	Res = FMath::Max(1, InRes);
	Levels.SetNum(1);
	Levels[0].SetNumZeroed(6 * Res * Res);
}

void FGeoField2D::DirToFace(const FVector3f& D, int32& OutFace, float& OutU, float& OutV)
{
	const float AX = FMath::Abs(D.X), AY = FMath::Abs(D.Y), AZ = FMath::Abs(D.Z);
	if (AX >= AY && AX >= AZ) { OutFace = D.X > 0.f ? 0 : 1; OutU = D.Y / AX; OutV = D.Z / AX; }
	else if (AY >= AZ)        { OutFace = D.Y > 0.f ? 2 : 3; OutU = D.X / AY; OutV = D.Z / AY; }
	else                      { OutFace = D.Z > 0.f ? 4 : 5; OutU = D.X / AZ; OutV = D.Y / AZ; }
}

FVector3f FGeoField2D::FaceToDir(int32 Face, float U, float V)
{
	const float S = (Face & 1) ? -1.f : 1.f;
	switch (Face >> 1)
	{
	case 0:  return FVector3f(S, U, V).GetSafeNormal();
	case 1:  return FVector3f(U, S, V).GetSafeNormal();
	default: return FVector3f(U, V, S).GetSafeNormal();
	}
}

FVector3f FGeoField2D::TexelDir(int32 Face, int32 X, int32 Y) const
{
	const float U = (float(X) + 0.5f) / float(Res) * 2.f - 1.f;
	const float V = (float(Y) + 0.5f) / float(Res) * 2.f - 1.f;
	return FaceToDir(Face, U, V);
}

void FGeoField2D::Fill(TFunctionRef<float(const FVector3f&)> Func, bool bSingleThread)
{
	Levels.SetNum(1);
	ParallelFor(6 * Res, [this, &Func](int32 Row)
	{
		const int32 Face = Row / Res, Y = Row % Res;
		for (int32 X = 0; X < Res; ++X) { At(Face, X, Y) = Func(TexelDir(Face, X, Y)); }
	}, bSingleThread ? EParallelForFlags::ForceSingleThread : EParallelForFlags::None);
}

void FGeoField2D::BuildMips(int32 Count)
{
	Levels.SetNum(FMath::Max(1, Count));
	for (int32 Mip = 1; Mip < Levels.Num(); ++Mip)
	{
		const int32 R = MipRes(Mip), P = MipRes(Mip - 1);
		Levels[Mip].SetNumUninitialized(6 * R * R);
		for (int32 Face = 0; Face < 6; ++Face)
		for (int32 Y = 0; Y < R; ++Y)
		for (int32 X = 0; X < R; ++X)
		{
			const int32 X0 = FMath::Min(2 * X, P - 1), X1 = FMath::Min(2 * X + 1, P - 1);
			const int32 Y0 = FMath::Min(2 * Y, P - 1), Y1 = FMath::Min(2 * Y + 1, P - 1);
			At(Face, X, Y, Mip) = 0.25f * (At(Face, X0, Y0, Mip - 1) + At(Face, X1, Y0, Mip - 1)
				+ At(Face, X0, Y1, Mip - 1) + At(Face, X1, Y1, Mip - 1));
		}
	}
}

float FGeoField2D::Sample(const FVector3f& Dir, int32 Mip) const
{
	if (Levels.IsEmpty()) { return 0.f; }
	Mip = FMath::Clamp(Mip, 0, Levels.Num() - 1);
	const int32 R = MipRes(Mip);
	int32 Face; float U, V;
	DirToFace(Dir, Face, U, V);
	const float FX = (U + 1.f) * 0.5f * float(R) - 0.5f, FY = (V + 1.f) * 0.5f * float(R) - 0.5f;
	const int32 X0 = FMath::FloorToInt(FX), Y0 = FMath::FloorToInt(FY);
	const float TX = FX - float(X0), TY = FY - float(Y0);
	const auto C = [R](int32 I) { return FMath::Clamp(I, 0, R - 1); };
	const float A = FMath::Lerp(At(Face, C(X0), C(Y0), Mip), At(Face, C(X0 + 1), C(Y0), Mip), TX);
	const float B = FMath::Lerp(At(Face, C(X0), C(Y0 + 1), Mip), At(Face, C(X0 + 1), C(Y0 + 1), Mip), TX);
	return FMath::Lerp(A, B, TY);
}

uint64 FGeoField2D::Hash() const
{
	uint64 H = GeoHash::Combine(uint64(Res), uint64(Levels.Num()));
	for (const TArray<float>& Level : Levels)
	{
		for (const float V : Level) { H = GeoHash::Combine(H, GeoHash::Float(V)); }
	}
	return H;
}

SIZE_T FGeoField2D::GetAllocatedSize() const
{
	SIZE_T Size = Levels.GetAllocatedSize();
	for (const TArray<float>& Level : Levels) { Size += Level.GetAllocatedSize(); }
	return Size;
}

float FGeoField2D::SampleNearest(const FVector3f& Dir) const
{
	if (Levels.IsEmpty()) { return 0.f; }
	int32 Face; float U, V;
	DirToFace(Dir, Face, U, V);
	const int32 X = FMath::Clamp(int32((U + 1.f) * 0.5f * float(Res)), 0, Res - 1);
	const int32 Y = FMath::Clamp(int32((V + 1.f) * 0.5f * float(Res)), 0, Res - 1);
	return At(Face, X, Y);
}

void FGeoField2D::Serialize(FArchive& Ar)
{
	Ar << Res;
	int32 Num = Levels.Num();
	Ar << Num;
	if (Ar.IsLoading()) { Levels.SetNum(Num); }
	for (int32 i = 0; i < Num; ++i)
	{
		int32 Count = Levels[i].Num();
		Ar << Count;
		if (Ar.IsLoading()) { Levels[i].SetNum(Count); }
		if (Count > 0) { Ar.Serialize(Levels[i].GetData(), Count * sizeof(float)); }
	}
}
