# 矿物剖面编辑器（星图 H 键）实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在星图模式按 H 把选中的体素行星沿过球心的任意平面「切开」：近相机一侧不可见，露出显示矿物分布的剖面圆盘，并用三根世界轴旋转环控制切面朝向；观察完按 H 或离开星图即完全恢复。

**Architecture:** 三层。① `StellarEraPlanet` 内新增纯数学（`FPlanetCutawayMath`）与纯 CPU 光栅化（`FPlanetSectionImage`），都不触引擎资产，可无头测试；② `UPlanetCutawayComponent` 挂在行星上，持有切面四元数/纹理/圆盘/圆环，通过 `UKismetMaterialLibrary` 把 `CutawayOrigin`（圆心 + Active）与 `CutawayNormal`（ClipNormal + 内球半径）写进两个单参数 MPC，行星材质据此把近侧做 OpacityMask 裁掉；③ `StellarEra` 侧 `AStellarEraPlayerController` 管状态机与输入（H 进出、左键拖环、右键转视角），`AStarMapCameraPawn::OrientToDirection` 把相机摆到切面正前方，`UStarMapUISystem` 画左上角图例。

**Tech Stack:** UE 5.8.3 / Win64，C++（四模块分层不变），动态材质实例 + 材质参数集合，`UTexture2D::CreateTransient` + `UpdateTextureRegions` 运行时纹理，`ULineBatchComponent` 世界空间线框，Enhanced Input 运行时构建，Python 资产脚本经 `UnrealEditor-Cmd -run=pythonscript` 执行。

**Spec:** `docs/superpowers/specs/2026-10-24-mineral-cutaway-editor-design.md`（唯一权威；本计划的裁定只在它沉默或自相矛盾处生效）

## Global Constraints

- 不修改 `Plugins/VoxelFree/`、`Plugins/VibeUE/`（第三方，勿改）。自定义代码只进 `Source/{StellarEra,StellarEraCore,StellarEraPlanet,StellarEraVehicle}` 并改对应 `Build.cs`。
- 依赖方向不变：`StellarEra → StellarEraVehicle → StellarEraPlanet → (Voxel)`；`StellarEraVehicle` **不能**依赖 `StellarEra`。本功能不新增任何跨模块反向依赖。
- 只观察：不改体素数据、不碰碰撞、不写存档、不推进 N 体。剖面关闭后视觉必须与进入前完全一致。
- 含中文的 `.h` / `.cpp` 必须 UTF-8 **with BOM**。新建文件写完立即加 BOM。
- 不要统一换行符：仓库是 CRLF/LF 混合，`autocrlf=true` 下 CRLF 是稳定态。
- 构建前**必须关闭编辑器**（Live Coding 锁 DLL）。只用捆绑 dotnet 直调 UBT，cwd 在项目外：

```bash
cd /d/ && "D:/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" \
  "D:/Application/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" \
  StellarEraEditor Win64 Development \
  -Project="F:/workspace/ue/StellarEra/StellarEra.uproject" -WaitMutex -FromMsBuild
```

- 自动化测试命令（编辑器开着也能跑）：

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra" -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout \
  -abslog="F:/workspace/ue/StellarEra/Saved/Logs/CutawayTests.log"
```

- 提交必须挂 pathspec（索引里有常驻暂存文件，裸 `git commit` 会吞掉全部）。新文件先 `git add`，再 `git commit -- <paths>`。
- 每个提交消息末尾空一行接 `Co-Authored-By: Claude Code <noreply@anthropic.com>`。
- PIE 视觉验收是**人的任务**，不要用日志冒充；本计划只在 Task 7 里列出清单。

## 与 spec 的实现裁定（执行时以本表为准）

spec 在这些点上沉默、自相矛盾或需要引擎事实补足。**执行时按本表，不要按记忆改回去。**

1. **两个单参数 MPC，不能合并成一个。** `UMaterialExpressionCollectionParameter::PostLoad`（`MaterialExpressions.cpp:17151`）用 `ParameterName = Collection->GetParameterName(ParameterId)`，即**由存储的 GUID 反查名字**；而 `UMaterialParameterCollection::SanitizeParameters`（`ParameterCollection.cpp:405`）只保证**第一个**零 GUID 参数保持零，后续零 GUID 参数会被随机化。Python 无法写 GUID。→ `MPC_CutawayOrigin`（参数 `CutawayOrigin`：xyz = 球心，w = Active）与 `MPC_CutawayNormal`（参数 `CutawayNormal`：xyz = ClipNormal，w = InsideRadiusCm）各含**恰好一个** Vector 参数。
2. **剖面圆盘的 UV 由世界位置算，不用网格 UV0。** 镜像方向依赖 `/Engine/BasicShapes/Plane` 的 UV 布局，不可靠。`M_PlanetSection` 用 MID 参数 `SectionAxisU` / `SectionAxisV`（Vector）与 `SectionHalfExtentCm`（Scalar）自行算 UV：`Rel = WorldPos − ObjectPos`；`u = dot(Rel, AxisU)/R`；`v = dot(Rel, AxisV)/R`；`UV = (u*0.5+0.5, v*0.5+0.5)`。**基与朝向都以未翻转的 `PlaneNormal = PlaneOrientation.GetAxisZ()` 为准**：`AxisV = Cross(PlaneNormal, AxisU)`（即 `ComputePlaneBasis` 的输出），圆盘旋转 `MakeFromXZ(AxisU, PlaneNormal)` 使网格局部 X/Y 恰为 `AxisU/AxisV`、正面朝 `+PlaneNormal`；只有 2 cm 的偏移用 `ClipNormal`（见 §3.1）。相机绕到切面另一侧时看到的是同一张平面的背面，图案**左右镜像**——这是一张平面画从背面看的正确外观，`AxisV` 不随之翻转（两侧看它都是屏幕上方），**不要**为此改用 `ClipNormal` 建基或重渲染：那会把图案上下翻转。MID 参数按名字匹配（不走 GUID），所以不需要第三个 MPC。
3. **`SectionRadiusCm = 1.05 × GetSurfaceRadiusCm()` 优先于 spec §9 的数值例子。** §9 的 `PlaneRadiusCm ≈ 1980 / Depth ≈ 80` 隐含 `SectionRadiusCm = 10000`（即 1.0×），与 §3.1 的 1.05× 冲突。按 §3.1：在 `u ≈ 0.19802` 处 `PlaneRadiusCm = 2079.2`、`DepthVoxels = 79.208`。测试断言 2079/79.2（容差 1.0）。
4. **`StellarEraPlanet.Build.cs` 需要私有 `"RHI"`。** `FUpdateTextureRegion2D` 定义在 `Runtime/RHI/Public/RHITypes.h:126`，spec §11 的「预计无需改动」在此不成立——只加这一项。另有一处 `Build.cs` 改动：Task 5 把 `StellarEraPlanet` 从 `StellarEra` 的**私有**依赖提升为**公开**依赖，因为 `ESectionRing` 出现在公开头 `StellarEraPlayerController.h` 里（公开头只能包含公开依赖的头）。两个 `Build.cs` 改动之外不动其它模块。
5. **剖面组件与其子组件在运行期创建。** 子组件（圆盘 SMC、圆环 LineBatch）在 `Activate` 时 `NewObject + SetupAttachment + RegisterComponent` 创建，`EndPlay` 里 `DestroyComponent`。理由：在组件构造函数里对**自身**做 `CreateDefaultSubobject` 再用 `SetupAttachment(this)` 的注册语义不稳；不激活时也不该有任何额外渲染对象。
6. **`CreateTransient` 之后必须显式 `UpdateResource()`**，否则纹理在第一次 `UpdateTextureRegions` 前可能没有 RHI 资源。
7. **像素缓冲必须跨帧持有**（`UpdateTextureRegions` 是异步上传）。两个纹理各配一个持久 `TArray<FColor>` 成员，不随调用栈销毁。
8. **圆环轴在 `StellarEra` 侧用 `uint8` 传，不用 Planet 模块的枚举。** `AStellarEraPlayerController` 的成员与参数一律 `uint8 CutawayRingAxis`（0=X/1=Y/2=Z），避免把 `PlanetCutawayMath.h` 的枚举塞进控制器公开头。控制器包含 `PlanetCutawayMath.h` 是允许的（`StellarEra → StellarEraPlanet` 已是既有方向）。
9. **图例数据用 `FCutawayLegendEntry` 中转**，`FormatCutawayLegendLines` 只吃 `TArrayView<const FCutawayLegendEntry>`，保持 `StarMapUISystem.h` 不含行星类型。
10. **Masked 材质的羽化会被 `OpacityMaskClipValue` 量化。** 圆盘边缘的 1.5px 羽化最终只体现为「过/不过阈值」，这是 Masked 的固有限制，spec §3.1 已接受；不要为此改成 Translucent（会引入排序问题）。
11. **单帧光栅化开销是已知风险。** 每个像素调一次 `GetSurfaceRadiusCmAtDirection`（内部 `MakeTerrainContext` + 噪声 + 2×`SampleGeometryDelta`）与一次 `Geology->Sample`。512² ≈ 26 万次，预计 0.2–1 s 一次性卡顿（spec 允许）；96² ≈ 9 千次，可能仍超 33 ms 预览预算。接受，并在 Task 7 的 PIE 清单里实测。方向网格缓存**不在本计划范围**。
12. **资产用命令头脚本创建，不用 MCP。** MCP 的 `execute_python_code` 无法在编辑器关闭时工作，而资产脚本必须与构建同批执行；`-run=pythonscript` 可无头复现。脚本必须幂等（重复运行只打 `already patched`）。

## 文件结构

**新建**

| 文件 | 职责 |
|---|---|
| `Source/StellarEraPlanet/Public/PlanetCutawayMath.h` | 枚举、圆环折线结构、纯函数声明（法线/基/旋转/拾取/遮罩镜像/环上取点） |
| `Source/StellarEraPlanet/Private/PlanetCutawayMath.cpp` | 上述实现，零 UObject 依赖 |
| `Source/StellarEraPlanet/Public/PlanetSectionImage.h` | `FPlanetSectionImageParams` 与 `FPlanetSectionImage::Render` 声明 |
| `Source/StellarEraPlanet/Private/PlanetSectionImage.cpp` | 逐像素光栅化（spec §3.1 公式），回调注入地表半径与地质采样 |
| `Source/StellarEraPlanet/Public/PlanetCutawayComponent.h` | 行星上的剖面组件：激活/退出/旋转/请求渲染/取环 |
| `Source/StellarEraPlanet/Private/PlanetCutawayComponent.cpp` | 资产加载、纹理上传、MPC 写入、圆盘/圆环更新、大气隐藏与恢复 |
| `Source/StellarEraPlanet/Private/PlanetCutawayTests.cpp` | 8 个自动化测试（Math 5 + Image 3） |
| `Source/StellarEra/Private/CutawayLegendTests.cpp` | 第 9 个测试（图例格式化） |
| `Tools/Materials/CreateCutawayAssets.py` | 幂等创建 2 个 MPC + `M_PlanetSection`，并给 `M_PlanetVertexColor` 插入裁剪遮罩 |

**修改**

| 文件 | 改动 |
|---|---|
| `Source/StellarEraPlanet/StellarEraPlanet.Build.cs` | 私有依赖加 `"RHI"` |
| `Source/StellarEra/StellarEra.Build.cs` | `StellarEraPlanet` 由私有依赖提升为公开依赖（公开头暴露 `ESectionRing`） |
| `Source/StellarEraPlanet/Public/PlanetGeologyField.h` / `.cpp` | 加 `GetElementDefinitions()` |
| `Source/StellarEraPlanet/Public/VoxelPlanetActor.h` / `.cpp` | 加 `GetCutawayComponent()`、`GetGeologyField()`、默认子对象 |
| `Source/StellarEra/Public/StarMapCameraPawn.h` / `.cpp` | 加 `OrientToDirection()` |
| `Source/StellarEra/Public/StellarEraPlayerController.h` / `.cpp` | 输入动作、状态机、进出场钩子、`ShowPromptFor`、`PickCutawayRing` |
| `Source/StellarEra/Public/StarMapUISystem.h` / `.cpp` | `FCutawayLegendEntry`、`FormatCutawayLegendLines`、`DrawCutawayLegend` |

**执行顺序理由：** 纯函数（Task 1、2）先落地并测通，后面所有集成都只调用它们；资产（Task 3）先于组件（Task 4）因为组件 `LoadObject` 需要它们存在；输入（Task 5）先于 UI（Task 6）因为图例读的是控制器上的激活状态。

---

### Task 0: 基线与分支卫生

**Files:** 无源码改动（只提交文档）

**Interfaces:**
- Consumes: 无
- Produces: 干净的基线与一个已入库的 spec+plan，供后续所有 `git commit -- <paths>` 使用

- [ ] **Step 1: 确认工作区在源码文档层面是干净的**

```bash
cd /f/workspace/ue/StellarEra && git status --short -- Source docs Tools
```

Expected: 只有本计划文件是未跟踪（`?? docs/superpowers/plans/2026-10-24-mineral-cutaway-editor.md`），以及 spec 若尚未提交（`?? docs/superpowers/specs/2026-10-24-mineral-cutaway-editor-design.md`）。若出现已跟踪源码的改动，先问清楚是谁的，不要顺手带走。

- [ ] **Step 2: 记录基线测试结果**

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra" -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout \
  -abslog="F:/workspace/ue/StellarEra/Saved/Logs/Baseline.log"
grep -E "Automation Test (Completed|Failed)|Result: (Passed|Failed)" \
  "F:/workspace/ue/StellarEra/Saved/Logs/Baseline.log" | tail -20
```

Expected: 全绿（此前基线为 53/53）。若有既存失败，先记录，并只对本功能相关的失败负责。

- [ ] **Step 3: 提交 spec 与计划**

```bash
cd /f/workspace/ue/StellarEra
git add docs/superpowers/specs/2026-10-24-mineral-cutaway-editor-design.md \
        docs/superpowers/plans/2026-10-24-mineral-cutaway-editor.md
git commit -m "docs(剖面): 矿物剖面编辑器设计稿与实施计划" \
  -m "Co-Authored-By: Claude Code <noreply@anthropic.com>" -- \
  docs/superpowers/specs/2026-10-24-mineral-cutaway-editor-design.md \
  docs/superpowers/plans/2026-10-24-mineral-cutaway-editor.md
git log --oneline -1
```

---

### Task 1: 剖面纯数学（`FPlanetCutawayMath`）

**Files:**
- Create: `Source/StellarEraPlanet/Public/PlanetCutawayMath.h`
- Create: `Source/StellarEraPlanet/Private/PlanetCutawayMath.cpp`
- Test: `Source/StellarEraPlanet/Private/PlanetCutawayTests.cpp`（本任务写 Math 的 5 个）

**Interfaces:**
- Consumes: 无（只用 Core）
- Produces:
  - `enum class ESectionRing : uint8 { X=0, Y=1, Z=2 };`
  - `struct FSectionRingWorldPolyline { ESectionRing Ring; TArray<FVector> Points; };`
  - `struct FSectionRingScreenPolyline { ESectionRing Ring; TArray<FVector2D> Points; };`
  - `static FVector FPlanetCutawayMath::ComputeClipNormal(const FVector& PlaneNormal, const FVector& CameraPos, const FVector& CenterCm);`
  - `static void FPlanetCutawayMath::ComputePlaneBasis(const FVector& PlaneNormal, FVector& OutAxisU, FVector& OutAxisV);`
  - `static FQuat FPlanetCutawayMath::RotatePlane(const FQuat& PlaneOrientation, const FVector& Axis, float DeltaDegrees);`
  - `static bool FPlanetCutawayMath::PickSectionRing(TArrayView<const FSectionRingScreenPolyline> Rings, const FVector2D& MousePx, float TolerancePx, ESectionRing& OutRing);`
  - `static float FPlanetCutawayMath::ComputeOpacityMask(const FVector& WorldPos, const FVector& Origin, const FVector& Normal, float InsideRadiusCm, bool bActive);`
  - `static FVector FPlanetCutawayMath::RingPoint(ESectionRing Ring, float Radius, float AngleRadians);`

- [ ] **Step 1: 写头文件**

Create `Source/StellarEraPlanet/Public/PlanetCutawayMath.h`:

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.
// 矿物剖面的纯数学：朝向法线、平面基、旋转、屏幕拾取、遮罩镜像、环上取点。
// 全部无引擎资产依赖，可在无头测试里直接调用。

#pragma once

#include "CoreMinimal.h"

/** 三根旋转环对应的世界轴。 */
enum class ESectionRing : uint8
{
	X = 0,
	Y = 1,
	Z = 2
};

/** 世界空间的圆环折线（组件产出，控制器投影）。 */
struct FSectionRingWorldPolyline
{
	ESectionRing Ring = ESectionRing::X;
	TArray<FVector> Points;
};

/** 屏幕空间的圆环折线（像素坐标）。 */
struct FSectionRingScreenPolyline
{
	ESectionRing Ring = ESectionRing::X;
	TArray<FVector2D> Points;
};

struct FPlanetCutawayMath
{
	/**
	 * 让「相机侧」有正投影的法线：dot(CameraPos − CenterCm, 结果) >= 0。
	 * 退化（法线为零或相机在球心）时原样返回归一化后的 PlaneNormal。
	 */
	static FVector ComputeClipNormal(
		const FVector& PlaneNormal,
		const FVector& CameraPos,
		const FVector& CenterCm);

	/**
	 * 由法线造右手正交基：AxisU、AxisV、N 两两垂直，且 Cross(N, AxisU) == AxisV。
	 * AxisV 即「从相机看过去的上方」，图像第 0 行对应 −AxisV（spec §3.1）。
	 */
	static void ComputePlaneBasis(
		const FVector& PlaneNormal,
		FVector& OutAxisU,
		FVector& OutAxisV);

	/** 绕世界轴旋转已有朝向：Quat(Axis, radians(DeltaDegrees)) * PlaneOrientation。 */
	static FQuat RotatePlane(
		const FQuat& PlaneOrientation,
		const FVector& Axis,
		float DeltaDegrees);

	/**
	 * 屏幕空间拾取最近的圆环折线。距离用点到折线各段的最短距离。
	 * 并列（差 < 1e-3 px）时按 X → Y → Z 顺序取先出现者。
	 * 全部超过 TolerancePx 时返回 false。
	 */
	static bool PickSectionRing(
		TArrayView<const FSectionRingScreenPolyline> Rings,
		const FVector2D& MousePx,
		float TolerancePx,
		ESectionRing& OutRing);

	/**
	 * spec §4.1 遮罩公式的 CPU 镜像，供测试与心智校验：
	 * Half = saturate(dot(P−Origin, N) * 10)
	 * Inside = saturate((InsideRadiusCm − |P−Origin|) * 0.02)
	 * Mask = 1 − Half * Inside * (bActive ? 1 : 0)
	 */
	static float ComputeOpacityMask(
		const FVector& WorldPos,
		const FVector& Origin,
		const FVector& Normal,
		float InsideRadiusCm,
		bool bActive);

