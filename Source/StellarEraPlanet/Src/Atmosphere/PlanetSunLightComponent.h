#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PlanetSun.h"
#include "PlanetSunLightComponent.generated.h"

class ADirectionalLight;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStarBodyComponent;

/**
 * 星球上的恒星照明：收集关卡里的恒星，逐颗转向/上色/写强度（含平方反比），
 * 再给每盏定向光挂行星遮挡光函数、把 SkyFill 那盏转到当地天顶。
 * 与大气壳无关——星球有没有大气都照样有光。
 */
UCLASS(ClassGroup = (StellarEra), meta = (BlueprintSpawnableComponent))
class STELLARERAPLANET_API UPlanetSunLightComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPlanetSunLightComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	/** 是否驱动恒星灯。关掉 = 灯保持原样不再被写；大气材质那份太阳列表不受影响。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|SunLight")
	bool bDrivesSunLights = true;

	/** 基础强度；每盏灯实际强度 = 本值 × 该恒星的相对亮度 × 平方反比因子。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|SunLight", meta = (ClampMin = "0.0"))
	float BaseLightIntensity = 10.0f;

	/** 平方反比的参考距离（厘米）：恒星正好在这个距离上时因子为 1，更近更亮、更远更暗。
	 *  1e8 cm = 1000 km，即当前三星关卡里主星到行星的距离。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|SunLight", meta = (ClampMin = "1.0"))
	float SunFalloffReferenceDistance = 1.0e8f;

	/** 天光补光强度（定向光单位）：每帧把打了 SkyFill 标签的那盏定向光转到当地天顶方向，
	 *  N·L=0 的暗面全靠它填。0 = 关闭。
	 *  量级参照：太阳仰角 8° 时平地的直射只有 10 × sin8° ≈ 1.4，补光比它略低才压不住明暗对比。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|SunLight", meta = (ClampMin = "0.0"))
	float SkyFillIntensity = 2.0f;

	/** 平方反比因子 (Ref / Dist)²，钳到 [0, 100]。纯函数，供测试。 */
	static float DistanceFalloff(float ReferenceDistance, double Distance);

	/** 从已排序列表挑主星：第一颗 Star 非空且 bDrivesDirectionalLight 的恒星；主星独占前向着色与大气太阳光名额。 */
	static UStarBodyComponent* SelectPrimaryStar(const TArray<FPlanetSun>& Suns);

	/** 该太阳的平方反比因子；回退灯（Star 为空）按 1 处理。大气用它算材质辐射。 */
	float SunFalloffFor(const FPlanetSun& Sun) const;

private:
	/** 逐颗驱动灯（仅游戏世界）。主星可能不是槽位 0 —— 槽位 0 只按距离排。 */
	void DriveSunLights(const TArray<FPlanetSun>& Suns);

	/** 确保行星阴影光函数基础材质已加载（失败只报一次）。 */
	UMaterialInterface* EnsurePlanetShadowMaterialBase();

	/** 取该灯对应的阴影 MID。每盏灯方向不同，参数不能共用一份。 */
	UMaterialInstanceDynamic* GetPlanetShadowMID(ADirectionalLight* Light);

	/** 给世界里每盏定向光挂行星遮挡光函数（仅游戏世界）。与「驱动灯」正交 —— 不读 bDrivesDirectionalLight。 */
	void ApplyPlanetShadow();

	/** 驱动关卡里打了 SkyFill 标签的定向光（仅游戏世界）：直射贴地时暗面全靠它，否则 N·L=0 是一圈硬边。 */
	void ApplySkyFill();

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> PlanetShadowBaseMaterial;

	UPROPERTY(Transient)
	TMap<TObjectPtr<ADirectionalLight>, TObjectPtr<UMaterialInstanceDynamic>> PlanetShadowMIDs;

	UPROPERTY(Transient)
	TObjectPtr<ADirectionalLight> SkyFillLight;

	bool bLoggedMissingPlanetShadowMaterial = false;
	bool bLoggedMissingSkyFill = false;
};
