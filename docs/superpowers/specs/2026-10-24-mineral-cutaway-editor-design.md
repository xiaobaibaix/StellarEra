# 矿物剖面编辑器（星图 H 键）设计

- 日期：2026-10-24
- 分支：`feat/mineral-cutaway`
- 前置：`docs/superpowers/specs/2026-10-24-planet-geology-design.md`

## 1. 目标

在星图模式里按 **H** 进入「矿物剖面」模式：把**选中的那颗体素行星**沿任意过球心的平面切成两半，
靠近相机的一半不可见，露出一个显示矿物分布的剖切面；提供一套「像旋转 3D 模型一样」的球心旋转环
控制切面朝向，并显示矿物图例。

只有观察，不改行星：体素数据、碰撞、存档、N 体状态一概不动。退出 H 后立刻恢复原样。

### 非目标

- 不对任何非体素天体（飞船、恒星）开剖面。
- 不提供编辑矿脉的入口（那是地质预设 JSON 的事）。
- 不保存剖面的朝向/开关状态（每次进入都是新剖面）。
- 不做体积渲染、不做行星材质的双面化、不剖大气壳。
- 剖面里不区分水/陆（海面是实心的，见 §7.4）。

## 2. 进入与退出

| 动作 | 结果 |
|---|---|
| 星图里按 H，且选中天体是 `AVoxelPlanetActor` 且地质场非空 | 进入剖面模式 |
| 星图里按 H，没有选中 / 选中的不是体素行星 / 地质场为空 | 不进入，屏幕提示 2 秒（理由见 §8） |
| 非星图里按 H | 忽略（不提示） |
| 剖面模式里再按 H | 退出，恢复 |
| 剖面模式里按 M（退出星图） | 先退出剖面，再退出星图 |
| 剖面模式里选中天体被清空 / Pawn 改变 / 关卡结束 | 退出剖面 |

进入时的相机动作（顺序固定）：

1. `StarMapPawn->FocusOn(Planet, 3 × 行星表面半径)` —— 复用现有的距离夹取与平滑拉远。
2. `StarMapPawn->OrientToDirection(PlaneNormal)` —— 新增的公开方法，把相机瞬时摆到正对切面
   （`OrbitRotation` 的语义是「相机 → 焦点」，所以这里传与 `InitialPlaneNormal` 同向的单位向量，
   见 §4.4）。
3. 初始切面：`PlaneNormal = normalize(Center - CameraPos)`（相机在进入时的位置），
   因此进入后看到的是一张**正对镜头的完整圆剖面**。

剖面模式里 LMB 不再选中天体：鼠标左键只用于拖圆环；要换一颗行星，按 H 退出、选中、再按 H。
右键拖动仍然旋转星图视角，滚轮仍然缩放 —— 切开后可以绕着看。

## 3. 视觉规格

### 3.1 剖面圆盘

- 位置：过行星球心、法线为当前 `PlaneNormal` 的平面，沿 `ClipNormal` 向相机侧偏 **2 cm**
  （`ClipNormal` 见 §5.1，避免与体素网格共面闪烁）。
- 网格：`/Engine/BasicShapes/Plane`，单面 quad，边长 `2 × SectionRadiusCm`；
  `SectionRadiusCm = 1.05 × 行星基准表面半径（cm）`（`GetSurfaceRadiusCm()`）；
  `BaseRadiusVoxels = GetSurfaceRadiusCm() / VoxelSize`。
- 材质：`/Game/Materials/M_PlanetSection`，**Unlit + Masked + Two Sided**，纹理参数 `SectionTexture`。
  - 这里的 Two Sided 只影响这一张 quad，不改变 §4.1「行星材质保持单面」的决定。
