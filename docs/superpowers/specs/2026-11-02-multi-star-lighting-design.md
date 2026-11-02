# 多恒星光照：大气与体积云的多光源适配 + 一星一灯 — 设计文档

- **日期**：2026-11-02
- **项目**：StellarEra（Unreal Engine 5.8）
- **状态**：设计已批准，待生成实现计划
- **前置文档**：`2026-11-02-sun-star-bodies-design.md`、`2026-11-02-planet-volumetric-clouds-design.md`
- **取代**：`2026-11-02-sun-star-bodies-design.md` §3「非目标」里的「多恒星光照不做」。本次就是做它。

---

## 1. 背景

`UniverseMap` 现在有三颗发光恒星与一颗行星。引擎在视口里报：

> 多个定向光源正在竞争成为唯一一个用于前向着色、半透明、水或体积雾的光源。
> 请调整他们的 ForwardShadingPriority。

实测关卡状态（2026-11-02，`execute_python_code` 读取）：

| 项 | 值 |
|---|---|
| 定向光数量 | 3（均为 `ADirectionalLight`） |
| `ForwardShadingPriority` | 3 盏全是 `0` |
| `bAtmosphereSunLight` | 3 盏全是 `True`，`AtmosphereSunLightIndex` 全是 `0` |
| Mobility / 强度 / 颜色 | 全 `Stationary` / `10.0` / 白 |
| 恒星 | `Star_A` @ (0,0,0)、`Star_B` @ (−2e8,0,0)、`Star_C` @ (2e8,0,0) |
| 行星 | (0, 1e8, 0) |

三盏灯并列在最高优先级 = 引擎无法挑出唯一一盏前向/半透明/体积雾光源；
三盏灯同时声明"我是大气太阳光" = SkyAtmosphere 也在争。**两个竞争条件是独立的，都得治。**

代码侧还有三处只支持单太阳：

1. `UPlanetAtmosphereComponent::ResolveSunDirection()` 只认**最近的一颗**恒星，且只转**遍历到的第一盏**灯；
   另外两盏既没人驱动、朝向也不变。
2. 材质只有一组 `SunDirection` + `SunIntensity` 输入。
3. `Shaders/PlanetClouds.ush` 的遮挡、相位、云自遮挡全部只按一个 `SunDir` 算。

恒星颜色与亮度目前只存在于 `MI_Sun_*` 材质实例的参数里（`CoolColor` / `MidColor` / `HotColor` / `Brightness`），
组件上没有任何字段承载。

## 2. 目标

- 大气与体积云对**最多 4 颗恒星**求和，每颗贡献自己的方向、颜色、相对亮度、行星遮挡与相位。
- **一星一灯**：每颗恒星驱动自己的一盏 `ADirectionalLight`。
- **只有离行星最近的那颗星**占用唯一前向着色 / 大气太阳光名额，引擎警告消失。
- 恒星颜色与相对亮度**从它自己的材质实例读**，不另建一份数据。
- 单恒星 / 无恒星关卡的行为与现在逐像素一致。

## 3. 非目标

- 体积光 / 丁达尔光束（god rays）不做，仍是后续独立特性。
- 多颗行星共用一组灯不做。定向光是全局的，一关只有一颗带大气的行星（现状即是），
  本设计按此建模；多行星是另一个设计。
- SkyAtmosphere 同时吃 3 个太阳不做。只有主星声明 `bAtmosphereSunLight`，
  另外两颗只贡献大气壳与云，不影响引擎天空。
- 云的**多次散射**不做。环境光底仍用常数 `CLOUD_AMBIENT`。
- 恒星光晕 / 镜头耀斑 / 恒星自转不做。
- 地形材质本身的改动不做（地形只吃引擎光照，不读大气材质的太阳列表）。

## 4. 关键约束

### 4.1 模块分层

`UStarBodyComponent` 与 `UPlanetAtmosphereComponent` 都在 `StellarEraPlanet`；
`ADirectionalLight` / `UDirectionalLightComponent` / `UMaterialInterface` 都在 `Engine`。
本次**不新增跨模块依赖**，`StellarEraPlanet` 仍然只依赖 `Voxel`。

