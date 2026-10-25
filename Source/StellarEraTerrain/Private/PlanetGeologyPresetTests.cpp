// Copyright Voxel Plugin SAS. All Rights Reserved.
// 地质预设资产的默认值与修订号。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PlanetGeologyPreset.h"
#include "UObject/SoftObjectPath.h"
#include "VoxelPlanetActor.h"

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
	const FPlanetGeologyElementParams Defaults;
	TestEqual(TEXT("默认 Abundance 1.0"), Defaults.Abundance, 1.0f);
	TestEqual(TEXT("默认 PodCount 16"), Defaults.PodCount, 16);
	TestEqual(TEXT("默认 PodRadius 0.05"), Defaults.PodRadius, 0.05f);
	TestEqual(TEXT("默认 PodRadiusJitter 0.5"), Defaults.PodRadiusJitter, 0.5f);
	TestEqual(TEXT("默认 PodDepthBias 0"), Defaults.PodDepthBias, 0.0f);
	TestEqual(TEXT("默认 PodDepthCenter 0.25"), Defaults.PodDepthCenter, 0.25f);
	TestEqual(TEXT("默认 PodDepthSpread 0.15"), Defaults.PodDepthSpread, 0.15f);
	TestEqual(TEXT("默认 ShapeDetail 3"), Defaults.ShapeDetail, 3.0f);
	TestEqual(TEXT("默认 Octaves 2"), Defaults.Octaves, 2);
	TestEqual(TEXT("默认 Lacunarity 2.0"), Defaults.Lacunarity, 2.0f);
	TestEqual(TEXT("默认 Gain 0.5"), Defaults.Gain, 0.5f);
	TestEqual(TEXT("默认 VeinThreshold 0.5"), Defaults.VeinThreshold, 0.5f);

	return true;
}

// PostEditChangeProperty / FPropertyChangedEvent 只在编辑器目标存在，
// 而 WITH_DEV_AUTOMATION_TESTS 在 Game 目标同样为 1，故整块再包一层 WITH_EDITOR。
#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetGeologyPresetRevisionTest,
	"StellarEra.Planet.Geology.PresetRevision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetGeologyPresetRevisionTest::RunTest(const FString& Parameters)
{
	UPlanetGeologyPreset* Preset = NewObject<UPlanetGeologyPreset>();
	const int32 Before = Preset->GetRevision();

	FPropertyChangedEvent Event(nullptr);
	Preset->PostEditChangeProperty(Event);

	TestTrue(*FString::Printf(TEXT("改属性后修订号自增（%d → %d）"),
		Before, Preset->GetRevision()), Preset->GetRevision() > Before);
	return true;
}
#endif

// 迁移路径的锚点：PostLoad 拿旧字段去 MakeLegacyGeologyAssetPath 拼路径，而
// DA_Geology_Earth 这个资产是建资产命令用字符串另外拼出来的。两边一旦漂移，
// 旧关卡就永久没矿，且症状只是「行星没矿」。这条测试把两边钉在同一路径上。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetGeologyLegacyPathTest,
	"StellarEra.Planet.Geology.LegacyAssetPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetGeologyLegacyPathTest::RunTest(const FString& Parameters)
{
	const FString Expected = TEXT("/Game/Geology/Planets/DA_Geology_Earth");

	TestEqual(TEXT("earth 迁移到 DA_Geology_Earth"),
		AVoxelPlanetActor::MakeLegacyGeologyAssetPath(TEXT("earth")).GetLongPackageName(),
		Expected);
	TestEqual(TEXT("首尾空格被去掉"),
		AVoxelPlanetActor::MakeLegacyGeologyAssetPath(TEXT("  earth  ")).GetLongPackageName(),
		Expected);
	TestEqual(TEXT("首字母大小写不影响"),
		AVoxelPlanetActor::MakeLegacyGeologyAssetPath(TEXT("Earth")).GetLongPackageName(),
		Expected);
	TestTrue(TEXT("空名返回空路径"),
		AVoxelPlanetActor::MakeLegacyGeologyAssetPath(FString()).IsNull());
	TestEqual(TEXT("其它名字按同一规则拼"),
		AVoxelPlanetActor::MakeLegacyGeologyAssetPath(TEXT("mars")).GetLongPackageName(),
		FString(TEXT("/Game/Geology/Planets/DA_Geology_Mars")));

	return true;
}

#endif