- 纹理：512×512（`FinalResolution`）与 96×96（`PreviewResolution`），`PF_B8G8R8A8`，sRGB，
  `AddressX/Y = Clamp`，无 mip，采样过滤 Bilinear。
  - 圆盘外（`PlaneRadiusCm > SectionRadiusCm`）alpha = 0。
  - 行星实体外（`PlaneRadiusCm > SurfaceRadiusCm(Dir)`）alpha = 0。
  - 边界外 1.5 像素做线性羽化，避免锯齿。

颜色规则（逐像素）：

```
Dir            = normalize(AxisU × u + AxisV × v)          // u,v ∈ [-1,1]
PlaneRadiusCm  = |AxisU × u + AxisV × v| × SectionRadiusCm
SurfaceRadiusCm= Planet->GetSurfaceRadiusCmAtDirection(Dir)  // 含地形与矿脉外凸
SurfaceVoxels  = SurfaceRadiusCm / VoxelSize
PlaneVoxels    = PlaneRadiusCm   / VoxelSize
DepthVoxels    = SurfaceVoxels - PlaneVoxels                 // 向内为正，与 Sample() 同义
Sample         = Geology->Sample(Dir, DepthVoxels)

Presence  = saturate((Sample.DominantNorm - Appearance.ColorThreshold) / max(Appearance.FadeRange, 1e-4))
PurityMix = saturate(Sample.Purity / max(Appearance.PurityForFullColor, 1e-4))
DepthShade= 1 - 0.4 × saturate(DepthVoxels / BaseRadiusVoxels)

Base  = Planet->SurfaceColor × 0.25 × DepthShade             // 暗底色，让矿物色跳出来
Color = lerp(Base, Sample.MineralColor, Presence × PurityMix)
```

`Appearance` 取自 `Geology->GetAppearance()`，与地表着色用的是同一组阈值 —— 剖面里看到的
「有色区」与站在地表上看到的有色区是同一个判据。

### 3.2 球心旋转环

- 三个圆环，圆心 = 行星球心，半径 `1.15 × SectionRadiusCm`，128 段：
  - X 轴红 `(1, 0.15, 0.15)` —— 所在平面 YZ，拖动绕 **世界 X** 转；
  - Y 轴绿 `(0.2, 1, 0.2)` —— 所在平面 XZ，拖动绕 **世界 Y** 转；
  - Z 轴蓝 `(0.2, 0.45, 1)` —— 所在平面 XY，拖动绕 **世界 Z** 转。
- 用 `ULineBatchComponent::DrawLines` 画 128 段折线（`DrawCircle` 没有线宽参数），
  `FBatchedLine(..., LifeTime = 0, Thickness = 3.f, SDPG_Foreground)`，每帧 `Flush()` 后重画
  （只在该组件 Tick 时；组件不激活就不 Tick）。
- 不做悬停高亮（保持简单）：命中判定是屏幕空间到圆环折线的最近距离 ≤ 12 px，
  三个环都命中时取最近的那个。

### 3.3 图例面板

`UStarMapUISystem` 在星图 UI 之上再画一个左上角面板（`Margin = 8`），与右侧天体信息面板并存：

```
矿物剖面 — <行星名>
────────────────────────
[色块] <元素显示名>        <丰度 × 100>%
[色块] ...                 ...
────────────────────────
H 退出   左键拖圆环转切面   右键拖转视角   滚轮缩放
```

- 色块 14×14 px，用元素的 `Color`；每行一个元素，顺序与元素表一致。
- 地质场没有元素时面板只画标题与提示行（正常进入流程里不会发生）。
- 行文本格式化是纯函数（§9），面板复用 `AStellarEraHUD::DrawText / DrawRect / GetTextSize`
  与现有的 `FStarMapPanelRect` 摆放方式。

## 4. 架构

### 4.1 切面裁剪：全局 MPC + 行星材质单面

行星材质 `M_PlanetVertexColor` 已经是 `BLEND_MASKED`，`OpacityMask` 接
`VoxelOpacity.Result`。在它前面插一个乘子即可：

