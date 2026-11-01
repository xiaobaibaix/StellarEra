# 行星体积云设计

**目标**：给体素行星加一层可见的体积云 —— 从轨道能看到成块的云团与缝隙，从地表抬头能看到云在动。
**范围**：只做云。体积光（god rays）另行设计。

## 1. 非目标

- 云影投到地面。云是半透明介质，不参与 SSGI/光照，落地阴影需要另一套机制，本次不做。
- 体积纹理、时间重投影、降噪。第一版全程序化。
- 与引擎 `AVolumetricCloud` 的任何互操作。

## 2. 架构：并入现有大气壳，不做第二层壳

大气壳与云壳是**同心球**。如果做成两层独立的半透明壳，绘制顺序只能全局唯一（`TranslucencySortPriority`），而两种视角要求的顺序相反：

| 视角 | 沿射线谁在前 | 正确的合成顺序 |
|---|---|---|
| 球内抬头 | 云壳背面比大气壳背面近 | 先大气，后云 |
| 球外看行星 | 大气壳正面比云壳正面近 | 先云，后大气 |

不存在同时满足两边的固定顺序。因此把云**并入同一次 ray march**：同一条射线依次累积大气与云，顺序天然正确，且只有一次 overdraw（比两层壳更便宜）。

代价：`M_PlanetAtmosphere` 的 HLSL 变长，云与大气的步数绑在一起（统一 32 步）。云的开关由 `CloudOpticalDepth = 0` 实现。

## 3. 参数（全部挂在 `UPlanetAtmosphereComponent`）

新增一个 `StellarEra|Clouds` 分类，与既有的 `StellarEra|Atmosphere` 并列。

| 参数 | 默认值 | 单位 | 含义 |
|---|---|---|---|
| `bCloudsEnabled` | `true` | — | 总开关；关闭时把光学厚度传 0 |
| `CloudBaseRatio` | `0.02` | 表面半径的倍数 | 云底高度 |
| `CloudTopRatio` | `0.07` | 表面半径的倍数 | 云顶高度，必须 < `AtmosphereHeightRatio` |
| `CloudCoverage` | `0.45` | 0..1 | 0 = 晴空，1 = 全阴 |
| `CloudOpticalDepth` | `6.0` | 无量纲 | 满密度、垂直穿层的总光学厚度 |
| `CloudNoiseScale` | `8.0` | 无量纲 | 天气图频率（归一化方向上的噪声坐标） |
| `CloudDetailScale` | `24.0` | 无量纲 | 细节侵蚀频率 |
| `CloudDetailStrength` | `0.35` | 0..1 | 细节侵蚀强度 |
| `CloudDriftPerSecond` | `0.02` | 噪声坐标/秒 | 云随时间漂移；0 = 静止 |

**光学厚度而非消光系数**：`β_cloud = CloudOpticalDepth / (CloudTopCm - CloudBaseCm)`。
这样换一颗更小/更大的行星时云的浓淡不变 —— 与 `PlanetAtmosphere::ComputeShellScatteringParams` 用 τ 反推 β 是同一个理由。

`CloudBaseRatio` / `CloudTopRatio` 必须小于 `AtmosphereHeightRatio`，否则云会长到壳外，被射线求交直接裁掉。组件在应用参数时做钳制（Top 不得超过壳半径，Base 不得超过 Top）。

## 4. 密度模型

以行星中心为原点，`R = length(p - PlanetCenter)`：

```
h        = (R - CloudBaseCm) / (CloudTopCm - CloudBaseCm)        // 0..1
profile  = smoothstep(0, 0.15, h) * smoothstep(1, 0.72, h)      // 底部平、顶部圆润
if (h <= 0 || h >= 1) density = 0                                // 早退，云层外的步不付噪声代价

q        = normalize(p - PlanetCenter) * CloudNoiseScale
w        = fbm3(q + Drift, 3 层)                                 // 3D 噪声按方向采样 → 球面无接缝
d        = saturate((w - (1 - CloudCoverage)) / max(CloudCoverage, 1e-3))
d       *= profile
d       -= CloudDetailStrength * fbm3(q * 3, 2 层) * (1 - d)     // 丝状侵蚀
d        = saturate(d)

density  = d * β_cloud
```

覆盖率到阈值的映射放在纯函数 `PlanetClouds::CoverageToNoiseThreshold(Coverage) = 1 - Coverage`（钳到 0..1）里，与 HLSL 里的公式一致，可单测边界。

**为什么按方向取 3D 噪声而不是经纬度 2D**：经纬度在极点会挤成一团，并且在 ±180° 有接缝。3D 噪声按单位方向采样没有这两个问题。

## 5. 光照与积分

在既有的循环里，每一步：

