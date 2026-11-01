# 行星大气：球内天空 — 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让行星大气的壳材质变成内外两用 —— 球外照旧画正面辉光，球内画背面天空，并用场景深度把散射积分截到真实几何，得到正确遮挡与空中透视。

**Architecture:** 只改三处：材质生成器里的开关与 HLSL、材质生成器里新增的两个输入节点、壳组件的半透明排序优先级。求交与散射积分主体一行不改 —— 现有的 `tEnter = max(-bOuter - sqrtOuter, 0.0)` 在相机位于壳内时本来就退化成 0。新增一个结构断言测试，锁死"资产与 HLSL 同步重建过"。

**Tech Stack:** Unreal Engine 5.8 / C++ / 材质表达式图（`UMaterialExpressionCustom` 内联 HLSL）/ UE Automation Test。

**Spec:** `docs/superpowers/specs/2026-11-01-atmosphere-inside-sky-design.md`

## Global Constraints

- 引擎版本 UE **5.8.3**，平台 Windows / Win64，模块 `StellarEraPlanet`。
- **不得修改 `Plugins/` 下任何文件**（`VoxelFree`、`VibeUE` 都是第三方）。
- `*Tests.cpp` 只能放在 `Source/StellarEraPlanet/Src/Tests/`。
- 含中文的 `.h` / `.cpp` 必须存 **UTF-8 with BOM**；改完后用 `head -c 3 <file> | xxd` 核对是 `efbbbf`。
- 不要统一换行符（仓库本来就是 CRLF/LF 混合）。
- 提交必须带 pathspec：`git commit -m "..." -- <paths>`。索引里有常驻暂存文件，裸 `git commit` 会吞掉全部。
- **构建前必须关闭编辑器**（Live Coding 会锁 DLL）。
- 外接程序的绝对路径含空格，作为**命令名**会被沙箱拒绝。一律先把目录塞进 `PATH`，再用裸名调用，且**不要 `cd` 出 worktree**：

```bash
export PATH="/d/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64:/d/Application/Epic Games/UE_5.8/Engine/Binaries/Win64:$PATH"
```

- 构建（`$WT` = worktree 根目录）：

```bash
dotnet "/d/Application/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" \
  StellarEraEditor Win64 Development \
  -Project="$WT/StellarEra.uproject" -WaitMutex -FromMsBuild
```

- 跑自动化测试（编辑器开着也能跑，约 25 s 干净退出）：

```bash
UnrealEditor-Cmd "$WT/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra.Planet.Atmosphere" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout -abslog="$WT/Saved/Logs/atmo-tests.log"
```

- 重建材质资产：

```bash
UnrealEditor-Cmd "$WT/StellarEra.uproject" \
  -ExecCmds="StellarEra.BuildAtmosphereMaterial,QUIT" \
  -unattended -nopause -nullrhi -nosplash -stdout -abslog="$WT/Saved/Logs/atmo-build.log"
```

---

## 文件结构

| 文件 | 职责 | 本次动作 |
|---|---|---|
| `Source/StellarEraPlanet/Src/Tests/PlanetAtmosphereMaterialTests.cpp` | 材质资产的结构断言（开关 + 9 个参数名 + HLSL 关键片段） | **新建** |
| `Source/StellarEraPlanet/Src/Atmosphere/PlanetAtmosphereMaterialBuilder.cpp` | 生成壳材质资产的唯一入口 | 改 |
| `Source/StellarEraPlanet/Src/Atmosphere/PlanetAtmosphereComponent.cpp` | 壳组件：注册、几何、逐帧参数 | 改（一行 + 注释） |
| `Source/StellarEraPlanet/Src/Atmosphere/PlanetAtmosphereComponent.h` | 同上，头部注释已过期 | 改（仅注释） |
| `Content/Code/Planet/Materials/M_PlanetAtmosphere.uasset` | 生成物 | **重新生成**（LFS） |

---

## Task 1: 材质结构断言测试（先红）