```
Mask = 1 - (Half × InsideSphere × Active)

Half         = saturate(dot(WorldPos - Origin.xyz, Normal.xyz) × 10.0)   // 0.1 cm 过渡
InsideSphere = saturate((Normal.w - |WorldPos - Origin.xyz|) × 0.02)    // 50 cm 过渡
OpacityMask  = VoxelOpacity.Result × Mask
```

- `Origin`/`Normal` 来自 `MPC_PlanetCutaway`（§6），每帧由剖面组件写入世界坐标。
- `Normal.w` = 行星基准表面半径 × 1.05（cm）：**包住这颗行星**，所以同一份全局 MPC
  不会裁到别的星球。
- `Active ∈ {0, 1}` 放在 `Origin.w`，退出时写 0，实现「立刻恢复」。
- **行星材质保持单面**（决策：单面）：被裁掉的那一半连同远侧半球一起被背面剔除，
  画面里只剩剖面圆盘。代价是看不到「壳内壁」，收益是零额外渲染成本。
  因此进入剖面时必须隐藏大气壳（否则会看到一层完整的球壳套在剖面外）。

MPC 的写法用 `UKismetMaterialLibrary::SetVectorParameterValue`（每世界实例），
组件激活期间每帧写一次。

### 4.2 组件划分

```
AVoxelPlanetActor
└── UPlanetCutawayComponent (USceneComponent, 默认不 Tick)
    ├── SectionMesh   (UStaticMeshComponent) —— 剖面圆盘，动态材质
    └── SectionRings  (ULineBatchComponent) —— 三个旋转环
```

`UPlanetCutawayComponent` 的职责（一个组件一件事）：
**持有切面状态、每帧同步 MPC/圆盘/圆环、进出时开关大气壳、按需生成剖面纹理。**

它不处理输入、不画 UI、不碰相机 —— 这三件事分别在 §4.4 / §4.5 / §4.6。

组件的公开接口（其余全部私有）：

```cpp
bool Activate(AVoxelPlanetActor* InPlanet, const FVector& InitialPlaneNormal); // 失败返回 false
void Deactivate();
bool IsActive() const;

void RotatePlane(ESectionRing Axis, float DeltaDegrees);
void RequestSectionRender(int32 Resolution);

/** 三个圆环的世界空间采样点（每环 128 个），给控制器投影后做拾取。 */
void GetRingWorldPoints(TArray<FSectionRingWorldPolyline>& OutRings) const;

AVoxelPlanetActor* GetCutawayPlanet() const;
TSharedPtr<const FPlanetGeologyField> GetGeologyField() const;

UPROPERTY(EditAnywhere) int32 FinalResolution = 512;
UPROPERTY(EditAnywhere) int32 PreviewResolution = 96;
```

### 4.3 切面图像

模块 `StellarEraPlanet` 里分两层：

- `FPlanetSectionImage`（纯 C++，无 UObject）：给定尺寸、`AxisU/AxisV/PlaneNormal`、
  `SectionRadiusCm`、采样回调，输出 `TArray<FColor>`。采样回调是
  `TFunction<float(const FVector& UnitDir)>`（表面半径）与
  `TFunction<FPlanetGeologySample(const FVector& UnitDir, double Depth)>`（矿物），
  因此测试里可以用假球体，不需要真的生成器。
- `UPlanetCutawayComponent::RequestSectionRender(int32 Resolution)`：同步调用上面那层，
  把像素写进 `Texture->UpdateTextureRegions`。

像素缓冲是组件的常驻成员（`TArray<FColor> UploadPixels`），在下次写之前不释放 ——
这是 `UpdateTextureRegions` 的异步渲染命令能安全读取它的前提。

生成时机：

| 时机 | 分辨率 |
|---|---|
| 进入 | Final（512） |
| 圆环拖动中，自上次生成后又转了 ≥ 0.75°，且距上次 ≥ 33 ms | Preview（96） |
| 拖动结束（期间转过） | Final（512） |
| 切面朝向未变（只是相机在动） | 不重算 |

### 4.4 相机

