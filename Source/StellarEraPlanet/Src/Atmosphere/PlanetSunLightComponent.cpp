#include "PlanetSunLightComponent.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/LightComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "PlanetAtmosphereComponent.h"
#include "StarBodyComponent.h"
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
}

void UPlanetSunLightComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	UWorld* World = GetWorld();
	if (!bDrivesSunLights || !World || !World->IsGameWorld())
	{
		// 编辑器视口不转灯：每帧转灯会把关卡一直标脏。
		return;
	}

	// 天体是个位数，每帧完整收集一次即可；不缓存，增删恒星时不需要失效通知。
	TArray<FPlanetSun> Suns;
	UPlanetAtmosphereComponent::CollectSunsFor(World, GetOwner()->GetActorLocation(), Suns);
	DriveSunLights(Suns);

	// 阴影与「驱动灯」正交：灯不由 C++ 驱动时也必须挂上，否则夜面没有遮挡。
	ApplyPlanetShadow();
	ApplySkyFill();
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

	return DistanceFalloff(SunFalloffReferenceDistance,
		FVector::Dist(StarActor->GetActorLocation(), GetOwner()->GetActorLocation()));
}

void UPlanetSunLightComponent::DriveSunLights(const TArray<FPlanetSun>& Suns)
{
	UStarBodyComponent* PrimaryStar = SelectPrimaryStar(Suns);

	const FVector PlanetCenter = GetOwner()->GetActorLocation();
	for (const FPlanetSun& Sun : Suns)
	{
		if (!Sun.Star || !Sun.Star->bDrivesDirectionalLight)
		{
			continue;
		}
		Sun.Star->DriveSunLight(PlanetCenter, BaseLightIntensity * SunFalloffFor(Sun),
			Sun.Star == PrimaryStar);
	}
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
	LightComponent->SetIntensity(SkyFillIntensity);

	if (SkyFillIntensity <= 0.0f)
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