**Files:**
- Create: `Source/StellarEraPlanet/Src/Tests/PlanetAtmosphereMaterialTests.cpp`

**Interfaces:**
- Consumes: 无（只读资产 `/Game/Code/Planet/Materials/M_PlanetAtmosphere`）
- Produces: 自动化测试名 `StellarEra.Planet.AtmosphereMaterial.Structure`；文件内私有辅助 `LoadAtmosphereMaterial()`、`HasParameter(const TArray<FMaterialParameterInfo>&, FName)`，Task 3 复用。

- [ ] **Step 1: 写失败的测试**

新建 `Source/StellarEraPlanet/Src/Tests/PlanetAtmosphereMaterialTests.cpp`（**UTF-8 with BOM**）：

```cpp
#include "CoreMinimal.h"
#include "MaterialTypes.h"
#include "Materials/Material.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** 与 PlanetAtmosphereComponent.cpp 的 AtmosphereMaterialPath 是同一个资产。 */
	const TCHAR* const AtmosphereMaterialPath =
		TEXT("/Game/Code/Planet/Materials/M_PlanetAtmosphere");

	UMaterial* LoadAtmosphereMaterial()
	{
		return LoadObject<UMaterial>(nullptr, AtmosphereMaterialPath);
	}

	bool HasParameter(const TArray<FMaterialParameterInfo>& Infos, const FName Name)
	{
		return Infos.ContainsByPredicate(
			[Name](const FMaterialParameterInfo& Info) { return Info.Name == Name; });
	}
}

// 锁死"资产真的按 PlanetAtmosphereMaterialBuilder.cpp 当前的样子重建过"。
// 改生成器代码忘了重跑 StellarEra.BuildAtmosphereMaterial，就会红在这里，
// 而不是等到 PIE 里发现天空还是黑的。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraAtmosphereMaterialStructureTest,
	"StellarEra.Planet.AtmosphereMaterial.Structure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraAtmosphereMaterialStructureTest::RunTest(const FString& Parameters)
{
	UMaterial* Material = LoadAtmosphereMaterial();
	if (!TestNotNull(TEXT("材质资产"), Material))
	{
		AddError(TEXT("资产不存在。先在编辑器里跑一次 StellarEra.BuildAtmosphereMaterial。"));
		return false;
	}

	// 球内看得见要靠双面；不被地形挡掉要靠关深度测试。
	TestTrue(TEXT("TwoSided（球内要看得见背面）"), Material->IsTwoSided());
	TestTrue(TEXT("bDisableDepthTest（不被地形挡掉）"), Material->bDisableDepthTest);

	TArray<FMaterialParameterInfo> ScalarInfos;
	TArray<FGuid> ScalarIds;
	Material->GetAllScalarParameterInfo(ScalarInfos, ScalarIds);

	const TCHAR* const ScalarNames[] = {
		TEXT("PlanetRadius"), TEXT("ShellRadius"),
		TEXT("RayleighScaleHeight"), TEXT("MieScaleHeight")
	};
	for (const TCHAR* Name : ScalarNames)
	{
		TestTrue(FString::Printf(TEXT("标量参数 %s"), Name), HasParameter(ScalarInfos, FName(Name)));
	}

	TArray<FMaterialParameterInfo> VectorInfos;
	TArray<FGuid> VectorIds;
	Material->GetAllVectorParameterInfo(VectorInfos, VectorIds);

	const TCHAR* const VectorNames[] = {
		TEXT("RayleighBeta"), TEXT("MieBeta"), TEXT("PlanetCenter"),
		TEXT("SunDirection"), TEXT("SunIntensity")
	};
	for (const TCHAR* Name : VectorNames)
	{
		TestTrue(FString::Printf(TEXT("向量参数 %s"), Name), HasParameter(VectorInfos, FName(Name)));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
```

- [ ] **Step 2: 构建**

关闭编辑器后运行 Global Constraints 里的构建命令（`$WT` 换成 worktree 根目录）。
Expected: `Result: Succeeded`（或 `Failed` 但只有链接 DLL 失败 —— 看 `grep -c "error C"` 为 0 即可；见记忆「UBT 直调 dotnet 规避 Build.bat 毒化」）。

