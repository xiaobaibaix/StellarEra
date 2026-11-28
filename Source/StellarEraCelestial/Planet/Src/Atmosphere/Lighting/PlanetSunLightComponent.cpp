#include "PlanetSunLightComponent.h"
#include "VoxelPlanetActor.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/ChildActorComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/LightComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "PlanetAtmosphereComponent.h"
#include "StarBodyComponent.h"
#include "UObject/UnrealType.h"
#include "VoxelPlanetActor.h"

namespace
{
	const TCHAR* const PlanetShadowMaterialPath = TEXT("/Game/Code/Planet/Materials/M_PlanetShadowLightFunction");

	/** 天光补光那盏定向光的标识：与关卡里 Actor 的标签一致。 */
	const TCHAR* const SkyFillTag = TEXT("SkyFill");

	// 光函数参数名，与 M_PlanetShadowLightFunction 里的 Custom 节点保持一致。
	const FName ParamShadowSunDirection(TEXT("SunDirection"));
	const FName ParamShadowPlanetCenterOffset(TEXT("PlanetCenterOffset"));
	const FName ParamShadowPlanetRadius(TEXT("PlanetRadius"));
	const FName ParamShadowPenumbra(TEXT("Penumbra"));
}

UPlanetSunLightComponent::UPlanetSunLightComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	// 编辑器里也要 tick：否则摆星、拖行星时三盏方向光停在初始朝向（全部指向同一处），
	// 只有进 PIE 才对。灯本身有变化检测，静止场景不会逐帧标脏。
	bTickInEditor = true;
}

void UPlanetSunLightComponent::BeginPlay()
{
	Super::BeginPlay();

	// 灯由组件自己生成：代理下不再依赖行星蓝图的 RebuildStarLights。
	if (const UWorld* World = GetWorld(); World && World->IsGameWorld() && bAutoManageLights)
	{
		RebuildSunLights();
	}
}

