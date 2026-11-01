// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetMineralViewComponent.h"

#include "PlanetAtmosphereComponent.h"
#include "PlanetGeologyField.h"
#include "PlanetGeologyPreset.h"
#include "PlanetMineralViewActor.h"
#include "VoxelComponents/VoxelInvokerComponent.h"
#include "VoxelMineralFieldActor.h"
#include "VoxelPlanetActor.h"
#include "VoxelViewpointInvokerUtils.h"

#include "Engine/World.h"

namespace
{
	/**
	 * 把地质场翻成每个元素一份的采样参数。
	 *
	 * 两个回调都捕获**共享指针**而不是裸指针：地质场对象可能被 ReloadGeology
	 * 换掉，裸指针会悬空；持有共享指针同时保证旧对象不会在生成器还在用的时候
	 * 被释放。
	 */
	TArray<FPlanetMineralVoxelParams> MakeVoxelParams(
		AVoxelPlanetActor* Planet,
		const TSharedPtr<const FPlanetGeologyField>& Geology,
		const TArray<FPlanetGeologyElement>& Elements,
		float SurfaceRadiusCm)
	{
		TArray<FPlanetMineralVoxelParams> Out;
		Out.SetNum(Elements.Num());

		// 共享指针只取一次：这些回调跑在插件工作线程上，按体素回弱指针上解析
		// 行星 Actor 是每格一笔无谓开销。TVoxelSharedPtr 本身线程安全、可拷贝，
		// 也让 FVoxelData（连同里面的生成器实例）活过行星 Actor 的更早一次销毁。
		const TVoxelSharedPtr<FVoxelData> PlanetData = Planet->GetDataSharedPtr();

		const double RadiusVoxels = double(SurfaceRadiusCm) / double(Planet->VoxelSize);
		const FPlanetGeologyAppearance& Appearance = Geology->GetAppearance();

		for (int32 Index = 0; Index < Elements.Num(); ++Index)
		{
			FPlanetMineralVoxelParams& Params = Out[Index];
			Params.RadiusVoxels = RadiusVoxels;
			Params.IsoValue = Appearance.ColorThreshold;
			Params.FadeRange = Appearance.FadeRange;

			TArray<FPlanetGeologyRegion> Regions;
			Geology->EnumerateRegions(Index, Regions);
			Params.RegionBoxes.Reserve(Regions.Num());
			for (const FPlanetGeologyRegion& Region : Regions)
			{
				Params.RegionBoxes.Add(Region.Bounds);
			}

			const TSharedPtr<const FPlanetGeologyField> FieldForElement = Geology;
			Params.SampleElementNorm =
				[FieldForElement, Index](const FVector& NormalizedPoint) -> float
			{
				return FieldForElement->SampleElementNormAtNormalized(Index, NormalizedPoint);
			};

			Params.IsSolidAtPlanetVoxel = [PlanetData](const FIntVector& VoxelIndex) -> bool
			{
				if (!PlanetData.IsValid())
				{
					// 世界还没建好：视作实心。宁可多画，不要整颗星没矿。
					return true;
				}
				return !PlanetData->GetValue(VoxelIndex, /*LOD=*/0).IsEmpty();
			};
		}
		return Out;
	}
} // namespace

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

#if WITH_EDITOR
	// 这次要重建的就是当下这份预设：过期标记随之清掉。构建失败时预览也没了，
	// 留着「过期」只会让人以为还有一份旧矿体可看。
	bPreviewStale = false;
