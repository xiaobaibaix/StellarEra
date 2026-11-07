# 行星天光（SkyLight）— 设计

- **日期**：2026-11-07
- **项目**：StellarEra（Unreal Engine 5.8）
- **模块**：`StellarEraPlanet`（`Atmosphere/`、`Sky/`、`VoxelPlanetActor`）
- **状态**：设计已确认，待实现
- **前置**：`2026-11-01-atmosphere-inside-sky-design.md`（球内外天空）、
  `2026-11-02-multi-star-lighting-design.md`（多恒星光源）、
  `2026-11-02-planet-volumetric-clouds-design.md`（云与大气共用一次 ray march）

---

## 1. 目标

项目不使用引擎的 `SkyAtmosphere`，行星天空由自建的半透明大气壳画出来。因此引擎
SkyLight 的默认捕获（只认 `SkyAtmosphere` / 体积云 / Sky 材质）抓不到任何东西，关卡里
那盏 SkyLight 一直用静态 `DaylightAmbientCubemap`——**白天黑夜、正午黄昏，环境光永远是同一张图**。

用户原话：

> 当前项目对于星球，需要加一个类似天光的东西，但是因为没有使用引擎的天空和大气，
> 使用的是自建的大气 actor，因此需要做一个适用于这个自建大气的天光 actor。

已确认的两个取舍：

| 问题 | 选择 |
|---|---|
| 作用范围 | **只做漫反射环境光**，不做天空镜面反射 |
| 夜面 / 深空亮度 | **用星空给夜光**：太阳照不到时环境光由星空/银河的微弱辐亮给，不留固定底值 |

## 2. 现状

- 大气壳 `UPlanetAtmosphereComponent` + `M_PlanetAtmosphere`：半透明、Unlit、双面、
  关深度测试，单次散射 ray march（`Shaders/PlanetClouds.ush`）。
- 壳的材质输出是 `float4(Acc, 1 - T)`：`Acc` 是累积散射辐亮度，alpha 是 `1 - 透射率`。
  半透明混合后**肉眼看到的天空是 `Acc × (1 - T)`**（背景按 `T` 透上来）。天光要和它
  对拍，就不能只用 `Acc`。
- 工作区里已有一份**未提交**的 `SkyFill` 定向光补光（固定蓝白、强度 2.0、每帧转到当地天顶）。
- 关卡 SkyLight 是静态 `DaylightAmbientCubemap`，`Intensity 2`。
- 引擎侧确认过的事实：
  - `UMaterial::bIsSky` 必须是 Opaque/Masked + Unlit。
  - `UPrimitiveComponent::bRenderInMainPass` 与 `bVisibleInRealTimeSkyCaptures` 正交，
    `bRenderInMainPass=false` 的 Sky 材质天球只出现在实时天空捕获里。
  - SkyLight 的 SH 是**世界空间**的，不会跟随当地天顶。
  - `USkyLightComponent::SetCubemap` 只在**指针变化**时重新卷积（源码里 `Cubemap != NewCubemap`）。
  - `UTextureCube::CreateTransient` + 直接填 `Mips[0].BulkData` + `UpdateResource()` 是引擎
    自带套路（`FImageUtils::CreateCheckerboardCubeTexture` 就这么写）。

## 3. 备选方案

| 方案 | 做法 | 结论 |
|---|---|---|
| **A：CPU 算立方图** | C++ 单次散射积分器算 `6×16²` 辐亮度 → 运行时 `UTextureCube` → 现成 SkyLight | **采用** |
| B：Sky 材质天球 + `RealTimeCapture` | 新 Opaque+Unlit+`bIsSky` 材质，天球只在实时捕获里出现 | 否：每帧 GPU 捕获，本关 2.5–3 FPS 时开销实打实；只要漫反射时它的优势（像素级一致、云自动进环境光）收益很小 |
| C：只换 `SkyFill` 的颜色 | 保留定向补光，颜色/强度改由解析式算 | 否：解决不了"环境光没有半球方向" |

选 A 的关键理由：需求只要漫反射，而漫反射只需要低阶 SH；`16²` 的立方图绰绰有余，
全套成本可以压到"每次更新不到 1 ms CPU + 一次 16² 卷积"。

## 4. 架构

### 4.1 组件归属

新增 `UPlanetSkyLightComponent`，在 `AVoxelPlanetActor` 构造函数里
`CreateDefaultSubobject`，**不放进蓝图**：

- 它需要行星中心 / 表面半径、大气参数、太阳列表，全在行星 Actor 上；
- "缺了会静默变成环境光永远是白天"属于骨架，按项目约定留 C++；
- `BP_VoxelPlanet` 不用动，新增行星自动带上。

它靠 `GetComponents<UPlanetAtmosphereComponent>()` 找同 Actor 的大气壳，拿不到就整个
组件不工作并日志一次。大气壳本身仍由蓝图承载（SCS 组件），本组件不要求它是 CDO 子对象。