- [ ] **Step 3: 跑测试确认它失败**

运行 Global Constraints 里的测试命令，然后查日志：

```bash
grep -i "atmospherematerial" "$WT/Saved/Logs/atmo-tests.log"
```

Expected: **FAIL**，失败项为 `TwoSided（球内要看得见背面）` 与 `bDisableDepthTest（不被地形挡掉）`（当前分别是 `false` / `false`）。9 个参数名应当全部通过（资产里本来就有）。

如果参数名也挂了，先别继续 —— 说明资产名或路径写错了，先核对 `/Game/Code/Planet/Materials/M_PlanetAtmosphere` 存在。

- [ ] **Step 4: 提交**

```bash
git add Source/StellarEraPlanet/Src/Tests/PlanetAtmosphereMaterialTests.cpp
git commit -m "test(大气): 材质结构断言——双面、关深度测试、9 个参数名

当前资产仍是单面且开着深度测试，本测试预期为红；下一个提交修好它。

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- Source/StellarEraPlanet/Src/Tests/PlanetAtmosphereMaterialTests.cpp
```

---

## Task 2: 材质开关 + 重新生成资产

**Files:**
- Modify: `Source/StellarEraPlanet/Src/Atmosphere/PlanetAtmosphereMaterialBuilder.cpp:249-254`
- Regenerate: `Content/Code/Planet/Materials/M_PlanetAtmosphere.uasset`

**Interfaces:**
- Consumes: Task 1 的测试。
- Produces: 资产具备 `TwoSided == true`、`bDisableDepthTest == true`。HLSL 与节点结构不变，所以本任务**不会**让球内天空正常工作（双面 + 没有面剔除 = 正反面各混合一次）—— 这是刻意的，先把"这两个开关能不能编译"单独验证掉（spec §8 风险 #3）。

- [ ] **Step 1: 改材质标志**

`Source/StellarEraPlanet/Src/Atmosphere/PlanetAtmosphereMaterialBuilder.cpp`，把

```cpp
	Material->BlendMode = BLEND_Translucent;
	Material->SetShadingModel(MSM_Unlit);
	Material->TwoSided = false;             // 球内看不见壳，交给引擎大气
```

改成

```cpp
	Material->BlendMode = BLEND_Translucent;
	Material->SetShadingModel(MSM_Unlit);

	// 双面：球内看到的是背面，不画背面就等于天空是黑的。
	// 两面会在同一屏幕区域各出一个片元，由 HLSL 里的 TwoSidedSign 早退剔掉多余的一面
	// （见下一个任务）—— 在此之前会有两次混合，这是有意的中间态。
	Material->TwoSided = true;

	// 壳在壳半径处，地形比它近。开深度测试的话，球内脚下往上看会被地形挡掉整片，
	// 只剩"透过地形的缝"。关掉之后由 HLSL 里的场景深度截断负责遮挡与空中透视。
	Material->bDisableDepthTest = true;
```

- [ ] **Step 2: 构建**

同 Task 1 Step 2。Expected: 成功。

- [ ] **Step 3: 重新生成材质资产**

先确认编辑器已关闭，然后运行 Global Constraints 的「重建材质资产」命令，再查日志：

```bash
grep -E "\[大气\]" "$WT/Saved/Logs/atmo-build.log"
```

Expected: `[大气] 材质已生成：/Game/Code/Planet/Materials/M_PlanetAtmosphere`。

若出现 `材质生成失败`：删掉 `Content/Code/Planet/Materials/M_PlanetAtmosphere.uasset` 再跑一次（spec §6.6）。

- [ ] **Step 4: 跑测试确认转绿**

同 Task 1 Step 3 的命令。Expected: **PASS**。

- [ ] **Step 5: 提交**

