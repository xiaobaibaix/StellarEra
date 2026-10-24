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
 *
 * **编辑器预览**：编辑器世界不执行 BeginPlay，而壳的材质、缩放、材质参数全都
 * 依赖它。若不做处理，编辑视图里就只有一个半径 50cm 的默认球，埋在行星中心，
 * 什么也看不到。因此材质在 OnRegister 里准备，并打开 bTickInEditor 让相机位置
 * 与行星半径变化在编辑视图里实时跟上。
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

	/**
	 * 大气阻力：海平面（ρ = 1）、相对速度 1 km/s 时的减速度（km/s²）。0 = 关闭。
	 *
	 * 阻力加速度按 a = 本值 × ρ(h) × (v / 1 km/s)² 反向于速度施加：与速度平方
	 * 成正比，速度翻倍阻力四倍。终端速度 ≈ √(g / 本值) km/s（g 为当地重力加速度，
	 * 单位 km/s²），所以调大本值 = 更快收敛到更低的落速。
	 *
	 * 密度 ρ(h) = exp(−h / 标高)，标高 = 表面半径 × RayleighScaleHeightRatio；
	 * 壳顶以上（h ≥ 表面半径 × AtmosphereHeightRatio）恒为 0。
	 *
	 * 与 bAtmosphereEnabled 无关：那个开关只管渲染，本值管物理。只有本值为 0
	 * 才意味着这颗行星没有大气阻力。
	 */
	UPROPERTY(EditAnywhere, Category = "StellarEra|Atmosphere", meta = (ClampMin = "0.0"))
	float DragAccelerationAtKmPerSecond = 10.0f;

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
	/**
	 * 注册时准备材质与几何。
	 *
	 * 编辑器世界没有 BeginPlay，但 OnRegister 一定执行 —— 这是编辑视图里唯一
	 * 能把壳从"半径 50cm 的默认球"变成真正大气的地方。游戏世界也走这里，
	 * 因此 BeginPlay 的后续调用必须是幂等的。
	 */
	virtual void OnRegister() override;
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

#if WITH_EDITOR
	/** 编辑器里改本组件参数（厚度比、开关）时立即重算，不依赖 BeginPlay。 */
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	/**
	 * 确保 AtmosphereMID 已创建（OnRegister / BeginPlay / 编辑器改参数都会调）。
	 * 失败时按既有约定把自己设为不可见并返回 false。
	 */
	bool EnsureAtmosphereMaterial();

	/** 解析太阳方向：取关卡里第一盏 ADirectionalLight 的朝向取反。找不到就保持上一次的值。 */
	FVector ResolveSunDirection();

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> AtmosphereMID;

	UPROPERTY(Transient)
	TObjectPtr<ADirectionalLight> CachedSunLight;

	/**
	 * SunDirection 的上一次值，用来判断"这一帧变了没有"。
	 *
	 * 只有它走门控。PlanetCenter 在 TickComponent 里**每帧无条件写**，
	 * 不经过这里，所以这里没有它的缓存。
	 *
	 * **相机位置不在这里，也不再有缓存**：材质用 CameraPositionWS 视图 uniform
	 * 逐帧自取。曾经由 C++ 写 MID，但编辑器里组件 Tick 早于视口 Tick，
	 * 写进去的相机位置慢一帧，快速移动视口时壳边缘会被算成过亮的暖白硬边。
	 */
	FVector LastSunDirection = FVector(UE_BIG_NUMBER);
	bool bLoggedMissingSun = false;

	/**
	 * 几何与材质参数是否已经按**当前**启用状态算过一遍。
	 *
	 * 不变式：为真 ⟺ `RefreshAtmosphereGeometry()` 已经在当前启用状态下完整跑过一次
	 * （**包括它的三条失败分支**）；`TickComponent` 观察到关掉时清假。
	 *
	 * 用来做边沿触发：`TickComponent` 只在它为假时重算一次。
	 *   - 不能每帧无条件调 —— 那会每帧写一次世界缩放和 7 个材质参数；
	 *   - 不能拿 `IsVisible()` 当判据 —— 三条失败分支本来就以"不可见"收场，
	 *     那样会每帧重试、每帧刷同一条日志。
	 * 失败分支也置真，正是为了不让它们每帧重试。
	 */
	bool bGeometryApplied = false;

	/**
	 * `RefreshAtmosphereGeometry()` 上次应用时的**表面半径**（cm）。
	 *
	 * 只服务编辑器预览：拖动 gizmo 缩放或改 Radius 时不会重跑 BeginPlay，
	 * 也没有可靠的 OnConstruction 时序。编辑器 Tick 比较这个值，变了才重算，
	 * 值不变时不写世界缩放与材质参数。
	 */
	float AppliedSurfaceRadiusCm = 0.0f;
};
