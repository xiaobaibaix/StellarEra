// Copyright Voxel Plugin SAS. All Rights Reserved.
// 地质预设资产的默认值与修订号。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PlanetGeologyPreset.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetGeologyPresetDefaultsTest,
	"StellarEra.Planet.Geology.PresetDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetGeologyPresetDefaultsTest::RunTest(const FString& Parameters)
{
	const UPlanetGeologyPreset* Preset = GetDefault<UPlanetGeologyPreset>();
	TestNotNull(TEXT("默认对象存在"), Preset);
	if (!Preset)
	{
		return false;
	}

	TestEqual(TEXT("默认没有元素"), Preset->Elements.Num(), 0);
	TestEqual(TEXT("默认几何幅度 1.5"), Preset->GeometryAmplitude, 1.5f);
	TestEqual(TEXT("默认阈值 0.22"), Preset->Appearance.ColorThreshold, 0.22f);
	TestEqual(TEXT("默认淡出 0.15"), Preset->Appearance.FadeRange, 0.15f);
	TestEqual(TEXT("默认纯度 0.7"), Preset->Appearance.PurityForFullColor, 0.7f);

	// USTRUCT 只生成 StaticStruct()，GetDefault<T>() 只支持 UObject，这里直接默认构造。
	const FPlanetGeologyElementParams DefaultInstance;
	const FPlanetGeologyElementParams* Defaults = &DefaultInstance;
	TestNotNull(TEXT("元素参数默认值存在"), Defaults);
	if (Defaults)
	{
		TestEqual(TEXT("默认 PodCount 16"), Defaults->PodCount, 16);
		TestEqual(TEXT("默认 PodRadius 0.05"), Defaults->PodRadius, 0.05f);
		TestEqual(TEXT("默认 PodRadiusJitter 0.5"), Defaults->PodRadiusJitter, 0.5f);
		TestEqual(TEXT("默认 PodDepthBias 0"), Defaults->PodDepthBias, 0.0f);
		TestEqual(TEXT("默认 ShapeDetail 3"), Defaults->ShapeDetail, 3.0f);
		TestEqual(TEXT("默认 Octaves 2"), Defaults->Octaves, 2);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetGeologyPresetRevisionTest,
	"StellarEra.Planet.Geology.PresetRevision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetGeologyPresetRevisionTest::RunTest(const FString& Parameters)
{
	UPlanetGeologyPreset* Preset = NewObject<UPlanetGeologyPreset>();
	const uint32 Before = Preset->GetRevision();

	FPropertyChangedEvent Event(nullptr);
	Preset->PostEditChangeProperty(Event);

	TestTrue(*FString::Printf(TEXT("改属性后修订号自增（%u → %u）"),
		Before, Preset->GetRevision()), Preset->GetRevision() > Before);
	return true;
}

#endif