1. **行星本影**：复用现有 `sunVis`（`bS < 0` 时求 P 到太阳射线的垂距）。
2. **云自遮挡**：朝太阳方向额外采 `CLOUD_SHADOW_STEPS`（3）个点，累乘 `exp(-density * 步长)`。云顶亮、云底暗全靠它。
3. **相位**：双瓣 Henyey-Greenstein，前向 `g = 0.6` 权重 0.7、后向 `g = -0.15` 权重 0.3。
4. **消光合并**：`extinction = 瑞利 + 米氏 + 云`（云取灰度）。
5. **散射累加**：大气项与云项分别按各自相位加权后相加，最后统一乘透射率。

输出结构不变：`float4(accumulated * SunIntensity.rgb, coverage)`，`coverage = 1 - mean(transmittance)`。

步数：`ATMO_STEPS` 16 → 32。云层外（`h <= 0 || h >= 1`）早退，不付噪声代价。按当前参数云层占壳厚的 **56%**（`(0.07-0.02)/0.09`），约 18 步落在云层内。

## 6. 改动点

| 文件 | 改动 |
|---|---|
| `Source/StellarEraPlanet/Src/Atmosphere/PlanetCloudParams.h/.cpp` | **新建**。`FCloudLayerRadii`、`ComputeCloudLayerRadiiCm`、`CoverageToNoiseThreshold`。纯函数，不含 UObject |
| `Source/StellarEraPlanet/Src/Atmosphere/PlanetAtmosphereComponent.h/.cpp` | 加 9 个 UPROPERTY；`RefreshAtmosphereGeometry` 里推新参数；`TickComponent` 里每帧写 `CloudDrift`；应用参数前钳制比例 |
| `Source/StellarEraPlanet/Src/Atmosphere/PlanetAtmosphereMaterialBuilder.cpp` | HLSL 加噪声/密度/相位；加 8 个标量参数节点；`ATMO_STEPS` → 32 |
| `Source/StellarEraPlanet/Src/Tests/PlanetCloudParamsTests.cpp` | **新建**。纯函数测试 |
| `Source/StellarEraPlanet/Src/Tests/PlanetAtmosphereMaterialTests.cpp` | 扩展既有两条测试：新增参数存在、HLSL 含云关键字 |

`Content/Code/Planet/Materials/M_PlanetAtmosphere.uasset` 由 `StellarEra.BuildAtmosphereMaterial` 重新生成，不算代码改动但必须重跑。

## 7. 性能与降级

- **估**（非实测）：1080p 全屏 2–4 ms。云层内每步 1 次 3 层 FBM + 3 次自遮挡采样，是主要开销。
- 降级顺序：`CloudDetailStrength = 0`（省 2 层 FBM） → `CloudOpticalDepth = 0`（关云） → `bAtmosphereEnabled = false`（关整层）。
- 验收必须先让编辑器在前台：后台整进程硬限 3 FPS（见 CLAUDE.md「已知坑」）。

## 8. 测试

| 测试 | 断言 |
|---|---|
| `StellarEra.Planet.CloudParams.Radii` | 云底/顶 = 表面半径 × 比例；Top < 壳半径；比例倒挂时钳成 Top = Base |
| `StellarEra.Planet.CloudParams.Coverage` | `Coverage=0` → 阈值 1（永不超阈）；`=1` → 0；越界钳制 |
| `StellarEra.Planet.AtmosphereMaterial.Structure`（扩展） | `CloudBaseRadius`/`CloudTopRadius`/`CloudCoverage`/`CloudExtinction`/`CloudNoiseScale`/`CloudDetailScale`/`CloudDetailStrength`/`CloudDrift` 八个标量参数存在 |
| `StellarEra.Planet.AtmosphereMaterial.Hlsl`（扩展） | HLSL 含 `CloudDensity`、`fbm3`、`CLOUD_SHADOW_STEPS`、`CloudBaseRadius` |

材质测试必须跑在重建过材质之后 —— 与既有约定一致（`BuildAtmosphereMaterial` 没重跑就红）。

## 9. 风险与未验证项

1. **条带**：壳内 32 步里约 18 步穿云层，云层厚 1.09e5 cm 时步长 ≈ 6000 cm，可能出现层状条带。备选：云层区间单独加密（`CLOUD_EXTRA_STEPS`），计划里留成可切换的常量。
2. **HLSL 长度**：`MaterialExpressionCustom` 的代码长度没有硬限制，但采样器数量有上限。云不用纹理，不新增采样器。
3. **双面剔除**：云与大气共用同一个片元与同一次早退，不需要额外处理。
4. **云在壳内、壳关深度测试**：云与大气一样靠 `SceneDepthCm` 截断，云不会盖住比它近的飞船/地形。