`AStarMapCameraPawn` 新增公开方法：

```cpp
/** 立即把轨道朝向设为 LookDirection（pitch 夹到 ±89°，roll = 0）。距离与焦点不变。
 *  LookDirection 是「相机 → 焦点」方向（与 OrbitRotation.Vector() 同义），
 *  因此要让相机正对行星球心，传 normalize(PlanetLocation - CameraLocation)。 */
void OrientToDirection(const FVector& LookDirection);
```

`OrbitRotation` 是 protected，所以必须由 Pawn 自己暴露这个方法。
调用顺序是 `FocusOn` 之后（`FocusOn` 会覆盖 `OrbitRotation`）。

判据（来自 `CalcCamera`）：`CameraLocation = Selected->GetActorLocation() - OrbitRotation.Vector() * OrbitDistanceCm`，
所以 `OrbitRotation.Vector()` 指向焦点。传反了相机会背对行星。

### 4.5 输入（`AStellarEraPlayerController`）

新增动作：

- `IA_CutawayToggle`（Boolean）← `EKeys::H`，`OnCutawayToggle()`。
- `IA_CutawayDrag`（Axis2D）← `EKeys::Mouse2D`，`OnCutawayDrag()`，`bConsumeInput = false`。

为什么拖圆环要单独一个鼠标位移动作：现有 `OnLook` 要求 `IA_LookActivate`（右键）按住才处理，
而拖圆环用的是左键。`IA_CutawayDrag` 与 `IA_Look` 绑同一个键，二者互不吞输入。

状态机（全在 PlayerController 里，组件只提供数据与几何）：

```
bCutawayDragging（私有）
OnStarMapSelectStarted:
    剖面没开 → 走原来的拾取逻辑
    剖面开着 → 命中圆环则记录 RingAxis、bCutawayDragging = true；否则什么都不做
OnCutawayDrag:
    没在拖 → return
    角度 = (MouseDelta.X - MouseDelta.Y) × 0.5°        // 固定符号约定，可测
    Component->RotatePlane(RingAxis, 角度)
OnStarMapSelectCompleted:
    bCutawayDragging = false；若期间转过 → Component->RequestSectionRender(Component->FinalResolution)
```

进入流程里控制器的两步：

```cpp
const FVector CameraPos = GetPlayerCameraManager()->GetCameraLocation();
const FVector InitialPlaneNormal = (Planet->GetActorLocation() - CameraPos).GetSafeNormal();
if (!Cutaway->Activate(Planet, InitialPlaneNormal)) { /* §8 的提示 */ }
StarMapPawn->FocusOn(Planet, 3.0 * Planet->GetSurfaceRadiusCm());
StarMapPawn->OrientToDirection(InitialPlaneNormal);   // 「相机 → 焦点」，不是反向量
```

圆环命中判定在 `PlanetCutawayMath`（§9）里是纯函数：组件给出三个环的采样点，
控制器用 `PlayerController::ProjectWorldLocationToScreen` 投影后调纯函数。

### 4.6 UI

`UStarMapUISystem::Draw` 末尾：若 `PlayerController->GetActiveCutaway()` 非空，
调 `DrawCutawayLegend(...)` 画 §3.3 面板。面板文本由纯函数
`FormatCutawayLegendLines(PlanetName, Elements)` 生成（首行标题，其余为内容行）。

## 5. 数学定义（必须与实现逐字一致）

### 5.1 相机侧裁剪

```cpp
FVector FPlanetCutawayMath::ComputeClipNormal(const FVector& PlaneNormal,
                                              const FVector& CameraPos,
                                              const FVector& CenterCm);
// CameraPos 与 CenterCm 重合时返回 PlaneNormal
// 返回 PlaneNormal 或 -PlaneNormal：使 dot(CameraPos - CenterCm, 结果) ≥ 0
```

近侧被裁掉的判据就是 `dot(P - Center, ClipNormal) > 0`，所以相机永远在「被裁掉的那一侧」，
相机绕过切面时可视一半自动翻转（这是刻意的）。

