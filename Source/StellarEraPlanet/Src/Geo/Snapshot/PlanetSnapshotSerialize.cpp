#include "PlanetSnapshotSerialize.h"
#include "GeoField2D.h"
#include "GeoMineral.h"
#include "GeoPrimitiveIndex.h"
#include "GeoProfile.h"
#include "GeoVolcano.h"
#include "PlanetSnapshot.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

namespace
{
	constexpr uint32 GeoBakeMagic = 0x434F4547u; // 'GEOC'

	template <typename T>
	void RawWrite(FArchive& Ar, const TArray<T>& Array)
	{
		int32 Count = Array.Num();
		Ar << Count;
		if (Count > 0) { Ar.Serialize(const_cast<T*>(Array.GetData()), int64(Count) * sizeof(T)); }
	}

	template <typename T>
	void RawRead(FArchive& Ar, TArray<T>& Array)
	{
		int32 Count = 0;
		Ar << Count;
		Array.SetNum(Count);
		if (Count > 0) { Ar.Serialize(Array.GetData(), int64(Count) * sizeof(T)); }
	}

	void WriteField(FArchive& Ar, const FGeoField2D& In)
	{
		const_cast<FGeoField2D&>(In).Serialize(Ar);
	}

	void WriteProfile(FArchive& Ar, const FGeoProfile& Profile)
	{
		int32 Count = Profile.Layers.Num();
		Ar << Count;
		for (int32 i = 0; i < Count; ++i)
		{
			const FGeoProfileLayer& L = Profile.Layers[i];
			Ar << const_cast<float&>(L.BottomDepthRatio) << const_cast<uint8&>(L.LithologyId) << const_cast<float&>(L.WobbleRatio);
		}
	}

	void ReadProfile(FArchive& Ar, FGeoProfile& Profile)
	{
		int32 Count = 0;
		Ar << Count;
		Profile.Layers.SetNum(Count);
		for (int32 i = 0; i < Count; ++i)
		{
			FGeoProfileLayer& L = Profile.Layers[i];
			Ar << L.BottomDepthRatio << L.LithologyId << L.WobbleRatio;
		}
	}
}

bool SerializePlanetSnapshot(const FPlanetSnapshot& S, TArray<uint8>& OutBlob)
{
	OutBlob.Reset();
	FMemoryWriter Ar(OutBlob, /*bIsPersistent=*/ true);
	uint32 Magic = GeoBakeMagic;
	int32 Version = GeoBakeCacheFormat::Version;
	Ar << Magic << Version;

	Ar << const_cast<double&>(S.R0) << const_cast<float&>(S.DensityScale);
	WriteProfile(Ar, S.Radial);
	WriteField(Ar, S.MohoWobble);
	RawWrite(Ar, S.Primitives.GetPrimitives());
	WriteField(Ar, S.Elevation);
	Ar << const_cast<float&>(S.CrustSplitElevation) << const_cast<float&>(S.PrimitiveDepthMaxRatio);
	for (int32 i = 0; i < 3; ++i) { Ar << const_cast<float&>(S.AlterBands[i]); }
	Ar << const_cast<double&>(S.MinSurfaceRadius) << const_cast<double&>(S.MaxSurfaceRadius);
	Ar << const_cast<uint64&>(S.ContentHash) << const_cast<uint32&>(S.DisabledStages);

	int32 DebugCount = S.DebugFields.Num();
	Ar << DebugCount;
	for (const TPair<FName, FGeoField2D>& Pair : S.DebugFields)
	{
		FName Name = Pair.Key;
		Ar << Name;
		WriteField(Ar, Pair.Value);
	}

	RawWrite(Ar, S.Volcanoes);
	// FGeoMineralInfo 含 FString，不是 POD：裸拷会把字符串内部指针写进缓存，读回来析构即崩。
	int32 MineralCount = S.Minerals.Num();
	Ar << MineralCount;
	for (const FGeoMineralInfo& M : S.Minerals)
	{
		FString Name = M.Name;
		FColor Color = M.Color;
		Ar << Name;
		Ar.Serialize(&Color, sizeof(FColor));
	}
	int32 OverlayCount = S.MineralOverlays.Num();
	Ar << OverlayCount;
	for (const FGeoMineralOverlay& O : S.MineralOverlays)
	{
		Ar << const_cast<uint8&>(O.MineralId) << const_cast<uint8&>(O.Grade) << const_cast<float&>(O.DepthVoxels);
		WriteField(Ar, O.Mask);
	}

	WriteField(Ar, S.StrataThickness);
	WriteField(Ar, S.StrataType);
	WriteField(Ar, S.FaultMask);
	WriteField(Ar, S.FoldField);
	WriteField(Ar, S.MetamorphicGrade);
	WriteField(Ar, S.WeatheringDepth);
	WriteField(Ar, S.SoilType);
	Ar << const_cast<float&>(S.MarbleThreshold);
	return Ar.IsError() == false;
}

TSharedPtr<FPlanetSnapshot> DeserializePlanetSnapshot(const TArray<uint8>& Blob)
{
	if (Blob.Num() < 8) { return nullptr; }
	FMemoryReader Ar(Blob, /*bIsPersistent=*/ true);
	uint32 Magic = 0;
	int32 Version = 0;
	Ar << Magic << Version;
	if (Magic != GeoBakeMagic || Version != GeoBakeCacheFormat::Version) { return nullptr; }

	TSharedPtr<FPlanetSnapshot> S = MakeShared<FPlanetSnapshot>();
	Ar << S->R0 << S->DensityScale;
	ReadProfile(Ar, S->Radial);
	S->MohoWobble.Serialize(Ar);

	TArray<FGeoPrimitive> Prims;
	RawRead(Ar, Prims);
	S->Primitives.Build(MoveTemp(Prims));

	S->Elevation.Serialize(Ar);
	Ar << S->CrustSplitElevation << S->PrimitiveDepthMaxRatio;
	for (int32 i = 0; i < 3; ++i) { Ar << S->AlterBands[i]; }
	Ar << S->MinSurfaceRadius << S->MaxSurfaceRadius;
	Ar << S->ContentHash << S->DisabledStages;

	int32 DebugCount = 0;
	Ar << DebugCount;
	for (int32 i = 0; i < DebugCount; ++i)
	{
		FName Name;
		Ar << Name;
		FGeoField2D Field;
		Field.Serialize(Ar);
		S->DebugFields.Add(Name, MoveTemp(Field));
	}

	RawRead(Ar, S->Volcanoes);
	int32 MineralCount = 0;
	Ar << MineralCount;
	S->Minerals.SetNum(MineralCount);
	for (FGeoMineralInfo& M : S->Minerals)
	{
		Ar << M.Name;
		Ar.Serialize(&M.Color, sizeof(FColor));
	}
	int32 OverlayCount = 0;
	Ar << OverlayCount;
	S->MineralOverlays.SetNum(OverlayCount);
	for (FGeoMineralOverlay& O : S->MineralOverlays)
	{
		Ar << O.MineralId << O.Grade << O.DepthVoxels;
		O.Mask.Serialize(Ar);
	}

	S->StrataThickness.Serialize(Ar);
	S->StrataType.Serialize(Ar);
	S->FaultMask.Serialize(Ar);
	S->FoldField.Serialize(Ar);
	S->MetamorphicGrade.Serialize(Ar);
	S->WeatheringDepth.Serialize(Ar);
	S->SoilType.Serialize(Ar);
	Ar << S->MarbleThreshold;
	if (Ar.IsError()) { return nullptr; }
	return S;
}