```bash
git add Source/StellarEraPlanet/Src/Atmosphere/PlanetAtmosphereMaterialBuilder.cpp Content/Code/Planet/Materials/M_PlanetAtmosphere.uasset
git commit -m "feat(大气): 壳材质改双面并关掉深度测试

球内看到的是背面，必须画；地形比壳近，必须不被它挡掉。
正反面重复混合由下一个提交的 TwoSidedSign 早退解决。

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- Source/StellarEraPlanet/Src/Atmosphere/PlanetAtmosphereMaterialBuilder.cpp Content/Code/Planet/Materials/M_PlanetAtmosphere.uasset
```

---

## Task 3: 面剔除 + 场景深度截断

**Files:**
- Modify: `Source/StellarEraPlanet/Src/Atmosphere/PlanetAtmosphereMaterialBuilder.cpp:5-15`（includes）、`:33-100`（HLSL 串）、`:204-227`（节点与 Custom 输入）
- Modify: `Source/StellarEraPlanet/Src/Tests/PlanetAtmosphereMaterialTests.cpp`（追加第二条测试）
- Regenerate: `Content/Code/Planet/Materials/M_PlanetAtmosphere.uasset`

**Interfaces:**
- Consumes: Task 1 的文件内辅助 `LoadAtmosphereMaterial()`；Task 2 的双面资产。
- Produces: Custom 节点 `Description == TEXT("PlanetAtmosphereSingleScatter")`，其 `Code` 含 `TwoSidedSign` 与 `SceneDepthCm`；Custom 输入新增两个，名字恰为 `TwoSidedSign`（标量）与 `SceneDepthCm`（float4）。

- [ ] **Step 1: 追加失败的 HLSL 断言测试**

在 `PlanetAtmosphereMaterialTests.cpp` 的 `#endif` **之前**追加，并把文件头的 include 补上 `Materials/MaterialExpressionCustom.h`：

```cpp
// HLSL 里出现这两个名字，说明生成器真的加上了面剔除与深度截断，
// 而不只是把材质开关改了。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraAtmosphereMaterialHlslTest,
	"StellarEra.Planet.AtmosphereMaterial.Hlsl",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraAtmosphereMaterialHlslTest::RunTest(const FString& Parameters)
{
	UMaterial* Material = LoadAtmosphereMaterial();
	if (!TestNotNull(TEXT("材质资产"), Material))
	{
		return false;
	}

	UMaterialExpressionCustom* Custom = nullptr;
	for (UMaterialExpression* Expr : Material->GetExpressionCollection().Expressions)
	{
		UMaterialExpressionCustom* Candidate = Cast<UMaterialExpressionCustom>(Expr);
		if (Candidate && Candidate->Description == TEXT("PlanetAtmosphereSingleScatter"))
		{
			Custom = Candidate;
			break;
		}
	}

	if (!TestNotNull(TEXT("Custom 节点 PlanetAtmosphereSingleScatter"), Custom))
	{
		return false;
	}

	TestTrue(TEXT("HLSL 含面剔除"), Custom->Code.Contains(TEXT("TwoSidedSign")));
	TestTrue(TEXT("HLSL 含场景深度截断"), Custom->Code.Contains(TEXT("SceneDepthCm")));

	return true;
}
```

- [ ] **Step 2: 构建 + 跑测试确认失败**

先构建，再跑测试，查日志：

```bash
grep -i "atmospherematerial" "$WT/Saved/Logs/atmo-tests.log"
```

Expected: `Structure` PASS，`Hlsl` **FAIL**（两处 `Contains` 都是 false）。

- [ ] **Step 3: 加三个 include**

`PlanetAtmosphereMaterialBuilder.cpp` 的 include 块改成（新增三条，位置按字典序）：

```cpp
#include "HAL/IConsoleManager.h"
#include "MaterialSceneTextureId.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCameraPositionWS.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionSceneTexture.h"
#include "Materials/MaterialExpressionTwoSidedSign.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
```

- [ ] **Step 4: 改 HLSL 串**

在 `AtmosphereHLSL` 里，紧跟 `float3 rd = normalize(WorldPos - CameraPos);` 之后插入面剔除：

