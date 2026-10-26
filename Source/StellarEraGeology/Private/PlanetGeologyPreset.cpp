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

FSoftObjectPath UPlanetGeologyPreset::MakeAssetPath(const FString& PlanetName)
{
	// 路径规则只留这一份（见头文件注释）。这里的实现从
	// AVoxelPlanetActor::MakeLegacyGeologyAssetPath 整体搬过来：迁移目标与建资产命令
	// 必须逐字一致，所以不许再各写一份。
	FString Name = PlanetName;
	Name.TrimStartAndEndInline();
	if (Name.IsEmpty())
	{
		return FSoftObjectPath();
	}
	Name[0] = FChar::ToUpper(Name[0]);

	const FString AssetName = FString::Printf(TEXT("DA_Geology_%s"), *Name);
	return FSoftObjectPath(FString::Printf(
		TEXT("/Game/Geology/Planets/%s.%s"), *AssetName, *AssetName));
}

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

			// 其余字段显式写成当前默认值（与 FPlanetGeologyElementParams 的成员初值逐字相同）。
			// 资产是「这份预设的完整定义」，不是「除默认值以外的一切」：哪天结构体默认值改了，
			// 这里不改就意味着地球的矿囊形状被悄悄换掉；显式写出来，改动才会出现在 diff 里。
			Params.PodRadiusJitter = 0.5f;
			Params.PodDepthBias = 0.0f;
			Params.PodDepthCenter = 0.25f;
			Params.PodDepthSpread = 0.15f;
			Params.ShapeDetail = 3.0f;
			Params.Octaves = 2;
			Params.Lacunarity = 2.0f;
			Params.Gain = 0.5f;
			Params.VeinThreshold = 0.5f;
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
		// 不在这里拼路径字面量：与 PostLoad 的迁移共用 UPlanetGeologyPreset::MakeAssetPath。
		const FSoftObjectPath AssetPath = UPlanetGeologyPreset::MakeAssetPath(TEXT("earth"));
		const FString PackageName = AssetPath.GetLongPackageName();
		const FString AssetName = AssetPath.GetAssetName();
		const FString ObjectPath = AssetPath.ToString();

		// 先看包在不在盘上，再决定要不要 LoadObject：首次创建时探一次加载会在日志里留下
		// 「未找到 Object」的噪音，而这里本来就预期它不存在。
		//
		// 仍用 LoadObject 而不是 CreatePackage 之后 FindObject：新进程里 CreatePackage 返回的是
		// 空包，FindObject 恒为 null，那样永远走「新建」，也就没法验证上一轮存下去的资产
		// 能不能读回来。这里读不到才建，读到就覆盖 —— 于是跑两次就能证明磁盘往返。
		UPlanetGeologyPreset* Preset = FPackageName::DoesPackageExist(PackageName)
			? LoadObject<UPlanetGeologyPreset>(nullptr, *ObjectPath)
			: nullptr;
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
		// Revision 只是缓存键（GetRevision 的消费者用它判断「同一份资产的内容变了」），
		// 不是内容。重跑这条命令也会自增，所以即使元素值一个字都没改，落盘后的资产字节也会变。
		++Preset->Revision;

		if (bCreated)
		{
			FAssetRegistryModule::AssetCreated(Preset);
		}
		// 覆盖分支里没有局部 Package 变量，包从资产本身取。
		UPackage* const AssetPackage = Preset->GetPackage();
		AssetPackage->MarkPackageDirty();

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		const FString FileName = FPackageName::LongPackageNameToFilename(
			PackageName, FPackageName::GetAssetPackageExtension());
		const bool bSaved = UPackage::SavePackage(AssetPackage, Preset, *FileName, SaveArgs);

		if (bSaved)
		{
			UE_LOG(LogTemp, Log, TEXT("StellarEra.CreateEarthGeologyPreset: 已保存（%s，%d 个元素）"),
				bCreated ? TEXT("新建") : TEXT("覆盖"),
				Preset->Elements.Num());
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("StellarEra.CreateEarthGeologyPreset: 保存失败（%s，%d 个元素）→ %s"),
				bCreated ? TEXT("新建") : TEXT("覆盖"),
				Preset->Elements.Num(),
				*FileName);
		}
	}
}

static FAutoConsoleCommand CreateEarthGeologyPresetCommand(
	TEXT("StellarEra.CreateEarthGeologyPreset"),
	TEXT("创建或覆盖 /Game/Geology/Planets/DA_Geology_Earth。"),
	FConsoleCommandDelegate::CreateStatic(&CreateEarthGeologyPreset));
#endif
