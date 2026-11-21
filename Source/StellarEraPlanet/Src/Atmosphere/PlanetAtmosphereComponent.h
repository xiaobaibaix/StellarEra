#pragma once

#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"
#include "PlanetSun.h"
#include "PlanetAtmosphereComponent.generated.h"

class ADirectionalLight;
class UStarBodyComponent;

/** 行星大气壳：套在行星外的双面半透明球，单次散射 ray march；球外画正面（辉光）、球内画背面（天空），
 *  bDisableDepthTest 下靠场景深度截断积分遮挡。编辑器无 BeginPlay，故 OnRegister 里准备并 bTickInEditor。 */
UCLASS(ClassGroup = (StellarEra), meta = (BlueprintSpawnableComponent))
class STELLARERAPLANET_API UPlanetAtmosphereComponent : public UStaticMeshComponent
{
	GENERATED_BODY()

public:
	UPlanetAtmosphereComponent();

	/** 壳厚度 / 行星表面半径。除渲染外，飞船大气阻力剖面也用它当壳顶，改大等于把可飞行的大气层抬得更高。 */
	UPROPERTY(EditAnywhere, Category = "StellarEra|Atmosphere", meta = (ClampMin = "0.0"))
	float AtmosphereHeightRatio = 0.09f;

	/** 瑞利标高 / 壳半径。越小，密度越贴地面，天空越低越浓、高空越透。 */
	UPROPERTY(EditAnywhere, Category = "StellarEra|Atmosphere", meta = (ClampMin = "0.0"))
	float RayleighScaleHeightRatio = 0.008f;

	/** 米氏标高 / 壳半径。 */
	UPROPERTY(EditAnywhere, Category = "StellarEra|Atmosphere", meta = (ClampMin = "0.0"))
	float MieScaleHeightRatio = 0.0015f;

	/** 大气阻力：海平面、1 km/s 时的减速度（km/s²），0 = 关闭。a = 本值 × ρ(h) × (v/1 km/s)²，
	 *  终端速度 ≈ √(g / 本值) km/s；ρ(h)=exp(−h/标高)，壳顶以上为 0。总开关见 bAtmosphereEnabled。 */
	UPROPERTY(EditAnywhere, Category = "StellarEra|Atmosphere", meta = (ClampMin = "0.0"))
	float DragAccelerationAtKmPerSecond = 10.0f;

	/** 太阳辐照度的线性 RGB 倍率。材质只算单次散射、瑞利相位在 90° 只有 3/(16π)，物理量级累积值约 0.01，
	 *  14 是这个差额的补偿系数，不是太阳真亮了 14 倍。调亮度改这里或材质实例，别改 RayleighBeta。 */
	UPROPERTY(EditAnywhere, Category = "StellarEra|Atmosphere")
	FLinearColor SunIntensity = FLinearColor(14.0f, 14.0f, 14.0f, 1.0f);

	/** 大气版的多次散射地板：单次散射在背阳采样点上恒为 0，而那里的消光照样把 alpha
	 *  推到 1，不透明又不出光就是地平线那圈纯黑。0 = 旧行为，可用来做 A/B 对照。
	 *  量级：0.02 把背阳切向弦从纯黑抬到 sRGB≈25/41/54 的暮光蓝，向阳侧只多约 +18。 */
	UPROPERTY(EditAnywhere, Category = "StellarEra|Atmosphere", meta = (ClampMin = "0.0"))
	float AtmosphereAmbient = 0.02f;

	/* 天光补光强度已随星灯驱动搬到 UPlanetSunLightComponent::SkyFillIntensity。 */

	/** 大气总开关：关掉 = 壳不可见、飞船密度比恒为 0（无阻力、HUD 显示真空）。运行中可切，可见性与重算由 TickComponent 兜底。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "StellarEra|Atmosphere")
	bool bAtmosphereEnabled = true;

	// ---- 体积云 ----
	// 云与大气共用一次 ray march：两层同心壳无法同时排对序（球内先大气后云、球外相反）。

	/** 云的总开关。关掉 = 光学厚度传 0，材质不重建。 */
	UPROPERTY(EditAnywhere, Category = "StellarEra|Clouds")
	bool bCloudsEnabled = true;

	/** 云底高度 / 表面半径。必须小于 AtmosphereHeightRatio，否则云会长到壳外被裁掉。 */
	UPROPERTY(EditAnywhere, Category = "StellarEra|Clouds", meta = (ClampMin = "0.0"))
	float CloudBaseRatio = 0.02f;

	/** 云顶高度 / 表面半径。 */
	UPROPERTY(EditAnywhere, Category = "StellarEra|Clouds", meta = (ClampMin = "0.0"))
	float CloudTopRatio = 0.07f;

