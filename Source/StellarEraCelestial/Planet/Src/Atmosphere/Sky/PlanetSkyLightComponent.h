#pragma once

#include "CoreMinimal.h"
#include "Components/SkyLightComponent.h"
#include "PlanetSkyModel.h"
// FPlanetSun：CollectSunsFor 的复用缓冲要完整类型。
#include "PlanetAtmosphereComponent.h"
#include "PlanetSkyLightComponent.generated.h"

class UTextureCube;
class FObjectPreSaveContext;

/**
 * 行星天光本体：自己就是那盏 USkyLightComponent，同时按玩家视角在 CPU 上积分出低分辨率
 * 天空立方图喂给自己（引擎 RealTimeCapture 只认 SkyAtmosphere / 体积云 / IsSky 网格，
 * 这里三者都不是，抓出来是黑图，数据链只能自己搭）。
 * 编辑器世界也用视口相机驱动，否则编辑预览与运行时亮度对不上。
 */
UCLASS(ClassGroup = (StellarEra), meta = (BlueprintSpawnableComponent))
class STELLARERACELESTIAL_API UPlanetSkyLightComponent : public USkyLightComponent
{
	GENERATED_BODY()

public:
	UPlanetSkyLightComponent();

	/** 立方图边长。漫反射只吃低阶 SH，16 足够；8 更省，32 肉眼看不出差别。 */
	UPROPERTY(EditAnywhere, Category = "Planet|SkyLight", meta = (ClampMin = "4", ClampMax = "64"))
	int32 CubeResolution = 16;

	/** 两次重算的最小间隔（秒）。每次重算要新建一张立方图并触发一次 GPU 卷积。 */
	UPROPERTY(EditAnywhere, Category = "Planet|SkyLight", meta = (ClampMin = "0.0"))
	float UpdateIntervalSeconds = 0.2f;

	/** 星空/银河的平均辐亮（线性值）。夜面与深空的环境光全靠它，调到 0 就是纯黑。 */
	UPROPERTY(EditAnywhere, Category = "Planet|SkyLight")
	FLinearColor SpaceSkyRadiance = FLinearColor(0.008f, 0.010f, 0.020f, 1.0f);

	/** 与壳着色器的亮度校准倍率。画面亮度对不上时调它，不要去改物理量。 */
	UPROPERTY(EditAnywhere, Category = "Planet|SkyLight", meta = (ClampMin = "0.0"))
	float BrightnessScale = 1.0f;

	/**
	 * 编辑器世界里也按视口相机重算，让编辑预览的亮度与 PIE 一致。
	 * 关掉就停在最后一次写入的立方图 —— 只在排查「编辑器为什么变慢/变亮」时用。
	 */
	UPROPERTY(EditAnywhere, Category = "Planet|SkyLight")
	bool bUpdateInEditorWorld = true;

	/** 立即重算并推送一次，忽略节流。调试与手动触发用。 */
	UFUNCTION(BlueprintCallable, Category = "Planet|SkyLight")
	void RefreshNow();

protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

#if WITH_EDITOR
	// 存盘前把瞬态立方图摘掉，否则会被序列化成空引用；卸载时再兜一次。
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
#endif
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	// 必须先声明再被下面的函数签名用到（类内成员函数声明的参数类型不享受「完整类上下文」）。
	/** 决定天空是否变化的那几个量。全部在世界空间里比，因为 SkyLight 的 SH 就是世界空间的。 */
	struct FUpdateKey
	{
		FVector Up = FVector::ZeroVector;
		/** 钳到 [0, 壳厚]：飞远以后高度不再触发重算。 */
		float AltitudeCm = 0.0f;
		float SurfaceRadiusCm = 0.0f;
		float ShellRadiusCm = 0.0f;
		/** 直接进积分的参数：改任何一个，玩家看得见的天空就变了。 */
		float RayleighScaleHeightRatio = 0.0f;
		float MieScaleHeightRatio = 0.0f;
		FLinearColor SunIntensity = FLinearColor::Black;
		float CloudCoverage = 0.0f;
		bool bAtmosphereEnabled = true;
		bool bCloudsEnabled = true;
		TArray<FVector> SunDirections;
	};

	bool ResolveAtmosphere();
	bool GetObserverLocation(FVector& OutLocation);
	bool BuildKey(const FVector& Observer, FUpdateKey& OutKey);
	bool NeedsUpdate(const FUpdateKey& Key) const;
	/** 成功才返回 true；失败不推进节流状态，下次 Tick 重试。 */
	bool UpdateCube(const FVector& Observer, const FUpdateKey& Key);

	UPROPERTY(Transient)
	TObjectPtr<UTextureCube> CurrentCubemap;

#if WITH_EDITOR
	void HandleObjectPreSave(UObject* Object, FObjectPreSaveContext SaveContext);
	FDelegateHandle ObjectPreSaveHandle;
#endif

	TWeakObjectPtr<UPlanetAtmosphereComponent> Atmosphere;

	/** 每帧复用的缓冲，避免每次更新重新分配。 */
	TArray<FPlanetSun> Suns;
	TArray<FLinearColor> Pixels;

	FUpdateKey LastKey;
	bool bHasUpdated = false;
	double LastUpdateTime = -1.0e9;

	bool bLoggedMissingAtmosphere = false;
	bool bLoggedMissingCamera = false;
	bool bLoggedCubemapFailure = false;
};
