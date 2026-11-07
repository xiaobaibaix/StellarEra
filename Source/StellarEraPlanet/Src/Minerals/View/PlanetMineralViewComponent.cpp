// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetMineralViewComponent.h"

#include "PlanetAtmosphereComponent.h"
#include "PlanetGeologyField.h"
#include "PlanetGeologyPreset.h"
#include "PlanetMineralViewActor.h"
#include "PlanetMineralVoxelSampling.h"
#include "PlanetMineralVolume.h"
#include "VoxelComponents/VoxelInvokerComponent.h"
#include "VoxelMineralFieldActor.h"
#include "VoxelPlanetActor.h"
#include "VoxelViewpointInvokerUtils.h"

#include "Engine/World.h"

namespace
{
	/**
	 * 把地质场翻成每个元素一份的采样参数。回调捕获共享指针而非裸指针：地质场可能被 ReloadGeology 换掉，
	 * 共享指针保证旧对象不会在生成器还在用时被释放。
	 */
	TArray<FPlanetMineralVoxelParams> MakeVoxelParams(
		AVoxelPlanetActor* Planet,
		const TSharedPtr<const FPlanetGeologyField>& Geology,
		const TArray<FPlanetGeologyElement>& Elements,
		float SurfaceRadiusCm)
	{
		TArray<FPlanetMineralVoxelParams> Out;
		Out.SetNum(Elements.Num());

		// 共享指针只取一次：回调跑在工作线程上，按体素回弱指针解析行星是每格开销；TVoxelSharedPtr 线程安全，
		// 也让 FVoxelData（连同生成器实例）活过行星 Actor 的更早一次销毁。
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

	/**
	 * 逐元素统计矿量。走解析场：与相机、LOD、体素世界无关；采样在归一化空间，区域 AABB 与步长同乘 RadiusVoxels。
	 * 重叠盒交给 CountMergedBoxSamples 去重（脉相邻段 AABB 必然重叠，逐盒各扫会让矿量系统性偏高）。
	 * 代价 O((并集 AABB 边长 / 步长)³)，不夹步长；调用方必须传米级粗步长。
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

		TArray<FPlanetMineralRegionBox> Boxes;
		Boxes.Reserve(Regions.Num());
		for (const FPlanetGeologyRegion& Region : Regions)
		{
			if (!Region.Bounds.IsValid)
			{
				continue;
			}
			FPlanetMineralRegionBox& Box = Boxes.AddDefaulted_GetRef();
			Box.Min = FVector3d(Region.Bounds.Min) * RadiusVoxels;
			Box.Max = FVector3d(Region.Bounds.Max) * RadiusVoxels;
		}

		const bool bPlanetCreated = Planet.IsCreated();
		const int64 SolidSamples = CountMergedBoxSamples(Boxes, StepVoxels,
			[&Geology, &Planet, ElementIndex, Threshold, RadiusVoxels, bPlanetCreated]
			(const FVector3d& Point) -> bool
			{
				const FVector Normalized(
					Point.X / RadiusVoxels, Point.Y / RadiusVoxels, Point.Z / RadiusVoxels);

				if (Geology.SampleElementNormAtNormalized(ElementIndex, Normalized) <= Threshold)
				{
					return false;
				}

				if (bPlanetCreated)
				{
					const FIntVector Index(
						FMath::RoundToInt(Point.X),
						FMath::RoundToInt(Point.Y),
						FMath::RoundToInt(Point.Z));
					if (Planet.GetData().GetValue(Index, /*LOD=*/0).IsEmpty())
					{
						return false;
					}
				}

				return true;
			});

		const double VoxelSizeCm = double(Planet.VoxelSize);
		return float(double(SolidSamples) * StepVoxels * StepVoxels * StepVoxels
			* VoxelSizeCm * VoxelSizeCm * VoxelSizeCm);
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

	// 订阅宿主行星的「预设指针换了」并先对齐一次：镜像过期会让面板显示行星上已不用的数据，按它写回还会覆盖旧值。
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
	// 宿主不是行星时置空而非保留旧值：留一份无主资产，下次按它写回会写到看似成立、其实没人读的地方。
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

	// 只有面板改槽才需写回。撤销粒度：事务已记下组件，Ctrl+Z 会还原组件那份、行星那份未必跟着回 —— 已知取舍，换磁盘上一处真值。
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
	// 这次重建的就是当下这份预设，过期标记随之清掉；构建失败时预览也没了，留着只会让人以为还有旧矿体可看。
	bPreviewStale = false;
