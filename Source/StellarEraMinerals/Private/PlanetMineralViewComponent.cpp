// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetMineralViewComponent.h"

#include "PlanetAtmosphereComponent.h"
#include "PlanetGeologyField.h"
#include "PlanetGeologyPreset.h"
#include "PlanetMineralViewActor.h"
#include "VoxelPlanetActor.h"

#include "Engine/World.h"

UPlanetMineralViewComponent::UPlanetMineralViewComponent()
{
	// 只在激活期间 Tick，且排在后更新阶段（此时 N 体已把行星位置写回）。
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UPlanetMineralViewComponent::OnRegister()
{
	Super::OnRegister();

	// 订阅宿主行星的「预设指针换了」，并先对齐一次：镜像一旦过期，面板上就会
	// 显示一份行星上已经不用的数据，而按它写回还会把旧值盖回去。
	// 局部变量不能叫 Planet：本类有同名成员，MSVC 的 C4458 在本模块是错误。
	if (AVoxelPlanetActor* OwningPlanet = Cast<AVoxelPlanetActor>(GetOwner()))
	{
		OwningPlanet->OnGeologyAssetChanged.AddUObject(
			this, &UPlanetMineralViewComponent::RefreshGeologyAssetFromPlanet);
		RefreshGeologyAssetFromPlanet();
	}
}

void UPlanetMineralViewComponent::OnUnregister()
{
	if (AVoxelPlanetActor* OwningPlanet = Cast<AVoxelPlanetActor>(GetOwner()))
	{
		OwningPlanet->OnGeologyAssetChanged.RemoveAll(this);
	}
	Super::OnUnregister();
}

UPlanetGeologyPreset* UPlanetMineralViewComponent::GetPlanetGeologyAsset() const
{
	const AVoxelPlanetActor* OwningPlanet = Cast<AVoxelPlanetActor>(GetOwner());
	return OwningPlanet ? OwningPlanet->GetGeologyPresetAsset() : nullptr;
}

void UPlanetMineralViewComponent::RefreshGeologyAssetFromPlanet()
{
	// 宿主不是行星时置空而不是保留旧值：槽里留着一份无主的资产，
	// 下次有人按它写回就会写到一个看似成立、其实没人读的地方。
	GeologyAsset = GetPlanetGeologyAsset();
}

bool UPlanetMineralViewComponent::ApplyGeologyAssetToPlanet()
{
	AVoxelPlanetActor* OwningPlanet = Cast<AVoxelPlanetActor>(GetOwner());
	if (!OwningPlanet)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[矿物] %s 不挂在行星上，地质预设槽无处写回"), *GetNameSafe(GetOwner()));
		return false;
	}

	if (OwningPlanet->GetGeologyPresetAsset() == GeologyAsset.Get())
	{
		// 已经是这个值：不重设、不标脏。否则每次编辑都会白白弄脏一个包。
		return true;
	}

	// 写回的是行星自己的字段，只有它一份真值；本组件那份是 Transient 镜像。
	OwningPlanet->Modify();
	OwningPlanet->GeologyAsset = GeologyAsset;
	OwningPlanet->MarkPackageDirty();
	return true;
}

#if WITH_EDITOR
void UPlanetMineralViewComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// 只有面板上改槽才需要写回。撤销粒度：这一步发生在事务已经记下组件之后，
	// 所以 Ctrl+Z 会把组件那份还原、行星那份未必跟着回 —— 已知取舍，
	// 换的是「磁盘上只有一处真值」。
	if (PropertyChangedEvent.GetPropertyName()
		== GET_MEMBER_NAME_CHECKED(UPlanetMineralViewComponent, GeologyAsset))
	{
		ApplyGeologyAssetToPlanet();
	}
}
#endif

