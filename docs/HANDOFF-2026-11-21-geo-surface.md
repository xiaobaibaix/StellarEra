# 交接：地表按体素高程着色（未完成）

- 日期：2026-11-21
- 分支：geo-pipeline
- 状态：**数据链路已通，画面颜色未验证通过**

## 目标

地表颜色由**每个体素自己的高程**决定（深海蓝 → 浅海青 → 沙滩 → 平原绿 → 高地棕 → 雪顶），
且接受恒星光照（有明暗），与地质查看器同一套配色。

## 已完成

1. `FGeoSample` 新增 `uint8 Elevation`（`Geo/Snapshot/GeoMaterialCodec.h`）
2. `GeoMaterialCodec::Encode/Decode` 把它映射到 **V1**（UV1.y），`PlanetGeoGenerator.cpp:107` 已写 `SetV1`
3. `PlanetMaterialEval::Sample` 计算：
   ```cpp
   T = (E - (MinSurfaceRadius - R0)) / (MaxSurfaceRadius - MinSurfaceRadius)
   Out.Elevation = clamp(round(T * 255), 0, 255)
   ```
4. `GeoMaterialBuilder.cpp`：`M_GeoPlanet` 的 BaseColor 改为
   `Lerp(RockColor, ElevationColor, SurfaceMask)`，`ElevationColor` 由
   `Step(SeaLevel, UV1.y)` + 6 段 Lerp 组成；`SeaLevel` 是 MID 参数，由 C++ 按快照算。
5. 立方图方案（`M_GeoSurface`、`GeoSurfaceMaterialBuilder.cpp`）已弃用，但仍留在仓库里。

## 现在的现象

- 地表灰白（≈色带的最高端），地形起伏和光照正常。
- MID 上 `SeaLevel = 0.543`，说明材质确实重建过、参数也在。

## 下一步怀疑点（按顺序）

1. **UV1.y 的真实值**。最可能它恒为 1（→ 色带顶端 = 灰白）。验证方法：把材质 BaseColor
   临时接成 `UV1.y` 的灰度输出，看是 0、1 还是连续变化。
2. 若 UV1.y 恒为 0：`PlanetGeoGenerator.cpp:107` 的 `SetV1(P.V1)` 有没有真的进到网格 UV1，
   看 `Voxel - UVConfig` / `bOneMaterialPerCubeSide` 之类开关。
3. 若 UV1.y 连续变化但颜色仍不对：色带分段（`SeaLevel` 与 6 段 Lerp 的位置）。

## 已知坑（本次踩过）

- `UMaterialExpressionIf` **只输出标量**，颜色接进去会变灰度。用 `Step + Lerp`。
- 材质重建命令是 `StellarEra.BuildGeoMaterials`（复数）。
- 改完 C++ 必须重建 `MF_GeoDecode` / `M_GeoPlanet`，光重编模块画面不变。
