# 高速飞行 LOD 切换叠影优化 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** 把「飞船 LOD1 细气泡 ↔ 全星 LOD8」的 7 级落差拆成 1→3→5→8 三级，缩短 LOD 切换的叠影窗口，并把每帧网格应用预算压到不再卡满一秒。

**Architecture:** 不动 VoxelFree 插件。在 `ASpaceshipPawn` 构造函数里加两个只做 LOD 的中间环调用器（LOD3 @ 600 m、LOD5 @ 1400 m，带预测），在 `AVoxelPlanetActor` 构造函数里把 `ChunksDitheringDuration` 调到 0.3 s、`MeshUpdatesBudget` 调到 8 ms。全部是插件既有机制（最细调用器胜出、dither 过渡、每帧预算）的参数化，不新增 LOD 管理逻辑。

**Tech Stack:** Unreal Engine 5.8 C++、VoxelFree 插件（只读，不修改）、UE Automation Test。

**Spec:** `docs/superpowers/specs/2026-10-21-lod-transition-optimization-design.md`

## Global Constraints

- 只改这四个文件：
  - `Source/StellarEraVehicle/Public/SpaceshipPawn.h`
  - `Source/StellarEraVehicle/Private/SpaceshipPawn.cpp`
  - `Source/StellarEraPlanet/Private/VoxelPlanetActor.cpp`
  - 两个新测试文件：`Source/StellarEraVehicle/Private/SpaceshipLODInvokerTests.cpp`、`Source/StellarEraPlanet/Private/PlanetLODTransitionTests.cpp`
- **不碰** `Plugins/VoxelFree`、`Plugins/VibeUE`；不碰 `Content/` 下任何资产；**不提交、不推送**（用户未要求）。完成后只报告验证证据。
- `StellarEraVehicle` 不能依赖 `StellarEra`（会成环）；本次不加任何新模块依赖。
- 新建的两个 `.cpp` 含中文，必须存 **UTF-8 with BOM**。修改的 `.cpp`/`.h` 事后要复核 BOM 仍在：
  ```bash
  head -c 3 <文件> | od -An -tx1    # 期望 ef bb bf
  ```
  `VoxelPlanetActor.cpp` 目前**没有** BOM（历史遗留），本次编辑后补上。
- 不要统一换行符；只做局部插入。
- 构建前**必须关闭编辑器**（Live Coding 会锁 DLL）。UBT 直调、cwd 在项目外：
  ```bash
  cd "$TEMP" && "D:/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" \
    "D:/Application/Epic Games/UE_5.8/Engine/Binaries/DOTNET/UnrealBuildTool/UnrealBuildTool.dll" \
    StellarEraEditor Win64 Development \
    -Project="F:/workspace/ue/StellarEra/StellarEra.uproject" -WaitMutex -FromMsBuild
  ```
  成功判据：退出码 0 且日志出现 `Result: Succeeded`（`Result: Failed` 也可能是「编译成功、只有链接失败」，以退出码为准）。
- 自动化测试（编辑器开着也能跑；editor world 不跑 BeginPlay，读 CDO 正好）：
  ```bash
  cd "$TEMP" && "D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
    "F:/workspace/ue/StellarEra/StellarEra.uproject" \
    -ExecCmds="Automation RunTests <前缀>" \
    -TestExit="Automation Test Queue Empty" \
    -unattended -nopause -nullrhi -nosplash -stdout -abslog="$TEMP/<日志名>.log"
  ```
  结果用 `grep -aE "Result=\{(Success|Failed)\}" <日志>` 读，不要只看退出码。
- 所有数值来自 spec §3/§4，**不要自行改动**（尤其是细气泡 `PredictionTime = 0.03` 和环的 `bUseForCollisions = false`）。

---

## File Structure

| 文件 | 改动 | 责任 |
|---|---|---|
| `Source/StellarEraVehicle/Public/SpaceshipPawn.h` | 加 2 个 `TObjectPtr<UVoxelInvokerWithPredictionComponent>` 成员 | 让环调用器在 CDO 上可被查找/序列化 |
| `Source/StellarEraVehicle/Private/SpaceshipPawn.cpp` | 构造函数加 2 个环调用器 | LOD 阶梯 1→3→5→8 |
| `Source/StellarEraVehicle/Private/SpaceshipLODInvokerTests.cpp` | 新建 | 锁住三个调用器的配置（含碰撞关闭） |
| `Source/StellarEraPlanet/Private/VoxelPlanetActor.cpp` | 构造函数加默认值 | 缩短 dither 窗口、限制每帧网格预算 |
| `Source/StellarEraPlanet/Private/PlanetLODTransitionTests.cpp` | 新建 | 锁住过渡时间与预算默认值 |