SkyLight 不新建 Actor：关卡里已经有一盏，组件去驱动它。

```
绑定优先级：组件上的 EditInstanceOnly SkyLight 指针
          → 找不到则按 Actor 标签 PlanetSky 找
          → 都没有：日志一次，组件不工作
```

### 4.2 数据流

```
玩家相机位置 ──┐
行星中心/半径 ──┤
CollectSunsFor ─┼→ 世界空间变化超过阈值？──否──→ 直接返回
大气散射参数 ──┘         │是
                         ▼
          PlanetSky::RenderCube（6×16²，CPU 单次散射）
                         ▼
     UTextureCube::CreateTransient(PF_FloatRGBA, SRGB=false)，写 6 面
                         ▼
          USkyLightComponent::SetCubemap(新纹理)
                         ▼
     引擎自行卷积成 SH / 辐照度 → 漫反射环境光
```

四条必须遵守的细则：

1. **射线从玩家相机位置发，不是从行星中心。** 天空的"上"就是玩家的当地天顶，世界空间
   SH 的 L1 项因此自动对齐玩家脚下；玩家绕球走到别处时重算。行星半径十几公里时地平线
   只有几百米远，看得见的地形都在玩家附近，这个近似够用。
2. **每帧新建 `UTextureCube`。** `SetCubemap` 只在指针变化时触发重卷积，复用同一张会让
   环境光停在第一次的数值。
3. 立方图存**线性 HDR 辐亮度**，`PF_FloatRGBA` + `SRGB=false`。8 位会把夜空和阴影里的
   弱光压成 0。
4. 只在玩家处于本行星影响范围（默认 `4 × 壳半径`）内时驱动。深空里最近的那颗行星组件
   照样算——从太空位置往外看全是星空，环境光自然落到星空的微弱辐亮。

### 4.3 更新节流

- 最小间隔 `UpdateIntervalSeconds = 0.2`；
- 满足任一条才重算：太阳方向（从观察点看）变化 > `1°`、当地天顶变化 > `1°`、
  高度变化 > `RayleighScaleHeight × 0.25`、大气参数被改。
- 立方图边长 `CubeResolution = 16`（可调 8 / 16 / 32）。

成本量级：`16² × 6 × ~24 步 ≈ 3.7 万次循环`，单次亚毫秒；真正的大头是 `SetCubemap`
触发的一次 GPU 卷积 + SH 回读，`16²` 下也很小。5 Hz 更新对 2.5 FPS 的场景可接受。

### 4.4 失效与降级

| 情况 | 行为 |
|---|---|
| 找不到大气壳 / SkyLight / 玩家相机 | 组件不工作，各日志一次 |
| SkyLight 是 Static Mobility | 首次驱动时改 Movable（和 `DriveSunLights` 处理补光灯一致） |
| 立方图创建失败 | 日志一次，跳过本次更新 |
| 编辑器世界 | 不驱动，避免每帧把关卡标脏；编辑器里看到的是原静态立方图 |
| 多个行星同时在天光范围内 | 各自都会驱动，最后 tick 的那颗生效——记为已知限制，不引入全局仲裁 |

## 5. 天空模型

新文件 `Sky/PlanetSkyModel.h/.cpp`，`namespace PlanetSky`，纯 C++（无 UObject），
输入输出都是普通结构体，可直接单测。

### 5.1 输入

- 观察点、行星中心（cm）、表面半径、壳半径；
- `PlanetAtmosphere::FShellScatteringParams`（Rayleigh/Mie 系数与标高，已有函数推导）；
- 太阳列表（单位方向 + 辐亮度，复用 `UPlanetAtmosphereComponent::CollectSunsFor`），
  上限沿用 `PlanetClouds::MaxSuns`（当前 4）；
- `SunIntensity` 补偿系数（沿用壳的 14）；
- `CloudCoverage`（只做一阶衰减）；
- `SpaceSkyRadiance`：星空/银河的平均辐亮。

### 5.2 逐方向计算

与壳着色器同构：

1. 射线-壳球求交；撞到行星表面则截断；
2. 沿 `[tEnter, tExit]` 做单次散射积分：Rayleigh + Mie 密度 `exp(-h/H)`、两个相位函数、
   最多 4 颗太阳的透射率；
3. 逃逸到太空的方向补 `SpaceSkyRadiance`；
4. 乘 `SunIntensity`，**再乘 `(1 - T)`**，与壳的半透明合成结果对齐；
5. 云只做一阶衰减：`× (1 - CloudCoverage × CloudAmbientOcclusion)`，不参与积分——
   云要真参与就得把整段噪声搬进 C++，收益不划算。

### 5.3 立方图约定

