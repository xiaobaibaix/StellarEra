#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "StarBodyComponent.generated.h"

class ADirectionalLight;
class UMaterialInterface;
class UWorld;

/**
 * 「这个 Actor 是一颗恒星」的标记 + 主光接管开关。恒星身份拆成两个组件：Cosmos 的
 * UCosmosOrbitComponent 管物理身份，本组件管视觉/大气身份（反向依赖会成环）。
 * 不 Tick、不存位置，位置就是 Owner；颜色/亮度从 Owner 网格的 MI 读并缓存，
 * 只有材质接口指针变了才重读。
 */
UCLASS(ClassGroup = (StellarEra), meta = (BlueprintSpawnableComponent))
class STELLARERAPLANET_API UStarBodyComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UStarBodyComponent();

	/**
	 * 是否驱动自己那盏定向光。每颗开启的恒星都驱动各自的灯；独占的只有主光名额
	 * （ForwardShadingPriority / bAtmosphereSunLight），由离行星最近且开启该标志的恒星占用。
	 * 关掉后这颗星仍作为大气/云光源参与着色（CollectSunsFor 不按此标志过滤）。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "StellarEra|Star")
	bool bDrivesDirectionalLight = true;

	/**
	 * 这颗恒星驱动的定向光。留空则自动认领第一盏未被别的恒星占用的 ADirectionalLight
	 * 并缓存在 AutoSunLight；显式绑定可消掉编辑器里"哪盏是主光"的竞争警告。
	 */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "StellarEra|Star")
	TObjectPtr<ADirectionalLight> SunLight;

	/** 辐射色：材质 HotColor 按最大分量归一化；读不到（无网格/材质/参数）时回退白色。 */
	UFUNCTION(BlueprintCallable, Category = "StellarEra|Star")
	FLinearColor GetSunColor() const;

	/**
	 * 相对亮度：材质 Brightness ÷ 150（G2V 预设），钳到 [0.05, 4]。Brightness 是美术
	 * 旋钮不是光度，÷150 得到 0.4 / 1.0 / 2.67 的压缩艺术比值。
	 */
	UFUNCTION(BlueprintCallable, Category = "StellarEra|Star")
	float GetRelativeBrightness() const;

	/** 按最大分量归一化；全黑或非正时返回白色。纯函数，供测试。 */
	static FLinearColor NormalizeSunColor(FLinearColor Color);

	/** Brightness → 相对亮度（÷150、钳 [0.05, 4]）。纯函数，供测试。 */
	static float BrightnessToRelative(float Brightness);

	/** 显式 SunLight 优先；否则自动认领一盏没被别的恒星占用的灯（结果缓存）。 */
	ADirectionalLight* ResolveSunLight();

	/**
	 * 让灯从 PlanetCenter 方向照过来，写入颜色、强度与主光名额。只在游戏世界执行，
	 * 编辑器里每帧转灯会把关卡一直标脏。
	 */
	void DriveSunLight(const FVector& PlanetCenter, float BaseIntensity, bool bIsPrimary);

	/** 世界里全部恒星组件。 */
	static void FindAll(const UWorld* World, TArray<UStarBodyComponent*>& OutStars);

	/**
	 * 离 FromLocation 最近的恒星组件；World 空或没有恒星返回 nullptr。每次都完整遍历
	 * —— 天体是个位数，换来增删/移动时不需要失效通知。
	 */
	static UStarBodyComponent* FindNearest(const UWorld* World, const FVector& FromLocation);

private:
	/** 读材质并更新缓存；材质指针变了才重读。 */
	void RefreshSunParameters() const;

	mutable FLinearColor CachedSunColor = FLinearColor::White;
	mutable float CachedRelativeBrightness = 1.0f;
	mutable TWeakObjectPtr<const UMaterialInterface> CachedColorSource;
	mutable bool bSunParametersValid = false;

	UPROPERTY(Transient)
	TObjectPtr<ADirectionalLight> AutoSunLight;

	/** 上次写进自己那盏灯的优先级 / 大气太阳光，用来做边沿触发。 */
	int32 AppliedForwardShadingPriority = INDEX_NONE;
	bool bAppliedAtmosphereSunLight = false;
};