---

### Task 1: 飞船中间 LOD 环

**Files:**
- Modify: `Source/StellarEraVehicle/Public/SpaceshipPawn.h:287-288`（`VoxelInvoker` 声明之后）
- Modify: `Source/StellarEraVehicle/Private/SpaceshipPawn.cpp:145-156`（`VoxelInvoker` 配置块之后）
- Test: `Source/StellarEraVehicle/Private/SpaceshipLODInvokerTests.cpp`（新建）

**Interfaces:**
- Consumes: `UVoxelInvokerWithPredictionComponent`（已 include）、`Hull`（组件，已在构造函数更早处创建）、`ASpaceshipPawn::GetVelocity()` 返回世界速度（预测用）。
- Produces: CDO 上三个默认子对象，名字固定为 `VoxelInvoker`、`VoxelInvokerLOD3`、`VoxelInvokerLOD5`；Task 3 的运行时验证依赖这三个名字。

- [x] **Step 1: 写失败测试**

新建 `Source/StellarEraVehicle/Private/SpaceshipLODInvokerTests.cpp`：

```cpp
// 飞船体素调用器配置的回归测试。
//
// 飞行中「不同 LOD 的网格重叠」由三段配置共同决定：
//   1. 行星调用器把整颗星压在 LOD 8（叶块 8.2 公里）；
//   2. 飞船只有 LOD 1 细气泡（240 米），与 LOD 8 之间落差 7 级；
//   3. 插件在旧块被替代后还要淡出 2 × ChunksDitheringDuration。
// 这里锁住第 2 段：细气泡 + 两个中间环，且环只做 LOD、不碰碰撞。
//
// 断言的是构造函数产出的 CDO 配置，不是「能编译」。
// 运行期生成的是 BP_SpaceshipPawn_C，所以直接加载蓝图再查组件。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "SpaceshipPawn.h"
#include "VoxelComponents/VoxelInvokerComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraSpaceshipLODInvokerTest,
	"StellarEra.Vehicle.SpaceshipLODInvokers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraSpaceshipLODInvokerTest::RunTest(const FString& Parameters)
{
	UClass* ShipClass = LoadClass<ASpaceshipPawn>(
		nullptr, TEXT("/Game/Code/BP_SpaceshipPawn.BP_SpaceshipPawn_C"));
	if (!TestNotNull(TEXT("BP_SpaceshipPawn_C 可加载"), ShipClass))
	{
		return false;
	}

	ASpaceshipPawn* CDO = Cast<ASpaceshipPawn>(ShipClass->GetDefaultObject());
	if (!TestNotNull(TEXT("BP_SpaceshipPawn CDO"), CDO))
	{
		return false;
	}

	const auto FindInvoker = [CDO](const TCHAR* Name) -> const UVoxelInvokerWithPredictionComponent*
	{
		return Cast<UVoxelInvokerWithPredictionComponent>(CDO->GetDefaultSubobjectByName(Name));
	};

	const UVoxelInvokerWithPredictionComponent* Fine = FindInvoker(TEXT("VoxelInvoker"));
	const UVoxelInvokerWithPredictionComponent* Ring3 = FindInvoker(TEXT("VoxelInvokerLOD3"));
	const UVoxelInvokerWithPredictionComponent* Ring5 = FindInvoker(TEXT("VoxelInvokerLOD5"));

	if (!TestNotNull(TEXT("细气泡 VoxelInvoker"), Fine) ||
		!TestNotNull(TEXT("中间环 VoxelInvokerLOD3"), Ring3) ||
		!TestNotNull(TEXT("中间环 VoxelInvokerLOD5"), Ring5))
	{
		return false;
	}

	// 细气泡：LOD 1 @ 240 米，带碰撞。PredictionTime 不能再大 ——
	// 碰撞范围 120 米，极速 3.6e5 cm/s × 0.03 s = 108 米，已经贴边。
	TestEqual(TEXT("细气泡 LODToSet"), Fine->LODToSet, 1);
	TestEqual(TEXT("细气泡 LODRange（cm）"), Fine->LODRange, 24000.f);
	TestTrue(TEXT("细气泡负责碰撞"), Fine->bUseForCollisions);
	TestTrue(TEXT("细气泡启用预测"), Fine->bEnablePrediction);
	TestEqual(TEXT("细气泡 PredictionTime（秒）"), Fine->PredictionTime, 0.03f, 1e-4f);

	// 中间环：只做 LOD。碰撞必须关 —— 环的范围在 Height 0 下会请求数万个区块。
	TestEqual(TEXT("LOD3 环 LODToSet"), Ring3->LODToSet, 3);
	TestEqual(TEXT("LOD3 环 LODRange（cm）"), Ring3->LODRange, 60000.f);
	TestFalse(TEXT("LOD3 环不负责碰撞"), Ring3->bUseForCollisions);
	TestFalse(TEXT("LOD3 环不负责导航"), Ring3->bUseForNavmesh);
	TestFalse(TEXT("LOD3 环不接事件"), Ring3->bUseForEvents);
	TestFalse(TEXT("LOD3 环不参与优先级"), Ring3->bUseForPriorities);
	TestTrue(TEXT("LOD3 环启用预测"), Ring3->bEnablePrediction);
	TestEqual(TEXT("LOD3 环 PredictionTime（秒）"), Ring3->PredictionTime, 0.1f, 1e-4f);

	TestEqual(TEXT("LOD5 环 LODToSet"), Ring5->LODToSet, 5);
	TestEqual(TEXT("LOD5 环 LODRange（cm）"), Ring5->LODRange, 140000.f);
	TestFalse(TEXT("LOD5 环不负责碰撞"), Ring5->bUseForCollisions);
	TestFalse(TEXT("LOD5 环不负责导航"), Ring5->bUseForNavmesh);
	TestFalse(TEXT("LOD5 环不接事件"), Ring5->bUseForEvents);
	TestFalse(TEXT("LOD5 环不参与优先级"), Ring5->bUseForPriorities);
	TestTrue(TEXT("LOD5 环启用预测"), Ring5->bEnablePrediction);
	TestEqual(TEXT("LOD5 环 PredictionTime（秒）"), Ring5->PredictionTime, 0.2f, 1e-4f);

	return true;
}

#endif
```

