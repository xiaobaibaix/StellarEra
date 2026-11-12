#include "PlanetGeoGenerator.h"
#include "GeoMaterialCodec.h"
#include "PlanetMaterialEval.h"
#include "PlanetShapeEval.h"
#include "VoxelIntBox.h"

TVoxelSharedRef<FVoxelGeneratorInstance> UPlanetGeoGenerator::GetInstance()
{
	if (!Snapshot)
	{
		UE_LOG(LogTemp, Error, TEXT("[地质] UPlanetGeoGenerator 没有快照，生成空气"));
	}
	return MakeVoxelShared<FPlanetGeoGeneratorInstance>(*this, Snapshot, bDebugLithologyColor);
}

FPlanetGeoGeneratorInstance::FPlanetGeoGeneratorInstance(
	UPlanetGeoGenerator& Owner, TSharedPtr<const FPlanetSnapshot> InSnapshot, bool bInDebugColor)
	: Super(&Owner)
	, Snapshot(MoveTemp(InSnapshot))
	, bDebugColor(bInDebugColor)
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
	const FGeoSample S = PlanetMaterialEval::Sample(*Snapshot, FVector3d(X, Y, Z));
	const FGeoPackedMaterial P = bDebugColor ? GeoMaterialCodec::EncodeDebugColor(S) : GeoMaterialCodec::Encode(S);
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
