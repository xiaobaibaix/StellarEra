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
		S.Lithology = uint8(L); S.Mineral = uint8(255 - L); S.Grade = uint8(G);
		S.Clue = uint8(L & 15); S.ClueStrength = uint8(G & 15);
		S.SecondaryMineral = uint8(G); S.SecondaryGrade = uint8(L);
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
	return true;
}

#endif