	/** 覆盖率：0 = 晴空，1 = 全阴。 */
	UPROPERTY(EditAnywhere, Category = "StellarEra|Clouds", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CloudCoverage = 0.45f;

	/** 满密度垂直穿层的总光学厚度（无量纲）；材质按 本值 / 云层厚度 换算消光系数，换行星大小时云的浓淡不变。 */
	UPROPERTY(EditAnywhere, Category = "StellarEra|Clouds", meta = (ClampMin = "0.0"))
	float CloudOpticalDepth = 6.0f;

	/** 天气图频率（噪声坐标取在归一化方向上）。越大云块越碎。 */
	UPROPERTY(EditAnywhere, Category = "StellarEra|Clouds", meta = (ClampMin = "0.0"))
	float CloudNoiseScale = 8.0f;

	/** 细节侵蚀的频率。 */
	UPROPERTY(EditAnywhere, Category = "StellarEra|Clouds", meta = (ClampMin = "0.0"))
	float CloudDetailScale = 24.0f;

	/** 细节侵蚀强度。设为 0 可省两层噪声，是首要的降级旋钮。 */
	UPROPERTY(EditAnywhere, Category = "StellarEra|Clouds", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CloudDetailStrength = 0.35f;

	/** 云随时间漂移的速度（噪声坐标/秒）。0 = 静止。 */
	UPROPERTY(EditAnywhere, Category = "StellarEra|Clouds")
	float CloudDriftPerSecond = 0.02f;

	/** 云亮度倍率。云与大气共用 SunIntensity（14 是为单次散射大气补的），直接照搬会把云顶推到溢出白，用本值单独补偿。 */
	UPROPERTY(EditAnywhere, Category = "StellarEra|Clouds", meta = (ClampMin = "0.0"))
	float CloudBrightness = 1.0f;

	/** 运行时改半径后调用，重算缩放与材质参数。 */
	UFUNCTION(BlueprintCallable, Category = "StellarEra|Atmosphere")
	void RefreshAtmosphereGeometry();

protected:
	/** 编辑器没有 BeginPlay，OnRegister 是唯一能把壳从默认 50cm 球变成真正大气的地方；游戏世界也走这里，故 BeginPlay 的后续调用必须幂等。 */
	virtual void OnRegister() override;
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

#if WITH_EDITOR
	/** 编辑器里改本组件参数（厚度比、开关）时立即重算，不依赖 BeginPlay。 */
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

public:
	/** 太阳方向（单位向量）。StarActor 非空用它的位置；否则回退 FallbackLight —— 定向光 forward 是传播方向，
	 *  太阳在其反向；都没有返回零向量。static 纯函数，供自动化测试覆盖。 */
	static FVector ComputeSunDirection(const AActor* StarActor, const ADirectionalLight* FallbackLight,
		const FVector& FromLocation);

	/** 收集照亮 PlanetCenter 的太阳，按距离升序，最多 MaxSuns 个。只读世界不碰材质（写材质在 ApplySuns，
	 *  驱动灯在 UPlanetSunLightComponent）。没有恒星时回退到第一盏 ADirectionalLight。 */
	static void CollectSunsFor(UWorld* World, const FVector& PlanetCenter, TArray<FPlanetSun>& OutSuns);

	/** 没有恒星时的回退：取一盏 ADirectionalLight 当太阳，Direction = −forward，Radiance 白，Star 空。
	 *  灯为空或方向为零向量时返回 false。纯函数。 */
	static bool MakeFallbackSun(const ADirectionalLight* Light, const FVector& FromLocation,
		FPlanetSun& OutSun);

private:
	/** 确保 AtmosphereMID 已创建；失败时把自己设为不可见并返回 false。 */
	bool EnsureAtmosphereMaterial();

	/** 把太阳列表写进 MID（SunDirection0..3 / SunRadiance0..3 / SunCount）。 */
	void ApplySuns(const TArray<FPlanetSun>& Suns);

	/** 每帧复用的缓冲，避免 CollectSunsFor 每帧重新分配。 */
	TArray<FPlanetSun> CachedSuns;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> AtmosphereMID;

	/** 几何与材质参数是否已按当前启用状态算过一遍（含三条失败分支）。TickComponent 只在它为假时重算：
	 *  不能每帧无条件调，也不能拿 IsVisible() 当判据（失败分支本就不可见）。 */
	bool bGeometryApplied = false;

	/** 上次应用时的表面半径（cm）。只服务编辑器预览：gizmo 缩放 / 改 Radius 不重跑 BeginPlay，Tick 比较此值，变了才重算。 */
	float AppliedSurfaceRadiusCm = 0.0f;

	/** 上次写进材质的解析地面半径（cm）。地形 / 水文重建会改它而基准半径不变，Tick 要单独比。 */
	float AppliedGroundRadiusCm = 0.0f;
};