	/** 某根环上角度处的世界偏移（未加球心）。X 环在 YZ 平面，Y 环在 XZ，Z 环在 XY。 */
	static FVector RingPoint(ESectionRing Ring, float Radius, float AngleRadians);
};
```

- [ ] **Step 2: 给新头文件加 BOM**

```bash
cd /f/workspace/ue/StellarEra
f=Source/StellarEraPlanet/Public/PlanetCutawayMath.h
printf '\xEF\xBB\xBF' > "$f.bom" && cat "$f" >> "$f.bom" && mv "$f.bom" "$f"
head -c 3 "$f" | od -An -tx1
```

Expected: ` ef bb bf`

- [ ] **Step 3: 写失败的测试（Math 5 个）**

Create `Source/StellarEraPlanet/Private/PlanetCutawayTests.cpp`:

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.
// 矿物剖面：纯数学与剖面光栅化的自动化测试（spec §9 的 9 条中的 8 条）。

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "PlanetCutawayMath.h"

namespace
{
	/** 造一个等边三角形环，避免依赖 128 段真实几何。 */
	FSectionRingScreenPolyline MakeTriangleRing(ESectionRing Ring, const FVector2D& Offset)
	{
		FSectionRingScreenPolyline Polyline;
		Polyline.Ring = Ring;
		Polyline.Points.Add(Offset + FVector2D(0.0, -100.0));
		Polyline.Points.Add(Offset + FVector2D(86.6, 50.0));
		Polyline.Points.Add(Offset + FVector2D(-86.6, 50.0));
		return Polyline;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetCutawayClipNormalTest,
	"StellarEra.Planet.Cutaway.Math.ClipNormalFacesCamera",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetCutawayClipNormalTest::RunTest(const FString& Parameters)
{
	const FVector Center(100.0, 0.0, 0.0);
	const FVector PlaneNormal = FVector::UpVector;

	const FVector CameraAbove = Center + FVector::UpVector * 1000.0;
	TestTrue(TEXT("法线朝相机时保持不变"),
		FPlanetCutawayMath::ComputeClipNormal(PlaneNormal, CameraAbove, Center).Equals(
			FVector::UpVector, 1e-9));

	const FVector CameraBelow = Center - FVector::UpVector * 1000.0;
	TestTrue(TEXT("相机在反侧时翻转"),
		FPlanetCutawayMath::ComputeClipNormal(PlaneNormal, CameraBelow, Center).Equals(
			-FVector::UpVector, 1e-9));

	TestTrue(TEXT("相机在球心时原样返回"),
		FPlanetCutawayMath::ComputeClipNormal(PlaneNormal, Center, Center).Equals(
			FVector::UpVector, 1e-9));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetCutawayPlaneBasisTest,
	"StellarEra.Planet.Cutaway.Math.PlaneBasisOrthonormal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetCutawayPlaneBasisTest::RunTest(const FString& Parameters)
{
	// 逐个法线方向验证正交性与右手性：这是「图像不镜像」的唯一数学依据。
	const TArray<FVector> Normals = {
		FVector::UpVector,
		FVector::ForwardVector,
		FVector::RightVector,
		FVector(1.0, 2.0, 3.0).GetSafeNormal(),
		FVector(-0.3, 0.9, 0.2).GetSafeNormal(),
		FVector(0.0, 0.0, -1.0)
	};

	for (const FVector& Normal : Normals)
	{
		FVector AxisU = FVector::ZeroVector;
		FVector AxisV = FVector::ZeroVector;
		FPlanetCutawayMath::ComputePlaneBasis(Normal, AxisU, AxisV);

		TestTrue(TEXT("AxisU 单位长"), FMath::IsNearlyEqual(AxisU.Size(), 1.0, 1e-6));
		TestTrue(TEXT("AxisV 单位长"), FMath::IsNearlyEqual(AxisV.Size(), 1.0, 1e-6));
		TestTrue(TEXT("AxisU 垂直于法线"),
			FMath::IsNearlyZero(FVector::DotProduct(AxisU, Normal), 1e-6));
		TestTrue(TEXT("AxisV 垂直于法线"),
			FMath::IsNearlyZero(FVector::DotProduct(AxisV, Normal), 1e-6));
		TestTrue(TEXT("AxisU 垂直于 AxisV"),
			FMath::IsNearlyZero(FVector::DotProduct(AxisU, AxisV), 1e-6));
		TestTrue(TEXT("Cross(N, AxisU) == AxisV"),
			FVector::CrossProduct(Normal, AxisU).Equals(AxisV, 1e-6));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetCutawayRotatePlaneTest,
	"StellarEra.Planet.Cutaway.Math.RotatePlaneCompose",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetCutawayRotatePlaneTest::RunTest(const FString& Parameters)
{
	const FQuat Identity = FQuat::Identity;

	// 绕 Z 转 90°：朝向的局部 Z（法线）应变成世界 +X。
	const FQuat Rotated = FPlanetCutawayMath::RotatePlane(Identity, FVector::UpVector, 90.f);
	TestTrue(TEXT("绕 Z 转 90° 后法线指向 +X"),
		Rotated.GetAxisZ().Equals(FVector::ForwardVector, 1e-6));

	// 再转 90° 应到 −X（叠加，而不是覆盖）。
	const FQuat RotatedTwice = FPlanetCutawayMath::RotatePlane(Rotated, FVector::UpVector, 90.f);
	TestTrue(TEXT("叠加第二次旋转后法线指向 −X"),
		RotatedTwice.GetAxisZ().Equals(-FVector::ForwardVector, 1e-6));

	// 0 度与零轴都不改变朝向。
	TestTrue(TEXT("0 度不变"),
		FPlanetCutawayMath::RotatePlane(Rotated, FVector::UpVector, 0.f).Equals(Rotated, 1e-9));
	TestTrue(TEXT("零轴不变"),
		FPlanetCutawayMath::RotatePlane(Rotated, FVector::ZeroVector, 45.f).Equals(Rotated, 1e-9));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetCutawayOpacityMaskTest,
	"StellarEra.Planet.Cutaway.Math.OpacityMask",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetCutawayOpacityMaskTest::RunTest(const FString& Parameters)
{
	const FVector Origin = FVector::ZeroVector;
	const FVector Normal = FVector::UpVector;
	constexpr float InsideRadiusCm = 10500.f;

	// 关闭时恒为 1：这是「退出即恢复」的数值保证。
	TestEqual(TEXT("未激活时近侧也不裁"),
		FPlanetCutawayMath::ComputeOpacityMask(
			FVector(0.0, 0.0, 500.0), Origin, Normal, InsideRadiusCm, false), 1.f);

	// 近侧（法线正方向）且在内球里 → 0。
	TestEqual(TEXT("近侧内球完全裁掉"),
		FPlanetCutawayMath::ComputeOpacityMask(
			FVector(0.0, 0.0, 500.0), Origin, Normal, InsideRadiusCm, true), 0.f);

	// 远侧（法线负方向）→ 1。
	TestEqual(TEXT("远侧不裁"),
		FPlanetCutawayMath::ComputeOpacityMask(
			FVector(0.0, 0.0, -500.0), Origin, Normal, InsideRadiusCm, true), 1.f);

	// 内球之外 → 1（大气/远景不误伤）。
	TestEqual(TEXT("内球之外不裁"),
		FPlanetCutawayMath::ComputeOpacityMask(
			FVector(0.0, 0.0, 20000.0), Origin, Normal, InsideRadiusCm, true), 1.f);

	// 切面上（dot == 0）→ 1。
	TestEqual(TEXT("切面本身不裁"),
		FPlanetCutawayMath::ComputeOpacityMask(
			FVector(900.0, 900.0, 0.0), Origin, Normal, InsideRadiusCm, true), 1.f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetCutawayRingPickTest,
	"StellarEra.Planet.Cutaway.Math.RingPick",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetCutawayRingPickTest::RunTest(const FString& Parameters)
{
	const FSectionRingScreenPolyline RingX = MakeTriangleRing(ESectionRing::X, FVector2D::ZeroVector);
	const FSectionRingScreenPolyline RingY = MakeTriangleRing(ESectionRing::Y, FVector2D(0.0, 300.0));
	const TArray<FSectionRingScreenPolyline> Rings = { RingX, RingY };

	ESectionRing Picked = ESectionRing::Z;

	// 命中 X 环的一条边（底边 y = -100）。
	TestTrue(TEXT("命中 X 环"),
		FPlanetCutawayMath::PickSectionRing(Rings, FVector2D(0.0, -100.0), 12.f, Picked));
	TestEqual(TEXT("拾取结果为 X"), Picked, ESectionRing::X);

	// 命中 Y 环的顶点（y = 300 + 50）。
	TestTrue(TEXT("命中 Y 环"),
		FPlanetCutawayMath::PickSectionRing(Rings, FVector2D(0.0, 350.0), 12.f, Picked));
	TestEqual(TEXT("拾取结果为 Y"), Picked, ESectionRing::Y);

	// 离所有环都远 → false。
	TestFalse(TEXT("远处不命中"),
		FPlanetCutawayMath::PickSectionRing(Rings, FVector2D(0.0, 150.0), 12.f, Picked));

	// 容差边界：底边下方 10px 命中，13px 不命中。
	TestTrue(TEXT("容差内命中"),
		FPlanetCutawayMath::PickSectionRing(Rings, FVector2D(0.0, -110.0), 12.f, Picked));
	TestFalse(TEXT("容差外不命中"),
		FPlanetCutawayMath::PickSectionRing(Rings, FVector2D(0.0, -113.0), 12.f, Picked));

	// 并列时 X 优先：两条环完全重合。
	const FSectionRingScreenPolyline RingZAtX =
		MakeTriangleRing(ESectionRing::Z, FVector2D::ZeroVector);
	const TArray<FSectionRingScreenPolyline> Overlapped = { RingX, RingZAtX };
	TestTrue(TEXT("重合时仍命中"),
		FPlanetCutawayMath::PickSectionRing(Overlapped, FVector2D(0.0, -100.0), 12.f, Picked));
	TestEqual(TEXT("并列取先出现的 X"), Picked, ESectionRing::X);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
```

- [ ] **Step 4: 给测试文件加 BOM**

```bash
cd /f/workspace/ue/StellarEra
f=Source/StellarEraPlanet/Private/PlanetCutawayTests.cpp
printf '\xEF\xBB\xBF' > "$f.bom" && cat "$f" >> "$f.bom" && mv "$f.bom" "$f"
head -c 3 "$f" | od -An -tx1
```

- [ ] **Step 5: 写实现**

Create `Source/StellarEraPlanet/Private/PlanetCutawayMath.cpp`:

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetCutawayMath.h"

namespace
{
	/** 二维点到线段的最短距离平方（UE 只提供 FVector 版本）。 */
	float DistanceSquaredToSegment2D(const FVector2D& Point, const FVector2D& A, const FVector2D& B)
	{
		const FVector2D AB = B - A;
		const double LengthSquared = AB.SizeSquared();
		if (LengthSquared <= 0.0)
		{
			return float(FVector2D::DistSquared(Point, A));
		}
		const double T = FMath::Clamp(
			FVector2D::DotProduct(Point - A, AB) / LengthSquared, 0.0, 1.0);
		return float(FVector2D::DistSquared(Point, A + AB * T));
	}
}

FVector FPlanetCutawayMath::ComputeClipNormal(
	const FVector& PlaneNormal,
	const FVector& CameraPos,
	const FVector& CenterCm)
{
	const FVector Normal = PlaneNormal.GetSafeNormal();

	const FVector ToCamera = CameraPos - CenterCm;
	if (Normal.IsNearlyZero() || ToCamera.IsNearlyZero())
	{
		return Normal;
	}

	return FVector::DotProduct(ToCamera, Normal) >= 0.0 ? Normal : -Normal;
}

void FPlanetCutawayMath::ComputePlaneBasis(
	const FVector& PlaneNormal,
	FVector& OutAxisU,
	FVector& OutAxisV)
{
	const FVector N = PlaneNormal.GetSafeNormal();

	// |N.Z| ≈ 1 时 Up 与 N 平行，cross 会退化，改用 Right 当参考轴。
	const FVector Reference =
		FMath::Abs(N.Z) < 0.99 ? FVector::UpVector : FVector::RightVector;

	OutAxisU = FVector::CrossProduct(Reference, N).GetSafeNormal();
	if (OutAxisU.IsNearlyZero())
	{
		// N 与 Up / Right 同时平行在几何上不可能；这里只保证不返回零向量。
		OutAxisU = FVector::ForwardVector;
	}
	OutAxisV = FVector::CrossProduct(N, OutAxisU);
}

FQuat FPlanetCutawayMath::RotatePlane(
	const FQuat& PlaneOrientation,
	const FVector& Axis,
	float DeltaDegrees)
{
	const FVector UnitAxis = Axis.GetSafeNormal();
	if (UnitAxis.IsNearlyZero() || DeltaDegrees == 0.f)
	{
		return PlaneOrientation;
	}

	const FQuat Delta(UnitAxis, FMath::DegreesToRadians(DeltaDegrees));
	return (Delta * PlaneOrientation).GetNormalized();
}

bool FPlanetCutawayMath::PickSectionRing(
	TArrayView<const FSectionRingScreenPolyline> Rings,
	const FVector2D& MousePx,
	float TolerancePx,
	ESectionRing& OutRing)
{
	// 距离差小于这个值视为并列：先遍历到的环优先（X → Y → Z）。
	constexpr float TieEpsilonPx = 1e-3f;

	float BestDistanceSquared = TNumericLimits<float>::Max();
	ESectionRing BestRing = ESectionRing::X;
	bool bFound = false;

	for (const FSectionRingScreenPolyline& Polyline : Rings)
	{
		const int32 Count = Polyline.Points.Num();
		if (Count < 2)
		{
			continue;
		}

		float RingDistanceSquared = TNumericLimits<float>::Max();
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const FVector2D& A = Polyline.Points[Index];
			const FVector2D& B = Polyline.Points[(Index + 1) % Count];
			RingDistanceSquared = FMath::Min(
				RingDistanceSquared, DistanceSquaredToSegment2D(MousePx, A, B));
		}

		if (!bFound
			|| FMath::Sqrt(RingDistanceSquared) < FMath::Sqrt(BestDistanceSquared) - TieEpsilonPx)
		{
			BestDistanceSquared = RingDistanceSquared;
			BestRing = Polyline.Ring;
			bFound = true;
		}
	}

	OutRing = BestRing;
	return bFound && FMath::Sqrt(BestDistanceSquared) <= TolerancePx;
}

float FPlanetCutawayMath::ComputeOpacityMask(
	const FVector& WorldPos,
	const FVector& Origin,
	const FVector& Normal,
	float InsideRadiusCm,
	bool bActive)
{
	if (!bActive)
	{
		return 1.f;
	}

	const FVector Relative = WorldPos - Origin;
	const FVector UnitNormal = Normal.GetSafeNormal();

	const float Half = FMath::Clamp(
		float(FVector::DotProduct(Relative, UnitNormal)) * 10.f, 0.f, 1.f);
	const float Inside = FMath::Clamp(
		(InsideRadiusCm - float(Relative.Size())) * 0.02f, 0.f, 1.f);

	return 1.f - Half * Inside;
}

FVector FPlanetCutawayMath::RingPoint(ESectionRing Ring, float Radius, float AngleRadians)
{
	const float C = FMath::Cos(AngleRadians);
	const float S = FMath::Sin(AngleRadians);

	switch (Ring)
	{
	case ESectionRing::X: return FVector(0.f, C, S) * Radius;  // YZ 平面
	case ESectionRing::Y: return FVector(C, 0.f, S) * Radius;  // XZ 平面
	default:              return FVector(C, S, 0.f) * Radius;  // XY 平面
	}
}
```

- [ ] **Step 6: 给实现文件加 BOM**

```bash
cd /f/workspace/ue/StellarEra
f=Source/StellarEraPlanet/Private/PlanetCutawayMath.cpp
printf '\xEF\xBB\xBF' > "$f.bom" && cat "$f" >> "$f.bom" && mv "$f.bom" "$f"
head -c 3 "$f" | od -An -tx1
```

- [ ] **Step 7: 构建（此时仍可能因 Task 2 的头未就位而红）**

```bash
cd /d/ && "D:/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" \
  "D:/Application/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" \
  StellarEraEditor Win64 Development \
  -Project="F:/workspace/ue/StellarEra/StellarEra.uproject" -WaitMutex -FromMsBuild 2>&1 | tail -20
```

Expected: 编译通过、只有 DLL 链接阶段前一切正常。若报 `PlanetCutawayMath` 相关错误 → 修完再继续。本任务不引入 `PlanetSectionImage.h`，测试文件此时也不 include 它（Task 2 才加），所以本任务应能独立转绿。

- [ ] **Step 8: 跑本任务测试**

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra.Planet.Cutaway.Math" \
  -TestExit="Automation Test Queue Empty" -unattended -nopause -nullrhi -nosplash -stdout \
  -abslog="F:/workspace/ue/StellarEra/Saved/Logs/CutawayMathTests.log"
grep -E "Test (Completed|Failed)|Result:" "F:/workspace/ue/StellarEra/Saved/Logs/CutawayMathTests.log" | tail -20
```

Expected: 5/5 passed（`ClipNormalFacesCamera`、`PlaneBasisOrthonormal`、`RotatePlaneCompose`、`OpacityMask`、`RingPick`）。

- [ ] **Step 9: 提交**

```bash
cd /f/workspace/ue/StellarEra
git add Source/StellarEraPlanet/Public/PlanetCutawayMath.h \
        Source/StellarEraPlanet/Private/PlanetCutawayMath.cpp \
        Source/StellarEraPlanet/Private/PlanetCutawayTests.cpp
git commit -m "feat(剖面): 剖面纯数学（法线/基/旋转/拾取/遮罩镜像）" \
  -m "Co-Authored-By: Claude Code <noreply@anthropic.com>" -- \
  Source/StellarEraPlanet/Public/PlanetCutawayMath.h \
  Source/StellarEraPlanet/Private/PlanetCutawayMath.cpp \
  Source/StellarEraPlanet/Private/PlanetCutawayTests.cpp
```

---

### Task 2: 剖面光栅化（`FPlanetSectionImage`）+ `RHI` 依赖

**Files:**
- Create: `Source/StellarEraPlanet/Public/PlanetSectionImage.h`
- Create: `Source/StellarEraPlanet/Private/PlanetSectionImage.cpp`
- Modify: `Source/StellarEraPlanet/StellarEraPlanet.Build.cs`（私有依赖加 `"RHI"`）
- Test: `Source/StellarEraPlanet/Private/PlanetCutawayTests.cpp`（追加 Image 3 个）