```hlsl
// 双面材质下，同一屏幕位置的正反面各出一个片元。不剔掉多余的一面，
// 大气会被混合两遍（颜色约翻倍、透射率平方），ray march 也白跑一遍。
// 符号约定：正面 +1、背面 -1（MaterialTemplate.ush 的 GetFloatFacingSign，D3D 分支）。
// 相机在壳内就要背面，在壳外就要正面。
float sideWanted = (length(ro - PlanetCenter) < ShellRadius) ? -1.0 : 1.0;
if (TwoSidedSign * sideWanted < 0.0)
{
    return float4(0.0, 0.0, 0.0, 0.0);
}
```

然后在"撞到行星地面就停在表面"那段结束（第二个 `if (tExit <= tEnter)` 块）之后、相位函数之前，插入深度截断：

```hlsl
// 关掉深度测试后，比壳更近的物体（飞船、水面）也会被这片片元覆盖。
// SceneDepthCm 是视空间 Z（沿相机前向的距离），除以射线与相机前向的夹角余弦
// 才是射线上的距离 —— 引擎自己也是这么换算的（MaterialTemplate.ush 里
// PixelDepthOffset 那段就是 dot(CameraPixelForwardVector, ResolvedView.ViewForward)）。
// 兜底：天空像素的深度是远平面，tScene 会大于 tExit，此时不截断。
float tScene = SceneDepthCm.r / max(dot(rd, ResolvedView.ViewForward), 0.01);
if (tScene > 0.0 && tScene < tExit)
{
    tExit = tScene;
}
if (tExit <= tEnter)
{
    return float4(0.0, 0.0, 0.0, 0.0);
}
```

> `SceneDepthCm` 必须取 `.r`：`UMaterialExpressionSceneTexture` 的输出是 float4，Custom 输入按 `MCT_Float4` 生成 `MaterialFloat4`，直接当 `float` 用会触发隐式截断。

- [ ] **Step 5: 建两个节点并接到 Custom**

在 `SunIntensity` 创建之后、`// --- Custom 节点 ---` 之前插入：

```cpp
	// 双面材质要靠它区分正反面（正面 +1 / 背面 -1）。
	UMaterialExpressionTwoSidedSign* TwoSidedSignNode =
		NewObject<UMaterialExpressionTwoSidedSign>(Material);
	TwoSidedSignNode->Material = Material;
	TwoSidedSignNode->MaterialExpressionEditorX = -800;
	TwoSidedSignNode->MaterialExpressionEditorY = 600;
	Material->GetExpressionCollection().AddExpression(TwoSidedSignNode);

	// 场景深度：视空间 Z，世界单位 cm。
	UMaterialExpressionSceneTexture* SceneDepthNode =
		NewObject<UMaterialExpressionSceneTexture>(Material);
	SceneDepthNode->Material = Material;
	SceneDepthNode->SceneTextureId = PPI_SceneDepth;
	SceneDepthNode->bFiltered = false;
	SceneDepthNode->MaterialExpressionEditorX = -800;
	SceneDepthNode->MaterialExpressionEditorY = 700;
	Material->GetExpressionCollection().AddExpression(SceneDepthNode);
```

在 `AddCustomInput(Custom, TEXT("SunIntensity"), SunIntensity);` 之后追加两行：

```cpp
	AddCustomInput(Custom, TEXT("TwoSidedSign"), TwoSidedSignNode);
	AddCustomInput(Custom, TEXT("SceneDepthCm"), SceneDepthNode);
```

- [ ] **Step 6: 构建**

Expected: 成功。若报 `MaterialSceneTextureId.h` 找不到（它在 `Engine/Public/` 根，不在 `Materials/` 下），用
`ls "/d/Application/Epic Games/UE_5.8/Engine/Source/Runtime/Engine/Public/" | grep -i scenetexture` 确认实际位置后再调整 include。

- [ ] **Step 7: 重新生成资产并跑测试**

