// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetMineralViewComponent.h"

#include "PlanetAtmosphereComponent.h"
#include "PlanetGeologyField.h"
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
	Params.GridResolution = GridResolution;
	// 等值线跟地表着色共用一份参数：矿体边界与地表明暗是同一套判据。
	Params.IsoValue = Geology->GetAppearance().ColorThreshold;

	// 键里不含 IsoValue / VoxelSizeCm / 元素表：它们都来自地质场，而地质场**不在键里**。
	// ReloadGeology()（含 StellarEra.ReloadGeology 命令）会用新的地质对象替换旧的，而行星、
	// 半径、缩放全不变 —— 只比键会静默复用旧几何，再按新元素表上色（同元素数），
	// 或直接让观察体的「网格数 == 元素数」校验失败（不同元素数）。所以另外单独比对
	// 地质对象本身：共享指针相等才算同一份数据。GridResolution 是组件上可编辑的属性，
	// 必须进键。
	PlanetMineralSurface::FCacheKey Key;
	Key.Planet = InPlanet;
	Key.SurfaceRadiusCm = SurfaceRadiusCm;
	Key.Scale = InPlanet->GetActorScale3D();
	Key.GridResolution = GridResolution;

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

	UE_LOG(LogTemp, Display, TEXT("[矿物] %s：网格 %d³（%d 个角点），%d 个三角形，构建 %.1f ms%s"),
		*InPlanet->GetName(), GridResolution, CachedSurface.GridPoints, TotalTriangles,
		CachedSurface.BuildSeconds * 1000.0,
		bReused ? TEXT("（复用缓存，耗时为首建）") : TEXT(""));
	UE_LOG(LogTemp, Display, TEXT("[矿物] %s：每元素矿体 %s"),
		*InPlanet->GetName(),
		PerElementParts.IsEmpty() ? TEXT("(无元素)") : *FString::Join(PerElementParts, TEXT(" ")));

	FActorSpawnParameters SpawnParams;
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

	bActive = true;
	SetComponentTickEnabled(true);
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

	if (AVoxelPlanetActor* RestorePlanet = Planet.Get())
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
