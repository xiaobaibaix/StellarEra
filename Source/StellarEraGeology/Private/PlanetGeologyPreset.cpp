// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetGeologyPreset.h"

#if WITH_EDITOR
// ICONSOLEMANAGER：文件末尾的 StellarEra.CreateEarthGeologyPreset 用 FAutoConsoleCommand，
// 它不在 CoreMinimal 里。
#include "HAL/IConsoleManager.h"
// 以下几项只服务那条建资产命令：登记到资产注册表、拼磁盘路径、保存包。
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#endif

#if WITH_EDITOR
void UPlanetGeologyPreset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	++Revision;
	OnPresetChanged.Broadcast(this);
}
#endif

#if WITH_EDITOR
namespace
{
	void FillEarthPreset(UPlanetGeologyPreset& Preset)
	{
		auto AddElement = [&Preset](const TCHAR* Id, const TCHAR* DisplayName,
			const FLinearColor& Color, float Abundance, int32 PodCount, float PodRadius,
			int32 Seed)
		{
			FPlanetGeologyElementParams& Params = Preset.Elements.AddDefaulted_GetRef();
			Params.Id = Id;
			Params.DisplayName = DisplayName;
			Params.Color = Color;
			Params.Abundance = Abundance;
			Params.PodCount = PodCount;
			Params.PodRadius = PodRadius;
			Params.Seed = Seed;
		};

		Preset.Elements.Reset();
		AddElement(TEXT("silicon"), TEXT("硅"), FLinearColor(0.62f, 0.58f, 0.50f), 0.60f, 20, 0.050f, 303);
		AddElement(TEXT("iron"), TEXT("铁"), FLinearColor(0.72f, 0.42f, 0.36f), 0.75f, 20, 0.050f, 101);
		AddElement(TEXT("water"), TEXT("水"), FLinearColor(0.20f, 0.45f, 0.90f), 0.20f, 16, 0.048f, 606);
		AddElement(TEXT("copper"), TEXT("铜"), FLinearColor(0.85f, 0.55f, 0.25f), 0.45f, 16, 0.045f, 202);
		AddElement(TEXT("titanium"), TEXT("钛"), FLinearColor(0.70f, 0.72f, 0.78f), 0.15f, 12, 0.042f, 404);
		AddElement(TEXT("uranium"), TEXT("铀"), FLinearColor(0.45f, 0.85f, 0.35f), 0.10f, 10, 0.038f, 505);

		Preset.Appearance.ColorThreshold = 0.05f;
		Preset.Appearance.FadeRange = 0.12f;
		Preset.Appearance.PurityForFullColor = 0.45f;
		Preset.GeometryAmplitude = 1.5f;
	}

	void CreateEarthGeologyPreset()
	{
		const FString PackageName = TEXT("/Game/Geology/Planets/DA_Geology_Earth");
		const FString AssetName = TEXT("DA_Geology_Earth");
		const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *PackageName, *AssetName);

		// 先 LoadObject 而不是 CreatePackage 之后 FindObject：新进程里 CreatePackage 返回的是
		// 空包，FindObject 恒为 null，那样永远走「新建」，也就没法验证上一轮存下去的资产
		// 能不能读回来。这里读不到才建，读到就覆盖 —— 于是跑两次就能证明磁盘往返。
		UPlanetGeologyPreset* Preset = LoadObject<UPlanetGeologyPreset>(nullptr, *ObjectPath);
		const bool bCreated = Preset == nullptr;
		if (bCreated)
		{
			UPackage* Package = CreatePackage(*PackageName);
			if (!Package)
			{
				UE_LOG(LogTemp, Error, TEXT("建包失败: %s"), *PackageName);
				return;
			}
			Preset = NewObject<UPlanetGeologyPreset>(Package, *AssetName, RF_Public | RF_Standalone);
		}

		FillEarthPreset(*Preset);
		++Preset->Revision;

		if (bCreated)
		{
			FAssetRegistryModule::AssetCreated(Preset);
		}
		// 覆盖分支里没有局部 Package 变量，包从资产本身取。
		Preset->GetOutermost()->MarkPackageDirty();

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		const FString FileName = FPackageName::LongPackageNameToFilename(
			PackageName, FPackageName::GetAssetPackageExtension());
		const bool bSaved = UPackage::SavePackage(
			CastChecked<UPackage>(Preset->GetOutermost()), Preset, *FileName, SaveArgs);

		UE_LOG(LogTemp, Log, TEXT("StellarEra.CreateEarthGeologyPreset: %s（%s，%d 个元素）"),
			bSaved ? TEXT("已保存") : TEXT("保存失败"),
			bCreated ? TEXT("新建") : TEXT("覆盖"),
			Preset->Elements.Num());
	}
}

static FAutoConsoleCommand CreateEarthGeologyPresetCommand(
	TEXT("StellarEra.CreateEarthGeologyPreset"),
	TEXT("创建或覆盖 /Game/Geology/Planets/DA_Geology_Earth。"),
	FConsoleCommandDelegate::CreateStatic(&CreateEarthGeologyPreset));
#endif