- [x] **Step 2: 给新文件加 BOM**

```bash
f=Source/StellarEraVehicle/Private/SpaceshipLODInvokerTests.cpp
if [ "$(head -c 3 "$f" | od -An -tx1 | tr -d ' \n')" != "efbbbf" ]; then
  printf '\xEF\xBB\xBF' > "$f.tmp" && cat "$f" >> "$f.tmp" && mv "$f.tmp" "$f"
fi
head -c 3 "$f" | od -An -tx1   # 期望 ef bb bf
```

- [x] **Step 3: 构建**

用 Global Constraints 里的 UBT 命令。期望退出码 0。

- [x] **Step 4: 跑测试，确认失败**

```bash
cd "$TEMP" && "D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra.Vehicle.SpaceshipLODInvokers" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout -abslog="$TEMP/StellarEra-ShipLODInvokers.log"
grep -aE "Result=\{(Success|Failed)\}" "$TEMP/StellarEra-ShipLODInvokers.log" | tail -5
```

期望：出现 `Result={Failed}`，日志里有 `中间环 VoxelInvokerLOD3` / `VoxelInvokerLOD5` 的 NotNull 失败。若测试名都没跑起来（0 条 Result），说明测试没编译进去，回到 Step 3 查构建日志。

- [x] **Step 5: 实现头文件声明**

在 `SpaceshipPawn.h` 里 `VoxelInvoker` 的 `UPROPERTY` 块之后插入：

```cpp
	/**
	 * 中间 LOD 环：把「LOD1 细气泡 → LOD8 全星」的 7 级落差拆成三级。
	 *
	 * 只有细气泡时，紧挨着细气泡的就是 8.2 公里的粗块；插件在
	 * WaitingForNewChunks 阶段故意保留旧块直到替代块全部完成
	 * （VoxelDefaultRenderer.cpp:464），再用 2 × ChunksDitheringDuration 淡出，
	 * 于是 8 公里级面片会横穿细地形 —— 高速飞行的「大片白幕」就是这么来的。
	 *
	 * 只做 LOD。碰撞交给细气泡：碰撞细分不看 LODToSet，一路到 Height 0，
	 * 600 米的盒子会请求约 5.5e4 个区块，足以顶爆显存。
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Spaceship")
	TObjectPtr<UVoxelInvokerWithPredictionComponent> VoxelInvokerLOD3;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Spaceship")
	TObjectPtr<UVoxelInvokerWithPredictionComponent> VoxelInvokerLOD5;
```