- 6 面 × `CubeResolution²`，面上像素存 `PF_FloatRGBA`；
- 面序与"方向 → UV"的映射必须与引擎的立方图取样约定一致，否则环境光会从错误的方向来。
  这两条**不凭记忆写**：实现时先读 `FTextureCubeResource` 的面序与引擎的立方图→SH 路径，
  再落代码；spec 这里只登记为必须核对的事实。

## 6. 编辑器与关卡接线

1. 关卡 `UniverseMap` 现有 SkyLight：`Mobility = Movable`、`SourceType = SpecifiedCubemap`、
   `Intensity = 1`、`LightColor = 白`、Actor 打标签 `PlanetSky`；
2. 组件在行星上自动存在，无需放置；
3. `SkyFill` 定向光**本次不删**：先把 `SkyFillIntensity` 留在组件上，PIE 对比过再决定
   是删掉还是保留成可关的兜底。

## 7. 测试

自动化测试 `StellarEra.Planet.SkyLight.*`（纯模型，不需要世界）：

| 用例 | 断言 |
|---|---|
| 正午 | 天顶 B/R 高于地平线 |
| 日出/日落 | R/B 高于正午 |
| 夜面 | 总辐照度 < 正午的 1% |
| 单色太阳 + 单色星空 | 输出单调，且下半球方向只有星空项 |
| 面方向 | 只在 +Z 打一颗太阳 → +Z 面平均亮度最大 |

面方向这条只能锁住"我们自己前后一致"，**不能证明与引擎约定一致**；与引擎的对齐要在
PIE 里看白模球的环境光方向确认（实现时先读引擎的立方图→SH 实现再落代码）。

运行时验证（`UniverseMap` PIE，需人工）：

- 站在晨昏线附近看白模球，环境光应从太阳一侧滚到另一侧；
- 夜里地面降到接近黑，只剩星空给的微光；
- 水面 / 飞船金属的漫反射颜色跟着日色变化。

## 8. 文件清单

| 动作 | 文件 |
|---|---|
| 新增 | `Source/StellarEraPlanet/Src/Sky/PlanetSkyModel.h/.cpp` |
| 新增 | `Source/StellarEraPlanet/Src/Sky/PlanetSkyLightComponent.h/.cpp` |
| 新增 | `Source/StellarEraPlanet/Src/Tests/PlanetSkyModelTests.cpp` |
| 修改 | `Source/StellarEraPlanet/Src/VoxelPlanetActor.h/.cpp`（CDO 子对象） |
| 修改 | 关卡 `UniverseMap`（SkyLight 的标签 / Mobility / 参数） |
| 可能修改 | `PlanetAtmosphereComponent`（暴露大气参数供天光读取；若现有 public 属性够用则不改） |

## 9. 风险与未决项

- **双实现漂移**：C++ 模型是壳着色器的第二份实现。单测只锁性质（色相、单调性、量级），
  不锁像素。留一个校准倍率参数，画面不一致时调它，不改物理量。
- **面方向约定**：见 7 节，单测锁不住与引擎的一致性，必须 PIE 目视确认。
- **世界空间 SH 只在玩家附近正确**：离开玩家几公里的地形会拿到斜掉的环境光方向。
  行星曲率把远处挡住，实际影响小。
- **深空环境光接近黑**：若飞船在太空里轮廓太难读，调高 `SpaceSkyRadiance` 或另加补光。
- **云只有一阶衰减**：阴天环境光会比可见云层略亮。
- **编辑器不驱动**：编辑器视口里天光仍是静态立方图，改大气参数不会即时反映。
- **`SkyFill` 去留**：验证后再定。

## 10. 实施记录（2026-11-07）

PIE `UniverseMap`，编辑器在前台（约 28–31 FPS），立方图边长 16、太阳 3 颗。日志过滤 `[天光]`：
**没有任何 Warning**（缺大气壳 / 缺 SkyLight / 缺相机三条都没出现）。

- **立方图重算节奏**：站着不动（飞船随行星走，只有太阳方向在变）62 s 内 11 次，
  间隔 5.29–5.38 s（均值 5.34 s = 1° 阈值 ÷ 约 0.19°/s）；移动时（视图换到一台绕行星表面
  30°/s 运动的相机）8.01 s 内 36 次，间隔 0.20–0.27 s（均值 0.22 s，≈4.5 Hz），贴着
  `UpdateIntervalSeconds = 0.2` 的上限，**不是每帧**。PIE 刚起来的前约 10 s 有一段逐帧重算
  （相机/行星就位中），之后回落到稀疏节奏。
