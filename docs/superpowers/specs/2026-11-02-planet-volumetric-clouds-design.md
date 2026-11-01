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
| `CloudBaseRatio` | `0.02` | 高度 / 表面半径 | 云底高度（绝对半径 = r × (1 + 比例)） |
| `CloudTopRatio` | `0.07` | 高度 / 表面半径 | 云顶高度（同上），必须 < `AtmosphereHeightRatio` |
| `CloudCoverage` | `0.45` | 0..1 | 0 = 晴空，1 = 全阴 |
| `CloudOpticalDepth` | `6.0` | 无量纲 | 满密度、垂直穿层的总光学厚度 |
| `CloudNoiseScale` | `8.0` | 无量纲 | 天气图频率（归一化方向上的噪声坐标） |
| `CloudDetailScale` | `24.0` | 无量纲 | 细节侵蚀频率 |
| `CloudDetailStrength` | `0.35` | 0..1 | 细节侵蚀强度 |
| `CloudDriftPerSecond` | `0.02` | 噪声坐标/秒 | 云随时间漂移；0 = 静止 |
| `CloudBrightness` | `1.0` | 倍率 | 云的亮度补偿。云与大气共用 `SunIntensity`（14，为单次散射的大气补的），直接照搬会把云顶推到溢出白；补偿只加在云项上，不改 `SunIntensity` |

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
w        = fbm3(q + Drift, 3 层)                                 // 3D 梯度噪声，按方向采样 → 球面无接缝
d        = saturate((w - Thresh(Coverage)) / 0.04)
d       *= profile
d       *= saturate(1 - CloudDetailStrength * fbm3(q * 3, 2 层)) // 细节只调不透明度
d        = saturate(d)

density  = d * β_cloud
```

**阈值必须映射到噪声的实际取值区间。** 教科书写的是 `Thresh = 1 - Coverage`，那假设 `w` 在 [0,1] 上均匀；但 fBm 是若干八度相加，分布是钟形的。实测这颗行星上的 `w`：均值 0.497、标准差 0.050、区间 [0.243, 0.631]。用 `1 - Coverage` 时 Coverage=0.45 把阈值放在 0.55，即均值 **+1σ** —— 只有三分之一的方向穿过阈值，且穿过的样本平均密度只有 0.07（τ≈0.4），画面上是一层灰雾加暗斑，不是云。所以改成在实测区间上线性映射：`Thresh = lerp(0.63, 0.36, Coverage)`。密度斜坡 0.04 也必须相对 0.050 的标准差足够小，否则整段分布被斜坡吃掉、永远没有不透明的地方。

纯函数 `PlanetClouds::CoverageToNoiseThreshold` 与 HLSL 的 `CLOUD_THRESHOLD_AT_ZERO` / `CLOUD_THRESHOLD_AT_ONE` 是同一个式子，测试里钉了「阈值必须落在噪声区间内」这条不变量。

**细节项只能是乘法，不能从密度里减。** 减法等于以 3 倍频率重新做一次阈值，云边缘变成一条硬等值面；而 12~48 步的 march 把这个硬边场重建成满屏的**三角格**（值噪声是分片三线性，等值面本来就是多边形）。改成有界乘法后不再产生新的等值面，同样的采样率下就没有格子了。

**噪声用梯度噪声而不是值噪声。** 值噪声的等值面同样是分片多边形，细节一硬就开始显出格子；梯度噪声在每个频率上等值面都是光滑的。八度之间还乘一个正交旋转矩阵，避免相邻八度的格点对齐。

**为什么按方向取 3D 噪声而不是经纬度 2D**：经纬度在极点会挤成一团，并且在 ±180° 有接缝。3D 噪声按单位方向采样没有这两个问题。

## 5. 光照与积分

在既有的循环里，每一步：

1. **行星本影**：复用现有 `sunVis`（`bS < 0` 时求 P 到太阳射线的垂距）。
2. **云自遮挡**：朝太阳方向额外采 `CLOUD_SHADOW_STEPS`（3）个点，累乘 `exp(-density * 步长)`。云顶亮、云底暗全靠它。
3. **相位**：双瓣 Henyey-Greenstein，前向 `g = 0.6` 权重 0.7、后向 `g = -0.15` 权重 0.3。
4. **消光合并**：`extinction = 瑞利 + 米氏 + 云`（云取灰度）。
5. **散射累加**：大气项与云项分别按各自相位加权后相加，最后统一乘透射率。

输出结构不变：`float4(accumulated * SunIntensity.rgb, coverage)`，`coverage = 1 - mean(transmittance)`。

**昼夜两面都要能看。** 纯单次散射在行星本影里（`sunVis = 0`）和深度自遮挡处会把云压成 0，而云照样挡着后面的天空 —— 结果是盘面上一块块**黑洞**。所以云项取 `max(CloudLight, CLOUD_AMBIENT)`（0.25），代表多次散射加地面反照，夜面才不会比地表还黑。

步数：整条射线**按云层分段**积分，不是均匀铺满整条弦。`[tEnter, tExit]` 被切成「云前 / 云中 / 云后」三段，累加器 `Acc` 与透射率 `Tr` 用 `inout` 跨段传递，等价于一次步长非均匀的 march。

- 云前、云后两段各 `ATMO_STEPS`（24）步。瑞利/米氏是低频光滑量，不需要更密。
- 云中那段步数自适应：`ceil(层长 / (0.4 × 云厚))`，钳在 `[CLOUD_MIN_STEPS 12, CLOUD_STEPS 48]`。正对看穿一层云厚得 12 步，掠射到地平线时同一条射线在云层里能待 6 倍长，固定步数会沿轮廓变成扇贝状条带。
- 云层外（`h <= 0 || h >= 1`）早退，不付噪声代价。

早先的做法是整条弦均匀 32~64 步，云层只占壳厚 56% 时平均只有一个多采样落在云里 —— 消光加上、散射没积到，画面上是硬边黑楔形。

**改 `.ush` 必须改版本号。** `FMaterial::GetExpressionIncludesHash()` 名义上把 include 内容算进 shader map id，但实测改 `.ush` 后重生成材质、甚至 `recompile_material()` 都得到像素级一致的画面。现成办法是在 Custom 节点的 Code 开头写 `#define PLANET_CLOUDS_VERSION N`，每次改完 `.ush` 就把 N 加一，材质才会真正重编。