- [x] **Step 6: 实现构造函数**

在 `SpaceshipPawn.cpp` 中 `VoxelInvoker->PredictionTime = 0.03f;` 之后插入：

```cpp
	// ---- 中间 LOD 环：见头文件声明处的说明 ----
	//
	// 预测时间受硬约束「范围 ≥ 最大速度 × PredictionTime」，最难情况是
	// BoostMultiplier 全开 3.6e5 厘米/秒：
	//   - LOD3 @ 0.1 秒：偏移 3.6e4 < 范围 6e4；
	//   - LOD5 @ 0.2 秒：偏移 7.2e4 < 范围 1.4e5。
	// 细气泡保持 0.03 秒：它的碰撞范围只有 1.2e4 厘米，0.03 秒时偏移 1.08e4
	// 已经贴边，再调大飞船就会在极速下掉出自己的碰撞盒。
	VoxelInvokerLOD3 = CreateDefaultSubobject<UVoxelInvokerWithPredictionComponent>(TEXT("VoxelInvokerLOD3"));
	VoxelInvokerLOD3->SetupAttachment(Hull);
	VoxelInvokerLOD3->bUseForLOD = true;
	VoxelInvokerLOD3->LODToSet = 3;
	VoxelInvokerLOD3->LODRange = 60000.f;
	VoxelInvokerLOD3->bUseForCollisions = false;
	VoxelInvokerLOD3->bUseForNavmesh = false;
	VoxelInvokerLOD3->bUseForEvents = false;
	VoxelInvokerLOD3->bUseForPriorities = false;
	VoxelInvokerLOD3->bEnablePrediction = true;
	VoxelInvokerLOD3->PredictionTime = 0.1f;

	VoxelInvokerLOD5 = CreateDefaultSubobject<UVoxelInvokerWithPredictionComponent>(TEXT("VoxelInvokerLOD5"));
	VoxelInvokerLOD5->SetupAttachment(Hull);
	VoxelInvokerLOD5->bUseForLOD = true;
	VoxelInvokerLOD5->LODToSet = 5;
	VoxelInvokerLOD5->LODRange = 140000.f;
	VoxelInvokerLOD5->bUseForCollisions = false;
	VoxelInvokerLOD5->bUseForNavmesh = false;
	VoxelInvokerLOD5->bUseForEvents = false;
	VoxelInvokerLOD5->bUseForPriorities = false;
	VoxelInvokerLOD5->bEnablePrediction = true;
	VoxelInvokerLOD5->PredictionTime = 0.2f;
```

- [x] **Step 7: 复核 BOM 与换行符**

```bash
for f in Source/StellarEraVehicle/Public/SpaceshipPawn.h Source/StellarEraVehicle/Private/SpaceshipPawn.cpp; do
  printf '%s: ' "$f"; head -c 3 "$f" | od -An -tx1 | tr -d ' \n'; echo
done
```

期望两行都是 `efbbbf`。若被编辑器工具重写成无 BOM，按 Step 2 的方法补回。

- [x] **Step 8: 构建**

用 Global Constraints 里的 UBT 命令。期望退出码 0。

- [x] **Step 9: 跑测试，确认通过**

```bash
cd "$TEMP" && "D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra.Vehicle.SpaceshipLODInvokers" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout -abslog="$TEMP/StellarEra-ShipLODInvokers.log"
grep -aE "Result=\{(Success|Failed)\}" "$TEMP/StellarEra-ShipLODInvokers.log" | tail -5
```

期望：只有 `Result={Success}`，没有 `Result={Failed}`。

**排障：** 若 `VoxelInvokerLOD3` 在 BP CDO 上仍为 null —— 打开编辑器，编译并保存
`/Game/Code/BP_SpaceshipPawn`，再重跑本步。原生默认子对象由基类构造函数创建，
通常无需重编蓝图；这条只是兜底。

---

### Task 2: 行星 LOD 过渡设置

