// 编辑器态映射预览：以当前编辑好的天体布局为初始数据，按编辑器相机做距离压缩。
// 开启时快照全部天体的世界变换；之后每帧按相机位置重新摆放，关闭时恢复快照。

#include "CosmosLayoutPreview.h"

#if WITH_EDITOR

#include "Editor.h"
#include "EditorViewportClient.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "CosmosOrbitComponent.h"

namespace
{
	/** 1 = 编辑器里按相机压缩天体布局；0 = 正常编辑。 */
	TAutoConsoleVariable<int32> CVarEditorPreview(
		TEXT("StellarEra.Cosmos.EditorPreview"),
		0,
		TEXT("编辑器态映射预览：1 开、0 关（默认关）"),
		ECVF_Default);

	/** 近场半径（厘米）：相机距离小于它时保持 1:1。 */
	TAutoConsoleVariable<float> CVarPreviewNearCm(
		TEXT("StellarEra.Cosmos.EditorPreviewNearCm"),
		1.0e7f,
		TEXT("编辑器预览：近场半径（厘米），默认 100 km"),
		ECVF_Default);

	/** 远场压缩系数：显示距离 = Near + (d - Near) × 系数。越小压得越狠。 */
	TAutoConsoleVariable<float> CVarPreviewCompression(
		TEXT("StellarEra.Cosmos.EditorPreviewCompression"),
		0.05f,
		TEXT("编辑器预览：远场距离压缩系数（0.001–1，默认 0.05）"),
		ECVF_Default);

	struct FBodySnapshot
	{
		TWeakObjectPtr<AActor> Actor;
		FTransform Transform;
	};

	TArray<FBodySnapshot> GSnapshot;
	bool GPreviewActive = false;

	UWorld* FindEditorWorld()
	{
		if (!GEngine)
		{
			return nullptr;
		}
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::Editor && Context.World())
			{
				return Context.World();
			}
		}
		return nullptr;
	}

	bool FindEditorCamera(FVector& OutLocation)
	{
		if (!GEditor)
		{
			return false;
		}
		const FViewport* Viewport = GEditor->GetActiveViewport();
		if (!Viewport)
		{
			return false;
		}
		const FEditorViewportClient* Client = static_cast<const FEditorViewportClient*>(Viewport->GetClient());
		if (!Client)
		{
			return false;
		}
		OutLocation = Client->GetViewLocation();
		return true;
	}
}

void StellarEraCosmosUpdateEditorPreview(bool bEnabled)
{
	if (!bEnabled)
	{
		// 关：恢复开启前的编辑状态，撤销预览造成的位移/缩放。
		if (GPreviewActive)
		{
			for (const FBodySnapshot& Snapshot : GSnapshot)
			{
				if (AActor* Actor = Snapshot.Actor.Get())
				{
					Actor->SetActorTransform(Snapshot.Transform, false, nullptr, ETeleportType::TeleportPhysics);
				}
			}
			GSnapshot.Reset();
			GPreviewActive = false;
		}
		return;
	}

	UWorld* World = FindEditorWorld();
	if (!World)
	{
		return;
	}

	// 首次开启：把当前编辑好的布局快照下来，作为这次预览的初始数据。
	if (!GPreviewActive)
	{
		GSnapshot.Reset();
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->FindComponentByClass<UCosmosOrbitComponent>())
			{
				GSnapshot.Add({ *It, It->GetActorTransform() });
			}
		}
		GPreviewActive = true;
	}

	FVector Camera;
	if (!FindEditorCamera(Camera))
	{
		return;
	}

	const double NearCm = FMath::Max(double(CVarPreviewNearCm.GetValueOnGameThread()), 1.0);
	const double Compression = FMath::Clamp(double(CVarPreviewCompression.GetValueOnGameThread()), 0.001, 1.0);

	for (const FBodySnapshot& Snapshot : GSnapshot)
	{
		AActor* Actor = Snapshot.Actor.Get();
		if (!Actor)
		{
			continue;
		}

		const FVector Initial = Snapshot.Transform.GetLocation();
		const double Distance = (Initial - Camera).Size();
		// 相似变换：距离按 D 压，尺寸按 D/d 同比缩，角直径严格守恒。
		const double DisplayDistance = Distance <= NearCm
			? Distance
			: NearCm + (Distance - NearCm) * Compression;
		const double Scale = Distance > 1.0 ? DisplayDistance / Distance : 1.0;

		FTransform Preview = Snapshot.Transform;
		Preview.SetLocation(Camera + (Initial - Camera) * Scale);
		Preview.SetScale3D(Snapshot.Transform.GetScale3D() * Scale);
		Actor->SetActorTransform(Preview, false, nullptr, ETeleportType::TeleportPhysics);
	}
}

#endif // WITH_EDITOR
