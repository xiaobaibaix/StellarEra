#include "StarBodyComponent.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"

namespace
{
	// G2V 预设的 Brightness（Source/StellarEraPlanet/Src/Star/SunMaterialBuilder.cpp 的
	// FSolarPreset 表）。相对亮度的标定基准：G2V = 1.0。
	constexpr float ReferenceBrightness = 150.0f;
	constexpr float MinRelativeBrightness = 0.05f;
	constexpr float MaxRelativeBrightness = 4.0f;

	/** 归一化时"算黑"的门槛，避免除以一个 float 噪声。 */
	constexpr float BlackColorThreshold = 1e-4f;
}

UStarBodyComponent::UStarBodyComponent()
{
	// 只存参数，不参与 Tick。
	PrimaryComponentTick.bCanEverTick = false;
}

FLinearColor UStarBodyComponent::NormalizeSunColor(FLinearColor Color)
{
	const float MaxComponent = FMath::Max3(Color.R, Color.G, Color.B);
	if (MaxComponent <= BlackColorThreshold)
	{
		return FLinearColor::White;
	}
	return FLinearColor(Color.R / MaxComponent, Color.G / MaxComponent, Color.B / MaxComponent, 1.0f);
}

float UStarBodyComponent::BrightnessToRelative(float Brightness)
{
	return FMath::Clamp(Brightness / ReferenceBrightness, MinRelativeBrightness, MaxRelativeBrightness);
}

void UStarBodyComponent::RefreshSunParameters() const
{
	const AActor* Owner = GetOwner();
	const UStaticMeshComponent* Mesh = Owner ? Owner->FindComponentByClass<UStaticMeshComponent>() : nullptr;
	const UMaterialInterface* Material = Mesh ? Mesh->GetMaterial(0) : nullptr;

	if (bSunParametersValid && Material == CachedColorSource.Get())
	{
		return;
	}

	CachedColorSource = Material;
	bSunParametersValid = true;
	CachedSunColor = FLinearColor::White;
	CachedRelativeBrightness = 1.0f;

	if (!Material)
	{
		return;
	}

	// UMaterialInstance 覆写了 GetParameterValue，取到的是实例覆盖后的值，不是父材质默认值。
	FLinearColor HotColor = FLinearColor::White;
	if (Material->GetVectorParameterValue(TEXT("HotColor"), HotColor))
	{
		CachedSunColor = NormalizeSunColor(HotColor);
	}

	float Brightness = ReferenceBrightness;
	if (Material->GetScalarParameterValue(TEXT("Brightness"), Brightness))
	{
		CachedRelativeBrightness = BrightnessToRelative(Brightness);
	}
}

FLinearColor UStarBodyComponent::GetSunColor() const
{
	RefreshSunParameters();
	return CachedSunColor;
}

float UStarBodyComponent::GetRelativeBrightness() const
{
	RefreshSunParameters();
	return CachedRelativeBrightness;
}

void UStarBodyComponent::FindAll(const UWorld* World, TArray<UStarBodyComponent*>& OutStars)
{
	OutStars.Reset();
	if (!World)
	{
		return;
	}

	// TActorIterator 只收非 const UWorld*。这里不改世界，const_cast 是有意的。
	for (TActorIterator<AActor> It(const_cast<UWorld*>(World)); It; ++It)
	{
		if (UStarBodyComponent* Star = It->FindComponentByClass<UStarBodyComponent>())
		{
			OutStars.Add(Star);
		}
	}
}

UStarBodyComponent* UStarBodyComponent::FindNearest(const UWorld* World, const FVector& FromLocation)
{
	TArray<UStarBodyComponent*> Stars;
	FindAll(World, Stars);

	UStarBodyComponent* Nearest = nullptr;
	double NearestSquared = TNumericLimits<double>::Max();
	for (UStarBodyComponent* Star : Stars)
	{
		const AActor* Owner = Star ? Star->GetOwner() : nullptr;
		if (!Owner)
		{
			continue;
		}
		const double Squared = FVector::DistSquared(Owner->GetActorLocation(), FromLocation);
		if (Squared < NearestSquared)
		{
			NearestSquared = Squared;
			Nearest = Star;
		}
	}
	return Nearest;
}

ADirectionalLight* UStarBodyComponent::ResolveSunLight()
{
	if (IsValid(SunLight))
	{
		return SunLight;
	}
	if (IsValid(AutoSunLight))
	{
		return AutoSunLight;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	TArray<UStarBodyComponent*> Stars;
	FindAll(World, Stars);

	for (TActorIterator<ADirectionalLight> It(World); It; ++It)
	{
		ADirectionalLight* Candidate = *It;
		if (!IsValid(Candidate))
		{
			continue;
		}

		bool bClaimed = false;
		for (const UStarBodyComponent* Other : Stars)
		{
			if (Other == this)
			{
				continue;
			}
			// 显式绑定与已缓存的自动认领都算"被占用"，否则两颗星会抢同一盏灯。
			if (Other->SunLight == Candidate || Other->AutoSunLight == Candidate)
			{
				bClaimed = true;
				break;
			}
		}
		if (bClaimed)
		{
			continue;
		}

		AutoSunLight = Candidate;
		return Candidate;
	}

	return nullptr;
}

void UStarBodyComponent::DriveSunLight(const FVector& PlanetCenter, float BaseIntensity, bool bIsPrimary)
{
	ADirectionalLight* Light = ResolveSunLight();
	if (!Light)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		// 编辑器视口不转灯：每帧转灯会把关卡一直标脏。
		return;
	}

	const AActor* Owner = GetOwner();
	const FVector StarLocation = Owner ? Owner->GetActorLocation() : FVector::ZeroVector;
	const FVector Travel = (PlanetCenter - StarLocation).GetSafeNormal();

	// Stationary / Static 的组件拒绝运行时转向，并逐帧刷 "Mobility must be 'Movable'"。
	// 关卡里这些灯是静态摆放的，这里切一次。
	if (USceneComponent* LightRoot = Light->GetRootComponent())
	{
		if (LightRoot->Mobility != EComponentMobility::Movable)
		{
			LightRoot->SetMobility(EComponentMobility::Movable);
		}
	}

	if (!Travel.IsNearlyZero())
	{
		Light->SetActorRotation(Travel.Rotation());
	}

	UDirectionalLightComponent* LightComponent = Cast<UDirectionalLightComponent>(Light->GetLightComponent());
	if (!LightComponent)
	{
		return;
	}

	RefreshSunParameters();
	LightComponent->SetLightColor(CachedSunColor, /*bSRGB=*/false);
	LightComponent->SetIntensity(BaseIntensity * CachedRelativeBrightness);

	// 边沿触发：引擎那条"多个定向光源竞争"的警告只看这两项，
	// 每帧重写没有意义，还会无谓地标脏。
	const int32 Priority = bIsPrimary ? 1 : 0;
	if (AppliedForwardShadingPriority != Priority)
	{
		AppliedForwardShadingPriority = Priority;
		LightComponent->SetForwardShadingPriority(Priority);
	}
	if (bAppliedAtmosphereSunLight != bIsPrimary)
	{
		bAppliedAtmosphereSunLight = bIsPrimary;
		LightComponent->SetAtmosphereSunLight(bIsPrimary);
	}
}
