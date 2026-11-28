#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PlanetSun.h"
#include "PlanetSunLightComponent.generated.h"

class ADirectionalLight;
class AVoxelPlanetActor;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStarBodyComponent;

/**
 * 星球上的恒星照明：收集关卡里的恒星，逐颗转向/上色/写强度（含平方反比），
 * 再给每盏定向光挂行星遮挡光函数、把 SkyFill 那盏转到当地天顶。
 * 与大气壳无关——星球有没有大气都照样有光。
 */
UCLASS(ClassGroup = (StellarEra), meta = (BlueprintSpawnableComponent))
class STELLARERACELESTIAL_API UPlanetSunLightComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPlanetSunLightComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;

#if WITH_EDITOR
	// 编辑器里改列表/灯类就立刻重建，否则要等进 PIE 才看得到灯。
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

public:

	/** 是否驱动关卡里的定向光。关掉 = 灯保持原样不再被写；阴影/天空补光仍然生效。 */
	UPROPERTY(EditAnywhere, Category = "Planet|SunLight", meta = (DisplayName = "DrivesSunLights"))
	bool bDrivesSunLights = true;

	/** 基础强度；每盏灯实际强度 = 本值 × 该恒星的相对亮度 × 平方反比因子。 */
	UPROPERTY(EditAnywhere, Category = "Planet|SunLight", meta = (DisplayName = "BaseLightIntensity", ClampMin = "0.0"))
	float BaseLightIntensity = 10.0f;

	/** 平方反比的参考距离（厘米）：1e8 cm = 1000 km，即主星到行星的典型距离。 */
	UPROPERTY(EditAnywhere, Category = "Planet|SunLight", meta = (DisplayName = "SunFalloffReferenceDistance", ClampMin = "1.0"))
	float SunFalloffReferenceDistance = 1.0e8f;

	/** 天光补光强度：暗面全靠它填，0 = 关闭。 */
	UPROPERTY(EditAnywhere, Category = "Planet|SunLight", meta = (DisplayName = "SkyFillIntensity", ClampMin = "0.0"))
	float SkyFillIntensity = 2.0f;

	/** 本驱动管理的恒星列表。空 = 自动找世界里的 UStarBodyComponent。 */
	UPROPERTY(EditAnywhere, Category = "Planet|SunLight", meta = (DisplayName = "SunStars"))
	TArray<TObjectPtr<AActor>> SunStars;

	/** 生成的方向光灯类，通常配 BP_StarLight。空 = 不自动生成，沿用关卡里已有的灯。 */
	UPROPERTY(EditAnywhere, Category = "Planet|SunLight", meta = (DisplayName = "StarLightClass"))
	TSubclassOf<ADirectionalLight> StarLightClass;

	/** 自动按恒星生成/重建方向光。关掉 = 只驱动关卡里已有的灯。 */
	UPROPERTY(EditAnywhere, Category = "Planet|SunLight", meta = (DisplayName = "AutoManageLights"))
	bool bAutoManageLights = true;

	/** 按 SunStars 重新生成灯：销毁旧灯、逐星生成、刷新。细节面板也给了按钮。 */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Planet|SunLight")
	void RebuildSunLights();

	/** 平方反比因子 (Ref / Dist)²，钳到 [0, 100]。纯函数，供测试。 */
	static float DistanceFalloff(float ReferenceDistance, double Distance);

	/** 从已排序列表挑主星：第一颗 Star 非空且 bDrivesDirectionalLight 的恒星；主星独占前向着色与大气太阳光名额。 */
	static UStarBodyComponent* SelectPrimaryStar(const TArray<FPlanetSun>& Suns);

	/** 该太阳的平方反比因子；回退灯（Star 为空）按 1 处理。大气用它算材质辐射。 */
	float SunFalloffFor(const FPlanetSun& Sun) const;

private:
	/** 参数都在行星 Actor 上（和 SunStars 列表同一分类）。 */
	const AVoxelPlanetActor* GetPlanetOwner() const;
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
	TArray<TObjectPtr<ADirectionalLight>> SpawnedLights;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> PlanetShadowBaseMaterial;

	UPROPERTY(Transient)
	TMap<TObjectPtr<ADirectionalLight>, TObjectPtr<UMaterialInstanceDynamic>> PlanetShadowMIDs;

	UPROPERTY(Transient)
	TObjectPtr<ADirectionalLight> SkyFillLight;

	bool bLoggedMissingPlanetShadowMaterial = false;
	bool bLoggedMissingSkyFill = false;
};