**Interfaces:**
- Consumes: `FPlanetGeologySample`、`FPlanetGeologyAppearance`（`PlanetGeologyTypes.h`）
- Produces:
  - `struct FPlanetSectionImageParams { int32 Resolution; FVector AxisU, AxisV, PlaneNormal; float SectionRadiusCm, VoxelSize, BaseRadiusVoxels; FLinearColor SurfaceColor; FPlanetGeologyAppearance Appearance; }`
  - `static void FPlanetSectionImage::Render(const FPlanetSectionImageParams&, TFunctionRef<float(const FVector&)>, TFunctionRef<FPlanetGeologySample(const FVector&, double)>, TArray<FColor>& OutPixels)`

- [ ] **Step 1: 改 `Build.cs`**

在 `Source/StellarEraPlanet/StellarEraPlanet.Build.cs` 里找到 `PrivateDependencyModuleNames.AddRange(...)`，把列表**追加** `"RHI"`（保留原有项）：

```csharp
	// RHI：FUpdateTextureRegion2D（RHITypes.h）是 UTexture2D::UpdateTextureRegions 的参数类型。
	PrivateDependencyModuleNames.AddRange(new string[] { "AssetRegistry", "Json", "RHI" });
```

- [ ] **Step 2: 写头文件**

Create `Source/StellarEraPlanet/Public/PlanetSectionImage.h`:

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.
// 剖面圆盘的 CPU 光栅化：逐像素算矿物的颜色与不透明度。
// 只依赖值类型与两个回调，不触引擎、不触资产，可在无头测试里跑。

#pragma once

#include "CoreMinimal.h"
#include "PlanetGeologyTypes.h"

/** 一次剖面渲染的全部输入。 */
struct FPlanetSectionImageParams
{
	int32 Resolution = 512;

	/** 平面基：AxisU 向右、AxisV 向上（图像第 0 行对应 −AxisV）。 */
	FVector AxisU = FVector::ForwardVector;
	FVector AxisV = FVector::RightVector;

	/** 切面法线。光栅化本身不用它，保留给调试与未来的各向异性。 */
	FVector PlaneNormal = FVector::UpVector;

	/** 剖面半径（厘米）= 1.05 × 基准表面半径。 */
	float SectionRadiusCm = 100.f;

	/** 一个体素的边长（厘米）。 */
	float VoxelSize = 100.f;

	/** 基准表面半径（体素），用于深度变暗的归一化。 */
	float BaseRadiusVoxels = 100.f;

	/** 没有矿处的基础底色（行星 SurfaceColor）。 */
	FLinearColor SurfaceColor = FLinearColor::Black;

	/** 颜色显现参数（阈值、渐变、纯色门限）。 */
	FPlanetGeologyAppearance Appearance;
};

struct FPlanetSectionImage
{
	/**
	 * 填充 OutPixels（Res×Res，行主序，BGRA 语义由 FColor 承担）。
	 *
	 * SurfaceRadiusCmFn：给单位方向，返回该方向的地表半径（厘米，含起伏）。
	 * SampleFn：给单位方向与深度（体素，向内为正），返回成分采样。
	 *
	 * 公式见 spec §3.1；每像素两次回调，调用方负责它们的开销。
	 */
	static void Render(
		const FPlanetSectionImageParams& Params,
		TFunctionRef<float(const FVector& UnitDir)> SurfaceRadiusCmFn,
		TFunctionRef<FPlanetGeologySample(const FVector& UnitDir, double DepthVoxels)> SampleFn,
		TArray<FColor>& OutPixels);
};
```

- [ ] **Step 3: 给头文件加 BOM**

```bash
cd /f/workspace/ue/StellarEra
f=Source/StellarEraPlanet/Public/PlanetSectionImage.h
printf '\xEF\xBB\xBF' > "$f.bom" && cat "$f" >> "$f.bom" && mv "$f.bom" "$f"
head -c 3 "$f" | od -An -tx1
```

- [ ] **Step 4: 追加 3 个失败的测试**

在 `Source/StellarEraPlanet/Private/PlanetCutawayTests.cpp` 的 `#include "PlanetCutawayMath.h"` 下一行加：

```cpp
#include "PlanetSectionImage.h"
```

然后在 `#endif // WITH_DEV_AUTOMATION_TESTS` **之前**插入：

```cpp
namespace
{
	/** 一个均匀球 + 固定成分采样，供 Image 测试共用。 */
	FPlanetSectionImageParams MakeUniformSphereParams(float SectionRadiusCm = 10500.f)
	{
		FPlanetSectionImageParams Params;
		Params.Resolution = 101;
		Params.AxisU = FVector::ForwardVector;
		Params.AxisV = FVector::RightVector;
		Params.SectionRadiusCm = SectionRadiusCm;
		Params.VoxelSize = 100.f;
		Params.BaseRadiusVoxels = 100.f;
		Params.SurfaceColor = FLinearColor(0.2f, 0.4f, 0.6f);
		Params.Appearance = FPlanetGeologyAppearance();
		return Params;
	}

	bool ByteNear(uint8 Actual, uint8 Expected, int32 Tolerance = 1)
	{
		return FMath::Abs(int32(Actual) - int32(Expected)) <= Tolerance;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetSectionImageSolidSphereTest,
	"StellarEra.Planet.Cutaway.Image.SolidSphereAlpha",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetSectionImageSolidSphereTest::RunTest(const FString& Parameters)
{
	// 均匀球：中心不透明，边界羽化，圆盘外全透明。
	constexpr int32 Res = 101;
	const FPlanetSectionImageParams Params = MakeUniformSphereParams();

	TArray<FColor> Pixels;
	FPlanetSectionImage::Render(Params,
		[](const FVector&) { return 10000.f; },
		[](const FVector&, double)
		{
			FPlanetGeologySample Sample;
			return Sample;
		},
		Pixels);

	TestEqual(TEXT("像素数 = Res²"), Pixels.Num(), Res * Res);
	TestEqual(TEXT("中心不透明"), int32(Pixels[50 + 50 * Res].A), 255);

	// (95,50)：u≈0.891 → PlaneRadius≈9356，在羽化带内侧。
	TestEqual(TEXT("内圈不透明"), int32(Pixels[95 + 50 * Res].A), 255);
	// (100,50)：u≈0.990 → PlaneRadius≈10396，越过表面 1.9px（> 1.5px 羽化）。
	TestEqual(TEXT("球外透明"), int32(Pixels[100 + 50 * Res].A), 0);
	// 角落在圆盘之外。
	TestEqual(TEXT("圆盘角透明"), int32(Pixels[0].A), 0);

	// 羽化带里必须存在中间值：证明边缘不是硬切。
	bool bFoundFeather = false;
	for (int32 X = 90; X < 101; ++X)
	{
		const uint8 Alpha = Pixels[X + 50 * Res].A;
		bFoundFeather |= (Alpha > 0 && Alpha < 255);
	}
	TestTrue(TEXT("存在半透明羽化像素"), bFoundFeather);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetSectionImagePresenceTest,
	"StellarEra.Planet.Cutaway.Image.PresenceThreshold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetSectionImagePresenceTest::RunTest(const FString& Parameters)
{
	// 中心像素：Surface 10000、PlaneRadius 0 → Depth 100 体素 = BaseRadiusVoxels
	// → DepthShade = 1 − 0.4 = 0.6 → Base = SurfaceColor × 0.25 × 0.6。
	constexpr int32 Res = 101;
	const FPlanetSectionImageParams Params = MakeUniformSphereParams();
	const FLinearColor Base =
		Params.SurfaceColor * 0.25f * (1.f - 0.4f * FMath::Clamp(100.f / 100.f, 0.f, 1.f));

	auto RenderCenter = [&Params](float DominantNorm, float Purity, const FLinearColor& Mineral)
	{
		TArray<FColor> Pixels;
		FPlanetSectionImage::Render(Params,
			[](const FVector&) { return 10000.f; },
			[&](const FVector&, double)
			{
				FPlanetGeologySample Sample;
				Sample.DominantNorm = DominantNorm;
				Sample.Purity = Purity;
				Sample.MineralColor = Mineral;
				return Sample;
			},
			Pixels);
		return Pixels[50 + 50 * Res];
	};

	// A：DominantNorm 低于阈值（0.22）→ 纯底色。
	{
		const FColor Actual = RenderCenter(0.10f, 1.f, FLinearColor(1.f, 0.f, 0.f));
		const FColor Expected = Base.ToFColor(true);
		TestTrue(TEXT("低于阈值 = 底色"),
			ByteNear(Actual.R, Expected.R) && ByteNear(Actual.G, Expected.G)
			&& ByteNear(Actual.B, Expected.B));
		TestEqual(TEXT("低于阈值仍不透明"), int32(Actual.A), 255);
	}

	// B：阈值 + 渐变以上、纯度满 → 纯矿物色。
	{
		const FColor Actual = RenderCenter(0.50f, 1.f, FLinearColor(1.f, 0.f, 0.f));
		const FColor Expected = FLinearColor(1.f, 0.f, 0.f).ToFColor(true);
		TestTrue(TEXT("高纯度 = 矿物色"),
			ByteNear(Actual.R, Expected.R) && ByteNear(Actual.G, Expected.G)
			&& ByteNear(Actual.B, Expected.B));
	}

	// C：纯度 0.35 / 0.7 = 0.5 → 底色与矿物色各一半。
	{
		const FColor Actual = RenderCenter(0.50f, 0.35f, FLinearColor(1.f, 0.f, 0.f));
		const FColor Expected =
			FLinearColor::LerpUsingHSV(Base, FLinearColor(1.f, 0.f, 0.f), 0.5f).ToFColor(true);
		TestTrue(TEXT("半纯度 = 半混合"),
			ByteNear(Actual.R, Expected.R) && ByteNear(Actual.G, Expected.G)
			&& ByteNear(Actual.B, Expected.B));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetSectionImageDepthTest,
	"StellarEra.Planet.Cutaway.Image.DepthMapping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetSectionImageDepthTest::RunTest(const FString& Parameters)
{
	// SectionRadius = 1.05 × 10000 = 10500（spec §3.1 优先于 §9 的数值例子）。
	// Res = 101 时像素 x=60 的 u = (60.5/101)*2 − 1 ≈ 0.19802。
	constexpr int32 Res = 101;
	constexpr float SectionRadiusCm = 10500.f;
	constexpr float SurfaceRadiusCm = 10000.f;
	constexpr float VoxelSize = 100.f;

	const double U = (60.0 + 0.5) / double(Res) * 2.0 - 1.0;
	const double ExpectedPlaneRadiusCm = U * double(SectionRadiusCm);
	const double ExpectedDepthVoxels = (double(SurfaceRadiusCm) - ExpectedPlaneRadiusCm) / double(VoxelSize);

	// 这两个数是本计划对 spec §9 的裁定值：2079.2 / 79.208。
	TestTrue(TEXT("PlaneRadius ≈ 2079.2"),
		FMath::IsNearlyEqual(ExpectedPlaneRadiusCm, 2079.2, 1.0));
	TestTrue(TEXT("Depth ≈ 79.208"),
		FMath::IsNearlyEqual(ExpectedDepthVoxels, 79.208, 0.5));

	const FPlanetSectionImageParams Params = MakeUniformSphereParams(SectionRadiusCm);

	TArray<double> Depths;
	TArray<FVector> Directions;
	TArray<FColor> Pixels;
	FPlanetSectionImage::Render(Params,
		[](const FVector&) { return SurfaceRadiusCm; },
		[&](const FVector& Dir, double DepthVoxels)
		{
			Depths.Add(DepthVoxels);
			Directions.Add(Dir);
			FPlanetGeologySample Sample;
			return Sample;
		},
		Pixels);

	// 某个像素的深度应精确等于推导值，且方向就是 AxisU（v = 0 那一行）。
	int32 Matched = 0;
	bool bDirectionOk = false;
	for (int32 Index = 0; Index < Depths.Num(); ++Index)
	{
		if (FMath::Abs(Depths[Index] - ExpectedDepthVoxels) <= 0.05)
		{
			++Matched;
			bDirectionOk |= Directions[Index].Equals(FVector::ForwardVector, 1e-4);
		}
	}

	TestEqual(TEXT("恰有一个像素落在该深度"), Matched, 1);
	TestTrue(TEXT("该像素方向为 AxisU"), bDirectionOk);

	return true;
}
```

- [ ] **Step 5: 写实现**

Create `Source/StellarEraPlanet/Private/PlanetSectionImage.cpp`:

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetSectionImage.h"

void FPlanetSectionImage::Render(
	const FPlanetSectionImageParams& Params,
	TFunctionRef<float(const FVector& UnitDir)> SurfaceRadiusCmFn,
	TFunctionRef<FPlanetGeologySample(const FVector& UnitDir, double DepthVoxels)> SampleFn,
	TArray<FColor>& OutPixels)
{
	const int32 Res = FMath::Max(Params.Resolution, 1);
	OutPixels.Reset();
	OutPixels.SetNumUninitialized(Res * Res);

	const float SectionRadiusCm = FMath::Max(Params.SectionRadiusCm, KINDA_SMALL_NUMBER);
	const float VoxelSize = FMath::Max(Params.VoxelSize, KINDA_SMALL_NUMBER);

	// 一个像素覆盖多长（厘米）：整条边长 2R 摊到 Res 个像素。
	const float PixelSizeCm = 2.f * SectionRadiusCm / float(Res);
	constexpr float FeatherPixels = 1.5f;

	// 深度变暗：DepthShade = 1 − 0.4 × saturate(DepthVoxels / BaseRadiusVoxels)。
	const float DepthShadeScale =
		Params.BaseRadiusVoxels > KINDA_SMALL_NUMBER ? 0.4f / Params.BaseRadiusVoxels : 0.f;
	const float PresenceScale = 1.f / FMath::Max(Params.Appearance.FadeRange, 1e-4f);
	const float PurityScale = 1.f / FMath::Max(Params.Appearance.PurityForFullColor, 1e-4f);

	const FVector AxisU = Params.AxisU.GetSafeNormal();
	const FVector AxisV = Params.AxisV.GetSafeNormal();

	for (int32 Y = 0; Y < Res; ++Y)
	{
		// 图像第 0 行对应 v = −1（spec §3.1 的像素中心公式）。
		const float VCoord = (float(Y) + 0.5f) / float(Res) * 2.f - 1.f;

		for (int32 X = 0; X < Res; ++X)
		{
			const float UCoord = (float(X) + 0.5f) / float(Res) * 2.f - 1.f;

			const FVector Offset = AxisU * UCoord + AxisV * VCoord;
			const float OffsetLength = float(Offset.Size());
			const FVector Dir = OffsetLength > KINDA_SMALL_NUMBER
				? Offset / OffsetLength
				: AxisU;

			const float PlaneRadiusCm = OffsetLength * SectionRadiusCm;
			const float SurfaceRadiusCm = SurfaceRadiusCmFn(Dir);
			const float ClipRadiusCm = FMath::Min(SectionRadiusCm, SurfaceRadiusCm);

			// 越过 min(剖面半径, 地表半径) 的部分向外线性淡出 1.5px。
			const float OverPx = (PlaneRadiusCm - ClipRadiusCm) / PixelSizeCm;
			const float Alpha = FMath::Clamp(1.f - OverPx / FeatherPixels, 0.f, 1.f);

			FColor& Pixel = OutPixels[X + Y * Res];
			if (Alpha <= 0.f)
			{
				Pixel = FColor(0, 0, 0, 0);
				continue;
			}

			const double DepthVoxels = double(SurfaceRadiusCm - PlaneRadiusCm) / double(VoxelSize);
			const FPlanetGeologySample Sample = SampleFn(Dir, DepthVoxels);

			const float Presence = FMath::Clamp(
				(Sample.DominantNorm - Params.Appearance.ColorThreshold) * PresenceScale, 0.f, 1.f);
			const float PurityMix = FMath::Clamp(Sample.Purity * PurityScale, 0.f, 1.f);
			const float DepthShade = 1.f - 0.4f * FMath::Clamp(
				float(DepthVoxels) * DepthShadeScale, 0.f, 1.f);

			const FLinearColor Base = Params.SurfaceColor * 0.25f * DepthShade;
			const FLinearColor Color =
				Base + (Sample.MineralColor - Base) * (Presence * PurityMix);

			Pixel = Color.ToFColor(true);
			Pixel.A = uint8(FMath::RoundToInt(Alpha * 255.f));
		}
	}
}
```

- [ ] **Step 6: 给实现文件加 BOM**

```bash
cd /f/workspace/ue/StellarEra
f=Source/StellarEraPlanet/Private/PlanetSectionImage.cpp
printf '\xEF\xBB\xBF' > "$f.bom" && cat "$f" >> "$f.bom" && mv "$f.bom" "$f"
head -c 3 "$f" | od -An -tx1
```

- [ ] **Step 7: 构建**

```bash
cd /d/ && "D:/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" \
  "D:/Application/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" \
  StellarEraEditor Win64 Development \
  -Project="F:/workspace/ue/StellarEra/StellarEra.uproject" -WaitMutex -FromMsBuild 2>&1 | tail -30
```

Expected: `Result: Succeeded`（或仅有既存的链接期噪声）。若报 `FPlanetSectionImage` 未定义 → 头文件没进编译（检查 BOM 与文件名）。

- [ ] **Step 8: 跑 Cutaway 全部 8 个测试**

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra.Planet.Cutaway" \
  -TestExit="Automation Test Queue Empty" -unattended -nopause -nullrhi -nosplash -stdout \
  -abslog="F:/workspace/ue/StellarEra/Saved/Logs/CutawayImageTests.log"
grep -E "Test (Completed|Failed)|Result:" "F:/workspace/ue/StellarEra/Saved/Logs/CutawayImageTests.log" | tail -30
```

Expected: 8/8 passed。特别注意 `DepthMapping` 的 `Matched == 1`：若为 0，说明 SectionRadius 没按 1.05×（回到裁定 3）；若 > 1，说明容差过宽。

- [ ] **Step 9: 提交**

```bash
cd /f/workspace/ue/StellarEra
git add Source/StellarEraPlanet/Public/PlanetSectionImage.h \
        Source/StellarEraPlanet/Private/PlanetSectionImage.cpp \
        Source/StellarEraPlanet/Private/PlanetCutawayTests.cpp \
        Source/StellarEraPlanet/StellarEraPlanet.Build.cs
git commit -m "feat(剖面): 剖面圆盘 CPU 光栅化与 RHI 依赖" \
  -m "Co-Authored-By: Claude Code <noreply@anthropic.com>" -- \
  Source/StellarEraPlanet/Public/PlanetSectionImage.h \
  Source/StellarEraPlanet/Private/PlanetSectionImage.cpp \
  Source/StellarEraPlanet/Private/PlanetCutawayTests.cpp \
  Source/StellarEraPlanet/StellarEraPlanet.Build.cs
```

---

### Task 3: 资产（2 个 MPC + `M_PlanetSection` + 行星材质裁剪遮罩）