void UPlanetSunLightComponent::RebuildSunLights()
{
	UWorld* World = GetWorld();
	AActor* Owner = GetOwner();
	if (!World || !Owner)
	{
		return;
	}

	for (ADirectionalLight* Light : SpawnedLights)
	{
		if (IsValid(Light))
		{
			Light->Destroy();
		}
	}
	SpawnedLights.Reset();

	// 重启编辑器后瞬态列表是空的：按挂载关系清掉上一次生成的灯，避免重启后重复。
	TArray<AActor*> AttachedActors;
	Owner->GetAttachedActors(AttachedActors);
	for (AActor* Attached : AttachedActors)
	{
		const bool bOwnClass = StarLightClass
			? Attached && Attached->IsA(StarLightClass.Get())
			: Attached && Attached->IsA(ADirectionalLight::StaticClass());
		if (bOwnClass)
		{
			Attached->Destroy();
		}
	}

	// 显式列表优先；空则按世界里的恒星自动填充。
	TArray<AActor*> Stars;
	for (AActor* Star : SunStars)
	{
		Stars.AddUnique(Star);
	}
	if (Stars.Num() == 0)
	{
		TArray<FPlanetSun> Suns;
		UPlanetAtmosphereComponent::CollectSunsFor(World, Owner->GetActorLocation(), Suns);
		for (const FPlanetSun& Sun : Suns)
		{
			if (Sun.Star && IsValid(Sun.Star->GetOwner()))
			{
				Stars.AddUnique(Sun.Star->GetOwner());
			}
		}
	}

	if (Stars.Num() == 0 || !StarLightClass)
	{
		return;
	}

	// 最近星占主光名额（ForwardShadingPriority / AtmosphereSunLight）。
	AActor* NearestStar = nullptr;
	double NearestDist = TNumericLimits<double>::Max();
	for (AActor* Star : Stars)
	{
		if (!IsValid(Star))
		{
			continue;
		}
		const double Distance = FVector::Dist(Owner->GetActorLocation(), Star->GetActorLocation());
		if (Distance < NearestDist)
		{
			NearestDist = Distance;
			NearestStar = Star;
		}
	}

	const FTransform SpawnTransform = Owner->GetActorTransform();
	for (AActor* Star : Stars)
	{
		if (!IsValid(Star))
		{
			continue;
		}

		ADirectionalLight* Light = World->SpawnActorDeferred<ADirectionalLight>(
			StarLightClass, SpawnTransform, Owner, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Light)
		{
			continue;
		}

		// BP_StarLight 用自己的 StarActor/PlanetActor 变量认星与行星。
		if (FObjectProperty* StarProperty = FindFProperty<FObjectProperty>(Light->GetClass(), TEXT("StarActor")))
		{
			StarProperty->SetObjectPropertyValue_InContainer(Light, Star);
		}
		if (FObjectProperty* PlanetProperty = FindFProperty<FObjectProperty>(Light->GetClass(), TEXT("PlanetActor")))
		{
			PlanetProperty->SetObjectPropertyValue_InContainer(Light, Owner);
		}

		Light->FinishSpawning(SpawnTransform);
		Light->AttachToActor(Owner, FAttachmentTransformRules::KeepWorldTransform);

		if (UDirectionalLightComponent* LightComponent = Cast<UDirectionalLightComponent>(Light->GetLightComponent()))
		{
			const bool bPrimary = (Star == NearestStar);
			LightComponent->SetForwardShadingPriority(bPrimary ? 1 : 0);
			LightComponent->SetAtmosphereSunLight(bPrimary);
		}

		// 显式绑定：恒星驱动它自己那盏灯，不再靠"第一盏未认领"的竞争。
		if (UStarBodyComponent* StarComponent = Star->FindComponentByClass<UStarBodyComponent>())
		{
			StarComponent->SunLight = Light;
		}

		SpawnedLights.Add(Light);
	}

	// 每盏灯刷新一次（BP_StarLight::RefreshLight）。
	for (ADirectionalLight* Light : SpawnedLights)
	{
		if (IsValid(Light))
		{
			if (UFunction* RefreshFunction = Light->FindFunction(TEXT("RefreshLight")))
			{
				Light->ProcessEvent(RefreshFunction, nullptr);
			}
		}
	}

	UE_LOG(LogTemp, Display, TEXT("[星灯] %s 生成 %d 盏方向光（主光 %s）"),
		*Owner->GetName(), SpawnedLights.Num(), *GetNameSafe(NearestStar));
}

#if WITH_EDITOR
void UPlanetSunLightComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (!bAutoManageLights)
	{
		return;
	}

	const FName ChangedProperty = PropertyChangedEvent.GetPropertyName();
	if (ChangedProperty == GET_MEMBER_NAME_CHECKED(UPlanetSunLightComponent, SunStars)
		|| ChangedProperty == GET_MEMBER_NAME_CHECKED(UPlanetSunLightComponent, StarLightClass)
		|| ChangedProperty == GET_MEMBER_NAME_CHECKED(UPlanetSunLightComponent, bAutoManageLights))
	{
		RebuildSunLights();
	}
}
#endif

void UPlanetSunLightComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	UWorld* World = GetWorld();
	const AVoxelPlanetActor* Planet = GetPlanetOwner();
	if (!Planet || !bDrivesSunLights || !World)
	{
		return;
	}

	// 代理上已有驱动组件时，行星自带的这份让位，避免两盏灯互相抢。
	if (const AActor* Parent = GetOwner() ? GetOwner()->GetAttachParentActor() : nullptr)
	{
		if (Parent->FindComponentByClass<UPlanetSunLightComponent>())
		{
			return;
		}
	}

	RefreshLightingNow();
}

