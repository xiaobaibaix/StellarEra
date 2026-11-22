// 烘焙快照的 Content 资产缓存：命中就跳过烘焙；烘焙后标脏，关编辑器时静默落盘。
#pragma once
#include "CoreMinimal.h"

#include "UObject/Object.h"
#include "GeoBakeCache.generated.h"

class AActor;
struct FPlanetSnapshot;

/** 占位属性类型：编辑器里由属性行定制渲染成「立即烘焙」按钮。 */
USTRUCT()
struct STELLARERAPLANET_API FGeoBakeNowAction
{
	GENERATED_BODY()
};

UCLASS()
class STELLARERAPLANET_API UGeoBakeCache : public UObject
{
	GENERATED_BODY()

public:
#if WITH_EDITORONLY_DATA
	UPROPERTY() int32 FormatVersion = 0;
	UPROPERTY() int64 ConfigHash = 0;
	UPROPERTY() int32 RadiusVoxels = 0;
	/** SerializePlanetSnapshot 的产物。 */
	UPROPERTY() TArray<uint8> Blob;
#endif
};

namespace GeoBakeCache
{
	/** 每颗星一个包：/Game/Code/Planet/BakeCache/PC_<关卡>_<Actor>。 */
	STELLARERAPLANET_API FString PackagePathFor(const AActor& Actor);

	/** 版本、配置哈希、半径全等才命中；命中返回快照，否则 nullptr。 */
	STELLARERAPLANET_API TSharedPtr<const FPlanetSnapshot> Load(const FString& PackagePath, int64 ConfigHash, int32 RadiusVoxels);

	/** 写进缓存资产并标脏（不立即落盘）。 */
	STELLARERAPLANET_API void Store(const FString& PackagePath, const FPlanetSnapshot& Snapshot, int64 ConfigHash, int32 RadiusVoxels);

	/** 保存所有脏的缓存包（关编辑器时调用）；返回保存数量。 */
	STELLARERAPLANET_API int32 SaveDirty();
}
