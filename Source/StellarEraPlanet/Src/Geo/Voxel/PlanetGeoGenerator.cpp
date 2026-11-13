#include "PlanetGeoGenerator.h"
#include "GeoMaterialCodec.h"
#include "PlanetMaterialEval.h"
#include "PlanetShapeEval.h"
#include "PlanetDebugColor.h"
#include "VoxelIntBox.h"

TVoxelSharedRef<FVoxelGeneratorInstance> UPlanetGeoGenerator::GetInstance()
{
	if (!Snapshot)
	{
		UE_LOG(LogTemp, Error, TEXT("[地质] UPlanetGeoGenerator 没有快照，生成空气"));
	}
	return MakeVoxelShared<FPlanetGeoGeneratorInstance>(*this, Snapshot, DebugColor);
}

FPlanetGeoGeneratorInstance::FPlanetGeoGeneratorInstance(
	UPlanetGeoGenerator& Owner, TSharedPtr<const FPlanetSnapshot> InSnapshot, EPlanetGeoDebugColor InDebugColor)
	: Super(&Owner)
	, Snapshot(MoveTemp(InSnapshot))
	, DebugColor(InDebugColor)
{
}

v_flt FPlanetGeoGeneratorInstance::GetValueImpl(v_flt X, v_flt Y, v_flt Z, int32 LOD, const FVoxelItemStack& Items) const
{
	if (!Snapshot) { return 1; }
	return v_flt(PlanetShapeEval::Value(*Snapshot, FVector3d(X, Y, Z)));
}

FVoxelMaterial FPlanetGeoGeneratorInstance::GetMaterialImpl(v_flt X, v_flt Y, v_flt Z, int32 LOD, const FVoxelItemStack& Items) const
{
	FVoxelMaterial M(ForceInit);
	if (!Snapshot) { return M; }
	const FVector3d Pos(X, Y, Z);
	const FGeoSample S = PlanetMaterialEval::Sample(*Snapshot, Pos);
	FGeoPackedMaterial P = DebugColor == EPlanetGeoDebugColor::None ? GeoMaterialCodec::Encode(S) : GeoMaterialCodec::EncodeDebugColor(S);
	if (DebugColor == EPlanetGeoDebugColor::Elevation)
	{
		const double D = Pos.Size();
		const FVector3f Dir = D > 1e-6 ? FVector3f(Pos / D) : FVector3f::UpVector;
		const double Surface = PlanetShapeEval::SurfaceRadius(*Snapshot, Dir);
		// 只染地表附近：挖下去要能看见圈层。粗 LOD 的材质取在 2^LOD 体素网格点上，可能深达一格，
		// 门限只给 16 会让远看的陆地全落回岩性色。
		if (Surface - D < 16.0 + 2.0 * double(1 << FMath::Clamp(LOD, 0, 20)))
		{
			const FColor C = PlanetDebugColor::FromElevation(float(Surface - Snapshot->R0),
				float(Snapshot->R0 - Snapshot->MinSurfaceRadius), float(Snapshot->MaxSurfaceRadius - Snapshot->R0),
				Snapshot->CrustSplitElevation);
			P.R = C.R; P.G = C.G; P.B = C.B;
		}
	}
	M.SetR(P.R); M.SetG(P.G); M.SetB(P.B); M.SetA(P.A);
	M.SetU0(P.U0); M.SetV0(P.V0); M.SetU1(P.U1); M.SetV1(P.V1);
	return M;
}

TVoxelRange<v_flt> FPlanetGeoGeneratorInstance::GetValueRangeImpl(const FVoxelIntBox& Bounds, int32 LOD, const FVoxelItemStack& Items) const
{
	if (!Snapshot) { return TVoxelRange<v_flt>(1); }
	const FGeoRange R = PlanetShapeEval::ValueRange(*Snapshot,
		FVector3d(Bounds.Min.X, Bounds.Min.Y, Bounds.Min.Z), FVector3d(Bounds.Max.X, Bounds.Max.Y, Bounds.Max.Z));
	return TVoxelRange<v_flt>(v_flt(R.Lo), v_flt(R.Hi));
}

FVector FPlanetGeoGeneratorInstance::GetUpVector(v_flt X, v_flt Y, v_flt Z) const
{
	const FVector P(X, Y, Z);
	return P.SizeSquared() < UE_DOUBLE_SMALL_NUMBER ? FVector::UpVector : P.GetSafeNormal();
}
