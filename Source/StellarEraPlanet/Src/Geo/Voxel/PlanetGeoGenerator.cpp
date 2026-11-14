#include "PlanetGeoGenerator.h"
#include "GeoMaterialCodec.h"
#include "PlanetBakePipeline.h"
#include "PlanetDebugColor.h"
#include "PlanetGeoDebugRender.h"
#include "PlanetMaterialEval.h"
#include "PlanetShapeEval.h"
#include "VoxelIntBox.h"

namespace
{
	/** 到切除区的有符号距离（体素），> 0 在被切掉的那一侧。 */
	double GeoCutDistance(EPlanetGeoCutaway Cut, const FVector3d& P)
	{
		switch (Cut)
		{
		case EPlanetGeoCutaway::Half:    return P.X;
		case EPlanetGeoCutaway::Quarter: return FMath::Min(P.X, -P.Y);
		default:                         return -1e30;
		}
	}

	void GeoSetRGB(FGeoPackedMaterial& P, const FColor& C) { P.R = C.R; P.G = C.G; P.B = C.B; }

	/** 粗 LOD 的材质采样点在 2^LOD 体素网格上，可能离表面一整格；门限要跟着放宽。 */
	double GeoNearBand(int32 LOD) { return 16.0 + 2.0 * double(1 << FMath::Clamp(LOD, 0, 20)); }
}

TVoxelSharedRef<FVoxelGeneratorInstance> UPlanetGeoGenerator::GetInstance()
{
	if (!Snapshot)
	{
		UE_LOG(LogTemp, Error, TEXT("[地质] UPlanetGeoGenerator 没有快照，生成空气"));
	}
	FPlanetGeoDebugView View;
	View.Color = DebugColor;
	View.Cutaway = Cutaway;
	View.SurfaceLayer = FPlanetBakePipeline::FindDefaultDebugLayer(SurfaceLayer);
	View.VolumeLayer = FPlanetBakePipeline::FindDefaultDebugLayer(VolumeLayer);
	return MakeVoxelShared<FPlanetGeoGeneratorInstance>(*this, Snapshot, View);
}

FPlanetGeoGeneratorInstance::FPlanetGeoGeneratorInstance(
	UPlanetGeoGenerator& Owner, TSharedPtr<const FPlanetSnapshot> InSnapshot, const FPlanetGeoDebugView& InView)
	: Super(&Owner)
	, Snapshot(MoveTemp(InSnapshot))
	, View(InView)
{
}

v_flt FPlanetGeoGeneratorInstance::GetValueImpl(v_flt X, v_flt Y, v_flt Z, int32 LOD, const FVoxelItemStack& Items) const
{
	if (!Snapshot) { return 1; }
	const FVector3d P(X, Y, Z);
	const float V = PlanetShapeEval::Value(*Snapshot, P);
	if (View.Cutaway == EPlanetGeoCutaway::None) { return v_flt(V); }
	return v_flt(FMath::Max(double(V), GeoCutDistance(View.Cutaway, P) / double(Snapshot->DensityScale)));
}

FVoxelMaterial FPlanetGeoGeneratorInstance::GetMaterialImpl(v_flt X, v_flt Y, v_flt Z, int32 LOD, const FVoxelItemStack& Items) const
{
	FVoxelMaterial M(ForceInit);
	if (!Snapshot) { return M; }
	const FVector3d Pos(X, Y, Z);
	const FGeoSample S = PlanetMaterialEval::Sample(*Snapshot, Pos);
	FGeoPackedMaterial P = View.Color == EPlanetGeoDebugColor::None ? GeoMaterialCodec::Encode(S) : GeoMaterialCodec::EncodeDebugColor(S);

	const double D = Pos.Size();
	const FVector3f Dir = D > 1e-6 ? FVector3f(Pos / D) : FVector3f::UpVector;
	const double Surface = PlanetShapeEval::SurfaceRadius(*Snapshot, Dir);
	const bool bNearSurface = Surface - D < GeoNearBand(LOD);
	const bool bOnCut = View.Cutaway != EPlanetGeoCutaway::None && View.VolumeLayer
		&& GeoCutDistance(View.Cutaway, Pos) > -GeoNearBand(LOD);

	if (bOnCut)
	{
		const FGeoDebugLayer& L = *View.VolumeLayer;
		const FVector3d Q = L.Domain == EGeoDebugDomain::Volume ? Pos : PlanetGeoDebugRender::SurfacePoint(*Snapshot, Dir, L.Domain);
		GeoSetRGB(P, L.ColorOf(*Snapshot, L.Sample ? L.Sample(*Snapshot, Q) : FGeoDebugLayer::NoData()));
	}
	else if (bNearSurface && View.SurfaceLayer)
	{
		const FGeoDebugLayer& L = *View.SurfaceLayer;
		const FVector3d Q = PlanetGeoDebugRender::SurfacePoint(*Snapshot, Dir, L.Domain);
		GeoSetRGB(P, L.ColorOf(*Snapshot, L.Sample ? L.Sample(*Snapshot, Q) : FGeoDebugLayer::NoData()));
	}
	else if (bNearSurface && View.Color == EPlanetGeoDebugColor::Elevation)
	{
		// 只染地表附近：挖下去要能看见圈层。
		GeoSetRGB(P, PlanetDebugColor::FromElevation(float(Surface - Snapshot->R0),
			float(Snapshot->R0 - Snapshot->MinSurfaceRadius), float(Snapshot->MaxSurfaceRadius - Snapshot->R0),
			Snapshot->CrustSplitElevation));
	}
	M.SetR(P.R); M.SetG(P.G); M.SetB(P.B); M.SetA(P.A);
	M.SetU0(P.U0); M.SetV0(P.V0); M.SetU1(P.U1); M.SetV1(P.V1);
	return M;
}

TVoxelRange<v_flt> FPlanetGeoGeneratorInstance::GetValueRangeImpl(const FVoxelIntBox& Bounds, int32 LOD, const FVoxelItemStack& Items) const
{
	if (!Snapshot) { return TVoxelRange<v_flt>(1); }
	const FVector3d Lo(Bounds.Min.X, Bounds.Min.Y, Bounds.Min.Z), Hi(Bounds.Max.X, Bounds.Max.Y, Bounds.Max.Z);
	const FGeoRange R = PlanetShapeEval::ValueRange(*Snapshot, Lo, Hi);
	if (View.Cutaway == EPlanetGeoCutaway::None) { return TVoxelRange<v_flt>(v_flt(R.Lo), v_flt(R.Hi)); }
	// max(V, C) 的区间 = [max(V.lo, C.lo), max(V.hi, C.hi)]；min(X, −Y) 的区间同理逐端取 min。
	double CLo = Lo.X, CHi = Hi.X;
	if (View.Cutaway == EPlanetGeoCutaway::Quarter) { CLo = FMath::Min(Lo.X, -Hi.Y); CHi = FMath::Min(Hi.X, -Lo.Y); }
	const double Scale = double(Snapshot->DensityScale);
	return TVoxelRange<v_flt>(v_flt(FMath::Max(double(R.Lo), CLo / Scale)), v_flt(FMath::Max(double(R.Hi), CHi / Scale)));
}

FVector FPlanetGeoGeneratorInstance::GetUpVector(v_flt X, v_flt Y, v_flt Z) const
{
	const FVector P(X, Y, Z);
	return P.SizeSquared() < UE_DOUBLE_SMALL_NUMBER ? FVector::UpVector : P.GetSafeNormal();
}
