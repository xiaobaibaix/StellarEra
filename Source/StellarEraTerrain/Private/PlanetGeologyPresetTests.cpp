// Copyright Voxel Plugin SAS. All Rights Reserved.
// 地质预设资产的默认值与修订号。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "PlanetGeologyDisseminatedDistribution.h"
#include "PlanetGeologyField.h"
#include "PlanetGeologyLensDistribution.h"
#include "PlanetGeologyPreset.h"
#include "PlanetGeologySpherePodsDistribution.h"
#include "PlanetGeologyVeinDistribution.h"
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
	TestEqual(TEXT("默认没有生成器"), Defaults.Distributions.Num(), 0);

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetGeologyPresetNestedEditTest,
	"StellarEra.Planet.Geology.PresetRevisionNestedEdit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetGeologyPresetNestedEditTest::RunTest(const FString& Parameters)
{
	UPlanetGeologyPreset* Preset = NewObject<UPlanetGeologyPreset>();
	FPlanetGeologyElementParams& Element = Preset->Elements.AddDefaulted_GetRef();
	Element.Id = TEXT("iron");

	UPlanetGeologyLensDistribution* Lens = NewObject<UPlanetGeologyLensDistribution>(Preset);
	Element.Distributions.Add(Lens);

	int32 Broadcasts = 0;
	Preset->OnPresetChanged.AddLambda(
		[&Broadcasts](UPlanetGeologyPreset*) { ++Broadcasts; });
	const int32 Before = Preset->GetRevision();

	FPropertyChangedEvent Event(UPlanetGeologyLensDistribution::StaticClass()
		->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UPlanetGeologyLensDistribution, SeamCount)));
	Lens->PostEditChangeProperty(Event);

	TestTrue(*FString::Printf(TEXT("嵌套编辑后修订号自增（%d → %d）"),
		Before, Preset->GetRevision()), Preset->GetRevision() > Before);
	TestEqual(TEXT("广播恰好一次"), Broadcasts, 1);
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
	const FString AssetPath = UPlanetGeologyPreset::MakeAssetPath(TEXT("earth")).ToString();
	UPlanetGeologyPreset* Preset = LoadObject<UPlanetGeologyPreset>(nullptr, *AssetPath);
	if (!TestNotNull(*FString::Printf(TEXT("能加载 %s"), *AssetPath), Preset))
	{
		return false;
	}

	const TCHAR* ExpectedIds[] = {
		TEXT("silicon"), TEXT("iron"), TEXT("water"),
		TEXT("copper"), TEXT("titanium"), TEXT("uranium"),
	};
	const float ExpectedAbundances[] = { 0.60f, 0.75f, 0.20f, 0.45f, 0.15f, 0.10f };
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
		TestEqual(*FString::Printf(TEXT("%s 的 Abundance"), *Label),
			Element.Abundance, ExpectedAbundances[Index]);
		TestEqual(*FString::Printf(TEXT("%s 的 Color"), *Label),
			Element.Color, ExpectedColors[Index]);
		// 遗留字段必须清零：留着就等于资产里有两套数据。
		TestEqual(*FString::Printf(TEXT("%s 的遗留 PodCount"), *Label), Element.PodCount, 0);
		TestEqual(*FString::Printf(TEXT("%s 一个生成器"), *Label),
			Element.Distributions.Num(), 1);
		if (Element.Distributions.Num() != 1)
		{
			continue;
		}
		TestNotNull(*FString::Printf(TEXT("%s 的生成器非空"), *Label),
			Element.Distributions[0].Get());
	}

	// 硅 / 水：片层
	{
		const auto* Lens = Cast<UPlanetGeologyLensDistribution>(
			Preset->Elements[0].Distributions[0]);
		if (TestNotNull(TEXT("硅是片层"), Lens))
		{
			TestEqual(TEXT("硅 16 片"), Lens->SeamCount, 16);
			TestEqual(TEXT("硅 A 0.14"), Lens->SeamLength, 0.14f);
			TestEqual(TEXT("硅 B 0.07"), Lens->SeamWidth, 0.07f);
			TestEqual(TEXT("硅 C 0.008"), Lens->SeamThickness, 0.008f);
			TestEqual(TEXT("硅深度中心 0.10"), Lens->DepthCenter, 0.10f);
			TestEqual(TEXT("硅深度偏置 0.5"), Lens->DepthBias, 0.5f);
			TestEqual(TEXT("硅 VeinThreshold 0.45"), Lens->VeinThreshold, 0.45f);
		}
		const auto* WaterLens = Cast<UPlanetGeologyLensDistribution>(
			Preset->Elements[2].Distributions[0]);
		if (TestNotNull(TEXT("水是片层"), WaterLens))
		{
			TestEqual(TEXT("水 14 片"), WaterLens->SeamCount, 14);
			TestEqual(TEXT("水 A 0.18"), WaterLens->SeamLength, 0.18f);
			TestEqual(TEXT("水 B 0.10"), WaterLens->SeamWidth, 0.10f);
			TestEqual(TEXT("水 C 0.010"), WaterLens->SeamThickness, 0.010f);
			TestEqual(TEXT("水深度中心 0.18"), WaterLens->DepthCenter, 0.18f);
			TestEqual(TEXT("水 VeinThreshold 0.40"), WaterLens->VeinThreshold, 0.40f);
		}
	}

	// 铁 / 铜：脉状
	{
		const auto* IronVein = Cast<UPlanetGeologyVeinDistribution>(
			Preset->Elements[1].Distributions[0]);
		if (TestNotNull(TEXT("铁是脉状"), IronVein))
		{
			TestEqual(TEXT("铁 5 条"), IronVein->VeinCount, 5);
			TestEqual(TEXT("铁长 0.40"), IronVein->VeinLength, 0.40f);
			TestEqual(TEXT("铁半径 0.009"), IronVein->VeinRadius, 0.009f);
			TestEqual(TEXT("铁 1 分叉"), IronVein->BranchCount, 1);
			TestEqual(TEXT("铁 VeinThreshold 0.50"), IronVein->VeinThreshold, 0.50f);
		}
		const auto* CopperVein = Cast<UPlanetGeologyVeinDistribution>(
			Preset->Elements[3].Distributions[0]);
		if (TestNotNull(TEXT("铜是脉状"), CopperVein))
		{
			TestEqual(TEXT("铜 3 条"), CopperVein->VeinCount, 3);
			TestEqual(TEXT("铜长 0.28"), CopperVein->VeinLength, 0.28f);
			TestEqual(TEXT("铜半径 0.007"), CopperVein->VeinRadius, 0.007f);
			TestEqual(TEXT("铜无分叉"), CopperVein->BranchCount, 0);
			TestEqual(TEXT("铜 VeinThreshold 0.55"), CopperVein->VeinThreshold, 0.55f);
		}
	}

	// 钛 / 铀：浸染
	{
		const auto* Titanium = Cast<UPlanetGeologyDisseminatedDistribution>(
			Preset->Elements[4].Distributions[0]);
		if (TestNotNull(TEXT("钛是浸染"), Titanium))
		{
			TestEqual(TEXT("钛 320 粒"), Titanium->GrainCount, 320);
			TestEqual(TEXT("钛粒径 0.0025"), Titanium->GrainRadius, 0.0025f);
			TestEqual(TEXT("钛 VeinThreshold 0.60"), Titanium->VeinThreshold, 0.60f);
		}
		const auto* Uranium = Cast<UPlanetGeologyDisseminatedDistribution>(
			Preset->Elements[5].Distributions[0]);
		if (TestNotNull(TEXT("铀是浸染"), Uranium))
		{
			TestEqual(TEXT("铀 140 粒"), Uranium->GrainCount, 140);
			TestEqual(TEXT("铀粒径 0.0015"), Uranium->GrainRadius, 0.0015f);
			TestEqual(TEXT("铀深度中心 0.35"), Uranium->DepthCenter, 0.35f);
			TestEqual(TEXT("铀深度偏置 0.6"), Uranium->DepthBias, 0.6f);
			TestEqual(TEXT("铀 VeinThreshold 0.65"), Uranium->VeinThreshold, 0.65f);
		}
	}

	TestEqual(TEXT("ColorThreshold"), Preset->Appearance.ColorThreshold, 0.05f);
	TestEqual(TEXT("FadeRange"), Preset->Appearance.FadeRange, 0.12f);
	TestEqual(TEXT("PurityForFullColor"), Preset->Appearance.PurityForFullColor, 0.45f);
	TestEqual(TEXT("GeometryAmplitude"), Preset->GeometryAmplitude, 1.5f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetGeologyLegacyMigrationFidelityTest,
	"StellarEra.Planet.Geology.LegacyMigrationFidelity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetGeologyLegacyMigrationFidelityTest::RunTest(const FString& Parameters)
{
	// 旧资产形态：只有遗留字段。
	FPlanetGeologyElementParams Legacy;
	Legacy.Id = TEXT("iron");
	Legacy.DisplayName = TEXT("铁");
	Legacy.Abundance = 0.75f;
	Legacy.PodCount = 20;
	Legacy.PodRadius = 0.05f;
	Legacy.Seed = 0; // 派生路径最能暴露「新旧种子公式不同」
	Legacy.VeinThreshold = 0.5f;

	UPlanetGeologyPreset* LegacyPreset = NewObject<UPlanetGeologyPreset>();
	LegacyPreset->Elements.Add(Legacy);
	const auto LegacyField = FPlanetGeologyField::BuildFromPreset(*LegacyPreset, 40000.0);

	// 迁移后的资产形态：显式囊状生成器。
	UPlanetGeologyPreset* MigratedPreset = NewObject<UPlanetGeologyPreset>();
	MigratedPreset->Elements.Add(Legacy);
	UPlanetGeologyPreset::MigrateLegacyElement(MigratedPreset->Elements[0], MigratedPreset);

	if (!TestEqual(TEXT("迁移后一个元素一个生成器"),
		MigratedPreset->Elements[0].Distributions.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("PodCount 置 0 作已迁移标记"), MigratedPreset->Elements[0].PodCount, 0);

	const UPlanetGeologySpherePodsDistribution* Migrated =
		Cast<UPlanetGeologySpherePodsDistribution>(MigratedPreset->Elements[0].Distributions[0]);
	if (!TestNotNull(TEXT("迁移目标是囊状生成器"), Migrated))
	{
		return false;
	}
	TestEqual(TEXT("种子 = 旧公式"), Migrated->Seed,
		int32(GetTypeHash(Legacy.Id) & 0x7fffffffu));

	// Seed 非 0 时 ResolveSeed 本来就返回字面值，这条与下面那条一起才钉得住
	// 「旧公式算出 0 时也不会落回新派生规则」。
	TestTrue(TEXT("迁移锁定显式种子"),
		Migrated->bSeedExplicit && Migrated->Seed == int32(GetTypeHash(Legacy.Id) & 0x7fffffffu));

	const auto MigratedField = FPlanetGeologyField::BuildFromPreset(*MigratedPreset, 40000.0);
	TestTrue(TEXT("迁移前后 IsEmpty 一致"),
		LegacyField->IsEmpty() == MigratedField->IsEmpty());

	int32 Mismatches = 0;
	int32 HitPoints = 0;
	auto ComparePoint = [&](const FVector& P)
	{
		const float A = LegacyField->SampleElementNormAtNormalized(0, P);
		const float B = MigratedField->SampleElementNormAtNormalized(0, P);
		Mismatches += FMath::IsNearlyEqual(A, B, 1e-6f) ? 0 : 1;
		HitPoints += A > 0.f ? 1 : 0;
	};

	FRandomStream Rng(5150);
	for (int32 SampleIndex = 0; SampleIndex < 4000; ++SampleIndex)
	{
		const FVector P(Rng.FRandRange(-1.f, 1.f), Rng.FRandRange(-1.f, 1.f),
			Rng.FRandRange(-1.f, 1.f));
		if (P.SizeSquared() <= 1.f)
		{
			ComparePoint(P);
		}
	}
	// 随机点几乎全在矿囊外，补一圈囊内探针，避免 0 == 0 的空转。
	for (const FPlanetGeologyPod& Pod : LegacyField->GetPods(0))
	{
		for (int32 SampleIndex = 0; SampleIndex < 32; ++SampleIndex)
		{
			const FVector Offset(Rng.FRandRange(-1.f, 1.f), Rng.FRandRange(-1.f, 1.f),
				Rng.FRandRange(-1.f, 1.f));
			const FVector P = Pod.Center + Offset * Pod.Radius * 1.2f;
			if (P.SizeSquared() <= 1.f)
			{
				ComparePoint(P);
			}
		}
	}

	TestEqual(TEXT("迁移前后逐点相等"), Mismatches, 0);
	TestTrue(*FString::Printf(TEXT("探针确实命中矿（%d 点）"), HitPoints), HitPoints > 0);
	return true;
}

// 遗留兜底的判据必须是数据层的 Params.Distributions，不是过滤掉空 runtime 之后的
// Definition.Distributions：挂一个暂时为空的生成器（SeamCount = 0 这类调参中间态）时
// 数据层明明有生成器，过滤后却变空 —— 拿过滤后的数组判会凭空冒出 PodCount 个旧矿囊，
// 而且与 MigrateLegacyElement（按数据层判、不会迁）对同一输入不等价。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetGeologyLegacyFallbackNeedsNoDataDistributionsTest,
	"StellarEra.Planet.Geology.LegacyFallbackNeedsNoDataDistributions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetGeologyLegacyFallbackNeedsNoDataDistributionsTest::RunTest(const FString& Parameters)
{
	UPlanetGeologyPreset* Preset = NewObject<UPlanetGeologyPreset>();
	FPlanetGeologyElementParams& Element = Preset->Elements.AddDefaulted_GetRef();
	Element.Id = TEXT("silicon");
	// 结构体默认就是 16：留着才测得出「有生成器却仍走兜底」。
	Element.PodCount = 16;

	// 空 runtime：BuildRuntime 会返回有效但 IsEmpty() 的对象，BuildFromPreset 把它过滤掉。
	UPlanetGeologyLensDistribution* EmptyLens =
		NewObject<UPlanetGeologyLensDistribution>(Preset);
	EmptyLens->SeamCount = 0;
	Element.Distributions.Add(EmptyLens);

	const auto Field = FPlanetGeologyField::BuildFromPreset(*Preset, 40000.0);
	if (!TestTrue(TEXT("场建成功"), Field.IsValid()))
	{
		return false;
	}
	TestEqual(TEXT("数据层有生成器时不得走遗留兜底"), Field->GetPods(0).Num(), 0);
	return true;
}

#endif
