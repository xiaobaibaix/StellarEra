#include "PlanetParamBridge.h"

#include "PlanetAtmosphereComponent.h"
#include "PlanetSkyLightComponent.h"
#include "VoxelComponents/VoxelInvokerComponent.h"
#include "VoxelPlanetActor.h"

void UPlanetParamBridge::ApplyToChild(AActor* Child)
{
	AVoxelPlanetActor* Planet = Cast<AVoxelPlanetActor>(Child);
	if (!Planet)
	{
		return;
	}

	Planet->Modify();
	Planet->VoxelSize = FMath::Max(VoxelSize, 0.01f);
	Planet->RadiusMeters = FMath::Max(RadiusMeters, 1.f);
	// 半径两套单位与 VoxelPlanetActor::PostEditChangeProperty 同一公式。
	Planet->Radius = FMath::Max(16.f, FMath::RoundToFloat(
		Planet->RadiusMeters * 100.f / Planet->VoxelSize));
	Planet->RadiusMeters = Planet->Radius * Planet->VoxelSize / 100.f;
	Planet->GeoRecipe = GeoRecipe;
	Planet->CacheKey = CacheKey;
	Planet->MaxLOD = MaxLOD;

	if (Planet->LODInvoker)
	{
		Planet->LODInvoker->LODToSet = InvokerLODToSet;
		Planet->LODInvoker->LODRange = InvokerLODRange;
	}

	if (UPlanetAtmosphereComponent* Atmosphere = Planet->FindComponentByClass<UPlanetAtmosphereComponent>())
	{
		Atmosphere->AtmosphereHeightRatio = AtmosphereHeightRatio;
		Atmosphere->RayleighScaleHeightRatio = RayleighScaleHeightRatio;
		Atmosphere->MieScaleHeightRatio = MieScaleHeightRatio;
		Atmosphere->AtmosphereAmbient = AtmosphereAmbient;
		Atmosphere->SunIntensity = SunIntensity;
		Atmosphere->DragAccelerationAtKmPerSecond = DragAccelerationAtKmPerSecond;
		Atmosphere->bAtmosphereEnabled = bAtmosphereEnabled;
		Atmosphere->bCloudsEnabled = bCloudsEnabled;
		Atmosphere->CloudBaseRatio = CloudBaseRatio;
		Atmosphere->CloudTopRatio = CloudTopRatio;
		Atmosphere->CloudCoverage = CloudCoverage;
		Atmosphere->CloudOpticalDepth = CloudOpticalDepth;
		Atmosphere->CloudNoiseScale = CloudNoiseScale;
		Atmosphere->CloudDetailScale = CloudDetailScale;
		Atmosphere->CloudDetailStrength = CloudDetailStrength;
		Atmosphere->CloudDriftPerSecond = CloudDriftPerSecond;
		Atmosphere->CloudBrightness = CloudBrightness;

#if WITH_EDITOR
		// 直接写属性绕过了组件的 PostEditChangeProperty：壳的缩放与整批材质参数
		// 只有 RefreshAtmosphereGeometry 会重算，不调就会「值变了、画面不变」。
		Atmosphere->RefreshAtmosphereGeometry();
#endif
	}

	if (UPlanetSkyLightComponent* SkyLight = Planet->FindComponentByClass<UPlanetSkyLightComponent>())
	{
		SkyLight->CubeResolution = CubeResolution;
		SkyLight->UpdateIntervalSeconds = UpdateIntervalSeconds;
		SkyLight->SpaceSkyRadiance = SpaceSkyRadiance;
		SkyLight->BrightnessScale = BrightnessScale;
		SkyLight->bUpdateInEditorWorld = bUpdateInEditorWorld;

#if WITH_EDITOR
		// 同上：天光按自己的节流 Tick 重算，参数改了要立刻推一次，否则要等下一次节流。
		SkyLight->RefreshNow();
#endif
	}
}

void UPlanetParamBridge::PullFromChild(AActor* Child)
{
	const AVoxelPlanetActor* Planet = Cast<AVoxelPlanetActor>(Child);
	if (!Planet)
	{
		return;
	}

	Modify();
	RadiusMeters = Planet->RadiusMeters;
	VoxelSize = Planet->VoxelSize;
	GeoRecipe = Planet->GeoRecipe;
	CacheKey = Planet->CacheKey;
	MaxLOD = Planet->MaxLOD;
	if (Planet->LODInvoker)
	{
		InvokerLODToSet = Planet->LODInvoker->LODToSet;
		InvokerLODRange = Planet->LODInvoker->LODRange;
	}

	if (const UPlanetAtmosphereComponent* Atmosphere = Planet->FindComponentByClass<UPlanetAtmosphereComponent>())
	{
		AtmosphereHeightRatio = Atmosphere->AtmosphereHeightRatio;
		RayleighScaleHeightRatio = Atmosphere->RayleighScaleHeightRatio;
		MieScaleHeightRatio = Atmosphere->MieScaleHeightRatio;
		AtmosphereAmbient = Atmosphere->AtmosphereAmbient;
		SunIntensity = Atmosphere->SunIntensity;
		DragAccelerationAtKmPerSecond = Atmosphere->DragAccelerationAtKmPerSecond;
		bAtmosphereEnabled = Atmosphere->bAtmosphereEnabled;
		bCloudsEnabled = Atmosphere->bCloudsEnabled;
		CloudBaseRatio = Atmosphere->CloudBaseRatio;
		CloudTopRatio = Atmosphere->CloudTopRatio;
		CloudCoverage = Atmosphere->CloudCoverage;
		CloudOpticalDepth = Atmosphere->CloudOpticalDepth;
		CloudNoiseScale = Atmosphere->CloudNoiseScale;
		CloudDetailScale = Atmosphere->CloudDetailScale;
		CloudDetailStrength = Atmosphere->CloudDetailStrength;
		CloudDriftPerSecond = Atmosphere->CloudDriftPerSecond;
		CloudBrightness = Atmosphere->CloudBrightness;
	}

	if (const UPlanetSkyLightComponent* SkyLight = Planet->FindComponentByClass<UPlanetSkyLightComponent>())
	{
		CubeResolution = SkyLight->CubeResolution;
		UpdateIntervalSeconds = SkyLight->UpdateIntervalSeconds;
		SpaceSkyRadiance = SkyLight->SpaceSkyRadiance;
		BrightnessScale = SkyLight->BrightnessScale;
		bUpdateInEditorWorld = SkyLight->bUpdateInEditorWorld;
	}
}

bool UPlanetParamBridge::SupportsChild(const AActor* Child) const
{
	return Cast<AVoxelPlanetActor>(Child) != nullptr;
}