重建材质（Global Constraints 第三条命令），再跑测试。Expected: `Structure` 与 `Hlsl` 都 **PASS**。

重建日志里还要确认材质编译没报错：

```bash
grep -iE "LogMaterial: .*([Ee]rror|[Ff]ailed)" "$WT/Saved/Logs/atmo-build.log" | head
```

Expected: 无输出。

- [ ] **Step 8: 提交**

```bash
git add Source/StellarEraPlanet/Src/Atmosphere/PlanetAtmosphereMaterialBuilder.cpp Source/StellarEraPlanet/Src/Tests/PlanetAtmosphereMaterialTests.cpp Content/Code/Planet/Materials/M_PlanetAtmosphere.uasset
git commit -m "feat(大气): 壳 HLSL 加面剔除与场景深度截断

双面后正反面各出一个片元，按 TwoSidedSign 只留需要的一面；
关掉深度测试后靠 SceneDepth 把积分截到真实几何，顺带得到空中透视。

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- Source/StellarEraPlanet/Src/Atmosphere/PlanetAtmosphereMaterialBuilder.cpp Source/StellarEraPlanet/Src/Tests/PlanetAtmosphereMaterialTests.cpp Content/Code/Planet/Materials/M_PlanetAtmosphere.uasset
```

---

## Task 4: 组件排序优先级 + 过期注释

**Files:**
- Modify: `Source/StellarEraPlanet/Src/Atmosphere/PlanetAtmosphereComponent.cpp:50-56`
- Modify: `Source/StellarEraPlanet/Src/Atmosphere/PlanetAtmosphereComponent.h:9-25`、`:34`

**Interfaces:**
- Consumes: 无。
- Produces: `TranslucencySortPriority == -10`（可从 CDO 读到）。无新 API。

- [ ] **Step 1: 改排序优先级**

`PlanetAtmosphereComponent.cpp`，把

```cpp
	TranslucencySortPriority = -1;
```

改成

```cpp
	// -10 而不是 -1：材质现在 bDisableDepthTest = true，深度测试不再替我们排序，
	// 绘制顺序完全由这里决定。壳是球内天空的"底"，必须排在水面、矿物等半透明之前
	// （即 priority 更低）。-1 与常见的 0 太接近，容易被别的半透明抢在前面。
	TranslucencySortPriority = -10;
```

- [ ] **Step 2: 改过期注释**

`PlanetAtmosphereComponent.h` 的类注释，把

```cpp
 * 一个套在行星外的半透明球，像素着色器里做单次散射 ray march。
 * 球面法线朝外，所以：
 *   - 相机在球外（太空）→ 看到正面 → 壳渲染，这就是"大气辉光"
 *   - 相机在球内（行星表面）→ 看到背面 → 被背面剔除 → 壳不渲染
 * 球内的天空交给引擎原生 SkyAtmosphere（由 UCosmosSimulationSubsystem 重定位），
 * 两者天然不打架，不需要手工切可见性。
```

改成

```cpp
 * 一个套在行星外的半透明球，像素着色器里做单次散射 ray march。
 * 材质是**双面**的，内外都渲染，由同一个 ray march 服务：
 *   - 相机在球外（太空）→ 画正面 → 这就是"大气辉光"
 *   - 相机在球内（行星表面）→ 画背面 → 这就是天空
 * 射线的进/出交点本来就按相机位置算，相机在壳内时入射点退化为 0，
 * 所以穿壳边界天然连续，不需要手工切可见性。
 *
 * 材质 bDisableDepthTest = true，遮挡不靠深度测试，靠 HLSL 里用场景深度
 * 截断积分 —— 顺带把远处地形压暗混色，得到空中透视。
 * 关卡里没有 ASkyAtmosphere，球内天空完全由本组件负责。
```

`AtmosphereHeightRatio` 的注释，把

```cpp
	/** 壳厚度 / 行星表面半径。**必须**与引擎大气用的值一致，否则穿过壳边界会跳变。 */
```

改成