**Files:**
- Create: `Tools/Materials/CreateCutawayAssets.py`
- Create（脚本产出）: `Content/Materials/MPC_CutawayOrigin.uasset`、`Content/Materials/MPC_CutawayNormal.uasset`、`Content/Materials/M_PlanetSection.uasset`
- Modify（脚本产出）: `Content/Materials/M_PlanetVertexColor.uasset`

**Interfaces:**
- Consumes: 无（脚本独立）
- Produces:
  - `MPC_CutawayOrigin`，唯一 Vector 参数 `CutawayOrigin`，默认 `(0,0,0,0)`
  - `MPC_CutawayNormal`，唯一 Vector 参数 `CutawayNormal`，默认 `(0,0,1,0)`
  - `M_PlanetSection`：Unlit + Masked + TwoSided，参数 `SectionTexture` / `SectionAxisU` / `SectionAxisV` / `SectionHalfExtentCm`
  - `M_PlanetVertexColor` 的 OpacityMask 输入改为 `原输入 × Mask`

- [ ] **Step 1: 写脚本**

Create `Tools/Materials/CreateCutawayAssets.py`:

```python
"""创建 / 修补矿物剖面所需的资产。幂等：重复运行只打 already patched。

用命令头执行（编辑器必须关闭）：
  UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script="<abs path>" \
    -EnablePlugins=PythonScriptPlugin -unattended -nopause -nosplash -stdout -abslog=<log>

设计依据见 docs/superpowers/plans/2026-10-24-mineral-cutaway-editor.md 的裁定 1 / 2。
"""

import unreal

AT = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary

MAT_DIR = "/Game/Materials"
ORIGIN_MPC_PATH = MAT_DIR + "/MPC_CutawayOrigin"
NORMAL_MPC_PATH = MAT_DIR + "/MPC_CutawayNormal"
SECTION_MAT_PATH = MAT_DIR + "/M_PlanetSection"
PLANET_MAT_PATH = MAT_DIR + "/M_PlanetVertexColor"
DEFAULT_TEXTURE_PATH = "/Engine/EngineResources/DefaultTexture"

# 本次创建的节点都带这个 desc 前缀，用于幂等判定与排错。
TAG = "CUTAWAY_"

ERRORS = []


def log(message):
    unreal.log("[Cutaway] " + message)


def fail(message):
    ERRORS.append(message)
    unreal.log_error("[Cutaway] " + message)


def load(path):
    if not EAL.does_asset_exist(path):
        return None
    return EAL.load_asset(path)


def save(asset, label):
    if not asset:
        fail("save: 资产为空 " + label)
        return False
    if not EAL.save_loaded_asset(asset):
        fail("save 失败 " + label)
        return False
    log("saved: " + label)
    return True


# ---------------------------------------------------------------- MPC

def ensure_mpc(path, parameter_name, default_value):
    if EAL.does_asset_exist(path):
        mpc = EAL.load_asset(path)
        params = mpc.get_editor_property("vector_parameters")
        if len(params) != 1 or str(params[0].get_editor_property("parameter_name")) != parameter_name:
            fail("MPC 结构不符预期: %s（应恰有一个 %s）" % (path, parameter_name))
        else:
            log("exists: " + path)
        return mpc

    package_path, asset_name = path.rsplit("/", 1)
    factory = unreal.MaterialParameterCollectionFactoryNew()
    mpc = AT.create_asset(asset_name, package_path, unreal.MaterialParameterCollection, factory)
    if not mpc:
        fail("create_asset 失败 " + path)
        return None

    parameter = unreal.CollectionVectorParameter()
    parameter.set_editor_property("parameter_name", unreal.Name(parameter_name))
    parameter.set_editor_property("default_value", default_value)
    mpc.set_editor_property("vector_parameters", [parameter])
    save(mpc, path)

    params = mpc.get_editor_property("vector_parameters")
    if len(params) != 1:
        fail("MPC 参数数不是 1: " + path)
    return mpc


# ---------------------------------------------------------------- 节点工具

def node(material, expression_class, x, y, tag=None):
    expression = MEL.create_material_expression(material, expression_class, x, y)
    if not expression:
        fail("create_material_expression 失败: %s" % expression_class)
        return None
    if tag:
        expression.set_editor_property("desc", TAG + tag)
    return expression


def link(from_node, from_output, to_node, to_input):
    if not from_node or not to_node:
        fail("link 缺节点: %s -> %s" % (from_output, to_input))
        return False
    if not MEL.connect_material_expressions(from_node, from_output, to_node, to_input):
        fail("connect_material_expressions 失败: %s -> %s" % (from_output, to_input))
        return False
    return True


def const(node_obj, value):
    node_obj.set_editor_property("const_b", value)


# ---------------------------------------------------------------- 剖面材质

def ensure_section_material():
    if EAL.does_asset_exist(SECTION_MAT_PATH):
        log("exists: " + SECTION_MAT_PATH)
        return EAL.load_asset(SECTION_MAT_PATH)

    factory = unreal.MaterialFactoryNew()
    material = AT.create_asset("M_PlanetSection", MAT_DIR, unreal.Material, factory)
    if not material:
        fail("创建 M_PlanetSection 失败")
        return None

    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", True)
    material.set_editor_property("opacity_mask_clip_value", 0.333)

    # Rel = WorldPos − ObjectPos（世界位置相对圆盘中心）。
    world_pos = node(material, unreal.MaterialExpressionWorldPosition, -1400, 0, "WorldPos")
    object_pos = node(material, unreal.MaterialExpressionObjectPosition, -1400, 120, "ObjectPos")
    relative = node(material, unreal.MaterialExpressionSubtract, -1200, 60, "Rel")
    link(world_pos, "XYZ", relative, "A")
    link(object_pos, "", relative, "B")

    axis_u = node(material, unreal.MaterialExpressionVectorParameter, -1200, 240, "AxisU")
    axis_u.set_editor_property("parameter_name", unreal.Name("SectionAxisU"))
    axis_u.set_editor_property("default_value", unreal.LinearColor(1.0, 0.0, 0.0, 1.0))

    axis_v = node(material, unreal.MaterialExpressionVectorParameter, -1200, 360, "AxisV")
    axis_v.set_editor_property("parameter_name", unreal.Name("SectionAxisV"))
    axis_v.set_editor_property("default_value", unreal.LinearColor(0.0, 1.0, 0.0, 1.0))

    half_extent = node(material, unreal.MaterialExpressionScalarParameter, -1200, 480, "HalfExtent")
    half_extent.set_editor_property("parameter_name", unreal.Name("SectionHalfExtentCm"))
    half_extent.set_editor_property("default_value", 100.0)

    def axis_coord(axis_node, y, tag):
        # UV 分量 = dot(Rel, Axis) / R * 0.5 + 0.5
        dot = node(material, unreal.MaterialExpressionDotProduct, -1000, y, tag + "Dot")
        link(relative, "", dot, "A")
        link(axis_node, "", dot, "B")

        divide = node(material, unreal.MaterialExpressionDivide, -820, y, tag + "Div")
        link(dot, "", divide, "A")
        link(half_extent, "", divide, "B")

        scale = node(material, unreal.MaterialExpressionMultiply, -640, y, tag + "Scale")
        link(divide, "", scale, "A")
        const(scale, 0.5)

        bias = node(material, unreal.MaterialExpressionAdd, -460, y, tag + "Bias")
        link(scale, "", bias, "A")
        const(bias, 0.5)
        return bias

    coord_u = axis_coord(axis_u, 240, "CoordU")
    coord_v = axis_coord(axis_v, 380, "CoordV")

    uv = node(material, unreal.MaterialExpressionAppendVector, -280, 300, "UV")
    link(coord_u, "", uv, "A")
    link(coord_v, "", uv, "B")

    texture = node(material, unreal.MaterialExpressionTextureSampleParameter2D, -60, 300, "Texture")
    texture.set_editor_property("parameter_name", unreal.Name("SectionTexture"))
    default_texture = load(DEFAULT_TEXTURE_PATH)
    if default_texture:
        texture.set_editor_property("texture", default_texture)
    else:
        fail("默认纹理缺失: " + DEFAULT_TEXTURE_PATH)
    link(uv, "", texture, "Coordinates")

    if not MEL.connect_material_property(texture, "RGB", unreal.MaterialProperty.MP_EMISSIVE_COLOR):
        fail("连接 Emissive 失败")
    if not MEL.connect_material_property(texture, "A", unreal.MaterialProperty.MP_OPACITY_MASK):
        fail("连接 OpacityMask 失败")

    # RecompileMaterial 返回的是错误字符串数组，不是 bool —— 成功时返回空列表，
    # 写成 `if not ...` 会把成功当失败。
    errors = MEL.recompile_material(material)
    if errors:
        fail("M_PlanetSection 编译失败: %s" % "; ".join(str(e) for e in errors))
    save(material, SECTION_MAT_PATH)
    return material


# ---------------------------------------------------------------- 行星材质裁剪

def find_tagged(material, tag):
    for expression in MEL.get_material_expressions(material):
        try:
            if expression.get_editor_property("desc") == TAG + tag:
                return expression
        except Exception:
            continue
    return None


def patch_planet_material(origin_mpc, normal_mpc):
    material = load(PLANET_MAT_PATH)
    if not material:
        fail("找不到 " + PLANET_MAT_PATH)
        return

    if find_tagged(material, "Mask"):
        log("already patched: " + PLANET_MAT_PATH)
        return

    # --- 集合参数 ---
    origin_node = node(material, unreal.MaterialExpressionCollectionParameter, -1900, -520, "Origin")
    origin_node.set_editor_property("collection", origin_mpc)
    origin_node.set_editor_property("parameter_name", unreal.Name("CutawayOrigin"))

    normal_node = node(material, unreal.MaterialExpressionCollectionParameter, -1900, -320, "Normal")
    normal_node.set_editor_property("collection", normal_mpc)
    normal_node.set_editor_property("parameter_name", unreal.Name("CutawayNormal"))

    origin_xyz = node(material, unreal.MaterialExpressionComponentMask, -1700, -520, "OriginXYZ")
    origin_xyz.set_editor_property("r", True)
    origin_xyz.set_editor_property("g", True)
    origin_xyz.set_editor_property("b", True)
    origin_xyz.set_editor_property("a", False)
    link(origin_node, "", origin_xyz, "")

    origin_w = node(material, unreal.MaterialExpressionComponentMask, -1700, -430, "OriginW")
    origin_w.set_editor_property("r", False)
    origin_w.set_editor_property("g", False)
    origin_w.set_editor_property("b", False)
    origin_w.set_editor_property("a", True)
    link(origin_node, "", origin_w, "")

    normal_xyz = node(material, unreal.MaterialExpressionComponentMask, -1700, -320, "NormalXYZ")
    normal_xyz.set_editor_property("r", True)
    normal_xyz.set_editor_property("g", True)
    normal_xyz.set_editor_property("b", True)
    normal_xyz.set_editor_property("a", False)
    link(normal_node, "", normal_xyz, "")

    normal_w = node(material, unreal.MaterialExpressionComponentMask, -1700, -230, "NormalW")
    normal_w.set_editor_property("r", False)
    normal_w.set_editor_property("g", False)
    normal_w.set_editor_property("b", False)
    normal_w.set_editor_property("a", True)
    link(normal_node, "", normal_w, "")

    # --- Half = saturate(dot(Rel, Normal.xyz) * 10) ---
    world_pos = node(material, unreal.MaterialExpressionWorldPosition, -1900, -660, "WorldPos")
    relative = node(material, unreal.MaterialExpressionSubtract, -1520, -520, "Rel")
    link(world_pos, "XYZ", relative, "A")
    link(origin_xyz, "", relative, "B")

    dot = node(material, unreal.MaterialExpressionDotProduct, -1340, -580, "HalfDot")
    link(relative, "", dot, "A")
    link(normal_xyz, "", dot, "B")

    half_scale = node(material, unreal.MaterialExpressionMultiply, -1160, -580, "HalfScale")
    link(dot, "", half_scale, "A")
    const(half_scale, 10.0)

    half = node(material, unreal.MaterialExpressionSaturate, -980, -580, "Half")
    link(half_scale, "", half, "")

    # --- Inside = saturate((Normal.w − |Rel|) * 0.02) ---
    length = node(material, unreal.MaterialExpressionLength, -1340, -400, "RelLength")
    link(relative, "", length, "")

    inside_delta = node(material, unreal.MaterialExpressionSubtract, -1160, -400, "InsideDelta")
    link(normal_w, "", inside_delta, "A")
    link(length, "", inside_delta, "B")

    inside_scale = node(material, unreal.MaterialExpressionMultiply, -980, -400, "InsideScale")
    link(inside_delta, "", inside_scale, "A")
    const(inside_scale, 0.02)

    inside = node(material, unreal.MaterialExpressionSaturate, -800, -400, "Inside")
    link(inside_scale, "", inside, "")

    # --- Mask = 1 − Half × Inside × Active ---
    clip_product = node(material, unreal.MaterialExpressionMultiply, -620, -500, "ClipProduct")
    link(half, "", clip_product, "A")
    link(inside, "", clip_product, "B")

    active_product = node(material, unreal.MaterialExpressionMultiply, -440, -500, "ActiveProduct")
    link(clip_product, "", active_product, "A")
    link(origin_w, "", active_product, "B")

    mask = node(material, unreal.MaterialExpressionOneMinus, -260, -500, "Mask")
    link(active_product, "", mask, "")

    # --- 与原 OpacityMask 输入相乘 ---
    existing = MEL.get_material_property_input_node(
        material, unreal.MaterialProperty.MP_OPACITY_MASK)
    existing_output = MEL.get_material_property_input_node_output_name(
        material, unreal.MaterialProperty.MP_OPACITY_MASK)
    if not existing:
        fail("M_PlanetVertexColor 的 OpacityMask 没有输入，无法插入裁剪")
        return

    multiply = node(material, unreal.MaterialExpressionMultiply, -80, -500, "MaskedOpacity")
    link(existing, existing_output, multiply, "A")
    link(mask, "", multiply, "B")

    if not MEL.connect_material_property(
            multiply, "", unreal.MaterialProperty.MP_OPACITY_MASK):
        fail("连接行星 OpacityMask 失败")
        return

    errors = MEL.recompile_material(material)
    if errors:
        fail("M_PlanetVertexColor 编译失败: %s" % "; ".join(str(e) for e in errors))
    save(material, PLANET_MAT_PATH)


# ---------------------------------------------------------------- 校验

def verify():
    section = load(SECTION_MAT_PATH)
    if not section:
        fail("verify: M_PlanetSection 不存在")
    else:
        emissive = MEL.get_material_property_input_node(
            section, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        if not emissive:
            fail("verify: M_PlanetSection 的 Emissive 没接")
        else:
            log("verify: M_PlanetSection emissive = %s" % emissive.get_class().get_name())

    planet = load(PLANET_MAT_PATH)
    if not planet:
        fail("verify: M_PlanetVertexColor 不存在")
        return
    opacity = MEL.get_material_property_input_node(
        planet, unreal.MaterialProperty.MP_OPACITY_MASK)
    if not opacity:
        fail("verify: M_PlanetVertexColor 的 OpacityMask 没接")
        return
    class_name = opacity.get_class().get_name()
    log("verify: planet opacity mask = %s" % class_name)
    if class_name != "MaterialExpressionMultiply":
        fail("verify: 行星 OpacityMask 顶点不是 Multiply（patch 没生效）")

    for tag in ("Origin", "Normal", "Mask"):
        if not find_tagged(planet, tag):
            fail("verify: 缺少标记节点 " + TAG + tag)


def main():
    origin_mpc = ensure_mpc(
        ORIGIN_MPC_PATH, "CutawayOrigin", unreal.LinearColor(0.0, 0.0, 0.0, 0.0))
    normal_mpc = ensure_mpc(
        NORMAL_MPC_PATH, "CutawayNormal", unreal.LinearColor(0.0, 0.0, 1.0, 0.0))

    ensure_section_material()

    if origin_mpc and normal_mpc:
        patch_planet_material(origin_mpc, normal_mpc)

    verify()

    if ERRORS:
        print("CUTAWAY_ASSETS_FAILED count=%d" % len(ERRORS))
        raise SystemExit(1)
    print("CUTAWAY_ASSETS_OK")


main()
```

> **注意：** 上面 `mask_xyz` 是一个残留的辅助函数占位，**不要保留**——脚本里已经用不到它（`ComponentMask` 都是就地设置的）。写文件时把它整段删掉。保留它会导致 `expression_material` 未定义（虽然不会被执行），属于计划缺陷。

- [ ] **Step 2: 确认编辑器已关闭**

```bash
tasklist 2>/dev/null | grep -i UnrealEditor || echo "no editor"
```

Expected: `no editor`。（沙箱里 tasklist 可能看不见进程；若不确定，直接看 UBT 是否报 DLL 被占用。）

- [ ] **Step 3: 运行脚本**

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -run=pythonscript \
  -script="F:/workspace/ue/StellarEra/Tools/Materials/CreateCutawayAssets.py" \
  -EnablePlugins=PythonScriptPlugin \
  -unattended -nopause -nosplash -stdout \
  -abslog="F:/workspace/ue/StellarEra/Saved/Logs/CutawayAssets.log"
grep -E "\[Cutaway\]|CUTAWAY_ASSETS" "F:/workspace/ue/StellarEra/Saved/Logs/CutawayAssets.log"
```

Expected（首次）:

```
[Cutaway] saved: /Game/Materials/MPC_CutawayOrigin
[Cutaway] saved: /Game/Materials/MPC_CutawayNormal
[Cutaway] saved: /Game/Materials/M_PlanetSection
[Cutaway] saved: /Game/Materials/M_PlanetVertexColor
[Cutaway] verify: M_PlanetSection emissive = MaterialExpressionTextureSampleParameter2D
[Cutaway] verify: planet opacity mask = MaterialExpressionMultiply
CUTAWAY_ASSETS_OK
```

若出现 `CUTAWAY_ASSETS_FAILED`，逐条读 `[Cutaway]` 错误行再改脚本，**不要**手工在编辑器里补资产（下次运行仍会报不一致）。

- [ ] **Step 4: 再跑一次，验证幂等**

重复 Step 3 的命令，然后：

```bash
grep -E "already patched|exists: /Game/Materials|CUTAWAY_ASSETS" \
  "F:/workspace/ue/StellarEra/Saved/Logs/CutawayAssets.log"
```

Expected: 包含 `already patched: /Game/Materials/M_PlanetVertexColor`、三条 `exists:`，仍以 `CUTAWAY_ASSETS_OK` 结尾。若第二次又 `saved` 了材质，说明 `desc` 标记没写进去——查 `set_editor_property("desc", ...)` 是否被材质编辑器清空。

- [ ] **Step 5: 提交资产**

```bash
cd /f/workspace/ue/StellarEra
git add Content/Materials/MPC_CutawayOrigin.uasset \
        Content/Materials/MPC_CutawayNormal.uasset \
        Content/Materials/M_PlanetSection.uasset \
        Content/Materials/M_PlanetVertexColor.uasset \
        Tools/Materials/CreateCutawayAssets.py
