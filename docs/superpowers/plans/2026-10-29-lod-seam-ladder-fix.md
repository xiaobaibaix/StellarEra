# 体素 LOD 接缝修复（方案 C）

**日期**：2026-10-29
**触发**：星图视角贴地看行星时，地表出现一条笔直的接缝，两侧「风格和对比完全不同」。
**诊断**：`superpowers:systematic-debugging` Phase 1（结论见下）。

## 1. 根因（实测）

`BP_VoxelPlanet3`：`voxel_size = 50`、半径 40000 体素、**`max_lod = 25`**（另两颗星是 6）。

叶块边长 = `32 · 2^LOD · VoxelSize`。星图相机身上的环是 LOD1@80 m / LOD3@300 m / LOD5@1 km，
行星调用器 `PlanetLODInvoker` 给整颗星 LOD 8：

| 距相机 | 生效 LOD | 叶块 |
|---|---|---|
| ≤ 80 m | 1 | 32 m |
| ≤ 300 m | 3 | 128 m |
| ≤ 1 km | 5 | 512 m |
| > 1 km | 8 | **4.1 km** |

1 公里处跨 3 级、8 倍。八叉树块是轴对齐立方体（`VoxelRenderOctree.cpp:601`
`ShouldSubdivideByDistance`），边界投到地表上必然是直线；粗侧 32³ 个采样点跨 4 公里，
法线、顶点色、地形起伏全被抹平，看起来就是「悬空的板子」。`MaxLOD` 本该兜住
（`Height > MaxLOD` 无条件细分），25 形同虚设。

## 2. 改动（方案 C）

| # | 改动 | 位置 |
|---|---|---|
| ① | `max_lod` 25 → 7（叶块 2.05 km，约 1200 块，现约 300） | UniverseMap 里 `BP_VoxelPlanet3` 实例 |
| ② | 星图相机补 LOD6 环：`LODToSet 6`、`LODRange 300000`（±3 km），只做 LOD | `StarMapCameraPawn.cpp` 构造函数 |
| ③ | 飞船补同一档环：`PredictionTime 0.2`（3.6e5 × 0.2 = 72000 ≤ 300000） | `SpaceshipPawn.cpp` 构造函数 |
| ④ | 视点门控把新环一起开关 | 两处 `ApplyViewpointInvokerState` |
| ⑤ | 调用器个数断言 3 → 4 | `VoxelViewpointInvokerTests.cpp` |

改完的阶梯（行星 3）：32 m → 128 m → 512 m → 1.02 km → 2.05 km，每档 ≤ 4 倍，
接缝从「3 级落差」降到「1 级」。

碰撞环一概不开：碰撞细分路径不读 `LODToSet`、锁死 Height 0，开大了顶爆显存。

## 3. 块数账

叶块 LOD6 = 32·2^6·50 = 102400 cm = 1.02 km。LOD6 盒 ±3 km 与球面相交的面积
≈ π(3²−1²) = 25 km²，除以叶块面积 ≈ 24 块；LOD7 全星约 1200 块。

## 4. 验证口径

- 自动化：`StellarEra.Planet.InvokerViewpoint.*`（个数 4 + 门控幂等）+ 全量 `StellarEra`。
- PIE（UniverseMap，编辑器置前台）：星图贴地 1 km 高度平视地平线，
  接缝应落在 LOD6 环边界上且落差肉眼基本不可辨；`stat unit` 与块生成时间不比改前差一个量级。

## 5. 不做什么

- 不改 `Plugins/VoxelFree`。
- 不改行星调用器的全星 LOD 8（`ComputeOrbitLOD` 的既有取舍），靠 `MaxLOD` 兜。
- 不补 LOD2/LOD4 等更细的档：角色身上已有（`PlanetCharacter.cpp` FarRings），
  飞行/星图视角用不到那么细。

## 6. 实施记录（2026-10-29）

| # | 状态 | 证据 |
|---|---|---|
| ① | 已改 | `BP_VoxelPlanet3` 的 `max_lod` 25 → 7，写回外部 Actor 包 `__ExternalActors__/Maps/UniverseMap/4/W8/Z5L3UJGKQYPNHRCJ37BF7A`（`git status` 单条 M）。重建后该星渲染块 416→**2236**，另两颗 `max_lod 6` 的仍是 416 |
| ②③④ | 已改 | 星图相机 / 飞船构造函数各加 `VoxelInvokerLOD6`（`LODToSet 6`、`LODRange 300000`），`ApplyViewpointInvokerState` 一并开关 |
| ⑤ | 已改 | 三处计数断言 3 → 4：`VoxelViewpointInvokerTests.cpp`（Ship / StarMap）、`StarMapCameraInvokerTests.cpp`；飞船侧 `SpaceshipLODInvokerTests.cpp` 另加 LOD6 逐项断言 |

**构建**：UBT `StellarEraEditor Win64 Development` → `Result: Succeeded`（7.61 s UBA）。

