#include "PlanetGeoDebugRender.h"
#include "Async/ParallelFor.h"
#include "Engine/TextureCube.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PlanetBiomeCubemap.h"
#include "PlanetBakePipeline.h"
#include "PlanetGeoDebugView3D.h"
#include "PlanetShapeEval.h"

namespace
{
	const FColor GeoDebugSpaceColor(12, 12, 18, 255);

	FColor GeoDebugOpaque(FColor C) { C.A = 255; return C; }

	float GeoDebugSampleAt(const FPlanetSnapshot& S, const FGeoDebugLayer& L, const FVector3d& P)
	{
		return L.Sample ? L.Sample(S, P) : FGeoDebugLayer::NoData();
	}
}

FVector3d PlanetGeoDebugRender::SurfacePoint(const FPlanetSnapshot& S, const FVector3f& Dir, EGeoDebugDomain Domain)
{
	const double R = PlanetShapeEval::SurfaceRadius(S, Dir) - (Domain == EGeoDebugDomain::Volume ? 2.0 : 0.0);
	return FVector3d(Dir) * R;
}

FVector3f PlanetGeoDebugRender::MapPixelToDir(int32 X, int32 Y, int32 W, int32 H)
{
	const double Lon = (double(X) + 0.5) / double(W) * 2.0 * PI - PI;
	const double Lat = 0.5 * PI - (double(Y) + 0.5) / double(H) * PI;
	return FVector3f(float(FMath::Cos(Lat) * FMath::Cos(Lon)), float(FMath::Cos(Lat) * FMath::Sin(Lon)), float(FMath::Sin(Lat)));
}

void PlanetGeoDebugRender::RenderMap(const FPlanetSnapshot& S, const FGeoDebugLayer& Layer, int32 W, int32 H,
	TArray<FColor>& OutColors, TArray<float>* OutValues)
{
	W = FMath::Max(W, 1); H = FMath::Max(H, 1);
	OutColors.SetNumUninitialized(W * H);
	if (OutValues) { OutValues->SetNumUninitialized(W * H); }
	ParallelFor(H, [&](int32 Y)
	{
		for (int32 X = 0; X < W; ++X)
		{
			const float V = GeoDebugSampleAt(S, Layer, SurfacePoint(S, MapPixelToDir(X, Y, W, H), Layer.Domain));
			OutColors[Y * W + X] = GeoDebugOpaque(Layer.ColorOf(S, V));
			if (OutValues) { (*OutValues)[Y * W + X] = V; }
		}
	});
}

double PlanetGeoDebugRender::SectionHalfExtent(const FPlanetSnapshot& S)
{
	return FMath::Max(S.MaxSurfaceRadius, S.R0) * 1.02;
}

FVector3d PlanetGeoDebugRender::SectionPixelToPos(const FPlanetSnapshot& S, int32 X, int32 Y, int32 Size, ESectionPlane Plane)
{
	const double E = SectionHalfExtent(S);
	const double U = ((double(X) + 0.5) / double(Size) * 2.0 - 1.0) * E;
	const double V = (1.0 - (double(Y) + 0.5) / double(Size) * 2.0) * E;
	switch (Plane)
	{
	case ESectionPlane::YZ: return FVector3d(0.0, U, V);
	case ESectionPlane::XY: return FVector3d(U, V, 0.0);
	default:                return FVector3d(U, 0.0, V);
	}
}

void PlanetGeoDebugRender::RenderSection(const FPlanetSnapshot& S, const FGeoDebugLayer& Layer, int32 Size, ESectionPlane Plane,
	TArray<FColor>& OutColors, TArray<float>* OutValues)
{
	Size = FMath::Max(Size, 1);
	OutColors.SetNumUninitialized(Size * Size);
	if (OutValues) { OutValues->SetNumUninitialized(Size * Size); }
	ParallelFor(Size, [&](int32 Y)
	{
		for (int32 X = 0; X < Size; ++X)
		{
			const FVector3d P = SectionPixelToPos(S, X, Y, Size, Plane);
			float V = FGeoDebugLayer::NoData();
			FColor C = GeoDebugSpaceColor;
			if (PlanetShapeEval::Value(S, P) <= 0.f)
			{
				const FVector3d Q = Layer.Domain == EGeoDebugDomain::Volume ? P
					: SurfacePoint(S, FVector3f(P.GetSafeNormal()), Layer.Domain);
				V = GeoDebugSampleAt(S, Layer, Q);
				C = GeoDebugOpaque(Layer.ColorOf(S, V));
			}
			OutColors[Y * Size + X] = C;
			if (OutValues) { (*OutValues)[Y * Size + X] = V; }
		}
	});
}

