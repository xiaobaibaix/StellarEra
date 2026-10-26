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
