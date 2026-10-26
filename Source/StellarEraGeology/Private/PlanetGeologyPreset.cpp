// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetGeologyPreset.h"

#include "PlanetGeologySpherePodsDistribution.h"

#if WITH_EDITOR
// 地球初始数据用到的三种形态（片层 / 脉状 / 浸染）；囊状在上面，迁移也要用。
#include "PlanetGeologyDisseminatedDistribution.h"
#include "PlanetGeologyLensDistribution.h"
#include "PlanetGeologyVeinDistribution.h"
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

void UPlanetGeologyPreset::MigrateLegacyElement(FPlanetGeologyElementParams& Element,
	UObject* Outer)
{
	if (Element.Distributions.Num() > 0 || Element.PodCount <= 0)
	{
		return;
	}

	UPlanetGeologySpherePodsDistribution* Distribution =
		NewObject<UPlanetGeologySpherePodsDistribution>(Outer);
	Distribution->PodCount = Element.PodCount;
	Distribution->PodRadius = Element.PodRadius;
	Distribution->PodRadiusJitter = Element.PodRadiusJitter;
	Distribution->PodDepthBias = Element.PodDepthBias;
	Distribution->PodDepthCenter = Element.PodDepthCenter;
	Distribution->PodDepthSpread = Element.PodDepthSpread;
	Distribution->ShapeDetail = Element.ShapeDetail;
	Distribution->Octaves = Element.Octaves;
	Distribution->Lacunarity = Element.Lacunarity;
	Distribution->Gain = Element.Gain;
	Distribution->VeinThreshold = Element.VeinThreshold;
	// 旧公式。Seed=0 时也要锁定「派生后的值」，否则运行期会走新派生规则。
	Distribution->Seed = Element.Seed != 0
		? Element.Seed
		: int32(GetTypeHash(Element.Id) & 0x7fffffffu);
	Distribution->bSeedExplicit = true;

	Element.Distributions.Add(Distribution);
	Element.PodCount = 0;
}

void UPlanetGeologyPreset::PostLoad()
{
	Super::PostLoad();

	// 异步加载线程上不能建 UObject。跳过迁移不会改变行为：BuildFromPreset 的
	// 遗留兜底会按同一份旧字段建出同样的囊表；下次重新加载这份资产时会再试一次。
	if (!IsInGameThread())
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("[地质] %s 在非游戏线程加载，跳过旧字段迁移（运行期走遗留兜底）"),
			*GetPathName());
		return;
	}

	for (FPlanetGeologyElementParams& Element : Elements)
	{
		MigrateLegacyElement(Element, this);
	}
}

void UPlanetGeologyPreset::NotifyContentChanged()
{
	++Revision;
#if WITH_EDITOR
	OnPresetChanged.Broadcast(this);
#endif
}

#if WITH_EDITOR
void UPlanetGeologyPreset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	NotifyContentChanged();
}
#endif

#if WITH_EDITOR
namespace
{
	template <typename TDistribution>
	TDistribution* AddEarthDistribution(UPlanetGeologyPreset& Preset,
		FPlanetGeologyElementParams& Element)
	{
		// 只清 PodCount：它是遗留兜底的唯一开关（Distributions 为空 **且** PodCount > 0 才生效），
		// 其余遗留字段在有生成器时根本不参与采样。别删这一行 —— 删了这里的两套数据就会并存。
		Element.PodCount = 0;
		TDistribution* Distribution = NewObject<TDistribution>(&Preset);
		Distribution->ShapeDetail = 3.0f;
		Distribution->Octaves = 2;
		Distribution->Lacunarity = 2.0f;
		Distribution->Gain = 0.5f;
		Element.Distributions.Add(Distribution);
		return Distribution;
	}

