// 快照序列化往返：内容哈希、场采样必须与序列化前一致；版本不符要判失效。
#include "Misc/AutomationTest.h"
#include "PlanetSnapshot.h"
#include "PlanetSnapshotSerialize.h"

#if WITH_EDITOR

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetSnapshotSerializeRoundTripTest,
	"StellarEra.Planet.Geo.Cache.RoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetSnapshotSerializeRoundTripTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FPlanetSnapshot> S = MakeShared<FPlanetSnapshot>();
	S->R0 = 40000.0;
	S->DensityScale = 1.f;
	S->MinSurfaceRadius = 39000.0;
	S->MaxSurfaceRadius = 41000.0;
	S->Elevation.Init(16);
	S->Elevation.Fill([](const FVector3f& Dir) { return Dir.Z * 100.f; });
	S->DisabledStages = 0x5u;
	S->Volcanoes.Add(FGeoVolcano());
	S->FinalizeHash();
	const uint64 Hash = S->ContentHash;

	TArray<uint8> Blob;
	if (!TestTrue(TEXT("序列化成功"), SerializePlanetSnapshot(*S, Blob))) { return false; }
	TestTrue(TEXT("blob 非空"), Blob.Num() > 0);

	TSharedPtr<FPlanetSnapshot> D = DeserializePlanetSnapshot(Blob);
	if (!TestNotNull(TEXT("反序列化成功"), D.Get())) { return false; }
	TestEqual(TEXT("内容哈希一致"), D->ContentHash, Hash);
	TestEqual(TEXT("高程场分辨率一致"), D->Elevation.Res, S->Elevation.Res);
	TestEqual(TEXT("禁用阶段一致"), D->DisabledStages, S->DisabledStages);
	TestEqual(TEXT("火山数量一致"), D->Volcanoes.Num(), S->Volcanoes.Num());
	TestEqual(TEXT("场采样一致"), D->Elevation.Sample(FVector3f(0.3f, 0.4f, 0.866f)), S->Elevation.Sample(FVector3f(0.3f, 0.4f, 0.866f)));

	// 版本字节不符必须返回空（缓存失效路径）
	TArray<uint8> Bad = Blob;
	if (Bad.Num() > 4) { Bad[4] = 0x7F; }
	TestNull(TEXT("版本不符返回空"), DeserializePlanetSnapshot(Bad).Get());
	return true;
}

#endif