**自动化**：全量 `StellarEra` → 147 条，146 pass / 1 fail。
唯一失败是既有的 `StellarEra.Planet.Geology.EarthPresetValues`
（`iron 一个生成器` 期望 1 实际 2，`PlanetGeologyPresetTests.cpp:228`）——
地质相关文件在工作区无改动（`git status` 为空），与本次改动无关。

**视觉**：编辑器视口在星表面 1 km / 50 km 各截一张，球体完整、无孔洞、无悬空板。
星图视角的最终确认仍需 PIE 里人工看一眼 —— 星图相机的 `OrbitDistanceCm` 是只读属性，
`OrbitLook` / `AddZoom` 也不是 UFUNCTION，脚本没法把它摆到那个机位。

**待办**：`Content/Saves/PlanetVoxel/UniverseMap/*.uasset` 仍是旧 LOD 布局（工作区里本就
是 M 状态）。想让存档与新阶梯对齐、避免下次加载重新生成，可跑一次
`StellarEra.Planet.ResaveAll 0`。

## 7. 第二刀：biome cubemap，让颜色与网格密度解耦（2026-10-30）

第 1 节的阶梯只把落差从 3 级压到 1 级，接缝仍在：两侧凹凸不一样是几何问题，
而**颜色整片翻档**是采样密度问题 —— 配色是 `Offset = max(fBm, SeaLevel)` 的
硬阈值函数，顶点色只存在于网格采样点上（LOD5 16 m / LOD6 32 m / LOD7 64 m），
粗侧一个采样点代表的区域，细侧能分出水和沙。阶梯再密也只能减少，不能消除。

根治：颜色本来就只跟**方向**有关（`ComputePlanetSurfaceColor` 的输入就是单位方向），
烘到一张 cubemap 上，材质按行星局部方向查表。粗 LOD 于是只是几何糊，颜色不再换一套。

### 7.1 改动

| 文件 | 内容 |
|---|---|
| `Source/StellarEraTerrain/Public/PlanetBiomeCubemap.h`（新） | `FaceUVToDirection` / `DirectionToFaceUV` / `BakeFaceColors` / `BakeTexture` + D3D 面表 |
| `Source/StellarEraTerrain/Private/PlanetBiomeCubemap.cpp`（新） | 面表实现；`BakeTexture` 走 `UTextureCube::CreateTransient` → 直接写 `Mips[0].BulkData` → `UpdateResource()` |
| `Source/StellarEraTerrain/Private/PlanetBiomeCubemapTests.cpp`（新） | 4 条自动化测试 |
| `VoxelPlanetActor.h/.cpp` | `BiomeCubemapResolution`（256）/ `BiomeCubemapBlend`（1.0）/ `InstallBiomeMaterial()` / `PreSave`+`PostSaveRoot` 换回父材质 |
| `Content/Materials/M_PlanetVertexColor.uasset` | 21 → 27 个表达式，`BaseColor ← Lerp(VertexColor, BiomeCube.RGB, BiomeCubeBlend)` |

关键接线（每一条都对应一个会「静默出错」的失败面）：

- **方向**：材质里 `AbsoluteWorldPosition − ActorPositionWS`，两端都用绝对原点，
  相减留在 LWC 里保精度。天体每帧被 N 体子系统搬位置，`SetTransform` 会同时刷新
  proxy 的 `ActorPosition`（`PrimitiveSceneProxy.cpp:904`），所以不需要每帧往 MID
  推行星中心。体素网格组件的 outer 是本 Actor（`VoxelRendererMeshHandler.cpp:268`
  用 `RootComponent` 做 outer），`ActorWorldPosition` 因此就是行星中心。
- **编码**：顶点色走插件的 `FVoxelUtilities::FloatToUINT8`（**线性**量化，shader
  直接当线性值用）；烘焙存 `ToFColor(true)`（sRGB）配贴图 `SRGB=true` 解回线性，
  两端落在同一个色上。这一步弄反，整颗星会亮暗一遍。
- **生命周期**：运行期 `VoxelMaterial` 指着瞬态 MID，直接存会把该属性写成空引用
  （下次加载是 "Invalid VoxelMaterial" 灰球），因此 `PreSave` 换回
  `BaseVoxelMaterial`、`PostSaveRoot` 再换回来。
- **时机**：`InstallBiomeMaterial(true)` 排在哈希守卫之后、`CreateWorld`/`Toggle`
  之前 —— 渲染器在建世那一刻快照材质。`PostEditChangeProperty` 走
  `bForceBake=false` + `ApplyNewMaterials`，调分辨率/权重不销毁体素世界。
- **默认值**：材质里 `BiomeCubeBlend` 默认 0，没挂 MID 的行星完全不受影响。

### 7.2 验证

- 构建：UBT `StellarEraEditor Win64 Development` → Succeeded；`UnrealEditor-StellarEraTerrain.dll`
  与 `PlanetBiomeCubemap.cpp.obj` 均晚于源文件，UHT 生成的
  `VoxelPlanetActor.gen.cpp` 含 `BiomeCubemapResolution`。