git commit -m "feat(剖面): 剖面 MPC 与圆盘材质，行星材质插入裁剪遮罩" \
  -m "Co-Authored-By: Claude Code <noreply@anthropic.com>" -- \
  Content/Materials/MPC_CutawayOrigin.uasset \
  Content/Materials/MPC_CutawayNormal.uasset \
  Content/Materials/M_PlanetSection.uasset \
  Content/Materials/M_PlanetVertexColor.uasset \
  Tools/Materials/CreateCutawayAssets.py
git status --short -- Content/Materials
```

Expected: 三个新资产 + 一个修改资产；`git status` 不再列出它们。若 `M_PlanetVertexColor` 不在 `Content/Materials/`（早期版本路径不同），先 `git ls-files | grep M_PlanetVertexColor` 确认真实路径，脚本里的 `PLANET_MAT_PATH` 也同步改。

---

### Task 4: 行星侧集成（`GetElementDefinitions` + Actor 管道 + `UPlanetCutawayComponent`）

**Files:**
- Modify: `Source/StellarEraPlanet/Public/PlanetGeologyField.h`、`Source/StellarEraPlanet/Private/PlanetGeologyField.cpp`
- Modify: `Source/StellarEraPlanet/Public/VoxelPlanetActor.h`、`Source/StellarEraPlanet/Private/VoxelPlanetActor.cpp`
- Create: `Source/StellarEraPlanet/Public/PlanetCutawayComponent.h`
- Create: `Source/StellarEraPlanet/Private/PlanetCutawayComponent.cpp`

**Interfaces:**
- Consumes: `FPlanetCutawayMath`、`FPlanetSectionImage`、`FPlanetGeologyField::Sample/GetAppearance`、`AVoxelPlanetActor::GetSurfaceRadiusCm/GetSurfaceRadiusCmAtDirection/SurfaceColor/VoxelSize/GetAtmosphereComponent`
- Produces:
  - `TArray<FPlanetGeologyElement> FPlanetGeologyField::GetElementDefinitions() const;`
  - `TSharedPtr<const FPlanetGeologyField> AVoxelPlanetActor::GetGeologyField();`
  - `UPlanetCutawayComponent* AVoxelPlanetActor::GetCutawayComponent() const;`
  - `UPlanetCutawayComponent::Activate(AVoxelPlanetActor*, const FVector&) -> bool`、`Deactivate()`、`IsActive()`、`RotatePlane(ESectionRing, float)`、`RequestSectionRender(int32)`、`GetRingWorldPoints(TArray<FSectionRingWorldPolyline>&) const`、`GetCutawayPlanet()`、`GetGeologyField()`、公开 `FinalResolution` / `PreviewResolution`

- [ ] **Step 1: 给 `FPlanetGeologyField` 加元素定义导出**

在 `Source/StellarEraPlanet/Public/PlanetGeologyField.h` 的 `float GetTotalAbundance() const { return TotalAbundance; }` 下一行插入：

```cpp
	/** 元素定义的副本（元素表顺序），给矿物剖面图例用。 */
	TArray<FPlanetGeologyElement> GetElementDefinitions() const;
```

在 `Source/StellarEraPlanet/Private/PlanetGeologyField.cpp` 末尾追加：

```cpp
TArray<FPlanetGeologyElement> FPlanetGeologyField::GetElementDefinitions() const
{
	TArray<FPlanetGeologyElement> Out;
	Out.Reserve(Elements.Num());
	for (const FElementRuntime& Runtime : Elements)
	{
		Out.Add(Runtime.Definition);
	}
	return Out;
}
```

- [ ] **Step 2: 给 `AVoxelPlanetActor` 加组件与地质场访问**

在 `Source/StellarEraPlanet/Public/VoxelPlanetActor.h`：

1）前向声明区（`class UVoxelSimpleInvokerComponent;` 之后）加：

```cpp
class FPlanetGeologyField;
class UPlanetCutawayComponent;
```

2）在 `GetSurfaceRadiusCmAtDirection` 声明之后加：

```cpp
	/** 行星的地质场。生成器还没建好时为 nullptr。 */
	TSharedPtr<const FPlanetGeologyField> GetGeologyField();
```

3）在 `EditGrid` 的 `UPROPERTY` 之后（`SaveDebounceSeconds` 之前）加：

```cpp
	/** 矿物剖面视图组件。构造时自动创建；未激活时不 Tick、不建任何资产。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Planet|Cutaway")
	TObjectPtr<UPlanetCutawayComponent> CutawayComponent;

	UFUNCTION(BlueprintPure, Category = "Planet|Cutaway")
	UPlanetCutawayComponent* GetCutawayComponent() const { return CutawayComponent; }
```

在 `Source/StellarEraPlanet/Private/VoxelPlanetActor.cpp`：

1）include 区加 `#include "PlanetCutawayComponent.h"`。

2）构造函数里，`AtmosphereComponent` 创建语句之后、`EditGrid` 创建之前加：

```cpp
	// 剖面组件默认不 Tick，第一个 Activate 才创建圆盘/圆环并加载资产。
	CutawayComponent = CreateDefaultSubobject<UPlanetCutawayComponent>(TEXT("PlanetCutaway"));
	CutawayComponent->SetupAttachment(RootComponent);
```

3）在 `GetSurfaceRadiusCmAtDirection` 的实现之后加：

```cpp
TSharedPtr<const FPlanetGeologyField> AVoxelPlanetActor::GetGeologyField()
{
	return PlanetGenerator ? PlanetGenerator->GetGeology() : nullptr;
}
```

> 若 `UVoxelPlanetGenerator::GetGeology` 的返回类型不是 `TSharedPtr<const FPlanetGeologyField>`，以实际签名为准（`grep -n "GetGeology" Source/StellarEraPlanet/Public/VoxelPlanetGenerator.h`），只改这一行的转换。

- [ ] **Step 3: 写组件头文件**

Create `Source/StellarEraPlanet/Public/PlanetCutawayComponent.h`:

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.
// 行星矿物剖面：切面状态、剖面纹理、圆盘、旋转环、全局裁剪参数。

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "PlanetCutawayMath.h"
#include "PlanetGeologyField.h"
#include "PlanetCutawayComponent.generated.h"

class AVoxelPlanetActor;
class ULineBatchComponent;
class UMaterialInstanceDynamic;
class UMaterialParameterCollection;
class UStaticMesh;
class UStaticMeshComponent;
class UTexture2D;

/**
 * 一颗行星的剖面视图。观察性质：不改体素、不碰碰撞、不写存档。
 *
 * 生命周期：Activate 时加载资产并创建圆盘/圆环，Deactivate 时把全局 MPC 的
 * Active 写 0、隐藏子组件、恢复大气；EndPlay 兜底 Deactivate 并销毁子对象。
 *
 * 每帧 Tick 做三件事：按当前相机重算 ClipNormal、同步圆盘与圆环、限频重渲染预览。
 */
UCLASS(ClassGroup = (StellarEra), meta = (BlueprintSpawnableComponent))
class STELLARERAPLANET_API UPlanetCutawayComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UPlanetCutawayComponent();

	//~ Begin UActorComponent Interface
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
	//~ End UActorComponent Interface

	/** 进入剖面。法线是「平面法线」，组件会用相机位置把它翻到相机侧。 */
	bool Activate(AVoxelPlanetActor* InPlanet, const FVector& InitialPlaneNormal);

	/** 退出剖面并恢复全部视觉。重复调用安全。 */
	void Deactivate();

	bool IsActive() const { return bActive; }

	/** 绕世界轴旋转切面；DeltaDegrees 为正 = 右手方向。 */
	void RotatePlane(ESectionRing Ring, float DeltaDegrees);

	/** 立即渲染一帧剖面（分辨率取 FinalResolution 或 PreviewResolution 中较大者）。 */
	void RequestSectionRender(int32 Resolution);

	/** 输出三根圆环的世界折线（各 128 点）；未激活时输出空数组。 */
	void GetRingWorldPoints(TArray<FSectionRingWorldPolyline>& OutRings) const;

	AVoxelPlanetActor* GetCutawayPlanet() const { return Planet.Get(); }
	TSharedPtr<const FPlanetGeologyField> GetGeologyField() const { return CachedGeology; }

	UPROPERTY(EditAnywhere, Category = "Planet|Cutaway", meta = (ClampMin = "16"))
	int32 FinalResolution = 512;

	UPROPERTY(EditAnywhere, Category = "Planet|Cutaway", meta = (ClampMin = "16"))
	int32 PreviewResolution = 96;

	/** 圆环半径 / 剖面半径。 */
	UPROPERTY(EditAnywhere, Category = "Planet|Cutaway")
	float RingRadiusScale = 1.15f;

	/** 圆盘沿 ClipNormal 向相机侧的偏移（厘米），避免与体素表面共面闪烁。 */
	UPROPERTY(EditAnywhere, Category = "Planet|Cutaway")
	float DiscOffsetCm = 2.f;

	/** 剖面半径 / 基准表面半径（spec §3.1）。 */
	static constexpr float SectionRadiusScale = 1.05f;