`AStarBodyActor` 在 `StellarEraCosmos`，`UStarBodyComponent` 不能 include 它。
因此组件取材质走 `GetOwner()->FindComponentByClass<UStaticMeshComponent>()`，
不认识具体恒星 Actor 类。

### 4.2 灯与星的配对必须显式优先

`AStarBodyActor` 自己不带灯；灯是关卡里独立摆放的 Actor。三盏灯和三颗星之间没有任何
天然对应关系，靠 Actor 名字或遍历顺序配对是隐式的、重排即错。

因此 `UStarBodyComponent` 增加一个**关卡里显式指定**的 `SunLight` 引用；
只有未指定时才自动认领一盏没人要的灯（保住单星关卡的既有行为）。

### 4.3 引擎 API（已从 5.8 源码核对）

| 用途 | API | 出处 |
|---|---|---|
| 前向着色优先级 | `int32 ForwardShadingPriority` / `SetForwardShadingPriority(int32)` | `DirectionalLightComponent.h:131,320` |
| 大气太阳光 | `bAtmosphereSunLight` / `SetAtmosphereSunLight(bool)` | `DirectionalLightComponent.h:164,311` |
| 强度 / 颜色 | `ULightComponent::SetIntensity(float)`、`SetLightColor(FLinearColor, bool bSRGB = true)` | `LightComponent.h:286,296` |
| 读材质参数 | `UMaterialInterface::GetVectorParameterValue(const FHashedMaterialParameterInfo&, FLinearColor&, bool)`、`GetScalarParameterValue(...)` | `MaterialInterface.h:1213,1217` |

`UMaterialInstance::GetParameterValue` 有覆写（`MaterialInstance.cpp:1184`），
所以从 `MI_Sun_G2V` 读 `HotColor` 会返回**实例覆盖后**的值，不是父材质默认值。

`FHashedMaterialParameterInfo` 有 `const TCHAR*` 隐式构造（`MaterialParameters.h:100`），
调用点可以直接写 `Mat->GetVectorParameterValue(TEXT("HotColor"), Out)`。

### 4.4 灯的朝向用 `SetActorRotation`，不要用 spawn 变换

`ADirectionalLight` 构造函数给灯光组件写死 `SetRelativeRotation(FRotator(−46,0,0))`
（`Light.cpp:245`），且 `RootComponent = DirectionalLightComponent`（`Light.cpp:250`）。
`SpawnActor` 的 `SelectDefaultAtRuntime` 分支做的是 `RootTransform * UserSpawnTransform`
（`Actor.cpp:4321`），旋转会**相乘**，所以 spawn 时传的 rotator 不是最终朝向。

但 `SetActorRotation` 是直接覆写根组件的相对旋转，没有这个问题。
运行时驱动灯一律用 `SetActorRotation`。

## 5. 设计

### 5.1 一颗恒星贡献什么

`UStarBodyComponent` 新增：

```cpp
/** 这颗恒星驱动的定向光。关卡里显式指定；留空则自动认领一盏没被别的恒星占用的灯。 */
UPROPERTY(EditInstanceOnly, Category = "StellarEra|Star")
TObjectPtr<ADirectionalLight> SunLight;

/** 恒星的辐射色（线性，已归一化到最大分量 = 1）。取自恒星网格材质的 HotColor。 */
FLinearColor GetSunColor() const;

/** 相对亮度。取自恒星网格材质的 Brightness，除以参考值 150（G2V 预设）并钳到 [0.05, 4]。 */
float GetRelativeBrightness() const;

/** 让这盏灯从 PlanetCenter 的反方向照过来，并写入颜色与强度。 */
void DriveSunLight(const FVector& PlanetCenter, float BaseIntensity, bool bIsPrimary);
```

**颜色取 `HotColor`**：色带三段里只有它代表"日面的峰值辐射"。
G2V → (1, 0.94, 0.78) 暖白、M2V → (1, 0.62, 0.26) 橙、B3V → (0.92, 0.96, 1.0) 蓝白，
正是平行光该有的颜色。再按最大分量归一化（`MaxComponent <= 1e-4` 时直接用白色，避免除零），
让亮度全部落到 `RelativeBrightness` 上，避免"颜色更亮"和"光更强"混在一个数里。