void UPlanetSunLightComponent::RefreshLightingNow()
{
	UWorld* World = GetWorld();
	if (!World || !GetPlanetOwner() || !bDrivesSunLights)
	{
		return;
	}

	// 天体是个位数，每次完整收集一遍即可；不缓存，增删恒星时不需要失效通知。
	TArray<FPlanetSun> Suns;
	UPlanetAtmosphereComponent::CollectSunsFor(World, GetOwner()->GetActorLocation(), Suns);
	const bool bLightsChanged = DriveSunLights(Suns);

	// 阴影与「驱动灯」正交：灯不由 C++ 驱动时也必须挂上，否则夜面没有遮挡。
	// 只在灯真的变了（或首帧）时重挂：编辑器里静止场景不逐帧标脏。
	if (bLightsChanged || !bHasAppliedLighting)
	{
		ApplyPlanetShadow();
		ApplySkyFill();
		bHasAppliedLighting = true;
	}
}

const AVoxelPlanetActor* UPlanetSunLightComponent::GetPlanetOwner() const
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return nullptr;
	}

	// 行星自己挂这份组件时就是本体。
	if (const AVoxelPlanetActor* Self = Cast<AVoxelPlanetActor>(Owner))
	{
		return Self;
	}

	// 代理预制体：组件挂在代理上，真正的行星是它下面的子 Actor。
	TArray<UChildActorComponent*> ChildComponents;
	Owner->GetComponents<UChildActorComponent>(ChildComponents);
	for (UChildActorComponent* ChildComponent : ChildComponents)
	{
		if (const AVoxelPlanetActor* Planet =
			ChildComponent ? Cast<AVoxelPlanetActor>(ChildComponent->GetChildActor()) : nullptr)
		{
			return Planet;
		}
	}

	TArray<AActor*> AttachedActors;
	Owner->GetAttachedActors(AttachedActors);
	for (AActor* Attached : AttachedActors)
	{
		if (const AVoxelPlanetActor* Planet = Cast<AVoxelPlanetActor>(Attached))
		{
			return Planet;
		}
	}
	return nullptr;
}

float UPlanetSunLightComponent::DistanceFalloff(float ReferenceDistance, double Distance)
{
	if (ReferenceDistance <= 0.0f)
	{
		return 1.0f;
	}

	const double Ratio = static_cast<double>(ReferenceDistance) / FMath::Max(Distance, 1.0);
	// shortcut: 上限 100 倍，恒星与行星重合时不至于把灯烧爆
	return static_cast<float>(FMath::Clamp(Ratio * Ratio, 0.0, 100.0));
}

UStarBodyComponent* UPlanetSunLightComponent::SelectPrimaryStar(const TArray<FPlanetSun>& Suns)
{
	for (const FPlanetSun& Sun : Suns)
	{
		if (Sun.Star && Sun.Star->bDrivesDirectionalLight)
		{
			return Sun.Star;
		}
	}
	return nullptr;
}

float UPlanetSunLightComponent::SunFalloffFor(const FPlanetSun& Sun) const
{
	const AActor* StarActor = (Sun.Star && Sun.Star->GetOwner()) ? Sun.Star->GetOwner() : nullptr;
	if (!StarActor)
	{
		return 1.0f;
	}

	const float Ref = SunFalloffReferenceDistance;
	return DistanceFalloff(Ref,
		FVector::Dist(StarActor->GetActorLocation(), GetOwner()->GetActorLocation()));
}

bool UPlanetSunLightComponent::DriveSunLights(const TArray<FPlanetSun>& Suns)
{
	UStarBodyComponent* PrimaryStar = SelectPrimaryStar(Suns);

	const FVector PlanetCenter = GetOwner()->GetActorLocation();
	bool bAnyChanged = false;
	for (const FPlanetSun& Sun : Suns)
	{
		if (!Sun.Star || !Sun.Star->bDrivesDirectionalLight)
		{
			continue;
		}
		bAnyChanged |= Sun.Star->DriveSunLight(PlanetCenter, BaseLightIntensity * SunFalloffFor(Sun),
			Sun.Star == PrimaryStar);
	}
	return bAnyChanged;
}

