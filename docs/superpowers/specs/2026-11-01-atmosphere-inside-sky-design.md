# 行星大气：球内天空 — 设计文档

- **日期**：2026-11-01
- **项目**：StellarEra（Unreal Engine 5.8）
- **状态**：设计已批准，待生成实现计划
- **前置文档**：`2026-10-18-planet-atmosphere-design.md`、`2026-10-19-units-design.md`

---

## 1. 背景

当前大气只有"从太空看的一圈辉光"，**站在行星表面抬头是纯黑的**。

原因不是壳写坏了，而是 `2026-10-18-planet-atmosphere-design.md` §5.4 在 2026-10-19 把"行星表面天空"整条撤销了（提交 `ef65a5a`），关卡里的 `ASkyAtmosphere` 也被删除。壳又是有意的单面材质（`TwoSided = false`）——球内看到的是背面，被背面剔除，整片不渲染。

编辑器实测状态（2026-11-01）：

- `UniverseMap` 的外部 Actor 里只有 1 盏 `DirectionalLight` + 1 盏 `SkyLight`，**没有 `ASkyAtmosphere`**。
- 三颗行星各挂了一个 `UPlanetAtmosphereComponent`（外部 Actor `5/8M`、`7/Z4`、`E/I0`）。
- `Content/Code/Star/` 有 `M_Sun` 与三个光谱型 MI，但**没有任何关卡 Actor 引用它们**。

结果：球外"有一点效果"，球内"完全空的"。

## 2. 目标

球内视角有大气：

- 抬头不是黑的，有天空颜色；
- 地平线附近比天顶更亮更白；
- 亮侧跟随太阳方向，背阳侧变暗；
- 远处地形有轻微的空中透视，近处清晰；
- 球外的辉光观感保持不变；
- 相机穿过壳边界时无跳变。

## 3. 范围

### 本次做

- 把壳材质改成**内外两用**：球外画正面（现状），球内画背面（新增）。
- 用场景深度把散射积分截断到真实几何距离，得到正确的空中透视与遮挡。
- 组件只调整半透明排序优先级。
- 新增一条材质资产的结构断言测试。

### 本次明确不做

- **太阳盘**：交给 `M_Sun` 星体。大气负责按透射率衰减它（混合公式天然做到），不自己画圆盘。
- **环境光染色**：`SkyLight` 继续用现在那张固定 cubemap（见记忆「UniverseMap 天光已改指定立方体贴图」），不改成实时捕获。
- 云、多次散射、臭氧吸收、日食阴影。
- 引擎 `ASkyAtmosphere` 的恢复（理由见 §7）。
- 多恒星光照。

## 4. 关键前置发现

四条源码事实决定了实现路径，全部在本机 UE 5.8 源码里核过。

### 4.1 现有求解已经支持相机在球内

`PlanetAtmosphereMaterialBuilder.cpp` 的 `AtmosphereHLSL` 里：

```hlsl
float tEnter = max(-bOuter - sqrtOuter, 0.0);
float tExit  = -bOuter + sqrtOuter;
```

相机在壳内时 `-bOuter - sqrtOuter < 0`，`tEnter` 自然退化成 0，`tExit` 是另一侧出口。**求交与积分一行都不用改**，缺的只是"球内那片背面没有被渲染"。

### 4.2 双面材质用 `TwoSidedSign` 区分正反面

- 节点 `UMaterialExpressionTwoSidedSign`（`Engine/Source/Runtime/Engine/Public/Materials/MaterialExpressionTwoSidedSign.h:12`）输出 `Parameters.TwoSidedSign`。
- 符号约定在 `Engine/Shaders/Private/MaterialTemplate.ush:4641` 的 `GetFloatFacingSign`：**正面 +1、背面 −1**（D3D/Windows 分支）。
- `MaterialTemplate.ush:4718-4728` 还会乘 `ResolvedView.CullingSign` 与 `GetPrimitive_DeterminantSign`。壳的缩放是正数，行列式符号为 +1，不影响约定。

### 4.3 半透明材质可以关深度测试

`UMaterial::bDisableDepthTest`（`.../Public/Materials/Material.h:659`，`UPROPERTY(EditAnywhere, Category=Translucency)`）注释原文："Whether to draw on top of opaque pixels even if behind them. This only has meaning for translucency."

这正是球内需要的：壳在壳半径处、地形更近，不关深度测试就会被地形挡掉。

### 4.4 场景深度能换回射线距离

