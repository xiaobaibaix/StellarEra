#pragma once

#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"
#include "PlanetAtmosphereComponent.generated.h"

class ADirectionalLight;

/**
 * 行星大气壳。
 *
 * 一个套在行星外的半透明球，像素着色器里做单次散射 ray march。
 * 材质是**双面**的，内外都渲染，由同一个 ray march 服务：
 *   - 相机在球外（太空）→ 画正面 → 这就是"大气辉光"
 *   - 相机在球内（行星表面）→ 画背面 → 这就是天空
 * 射线的进/出交点本来就按相机位置算，相机在壳内时入射点退化为 0，
 * 所以穿壳边界天然连续，不需要手工切可见性。
 *
 * 材质 bDisableDepthTest = true，遮挡不靠深度测试，靠 HLSL 里用场景深度
 * 截断积分 —— 顺带把远处地形压暗混色，得到空中透视。
 * 关卡里没有 ASkyAtmosphere，球内天空完全由本组件负责。
 *
 * C++ 构造函数不再创建它；行星 BP 需要把它挂到行星 Actor 的根组件上。
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

	/**
	 * 壳厚度 / 行星表面半径。决定辉光与天空的"厚度"，与瑞利/米氏标高一起决定地平线颜色。
	 *
	 * 注意这不是纯渲染参数：飞船的大气阻力剖面用同一属性当壳顶
	 * （SpaceshipPawn 的密度函数），改大等于把可飞行的大气层抬得更高。
	 */
	UPROPERTY(EditAnywhere, Category = "StellarEra|Atmosphere", meta = (ClampMin = "0.0"))
	float AtmosphereHeightRatio = 0.09f;

	/** 瑞利标高 / 壳半径。越小，密度越贴地面，天空越低越浓、高空越透。 */
	UPROPERTY(EditAnywhere, Category = "StellarEra|Atmosphere", meta = (ClampMin = "0.0"))
	float RayleighScaleHeightRatio = 0.008f;

	/** 米氏标高 / 壳半径。 */
	UPROPERTY(EditAnywhere, Category = "StellarEra|Atmosphere", meta = (ClampMin = "0.0"))
	float MieScaleHeightRatio = 0.0015f;

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
	 * 本值是**物理侧**的细调；bAtmosphereEnabled 是总开关：总开关关掉时本值根本不
	 * 参与计算。本值为 0 只表示「这颗行星有大气、但没有阻力」。
	 */
	UPROPERTY(EditAnywhere, Category = "StellarEra|Atmosphere", meta = (ClampMin = "0.0"))
	float DragAccelerationAtKmPerSecond = 10.0f;

	/**
	 * 太阳辐照度的线性 RGB 倍率。
	 *
	 * 名义上是"与地形那盏 DirectionalLight 相称"，实际默认值远大于 1：材质只算
	 * **单次散射**，而真实天空的亮度有相当一部分来自多次散射，瑞利相位函数在 90°
	 * 方向又只有 3/(16π) ≈ 0.06。两项合起来，物理量级的 accumulated 只有 ~0.01
	 * 线性值 —— 混合出来不是"发蓝光"，而是把背景压暗成暗蓝。14 就是把这段差额
	 * 补回来的系数，不是太阳真的亮了 14 倍。
	 *
	 * 想更亮/更暗直接调这里或材质实例参数，不要靠改 RayleighBeta 去凑亮度
	 * —— 那会连光学厚度一起改，颜色会跟着变。
	 */
	UPROPERTY(EditAnywhere, Category = "StellarEra|Atmosphere")
	FLinearColor SunIntensity = FLinearColor(14.0f, 14.0f, 14.0f, 1.0f);

	/**
	 * 大气总开关：关掉 = 这颗行星没有大气。
	 *
	 * 同时关掉两侧：
	 *   - 渲染：壳立即不可见（TickComponent 的边沿处理，重新勾上会重算）；
	 *   - 物理：飞船读到的密度比恒为 0，因此没有大气阻力，HUD 显示「真空」。
	 *
	 * 只是不想看壳、想对比 stat unit 时用 DragAccelerationAtKmPerSecond 不行 ——
	 * 那是物理侧的细调，管不到渲染。
	 *
	 * BlueprintReadWrite：运行中可切；可见性由 TickComponent 每帧兜底，蓝图改完
	 * 不需要额外调用任何函数。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "StellarEra|Atmosphere")
	bool bAtmosphereEnabled = true;

	// ---- 体积云 ----
	// 云与大气共用同一次 ray march：两层同心半透明壳无法同时排对序
	// （球内要"先大气后云"，球外要"先云后大气"），见
	// docs/superpowers/specs/2026-11-02-planet-volumetric-clouds-design.md。

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

	/**
	 * 满密度垂直穿层的总光学厚度（无量纲）。
	 *
	 * 材质要的是消光系数（1/cm），由 本值 / 云层厚度 换算 —— 换一颗更大/更小的
	 * 行星时云的浓淡不变。与 ComputeShellScatteringParams 用 τ 反推 β 同一个理由。
	 */
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

	/**
	 * 云的亮度倍率。
	 *
	 * 云与大气共用 SunIntensity（14，那是为单次散射的大气补的），直接照搬会把
	 * 云顶推到溢出白。这个倍率是给云单独的补偿项 —— 不要去改 SunIntensity。
	 */
	UPROPERTY(EditAnywhere, Category = "StellarEra|Clouds", meta = (ClampMin = "0.0"))
	float CloudBrightness = 1.0f;

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