private:
	bool EnsureViewAssets();
	void ReleaseViewAssets();

	/** 把球心/ClipNormal/Active 与内球半径写进两个 MPC。 */
	void ApplyClipMask();
	void UpdateDiscTransform();
	void UpdateRingGeometry();
	void RenderSection(int32 Resolution);
	void UploadSection(UTexture2D* Target, TArray<FColor>& Buffer, int32 Resolution);

	float ComputeSectionRadiusCm() const;
	FVector GetCameraLocationOrDefault() const;

	bool bActive = false;
	TWeakObjectPtr<AVoxelPlanetActor> Planet;
	TSharedPtr<const FPlanetGeologyField> CachedGeology;

	FQuat PlaneOrientation = FQuat::Identity;
	FVector ClipNormal = FVector::UpVector;
	FVector AxisU = FVector::ForwardVector;
	FVector AxisV = FVector::RightVector;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> SectionMesh;

	UPROPERTY(Transient)
	TObjectPtr<ULineBatchComponent> SectionRings;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SectionMaterialInstance;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> FinalTexture;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> PreviewTexture;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialParameterCollection> CutawayOriginMpc;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialParameterCollection> CutawayNormalMpc;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> SectionPlaneMesh;

	/** UpdateTextureRegions 是异步的：像素缓冲必须活到渲染线程读完。 */
	TArray<FColor> FinalPixels;
	TArray<FColor> PreviewPixels;

	float SecondsSincePreviewRender = 0.f;
	float PendingRotationDegrees = 0.f;

	bool bSavedAtmosphereEnabled = true;
	bool bSavedAtmosphereVisible = true;

	static constexpr int32 RingSegments = 128;
};
```

- [ ] **Step 4: 给头文件加 BOM**

```bash
cd /f/workspace/ue/StellarEra
f=Source/StellarEraPlanet/Public/PlanetCutawayComponent.h
printf '\xEF\xBB\xBF' > "$f.bom" && cat "$f" >> "$f.bom" && mv "$f.bom" "$f"
head -c 3 "$f" | od -An -tx1
```

- [ ] **Step 5: 写组件实现**

Create `Source/StellarEraPlanet/Private/PlanetCutawayComponent.cpp`:

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetCutawayComponent.h"

#include "PlanetAtmosphereComponent.h"
#include "PlanetSectionImage.h"
#include "VoxelPlanetActor.h"

#include "Components/LineBatchComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "GameFramework/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "RHITypes.h"

namespace
{
	const TCHAR* SectionMaterialPath = TEXT("/Game/Materials/M_PlanetSection.M_PlanetSection");
	const TCHAR* CutawayOriginMpcPath = TEXT("/Game/Materials/MPC_CutawayOrigin.MPC_CutawayOrigin");
	const TCHAR* CutawayNormalMpcPath = TEXT("/Game/Materials/MPC_CutawayNormal.MPC_CutawayNormal");
	const TCHAR* SectionPlaneMeshPath = TEXT("/Engine/BasicShapes/Plane.Plane");

	const TCHAR* SectionTextureParameter = TEXT("SectionTexture");
	const TCHAR* SectionAxisUParameter = TEXT("SectionAxisU");
	const TCHAR* SectionAxisVParameter = TEXT("SectionAxisV");
	const TCHAR* SectionHalfExtentParameter = TEXT("SectionHalfExtentCm");
	const TCHAR* CutawayOriginParameter = TEXT("CutawayOrigin");
	const TCHAR* CutawayNormalParameter = TEXT("CutawayNormal");

	// 预览限频：转过至少 0.75° 且距上次 ≥ 33ms 才重渲染。
	constexpr float PreviewRenderIntervalSeconds = 0.033f;
	constexpr float PreviewRenderStepDegrees = 0.75f;

	const FLinearColor RingColors[3] = {
		FLinearColor(1.f, 0.15f, 0.15f, 1.f),
		FLinearColor(0.2f, 1.f, 0.2f, 1.f),
		FLinearColor(0.2f, 0.45f, 1.f, 1.f)
	};
}

UPlanetCutawayComponent::UPlanetCutawayComponent()
{
	// 只在激活期间 Tick，且排在后更新阶段（此时相机位置本帧已定）。
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

bool UPlanetCutawayComponent::Activate(
	AVoxelPlanetActor* InPlanet,
	const FVector& InitialPlaneNormal)
{
	if (bActive)
	{
		Deactivate();
	}

	if (!InPlanet)
	{
		return false;
	}

	const FVector Normal = InitialPlaneNormal.GetSafeNormal();
	if (Normal.IsNearlyZero())
	{
		return false;
	}

	TSharedPtr<const FPlanetGeologyField> Geology = InPlanet->GetGeologyField();
	if (!Geology.IsValid() || Geology->IsEmpty())
	{
		return false;
	}

	// 资产缺失时在 EnsureViewAssets 里报错返回：调用方据此提示「该行星没有矿物分布」。
	Planet = InPlanet;
	CachedGeology = Geology;
	if (!EnsureViewAssets())
	{
		Planet.Reset();
		CachedGeology.Reset();
		return false;
	}

	PlaneOrientation = FRotationMatrix::MakeFromZ(Normal).ToQuat();
	ClipNormal = FPlanetCutawayMath::ComputeClipNormal(
		Normal, GetCameraLocationOrDefault(), InPlanet->GetActorLocation());
	FPlanetCutawayMath::ComputePlaneBasis(Normal, AxisU, AxisV);
	PendingRotationDegrees = 0.f;
	SecondsSincePreviewRender = PreviewRenderIntervalSeconds;

	if (UPlanetAtmosphereComponent* Atmosphere = InPlanet->GetAtmosphereComponent())
	{
		bSavedAtmosphereEnabled = Atmosphere->bAtmosphereEnabled;
		bSavedAtmosphereVisible = Atmosphere->IsVisible();
		Atmosphere->bAtmosphereEnabled = false;
		Atmosphere->SetVisibility(false);
	}

	bActive = true;
	ApplyClipMask();
	RenderSection(FinalResolution);
	UpdateDiscTransform();
	UpdateRingGeometry();

	if (SectionMesh)
	{
		SectionMesh->SetVisibility(true);
	}
	if (SectionRings)
	{
		SectionRings->SetVisibility(true);
	}

	SetComponentTickEnabled(true);
	return true;
}

void UPlanetCutawayComponent::Deactivate()
{
	if (!bActive)
	{
		return;
	}
	bActive = false;
	SetComponentTickEnabled(false);

	if (AVoxelPlanetActor* RestorePlanet = Planet.Get())
	{
		if (UPlanetAtmosphereComponent* Atmosphere = RestorePlanet->GetAtmosphereComponent())
		{
			Atmosphere->bAtmosphereEnabled = bSavedAtmosphereEnabled;
			Atmosphere->SetVisibility(bSavedAtmosphereVisible);
			if (bSavedAtmosphereEnabled)
			{
				Atmosphere->RefreshAtmosphereGeometry();
			}
		}
	}

	if (SectionMesh)
	{
		SectionMesh->SetVisibility(false);
	}
	if (SectionRings)
	{
		SectionRings->Flush();
		SectionRings->SetVisibility(false);
	}

	// 退出即恢复的另一半：Active = 0 让全星球的 OpacityMask 立即回到 1。
	if (CutawayOriginMpc)
	{
		UKismetMaterialLibrary::SetVectorParameterValue(
			this, CutawayOriginMpc, CutawayOriginParameter,
			FLinearColor(0.f, 0.f, 0.f, 0.f));
	}

	Planet.Reset();
	CachedGeology.Reset();
}

void UPlanetCutawayComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Deactivate();
	ReleaseViewAssets();
	Super::EndPlay(EndPlayReason);
}

void UPlanetCutawayComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	AVoxelPlanetActor* CutawayPlanet = Planet.Get();
	if (!bActive || !CutawayPlanet)
	{
		return;
	}

	const FVector Center = CutawayPlanet->GetActorLocation();
	const FVector PlaneNormal = PlaneOrientation.GetAxisZ();

	// 相机转过去时自动换边：看得见的永远是远侧。
	ClipNormal = FPlanetCutawayMath::ComputeClipNormal(
		PlaneNormal, GetCameraLocationOrDefault(), Center);
	FPlanetCutawayMath::ComputePlaneBasis(PlaneNormal, AxisU, AxisV);

	ApplyClipMask();
	UpdateDiscTransform();
	UpdateRingGeometry();

	SecondsSincePreviewRender += DeltaTime;
	if (PendingRotationDegrees >= PreviewRenderStepDegrees
		&& SecondsSincePreviewRender >= PreviewRenderIntervalSeconds)
	{
		RenderSection(PreviewResolution);
	}
}

void UPlanetCutawayComponent::RotatePlane(ESectionRing Ring, float DeltaDegrees)
{
	if (!bActive || DeltaDegrees == 0.f)
	{
		return;
	}

	FVector Axis = FVector::UpVector;
	switch (Ring)
	{
	case ESectionRing::X: Axis = FVector::ForwardVector; break;
	case ESectionRing::Y: Axis = FVector::RightVector; break;
	default:              Axis = FVector::UpVector; break;
	}

	PlaneOrientation = FPlanetCutawayMath::RotatePlane(PlaneOrientation, Axis, DeltaDegrees);
	PendingRotationDegrees += FMath::Abs(DeltaDegrees);

	// 盘与环立刻跟随，纹理在 Tick 里限频重渲染。
	FPlanetCutawayMath::ComputePlaneBasis(PlaneOrientation.GetAxisZ(), AxisU, AxisV);
	UpdateDiscTransform();
	UpdateRingGeometry();
}

void UPlanetCutawayComponent::RequestSectionRender(int32 Resolution)
{
	if (!bActive)
	{
		return;
	}
	RenderSection(Resolution >= FinalResolution ? FinalResolution : PreviewResolution);
}

void UPlanetCutawayComponent::GetRingWorldPoints(
	TArray<FSectionRingWorldPolyline>& OutRings) const
{
	OutRings.Reset();
	if (!bActive || !Planet.IsValid())
	{
		return;
	}

	const FVector Center = Planet->GetActorLocation();
	const float RadiusCm = ComputeSectionRadiusCm() * RingRadiusScale;

	OutRings.Reserve(3);
	for (int32 RingIndex = 0; RingIndex < 3; ++RingIndex)
	{
		FSectionRingWorldPolyline& Polyline = OutRings.AddDefaulted_GetRef();
		Polyline.Ring = static_cast<ESectionRing>(RingIndex);
		Polyline.Points.Reserve(RingSegments);
		for (int32 PointIndex = 0; PointIndex < RingSegments; ++PointIndex)
		{
			const float Angle = 2.f * PI * float(PointIndex) / float(RingSegments);
			Polyline.Points.Add(
				Center + FPlanetCutawayMath::RingPoint(Polyline.Ring, RadiusCm, Angle));
		}
	}
}

bool UPlanetCutawayComponent::EnsureViewAssets()
{
	if (SectionMesh && SectionRings && SectionMaterialInstance)
	{
		return true;
	}

	UMaterialInterface* SectionMaterial =
		LoadObject<UMaterialInterface>(nullptr, SectionMaterialPath);
	CutawayOriginMpc =
		LoadObject<UMaterialParameterCollection>(nullptr, CutawayOriginMpcPath);
	CutawayNormalMpc =
		LoadObject<UMaterialParameterCollection>(nullptr, CutawayNormalMpcPath);
	SectionPlaneMesh = LoadObject<UStaticMesh>(nullptr, SectionPlaneMeshPath);

	if (!SectionMaterial || !CutawayOriginMpc || !CutawayNormalMpc || !SectionPlaneMesh)
	{
		UE_LOG(LogTemp, Error,
			TEXT("UPlanetCutawayComponent: 剖面资产缺失"
				 "（M_PlanetSection / MPC_CutawayOrigin / MPC_CutawayNormal / Plane）"));
		return false;
	}

	FinalTexture = UTexture2D::CreateTransient(FinalResolution, FinalResolution, PF_B8G8R8A8);
	PreviewTexture = UTexture2D::CreateTransient(
		PreviewResolution, PreviewResolution, PF_B8G8R8A8);
	if (!FinalTexture || !PreviewTexture)
	{
		UE_LOG(LogTemp, Error, TEXT("UPlanetCutawayComponent: CreateTransient 失败"));
		ReleaseViewAssets();
		return false;
	}

	for (UTexture2D* Texture : { FinalTexture.Get(), PreviewTexture.Get() })
	{
		Texture->SRGB = true;
		Texture->Filter = TF_Bilinear;
		Texture->AddressX = TA_Clamp;
		Texture->AddressY = TA_Clamp;
		Texture->NeverStream = true;
		Texture->UpdateResource();
	}

	SectionMaterialInstance = UMaterialInstanceDynamic::Create(SectionMaterial, this);
	if (!SectionMaterialInstance)
	{
		ReleaseViewAssets();
		return false;
	}
	SectionMaterialInstance->SetTextureParameterValue(SectionTextureParameter, FinalTexture);

	SectionMesh = NewObject<UStaticMeshComponent>(this, TEXT("PlanetSectionMesh"), RF_Transient);
	SectionMesh->SetupAttachment(this);
	SectionMesh->SetStaticMesh(SectionPlaneMesh);
	SectionMesh->SetMobility(EComponentMobility::Movable);
	SectionMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SectionMesh->SetCastShadow(false);
	SectionMesh->bReceivesDecals = false;
	SectionMesh->RegisterComponent();
	SectionMesh->SetMaterial(0, SectionMaterialInstance);
	SectionMesh->SetVisibility(false);

	SectionRings = NewObject<ULineBatchComponent>(this, TEXT("PlanetSectionRings"), RF_Transient);
	SectionRings->SetupAttachment(this);
	SectionRings->SetMobility(EComponentMobility::Movable);
	SectionRings->RegisterComponent();
	SectionRings->SetVisibility(false);

	return true;
}

void UPlanetCutawayComponent::ReleaseViewAssets()
{
	if (SectionRings)
	{
		SectionRings->Flush();
		SectionRings->DestroyComponent();
		SectionRings = nullptr;
	}
	if (SectionMesh)
	{
		SectionMesh->DestroyComponent();
		SectionMesh = nullptr;
	}

	SectionMaterialInstance = nullptr;
	FinalTexture = nullptr;
	PreviewTexture = nullptr;
	SectionPlaneMesh = nullptr;
	CutawayOriginMpc = nullptr;
	CutawayNormalMpc = nullptr;
	FinalPixels.Reset();
	PreviewPixels.Reset();
}

void UPlanetCutawayComponent::ApplyClipMask()
{
	AVoxelPlanetActor* CutawayPlanet = Planet.Get();
	if (!CutawayOriginMpc || !CutawayNormalMpc || !CutawayPlanet)
	{
		return;
	}

	const FVector Center = CutawayPlanet->GetActorLocation();
	const float InsideRadiusCm =
		SectionRadiusScale * CutawayPlanet->GetSurfaceRadiusCm();

	UKismetMaterialLibrary::SetVectorParameterValue(
		this, CutawayOriginMpc, CutawayOriginParameter,
		FLinearColor(Center.X, Center.Y, Center.Z, bActive ? 1.f : 0.f));

	UKismetMaterialLibrary::SetVectorParameterValue(
		this, CutawayNormalMpc, CutawayNormalParameter,
		FLinearColor(ClipNormal.X, ClipNormal.Y, ClipNormal.Z, InsideRadiusCm));
}

void UPlanetCutawayComponent::UpdateDiscTransform()
{
	AVoxelPlanetActor* CutawayPlanet = Planet.Get();
	if (!SectionMesh || !CutawayPlanet)
	{
		return;
	}

	const FVector Center = CutawayPlanet->GetActorLocation();
	const FVector PlaneNormal = PlaneOrientation.GetAxisZ();
	const float SectionRadiusCm = ComputeSectionRadiusCm();

	// 基与朝向都用未翻转的 PlaneNormal：局部 X/Y = AxisU/AxisV，正面朝 +PlaneNormal。
	// 只有偏移用 ClipNormal。相机绕到背面时图案左右镜像，这是平面画从背面看的正确外观（裁定 2）。
	SectionMesh->SetWorldLocation(Center + ClipNormal * DiscOffsetCm);
	SectionMesh->SetWorldRotation(
		FRotationMatrix::MakeFromXZ(AxisU, PlaneNormal).Rotator());

	// 内置 Plane 是 100×100 cm，等比缩放到 2R 边长。
	const float Scale = 2.f * SectionRadiusCm / 100.f;
	SectionMesh->SetWorldScale3D(FVector(Scale, Scale, 1.f));

	if (SectionMaterialInstance)
	{
		SectionMaterialInstance->SetVectorParameterValue(
			SectionAxisUParameter, FLinearColor(AxisU.X, AxisU.Y, AxisU.Z, 0.f));
		SectionMaterialInstance->SetVectorParameterValue(
			SectionAxisVParameter, FLinearColor(AxisV.X, AxisV.Y, AxisV.Z, 0.f));
		SectionMaterialInstance->SetScalarParameterValue(
			SectionHalfExtentParameter, SectionRadiusCm);
	}
}

void UPlanetCutawayComponent::UpdateRingGeometry()
{
	AVoxelPlanetActor* CutawayPlanet = Planet.Get();
	if (!SectionRings || !CutawayPlanet)
	{
		return;
	}

	const FVector Center = CutawayPlanet->GetActorLocation();
	const float RadiusCm = ComputeSectionRadiusCm() * RingRadiusScale;

	TArray<FBatchedLine> Lines;
	Lines.Reserve(3 * RingSegments);

	for (int32 RingIndex = 0; RingIndex < 3; ++RingIndex)
	{
		const ESectionRing Ring = static_cast<ESectionRing>(RingIndex);
		for (int32 Segment = 0; Segment < RingSegments; ++Segment)
		{
			const float Angle0 = 2.f * PI * float(Segment) / float(RingSegments);
			const float Angle1 = 2.f * PI * float(Segment + 1) / float(RingSegments);
			Lines.Emplace(
				Center + FPlanetCutawayMath::RingPoint(Ring, RadiusCm, Angle0),
				Center + FPlanetCutawayMath::RingPoint(Ring, RadiusCm, Angle1),
				RingColors[RingIndex], 0.f, 3.f, SDPG_Foreground);
		}
	}

	SectionRings->Flush();
	SectionRings->DrawLines(Lines);
}

void UPlanetCutawayComponent::RenderSection(int32 Resolution)
{
	AVoxelPlanetActor* CutawayPlanet = Planet.Get();
	if (!bActive || !CutawayPlanet || !CachedGeology.IsValid() || !SectionMaterialInstance)
	{
		return;
	}

	const bool bFinal = Resolution >= FinalResolution;
	const int32 Res = bFinal ? FinalResolution : FMath::Max(PreviewResolution, 1);
	UTexture2D* Target = bFinal ? FinalTexture.Get() : PreviewTexture.Get();
	TArray<FColor>& Buffer = bFinal ? FinalPixels : PreviewPixels;
	if (!Target)
	{
		return;
	}

	const float SurfaceRadiusCm = CutawayPlanet->GetSurfaceRadiusCm();

	FPlanetSectionImageParams Params;
	Params.Resolution = Res;
	Params.AxisU = AxisU;
	Params.AxisV = AxisV;
	Params.PlaneNormal = PlaneOrientation.GetAxisZ();
	Params.SectionRadiusCm = ComputeSectionRadiusCm();
	Params.VoxelSize = FMath::Max(CutawayPlanet->VoxelSize, 1e-3f);
	Params.BaseRadiusVoxels = SurfaceRadiusCm / Params.VoxelSize;
	Params.SurfaceColor = CutawayPlanet->SurfaceColor;
	Params.Appearance = CachedGeology->GetAppearance();

	// 时间戳留在这里是有意的：每像素一次地表半径查询 + 一次地质采样，512² 是
	// 唯一可能卡顿的一帧（见 spec §9 的风险）。这行日志就是 Task 7 的量测证据。
	const double RenderStartSeconds = FPlatformTime::Seconds();

	FPlanetSectionImage::Render(Params,
		[CutawayPlanet](const FVector& UnitDir)
		{
			return CutawayPlanet->GetSurfaceRadiusCmAtDirection(UnitDir);
		},
		[this](const FVector& UnitDir, double DepthVoxels)
		{
			return CachedGeology->Sample(UnitDir, DepthVoxels);
		},
		Buffer);

	UploadSection(Target, Buffer, Res);
	SectionMaterialInstance->SetTextureParameterValue(SectionTextureParameter, Target);

	const double RenderMs = (FPlatformTime::Seconds() - RenderStartSeconds) * 1000.0;
	UE_LOG(LogTemp, Log, TEXT("Cutaway: 剖面 %dx%d 渲染+上传 %.1f ms"), Res, Res, RenderMs);

	PendingRotationDegrees = 0.f;
	SecondsSincePreviewRender = 0.f;
}

void UPlanetCutawayComponent::UploadSection(
	UTexture2D* Target,
	TArray<FColor>& Buffer,
	int32 Resolution)
{
	if (!Target || Resolution <= 0 || Buffer.Num() < Resolution * Resolution)
	{
		return;
	}

	FUpdateTextureRegion2D Region(
		0, 0, 0, 0, uint32(Resolution), uint32(Resolution));
	Target->UpdateTextureRegions(
		0, 1, &Region,
		uint32(Resolution * sizeof(FColor)),
		uint32(sizeof(FColor)),
		reinterpret_cast<uint8*>(Buffer.GetData()));
}

float UPlanetCutawayComponent::ComputeSectionRadiusCm() const
{
	const AVoxelPlanetActor* CutawayPlanet = Planet.Get();
	return CutawayPlanet
		? SectionRadiusScale * CutawayPlanet->GetSurfaceRadiusCm()
		: 0.f;
}

FVector UPlanetCutawayComponent::GetCameraLocationOrDefault() const
{
	if (const UWorld* World = GetWorld())
	{
		if (const APlayerController* PlayerController = World->GetFirstPlayerController())
		{
			if (const APlayerCameraManager* CameraManager = PlayerController->PlayerCameraManager)
			{
				return CameraManager->GetCameraLocation();
			}
		}
	}

	return Planet.IsValid()
		? Planet->GetActorLocation() + FVector::UpVector * Planet->GetSurfaceRadiusCm()
		: FVector::ZeroVector;
}
```

- [ ] **Step 6: 给实现文件加 BOM**

```bash
cd /f/workspace/ue/StellarEra
f=Source/StellarEraPlanet/Private/PlanetCutawayComponent.cpp
printf '\xEF\xBB\xBF' > "$f.bom" && cat "$f" >> "$f.bom" && mv "$f.bom" "$f"
head -c 3 "$f" | od -An -tx1
```

- [ ] **Step 7: 构建**

```bash
cd /d/ && "D:/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" \
  "D:/Application/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" \
  StellarEraEditor Win64 Development \
  -Project="F:/workspace/ue/StellarEra/StellarEra.uproject" -WaitMutex -FromMsBuild 2>&1 | tail -40
```

Expected: `Result: Succeeded`。

**常见编译错误与对策（不要瞎改）：**

| 报错 | 原因 | 处理 |
|---|---|---|
| `FUpdateTextureRegion2D` 未定义 | `Build.cs` 没加 `"RHI"` | 回 Task 2 Step 1 |
| `GetCameraLocationOrDefault` 未声明 | 头文件漏了私有声明 | 补声明 |
| `bAtmosphereEnabled` 不是 `UPlanetAtmosphereComponent` 成员 | 成员名不同 | `grep -n "bAtmosphereEnabled" Source/StellarEraPlanet/Public/PlanetAtmosphereComponent.h`，按实际名改 |
| `DrawLines` 参数不匹配 | 传了 `const TArray&` 给非 const 视图 | 确认 `TArrayView<FBatchedLine>`；`Lines` 必须非 const |
| `FBatchedLine` 构造参数不足 | `DepthPriority` 没有默认值 | 必须传 `SDPG_Foreground` |

- [ ] **Step 8: 提交**

```bash
cd /f/workspace/ue/StellarEra
git add Source/StellarEraPlanet/Public/PlanetCutawayComponent.h \
        Source/StellarEraPlanet/Private/PlanetCutawayComponent.cpp \
        Source/StellarEraPlanet/Public/PlanetGeologyField.h \
        Source/StellarEraPlanet/Private/PlanetGeologyField.cpp \
        Source/StellarEraPlanet/Public/VoxelPlanetActor.h \
        Source/StellarEraPlanet/Private/VoxelPlanetActor.cpp
git commit -m "feat(剖面): 剖面组件与行星挂载（纹理/圆盘/圆环/裁剪 MPC）" \
  -m "Co-Authored-By: Claude Code <noreply@anthropic.com>" -- \
  Source/StellarEraPlanet/Public/PlanetCutawayComponent.h \
  Source/StellarEraPlanet/Private/PlanetCutawayComponent.cpp \
  Source/StellarEraPlanet/Public/PlanetGeologyField.h \
  Source/StellarEraPlanet/Private/PlanetGeologyField.cpp \
  Source/StellarEraPlanet/Public/VoxelPlanetActor.h \
  Source/StellarEraPlanet/Private/VoxelPlanetActor.cpp
```

---

### Task 5: 星图侧交互（H 开关、圆环拾取、拖拽旋转、相机让位）

**Files:**
- Modify: `Source/StellarEra/Public/StarMapCameraPawn.h`、`Source/StellarEra/Private/StarMapCameraPawn.cpp`
- Modify: `Source/StellarEra/Public/StellarEraPlayerController.h`、`Source/StellarEra/Private/StellarEraPlayerController.cpp`
- Modify: `Source/StellarEra/StellarEra.Build.cs`（`StellarEraPlanet` 私有 → 公开）

**Interfaces:**
- Consumes:
  - `FPlanetCutawayMath::PickSectionRing(TArrayView<const FSectionRingScreenPolyline>, const FVector2D&, float, ESectionRing&)`（Task 1）
  - `AStarMapCameraPawn::FocusOn(AActor*, double)`、`AddZoom`（既有）
  - `UCosmosSimulationSubsystem::GetSelectedBody()`
  - `AVoxelPlanetActor::GetCutawayComponent()`（Task 4）
  - `UPlanetCutawayComponent::Activate/Deactivate/RotatePlane/RequestSectionRender/GetRingWorldPoints/GetCutawayPlanet/FinalResolution`（Task 4）
- Produces:
  - `void AStarMapCameraPawn::OrientToDirection(const FVector& Direction);`
  - `UPlanetCutawayComponent* AStellarEraPlayerController::GetActiveCutaway() const;`
  - `void AStellarEraPlayerController::ExitCutawayIfActive();`

> **输入共存的事实（本会话已核，不要再改机制）：**
> 1. `bConsumeInput` 是 **`UInputAction` 的成员**（`Plugins/EnhancedInput/Source/EnhancedInput/Public/InputAction.h:96`），**不是** `FEnhancedActionKeyMapping` 的成员。计划里任何 `Mapping.bConsumeInput = ...` 都是错的。
> 2. 同一个 IMC 内，多个动作映射到同一个键**互不吞输入**：`AppliedKeys` 在 context 循环**之外**声明（`EnhancedInputSubsystemInterface.cpp:949`），循环内的 `ContextAppliedKeys`（:967）只在整段 context 处理完后才 `AppliedKeys.Append`（:1091），而屏蔽判断 `!AppliedKeys.Contains(Mapping.Key)`（:1041）因此只挡**更高优先级 context**。本仓库已有同形用例：`IA_Move` 与 `IA_Thrust` 共用 `EKeys::W`（`StellarEraPlayerController.cpp:251,264`）。
> 3. 所以 `IA_CutawayDrag` 直接与 `IA_Look` 共用 `EKeys::Mouse2D`，不需要任何消费标记；分派在 handler 里用 `ActiveCutaway`/`bCutawayDragActive` 判断。

- [ ] **Step 1: 给星图相机加 `OrientToDirection`**

在 `Source/StellarEra/Public/StarMapCameraPawn.h` 的 `void AddZoom(float Value);` 声明之后插入：

```cpp
	/**
	 * 把「相机 → 焦点」的视线方向设为 Direction（不必归一化）。
	 *
	 * 与 FocusOn 的分工：FocusOn 只改焦点与距离（平滑拉远），朝向在被调用时取
	 * 「当前位置 → 新焦点」；本函数用于之后再把朝向校正到指定方向 —— 剖面入口
	 * 要保证相机正对球心。pitch 夹在 ±89°、roll 恒为 0，避免万向节翻面。
	 */
	void OrientToDirection(const FVector& Direction);
```