- **单次 CPU 积分耗时**（边长 16、3 颗太阳，日志 `用时`）：移动窗口 1.49–9.77 ms（均值 4.17 ms）；
  定点连续 10 次 `RefreshNow()` 为 5.3–7.2 ms（均值 5.9 ms）。编辑器失焦（3 FPS 限速）时会被
  拉到 5.2–20.8 ms，该组不可用。**结论：不是预期的「远小于 1 ms」**——`用时` 覆盖 `RenderCube`
  + 每次新建 `UTextureCube` + `UpdateResource` + `SetCubemap` 全段，日志没把积分本身拆出来；
  成本随画面内容变（夜面 1.5–2.5 ms，白天 5–7 ms）。按 5 Hz 上限折算，最坏约 3% 单线程。
- **面序与引擎约定是否一致**：本次**没有做白模球对照**，不能确认。画面里没有出现「环境光从
  太阳反方向来」的明显异常，但这不算证据。<待控制器复核>
- **天空亮度与壳的对拍**：`BrightnessScale` 保持 1.0，未做壳 vs 立方图的区域亮度对拍，未调倍率。
  <待控制器复核>
- **`SkyFill` 去留**：见 10.2，本任务未删任何代码；结论留给控制器。

### 10.1 先决条件：关卡里大气与 SkyFill 灯的状态

PIE 里查到、并在退出 PIE 后用编辑器世界复核（脏包 0）的状态：

- `BP_VoxelPlanet` 的大气组件 `bAtmosphereEnabled = False`、`IsVisible() = False`；
- 关卡里**没有** `SkyFill` 标签的定向光。证据：`LogTemp` Warning
  `[大气] 关卡里没有 SkyFill 标签的定向光，直射光没人补，晨昏线会剩硬边等高线。`（15:02:20.837）；
- SkyLight 是任务 3 的状态：标签 `PlanetSky` / Movable / SpecifiedCubemap / 强度 1.0 / 初始无立方图。

所以下面的天光/大气截图与 A/B 全部是 **PIE 运行时**把 `bAtmosphereEnabled` 重新打开
（再调 `RefreshAtmosphereGeometry()`）、并临时 `summon` 一盏打了 `SkyFill` 标签的定向光做的；
**全程没有保存关卡**（退出 PIE 后脏包 = 0）。

### 10.2 截图与 `SkyFill` 数值（`/d/Temp/ue_mcp/`，PIE 运行时）

| 文件 | 相机 / 场景 | 客观描述 |
|---|---|---|
| `task4_day.png` | 日面，地表上方 100 m，局部俯角 15°，朝太阳水平方向 | 太阳圆面在地平线上方；天空由地平线白过渡到顶部蓝；地面浅绿近白、过曝。该机位三颗太阳仰角 28.0° / -2.6° / -42.4° |
| `task4_day_side.png` | 同上位置，改朝太阳的侧向 | 地面占下 70%（浅绿白），顶部一条蓝色天空 |
| `task4_day_ground.png` | 同方位，30 m 高、俯角 30° | 近距离平地，整幅浅绿白；**没有可见投影**（该处地形低起伏） |
| `task4_day_hill.png` | 方位角 125°（扫描到的最高地形附近），50 m 高、俯角 8° | 明亮太阳在画面上部，蓝天 + 浅色地面 |
| `task4_night.png` | 夜面（三颗太阳仰角 -74.1° / -49.5° / -49.8°），局部俯角 15° | 星空 + 星云；地面与天空都近黑，肉眼分不出地平线 |
| `task4_night_sky0.png` | 夜面（**旧构图**），SkyLight 强度 0 | 与 23:16 同构图、强度 1 的那张成对测量：画面下 1/3 均值 0.72（开）/ 0.06（关）/255；当前 `task4_night.png` 已重拍为新构图 |
| `task4_term_fill2.png` / `task4_term_fill0.png` | 晨昏线（主星仰角 0.00°），**关卡默认无 SkyFill 灯** | 两图差值为 TAA 噪声级：整图均值差 0.07/255，>2 的像素 1.4% —— 调 `SkyFillIntensity` 无效 |
| `task4_term_fill2_lit.png` / `task4_term_fill0_lit.png` | 同机位，**运行时临时加**的 SkyFill 灯 | 地面下半幅均值 111.0（fill 2）vs 84.9（fill 0）；天空上半幅 155.0 vs 154.9 |

`SkyFill` 判断数据：补光**只抬地面**（+26.2/255，约 +31%），天空基本不动，符合「整天穹补光」的设计；
天光单独照明时晨昏线地面仍有 84.9/255（暗蓝、非死黑），夜里地面约 0.7（开）/ 0.06（关）/255。
<待控制器复核：截图路径与保留 / 建议删除的结论>

### 10.3 测试

`Automation RunTests StellarEra.Planet.SkyLight`：**9/9 Success**（CloudsDimSky、CubeShape、
FaceCenters、FaceCorner、GroundIsBlack、NightIgnoresSun、OutsideShellIsStar、
PlanetHasComponent、SunElevationMonotonic），日志 `D:/Temp/skylight_test.log`。
