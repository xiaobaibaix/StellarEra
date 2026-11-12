// 剖面决定「挖到多深是什么」：层序必须单调，起伏只能挪界面、不能把两层顺序翻过来。
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "GeoLithology.h"
#include "GeoProfile.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoProfileTest, "StellarEra.Planet.Geo.Profile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoProfileTest::RunTest(const FString& Parameters)
{
	FGeoProfile P;
	P.Layers = {
		{ 0.25f, GeoLithology::Crust,       0.01f },
		{ 0.38f, GeoLithology::UpperMantle, 0.001f },
		{ 0.42f, GeoLithology::Transition,  0.001f },
		{ 0.55f, GeoLithology::LowerMantle, 0.005f },
		{ 0.80f, GeoLithology::OuterCore,   0.001f },
		{ 1.00f, GeoLithology::InnerCore,   0.f },
	};
	TestEqual(TEXT("地表以上按地壳"), P.LithologyAt(-0.1f, 0.f), GeoLithology::Crust);
	TestEqual(TEXT("0.10 地壳"), P.LithologyAt(0.10f, 0.f), GeoLithology::Crust);
	TestEqual(TEXT("0.30 上地幔"), P.LithologyAt(0.30f, 0.f), GeoLithology::UpperMantle);
	TestEqual(TEXT("0.40 过渡带"), P.LithologyAt(0.40f, 0.f), GeoLithology::Transition);
	TestEqual(TEXT("0.50 下地幔"), P.LithologyAt(0.50f, 0.f), GeoLithology::LowerMantle);
	TestEqual(TEXT("0.70 外核"), P.LithologyAt(0.70f, 0.f), GeoLithology::OuterCore);
	TestEqual(TEXT("0.95 内核"), P.LithologyAt(0.95f, 0.f), GeoLithology::InnerCore);
	TestEqual(TEXT("球心 内核"), P.LithologyAt(1.0f, 0.f), GeoLithology::InnerCore);
	TestEqual(TEXT("莫霍面上抬：0.245 在 wobble=-1 时已是上地幔"), P.LithologyAt(0.245f, -1.f), GeoLithology::UpperMantle);
	TestEqual(TEXT("莫霍面下压：0.255 在 wobble=+1 时仍是地壳"), P.LithologyAt(0.255f, 1.f), GeoLithology::Crust);

	bool bMonotonic = true;
	for (const float W : { -1.f, 0.f, 1.f })
	{
		uint8 Prev = 0;
		for (int32 i = 0; i <= 1000; ++i)
		{
			const uint8 L = P.LithologyAt(float(i) / 1000.f, W);
			bMonotonic &= (L >= Prev);
			Prev = L;
		}
	}
	TestTrue(TEXT("任意 wobble 下层序单调"), bMonotonic);

	FGeoProfile Q = P;
	TestTrue(TEXT("同剖面同哈希"), P.Hash() == Q.Hash());
	Q.Layers[0].BottomDepthRatio = 0.26f;
	TestTrue(TEXT("改界面哈希变"), P.Hash() != Q.Hash());
	TestTrue(TEXT("各圈层调试色互不相同"), GeoLithology::DebugColor(GeoLithology::Crust) != GeoLithology::DebugColor(GeoLithology::UpperMantle));
	return true;
}

#endif
