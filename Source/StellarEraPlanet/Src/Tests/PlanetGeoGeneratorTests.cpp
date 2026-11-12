// 适配器必须是快照的薄转发：密度、值域、材质与求值核逐点一致，否则体素世界与仪器查询分叉。
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PlanetGeoGenerator.h"
#include "PlanetBakePipeline.h"
#include "PlanetShapeEval.h"
#include "PlanetMaterialEval.h"
#include "GeoMaterialCodec.h"
#include "GeoRng.h"
#include "VoxelIntBox.h"
#include "VoxelItemStack.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoGeneratorTest, "StellarEra.Planet.Geo.Generator",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoGeneratorTest::RunTest(const FString& Parameters)
{
	FPlanetRecipeConfig Config;
	Config.Radius = 2000.0;
	Config.FaceRes = 128;
	FPlanetBakeReport Report;
	const TSharedPtr<const FPlanetSnapshot> Snap = FPlanetBakePipeline::MakeDefault().Bake(Config, Report);
	if (!TestTrue(TEXT("烘焙成功"), Snap.IsValid())) { return false; }

	UPlanetGeoGenerator* Gen = NewObject<UPlanetGeoGenerator>();
	Gen->bDebugLithologyColor = false;
	Gen->SetSnapshot(Snap);
	const TVoxelSharedRef<FVoxelGeneratorInstance> Base = Gen->GetInstance();
	const FPlanetGeoGeneratorInstance& Inst = static_cast<const FPlanetGeoGeneratorInstance&>(*Base);
	const FVoxelItemStack& Items = FVoxelItemStack::Empty;

	FGeoRng Rng = FGeoRng::Derive(3, 0, 0);
	bool bValue = true, bMaterial = true, bRange = true;
	for (int32 i = 0; i < 5000; ++i)
	{
		const FVector3d P(Rng.Range(-2200.f, 2200.f), Rng.Range(-2200.f, 2200.f), Rng.Range(-2200.f, 2200.f));
		bValue &= float(Inst.GetValueImpl(v_flt(P.X), v_flt(P.Y), v_flt(P.Z), 0, Items)) == PlanetShapeEval::Value(*Snap, P);
		const FVoxelMaterial M = Inst.GetMaterialImpl(v_flt(P.X), v_flt(P.Y), v_flt(P.Z), 0, Items);
		const FGeoPackedMaterial E = GeoMaterialCodec::Encode(PlanetMaterialEval::Sample(*Snap, P));
		bMaterial &= M.GetR() == E.R && M.GetG() == E.G && M.GetB() == E.B && M.GetA() == E.A && M.GetU0() == E.U0 && M.GetV0() == E.V0;

		const FIntVector C(FMath::FloorToInt(P.X), FMath::FloorToInt(P.Y), FMath::FloorToInt(P.Z));
		const int32 H = 1 + int32(Rng.NextU64() % 32);
		const FVoxelIntBox Box(C - FIntVector(H), C + FIntVector(H));
		const TVoxelRange<v_flt> R = Inst.GetValueRangeImpl(Box, 0, Items);
		const FVector3d Q(FMath::Lerp(double(Box.Min.X), double(Box.Max.X), double(Rng.NextFloat())),
			FMath::Lerp(double(Box.Min.Y), double(Box.Max.Y), double(Rng.NextFloat())),
			FMath::Lerp(double(Box.Min.Z), double(Box.Max.Z), double(Rng.NextFloat())));
		const v_flt V = Inst.GetValueImpl(v_flt(Q.X), v_flt(Q.Y), v_flt(Q.Z), 0, Items);
		bRange &= (V >= R.Min && V <= R.Max);
	}
	TestTrue(TEXT("密度 == 求值核"), bValue);
	TestTrue(TEXT("材质 == 编解码器(求值核)"), bMaterial);
	TestTrue(TEXT("值域覆盖盒内采样"), bRange);
	TestTrue(TEXT("上方向 = 径向"), Inst.GetUpVector(0.0, 0.0, 1000.0).Equals(FVector::UpVector, 1e-6));

	UPlanetGeoGenerator* Empty = NewObject<UPlanetGeoGenerator>();
	AddExpectedError(TEXT("没有快照"), EAutomationExpectedErrorFlags::Contains, 1);
	const TVoxelSharedRef<FVoxelGeneratorInstance> EmptyBase = Empty->GetInstance();
	const FPlanetGeoGeneratorInstance& EI = static_cast<const FPlanetGeoGeneratorInstance&>(*EmptyBase);
	TestTrue(TEXT("无快照 → 空气"), EI.GetValueImpl(0, 0, 0, 0, Items) > 0);
	return true;
}

#endif