UMaterialInterface* UPlanetSunLightComponent::EnsurePlanetShadowMaterialBase()
{
	if (PlanetShadowBaseMaterial)
	{
		return PlanetShadowBaseMaterial;
	}
	// 生成光函数材质的命令只在编辑器里跑；缺一次就别每帧重试（LoadObject 失败会走包名解析）。
	if (bLoggedMissingPlanetShadowMaterial)
	{
		return nullptr;
	}

	PlanetShadowBaseMaterial = LoadObject<UMaterialInterface>(nullptr, PlanetShadowMaterialPath);
	if (!PlanetShadowBaseMaterial)
	{
		bLoggedMissingPlanetShadowMaterial = true;
		UE_LOG(LogTemp, Warning,
			TEXT("[行星阴影] 找不到 %s —— 在编辑器里跑一次 StellarEra.BuildPlanetShadowLightFunction。")
			TEXT("夜面物体将被直射阳光照亮。"),
			PlanetShadowMaterialPath);
	}

	return PlanetShadowBaseMaterial;
}

UMaterialInstanceDynamic* UPlanetSunLightComponent::GetPlanetShadowMID(ADirectionalLight* Light)
{
	if (!PlanetShadowBaseMaterial || !IsValid(Light))
	{
		return nullptr;
	}

	const TObjectPtr<ADirectionalLight> Key(Light);
	if (const TObjectPtr<UMaterialInstanceDynamic>* Existing = PlanetShadowMIDs.Find(Key))
	{
		return *Existing;
	}

	UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(PlanetShadowBaseMaterial, this);
	if (!MID)
	{
		return nullptr;
	}
	PlanetShadowMIDs.Add(Key, MID);

	return MID;
}

void UPlanetSunLightComponent::ApplyPlanetShadow()
{
	// 定向光阴影图只覆盖相机附近（本关 400 m），而挡住太阳的球体离被照点 17~20 km，
	// 永远进不了阴影图，夜面的飞船仍被直射照亮。这里给每盏定向光挂解析求交的光函数；
	// 遍历所有灯而非被驱动的恒星灯（本关恒星的 bDrivesDirectionalLight 是 false）。
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		return;
	}

	if (!EnsurePlanetShadowMaterialBase())
	{
		return;
	}

	const AVoxelPlanetActor* PlanetActor = Cast<AVoxelPlanetActor>(GetOwner());
	const float PlanetRadiusCm = PlanetActor ? PlanetActor->GetSurfaceRadiusCm() : 0.0f;
	if (PlanetRadiusCm <= 0.0f)
	{
		return;
	}
	// 半影 = 半径的 1%（≈200 m / 0.57°）：球缘给一点软边，太窄会在地平线上出锯齿硬边。
	const float PenumbraCm = PlanetRadiusCm * 0.01f;
	const FVector PlanetCenter = GetOwner()->GetActorLocation();

	// 光函数要的是「渲染时」的球心，这里读到的却永远是上一帧：Cosmos 子系统在
	// UWorld::Tick 末尾才写回 Actor。行星 15 km/s，实测球心偏 1.6 km 且逐帧来回跳，
	// 而半影只有 200 m —— 晨昏线于是整片翻面，就是山体阴影一闪一闪。
	// 相机与行星刚性相连，改传两者之差（同一帧相减，慢的那一帧抵消），着色器里再加回
	// 渲染侧的 CameraPositionWS。
	const APlayerController* PlayerController = World->GetFirstPlayerController();
	const APlayerCameraManager* CameraManager =
		PlayerController ? PlayerController->PlayerCameraManager : nullptr;
	if (!CameraManager)
	{
		// 没有视口就没有光函数可挂，不必纠正。
		return;
	}
	const FVector PlanetCenterOffset = PlanetCenter - CameraManager->GetCameraLocation();

	for (TActorIterator<ADirectionalLight> It(World); It; ++It)
	{
		ADirectionalLight* Light = *It;
		if (!IsValid(Light) || Light->ActorHasTag(SkyFillTag))
		{
			// 补光代表整天穹，不该被行星自遮挡成一块黑影。
			continue;
		}
		ULightComponent* LightComponent = Light->GetLightComponent();
		if (!LightComponent)
		{
			continue;
		}
		UMaterialInstanceDynamic* ShadowMID = GetPlanetShadowMID(Light);
		if (!ShadowMID)
		{
			continue;
		}

		// 定向光的 forward 是光的**传播**方向（恒星 → 行星），太阳在它的反向。
		const FVector DirectionToSun = -Light->GetActorForwardVector();
		ShadowMID->SetVectorParameterValue(ParamShadowSunDirection,
			FLinearColor(DirectionToSun.X, DirectionToSun.Y, DirectionToSun.Z, 0.0f));
		ShadowMID->SetVectorParameterValue(ParamShadowPlanetCenterOffset,
			FLinearColor(PlanetCenterOffset.X, PlanetCenterOffset.Y, PlanetCenterOffset.Z, 0.0f));
		ShadowMID->SetScalarParameterValue(ParamShadowPlanetRadius, PlanetRadiusCm);
		ShadowMID->SetScalarParameterValue(ParamShadowPenumbra, PenumbraCm);

		LightComponent->SetLightFunctionMaterial(ShadowMID);
	}
}