在 `Source/StellarEra/Private/StarMapCameraPawn.cpp` 的 `AddZoom` 实现之后插入：

```cpp
void AStarMapCameraPawn::OrientToDirection(const FVector& Direction)
{
	if (Direction.IsNearlyZero())
	{
		return;
	}

	FRotator Rotation = Direction.Rotation();
	Rotation.Pitch = FMath::Clamp(Rotation.Pitch, -89.0, 89.0);
	Rotation.Roll = 0.0;
	OrbitRotation = Rotation;
}
```

- [ ] **Step 2: 控制器头文件——包含、前置声明、公开访问器**

先改 `Source/StellarEra/StellarEra.Build.cs`：把 `"StellarEraPlanet"` 从 `PrivateDependencyModuleNames` 删掉、加进 `PublicDependencyModuleNames`。公开头 `StellarEraPlayerController.h` 现在包含 `PlanetCutawayMath.h`，而 UBT 的规则是**公开头只能包含公开依赖的头**：

```csharp
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "StellarEraPlanet",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"StellarEraCore",
			"StellarEraVehicle",
```

然后改 `Source/StellarEra/Public/StellarEraPlayerController.h`：

1）在 `#include "GameFramework/PlayerController.h"` 之后插入（`ESectionRing` 是成员类型，必须给完整定义；`PlanetCutawayMath.h` 只依赖 CoreMinimal）：

```cpp
#include "PlanetCutawayMath.h"
```

2）在前置声明区（`class UPlanetEditComponent;` 之后）插入：

```cpp
class UPlanetCutawayComponent;
```

3）在 `protected:` 的 `UPlanetEditComponent* GetEditComponentInEditMode() const;` 之前插入公开访问器（放在 `public:` 段里 —— 星图 UI 每帧要读它）：

```cpp
	/** 当前激活的矿物剖面组件；未激活时为空。星图 UI 用它画图例。 */
	UPlanetCutawayComponent* GetActiveCutaway() const { return ActiveCutaway; }
```

4）在 `void ToggleStarMap();` 之后插入：

```cpp
	/**
	 * H 键：进入 / 退出矿物剖面。只在星图里生效。
	 *
	 * 进入条件：当前选中的是体素行星、且它带地质场。失败时给 2 秒提示，不改变状态。
	 * 退出条件：再按一次 H、退出星图、换 Pawn、或选中天体变了（见 PlayerTick）。
	 */
	void OnCutawayToggle();

	/** 左键拖拽圆环：Axis2D 是鼠标像素位移，换算成绕该圆环世界轴的旋转角。 */
	void OnCutawayDrag(const FInputActionValue& Value);

	/** 退出剖面（幂等）。退出星图 / 换 Pawn / 选中变化 / EndPlay 都走这里。 */
	void ExitCutawayIfActive();

	/** 屏幕点是否落在某根圆环上。命中时写 OutRing 并返回 true。 */
	bool PickCutawayRing(const FVector2D& ScreenPosition, ESectionRing& OutRing) const;

	/** 带时长的提示：几秒后自动消失，与每帧刷新的 ShowPrompt 分开。 */
	void ShowPromptFor(const FString& Text, float DurationSeconds);
```

5）在 `IA_EditModeToggle` 的 `UPROPERTY` 之后、类结尾 `};` 之前插入：

```cpp
	/** 矿物剖面开关（H）。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> IA_CutawayToggle;

	/** 拖拽剖面圆环（鼠标左键位移）。与 IA_Look 共用 Mouse2D，靠 ActiveCutaway 分派。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> IA_CutawayDrag;

	/** 当前激活的剖面组件。行星销毁时 GC 会把它置空。 */
	UPROPERTY(Transient)
	TObjectPtr<UPlanetCutawayComponent> ActiveCutaway;

	/** 左键本轮拖拽抓住的圆环；只有 bCutawayDragActive 为真时有意义。 */
	ESectionRing CutawayDragRing = ESectionRing::X;

	/** 本轮左键是否抓住了圆环（没抓住 = 剖面模式下左键什么都不做）。 */
	bool bCutawayDragActive = false;

	/** 本轮拖拽是否真的转过角度：没转过就不必在松手时补渲染一张 512²。 */
	bool bCutawayRotatedThisDrag = false;

	/** 圆环拾取容差（像素）。圆环本身 3px 粗，12px 让远处也能点中。 */
	UPROPERTY(EditAnywhere, Category = "StarMap", meta = (ClampMin = "0.0"))
	float CutawayRingPickTolerancePx = 12.f;
```

> 若把 5）里的成员放在 `protected:` 段，`UPROPERTY` 不受影响；`CutawayRingPickTolerancePx` 若想出现在 Details 面板，保持在 `protected:` 也可以（`EditAnywhere` 不看访问级别）。

- [ ] **Step 3: 输入资产与绑定**

`Source/StellarEra/Private/StellarEraPlayerController.cpp`：

1）`BuildInputAssets` 里，`IA_EditModeToggle = MakeAction(...)` 之后插入：

```cpp
	IA_CutawayToggle = MakeAction(this, TEXT("IA_CutawayToggle"), EInputActionValueType::Boolean);
	IA_CutawayDrag = MakeAction(this, TEXT("IA_CutawayDrag"), EInputActionValueType::Axis2D);
```

2）`MapKey(MappingContext, IA_EditModeToggle, EKeys::G);` 之后插入：

```cpp
	// 剖面：H 进出；左键位移转切面。IA_CutawayDrag 与 IA_Look 共用 Mouse2D ——
	// 同一个 IMC 内的动作互不吞输入（IA_Move / IA_Thrust 共用 W 是同一机制的既有用例），
	// 谁来处理由 ActiveCutaway 判断。
	MapKey(MappingContext, IA_CutawayToggle, EKeys::H);
	MapKey(MappingContext, IA_CutawayDrag, EKeys::Mouse2D);
```

3）`SetupInputComponent` 里，`IA_Interact` 的绑定之后插入：

```cpp
	EnhancedInput->BindAction(IA_CutawayToggle, ETriggerEvent::Started,
		this, &AStellarEraPlayerController::OnCutawayToggle);
	EnhancedInput->BindAction(IA_CutawayDrag, ETriggerEvent::Triggered,
		this, &AStellarEraPlayerController::OnCutawayDrag);
```

- [ ] **Step 4: 写四个新函数**

在 `AStellarEraPlayerController::ToggleStarMap()` 之前插入：

```cpp
void AStellarEraPlayerController::OnCutawayToggle()
{
	if (!Cast<AStarMapCameraPawn>(GetPawn()))
	{
		return;
	}

	if (ActiveCutaway)
	{
		ExitCutawayIfActive();
		return;
	}

	UCosmosSimulationSubsystem* Subsystem = GetWorld()
		? GetWorld()->GetSubsystem<UCosmosSimulationSubsystem>()
		: nullptr;
	AVoxelPlanetActor* Planet = Subsystem
		? Cast<AVoxelPlanetActor>(Subsystem->GetSelectedBody())
		: nullptr;
	UPlanetCutawayComponent* Cutaway = Planet ? Planet->GetCutawayComponent() : nullptr;
	if (!Cutaway)
	{
		ShowPromptFor(TEXT("矿物剖面只支持行星"), 2.f);
		return;
	}

	// 初始切面法线 = 相机看向球心的方向：进剖面的第一眼是「正对镜头的一面被切开」。
	FVector ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FVector ToCenter = (Planet->GetActorLocation() - ViewLocation).GetSafeNormal();
	const FVector InitialNormal = ToCenter.IsNearlyZero() ? FVector::UpVector : ToCenter;

	if (!Cutaway->Activate(Planet, InitialNormal))
	{
		// Activate 失败只有两种原因：没有地质场、或资产缺失（后者会先打 Error 日志）。
		ShowPromptFor(TEXT("该行星没有矿物分布"), 2.f);
		return;
	}

	// 拉到能看全剖面圆盘的距离：3 × 表面半径（圆盘半径 1.05R，视场 90° 时 3R 留有余量）。
	AStarMapCameraPawn* StarMap = Cast<AStarMapCameraPawn>(GetPawn());
	if (StarMap)
	{
		StarMap->FocusOn(Planet, 3.0 * Planet->GetSurfaceRadiusCm());
		StarMap->OrientToDirection(ToCenter);
	}

	ActiveCutaway = Cutaway;
}

void AStellarEraPlayerController::OnCutawayDrag(const FInputActionValue& Value)
{
	if (!ActiveCutaway || !bCutawayDragActive)
	{
		return;
	}

	const FVector2D Delta = Value.Get<FVector2D>();
	if (Delta.IsNearlyZero())
	{
		return;
	}

	// 横拖与竖拖绕同一根世界轴、方向相反：这是「转环」的直觉（拇指推环的切线方向）。
	const float DeltaDegrees = (float(Delta.X) - float(Delta.Y)) * 0.5f;
	ActiveCutaway->RotatePlane(CutawayDragRing, DeltaDegrees);
	bCutawayRotatedThisDrag = true;
}

void AStellarEraPlayerController::ExitCutawayIfActive()
{
	if (!ActiveCutaway)
	{
		return;
	}

	ActiveCutaway->Deactivate();
	ActiveCutaway = nullptr;
	bCutawayDragActive = false;
	bCutawayRotatedThisDrag = false;
}

bool AStellarEraPlayerController::PickCutawayRing(
	const FVector2D& ScreenPosition,
	ESectionRing& OutRing) const
{
	if (!ActiveCutaway)
	{
		return false;
	}

	TArray<FSectionRingWorldPolyline> WorldRings;
	ActiveCutaway->GetRingWorldPoints(WorldRings);

	TArray<FSectionRingScreenPolyline> ScreenRings;
	ScreenRings.Reserve(WorldRings.Num());

	for (const FSectionRingWorldPolyline& WorldRing : WorldRings)
	{
		FSectionRingScreenPolyline ScreenRing;
		ScreenRing.Ring = WorldRing.Ring;
		ScreenRing.Points.Reserve(WorldRing.Points.Num());

		bool bAllVisible = true;
		for (const FVector& WorldPoint : WorldRing.Points)
		{
			FVector2D ScreenPoint = FVector2D::ZeroVector;
			if (!ProjectWorldLocationToScreen(WorldPoint, ScreenPoint))
			{
				bAllVisible = false;
				break;
			}
			ScreenRing.Points.Add(ScreenPoint);
		}

		// 任何一点在相机背后就整条环作废：投影到背后会把点翻到屏幕另一侧，
		// 凭空造出一段「假环」，拾取会抓到屏幕对面去。
		if (bAllVisible)
		{
			ScreenRings.Add(MoveTemp(ScreenRing));
		}
	}

	return FPlanetCutawayMath::PickSectionRing(
		ScreenRings, ScreenPosition, CutawayRingPickTolerancePx, OutRing);
}
```

在 `ShowPrompt` 实现之后插入：

```cpp
void AStellarEraPlayerController::ShowPromptFor(const FString& Text, float DurationSeconds)
{
	if (!GEngine)
	{
		return;
	}

	// 与 ShowPrompt 分开的 key：那条每帧刷新（duration 0），这条到点自动消失。
	static const uint64 TransientPromptMessageKey = 0x43555441ULL;
	GEngine->AddOnScreenDebugMessage(
		TransientPromptMessageKey, DurationSeconds, FColor::Yellow, Text);
}
```

- [ ] **Step 5: 接进既有输入流**

1）`OnLook` 的星图分支：

```cpp
	else if (AStarMapCameraPawn* StarMap = Cast<AStarMapCameraPawn>(GetPawn()))
	{
		// 星图里左键拖拽就是转视角，不设 bLookHeld 那道门 —— 那道门是给角色
		// 「不按右键就别动视角」用的；星图的相机除了转视角没有别的职责。
		StarMap->OrbitLook(DeltaYaw, DeltaPitch);
	}
```

改成：

```cpp
	else if (AStarMapCameraPawn* StarMap = Cast<AStarMapCameraPawn>(GetPawn()))
	{
		// 星图里左键拖拽就是转视角，不设 bLookHeld 那道门 —— 那道门是给角色
		// 「不按右键就别动视角」用的；星图的相机除了转视角没有别的职责。
		//
		// 剖面模式是唯一例外：左键归圆环拖拽，视角只认右键（与角色同一套门），
		// 否则抓住环一拖，相机和切面会一起转。
		if (ActiveCutaway && !bLookHeld)
		{
			return;
		}
		StarMap->OrbitLook(DeltaYaw, DeltaPitch);
	}
```

2）`OnStarMapSelectStarted`：在 `if (!Cast<AStarMapCameraPawn>(GetPawn())) { return; }` 之后、那句 `// 用 double 重载` 之前插入：

```cpp
	if (ActiveCutaway)
	{
		double CutawayMouseX = 0.0;
		double CutawayMouseY = 0.0;
		if (!GetMousePosition(CutawayMouseX, CutawayMouseY))
		{
			return;
		}

		bCutawayRotatedThisDrag = false;
		bCutawayDragActive = PickCutawayRing(
			FVector2D(CutawayMouseX, CutawayMouseY), CutawayDragRing);

		// 没抓到环就什么都不做：剖面模式下左键不再拾取天体，也不转视角（归右键）。
		return;
	}
```

3）`OnStarMapSelectCompleted`：函数最开头、`if (!bStarMapSelectHeld)` 之前插入：

```cpp
	if (ActiveCutaway)
	{
		if (bCutawayDragActive && bCutawayRotatedThisDrag)
		{
			// 拖拽期间只画 96² 预览；松手补一张 512²。没转过就省掉这一次。
			ActiveCutaway->RequestSectionRender(ActiveCutaway->FinalResolution);
		}
		bCutawayDragActive = false;
		bCutawayRotatedThisDrag = false;
		return;
	}
```

4）`ToggleStarMap` 的退出分支：`if (CurrentStarMapPawn)` 之后第一行插入：

```cpp
		// 先退剖再换 Pawn：剖面把全球材质的 OpacityMask 改了，必须在恢复视角之前还原。
		ExitCutawayIfActive();
```

5）`OnPossess`：`Super::OnPossess(InPawn);` 之后插入：

```cpp
	// 换 Pawn（登船 / 离船 / 进出星图 / DebugPossessShip）一律退出剖面。
	ExitCutawayIfActive();
```

6）`PlayerTick`：`Super::PlayerTick(DeltaTime);` 之后插入：

```cpp
	// 剖面绑在「选中的那颗行星」上。焦点被改选、或 Pawn 已经不是星图相机，
	// 就立刻退出 —— 否则另一颗行星会一直保持被切开的半透明状态。
	if (ActiveCutaway)
	{
		UCosmosSimulationSubsystem* CutawaySubsystem = GetWorld()
			? GetWorld()->GetSubsystem<UCosmosSimulationSubsystem>()
			: nullptr;
		const bool bSamePlanet = CutawaySubsystem &&
			CutawaySubsystem->GetSelectedBody() == ActiveCutaway->GetCutawayPlanet();
		if (!Cast<AStarMapCameraPawn>(GetPawn()) || !bSamePlanet)
		{
			ExitCutawayIfActive();
		}
	}
```

- [ ] **Step 6: 构建**

```bash
cd /d/ && "D:/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" \
  "D:/Application/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" \
  StellarEraEditor Win64 Development \
  -Project="F:/workspace/ue/StellarEra/StellarEra.uproject" -WaitMutex -FromMsBuild 2>&1 | tail -40
```

Expected: `Result: Succeeded`。

**常见错误与对策：**

| 报错 | 原因 | 处理 |
|---|---|---|
| `ESectionRing` 未定义 | 头文件没包含 `PlanetCutawayMath.h` | 回 Step 2 |
| `GetActiveCutaway` 找不到 `UPlanetCutawayComponent` | 只前置声明了、却在 inline 里调成员 | 本步只返回指针，不调成员；UI 侧在 .cpp 里调 |
| `ProjectWorldLocationToScreen` 参数不匹配 | 第三参默认值 | 只传两个参数 |
| `Value.Get<FVector2D>()` 编译失败 | IA 不是 Axis2D | 检查 `MakeAction(..., Axis2D)` |
| `Cutaway->FinalResolution` 不可访问 | Task 4 里写成了 private | 回 Task 4 确认它是 public |

- [ ] **Step 7: 提交**

```bash
cd /f/workspace/ue/StellarEra
git add Source/StellarEra/Public/StarMapCameraPawn.h \
        Source/StellarEra/Private/StarMapCameraPawn.cpp \
        Source/StellarEra/Public/StellarEraPlayerController.h \
        Source/StellarEra/Private/StellarEraPlayerController.cpp \
        Source/StellarEra/StellarEra.Build.cs
git commit -m "feat(剖面): H 开关、圆环拾取与拖拽旋转，剖面期相机让出左键" \
  -m "Co-Authored-By: Claude Code <noreply@anthropic.com>" -- \
  Source/StellarEra/Public/StarMapCameraPawn.h \
  Source/StellarEra/Private/StarMapCameraPawn.cpp \
  Source/StellarEra/Public/StellarEraPlayerController.h \
  Source/StellarEra/Private/StellarEraPlayerController.cpp \
  Source/StellarEra/StellarEra.Build.cs
```

---

### Task 6: 图例面板（左上角元素表 + 操作提示）

**Files:**
- Modify: `Source/StellarEra/Public/StarMapUISystem.h`
- Modify: `Source/StellarEra/Private/StarMapUISystem.cpp`
- Test: `Source/StellarEra/Private/CutawayLegendTests.cpp`（新建）

**Interfaces:**
- Consumes: `AStellarEraPlayerController::GetActiveCutaway()`（Task 5）、`UPlanetCutawayComponent::GetCutawayPlanet/GetGeologyField`（Task 4）、`FPlanetGeologyField::GetElementDefinitions/GetTotalAbundance`（Task 4）、`FPlanetGeologyElement`（已有）
- Produces:
  - `struct FCutawayLegendEntry { FString DisplayName; FLinearColor Color; float Abundance; };`
  - `static TArray<FString> UStarMapUISystem::FormatCutawayLegendLines(const FString& PlanetName, TArrayView<const FCutawayLegendEntry> Entries);`

- [ ] **Step 1: 写失败测试**