bool PlanetGeoDebugRender::SavePng(const FString& Path, int32 W, int32 H, const TArray<FColor>& Colors)
{
	if (Colors.Num() != W * H || W <= 0 || H <= 0) { return false; }
	TArray64<uint8> Png;
	FImageUtils::PNGCompressImageArray(W, H, TArrayView64<const FColor>(Colors.GetData(), Colors.Num()), Png);
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	return Png.Num() > 0 && FFileHelper::SaveArrayToFile(TArrayView64<const uint8>(Png.GetData(), Png.Num()), *Path);
}

UTextureCube* PlanetGeoDebugRender::BakeCube(const FPlanetSnapshot& S, const FGeoDebugLayer& Layer, int32 Resolution)
{
	if (Resolution <= 0) { return nullptr; }
	UTextureCube* Texture = UTextureCube::CreateTransient(Resolution, Resolution, PF_B8G8R8A8, NAME_None);
	if (!Texture || !Texture->GetPlatformData() || Texture->GetPlatformData()->Mips.Num() == 0) { return nullptr; }
	// 调试色是 sRGB 编码值，与 BiomeCube 同约定。
	Texture->SRGB = true;
	Texture->Filter = TF_Bilinear;

	TArray<FColor> All;
	All.SetNumUninitialized(6 * Resolution * Resolution);
	ParallelFor(6 * Resolution, [&](int32 Row)
	{
		const int32 Face = Row / Resolution, Y = Row % Resolution;
		for (int32 X = 0; X < Resolution; ++X)
		{
			const FVector D = PlanetBiomeCubemap::FaceUVToDirection(Face,
				(float(X) + 0.5f) / float(Resolution), (float(Y) + 0.5f) / float(Resolution));
			const FVector3f Dir(D.GetSafeNormal());
			All[Row * Resolution + X] = GeoDebugOpaque(Layer.ColorOf(S, GeoDebugSampleAt(S, Layer, SurfacePoint(S, Dir, Layer.Domain))));
		}
	});

	FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
	void* Dest = Mip.BulkData.Lock(LOCK_READ_WRITE);
	if (!Dest) { Mip.BulkData.Unlock(); return nullptr; }
	FMemory::Memcpy(Dest, All.GetData(), All.Num() * sizeof(FColor));
	Mip.BulkData.Unlock();
	Texture->UpdateResource();
	return Texture;
}

int32 PlanetGeoDebugRender::ExportAll(const FPlanetSnapshot& S, const TArray<FGeoDebugLayer>& Layers,
	const FString& Directory, int32 MapWidth)
{
	MapWidth = FMath::Max(MapWidth, 64);
	int32 Written = 0;
	TArray<FColor> Colors;
	for (const FGeoDebugLayer& L : Layers)
	{
		const FString Base = FPaths::Combine(Directory, FString::Printf(TEXT("Stage%u_%s"), L.Stage, *L.Id.ToString()));
		RenderMap(S, L, MapWidth, MapWidth / 2, Colors);
		Written += SavePng(Base + TEXT("_map.png"), MapWidth, MapWidth / 2, Colors) ? 1 : 0;
		RenderSection(S, L, MapWidth / 2, ESectionPlane::XZ, Colors);
		Written += SavePng(Base + TEXT("_section.png"), MapWidth / 2, MapWidth / 2, Colors) ? 1 : 0;
		PlanetGeoDebugView3D::Render(S, L, FPlanetBakePipeline::FindDefaultDebugLayer(TEXT("Elevation")),
			PlanetGeoDebugView3D::FView(), MapWidth / 2, MapWidth / 2, Colors);
		Written += SavePng(Base + TEXT("_view3d.png"), MapWidth / 2, MapWidth / 2, Colors) ? 1 : 0;
	}
	return Written;
}