- 自动化：`StellarEra.Planet.BiomeCubemap.*` 4 条全绿（面/UV 往返、邻面方向连续、
  逐像素等于配色函数、分辨率 0 交空数组）；全量 `StellarEra` → 153 pass / 1 fail，
  仍是既有的 `EarthPresetValues`。
- 材质图读回：27 个表达式，`BaseColor ← LinearInterpolate_0`，其 A/B/Alpha 分别是
  `VertexColor_0` / `TextureSampleParameterCube_0` / `ScalarParameter_0`；标量参数表
  多出 `BiomeCubeBlend`，贴图参数表多出 `BiomeCube`。`BP_VoxelPlanet` 的
  `voxel_material` 正是 `M_PlanetVertexColor`、`lod_materials` 为空（不会被逐 LOD
  材质盖掉），`biome_cubemap_resolution=256`、`biome_cubemap_blend=1.0`。
- **未验证**：材质 shader 有没有编译通过。`-run=pythonscript` 的 commandlet 不跑
  材质编译（`get_statistics` 恒 0），要确认得开编辑器看材质编辑器报不报错；
  画面效果同样要 PIE 里看一眼。

## 8. 事故：PIE 里 VoxelRenderUtilities.cpp:518 的 ensure（2026-10-31）

### 8.1 现象

进 PIE 后每个 chunk 建材质都会触发：

```
Ensure condition failed: MaterialInstanceObject
[File: .../Plugins/VoxelFree/Source/Voxel/Private/VoxelRender/VoxelRenderUtilities.cpp] [Line: 518]
```

伴随三条 `LogMaterial` 警告（每条建议一次 MID 创建）：

```
MaterialInstanceDynamic ...BP_VoxelPlanet_C_....BiomeMaterialMID is not a valid parent
for MaterialInstanceDynamic ...BiomeMaterialMID_0. Only Materials and
MaterialInstanceConstants are valid parents for a material instance.
```

两条日志按时间对齐：ensure 只出现在含 "biome cubemap ... 烘焙完成" 行的那两次运行里。

### 8.2 根因（逐步，每步都有直接证据）

1. PIE 复制关卡。`VoxelMaterial` **不是** Transient，被拷成上一轮
   `BiomeMaterialMID` 的副本（名字还是 `BiomeMaterialMID`）；`BiomeMaterial` 与
   `BaseVoxelMaterial` 都标了 `UPROPERTY(Transient)`，复制后清空。
2. `InstallBiomeMaterial` 于是执行 `BaseVoxelMaterial = VoxelMaterial`，拿到的是 **MID**。
3. `UMaterialInstanceDynamic::Create(MID, ...)` → `UMaterialInstance::SetParentInternal`
   **只接受 `UMaterial` / `UMaterialInstanceConstant`**，静默拒绝，
   新 MID（`BiomeMaterialMID_0`）的 `Parent` 留在 `nullptr`。**Create 不返回 null。**
4. VoxelFree 的建材质 lambda 走 `ParentInstance->Parent.Get()` → null →
   `FVoxelMaterialInterfaceManager::CreateMaterialInstance` 回退 `DefaultMaterial()`
   （一个普通 `UMaterial`）→ `Cast<UMaterialInstanceDynamic>` 失败 → `ensure`。
   ensure 按调用点去重，一条日志代表所有 chunk。

### 8.3 修法

- `AVoxelPlanetActor::StripMaterialInstanceDynamic`（public static，纯函数）：
  沿 `Parent` 只剥 **MID**，剥到 `Material` / `MaterialInstanceConstant` 就停
  （MIC 是合法父材质，再往上剥会丢用户在 MIC 里设的参数）；中途遇到父为空的 MID
  返回 nullptr；最多走 8 层防脏数据成环。
- 捕获处改成 `BaseVoxelMaterial = Strip(VoxelMaterial)`；剥空则退回
  `GetClass()->GetDefaultObject<AVoxelPlanetActor>()->VoxelMaterial`（蓝图里用户
  原本设的那个）。
- `UMaterialInstanceDynamic::Create` 之后**再查一次 `Parent`**：为空的 MID 直接
  丢弃（`BiomeMaterial = nullptr` + 警告 + 返回 false），绝不递给渲染器 —— 宁可
  这颗星继续用顶点色，也不能让 ensure 把所有 chunk 打成默认灰材质。

### 8.4 验证

- 新增 `StellarEra.Planet.BiomeMaterial.UnwrapDynamicInstance`：nullptr→nullptr、
  Material/MIC 原样返回、MID 剥到父材质、父为空的 MID→nullptr。
- `UnrealEditor-Cmd ... Automation RunTests StellarEra.Planet.BiomeMaterial+StellarEra.Planet.BiomeCubemap`
  → **5 tests performed，5 Success**。
- UBT `StellarEraEditor Win64 Development` → `Result: Succeeded`（38.32 s）。
- **未验证**：进 PIE 复跑一次确认 ensure 不再出现。上面第 1 步的「PIE 复制把
  `VoxelMaterial` 拷成 MID 副本」是从 `LogMaterial` 警告里的对象名直接读出来的，
  无头 `-nullrhi` 复现不到 chunk 材质这条路径。