```cpp
	/** 壳厚度 / 行星表面半径。决定辉光与天空的"厚度"，与瑞利/米氏标高一起决定地平线颜色。 */
```

- [ ] **Step 3: 核对 BOM**

```bash
head -c 3 Source/StellarEraPlanet/Src/Atmosphere/PlanetAtmosphereComponent.h | xxd
head -c 3 Source/StellarEraPlanet/Src/Atmosphere/PlanetAtmosphereComponent.cpp | xxd
```

Expected: 两行都以 `efbbbf` 开头。缺了用 `sed -i '1s/^/\xef\xbb\xbf/'` 补回。

- [ ] **Step 4: 构建 + 跑测试**

构建，再跑 `StellarEra.Planet.Atmosphere` 全组。Expected: 现有的 `AtmosphereParams.*` 4 条 + 新增的 `AtmosphereMaterial.*` 2 条全部 PASS。

- [ ] **Step 5: 提交**

```bash
git add Source/StellarEraPlanet/Src/Atmosphere/PlanetAtmosphereComponent.cpp Source/StellarEraPlanet/Src/Atmosphere/PlanetAtmosphereComponent.h
git commit -m "refactor(大气): 壳排序优先级改 -10，清掉已失效的注释

关深度测试后绘制顺序只由排序决定，壳必须排在其它半透明之前。
类注释里“球内交给引擎 SkyAtmosphere”已经不成立。

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- Source/StellarEraPlanet/Src/Atmosphere/PlanetAtmosphereComponent.cpp Source/StellarEraPlanet/Src/Atmosphere/PlanetAtmosphereComponent.h
```

---

## Task 5: PIE 人工验收

**Files:** 无（只读验证）

**Interfaces:**
- Consumes: Task 2–4 的全部产出。
- Produces: 验收记录（写进任务完成后的回复里，不落文件）。

- [ ] **Step 1: 打开 worktree 的工程并进 UniverseMap**

编辑器打开的是 **worktree 的** `StellarEra.uproject`（不是主仓的），加载 `UniverseMap`。
注意 `NewMap` 没有体素行星，验不了这条路径。

- [ ] **Step 2: 逐条过 spec §9**

1. 站在行星表面抬头：不是黑的，有可见的天空颜色。
2. 绕太阳转一圈：天空亮侧跟着太阳，背阳侧明显更暗。
3. 地平线附近比天顶更亮、更白。
4. 远处地形轻微发白、近处清晰，远近过渡连续，没有一圈硬边。
5. 从太空看三颗行星：辉光与改动前一致，边缘无硬边。
6. 飞船停在相机与行星之间时，飞船不被雾糊住。
7. 相机从球内升到球外穿壳边界：无跳变、无闪。
8. 编辑器日志无 `LogMaterial` 编译错误、无 ensure。

- [ ] **Step 3: 记性能**

球内与球外各看一次 `stat unit`，把 `Frame` 数值记下来（不预设阈值）。若球内明显掉帧，备选：`ATMO_STEPS` 16 → 12。

- [ ] **Step 4: 有任何一条不过就停下来修**

不要把失败的验收项当成"以后再说"提交。天空是黑的这类问题，最可能是面剔除的符号写反了 —— 先把 `sideWanted` 取反试一次，比读代码快。

---

## 自检记录

- spec §6.1 → Task 2；§6.2 → Task 3 Step 5；§6.3 → Task 3 Step 4；§6.5 → Task 4；§6.6 → Task 2 Step 3 / Task 3 Step 7；§9 → Task 5；§10 的自动化 → Task 1 + Task 3 Step 1。
- spec §10 写"7 个参数名齐全"，实际是 **9** 个（4 标量 + 5 向量）。已在 spec 里改正，本计划按 9 写。
- spec §6.2 说 Custom 输入 `SceneDepthCm` 是 `float`。实际节点输出 float4，所以 HLSL 里必须写 `SceneDepthCm.r`；本计划已按 `.r` 写，并在 Task 3 Step 4 的注里说明原因。
