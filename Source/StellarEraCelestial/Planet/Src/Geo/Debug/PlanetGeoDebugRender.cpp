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
#include "PlanetMaterialEval.h"
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

uint8 PlanetGeoDebugRender::MineralAt(const FPlanetSnapshot& S, const FVector3d& P, float Scale)
{
	if (Scale <= 1.001f) { return PlanetMaterialEval::Sample(S, P).Mineral; }
	const FVector3f Pf(P);
	uint8 Best = 0;
	float BestD = 1.f;
	if (!S.Primitives.IsEmpty())
	{
		// 原语包围盒是真实尺寸：查询盒按最大半宽外扩，放大后的矿体才不会被 BVH 剪掉。
		const float Pad = S.Primitives.GetMaxHalfExtent() * (Scale - 1.f) + 1.f;
		const TArray<FGeoPrimitive>& Prims = S.Primitives.GetPrimitives();
		S.Primitives.QueryBox(FBox3f(Pf - FVector3f(Pad), Pf + FVector3f(Pad)), [&](int32 i)
		{
			const FGeoPrimitive& Pr = Prims[i];
			if (Pr.MineralId == 0) { return; }
			const FVector3f L = Pr.Kind == EGeoPrimitiveKind::OrientedBlob ? Pr.Rotation.UnrotateVector(Pf - Pr.Center) : Pf - Pr.Center;
			const float D = (L / (Pr.Radii * Scale)).Size();
			if (D < BestD) { BestD = D; Best = Pr.MineralId; }
		});
	}
	if (Best != 0 || S.MineralOverlays.IsEmpty()) { return Best; }
	const double Dist = P.Size();
	const FVector3f Dir = Dist > 1e-6 ? FVector3f(P / Dist) : FVector3f::UpVector;
	const double Below = S.R0 + double(S.Elevation.Res > 0 ? S.Elevation.Sample(Dir) : 0.f) - Dist;
	for (const FGeoMineralOverlay& O : S.MineralOverlays)
	{
		if (Below >= -2.0 && Below <= double(O.DepthVoxels * Scale) && O.Mask.Sample(Dir) > 0.5f) { return O.MineralId; }
	}
	return 0;
}

void PlanetGeoDebugRender::RenderSection(const FPlanetSnapshot& S, const FGeoDebugLayer& Layer, int32 Size, ESectionPlane Plane,
	TArray<FColor>& OutColors, TArray<float>* OutValues, float MineralScale)
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
				if (MineralScale > 0.f)
				{
					const uint8 Id = MineralAt(S, P, MineralScale);
					if (Id != 0) { C = GeoDebugOpaque(S.MineralColor(Id)); }
				}
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

UTextureCube* PlanetGeoDebugRender::BakeCube(const FPlanetSnapshot& S, const FGeoDebugLayer& Layer, int32 Resolution,
	const FQuat& WorldToField)
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
			const FVector3f Dir(WorldToField.RotateVector(D).GetSafeNormal());
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
		// 地下图层（矿体图层自己除外）在剖面与 3D 切面上叠矿，放大 20 倍才看得见。
		const float OreScale = L.Domain == EGeoDebugDomain::Volume && !L.RayHits ? 20.f : 0.f;
		RenderSection(S, L, MapWidth / 2, ESectionPlane::XZ, Colors, nullptr, OreScale);
		Written += SavePng(Base + TEXT("_section.png"), MapWidth / 2, MapWidth / 2, Colors) ? 1 : 0;
		PlanetGeoDebugView3D::FView View;
		View.MineralScale = OreScale;
		// 与查看器同口径：默认隐藏的类（例如「无矿」）在导出里也隐藏，否则岩石把矿体全挡住。
		for (const int32 i : L.DefaultHidden) { if (i >= 0) { View.Hidden.SetNum(FMath::Max(View.Hidden.Num(), i + 1)); View.Hidden[i] = true; } }
		PlanetGeoDebugView3D::Render(S, L, L.DefaultHidden.IsEmpty() ? FPlanetBakePipeline::FindDefaultDebugLayer(TEXT("Elevation")) : nullptr,
			View, MapWidth / 2, MapWidth / 2, Colors);
		Written += SavePng(Base + TEXT("_view3d.png"), MapWidth / 2, MapWidth / 2, Colors) ? 1 : 0;
	}
	return Written;
}