### 5.2 平面基

```cpp
void FPlanetCutawayMath::ComputePlaneBasis(const FVector& PlaneNormal,
                                           FVector& OutAxisU, FVector& OutAxisV);
// RefUp = |N.Z| < 0.99 ? UpVector : RightVector
// AxisU = normalize(cross(RefUp, N));  AxisV = cross(N, AxisU)   // (U, V, N) 右手系
```

### 5.3 旋转

```cpp
FQuat FPlanetCutawayMath::RotatePlane(const FQuat& PlaneOrientation,
                                      const FVector& Axis,
                                      float DeltaDegrees);
// FQuat(Axis, radians(DeltaDegrees)) * PlaneOrientation
// PlaneNormal = PlaneOrientation.GetAxisZ()
```

初始 `PlaneOrientation = FRotationMatrix::MakeFromZ(InitialPlaneNormal).ToQuat()`。

### 5.4 圆环拾取

```cpp
enum class ESectionRing : uint8 { X, Y, Z };

/** 组件侧：世界空间折线。控制器投影后用下面的屏幕版做拾取。 */
struct FSectionRingWorldPolyline
{
    ESectionRing Ring;
    TArray<FVector> Points;     // 每环 128 个，首尾不重复
};

/** 控制器侧：投影后的屏幕折线。 */
struct FSectionRingScreenPolyline
{
    ESectionRing Ring;          // X / Y / Z
    TArray<FVector2D> Points;   // 屏幕像素，首尾不重复
};

bool FPlanetCutawayMath::PickSectionRing(
    TArrayView<const FSectionRingScreenPolyline> Rings,
    const FVector2D& MousePx,
    float TolerancePx,
    ESectionRing& OutRing);
// 到每条折线的最近像素距离的最小值；都超过 TolerancePx 返回 false；
// 两个环的距离差 < 1e-3 px 时按 X → Y → Z 的固定顺序取第一个
```

### 5.5 遮罩的 CPU 镜像

```cpp
float FPlanetCutawayMath::ComputeOpacityMask(const FVector& WorldPos,
                                             const FVector& Origin,
                                             const FVector& Normal,
                                             float InsideRadiusCm,
                                             bool bActive);
// 与 §4.1 的材质公式逐项相同；0 = 完全裁剪，1 = 完全不裁
```

它存在的意义：材质节点没法写单元测试，这个函数是公式的可执行规格。改材质时必须同步改它
（反之亦然），测试会抓住分离。

## 6. 资产（一次性在编辑器里创建）

| 资产 | 类型 | 内容 |
|---|---|---|
| `/Game/Materials/MPC_PlanetCutaway` | `UMaterialParameterCollection` | `CutawayOrigin` (Vector)、`CutawayNormal` (Vector) |
| `/Game/Materials/M_PlanetSection` | `UMaterial` | Unlit、Masked、Two Sided、`SectionTexture` (Texture2D 参数)，TextureSample 的 RGB → Emissive、A → OpacityMask |
| `/Game/Materials/M_PlanetVertexColor` | 修改 | 见 §4.1：两个 `CollectionParameter` + 算术节点，乘进现有 OpacityMask 链；BaseColor 链不动 |

这些资产是二进制 `.uasset`，必须用编辑器 MCP（`MaterialTools`）创建/修改，
并 `Recompile` 后核对 `GetPropertyInput(OpacityMask)` 的接线。

## 7. 边界与已知取舍

1. **单面带来的空洞感**：切掉一半后看不到壳内壁，只有一张剖面圆盘。这是已批准的取舍
   （零额外渲染成本）。缓解：进入时相机正对切面、隐藏大气壳、圆盘略向相机偏移。
2. **行星旋转假设**：N 体系统只写 `SetActorLocation`，行星旋转恒为单位 —— 世界方向可直接当
   体素空间方向用。若将来引入自转，剖面朝向与采样方向都要先转到行星局部空间。
