// 编解码是全项目唯一的通道约定，shader 与仪器都按它解码；往返不一致就是「看到的」和「挖到的」分叉。
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "GeoMaterialCodec.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoMaterialCodecTest, "StellarEra.Planet.Geo.MaterialCodec",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoMaterialCodecTest::RunTest(const FString& Parameters)
{
	bool bRoundTrip = true;
	for (int32 L = 0; L < 256; ++L)
	for (int32 G = 0; G < 256; ++G)
	{
		FGeoSample S;
		S.Lithology = uint8(L);
		S.Mineral = uint8((L * 7 + 3) & 255);
		S.Grade = uint8(G);
		S.Clue = uint8(L & 15);
		S.ClueStrength = uint8((G >> 4) & 15);
		S.SecondaryMineral = uint8((G * 13 + 5) & 255);
		S.SecondaryGrade = uint8((L + G * 3) & 255);
		const FGeoSample D = GeoMaterialCodec::Decode(GeoMaterialCodec::Encode(S));
		bRoundTrip &= D.Lithology == S.Lithology && D.Mineral == S.Mineral && D.Grade == S.Grade
			&& D.Clue == S.Clue && D.ClueStrength == S.ClueStrength
			&& D.SecondaryMineral == S.SecondaryMineral && D.SecondaryGrade == S.SecondaryGrade;
	}
	TestTrue(TEXT("256×256 往返一致"), bRoundTrip);

	FGeoSample Over;
	Over.Clue = 200; Over.ClueStrength = 99;
	const FGeoSample D = GeoMaterialCodec::Decode(GeoMaterialCodec::Encode(Over));
	TestEqual(TEXT("线索类型钳到 15"), D.Clue, uint8(15));
	TestEqual(TEXT("线索强度钳到 15"), D.ClueStrength, uint8(15));
	TestEqual(TEXT("R 通道就是岩性"), GeoMaterialCodec::Encode(FGeoSample{ 7 }).R, uint8(7));

	FGeoSample Distinct;
	Distinct.Lithology = 0x11;
	Distinct.Mineral = 0x22;
	Distinct.Grade = 0x33;
	Distinct.Clue = 5;
	Distinct.ClueStrength = 10;
	Distinct.SecondaryMineral = 0x66;
	Distinct.SecondaryGrade = 0x77;
	const FGeoPackedMaterial P = GeoMaterialCodec::Encode(Distinct);
	TestEqual(TEXT("R=岩性"), P.R, uint8(0x11));
	TestEqual(TEXT("G=矿物"), P.G, uint8(0x22));
	TestEqual(TEXT("B=品位"), P.B, uint8(0x33));
	TestEqual(TEXT("A=(线索类型<<4)|线索强度"), P.A, uint8(0x5A));
	TestEqual(TEXT("U0=次生矿物"), P.U0, uint8(0x66));
	TestEqual(TEXT("V0=次生品位"), P.V0, uint8(0x77));
	TestEqual(TEXT("U1 保留为 0"), P.U1, uint8(0));
	TestEqual(TEXT("V1 保留为 0"), P.V1, uint8(0));
	return true;
}

#endif