#endif

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

	// 采样元素表与两条渲染路径无关：图例、数字键、矿体下标都用它。
	const TArray<FPlanetGeologyElement> SampledElements =
		Geology->GetSampledElementDefinitions();

	// marching cubes 的几何与诊断只在走那条路时才做。体素路径用不到这份结果，
	// 留着它等于每次 Activate 都白付 ~1.4 s / 3.76 M 三角形（地球），还会把
	// 「体素路径到底多贵」的测量搅浑。
	if (!bUseVoxelWorlds)
	{
		const bool bReused = bHasCachedSurface && CachedSurfaceGeology == Geology
			&& CachedKey == Key;
		if (!bReused)
		{
			PlanetMineralSurface::Build(*Geology, Params, CachedSurface);
			CachedKey = Key;
			CachedSurfaceGeology = Geology;
			bHasCachedSurface = true;
		}

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

		UE_LOG(LogTemp, Display,
			TEXT("[矿物] %s：每特征 %d 格（%d 个求值角点），%d 个三角形，构建 %.1f ms%s"),
			*InPlanet->GetName(), CellsPerFeature, CachedSurface.GridPoints, TotalTriangles,
			CachedSurface.BuildSeconds * 1000.0,
			bReused ? TEXT("（复用缓存，耗时为首建）") : TEXT(""));
		UE_LOG(LogTemp, Display, TEXT("[矿物] %s：每元素矿体 %s"),
			*InPlanet->GetName(),
			PerElementParts.IsEmpty() ? TEXT("(无元素)") : *FString::Join(PerElementParts, TEXT(" ")));

		// 并行效率一行看全：JobSeconds/BuildSeconds 就是这次实际用到的核数（求值 + 抽面
		// 的累计墙钟 ÷ 总墙钟）。接近 1 = 有个大任务把并行度卡死了；撞上限的任务数非 0 =
		// 这批几何被降采样了，光降 CellsPerFeature 也救不回它们。
		const double EffectiveCores = CachedSurface.BuildSeconds > 0.0
			? CachedSurface.JobSeconds / CachedSurface.BuildSeconds
			: 0.0;
		UE_LOG(LogTemp, Display,
			TEXT("[矿物] %s：%d 个(元素,簇)任务（%d 个撞角点上限），求值 %.1f ms / 抽面 %.1f ms，约 %.1f 核并行"),
			*InPlanet->GetName(), CachedSurface.PairCount, CachedSurface.CappedPairs,
			CachedSurface.SampleSeconds * 1000.0, CachedSurface.MarchSeconds * 1000.0,
			EffectiveCores);
	}

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
	APlanetMineralViewActor* SpawnedView = World->SpawnActor<APlanetMineralViewActor>(
		APlanetMineralViewActor::StaticClass(),
		InPlanet->GetActorLocation(), FRotator::ZeroRotator, SpawnParams);

	if (!SpawnedView)
	{
		UE_LOG(LogTemp, Error, TEXT("[矿物] 观察体 spawn 失败：%s"), *InPlanet->GetName());
		return false;
	}

	bool bBuilt = false;
	if (bUseVoxelWorlds)
	{
		// 体素路径要求行星自己停下来：视图期间没有挖掘、也不需要行星继续生成块。
		// 停掉它同时让下面那把长读锁的等待有界 —— 只在飞的少量块会与它竞争。
		bDisabledPlanetInvoker = false;
		if (UVoxelSimpleInvokerComponent* PlanetInvoker =
			InPlanet->FindComponentByClass<UVoxelSimpleInvokerComponent>())
		{
			if (PlanetInvoker->IsInvokerEnabled())
			{
				VoxelViewpointInvokers::SetEnabled(PlanetInvoker, false);
				// SetEnabled 对未注册的组件是空操作，所以要读回确认真的关掉了：
				// 没关成就不该在 Deactivate 里替别人打开。
				bDisabledPlanetInvoker = !PlanetInvoker->IsInvokerEnabled();
			}
		}

		// 读锁必须在建世之前拿：生成器一 CreateWorld 就开始在自己的工作线程上
		// 读行星数据。
		//
		// 锁整颗星的**世界包围盒**（GetBoundsFromDepth 给的是 [-Size, Size)，体素
		// 世界以原点为中心），不是 [0, Size) 的正八分区：FVoxelDataOctreeBase::Get
		// 里的 ensureThreadSafe(IsLockedForRead) 只看实际走到的那个八叉树，
		// 半个世界没锁等于半个世界没保护。
		PlanetDataLock = MakeUnique<FVoxelReadScopeLock>(
			InPlanet->GetData(),
			InPlanet->GetWorldBounds(),
			TEXT("PlanetMineralVoxelView"));

		const TArray<FPlanetMineralVoxelParams> ParamsByElement =
			MakeVoxelParams(InPlanet, Geology, SampledElements, SurfaceRadiusCm);

		bBuilt = SpawnedView->BuildVoxelWorlds(
			ParamsByElement, *InPlanet, InPlanet->SurfaceColor,
			SurfaceRadiusCm, SampledElements);
	}
	else
	{
		bBuilt = SpawnedView->Build(CachedSurface.MeshesByElement, InPlanet->SurfaceColor,
			SurfaceRadiusCm, SampledElements);
	}

	ViewActor = SpawnedView;
	if (!bBuilt)
	{
		UE_LOG(LogTemp, Error, TEXT("[矿物] 观察体创建失败：%s"), *InPlanet->GetName());
		ViewActor->Destroy();
		ViewActor = nullptr;
		PlanetDataLock.Reset();

		// 先放锁再恢复调用器：反过来的话行星一恢复就开始写数据，而我们还持着
		// 读锁 —— 那才是真的死锁。失败路径上 Planet 按设计还没赋值，用 InPlanet。
		RestorePlanetInvoker(InPlanet);
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
		View->ReleaseAllFields();
		View->Destroy();
	}
	ViewActor = nullptr;

	// 先放锁再恢复行星调用器：反过来的话行星一恢复就开始写数据，而我们还持着
	// 读锁 —— 那才是真的死锁。
	PlanetDataLock.Reset();

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
	// 预览没了就没有「旧矿体」可言：再进来必然重算。
	bPreviewStale = false;