- 节点 `UMaterialExpressionSceneTexture`，字段 `SceneTextureId`（`MaterialExpressionSceneTexture.h:24`），枚举值 `PPI_SceneDepth`（`MaterialSceneTextureId.h:17`）。
- 它输出的值来自 `CalcSceneDepth`（`Shaders/Private/SceneTexturesCommon.ush:58`），即 `ConvertFromDeviceZ(...)` —— **视空间 Z（沿相机前向的距离，世界单位 cm）**，不是沿射线的距离。
- 换算：`tScene = SceneDepthCm / dot(rd, ResolvedView.ViewForward)`。`ResolvedView.ViewForward` 在材质着色器里可用（`MaterialTemplate.ush:2921` 就在用）。

## 5. 架构

只有一个改动面：**壳材质**。组件与数据层不动。

```
UPlanetAtmosphereComponent（每颗行星一个）
  └─ UMaterialInstanceDynamic
       └─ /Game/Code/Planet/Materials/M_PlanetAtmosphere
            ├─ TwoSided            = true      ← 球内能看到背面
            ├─ bDisableDepthTest   = true      ← 不被地形挡掉
            └─ Custom 节点（同一段 HLSL）
                 ├─ 输入新增 TwoSidedSign  ← 剔掉多余的一面
                 ├─ 输入新增 SceneDepthCm  ← 把积分截到真实几何
                 └─ tEnter / tExit / 散射积分 不变
```

同一段积分同时服务两个视角，所以**穿壳边界天然连续**：边界上 `tEnter == tExit`，两侧都输出 0。

## 6. 详细设计

### 6.1 材质本体（`BuildPlanetAtmosphereMaterial`）

在现有设置之后追加：

| 字段 | 值 | 位置 |
|---|---|---|
| `TwoSided` | `true` | 现在是 `false`（`PlanetAtmosphereMaterialBuilder.cpp:251`） |
| `bDisableDepthTest` | `true` | 新增 |

`BlendMode = BLEND_Translucent`、`ShadingModel = MSM_Unlit` 不变。

### 6.2 新增两个输入节点

1. `UMaterialExpressionTwoSidedSign` → Custom 输入 `TwoSidedSign`（`float`）。
2. `UMaterialExpressionSceneTexture`：`SceneTextureId = PPI_SceneDepth`，`bFiltered = false` → Custom 输入 `SceneDepthCm`（`float`）。

头文件：`Materials/MaterialExpressionTwoSidedSign.h`、`Materials/MaterialExpressionSceneTexture.h`。

### 6.3 HLSL 改动

只在开头加一段面剔除，在求交之后加一段深度截断。积分主体、相位函数、`coverage` 公式全部不动。

```hlsl
float3 ro = CameraPos;
float3 rd = normalize(WorldPos - CameraPos);

// --- 新增 A：面剔除 ---
// 双面材质下，正面与背面会在同一屏幕区域各出一个片元。不剔掉多余的一面，
// 大气会被混合两遍（颜色约翻倍、透射率平方），而且 ray march 白跑一遍。
// 约定：正面 +1、背面 -1（MaterialTemplate.ush:4641）。
float sideWanted = (length(ro - PlanetCenter) < ShellRadius) ? -1.0 : 1.0;
if (TwoSidedSign * sideWanted < 0.0)
{
    return float4(0.0, 0.0, 0.0, 0.0);
}

// ... 原有：与外球求交、与内球求交，得到 tEnter / tExit ...

// --- 新增 B：用场景深度截断 ---
// 关掉深度测试后，比壳更近的物体（飞船、水面）也会被这片片元覆盖。
// SceneDepthCm 是视空间 Z，除以射线与相机前向的夹角才是射线上的距离。
// 兜底：天空像素的深度是远平面，tScene 会大于 tExit，此时不截断。
float tScene = SceneDepthCm / max(dot(rd, ResolvedView.ViewForward), 0.01);
if (tScene > 0.0 && tScene < tExit)
{
    tExit = tScene;
}
if (tExit <= tEnter)
{
    return float4(0.0, 0.0, 0.0, 0.0);
}

// ... 原有：相位函数、固定步长中点法积分、coverage、返回 ...
```

> **为什么用早退而不是 `discard`**：`discard` 会让材质进入"需要 discard"的编译分支，且在没有半透明遮挡需求时是无谓的。早退在进入循环之前返回，代价只是一次分支。

### 6.4 混合为什么是对的

材质仍是普通半透明：`final = SrcColor × Opacity + DstColor × (1 − Opacity)`。
而 `Opacity = 1 − mean(T)`，于是 `DstColor × mean(T)` —— **底色被乘上的正好是透射率**。

由此四种情形自动正确：

| 视角 | tExit 被谁决定 | 结果 |
|---|---|---|
| 球内看天 | 壳的另一侧出口 | 底是黑色背景，输出就是散射光本身 |
| 球内看远处地形 | 场景深度（地面） | 地形被压暗并混入散射色 = 空中透视，且随距离连续 |
| 球外看行星 | 地面（与现状相同） | 与现在一致 |
| 球外看飞船 | `tScene < tEnter` | 空积分，Opacity = 0，飞船不被糊 |