	void FillEarthPreset(UPlanetGeologyPreset& Preset)
	{
		auto AddElement = [&Preset](const TCHAR* Id, const TCHAR* DisplayName,
			const FLinearColor& Color, float Abundance)
		{
			FPlanetGeologyElementParams& Params = Preset.Elements.AddDefaulted_GetRef();
			Params.Id = Id;
			Params.DisplayName = DisplayName;
			Params.Color = Color;
			Params.Abundance = Abundance;
			return &Params;
		};

		Preset.Elements.Reset();

		// 硅：片层，近地表的薄板
		{
			FPlanetGeologyElementParams& Element = *AddElement(
				TEXT("silicon"), TEXT("硅"), FLinearColor(0.62f, 0.58f, 0.50f), 0.60f);
			UPlanetGeologyLensDistribution* Lens =
				AddEarthDistribution<UPlanetGeologyLensDistribution>(Preset, Element);
			Lens->SeamCount = 16;
			Lens->SeamLength = 0.14f;
			Lens->SeamWidth = 0.07f;
			Lens->SeamThickness = 0.008f;
			Lens->DepthCenter = 0.10f;
			Lens->DepthBias = 0.5f;
			Lens->VeinThreshold = 0.45f;
		}

		// 铁：脉状，穿切地层
		{
			FPlanetGeologyElementParams& Element = *AddElement(
				TEXT("iron"), TEXT("铁"), FLinearColor(0.72f, 0.42f, 0.36f), 0.75f);
			UPlanetGeologyVeinDistribution* Vein =
				AddEarthDistribution<UPlanetGeologyVeinDistribution>(Preset, Element);
			Vein->VeinCount = 5;
			Vein->VeinLength = 0.40f;
			Vein->VeinRadius = 0.009f;
			Vein->BranchCount = 1;
			Vein->VeinThreshold = 0.50f;
		}

		// 水：更深更宽的片层（含水层）
		{
			FPlanetGeologyElementParams& Element = *AddElement(
				TEXT("water"), TEXT("水"), FLinearColor(0.20f, 0.45f, 0.90f), 0.20f);
			UPlanetGeologyLensDistribution* Lens =
				AddEarthDistribution<UPlanetGeologyLensDistribution>(Preset, Element);
			Lens->SeamCount = 14;
			Lens->SeamLength = 0.18f;
			Lens->SeamWidth = 0.10f;
			Lens->SeamThickness = 0.010f;
			Lens->DepthCenter = 0.18f;
			Lens->DepthBias = 0.5f;
			Lens->VeinThreshold = 0.40f;
		}

		// 铜：少而短的脉
		{
			FPlanetGeologyElementParams& Element = *AddElement(
				TEXT("copper"), TEXT("铜"), FLinearColor(0.85f, 0.55f, 0.25f), 0.45f);
			UPlanetGeologyVeinDistribution* Vein =
				AddEarthDistribution<UPlanetGeologyVeinDistribution>(Preset, Element);
			Vein->VeinCount = 3;
			Vein->VeinLength = 0.28f;
			Vein->VeinRadius = 0.007f;
			Vein->BranchCount = 0;
			Vein->VeinThreshold = 0.55f;
		}

		// 钛：浸染，细颗粒铺开
		{
			FPlanetGeologyElementParams& Element = *AddElement(
				TEXT("titanium"), TEXT("钛"), FLinearColor(0.70f, 0.72f, 0.78f), 0.15f);
			UPlanetGeologyDisseminatedDistribution* Disseminated =
				AddEarthDistribution<UPlanetGeologyDisseminatedDistribution>(Preset, Element);
			Disseminated->GrainCount = 320;
			Disseminated->GrainRadius = 0.0025f;
			Disseminated->VeinThreshold = 0.60f;
		}

		// 铀：更深、更稀的浸染
		{
			FPlanetGeologyElementParams& Element = *AddElement(
				TEXT("uranium"), TEXT("铀"), FLinearColor(0.45f, 0.85f, 0.35f), 0.10f);
			UPlanetGeologyDisseminatedDistribution* Disseminated =
				AddEarthDistribution<UPlanetGeologyDisseminatedDistribution>(Preset, Element);
			Disseminated->GrainCount = 140;
			Disseminated->GrainRadius = 0.0015f;
			Disseminated->DepthCenter = 0.35f;
			Disseminated->DepthBias = 0.6f;
			Disseminated->VeinThreshold = 0.65f;
		}

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
