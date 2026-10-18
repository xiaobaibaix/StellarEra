#pragma once

#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"
#include "PlanetAtmosphereComponent.generated.h"

class ADirectionalLight;

/**
 * 行星大气壳。
 *
 * 一个套在行星外的半透明球，像素着色器里做单次散射 ray march。
 * 球面法线朝外，所以：
 *   - 相机在球外（太空）→ 看到正面 → 壳渲染，这就是"大气辉光"
 *   - 相机在球内（行星表面）→ 看到背面 → 被背面剔除 → 壳不渲染
 * 球内的天空交给引擎原生 SkyAtmosphere（由 UCosmosSimulationSubsystem 重定位），
 * 两者天然不打架，不需要手工切可见性。
 *
 * 由 AVoxelPlanetActor 在构造时创建并挂到根组件上，每颗行星自动获得一个。
 */
UCLASS(ClassGroup = (StellarEra), meta = (BlueprintSpawnableComponent))
class STELLARERAPLANET_API UPlanetAtmosphereComponent : public UStaticMeshComponent
{
	GENERATED_BODY()

public:
	UPlanetAtmosphereComponent();

	/** 壳厚度 / 行星表面半径。**必须**与引擎大气用的值一致，否则穿过壳边界会跳变。 */
	UPROPERTY(EditAnywhere, Category = "StellarEra|Atmosphere", meta = (ClampMin = "0.0"))
	float AtmosphereHeightRatio = 0.05f;

	/** 瑞利标高 / 壳半径。 */
	UPROPERTY(EditAnywhere, Category = "StellarEra|Atmosphere", meta = (ClampMin = "0.0"))
	float RayleighScaleHeightRatio = 0.015f;

	/** 米氏标高 / 壳半径。 */
	UPROPERTY(EditAnywhere, Category = "StellarEra|Atmosphere", meta = (ClampMin = "0.0"))
	float MieScaleHeightRatio = 0.003f;

	/** 太阳辐照度的线性 RGB 倍率。调到与地形被同一盏 DirectionalLight 照出来的亮度相称。 */
	UPROPERTY(EditAnywhere, Category = "StellarEra|Atmosphere")
	FLinearColor SunIntensity = FLinearColor::White;

	/** 关掉即不渲染壳，用于 stat unit 对比。 */
	UPROPERTY(EditAnywhere, Category = "StellarEra|Atmosphere")
	bool bAtmosphereEnabled = true;

	/** 运行时改半径后调用，重算缩放与材质参数。 */
	UFUNCTION(BlueprintCallable, Category = "StellarEra|Atmosphere")
	void RefreshAtmosphereGeometry();

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

private:
	/** 解析太阳方向：取关卡里第一盏 ADirectionalLight 的朝向取反。找不到就保持上一次的值。 */
	FVector ResolveSunDirection();

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> AtmosphereMID;

	UPROPERTY(Transient)
	TObjectPtr<ADirectionalLight> CachedSunLight;

	/**
	 * CameraPos / SunDirection 的上一次值，用来判断"这一帧变了没有"。
	 *
	 * 只有这两个走门控。PlanetCenter 在 TickComponent 里**每帧无条件写**，
	 * 不经过这里，所以这里没有它的缓存。
	 */
	FVector LastCameraPos = FVector(UE_BIG_NUMBER);
	FVector LastSunDirection = FVector(UE_BIG_NUMBER);
	bool bLoggedMissingSun = false;
};