bool UPlanetMineralViewComponent::Activate(AVoxelPlanetActor* InPlanet)
{
	if (bActive)
	{
		Deactivate();
	}

	if (!InPlanet)
	{
		return false;
	}

	// 进来先对齐镜像：InPlanet 未必是 Owner（调用方可以传任意行星），而面板上的
	// 槽要显示「这次到底用了哪一份」。
	GeologyAsset = InPlanet->GetGeologyPresetAsset();

	TSharedPtr<const FPlanetGeologyField> Geology = InPlanet->GetGeologyField();
	if (!Geology.IsValid() || Geology->IsEmpty())
	{
		return false;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	// 编辑器预览世界（BP 编辑器预览、资产预览场景）不是关卡世界：在那里临时隐藏
	// 与包脏度语义都不成立，且掉进运行时分支会翻 bAtmosphereEnabled 等 UPROPERTY。
	if (World->WorldType == EWorldType::EditorPreview)
	{
		UE_LOG(LogTemp, Warning, TEXT("[矿物] 编辑器预览世界不支持矿物预览：%s"),
			*GetNameSafe(GetOwner()));
		return false;
	}

	// 编辑器世界与 PIE 是两套世界；用 WorldType 判，不用 GEditor->IsPlaySessionInProgress。
	// 非编辑器构建里没有「编辑器世界」这个概念，常量折叠成 false；同时也避开了
	// AActor 上只在 WITH_EDITOR 下才存在的临时隐藏 API（Actor.h 2583 行起）。
#if WITH_EDITOR
	const bool bInEditorWorld = World->WorldType == EWorldType::Editor;
#else
	const bool bInEditorWorld = false;
#endif

	const float SurfaceRadiusCm = InPlanet->GetSurfaceRadiusCm();
	// 地质深度以体素计，换回厘米要用单个体素的厘米尺寸（基类属性，与 Radius 同级）。
	// 明确假设行星 Actor 的等比缩放 S = 1：这个换算不含 Actor 缩放（见计划 Global Constraints）。
	const float VoxelSizeCm = InPlanet->VoxelSize;
	if (SurfaceRadiusCm <= 0.f || VoxelSizeCm <= 0.f)
	{
		return false;
	}

	PlanetMineralSurface::FBuildParams Params;
	Params.SurfaceRadiusCm = SurfaceRadiusCm;
	Params.VoxelSizeCm = VoxelSizeCm;
	Params.CellsPerFeature = CellsPerFeature;
	// 等值线跟地表着色共用一份参数：矿体边界与地表明暗是同一套判据。
	Params.IsoValue = Geology->GetAppearance().ColorThreshold;

	// 键里不含 IsoValue / VoxelSizeCm / 元素表：它们都来自地质场，而地质场**不在键里**。
	// ReloadGeology()（Details 面板的「重建行星」）会用新的地质对象替换旧的，而行星、
	// 半径、缩放全不变 —— 只比键会静默复用旧几何，再按新元素表上色（同元素数），
	// 或直接让观察体的「网格数 == 元素数」校验失败（不同元素数）。所以另外单独比对
	// 地质对象本身：共享指针相等才算同一份数据。CellsPerFeature 是组件上可编辑的属性，
	// 必须进键。
	PlanetMineralSurface::FCacheKey Key;
	Key.Planet = InPlanet;
	Key.SurfaceRadiusCm = SurfaceRadiusCm;
	Key.Scale = InPlanet->GetActorScale3D();
	Key.CellsPerFeature = CellsPerFeature;

	const bool bReused = bHasCachedSurface && CachedSurfaceGeology == Geology
		&& CachedKey == Key;
	if (!bReused)
	{
		PlanetMineralSurface::Build(*Geology, Params, CachedSurface);
		CachedKey = Key;
		CachedSurfaceGeology = Geology;
		bHasCachedSurface = true;
	}

	const TArray<FPlanetGeologyElement> SampledElements =
		Geology->GetSampledElementDefinitions();

	int32 TotalTriangles = 0;
	for (const FPlanetMineralMesh& Mesh : CachedSurface.MeshesByElement)
	{
		TotalTriangles += Mesh.Triangles.Num() / 3;
	}

	// 每元素「占据格数 / 面数」：诊断「全都没生成」与「某个元素为 0」是两回事，
	// 光看总数分不出来。顺序 = Elements（运行期采样顺序），下标与组件一致。
	TArray<FString> PerElementParts;
	for (int32 Index = 0; Index < CachedSurface.MeshesByElement.Num(); ++Index)
	{
		const FString Label =
			SampledElements.IsValidIndex(Index) && !SampledElements[Index].DisplayName.IsEmpty()
			? SampledElements[Index].DisplayName
			: FString::Printf(TEXT("#%d"), Index);
		PerElementParts.Add(FString::Printf(TEXT("元素%s:%d角点/%d面"), *Label,
			CachedSurface.MeshesByElement[Index].OccupiedCells,
			CachedSurface.MeshesByElement[Index].Triangles.Num() / 3));
	}

	UE_LOG(LogTemp, Display, TEXT("[矿物] %s：每特征 %d 格（%d 个角点），%d 个三角形，构建 %.1f ms%s"),
		*InPlanet->GetName(), CellsPerFeature, CachedSurface.GridPoints, TotalTriangles,
		CachedSurface.BuildSeconds * 1000.0,
		bReused ? TEXT("（复用缓存，耗时为首建）") : TEXT(""));
	UE_LOG(LogTemp, Display, TEXT("[矿物] %s：每元素矿体 %s"),
		*InPlanet->GetName(),
		PerElementParts.IsEmpty() ? TEXT("(无元素)") : *FString::Join(PerElementParts, TEXT(" ")));

	FActorSpawnParameters SpawnParams;
	if (bInEditorWorld)
	{
		// 永远不写进 umap。
		SpawnParams.ObjectFlags |= RF_Transient;
	}
	// Owner 是 AActor*：组件不能当 Owner，取本组件的宿主 Actor（就是这颗行星）。
	SpawnParams.Owner = GetOwner();
	SpawnParams.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ViewActor = World->SpawnActor<APlanetMineralViewActor>(
		APlanetMineralViewActor::StaticClass(),
		InPlanet->GetActorLocation(), FRotator::ZeroRotator, SpawnParams);

	if (!ViewActor
		|| !ViewActor->Build(CachedSurface.MeshesByElement, InPlanet->SurfaceColor,
			SurfaceRadiusCm, SampledElements))
	{
		UE_LOG(LogTemp, Error, TEXT("[矿物] 观察体创建失败：%s"), *InPlanet->GetName());
		if (ViewActor)
		{
			ViewActor->Destroy();
			ViewActor = nullptr;
		}
		return false;
	}

	Planet = InPlanet;
	CachedGeology = Geology;
	Elements = SampledElements;

	bEditorPreview = bInEditorWorld;

	if (bEditorPreview)
	{
		// 编辑器分支**不改任何 UPROPERTY**：翻 bAtmosphereEnabled / bActorEnableCollision
		// 会把关卡标脏，退出后留下待提交的脏包。临时隐藏是非序列化状态。
		// 这两个 API 在 AActor 上是 WITH_EDITOR 专有，非编辑器构建里 bEditorPreview
		// 恒为 false，所以整段才不会走到。
#if WITH_EDITOR
		bSavedPlanetHiddenInEditor = InPlanet->IsTemporarilyHiddenInEditor();
		InPlanet->SetIsTemporarilyHiddenInEditor(true);
#endif
	}
	else
	{
		// 先存后改：恢复时原样写回，别假设进入前一定是「可见 + 有碰撞」。
		bSavedPlanetHidden = InPlanet->IsHidden();
		bSavedCollisionEnabled = InPlanet->GetActorEnableCollision();
		InPlanet->SetActorEnableCollision(false);
		InPlanet->SetActorHiddenInGame(true);

		if (UPlanetAtmosphereComponent* Atmosphere =
			InPlanet->FindComponentByClass<UPlanetAtmosphereComponent>())
		{
			bSavedAtmosphereEnabled = Atmosphere->bAtmosphereEnabled;
			bSavedAtmosphereVisible = Atmosphere->IsVisible();
			Atmosphere->bAtmosphereEnabled = false;
			Atmosphere->SetVisibility(false);
		}
	}

#if WITH_EDITOR
	if (bEditorPreview)
	{
		if (UPlanetGeologyPreset* Preset = InPlanet->GetGeologyPresetAsset())
		{
			PresetChangedHandle = Preset->OnPresetChanged.AddUObject(
				this, &UPlanetMineralViewComponent::OnPreviewPresetChanged);
			SubscribedPreset = Preset;
		}
	}
#endif

	bActive = true;
	SetComponentTickEnabled(!bEditorPreview);
	return true;
}

void UPlanetMineralViewComponent::Deactivate()
{
	if (!bActive)
	{
		return;
	}
	bActive = false;
	SetComponentTickEnabled(false);

	if (APlanetMineralViewActor* View = ViewActor)
	{
		View->Destroy();
	}
	ViewActor = nullptr;

#if WITH_EDITOR
	if (PresetChangedHandle.IsValid())
	{
		// 从「订上的那一份」退订：预览期间若把 GeologyAsset 换掉，用当下的资产会退错对象，
		// 旧资产上留下一个指向本组件的订阅。
		if (UPlanetGeologyPreset* Preset = SubscribedPreset.Get())
		{
			Preset->OnPresetChanged.Remove(PresetChangedHandle);
		}
		PresetChangedHandle.Reset();
	}
	SubscribedPreset.Reset();
#endif

	if (AVoxelPlanetActor* RestorePlanet = Planet.Get())
	{
		if (bEditorPreview)
		{
#if WITH_EDITOR
			RestorePlanet->SetIsTemporarilyHiddenInEditor(bSavedPlanetHiddenInEditor);
#endif
		}
		else
		{
			RestorePlanet->SetActorHiddenInGame(bSavedPlanetHidden);
			RestorePlanet->SetActorEnableCollision(bSavedCollisionEnabled);

			if (UPlanetAtmosphereComponent* Atmosphere =
				RestorePlanet->FindComponentByClass<UPlanetAtmosphereComponent>())
			{
				Atmosphere->bAtmosphereEnabled = bSavedAtmosphereEnabled;
				if (bSavedAtmosphereEnabled)
				{
					// Refresh 末尾会 SetVisibility(true)：必须先刷新、再恢复进入前的
					// 可见性，否则「enabled 但被手动隐藏」的壳会被强行显示。
					Atmosphere->RefreshAtmosphereGeometry();
				}
				Atmosphere->SetVisibility(bSavedAtmosphereVisible);
			}
		}
	}

	bEditorPreview = false;

	Planet.Reset();
	CachedGeology.Reset();
	Elements.Reset();
}

void UPlanetMineralViewComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Deactivate();
	Super::EndPlay(EndPlayReason);
}

void UPlanetMineralViewComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bActive)
	{
		return;
	}

	// 只跟位置：N 体只平移，行星不转向也不缩放；跟着转会把矿体拧歪。
	if (const AVoxelPlanetActor* PlanetActor = Planet.Get())
	{
		if (ViewActor)
		{
			ViewActor->SetActorLocation(PlanetActor->GetActorLocation());
		}
	}
}

int32 UPlanetMineralViewComponent::NumElements() const
{
	return Elements.Num();
}

bool UPlanetMineralViewComponent::IsElementVisible(int32 Index) const
{
	return ViewActor && ViewActor->IsElementVisible(Index);
}

void UPlanetMineralViewComponent::SetElementVisible(int32 Index, bool bNewVisible)
{
	if (ViewActor)
	{
		ViewActor->SetElementVisible(Index, bNewVisible);
	}
}

void UPlanetMineralViewComponent::ToggleElement(int32 Index)
{
	if (!Elements.IsValidIndex(Index))
	{
		return;
	}
	SetElementVisible(Index, !IsElementVisible(Index));
}

void UPlanetMineralViewComponent::SetAllElementsVisible(bool bNewVisible)
{
	if (ViewActor)
	{
		ViewActor->SetAllElementsVisible(bNewVisible);
	}
}