**亮度取 `Brightness / 150`**：150 是 G2V 预设值（`SunMaterialBuilder.cpp:460`），用作标定参照。
得到 M2V = 0.4、G2V = 1.0、B3V = 2.67。

> **必须写进注释的偏差**：`Brightness` 在 `SunMaterialBuilder.cpp:441-445` 里的本义是
> "日面在屏幕上占多大"的画面参数，不是光度。这里把它当**相对亮度**用，比值是刻意压缩过的。
> 真实 M2V : G2V : B3V ≈ 0.04 : 1 : 1000+，照搬会让红矮星那边的行星全黑。

缓存在组件里（`mutable FLinearColor CachedSunColor` 等），失效判据是"材质指针变了"。
读不到材质或参数时回退白色 × 1.0，不报错（`TestMap` 里的试验球没有恒星组件，本来就不会走到）。

`DriveSunLight`：
- `SetActorRotation((StarLocation - PlanetCenter).GetSafeNormal().Rotation())`
  —— 定向光 forward 是光**传播**方向，星→行星正是它；
- `SetLightColor(SunColor, /*bSRGB=*/false)`（值已经是线性，别让引擎再转一次）；
- `SetIntensity(BaseIntensity * RelativeBrightness)`；
- `bIsPrimary` 为真时 `SetForwardShadingPriority(1)` 且 `SetAtmosphereSunLight(true)`，
  否则 `0` / `false`。这两项走**边沿触发**：内部记住上次写入的值，变了才写，
  免得每帧改组件属性触发渲染状态刷新。

### 5.2 大气组件：收集与排序

`ResolveSunDirection()` 被替换为私有方法：

```cpp
struct FPlanetSun
{
    FVector Direction;      // 单位向量：行星 → 恒星
    FLinearColor Radiance;  // 颜色 × 相对亮度
    UStarBodyComponent* Star = nullptr;  // 回退太阳时为 nullptr
};

/** 静态：不碰材质、不碰 MID，便于自动化测试直接喂一个世界和探针点。 */
static void CollectSunsFor(UWorld* World, const FVector& PlanetCenter,
    TArray<FPlanetSun>& OutSuns);
```

规则：

1. 遍历世界里全部 `UStarBodyComponent`（不论 `bDrivesDirectionalLight` 真假 ——
   这个开关只管"转不转灯"，恒星始终照亮天空；与 `StarBodyComponent.h:42` 现有注释一致）。
2. 按到行星中心的距离**升序**排序。**槽位 0 永远是最近的恒星**，这样
   "主星才算自遮挡"在 shader 里就是 `s == 0`，不需要额外传主星索引。
3. 超过 `PlanetClouds::MaxSuns`（= 4，见 §5.4）颗时只保留最近 4 颗，警告一次。
4. 一颗恒星都没有 → 回退到"第一盏 `ADirectionalLight` 的 forward 取反 + 白色 × 1.0"，
   与改动前完全一致；连灯都没有 → 空数组 + 只警告一次（沿用现有 `bLoggedMissingSun`）。

`bDrivesDirectionalLight == false` 的恒星仍然进入列表，但它的 `SunLight` 不参与主光竞争。

### 5.3 材质参数

新增 9 个参数，`PlanetAtmosphereMaterialBuilder.cpp` 里全部创建，Custom 节点全部接上：

| 参数 | 类型 | 含义 |
|---|---|---|
| `SunDirection0..3` | Vector | xyz = 行星→恒星的单位向量 |
| `SunRadiance0..3` | Vector | rgb = 颜色 × 相对亮度 |
| `SunCount` | Scalar | 有效槽位数，0~4 |

`SunDirection` / `SunIntensity` 两个旧参数：
- `SunDirection` **删除**（被 4 个槽位取代）；
- `SunIntensity`（默认 14）**保留**，语义不变：它是"单次散射补到可视量级"的系数，
  不是物理倍率（见 `PlanetAtmosphereComponent.h:71-82`）。最终仍是
  `return float4(march.rgb * SunIntensity.rgb, march.a);`。

