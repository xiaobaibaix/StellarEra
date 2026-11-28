#pragma once

#include "CoreMinimal.h"
#include "ChildActorParamBridge.h"
#include "PlanetParamBridge.generated.h"

class UPlanetRecipe;

/**
 * 体素行星的参数桥：形状 / 地质 / 渲染 / 大气 / 云 / 天光。
 * 参数写在桥上而不是组件上，换子 Actor 类型只需要换一个桥子类。
 */
UCLASS(EditInlineNew, DefaultToInstanced)
class STELLARERACELESTIAL_API UPlanetParamBridge : public UChildActorParamBridge
{
	GENERATED_BODY()

public:
	// ── Shape ──
	UPROPERTY(EditAnywhere, Category = "Planet|Shape", meta = (DisplayName = "RadiusMeters", ClampMin = "1.0"))
	float RadiusMeters = 1000.f;

	UPROPERTY(EditAnywhere, Category = "Planet|Shape", meta = (DisplayName = "VoxelSize", ClampMin = "0.01"))
	float VoxelSize = 100.f;

	// ── Geo ──
	UPROPERTY(EditAnywhere, Category = "Planet|Geo", meta = (DisplayName = "GeoRecipe"))
	TObjectPtr<UPlanetRecipe> GeoRecipe;

	UPROPERTY(EditAnywhere, Category = "Planet|Geo", meta = (DisplayName = "CacheKey"))
	FString CacheKey;

	// ── Render ──
	UPROPERTY(EditAnywhere, Category = "Planet|Render", meta = (DisplayName = "MaxLOD", ClampMin = "0", ClampMax = "26"))
	int32 MaxLOD = 6;

	UPROPERTY(EditAnywhere, Category = "Planet|Render", meta = (DisplayName = "InvokerLODToSet", ClampMin = "0", ClampMax = "26"))
	int32 InvokerLODToSet = 0;

	UPROPERTY(EditAnywhere, Category = "Planet|Render", meta = (DisplayName = "InvokerLODRange", ClampMin = "0"))
	float InvokerLODRange = 1000.f;

	// ── Atmosphere ──
	UPROPERTY(EditAnywhere, Category = "Planet|Atmosphere", meta = (DisplayName = "AtmosphereHeightRatio", ClampMin = "0.0"))
	float AtmosphereHeightRatio = 0.09f;

	UPROPERTY(EditAnywhere, Category = "Planet|Atmosphere", meta = (DisplayName = "RayleighScaleHeightRatio", ClampMin = "0.0"))
	float RayleighScaleHeightRatio = 0.008f;

	UPROPERTY(EditAnywhere, Category = "Planet|Atmosphere", meta = (DisplayName = "MieScaleHeightRatio", ClampMin = "0.0"))
	float MieScaleHeightRatio = 0.0015f;

	UPROPERTY(EditAnywhere, Category = "Planet|Atmosphere", meta = (DisplayName = "AtmosphereAmbient", ClampMin = "0.0"))
	float AtmosphereAmbient = 0.02f;

	UPROPERTY(EditAnywhere, Category = "Planet|Atmosphere", meta = (DisplayName = "SunIntensity"))
	FLinearColor SunIntensity = FLinearColor(14.0f, 14.0f, 14.0f, 1.0f);

	UPROPERTY(EditAnywhere, Category = "Planet|Atmosphere", meta = (DisplayName = "DragAccelerationAtKmPerSecond", ClampMin = "0.0"))
	float DragAccelerationAtKmPerSecond = 10.0f;

	UPROPERTY(EditAnywhere, Category = "Planet|Atmosphere", meta = (DisplayName = "AtmosphereEnabled"))
	bool bAtmosphereEnabled = true;

	// ── Clouds ──
	UPROPERTY(EditAnywhere, Category = "Planet|Clouds", meta = (DisplayName = "CloudsEnabled"))
	bool bCloudsEnabled = true;

	UPROPERTY(EditAnywhere, Category = "Planet|Clouds", meta = (DisplayName = "CloudBaseRatio", ClampMin = "0.0"))
	float CloudBaseRatio = 0.02f;

	UPROPERTY(EditAnywhere, Category = "Planet|Clouds", meta = (DisplayName = "CloudTopRatio", ClampMin = "0.0"))
	float CloudTopRatio = 0.07f;

	UPROPERTY(EditAnywhere, Category = "Planet|Clouds", meta = (DisplayName = "CloudCoverage", ClampMin = "0.0", ClampMax = "1.0"))
	float CloudCoverage = 0.45f;

	UPROPERTY(EditAnywhere, Category = "Planet|Clouds", meta = (DisplayName = "CloudOpticalDepth", ClampMin = "0.0"))
	float CloudOpticalDepth = 6.0f;

	UPROPERTY(EditAnywhere, Category = "Planet|Clouds", meta = (DisplayName = "CloudNoiseScale", ClampMin = "0.0"))
	float CloudNoiseScale = 8.0f;

	UPROPERTY(EditAnywhere, Category = "Planet|Clouds", meta = (DisplayName = "CloudDetailScale", ClampMin = "0.0"))
	float CloudDetailScale = 24.0f;

	UPROPERTY(EditAnywhere, Category = "Planet|Clouds", meta = (DisplayName = "CloudDetailStrength", ClampMin = "0.0", ClampMax = "1.0"))
	float CloudDetailStrength = 0.35f;

	UPROPERTY(EditAnywhere, Category = "Planet|Clouds", meta = (DisplayName = "CloudDriftPerSecond"))
	float CloudDriftPerSecond = 0.02f;

	UPROPERTY(EditAnywhere, Category = "Planet|Clouds", meta = (DisplayName = "CloudBrightness", ClampMin = "0.0"))
	float CloudBrightness = 1.0f;

	// ── SkyLight ──
	UPROPERTY(EditAnywhere, Category = "Planet|SkyLight", meta = (DisplayName = "CubeResolution", ClampMin = "4", ClampMax = "64"))
	int32 CubeResolution = 16;

	UPROPERTY(EditAnywhere, Category = "Planet|SkyLight", meta = (DisplayName = "UpdateIntervalSeconds", ClampMin = "0.0"))
	float UpdateIntervalSeconds = 0.2f;

	UPROPERTY(EditAnywhere, Category = "Planet|SkyLight", meta = (DisplayName = "SpaceSkyRadiance"))
	FLinearColor SpaceSkyRadiance = FLinearColor(0.008f, 0.010f, 0.020f, 1.0f);

	UPROPERTY(EditAnywhere, Category = "Planet|SkyLight", meta = (DisplayName = "BrightnessScale", ClampMin = "0.0"))
	float BrightnessScale = 1.0f;

	UPROPERTY(EditAnywhere, Category = "Planet|SkyLight", meta = (DisplayName = "UpdateInEditorWorld"))
	bool bUpdateInEditorWorld = true;

	//~ Begin UChildActorParamBridge Interface
	virtual void ApplyToChild(AActor* Child) override;
	virtual void PullFromChild(AActor* Child) override;
	virtual bool SupportsChild(const AActor* Child) const override;
	//~ End UChildActorParamBridge Interface
};