Create `Source/StellarEra/Private/CutawayLegendTests.cpp`:

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "StarMapUISystem.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FStellarEraCutawayLegendFormatTest,
	"StellarEra.StarMap.Cutaway.Legend.Format",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraCutawayLegendFormatTest::RunTest(const FString& Parameters)
{
	const TArray<FCutawayLegendEntry> Entries = {
		{ TEXT("硅"), FLinearColor::Gray, 0.5f },
		{ TEXT("铁"), FLinearColor::Red, 0.25f },
		{ TEXT(""), FLinearColor::Blue, 0.25f }
	};

	const TArray<FString> Lines =
		UStarMapUISystem::FormatCutawayLegendLines(TEXT("地球"), Entries);

	TestEqual(TEXT("标题 + 三行元素 + 提示"), Lines.Num(), 5);
	TestEqual(TEXT("标题带行星名"), Lines[0], FString(TEXT("矿物剖面 — 地球")));
	TestEqual(TEXT("元素行含名称与百分比"), Lines[1], FString(TEXT("硅  50%")));
	TestEqual(TEXT("第二行"), Lines[2], FString(TEXT("铁  25%")));
	TestEqual(TEXT("空名退回问号"), Lines[3], FString(TEXT("?  25%")));
	TestTrue(TEXT("末行是操作提示"), Lines.Last().StartsWith(TEXT("H 退出")));

	// 百分比夹取：负值按 0、超过 1 按 100。
	const TArray<FCutawayLegendEntry> Clamped = {
		{ TEXT("负"), FLinearColor::White, -0.4f },
		{ TEXT("溢"), FLinearColor::White, 1.7f }
	};
	const TArray<FString> ClampedLines =
		UStarMapUISystem::FormatCutawayLegendLines(TEXT("X"), Clamped);
	TestEqual(TEXT("负值夹到 0%"), ClampedLines[1], FString(TEXT("负  0%")));
	TestEqual(TEXT("超过 1 夹到 100%"), ClampedLines[2], FString(TEXT("溢  100%")));

	// 没有元素：仍给一行占位。
	const TArray<FCutawayLegendEntry> NoEntries;
	const TArray<FString> EmptyLines =
		UStarMapUISystem::FormatCutawayLegendLines(TEXT("X"), NoEntries);
	TestEqual(TEXT("空元素表共三行"), EmptyLines.Num(), 3);
	TestEqual(TEXT("占位文本"), EmptyLines[1], FString(TEXT("（无矿物元素）")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
```

- [ ] **Step 2: 给测试文件加 BOM**

```bash
cd /f/workspace/ue/StellarEra
f=Source/StellarEra/Private/CutawayLegendTests.cpp
printf '\xEF\xBB\xBF' > "$f.bom" && cat "$f" >> "$f.bom" && mv "$f.bom" "$f"
head -c 3 "$f" | od -An -tx1
```

- [ ] **Step 3: 构建并确认测试失败（红）**

```bash
cd /d/ && "D:/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" \
  "D:/Application/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" \
  StellarEraEditor Win64 Development \
  -Project="F:/workspace/ue/StellarEra/StellarEra.uproject" -WaitMutex -FromMsBuild 2>&1 | tail -20
```

Expected: **失败**，错误是 `FCutawayLegendEntry` 未定义 / `FormatCutawayLegendLines` 不是成员。这一步是 TDD 的红：若它竟然编译通过，说明 Step 4 的代码已经存在，先查是不是抄错了文件。

- [ ] **Step 4: 头文件**

`Source/StellarEra/Public/StarMapUISystem.h`：

1）在 `struct FStarMapPanelRect { ... };` 之后插入：

```cpp
/** 矿物剖面图例的一行。纯数据，不含 UObject 引用。 */
struct FCutawayLegendEntry
{
	FString DisplayName;

	/** 色块颜色 —— 与切面里该元素的显示色一致。 */
	FLinearColor Color = FLinearColor::White;

	/** 该元素在行星总含量里的份额（0..1，各行相加约为 1）。 */
	float Abundance = 0.f;
};
```

2）在 `static TArray<FString> FormatBodyInfoLines(const FStarMapBodyInfo& Info);` 之后插入：

```cpp
	/**
	 * 剖面图例的文本行：第 0 行标题（「矿物剖面 — 行星名」），其后每元素一行
	 * 「名称  百分比」，最后一行是操作提示。
	 *
	 * 百分比按 0..1 份额四舍五入，并夹到 [0,100]；元素表为空时给一行占位。
	 * 纯函数，便于无头测试。
	 */
	static TArray<FString> FormatCutawayLegendLines(
		const FString& PlanetName,
		TArrayView<const FCutawayLegendEntry> Entries);
```

3）在 `private:` 的 `DrawBodyPanel(...)` 之后插入：

```cpp
	void DrawCutawayLegend(
		AStellarEraHUD& Hud,
		const FString& PlanetName,
		const TArray<FCutawayLegendEntry>& Entries,
		const FVector2D& ViewportSizePx);
```

- [ ] **Step 5: 实现**

`Source/StellarEra/Private/StarMapUISystem.cpp`：

1）include 区，`#include "StellarEraPlayerController.h"` 之后插入：

```cpp
#include "PlanetCutawayComponent.h"
#include "PlanetGeologyField.h"
```

2）在 `FormatBodyInfoLines` 实现之后插入：

```cpp
TArray<FString> UStarMapUISystem::FormatCutawayLegendLines(
	const FString& PlanetName,
	TArrayView<const FCutawayLegendEntry> Entries)
{
	TArray<FString> Lines;
	Lines.Reserve(Entries.Num() + 2);

	Lines.Add(FString::Printf(TEXT("矿物剖面 — %s"), *PlanetName));

	if (Entries.Num() == 0)
	{
		Lines.Add(TEXT("（无矿物元素）"));
	}
	else
	{
		for (const FCutawayLegendEntry& Entry : Entries)
		{
			const FString Name = Entry.DisplayName.IsEmpty()
				? FString(TEXT("?"))
				: Entry.DisplayName;
			const int32 Percent = FMath::RoundToInt(
				FMath::Clamp(Entry.Abundance, 0.f, 1.f) * 100.f);
			Lines.Add(FString::Printf(TEXT("%s  %d%%"), *Name, Percent));
		}
	}

	Lines.Add(TEXT("H 退出   左键拖圆环转切面   右键拖转视角   滚轮缩放"));
	return Lines;
}
```

3）`Draw` 函数末尾（悬停面板那段之后、函数结束的 `}` 之前）插入：

```cpp
	// 矿物剖面图例：固定左上角，画在最后（压在所有天体面板之上）。
	if (UPlanetCutawayComponent* Cutaway = PlayerController->GetActiveCutaway())
	{
		AVoxelPlanetActor* CutawayPlanet = Cutaway->GetCutawayPlanet();
		if (CutawayPlanet)
		{
			TArray<FCutawayLegendEntry> Entries;
			if (const TSharedPtr<const FPlanetGeologyField> Geology = Cutaway->GetGeologyField();
				Geology.IsValid())
			{
				const TArray<FPlanetGeologyElement> Elements = Geology->GetElementDefinitions();
				const float TotalAbundance = FMath::Max(Geology->GetTotalAbundance(), 1e-6f);
				Entries.Reserve(Elements.Num());
				for (const FPlanetGeologyElement& Element : Elements)
				{
					FCutawayLegendEntry& Entry = Entries.AddDefaulted_GetRef();
					Entry.DisplayName = Element.DisplayName.IsEmpty()
						? Element.Id
						: Element.DisplayName;
					Entry.Color = Element.Color;
					Entry.Abundance = Element.Abundance / TotalAbundance;
				}
			}

			DrawCutawayLegend(
				Hud, CutawayPlanet->GetActorNameOrLabel(), Entries, ViewportSize);
		}
	}
```

4）在 `DrawBodyPanel` 实现之后追加：

```cpp
void UStarMapUISystem::DrawCutawayLegend(
	AStellarEraHUD& Hud,
	const FString& PlanetName,
	const TArray<FCutawayLegendEntry>& Entries,
	const FVector2D& ViewportSizePx)
{
	UFont* Font = GEngine ? GEngine->GetMediumFont() : nullptr;
	if (!Font)
	{
		return;
	}

	const TArray<FString> Lines = FormatCutawayLegendLines(PlanetName, Entries);
	if (Lines.Num() == 0)
	{
		return;
	}

	constexpr float TextScale = 1.f;
	constexpr float Padding = 10.f;
	constexpr float LineSpacing = 4.f;
	constexpr float SeparatorSpacing = 6.f;
	constexpr float SwatchSize = 14.f;
	constexpr float SwatchGap = 8.f;
	constexpr float Margin = 8.f;

	// 结构固定：标题 / 元素行（每条一行）/ 操作提示。
	const FString& Title = Lines[0];
	const FString& Hint = Lines.Last();

	float TitleWidth = 0.f;
	float TitleHeight = 0.f;
	Hud.GetTextSize(Title, TitleWidth, TitleHeight, Font, TextScale);

	float RowTextWidth = 0.f;
	float RowHeight = 0.f;
	for (int32 Index = 1; Index < Lines.Num() - 1; ++Index)
	{
		float LineWidth = 0.f;
		float ThisHeight = 0.f;
		Hud.GetTextSize(Lines[Index], LineWidth, ThisHeight, Font, TextScale);
		RowTextWidth = FMath::Max(RowTextWidth, LineWidth);
		RowHeight = FMath::Max(RowHeight, ThisHeight);
	}

	float HintWidth = 0.f;
	float HintHeight = 0.f;
	Hud.GetTextSize(Hint, HintWidth, HintHeight, Font, TextScale);

	const int32 EntryCount = Entries.Num();
	const float TextColumnWidth =
		FMath::Max(TitleWidth, FMath::Max(RowTextWidth, HintWidth));
	const float PanelWidth = Padding * 2.f + SwatchSize + SwatchGap + TextColumnWidth;
	const float RowSlotHeight = FMath::Max(RowHeight, SwatchSize);
	const float RowsHeight = (EntryCount > 0 ? EntryCount : 1) * (RowSlotHeight + LineSpacing);
	const float PanelHeight = Padding * 2.f + TitleHeight
		+ SeparatorSpacing * 2.f + 1.f
		+ RowsHeight
		+ SeparatorSpacing * 2.f + 1.f + HintHeight;

	// 左上角 = 锚点就在边距上、半径 0、间距 0（PlacePanelBesideAnchor 会把 Y 夹到 Margin）。
	const FStarMapPanelRect Rect = PlacePanelBesideAnchor(
		FVector2D(Margin, Margin), 0.f,
		FVector2D(PanelWidth, PanelHeight), ViewportSizePx, 0.f, Margin);

	const FLinearColor Background(0.f, 0.f, 0.f, 0.65f);
	const FLinearColor Gold(1.f, 0.85f, 0.30f, 1.f);
	const FLinearColor White(1.f, 1.f, 1.f, 1.f);
	const FLinearColor Dim(0.75f, 0.75f, 0.75f, 1.f);

	Hud.DrawRect(Background, Rect.X, Rect.Y, Rect.W, Rect.H);
	Hud.DrawRect(Gold, Rect.X, Rect.Y, Rect.W, 1.f);
	Hud.DrawRect(Gold, Rect.X, Rect.Y + Rect.H - 1.f, Rect.W, 1.f);
	Hud.DrawRect(Gold, Rect.X, Rect.Y, 1.f, Rect.H);
	Hud.DrawRect(Gold, Rect.X + Rect.W - 1.f, Rect.Y, 1.f, Rect.H);

	float CursorY = Rect.Y + Padding;
	Hud.DrawText(Title, Gold, Rect.X + Padding, CursorY, Font, TextScale);
	CursorY += TitleHeight + SeparatorSpacing;

	Hud.DrawRect(Gold, Rect.X + Padding, CursorY, Rect.W - Padding * 2.f, 1.f);
	CursorY += 1.f + SeparatorSpacing;

	if (EntryCount > 0)
	{
		for (int32 Index = 0; Index < EntryCount; ++Index)
		{
			const float SwatchY = CursorY + (RowSlotHeight - SwatchSize) * 0.5f;
			Hud.DrawRect(Entries[Index].Color,
				Rect.X + Padding, SwatchY, SwatchSize, SwatchSize);
			Hud.DrawText(Lines[Index + 1], White,
				Rect.X + Padding + SwatchSize + SwatchGap, CursorY, Font, TextScale);
			CursorY += RowSlotHeight + LineSpacing;
		}
	}
	else
	{
		Hud.DrawText(Lines[1], White, Rect.X + Padding, CursorY, Font, TextScale);
		CursorY += RowSlotHeight + LineSpacing;
	}

	CursorY += SeparatorSpacing;
	Hud.DrawRect(Gold, Rect.X + Padding, CursorY, Rect.W - Padding * 2.f, 1.f);
	CursorY += 1.f + SeparatorSpacing;

	Hud.DrawText(Hint, Dim, Rect.X + Padding, CursorY, Font, TextScale);
}
```

- [ ] **Step 6: 构建并跑测试（绿）**

```bash
cd /d/ && "D:/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" \
  "D:/Application/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" \
  StellarEraEditor Win64 Development \
  -Project="F:/workspace/ue/StellarEra/StellarEra.uproject" -WaitMutex -FromMsBuild 2>&1 | tail -20
```

Expected: `Result: Succeeded`。

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra.StarMap.Cutaway" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout \
  -abslog="F:/workspace/ue/StellarEra/Saved/Logs/CutawayLegendTests.log"
grep -E "Test Completed|Automation Test Queue Empty|FAILED" \
  "F:/workspace/ue/StellarEra/Saved/Logs/CutawayLegendTests.log" | tail -20
```

Expected: `StellarEra.StarMap.Cutaway.Legend.Format` 一项，`Result={Success}`，无 FAILED。

- [ ] **Step 7: 提交**

```bash
cd /f/workspace/ue/StellarEra
git add Source/StellarEra/Public/StarMapUISystem.h \
        Source/StellarEra/Private/StarMapUISystem.cpp \
        Source/StellarEra/Private/CutawayLegendTests.cpp
git commit -m "feat(剖面): 星图左上角矿物图例与操作提示" \
  -m "Co-Authored-By: Claude Code <noreply@anthropic.com>" -- \
  Source/StellarEra/Public/StarMapUISystem.h \
  Source/StellarEra/Private/StarMapUISystem.cpp \
  Source/StellarEra/Private/CutawayLegendTests.cpp
```

---

### Task 7: 整体验收（构建 + 全量测试 + PIE 清单）

**Files:** 无（只验证；发现问题回到对应任务修）

- [ ] **Step 1: 全量构建**

关闭编辑器后：

```bash
cd /d/ && "D:/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" \
  "D:/Application/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" \
  StellarEraEditor Win64 Development \
  -Project="F:/workspace/ue/StellarEra/StellarEra.uproject" -WaitMutex -FromMsBuild 2>&1 | tail -30
```

Expected: `Result: Succeeded`。若只有链接失败、没有编译错误，按记忆里的判据先确认是不是编辑器还开着锁了 DLL。

- [ ] **Step 2: 全量自动化测试**

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout \
  -abslog="F:/workspace/ue/StellarEra/Saved/Logs/StellarEraAllTests.log"
grep -cE "^Test Completed.*Result=\{Success\}" "F:/workspace/ue/StellarEra/Saved/Logs/StellarEraAllTests.log"
grep -E "Result=\{Fail\}|FAILED" "F:/workspace/ue/StellarEra/Saved/Logs/StellarEraAllTests.log" | head
```

Expected: **62** 条 Success（基线 53 + Task 1 的 5 + Task 2 的 3 + Task 6 的 1），`grep Fail` 无输出。

若基线不是 53（期间别的工作改过测试），以「基线 + 9」为准，并在报告里写出实际的基线与增量，不要直接套 62。

- [ ] **Step 3: PIE 人工验收（`UniverseMap`）**

打开编辑器、进 `UniverseMap`、让编辑器**保持前台**（后台会被限到 3 FPS，性能数字失真），PIE 后逐条打勾：

1. 飞近一颗行星 → `M` 进星图 → 左键点中该行星（右侧出现面板）→ 按 `H`：行星被切开，切面是一张正对镜头的圆盘，相机平滑拉到约 3 倍表面半径。
2. 三根圆环可见：红（X）、绿（Y）、蓝（Z），都以球心为中心；能透过行星看到（前景深度）。
3. 左键拖红色环：切面绕世界 X 轴转，圆盘与图像同步；松手后图像变清晰（512²）。
4. 剖面模式下在空白处左键拖：相机和切面都**不动**（左键在剖面模式不承担其他职责）。
5. 右键拖：相机绕行星转；转到背面时可见侧自动换边（切掉的永远是近侧）。
6. 滚轮缩放正常；圆环随相机缩放的视觉大小正常变化。
7. 左上角图例：标题是行星名；每行有色块、元素名、百分比，百分比之和 ≈ 100%（`earth` 预设为 6 行）。
8. 再按 `H`：行星恢复完整、大气壳回来、圆环与圆盘消失；**没有**行星残留半透明。
9. 剖面中用 `M` 退出星图：回到飞船，行星完整；再进星图重按 `H` 能再次进入。
10. 剖面中左键改选飞船（若可点中）→ 剖面自动退出。
11. 选中飞船（非行星）按 `H`：黄字提示「矿物剖面只支持行星」，2 秒消失。
12. 把某颗行星的 `GeologyPreset` 清空后重建，选中它按 `H`：提示「该行星没有矿物分布」。
13. 性能：在日志里找 `Cutaway: 剖面 512x512 渲染+上传 ... ms` 与 `96x96`；512² 若超过 ~200 ms，记录实际数值并写进交付说明（已知风险：每像素一次地表半径查询 + 一次地质采样）。

> PIE 是人工任务：执行者只负责跑完并把结果（通过/不通过 + 日志关键行）写进报告，不要用无头测试冒充。

- [ ] **Step 4: 证据汇总**

提交前把这三样贴进最终报告（verification-before-completion 的要求）：

1. 最后一次 UBT 输出里的 `Result: Succeeded` 行。
2. 全量测试的成功条数与失败条数（两个 `grep -c` 的原始数字）。
3. PIE 清单 13 条的实际结果，以及 `Cutaway: 剖面` 日志的 512² / 96² 耗时数值。

---

## 与 spec 的覆盖对照（自检用）

| spec 章节 | 落点 |
|---|---|
| §3.1 剖面图像公式（像素中心、深度、存在度、纯度、羽化） | Task 2 |
| §3.2 几何基与镜像条件 | Task 1（`ComputePlaneBasis`）、Task 3（UV 图）、Task 4（圆盘旋转 `MakeFromXZ(AxisU, PlaneNormal)`，偏移用 `ClipNormal`） |
| §3.3 圆环半径与颜色 | Task 1（`RingPoint`）、Task 4（`RingRadiusScale`、颜色表） |
| §4.1 裁剪遮罩公式与 `Active` | Task 3（材质图）、Task 4（`ApplyClipMask`） |
| §4.2 两个单参数 MPC（GUID 约束） | Task 3 |
| §5 进入/退出、相机 3R、输入分派 | Task 5 |
| §6 图例与提示 | Task 6（图例）、Task 5（两条失败提示） |
| §7 观察性（不改体素/碰撞/存档） | Task 4（组件只读写纹理与材质参数；不调任何体素 API） |
| §8 性能预算与风险 | Task 4（渲染日志）、Task 7 第 13 条量测 |
| §9 数值示例（2079.2 / 79.208） | Task 2 的 `DepthMapping` 测试 |