`SunCount = 0` 时 march 返回全零 —— 等同于今天"找不到太阳"的表现。

每帧写 9 个参数在 `TickComponent` 里（与 `PlanetCenter` 同一条路径，不额外门控：
恒星在 N 体里每帧都在动）；编辑器静态预览在 `RefreshAtmosphereGeometry()` 里写一次，
这是修 `PlanetCenter` 时确认过的唯一可靠时机。

### 5.4 HLSL

`Shaders/PlanetClouds.ush` 引入一个太阳集合结构，避免把参数表撑成两倍长。

太阳槽位数只有一个来源：`PlanetCloudParams.h` 里的
`constexpr int32 PlanetClouds::MaxSuns = 4;`。HLSL 侧的 `#define PLANET_MAX_SUNS 4`
必须与它一致 —— 两边分别控制 C++ 的截断与 shader 的数组长度，不一致的症状是
"第 4 颗星在材质里根本不存在"。`PlanetAtmosphereMaterial.Hlsl` 测试里断言
`ush 含 "PLANET_MAX_SUNS 4"` 且 `PlanetClouds::MaxSuns == 4`。

```hlsl
#define PLANET_MAX_SUNS 4

struct FPlanetSunSet
{
    int    Count;
    float3 Dir[PLANET_MAX_SUNS];
    float3 Radiance[PLANET_MAX_SUNS];
    float  PhaseR[PLANET_MAX_SUNS];   // Rayleigh 相位
    float  PhaseM[PLANET_MAX_SUNS];   // Mie 相位
    float  PhaseC[PLANET_MAX_SUNS];   // 云相位
};
```

`CloudAtmosphereRaymarch` 的新签名接收 4 组方向 + 4 组辐射 + `SunCount`，
内部构造并填好 `FPlanetSunSet`（相位按 `dot(Rd, Dir[s])` 每像素算一次），
再传给 `CloudAtmoSegment`。

`CloudAtmoSegment` 里每个采样点：

```hlsl
float3 Scatter = 0;
float3 CloudLit = 0;
[unroll]
for (int s = 0; s < PLANET_MAX_SUNS; ++s)
{
    if (s >= Suns.Count) continue;          // 常量索引 + 运行期跳过

    float3 SunDir = Suns.Dir[s];
    float  BS = dot(Ops, SunDir);
    float  SunVis = 1.0;
    if (BS < 0.0)
    {
        float DPerp = length(Ops - BS * SunDir);
        SunVis = smoothstep(PlanetRadius * 0.99, PlanetRadius * 1.01, DPerp);
    }

    // 大气：遮挡 × 相位求和；Tr * TrSeg 提到循环外，只乘一次
    Scatter += Suns.Radiance[s] * SunVis
             * (RayleighBeta * DR * Suns.PhaseR[s] + MieBeta * DM * Suns.PhaseM[s]);

    if (DC > 0.0)
    {
        float CloudLight = SunVis;
        // 只有主星（槽位 0）做自遮挡 march —— 3 次额外的 CloudDensity 是最贵的一段
        if (s == 0)
        {
            float ShadowStep = (CloudTopRadius - CloudBaseRadius) * (1.0 / float(CLOUD_SHADOW_STEPS));
            for (int j = 1; j <= CLOUD_SHADOW_STEPS; ++j)
            {
                float3 SP = P + SunDir * (float(j) * ShadowStep);
                float3 SOps = SP - Center;
                CloudLight *= exp(-CloudDensity(SOps, length(SOps), /*...*/) * ShadowStep
                                  * CLOUD_SHADOW_EXTINCTION_SCALE);
            }
            // 环境光底只在主星这一项里兜：N = 1 时与今天逐像素等价
            CloudLight = max(CloudLight, CLOUD_AMBIENT);
        }
        CloudLit += Suns.Radiance[s] * Suns.PhaseC[s] * CloudLight;
    }
}

Acc += Scatter * Tr * TrSeg * Ds;
if (DC > 0.0)
{
    Acc += CloudBrightness * DC * CloudLit * Tr * TrSeg * Ds;
}
```

要点：

