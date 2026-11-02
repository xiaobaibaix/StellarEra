#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "StarBodyComponent.generated.h"

class ADirectionalLight;
class UMaterialInterface;
class UWorld;

/**
 * 「这个 Actor 是一颗恒星」的标记 + 主光接管开关。
 *
 * ── 为什么不能只靠 UCosmosOrbitComponent 判断恒星 ──────────────
 * 判断恒星的代码是大气组件，它在本模块；而 UCosmosOrbitComponent 在 Cosmos 模块。
 * `StellarEraCosmos → StellarEraPlanet` 这条依赖已经存在，反向再来一条就成环
 * （见 docs/superpowers/specs/2026-11-02-sun-star-bodies-design.md §4）。
 *
 * 于是恒星的身份拆成两个互不认识的组件，由关卡里的同一个 Actor 同时挂上：
 *   - UCosmosOrbitComponent（Cosmos）：质量多少、初速多少 —— 物理身份
 *   - 本组件（Planet）            ：这是一颗恒星、是否接管主光 —— 视觉 / 大气身份
 * Cosmos 不需要知道「恒星」这个概念，Planet 也不需要知道 N 体。
 *
 * 与 UCosmosOrbitComponent 同样的取舍：**不 Tick，组件自己不存位置**。
 * 恒星位置就是 Owner 的位置，每帧现取 —— 恒星在 N 体里是会动的。
 *
 * 恒星的颜色与相对亮度**不在本组件上冗余字段**，每帧从 Owner 网格的材质实例
 * （MI_Sun_*）的 HotColor / Brightness 读出来并缓存 —— 改动只在材质实例上做，
 * 组件侧不需要同步任何数值。
 */
UCLASS(ClassGroup = (StellarEra), meta = (BlueprintSpawnableComponent))
class STELLARERAPLANET_API UStarBodyComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UStarBodyComponent();

	/**
	 * 这颗恒星是否接管关卡主光（第一盏 ADirectionalLight）的朝向。
	 *
	 * 只对该行星**最近**的那颗恒星有意义：大气按"最近的恒星"算明暗分界，
	 * 地形若还按另一盏灯的朝向算，两边会打架。多颗恒星时只有最近那颗生效，
	 * 所以同时只有一个写者。
	 *
	 * 关掉它就退回纯装饰的恒星：天空照得到它，但它不转灯。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "StellarEra|Star")
	bool bDrivesDirectionalLight = true;

	/**
	 * 这颗恒星驱动的定向光。
	 *
	 * 关卡里显式指定（`EditInstanceOnly`）；留空则自动认领关卡里**没被别的恒星占用**
	 * 的第一盏 ADirectionalLight，并把结果缓存在 AutoSunLight。
	 * 显式绑定存在的意义：自动认领按 Actor 遍历顺序，谁拿哪盏灯不可预期，
	 * 而"哪盏灯是主光"要写进资产才能消掉编辑器里那条竞争警告。
	 */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "StellarEra|Star")
	TObjectPtr<ADirectionalLight> SunLight;

	/**
	 * 辐射色：恒星网格材质实例的 HotColor，按最大分量归一化。
	 * 材质读不到（没有网格 / 没有材质 / 参数名不对）时回退白色。
	 */
	UFUNCTION(BlueprintCallable, Category = "StellarEra|Star")
	FLinearColor GetSunColor() const;

	/**
	 * 相对亮度：材质里的 Brightness ÷ 150（G2V 预设的现值），钳到 [0.05, 4]。
	 *
	 * 为什么不是物理光度：Brightness 是"太阳在屏幕上看起来多大"的美术旋钮
	 * （60 / 150 / 400），直接当光度用会让 M2V 照的行星黑成一片。
	 * 除以 150 得到的 0.4 / 1.0 / 2.67 是刻意压缩过的艺术比值。
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
	 * 让灯从 PlanetCenter 的方向照过来，同时写入颜色、强度与主光名额。
	 *
	 * **只在游戏世界执行**：编辑器视口里每帧转灯会把关卡一直标脏。
	 * 编辑器里要消除"多个定向光源竞争"的警告，靠的是关卡资产里的
	 * ForwardShadingPriority / bAtmosphereSunLight（Task 4 写入），不是这里。
	 */
	void DriveSunLight(const FVector& PlanetCenter, float BaseIntensity, bool bIsPrimary);

	/** 世界里全部恒星组件。 */
	static void FindAll(const UWorld* World, TArray<UStarBodyComponent*>& OutStars);

	/**
	 * 世界上离 FromLocation 最近的恒星组件；World 为空或世界里没有恒星时返回 nullptr。
	 *
	 * 用「最近」而不是「唯一」：三颗恒星下每颗行星应当被自己那颗恒星照亮。
	 * 每次都完整遍历 —— 关卡里天体是个位数，成本可以忽略，换来的是恒星移动、
	 * 增删时不需要任何失效通知。
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