**Files:**
- Modify: `Source/StellarEraPlanet/Private/VoxelPlanetActor.cpp:46-47`（`NumberOfThreads` / `MaxLOD` 之后）
- Test: `Source/StellarEraPlanet/Private/PlanetLODTransitionTests.cpp`（新建）

**Interfaces:**
- Consumes: `AVoxelWorld::ChunksDitheringDuration`、`AVoxelWorld::MeshUpdatesBudget`（插件公开 UPROPERTY，默认 1 / 1000）。
- Produces: `AVoxelPlanetActor` CDO 的新默认值（0.3 / 8），关卡里所有未覆盖该属性的行星实例都会继承。

- [x] **Step 1: 写失败测试**

新建 `Source/StellarEraPlanet/Private/PlanetLODTransitionTests.cpp`：

```cpp
// 行星 LOD 过渡设置回归测试。
//
// ChunksDitheringDuration 决定旧 LOD 块在替换块完成后再淡出多久
// （VoxelDefaultRenderer.cpp:884/900：同 LOD 用 1×，换 LOD 用 2×）。
// 默认 1 秒 ⇒ 换 LOD 时两套网格共存 2 秒；高速飞行时叠影肉眼可见。
//
// MeshUpdatesBudget 是每帧应用已完成块网格的毫秒预算（只在 Game play type
// 生效，编辑器预览固定 1000）。默认 1000 毫秒意味着一批块同时完成时游戏线程
// 可以卡满一秒 —— 高速飞行「卡一下」的直接来源。
//
// 断言构造函数产出的默认值，防止有人把它们改回插件的默认。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "VoxelPlanetActor.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraPlanetLODTransitionSettingsTest,
	"StellarEra.Planet.LODTransitionSettings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraPlanetLODTransitionSettingsTest::RunTest(const FString& Parameters)
{
	AVoxelPlanetActor* CDO = GetMutableDefault<AVoxelPlanetActor>();
	if (!TestNotNull(TEXT("AVoxelPlanetActor CDO"), CDO))
	{
		return false;
	}

	TestEqual(TEXT("ChunksDitheringDuration（秒）"), CDO->ChunksDitheringDuration, 0.3f, 1e-4f);
	TestEqual(TEXT("MeshUpdatesBudget（毫秒）"), CDO->MeshUpdatesBudget, 8.f, 1e-4f);

	return true;
}

#endif
```

- [x] **Step 2: 给新文件加 BOM**

```bash
f=Source/StellarEraPlanet/Private/PlanetLODTransitionTests.cpp
if [ "$(head -c 3 "$f" | od -An -tx1 | tr -d ' \n')" != "efbbbf" ]; then
  printf '\xEF\xBB\xBF' > "$f.tmp" && cat "$f" >> "$f.tmp" && mv "$f.tmp" "$f"
fi
head -c 3 "$f" | od -An -tx1   # 期望 ef bb bf
```

- [x] **Step 3: 构建**

用 Global Constraints 里的 UBT 命令。期望退出码 0。

- [x] **Step 4: 跑测试，确认失败**

```bash
cd "$TEMP" && "D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra.Planet.LODTransitionSettings" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout -abslog="$TEMP/StellarEra-PlanetLODSettings.log"
grep -aE "Result=\{(Success|Failed)\}" "$TEMP/StellarEra-PlanetLODSettings.log" | tail -5
```

期望：`Result={Failed}`，两条 TestEqual 的实测值分别为 1 和 1000。

- [x] **Step 5: 实现构造函数默认值**

在 `VoxelPlanetActor.cpp` 的 `MaxLOD = 6;` 之后插入：

```cpp
	// LOD 切换的过渡时长（基类默认 1 秒）：插件在旧块被新块完全替代后，要等
	// 2 × 这个值才把旧块淡出（VoxelDefaultRenderer.cpp:900），期间两套不同 LOD
	// 的网格同时可见。1 秒 ⇒ 2 秒的叠影窗口对高速飞行太长；0.3 秒压到 0.6 秒。
	ChunksDitheringDuration = 0.3f;

	// 每帧应用已完成块网格的时间预算（毫秒）。基类默认 1000 毫秒，意味着一批
	// 块同时完成时游戏线程可以卡满一整秒 —— 高速飞行「卡一下」的直接来源。
	// 8 毫秒把这一批分摊到多帧；代价是 LOD 收敛慢几帧。
	// 只在 Game play type 生效；编辑器预览由插件固定为 1000。
	MeshUpdatesBudget = 8.f;
```