- `[unroll]` + 运行期 `continue`，保证 `Suns.Dir[s]` 是**常量下标**；
  动态下标在部分平台会退化成 local memory 数组，是性能坑。
- `Tr * TrSeg` 从每个太阳的贡献里提出来，循环内只算一次。
- `max(CloudLight, CLOUD_AMBIENT)` 只对槽位 0 生效。今天这一项是
  `max(CloudLight, CLOUD_AMBIENT) * PhaseC`，所以这样写 N = 1 时**数值完全相同**；
  若改成给每个太阳各加一份环境光，3 颗星会得到 3 倍环境光、夜面发灰。
- 主星以外不跑自遮挡 march，所以最贵的 3 次 `CloudDensity` 仍然只跑一遍。

改完 `.ush` 必须把 `PlanetAtmosphereMaterialBuilder.cpp:58` 的
`#define PLANET_CLOUDS_VERSION 6` 改成 **7**，否则材质不会重编、画面零变化。

### 5.5 一星一灯与主光名额

`UPlanetAtmosphereComponent` 每帧（仅游戏世界）：

1. `CollectSuns` 得到排序后的列表。
2. **主星 = 列表里第一颗 `bDrivesDirectionalLight == true` 的恒星**（可能不是槽位 0：
   槽位 0 只按距离排，它可能被显式关掉了灯）。
3. 对每颗 `bDrivesDirectionalLight == true` 的恒星调用
   `Star->DriveSunLight(PlanetCenter, BaseLightIntensity, /*bIsPrimary=*/Star == PrimaryStar)`。
4. **主光切换是边沿事件**：边沿状态记在**每颗恒星自己**的
   `UStarBodyComponent` 里（上次写进自己那盏灯的 priority / `bAtmosphereSunLight`），
   与本次 `bIsPrimary` 不同才写。谁变成主星，谁的组件发现 `false → true` 就升级；
   谁掉出主星，它的组件发现 `true → false` 就降级。不需要再维护一份
   `LastPrimaryLight`，也就不存在"两份状态不同步"。

新增字段：

```cpp
/** 非主星时定向光的基础强度。10 与关卡现值一致。主星实际强度 = 本值 × 相对亮度。 */
UPROPERTY(EditAnywhere, Category = "StellarEra|Atmosphere")
float BaseLightIntensity = 10.0f;
```

**编辑器视口里不转灯**（沿用现有约定：每帧转灯会把关卡一直标脏）。但为了编辑器里
也不再出现那条引擎警告，**主光优先级与 `bAtmosphereSunLight` 写进关卡资产**：
实现时用 MCP 把三盏灯按上表设好，运行时只在主星切换时才改。

Stationary → Movable 的切换沿用现有逻辑，但现在对**所有**被驱动的灯做，不是只有第一盏。

### 5.6 关卡改动

- 三颗星各自的 `SunLight` 指向一盏灯（`StarBody` 组件的 `EditInstanceOnly` 属性）。
- 初始主星是 `Star_A`（离行星 1e8 cm，另两颗 2.24e8 cm）：它那盏灯
  `ForwardShadingPriority = 1`、`bAtmosphereSunLight = True`；
  另两盏 `0` / `False`。
- 灯的位置随意（定向光只看朝向），朝向由运行时驱动。

## 6. 数据流

```
关卡摆放                        每帧（仅游戏世界）
─────────                       ─────────────────────────────────────────────
AStarBodyActor(A/B/C)            UCosmosSimulationSubsystem.Tick
 ├ UCosmosOrbitComponent  ────→   推进 N 体、写回 Actor 位置
 └ UStarBodyComponent
    ├ SunLight ─────────────┐
    └ StarMesh(材质实例)     │      UPlanetAtmosphereComponent.TickComponent
         HotColor/Brightness │        ├ CollectSuns()：全部恒星 → 按距离排序 → 前 4 个
                             │        ├ 写 SunDirection0..3 / SunRadiance0..3 / SunCount
                             │        └ for i, sun: Star->DriveSunLight(center, base, i==0)
                             └──────→     ├ SetActorRotation(星→行星)
行星 BP_VoxelPlanet                        ├ SetLightColor / SetIntensity
 └ UPlanetAtmosphereComponent              └ 主光：SetForwardShadingPriority(1) + SetAtmosphereSunLight(true)
      └ MID: SunDirection*/SunRadiance*/SunCount
           └ M_PlanetAtmosphere（ray march，4 太阳求和）
```