3. **行星移动**：MPC 的 `Origin` 每帧写世界坐标，圆盘/圆环是行星的子组件，两者同帧更新。
4. **水**：海面在密度场里是实心平面，剖面里看起来就是实心；不在本次范围内区分。
5. **多行星同屏**：`InsideSphere` 用当前行星半径包住它自己，其它行星不受 `Active` 影响。
6. **有山峰超出 `SectionRadiusCm`**：圆盘会在地形最外缘之外截止 —— 取 1.05 倍基准半径，
   对当前 `Amplitude = 8`（半径 1000 体素）绰绰有余。

## 8. 错误处理

| 情况 | 行为 |
|---|---|
| 选中天体不是体素行星 | 屏幕提示「矿物剖面只支持行星」，不进 |
| `GetGeology()` 为空或 `IsEmpty()` | 屏幕提示「该行星没有矿物分布」，不进 |
| 行星生成器未建（`PlanetGenerator == nullptr`） | 同「没有矿物分布」 |
| 材质资产/MPC 找不到 | `UE_LOG(LogTemp, Error)` 一次，不进剖面（宁可不工作，也不留一颗半透明的行星） |
| `CreateTransient` 失败 | 日志 + 不进剖面 |
| 退出的任何路径（H / M / Pawn 变更 / EndPlay） | 同一份 `Deactivate()`：MPC `Active = 0`、圆盘与圆环 `SetVisibility(false)`、大气壳恢复进入前的可见性、Tick 关掉 |

## 9. 测试（`StellarEra.Planet.Cutaway.*`）

| 测试 | 断言 |
|---|---|
| `Math.ClipNormal` | 相机在 `+N` 侧返回 `+N`；在 `-N` 侧返回 `-N`；相机与球心重合返回原法线；返回值单位长度 |
| `Math.PlaneBasis` | 两轴单位正交且与法线正交；法线取 ±Z 时基仍有效、轴不退化；(U,V,N) 为右手系 |
| `Math.RotatePlane` | 绕轴转 360° 回到原朝向（误差 < 1e-3°）；结果仍是单位四元数；转 X 轴后法线与 X 轴的点积不变 |
| `Math.PickRing` | 鼠标落在某环折线上 → 命中该环；偏离 13 px → 不命中；两环距离差 < 1e-3 px → 取 X |
| `Math.OpacityMask` | 相机侧点 = 0；远侧点 = 1；`bActive = false` 恒 1；球半径外的点恒 1 |
| `SectionImage.SolidSphere` | 假采样器：表面半径恒 10000 cm、`VoxelSize` 100、矿物色与纯度均满 → 行星内、且离边界超过羽化宽度的像素 alpha = 255 且颜色 = 纯矿物色；`PlaneRadiusCm > SurfaceRadiusCm + FeatherCm` 的像素 alpha = 0。**边界一像素带宽内有部分 alpha，不要断言它** |
| `SectionImage.DepthMapping` | 同上假采样器记录 `Depth`：分辨率取 **101**（奇数）使中心像素 `(50,50)` 的 `u = v = 0`，Depth = 100 体素；像素 `(60,50)` 的 `u ≈ 0.198`、`PlaneRadiusCm ≈ 1980`，Depth ≈ 80 体素 |
| `SectionImage.PresenceThreshold` | `DominantNorm` 低于 `ColorThreshold` 时颜色等于 `Base`；`DominantNorm ≥ ColorThreshold + FadeRange` 时等于矿物色（取离边界足够远的像素，避开羽化） |
| `Legend.Format` | 首行含行星名与「剖面」；元素行数 = 元素数；丰度格式为整数百分比 |

构建：`StellarEraEditor Win64 Development`（捆绑 dotnet 直调 UBT，编辑器必须关闭）。
测试：`Automation RunTests StellarEra`（`-nullrhi`）。**测试是 editor world，不跑 BeginPlay**，
所以组件激活/退出链路只能人工 PIE 验证。

