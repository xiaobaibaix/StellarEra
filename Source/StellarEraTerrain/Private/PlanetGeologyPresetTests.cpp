// Copyright Voxel Plugin SAS. All Rights Reserved.
// 地质预设资产的默认值与修订号。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
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

	// 包名对但对象名漂移，PostLoad 的 TryLoad 一样会失败 —— 所以连对象名一起钉。
	TestEqual(TEXT("对象名与包名都一致"),
		AVoxelPlanetActor::MakeLegacyGeologyAssetPath(TEXT("earth")).ToString(),
		FString(TEXT("/Game/Geology/Planets/DA_Geology_Earth.DA_Geology_Earth")));

	// 迁移目标必须真的有资产：路径拼得再一致，资产不存在旧关卡还是没矿。
	TestTrue(TEXT("资产真的在磁盘上"), FPackageName::DoesPackageExist(Expected));

	return true;
}

// 迁移链的端到端验证：旧关卡只存 GeologyPreset 字符串，PostLoad 把它换成资产并清空旧字段。
// 起点是 CDO 默认值 "earth" —— 旧 Actor 从未序列化过该字段，删掉默认值迁移条件恒假，
// 老关卡会永久丢矿（Task 3 审查抓到过一次）。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetGeologyLegacyMigrationTest,
	"StellarEra.Planet.Geology.LegacyMigration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetGeologyLegacyMigrationTest::RunTest(const FString& Parameters)
{
	// 1) 默认值不许被删。
	TestEqual(TEXT("CDO 默认迁移名是 earth"),
		GetDefault<AVoxelPlanetActor>()->GeologyPreset, FString(TEXT("earth")));

	// 2) 真跑一次迁移。对象不放进世界：PostLoad 的迁移分支只读旧字段并 TryLoad；
	//    基类 PostLoad 的编辑器 Actor 元素、DataLayer 修复都以 GetLevel() 为守卫，
	//    GetLinkerUEVersion / GetLinkerCustomVersion 在无 Linker 时返回当前版本。
	AVoxelPlanetActor* Actor = NewObject<AVoxelPlanetActor>();
	if (!TestNotNull(TEXT("新建一个不入世界的行星 Actor"), Actor))
	{
		return false;
	}

	Actor->GeologyAsset = nullptr;
	Actor->GeologyPreset = TEXT("earth");
	Actor->PostLoad();

	UPlanetGeologyPreset* Asset = Actor->GetGeologyPresetAsset();
	if (!TestNotNull(TEXT("迁移后 GeologyAsset 指向资产"), Asset))
	{
		return false;
	}

	TestEqual(TEXT("迁移目标是 DA_Geology_Earth"),
		Asset->GetPackage()->GetName(), FString(TEXT("/Game/Geology/Planets/DA_Geology_Earth")));
	TestTrue(TEXT("迁移成功后旧字段被清空"), Actor->GeologyPreset.IsEmpty());
	return true;
}

// 这份资产是打包内容的一半（另一半是行星/BP 上的硬引用），值错了游戏里就是矿囊长错。
// 期望值从建资产命令的实现（PlanetGeologyPreset.cpp 的 FillEarthPreset）取数。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetGeologyEarthValuesTest,
	"StellarEra.Planet.Geology.EarthPresetValues",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetGeologyEarthValuesTest::RunTest(const FString& Parameters)
{
	// 加载路径也走 MakeAssetPath：测试里再拼一份字面量就等于又开了个漂移点。
	const FString AssetPath = UPlanetGeologyPreset::MakeAssetPath(TEXT("earth")).ToString();
	UPlanetGeologyPreset* Preset = LoadObject<UPlanetGeologyPreset>(nullptr, *AssetPath);
	if (!TestNotNull(*FString::Printf(TEXT("能加载 %s"), *AssetPath), Preset))
	{
		return false;
	}

	// 元素顺序 = 采样顺序 = 图例顺序 = 矿体组件下标，四者共用；错位会让颜色与元素对不上。
	const TCHAR* ExpectedIds[] = {
		TEXT("silicon"), TEXT("iron"), TEXT("water"),
		TEXT("copper"), TEXT("titanium"), TEXT("uranium"),
	};
	const int32 ExpectedPodCounts[] = { 20, 20, 16, 16, 12, 10 };
	const float ExpectedPodRadii[] = { 0.050f, 0.050f, 0.048f, 0.045f, 0.042f, 0.038f };
	const float ExpectedAbundances[] = { 0.60f, 0.75f, 0.20f, 0.45f, 0.15f, 0.10f };
	const int32 ExpectedSeeds[] = { 303, 101, 606, 202, 404, 505 };
	// Seed 改了矿囊位置、Color 改了图例与矿体颜色，两者都是「资产是内容的一半」的一部分。
	const FLinearColor ExpectedColors[] = {
		FLinearColor(0.62f, 0.58f, 0.50f),
		FLinearColor(0.72f, 0.42f, 0.36f),
		FLinearColor(0.20f, 0.45f, 0.90f),
		FLinearColor(0.85f, 0.55f, 0.25f),
		FLinearColor(0.70f, 0.72f, 0.78f),
		FLinearColor(0.45f, 0.85f, 0.35f),
	};
	const int32 ElementCount = UE_ARRAY_COUNT(ExpectedIds);

	TestEqual(TEXT("6 个元素"), Preset->Elements.Num(), ElementCount);
	if (Preset->Elements.Num() != ElementCount)
	{
		return false;
	}

	for (int32 Index = 0; Index < ElementCount; ++Index)
	{
		const FPlanetGeologyElementParams& Element = Preset->Elements[Index];
		const FString Label = ExpectedIds[Index];

		TestEqual(*FString::Printf(TEXT("%s 的 Id 顺序"), *Label),
			Element.Id.ToString(), Label);
		TestEqual(*FString::Printf(TEXT("%s 的 PodCount"), *Label),
			Element.PodCount, ExpectedPodCounts[Index]);
		TestEqual(*FString::Printf(TEXT("%s 的 PodRadius"), *Label),
			Element.PodRadius, ExpectedPodRadii[Index]);
		TestEqual(*FString::Printf(TEXT("%s 的 Abundance"), *Label),
			Element.Abundance, ExpectedAbundances[Index]);
		TestEqual(*FString::Printf(TEXT("%s 的 Seed"), *Label),
			Element.Seed, ExpectedSeeds[Index]);
		TestEqual(*FString::Printf(TEXT("%s 的 Color"), *Label),
			Element.Color, ExpectedColors[Index]);
	}

	TestEqual(TEXT("ColorThreshold"), Preset->Appearance.ColorThreshold, 0.05f);
	TestEqual(TEXT("FadeRange"), Preset->Appearance.FadeRange, 0.12f);
	TestEqual(TEXT("PurityForFullColor"), Preset->Appearance.PurityForFullColor, 0.45f);
	TestEqual(TEXT("GeometryAmplitude"), Preset->GeometryAmplitude, 1.5f);

	return true;
}

#endif