## 7. 测试与验证

### 7.1 自动化测试

| 测试名 | 断言 |
|---|---|
| `StellarEra.Planet.AtmosphereMaterial.Structure`（扩展） | `SunDirection0..3`、`SunRadiance0..3` 是 Vector 参数，`SunCount` 是 Scalar 参数；`SunDirection` 已不存在 |
| `StellarEra.Planet.AtmosphereMaterial.Hlsl`（扩展） | Code 含 `SunDirection3` / `SunCount`；ush 含 `FPlanetSunSet`、`[unroll]`、`#define PLANET_MAX_SUNS 4`，且与 `PlanetClouds::MaxSuns` 相等 |
| `StellarEra.Planet.Atmosphere.SunList.Build`（新增） | 两个/四个/五个恒星的数据下：顺序按距离升序；超过 4 个截断到 4；零恒星走回退单太阳；`Direction` 是单位向量且指向恒星 |
| `StellarEra.Planet.Atmosphere.SunRadiance.Math`（新增，纯函数） | 归一化：任意 `HotColor` 的最大分量变为 1；`Brightness` 除以 150 并钳到 [0.05, 4] |
| `StellarEra.Planet.Atmosphere.SunLight.Bind`（新增） | 显式 `SunLight` 优先；未显式指定时两颗星不会认领同一盏灯 |

`SunRadiance` 的"从材质读"这一层不写死具体预设数值（预设调色会红测试）；
改为断言三个预设的相对亮度顺序 `M2V < G2V < B3V`、且颜色互不相同。

### 7.2 构建与运行

1. 关编辑器 → UBT 构建（`CLAUDE.md` 的命令，不要用 `Build.bat`）。
2. 跑 `Automation RunTests StellarEra`，全绿。
3. 编辑器里跑一次 `StellarEra.BuildAtmosphereMaterial`（重建材质，带上 9 个新参数）。
4. PIE 进 `UniverseMap`：
   - 视口/日志里那条"多个定向光源竞争"的警告消失；
   - 三盏灯的朝向各自指向行星，明暗分界跟着最近的那颗星走；
   - 背光面能看到另外两颗星补的微光（不是纯黑）；
   - 云在夜面不出现"黑洞"（`CLOUD_AMBIENT × Radiance[0]` 仍生效）。
5. `stat unit` 与改动前对比，记录 GPU 时间差。

### 7.3 判据

- 截图对比：单星情况（临时把 `SunCount` 强制为 1）应与改动前逐像素接近；
  差异只应来自恒星颜色（G2V 是暖白，不是纯白）。
- 引擎警告消失的唯一证据是日志/视口里那条文本不再出现，不是"看起来对了"。

## 8. 风险与退路

| 风险 | 观察点 | 退路 |
|---|---|---|
| 4 次 `[unroll]` 的太阳循环把 march 的指令数推高 | `stat unit` GPU 时间 | 把 `PLANET_MAX_SUNS` 降到 3（关卡正好 3 颗），只改一个宏 |
| `[unroll]` + 运行期 `continue` 在某个平台编译失败或退化 | 材质编译日志；帧率 | 改成 4 段手写 `if (SunCount > n)` 展开，语义等价 |
| 从材质读参数在打包/非编辑器环境失效 | PIE 里颜色是否随 `MI_Sun_*` 变化 | 读失败已回退白色 × 1.0，最坏是失去色调差异，不会黑屏 |
| 主星切换时灯的属性写入造成一帧闪烁 | PIE 长时间观察三体运动 | 主星切换加距离滞回；朝向本来就每帧在改 |
| 三颗星同时照使受光面过曝 | 截图目视 | 调 `BaseLightIntensity` 与 `SunIntensity`，不改结构 |

## 9. 开放问题

无。三处此前待定的取舍已定：
一星一灯、求和 + 主星才算自遮挡、颜色与亮度从恒星材质读。