void UPlanetSunLightComponent::ApplySkyFill()
{
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		return;
	}

	if (!SkyFillLight)
	{
		for (TActorIterator<ADirectionalLight> It(World); It; ++It)
		{
			if (It->ActorHasTag(SkyFillTag))
			{
				SkyFillLight = *It;
				break;
			}
		}
		if (!SkyFillLight)
		{
			// 灯得在关卡里摆出来（本项目不用代码生成资产），空手上阵只会留一圈硬边等高线。
			if (!bLoggedMissingSkyFill)
			{
				bLoggedMissingSkyFill = true;
				UE_LOG(LogTemp, Warning,
					TEXT("[星灯] 关卡里没有 SkyFill 标签的定向光，直射光没人补，晨昏线会剩硬边等高线。"));
			}
			return;
		}
	}

	UDirectionalLightComponent* LightComponent =
		Cast<UDirectionalLightComponent>(SkyFillLight->GetLightComponent());
	if (!LightComponent)
	{
		return;
	}
	// 补光不能投影：它代表整天穹，被地形挡出阴影就成了会移动的硬斑，还多一张阴影图。
	LightComponent->SetCastShadows(false);
	// 略偏蓝：瑞利散射后的天光本色，但别过头成蓝紫。
	LightComponent->SetLightColor(FLinearColor(0.62f, 0.75f, 1.0f), /*bSRGB=*/false);
	const AVoxelPlanetActor* Planet = GetPlanetOwner();
	const float Fill = SkyFillIntensity;
	LightComponent->SetIntensity(Fill);

	if (!Planet || SkyFillIntensity <= 0.0f)
	{
		// 0 = 关闭。必须在保证强度已写进去之后才早退，否则关掉会留着上一帧的亮度。
		return;
	}

	const APlayerController* PlayerController = World->GetFirstPlayerController();
	const APlayerCameraManager* CameraManager =
		PlayerController ? PlayerController->PlayerCameraManager : nullptr;
	if (!CameraManager)
	{
		// 没有视口就定不了「当地天顶」，朝向是错的还不如不动。
		return;
	}
	const FVector Up =
		(CameraManager->GetCameraLocation() - GetOwner()->GetActorLocation()).GetSafeNormal();
	if (Up.IsNearlyZero())
	{
		return;
	}

	if (USceneComponent* LightRoot = SkyFillLight->GetRootComponent())
	{
		if (LightRoot->Mobility != EComponentMobility::Movable)
		{
			// 静态灯拒绝运行时转向并逐帧刷 Mobility 警告。
			LightRoot->SetMobility(EComponentMobility::Movable);
		}
	}

	// 定向光的 forward 是**传播**方向：从当地天顶照下 = 反向。
	const FRotator Desired = (-Up).Rotation();
	if (!SkyFillLight->GetActorRotation().Equals(Desired, 0.01f))
	{
		SkyFillLight->SetActorRotation(Desired);
	}
}