#endif

	if (AVoxelPlanetActor* RestorePlanet = Planet.Get())
	{
		// 与 bEditorPreview 无关：Activate 无条件关掉调用器，这里就得无条件恢复。
		// bIsInvokerEnabled 不是 UPROPERTY，切它不弄脏关卡。
		RestorePlanetInvoker(RestorePlanet);

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

void UPlanetMineralViewComponent::RestorePlanetInvoker(AVoxelPlanetActor* PlanetActor)
{
	if (!bDisabledPlanetInvoker)
	{
		// 不是我们关的（或根本没关成）：别替别人打开。星图模式也在切这个调用器。
		return;
	}
	bDisabledPlanetInvoker = false;

	if (!PlanetActor)
	{
		return;
	}
	if (UVoxelSimpleInvokerComponent* PlanetInvoker =
		PlanetActor->FindComponentByClass<UVoxelSimpleInvokerComponent>())
	{
		// SetEnabled 幂等：状态已经对时什么都不做，不会刷 Message Log。
		VoxelViewpointInvokers::SetEnabled(PlanetInvoker, true);
	}
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
	// 体素路径还要把每个矿物体素世界一起搬 —— 它们不 attach 到观察体上。
	if (const AVoxelPlanetActor* PlanetActor = Planet.Get())
	{
		if (ViewActor)
		{
			ViewActor->SyncToPlanetLocation(PlanetActor->GetActorLocation());
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

void UPlanetMineralViewComponent::RefreshMineralPreview()
{
	// 与进入预览同一条路，不另写一套：Activate 开头先拆旧观察体，缓存按地质对象
	// 判等，预设改过就必然重建。两条路分开写，迟早有一边忘了同步。
	EnterMineralPreviewInEditor();
}

bool UPlanetMineralViewComponent::MarkPreviewStale()
{
	if (bPreviewStale)
	{
		return false;
	}
	bPreviewStale = true;

	// 刻意不重建：改一次参数要重算整份等值面再加观察体，耗时随 CellsPerFeature
	// 平方增长；而 Details 拖一次数值框会连发十几次改动，自动重建的结果是编辑器
	// 一直无响应，中间十几次的结果也没人看。
	UE_LOG(LogTemp, Display,
		TEXT("[矿物] %s：地质预设已改动，预览仍是旧矿体。选中行星，在 Details 的"
			" Planet|MineralView 里点「刷新观测视图」重算。"), *GetNameSafe(GetOwner()));
	return true;
}

void UPlanetMineralViewComponent::OnPreviewPresetChanged(UPlanetGeologyPreset* Preset)
{
	// 只认当前订上的那份：换过资产的旧订阅回调到这里就丢掉。
	if (Preset != SubscribedPreset.Get())
	{
		return;
	}

	// 只标脏，不重建：见 MarkPreviewStale 里的理由。重建由 RefreshMineralPreview
	// 显式触发。
	MarkPreviewStale();
}
#endif

namespace
{
	/**
	 * 逐元素统计矿量。走解析场：与相机、LOD、体素世界全部无关。
	 *
	 * 代价是 O((区域 AABB 边长 / 步长)³)：调用方**必须传粗步长**（米级），
	 * 按单个体素去采样会把一次图例刷新变成几亿次求值。步长这里不夹 ——
	 * 夹了会让报出的数悄悄变。
	 *
	 * 采样点在**归一化空间**，步长从厘米换算过去。区域 AABB 之外的采样点直接跳过
	 * —— 那里 Norm 恒为 0，换算成体素后只会白白跑一遍分布求值。
	 */
	float ComputeElementVolumeCm3(
		const FPlanetGeologyField& Geology,
		const AVoxelPlanetActor& Planet,
		int32 ElementIndex,
		float StepCm,
		float SurfaceRadiusCm)
	{
		if (StepCm <= 0.f || SurfaceRadiusCm <= 0.f || Planet.VoxelSize <= 0.f)
		{
			return 0.f;
		}

		TArray<FPlanetGeologyRegion> Regions;
		Geology.EnumerateRegions(ElementIndex, Regions);
		if (Regions.IsEmpty())
		{
			return 0.f;
		}

		const float Threshold = Geology.GetAppearance().ColorThreshold;
		const double RadiusVoxels = double(SurfaceRadiusCm) / double(Planet.VoxelSize);
		const double StepVoxels = double(StepCm) / double(Planet.VoxelSize);

		const bool bPlanetCreated = Planet.IsCreated();
		int64 SolidSamples = 0;

		for (const FPlanetGeologyRegion& Region : Regions)
		{
			if (!Region.Bounds.IsValid)
			{
				continue;
			}
			const FVector3d Min = FVector3d(Region.Bounds.Min) * RadiusVoxels;
			const FVector3d Max = FVector3d(Region.Bounds.Max) * RadiusVoxels;

			for (double Z = Min.Z; Z <= Max.Z; Z += StepVoxels)
			{
				for (double Y = Min.Y; Y <= Max.Y; Y += StepVoxels)
				{
					for (double X = Min.X; X <= Max.X; X += StepVoxels)
					{
						const FVector Normalized(
							X / RadiusVoxels, Y / RadiusVoxels, Z / RadiusVoxels);

						if (Geology.SampleElementNormAtNormalized(ElementIndex, Normalized) <= Threshold)
						{
							continue;
						}

						if (bPlanetCreated)
						{
							const FIntVector Index(
								FMath::RoundToInt(X), FMath::RoundToInt(Y), FMath::RoundToInt(Z));
							if (Planet.GetData().GetValue(Index, /*LOD=*/0).IsEmpty())
							{
								continue;
							}
						}

						++SolidSamples;
					}
				}
			}
		}

		const double VoxelSizeCm = double(Planet.VoxelSize);
		return float(double(SolidSamples) * StepVoxels * StepVoxels * StepVoxels
			* VoxelSizeCm * VoxelSizeCm * VoxelSizeCm);
	}
} // namespace

float UPlanetMineralViewComponent::GetElementVolumeCm3(
	int32 ElementIndex, float StepCm) const
{
	const AVoxelPlanetActor* PlanetPtr = Planet.Get();
	const TSharedPtr<const FPlanetGeologyField>& GeologyPtr = CachedGeology;
	if (!PlanetPtr || !GeologyPtr.IsValid() || !Elements.IsValidIndex(ElementIndex))
	{
		return 0.f;
	}
	return ComputeElementVolumeCm3(
		*GeologyPtr, *PlanetPtr, ElementIndex, StepCm, PlanetPtr->GetSurfaceRadiusCm());
}

TArray<float> UPlanetMineralViewComponent::GetAllElementVolumesCm3(float StepCm) const
{
	TArray<float> Out;
	Out.SetNum(Elements.Num());
	for (int32 Index = 0; Index < Elements.Num(); ++Index)
	{
		Out[Index] = GetElementVolumeCm3(Index, StepCm);
	}
	return Out;
}

bool UPlanetMineralViewComponent::SetElementColor(
	int32 ElementIndex, const FLinearColor& Color)
{
	return ViewActor ? ViewActor->SetElementColor(ElementIndex, Color) : false;
}