## 10. 人工验收（PIE，`UniverseMap`）

1. M 进星图 → 选中行星 → H：看到正对镜头的圆剖面，圆盘上有颜色不一的矿脉斑块。
2. 图例面板列出该行星的全部元素，色块颜色与剖面里的斑块一致。
3. 拖红色环：切面绕 X 轴转，圆盘内容随之变化；松手后画面变清晰（96 → 512）。
4. 右键拖转视角：绕到侧面仍然只在相机侧缺一半；相机穿过切面时缺口换边。
5. 滚轮缩放：剖面与圆环一起保持贴住行星。
6. 再按 H：行星完整恢复，大气壳回来，圆环消失。
7. 剖面模式下按 M：先恢复行星，再退出星图。
8. 选中飞船按 H：提示「只支持行星」，不进入。
9. 前台运行，剖面模式下 `stat unit` 的 Game/Draw 与进入前同级（±1 ms）。

## 11. 文件清单

**新增（`StellarEraPlanet`）**

| 文件 | 职责 |
|---|---|
| `Public/PlanetCutawayMath.h` / `Private/PlanetCutawayMath.cpp` | §5 的纯函数 |
| `Public/PlanetSectionImage.h` / `Private/PlanetSectionImage.cpp` | 纯 C++ 剖面栅格化（采样回调注入） |
| `Public/PlanetCutawayComponent.h` / `Private/PlanetCutawayComponent.cpp` | §4.2 的组件 |
| `Private/PlanetCutawayTests.cpp` | §9 的自动化测试 |

**修改**

| 文件 | 改动 |
|---|---|
| `StellarEraPlanet/Public/PlanetGeologyField.h` | 新增 `TArray<FPlanetGeologyElement> GetElementDefinitions() const`（返回副本，调用频率是人机交互级） |
| `StellarEraPlanet/Public/VoxelPlanetActor.h` / `.cpp` | 新增 `UPlanetCutawayComponent` 默认子对象 + `GetCutawayComponent()`；新增 `GetGeologyField()`（转发 `PlanetGenerator->GetGeology()`） |
| `StellarEra/Public/StarMapCameraPawn.h` / `.cpp` | `OrientToDirection` |
| `StellarEra/Public/StellarEraPlayerController.h` / `.cpp` | `IA_CutawayToggle` / `IA_CutawayDrag`、H 映射、`OnCutawayToggle`、`OnCutawayDrag`、圆环拖拽状态机、`GetActiveCutaway()` |
| `StellarEra/Public/StarMapUISystem.h` / `.cpp` | 图例面板 + `FormatCutawayLegendLines` |

**资产**：§6 的三项。

**`Build.cs`**：预计无需改动 —— `UMaterialParameterCollection` / `UKismetMaterialLibrary` /
`LineBatchComponent` / `UTexture2D` 都在 Engine 里，`StellarEra` 已经依赖
`StellarEraPlanet`。若链接器要求 `RenderCore`，只加在 `StellarEraPlanet`。

## 12. 决策记录

| 决策 | 取值 | 理由 |
|---|---|---|
| 切片方案 | A：平面纹理 + MPC 全局裁剪 | 不引入额外的渲染 pass / 后处理材质，成本可忽略 |
| 行星材质 | 单面（不勾 Two Sided） | 零额外渲染成本；已接受「看不到壳内壁」 |
| 剖面内容 | 逐像素解析采样（表面半径 + 地质场），不是截图 | 任意朝向都能直接出图，不依赖当前相机渲染 |
| 交互 | 球心三轴旋转环 + LMB 拖拽 | 与 UE 旋转 gizmo 同一套心智模型 |
| 状态 | 只观察，不落盘 | 不碰体素数据/碰撞/存档，退出即恢复 |
| 剖面刷新 | 拖动中 96 预览 + 松手 512 终稿 | 拖动要跟手，终稿允许一次性卡顿 |