- [x] **Step 6: 补 BOM 并复核**

`VoxelPlanetActor.cpp` 当前没有 BOM（含中文，违反项目约定），本次一并补上：

```bash
f=Source/StellarEraPlanet/Private/VoxelPlanetActor.cpp
if [ "$(head -c 3 "$f" | od -An -tx1 | tr -d ' \n')" != "efbbbf" ]; then
  printf '\xEF\xBB\xBF' > "$f.tmp" && cat "$f" >> "$f.tmp" && mv "$f.tmp" "$f"
fi
head -c 3 "$f" | od -An -tx1   # 期望 ef bb bf
```

- [x] **Step 7: 构建**

用 Global Constraints 里的 UBT 命令。期望退出码 0。

- [x] **Step 8: 跑测试，确认通过**

```bash
cd "$TEMP" && "D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra.Planet.LODTransitionSettings" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout -abslog="$TEMP/StellarEra-PlanetLODSettings.log"
grep -aE "Result=\{(Success|Failed)\}" "$TEMP/StellarEra-PlanetLODSettings.log" | tail -5
```

期望：只有 `Result={Success}`。

---

### Task 3: 全量回归 + 运行时确认

**Files:**
- 无代码改动（只验证）。

- [x] **Step 1: 跑全量 `StellarEra` 套件**

```bash
cd "$TEMP" && "D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout -abslog="$TEMP/StellarEra-AllTests-LOD.log"
grep -ac "Result={Success}" "$TEMP/StellarEra-AllTests-LOD.log"
grep -aE "Result=\{Failed\}" "$TEMP/StellarEra-AllTests-LOD.log" | tail -10
```

期望：Success 计数 = 34（原 32 + 新增 2），`Result={Failed}` 输出为空。

- [x] **Step 2: 确认关卡里的行星实例继承新默认值**

打开编辑器（`UniverseMap`），对每颗 `AVoxelPlanetActor` 实例检查
`ChunksDitheringDuration` / `MeshUpdatesBudget`。若任一实例显示 1 / 1000
（被蓝图或关卡实例序列化覆盖），在 `BP_VoxelPlanet` 的类默认值里改成
0.3 / 8 并保存 —— 否则运行时仍走旧值。

- [ ] **Step 3: PIE 飞行验证（手动，待用户执行）**

在 `UniverseMap` PIE，**先把编辑器切到前台**（后台整进程限 3 FPS，体素生成
慢三个数量级，测出来的卡顿没有意义），登船后加速飞越行星表面：

- 起飞与巡航时不再出现横穿地形的 8 公里级白色面片；
- LOD 过渡的条纹带存在时间明显缩短（≤0.6 秒量级）；
- 不再有约 1 秒的整帧卡死；
- HUD `Total tasks remaining` 峰值可控（不应到几万），能快速排空。

把观察结果（最好附截图日志行）报告给用户；PIE 不改文件，不需要保存关卡。

---

## Self-Review

**1. Spec coverage（逐条对照 spec）**

| Spec 条目 | 任务 |
|---|---|
| ① 中间 LOD 环（LOD3/LOD5、只做 LOD） | Task 1 Step 5-6 |
| ② 环预测 0.1/0.2 s，细气泡保持 0.03 s | Task 1 Step 6 + 测试断言 |
| ③ `ChunksDitheringDuration` 0.3 s | Task 2 Step 5 |
| ④ `MeshUpdatesBudget` 8 ms | Task 2 Step 5 |
| 不改插件 / 不改行星 LOD8 | Global Constraints + 文件清单 |
| 验证口径（自动化 + PIE） | Task 3 |

**2. Placeholder scan：** 无 TBD/TODO；所有代码、命令、期望输出均为实际内容。

**3. Type consistency：** 组件名 `VoxelInvokerLOD3` / `VoxelInvokerLOD5` 在头文件、
构造函数、测试三处一致；属性名 `ChunksDitheringDuration` / `MeshUpdatesBudget`
与插件 `VoxelWorld.h` 中的 UPROPERTY 一致；测试用 `GetMutableDefault` +
`GetDefaultSubobjectByName`，与现有 `SpaceshipCollisionTests.cpp` 的取法一致。
