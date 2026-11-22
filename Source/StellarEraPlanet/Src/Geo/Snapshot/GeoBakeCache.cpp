#include "GeoBakeCache.h"

#if WITH_EDITOR
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/PackageName.h"
#include "PlanetSnapshot.h"
#include "PlanetSnapshotSerialize.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectIterator.h"
#include "HAL/IConsoleManager.h"

namespace
{
	FString Sanitize(const FString& In)
	{
		FString Out = In;
		for (TCHAR& C : Out)
		{
			if (!FChar::IsAlnum(C) && C != TEXT('_')) { C = TEXT('_'); }
		}
		return Out;
	}

	FString AssetNameOf(const FString& PackagePath)
	{
		int32 Slash = INDEX_NONE;
		PackagePath.FindLastChar(TEXT('/'), Slash);
		return Slash == INDEX_NONE ? PackagePath : PackagePath.RightChop(Slash + 1);
	}
}

FString GeoBakeCache::PackagePathFor(const AActor& Actor)
{
	// PIE 世界名带 UEDPIE_<n>_ 前缀：不剥掉就会去建第二份缓存，PIE 永远命中不了编辑器烘好的那份。
	FString Map = Actor.GetWorld() ? UWorld::RemovePIEPrefix(Actor.GetWorld()->GetMapName()) : TEXT("NoWorld");
	Map = Sanitize(Map);
	return FString::Printf(TEXT("/Game/Code/Planet/BakeCache/PC_%s_%s"), *Map, *Sanitize(Actor.GetActorNameOrLabel()));
}

TSharedPtr<const FPlanetSnapshot> GeoBakeCache::Load(const FString& PackagePath, int64 ConfigHash, int32 RadiusVoxels)
{
	UGeoBakeCache* Cache = LoadObject<UGeoBakeCache>(nullptr,
		*FString::Printf(TEXT("%s.%s"), *PackagePath, *AssetNameOf(PackagePath)), nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!Cache)
	{
		return nullptr;
	}
	if (Cache->FormatVersion != GeoBakeCacheFormat::Version || Cache->ConfigHash != ConfigHash || Cache->RadiusVoxels != RadiusVoxels)
	{
		UE_LOG(LogTemp, Display, TEXT("[烘焙缓存] %s 失效（版本/配置/半径不匹配），重新烘焙"), *PackagePath);
		return nullptr;
	}
	TSharedPtr<FPlanetSnapshot> Snapshot = DeserializePlanetSnapshot(Cache->Blob);
	UE_LOG(LogTemp, Display, TEXT("[烘焙缓存] %s 命中（%.1f MB）"), *PackagePath, double(Cache->Blob.Num()) / (1024.0 * 1024.0));
	return Snapshot;
}

void GeoBakeCache::Store(const FString& PackagePath, const FPlanetSnapshot& Snapshot, int64 ConfigHash, int32 RadiusVoxels)
{
	const FString AssetName = AssetNameOf(PackagePath);
	UGeoBakeCache* Cache = LoadObject<UGeoBakeCache>(nullptr,
		*FString::Printf(TEXT("%s.%s"), *PackagePath, *AssetName), nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!Cache)
	{
		UPackage* Package = CreatePackage(*PackagePath);
		if (!Package) { return; }
		Package->MarkAsFullyLoaded();
		Cache = NewObject<UGeoBakeCache>(Package, *AssetName, RF_Public | RF_Standalone);
	}
	if (!Cache) { return; }

	TArray<uint8> Blob;
	if (!SerializePlanetSnapshot(Snapshot, Blob))
	{
		UE_LOG(LogTemp, Error, TEXT("[烘焙缓存] 序列化失败，%s 不更新"), *PackagePath);
		return;
	}
	Cache->FormatVersion = GeoBakeCacheFormat::Version;
	Cache->ConfigHash = ConfigHash;
	Cache->RadiusVoxels = RadiusVoxels;
	Cache->Blob = MoveTemp(Blob);
	Cache->GetOutermost()->MarkPackageDirty();
	UE_LOG(LogTemp, Display, TEXT("[烘焙缓存] %s 已更新（%.1f MB，等待保存）"), *PackagePath,
		double(Cache->Blob.Num()) / (1024.0 * 1024.0));
}

FAutoConsoleCommand GSaveBakeCache(
	TEXT("StellarEra.Geo.SaveBakeCache"),
	TEXT("立即保存所有脏的烘焙缓存资产（关编辑器时也会自动做）。"),
	FConsoleCommandDelegate::CreateStatic([]()
	{
		const int32 Saved = GeoBakeCache::SaveDirty();
		UE_LOG(LogTemp, Display, TEXT("[烘焙缓存] 手动保存 %d 个"), Saved);
	}));

int32 GeoBakeCache::SaveDirty()
{
	int32 Saved = 0;
	for (TObjectIterator<UGeoBakeCache> It; It; ++It)
	{
		UPackage* Package = It->GetOutermost();
		if (!Package || !Package->IsDirty()) { continue; }
		const FString FileName = FPackageName::LongPackageNameToFilename(
			Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		if (UPackage::SavePackage(Package, *It, *FileName, Args))
		{
			++Saved;
			UE_LOG(LogTemp, Display, TEXT("[烘焙缓存] 已保存 %s"), *Package->GetName());
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[烘焙缓存] 保存失败 %s"), *Package->GetName());
		}
	}
	return Saved;
}
#endif