#endif

	if (!InPlanet)
	{
		return false;
	}

	// 进来先对齐镜像：InPlanet 未必是 Owner，而面板槽要显示「这次到底用了哪一份」。
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

	// 编辑器预览世界（BP 编辑器预览、资产预览场景）不是关卡世界：临时隐藏与包脏度语义都不成立，且掉进运行时分支会翻 UPROPERTY。
	if (World->WorldType == EWorldType::EditorPreview)
	{
		UE_LOG(LogTemp, Warning, TEXT("[矿物] 编辑器预览世界不支持矿物预览：%s"),
			*GetNameSafe(GetOwner()));
		return false;
	}

	// 用 WorldType 判编辑器世界 vs PIE，不用 GEditor->IsPlaySessionInProgress；非编辑器构建里常量折叠成 false，
	// 也避开 AActor 上只在 WITH_EDITOR 下存在的临时隐藏 API。
#if WITH_EDITOR
	const bool bInEditorWorld = World->WorldType == EWorldType::Editor;
#else
	const bool bInEditorWorld = false;
#endif

	const float SurfaceRadiusCm = InPlanet->GetSurfaceRadiusCm();
	// 地质深度以体素计，换回厘米用单个体素的厘米尺寸；明确假设行星 Actor 的等比缩放 S = 1（换算不含 Actor 缩放）。
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

	// 键里不含 IsoValue / VoxelSizeCm / 元素表：它们来自地质场，地质场不在键里。ReloadGeology() 换新对象时行星与半径不变，
	// 只比键会复用旧几何，所以还要比对地质对象。CellsPerFeature 可编辑，必须进键。
	PlanetMineralSurface::FCacheKey Key;
	Key.Planet = InPlanet;
	Key.SurfaceRadiusCm = SurfaceRadiusCm;
	Key.Scale = InPlanet->GetActorScale3D();
	Key.CellsPerFeature = CellsPerFeature;

	// 采样元素表与两条渲染路径无关：图例、数字键、矿体下标都用它。
	const TArray<FPlanetGeologyElement> SampledElements =
		Geology->GetSampledElementDefinitions();

	// marching cubes 的几何与诊断只在走那条路时才做；体素路径用不到，留着等于每次 Activate 白付 ~1.4 s / 3.76 M 三角形。
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

		// 每元素「占据格数 / 面数」：诊断「全都没生成」与「某个元素为 0」是两回事。顺序 = 运行期采样顺序。
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

		// 并行效率：JobSeconds/BuildSeconds ≈ 实际核数，接近 1 = 有个大任务卡死并行度；CappedPairs 非 0 = 几何被降采样。
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
		// 守卫必须在关调用器之前：早退时还没动过任何状态。必须在这里判是因为下面的 GetData() 解引用 Data，
		// 而 Data 只在 CreateWorldInternal 里创建；编辑器世界里 Toggle() 会留下「有地质场、Data 为空」的状态。
		if (!InPlanet->IsCreated())
		{
			UE_LOG(LogTemp, Error,
				TEXT("[矿物] 行星体素世界尚未创建，无法进入体素视图：%s"),
				*InPlanet->GetName());
			SpawnedView->Destroy();
			return false;
		}

		// 体素路径要求行星自己停下来：视图期间没有挖掘；停掉它也让下面长读锁的等待有界（只在飞的少量块会竞争）。
		bDisabledPlanetInvoker = false;
		if (UVoxelSimpleInvokerComponent* PlanetInvoker =
			InPlanet->FindComponentByClass<UVoxelSimpleInvokerComponent>())
		{
			if (PlanetInvoker->IsInvokerEnabled())
			{
				VoxelViewpointInvokers::SetEnabled(PlanetInvoker, false);
				// SetEnabled 对未注册组件是空操作，读回确认真的关掉了：没关成就不该在 Deactivate 里替别人打开。
				bDisabledPlanetInvoker = !PlanetInvoker->IsInvokerEnabled();
			}
		}

		// 读锁必须在建世之前拿：生成器一 CreateWorld 就在工作线程上读行星数据。锁整颗星的 [-Size, Size) 而非 [0, Size)：
		// 八叉树 Get 的 ensureThreadSafe 只看实际走到的树，没锁的那半个世界等于没保护。
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

		// 先放锁再恢复调用器：反过来行星一恢复就开始写数据而我们还持着读锁 —— 真死锁。失败路径 Planet 未赋值，用 InPlanet。
		RestorePlanetInvoker(InPlanet);
		return false;
	}

	Planet = InPlanet;
	CachedGeology = Geology;
	Elements = SampledElements;

	bEditorPreview = bInEditorWorld;

	if (bEditorPreview)
	{
		// 编辑器分支不改任何 UPROPERTY（翻 bAtmosphereEnabled / bActorEnableCollision 会弄脏关卡），临时隐藏是非序列化状态。
		// 这两个 API 在 AActor 上是 WITH_EDITOR 专有，非编辑器构建里 bEditorPreview 恒为 false，不会走到。
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

	// 先放锁再恢复行星调用器：反过来行星一恢复就开始写数据而我们还持着读锁 —— 真死锁。
	PlanetDataLock.Reset();

#if WITH_EDITOR
	if (PresetChangedHandle.IsValid())
	{
		// 从「订上的那一份」退订：预览期间换过 GeologyAsset 的话，用当下的资产会退错对象，旧资产上留下悬空订阅。
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
		// 与 bEditorPreview 无关：Activate 无条件关调用器，这里就无条件恢复。bIsInvokerEnabled 不是 UPROPERTY，不脏关卡。
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
					// Refresh 末尾会 SetVisibility(true)：先刷新、再恢复进入前的可见性，否则「enabled 但被手动隐藏」的壳会被强行显示。
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
		// 不是我们关的（或没关成）：进入时本来就关，退出时别打开 —— 那不是我们改的状态。
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

	// 只跟位置：N 体只平移，跟着转会把矿体拧歪；体素路径还要把每个矿物体素世界一起搬（它们不 attach 到观察体）。
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
	// 与进入预览同一条路，不另写一套：Activate 开头先拆旧观察体，缓存按地质对象判等，预设改过必然重建。
	EnterMineralPreviewInEditor();
}

bool UPlanetMineralViewComponent::MarkPreviewStale()
{
	if (bPreviewStale)
	{
		return false;
	}
	bPreviewStale = true;

	// 刻意不重建：一次重建耗时随 CellsPerFeature 平方增长，而 Details 拖一次数值框会连发十几次改动 ——
	// 自动重建会让编辑器全程无响应，中间结果也没人看。
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

	// 只标脏不重建（理由见 MarkPreviewStale），重建由 RefreshMineralPreview 显式触发。
	MarkPreviewStale();
}
#endif

float UPlanetMineralViewComponent::GetElementVolumeCm3(
	int32 ElementIndex, float StepCm) const
{
	const AVoxelPlanetActor* PlanetPtr = Planet.Get();
	const TSharedPtr<const FPlanetGeologyField>& GeologyPtr = CachedGeology;
	if (!PlanetPtr || !GeologyPtr.IsValid() || !Elements.IsValidIndex(ElementIndex))
	{
		return 0.f;
	}

	// 体素路径下 Activate 已持有覆盖整个视图期的读锁，这里不能再拿（同线程重复加读锁会 checkf）；
	// MC 路径没人拿锁而行星块生成线程还在写同一棵树，就地补一把。
	TUniquePtr<FVoxelReadScopeLock> LocalLock;
	if (!PlanetDataLock && PlanetPtr->IsCreated())
	{
		LocalLock = MakeUnique<FVoxelReadScopeLock>(
			PlanetPtr->GetData(), PlanetPtr->GetWorldBounds(),
			TEXT("PlanetMineralStats"));
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