## 6. 改动点

| 文件 | 改动 |
|---|---|
| `Source/StellarEraPlanet/Src/Atmosphere/PlanetCloudParams.h/.cpp` | **新建**。`FCloudLayerRadii`、`ComputeCloudLayerRadiiCm`、`CoverageToNoiseThreshold`。纯函数，不含 UObject |
| `Source/StellarEraPlanet/Src/Atmosphere/PlanetAtmosphereComponent.h/.cpp` | 加 10 个 UPROPERTY（1 个开关 + 9 个数值）；`RefreshAtmosphereGeometry` 里推新参数；`TickComponent` 里每帧写 `CloudDrift`；应用参数前钳制比例 |
| `Source/StellarEraPlanet/Src/Atmosphere/PlanetAtmosphereMaterialBuilder.cpp` | HLSL 只剩剔除/求交/地面/调一次 `CloudAtmosphereRaymarch`；积分与噪声在 `Shaders/PlanetClouds.ush`；加 9 个标量参数节点；Code 顶部带 `PLANET_CLOUDS_VERSION` 缓存版本号 |
| `Shaders/PlanetClouds.ush` | **新建**。梯度噪声、fBm、密度、双瓣相位、分段积分 |
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
| `StellarEra.Planet.CloudParams.Radii` | 云底/顶 = 表面半径 × (1 + 比例)，且必须 > 表面半径；Top < 壳半径；比例倒挂时钳成 Top = Base |
| `StellarEra.Planet.CloudParams.Coverage` | `Coverage=0` → 0.63、`=1` → 0.36；`0.45` → 0.5085；两个端点必须落在噪声实测区间 [0.243, 0.631] 内；越界钳制 |
| `StellarEra.Planet.AtmosphereMaterial.Structure`（扩展） | `CloudBaseRadius`/`CloudTopRadius`/`CloudCoverage`/`CloudExtinction`/`CloudNoiseScale`/`CloudDetailScale`/`CloudDetailStrength`/`CloudDrift`/`CloudBrightness` 九个标量参数存在 |
| `StellarEra.Planet.AtmosphereMaterial.Hlsl`（扩展） | HLSL 含 `CloudDensity`、`CloudFbm`、`CLOUD_SHADOW_STEPS`、`CloudBaseRadius`；`.ush` 含 `CLOUD_THRESHOLD_AT_ZERO`/`_AT_ONE`；**不含** `d -= DetailStrength`（减法细节会出三角格） |

材质测试必须跑在重建过材质之后 —— 与既有约定一致（`BuildAtmosphereMaterial` 没重跑就红）。

## 9. 风险与未验证项

1. **掠射处的边缘台阶**：云中段的步数虽然自适应，但上限 48 步。掠射角同一条射线在云层里能待很长，此时步长被拉大，轮廓上的云边会看出像素级的台阶（放大后可见，正常观看距离不明显）。彻底解决要加逐像素抖动（需要把 `PixelPosition` 作为 Custom 节点输入接进来），当前没做。
2. **HLSL 长度**：`MaterialExpressionCustom` 的代码长度没有硬限制，但采样器数量有上限。云不用纹理，不新增采样器。
3. **双面剔除**：云与大气共用同一个片元与同一次早退，不需要额外处理。
4. **云在壳内、壳关深度测试**：云与大气一样靠 `SceneDepthCm` 截断，云不会盖住比它近的飞船/地形。