#if WITH_EDITOR
void UPlanetMineralViewComponent::EnterMineralPreviewInEditor()
{
	// 宿主就是行星：组件挂在 AVoxelPlanetActor 上。
	if (!Activate(Cast<AVoxelPlanetActor>(GetOwner())))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[矿物] 预览失败：宿主没有地质预设，或采样参数非法（%s）"),
			*GetNameSafe(GetOwner()));
	}
}

void UPlanetMineralViewComponent::ExitMineralPreviewInEditor()
{
	Deactivate();
}

void UPlanetMineralViewComponent::OnPreviewPresetChanged(UPlanetGeologyPreset* Preset)
{
	// 只认当前订上的那份：换过资产的旧订阅回调到这里就丢掉。
	if (Preset != SubscribedPreset.Get())
	{
		return;
	}

	// 重新 Activate：BuildFromPreset 会造新场，缓存键里的地质对象随之改变 → 重建网格。
	if (AVoxelPlanetActor* InPlanet = Planet.Get())
	{
		// Activate 开头已 Deactivate，失败时预览已经消失；这里补一条日志，
		// 否则「预设的元素表被清空」表现为预览无声无息地没了。
		if (!Activate(InPlanet))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[矿物] 预设改动后重建失败，预览已退出：预设已无矿囊或采样参数非法（%s）"),
				*GetNameSafe(InPlanet));
		}
	}
}
#endif