### 6.5 组件

`PlanetAtmosphereComponent.cpp` 构造函数的 `TranslucencySortPriority` 从 `-1` 改为 `-10`。

理由：深度测试关掉之后，绘制顺序只由排序决定。壳应该先画（作为底层），水/矿物等半透明在其上。引擎语义见 `PrimitiveComponent.h`：lower sort priority draws behind objects with a higher priority。

其余全部不动：`OnRegister` / `BeginPlay` 幂等、`bGeometryApplied` 边沿触发、编辑器按半径重算、失败降级、`ResolveSunDirection`。

### 6.6 材质资产重建

改的是 C++ 里生成材质的逻辑，资产本身要重跑：

```
StellarEra.BuildAtmosphereMaterial
```

**已知坑**：重跑构建器有时会失败（同类问题在矿物材质上出现过）。失败时先删掉
`/Game/Code/Planet/Materials/M_PlanetAtmosphere` 再跑一次。构建器里已经是
`FindObject` → 复用 → `GetExpressionCollection().Empty()` → 重建，结构上支持覆盖；
删除只是兜底。

## 7. 为什么不用引擎 `ASkyAtmosphere`

`ef65a5a` 删掉的 292 行是完整的（`PlanetAtmosphereTargeting` + `UpdateSkyAtmosphereRetarget` + 单测），恢复成本很低，而且它能一次给到太阳盘、空中透视与环境光。**没有选它，是因为它在 0.75 km 半径下是否正常没有实测过**——引擎默认行星半径是 6360 km，相差四个数量级。设计文档 §7 风险 #1 当时就记了这条，退路写的正是"把壳改成双面"。

本次走退路。若以后实测引擎大气在小半径下正常，可以再评估是否换过去；两者不冲突——壳继续管球外，引擎大气管球内。

## 8. 风险

| # | 风险 | 影响 | 应对 |
|---|---|---|---|
| 1 | 引擎自带球 `/Engine/BasicShapes/Sphere` 面数不高，球内插值出的 `WorldPos` 落在弦上而不是球面，射线方向有系统性偏差；偏差随三角形面数下降 | 天空渐变可能出现条带/折面 | 先试；出现就换成高细分球（运行时可用的，不能是 EditorMeshes） |
| 2 | 球内时壳覆盖全屏，16 步 ray march 变成全屏开销 | 帧时间上升 | 先量 `stat unit`（球内/球外各一次）；不够就把步数降到 12，或按 `tScene` 与 `tExit` 的比例提前退出 |
| 3 | `bDisableDepthTest` + `SceneTexture` 在半透明材质的编译行为 | 材质编译失败或 pass 位置变化 | 实现第一步就只开这两个开关、保持 HLSL 原样编译一次，确认能过再改 HLSL |
| 4 | 重跑 `StellarEra.BuildAtmosphereMaterial` 失败 | 资产里还是旧结构 | 删资产再跑（§6.6） |
| 5 | 排序改成 `-10` 后与其他半透明的顺序变化 | 水面/矿物视图被壳盖住或反之 | PIE 里同时看水面与矿物视图 |

## 9. 验收标准

1. 站在行星表面抬头：**不是黑的**，有可见的天空颜色。
2. 绕太阳转一圈：天空亮侧跟着太阳；背阳侧明显更暗。
3. 地平线附近的颜色比天顶更亮、更白。
4. 远处地形有轻微发白，近处清晰；远近过渡连续，没有一圈硬边。
5. 从太空看三颗行星：辉光与改动前一致，边缘无硬边。
6. 飞船停在相机与行星之间时，飞船**不被雾糊住**。
7. 相机从球内升到球外穿越壳边界：无跳变、无闪。
8. 无 `LogMaterial` 编译错误、无 ensure。
9. 记录 `stat unit`：球内与球外的帧时间（不预设阈值）。

## 10. 测试方式

- **构建**：项目约定的 UBT 直调命令（`CLAUDE.md`「构建与测试」），构建前关闭编辑器。
- **材质生成**：编辑器里执行 `StellarEra.BuildAtmosphereMaterial`。
- **自动化**：
  - 现有 `StellarEra.Planet.AtmosphereParams.*` 不变。
  - 新增一条：加载 `/Game/Code/Planet/Materials/M_PlanetAtmosphere`，断言 `TwoSided == true`、`bDisableDepthTest == true`，且 7 个参数名齐全。防"改了 HLSL 忘了重生成资产"。
- **运行验证**：编辑器 → `UniverseMap` → PIE，人工过第 9 节。注意 `NewMap` 没有体素行星，验证不了这条路径。
- **回归**：不得破坏球外观感、飞船大气阻力（`SpaceshipAtmosphereDragTests`）、`AVoxelPlanetActor` 的既有行为。
