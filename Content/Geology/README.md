# 行星地质预设

一颗行星的矿物分布定义在 **UDataAsset** 里：`Content/Geology/Planets/DA_Geology_<名字>`。
JSON 预设已退役（2026-10-25）。

## 怎么改

1. 选中 `DA_Geology_Earth`，在 Details 里调 `Elements`。
2. 选中行星 Actor，`Planet|Geology → Geology Asset` 指向这份资产。
3. 点 `重建行星（应用地质预设改动）` **重建行星** —— 体素是烘焙的，改预设不会自动重刷地表。
   （按钮在行星 Actor Details 的 `Planet|Geology` 分类里；这个名字是 C++ 里写死的中文
   DisplayName，英文面板也显示中文。）

想看矿囊长什么样：选中行星上的矿物组件 → Details 的 `Planet|MineralView` 分类里点
`Enter Mineral Preview In Editor`（这个按钮没有 DisplayName，标签由 C++ 函数名
`EnterMineralPreviewInEditor` 自动分词而来）。
退出点同分类的 `Exit Mineral Preview In Editor`，关卡不会变脏（编辑器预览只做临时隐藏）。

## 参数语义

| 参数 | 含义 |
|---|---|
| `Abundance` | **只影响颜色混合与图例占比**，不改几何 |
| `PodCount` / `PodRadius` | 撒多少矿囊、每个多大（占行星半径比例） |
| `PodRadiusJitter` | 每囊半径的随机抖动 |
| `PodDepthBias` | 0 = 全深度体积均匀；>0 向 `PodDepthCenter` 聚集 |
| `ShapeDetail` | 一个矿囊直径内几个噪声周期，越大越破碎 |
| `VeinThreshold` | 噪声稀疏阈值，越高矿囊越破碎 |
| `Seed` | 0 = 按元素 Id 派生，不同元素默认不相关 |

**想看不同元素互相穿插**：重叠对数量 ≈ `PodCount² × (2·r)³`。半径砍半，重叠少 8 倍——
所以别靠加 `PodCount` 硬堆，先把 `PodRadius` 调大。

## 数值约束

地表几何的 `GetSurfaceRadiusVoxels` 是不动点迭代，收敛因子约为

    GeometryAmplitude · √2 / (PodRadius · 行星半径的体素数)

出货的地球值约 1e-3，收敛干净。`GeometryAmplitude 64` + `PodRadius 0.001` 这类组合会
大于 1、迭代发散 —— 症状是「脚下的地面与看见的表面差几体素」，不是崩溃。调这两个
参数前按上式估一下。

## 已知缺口：同元素相邻簇的盒边截断

观测视图按矿囊的空间邻近关系分簇，每簇一套局部栅格。簇之间只保证 AABB 不相交，
**不保证相隔一格**：同一元素、两簇距离小于一格且半径差很大时，对方的矿囊会落进
本簇盒子的一格余量里，等值面在盒边被截断（看起来像面上有缺口）。默认数值下罕见，
但 `PodRadius` / `PodCount` 调大后簇更多、格边更宽，碰到它的概率会上升 ——
调参后看到面有缺口，先怀疑这里，而不是材质。
