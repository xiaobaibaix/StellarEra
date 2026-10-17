# 星图模式 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 加一个按 M 进出的星图模式：鼠标点击选中一颗天体，左键拖拽绕它转视角，滚轮缩放，并在这个模式下才显示轨迹（选中的那颗高亮）。

**Architecture:** 进入星图时 `Possess` 一个新建的相机 Pawn —— 控制器里现有的 15 个输入回调里，13 个按 Pawn 类型分派、换 Pawn 后自动失效；剩下 2 个（右键按下/松开的 `OnLookActivateStarted`/`Completed`，只翻转 `bLookHeld`）不读 Pawn 类型，但 `bLookHeld` 唯一的读点在 `OnLook` 的角色分支里、且换 Pawn 时会被清零，所以同样无影响 —— 一行现有代码都不用改。相机位置在 `CalcCamera` 里现算（不是 tick）：`UWorld::Tick` 里子系统先推进天体、`UpdateCameraManager` 紧接着取相机，只有 `CalcCamera` 落在这两者之间。选中状态放已有的 `UCosmosSimulationSubsystem`（它已经是天体的唯一注册表）。轨迹组件的采样上移到显示门控之前，门控只决定「画不画」。

**Tech Stack:** Unreal Engine 5.8，C++，4 个 UBT 模块（`StellarEra` / `StellarEraCore` / `StellarEraPlanet` / `StellarEraVehicle`），Enhanced Input（运行时 `NewObject` 构造 IA/IMC，项目里没有任何输入资产文件）。

**Spec:** `docs/superpowers/specs/2026-10-17-starmap-mode-design.md`

## Global Constraints

以下每条都对本计划的**每一个** Task 生效。

- **插件纯净偏好**：自定义代码一律写进项目自己的 `Source/` 模块，**不许**动 `Plugins/VoxelFree/`。本计划全部代码进 `Source/StellarEra/`（框架模块）。
- **`StellarEra.Build.cs` 不用改**：只用已有的 `Core` / `CoreUObject` / `Engine`，没有新模块依赖。
- **不加 `_API` 宏**：`AStarMapCameraPawn` 只被同一个模块 `StellarEra` 引用。该模块现有的类（`UCosmosSimulationSubsystem` / `UCosmosTrajectoryComponent` / `UCosmosOrbitComponent` / `AStellarEraPlayerController`）都没有 `_API`，保持一致。
- **UTF-8 BOM**：所有含中文的源文件必须有 BOM（`efbbbf`）。**已经存在的文件用 `Edit` 改，不要用 `Write` 重写** —— `Write` 会把 BOM 冲掉。
- **换行符：什么都不改**。本仓库是混的，`core.autocrlf=true` 下 CRLF 才是稳定态。**不要**把任何文件「统一成 LF」。
- **C4458 在 UE 5.8 是硬错误**（`/we4458`）：局部变量**不许**遮蔽成员。`AController` 有成员 `Character`；`AStellarEraPlayerController` 里已有代码专门为此刻意用 `PawnCharacter` / `Ship` 这类名字。
- **`KINDA_SMALL_NUMBER` 已废弃**（UE 5.8 用 `UE_PRIVATE_MATH_DEPRECATION` 包起来了）。要用的场合写 `UE_KINDA_SMALL_NUMBER`（= `1.e-4f`）或字面量。
- **构建前必须关掉编辑器**：Live Coding 会挡住 UBT。由用户手动关，**不要**杀进程，**不要**用 `BuildConfiguration.xml` 绕过。
- **UBT 直调**：用捆绑的 dotnet 调 `UnrealBuildTool.dll`，**不用** `Build.bat`；cwd 在项目外。
- **证明口径**：一律 `sha256sum` 截 16 位。用 `sha1sum` 会得到另一串，别当成不一致。
- **提交只在用户明确要求时做**。`git add` 用**显式完整路径**（`Content/` 长期是脏的）。提交信息用中文，结尾 `Co-Authored-By: Claude Code <noreply@anthropic.com>`。
- **测试断言写法**：涉及 `AActor*` / 指针的比较一律用 `TestTrue(TEXT("..."), A == B)` 或 `TestFalse`，**不要**用 `TestEqual` —— `TestEqual` 的泛型重载要靠 `LexToString`，指针没有可靠的 `ToString`。数值比较用 `TestEqual` 没问题。

---

## 文件结构

| 文件 | 动作 | 职责 |
|---|---|---|
| `Source/StellarEra/Public/CosmosSimulationSubsystem.h` | 改 | 加选中状态、星图激活标志、天体枚举 |
| `Source/StellarEra/Private/CosmosSimulationSubsystem.cpp` | 改 | 同上 |
| `Source/StellarEra/Private/StarMapTests.cpp` | 新建 | 三组自动化测试（`StellarEra.StarMap.*`） |
| `Source/StellarEra/Public/CosmosOrbitComponent.h` | 改 | 加 `bShowOutsideStarMap`；改 `bDrawTrajectory` 的注释 |
| `Source/StellarEra/Public/CosmosTrajectoryComponent.h` | 改 | 加 `BrightenTowardWhite` 静态函数、`ClearDrawnLines` 私有方法 |
| `Source/StellarEra/Private/CosmosTrajectoryComponent.cpp` | 改 | 采样前移、显示门控、高亮 |
| `Source/StellarEra/Public/StarMapCameraPawn.h` | 新建 | 星图相机 Pawn + 拾取纯函数 + 候选结构体 |
| `Source/StellarEra/Private/StarMapCameraPawn.cpp` | 新建 | 同上 |
| `Source/StellarEra/Public/StellarEraPlayerController.h` | 改 | 两个 IA、星图回调、`ToggleStarMap`、新成员 |
| `Source/StellarEra/Private/StellarEraPlayerController.cpp` | 改 | 输入构造与绑定、鼠标模式第三分支、`OnLook`/`OnZoom` 分支、提示、进出编排 |

**依赖方向**（严格顺序，无并行）：

```
T1 子系统 ──→ T2 轨迹组件
          └──→ T3 相机 Pawn ──→ T4 控制器接线 ──→ T5 收尾
```

---

### Task 1: 子系统：选中状态与天体枚举

**Files:**
- Modify: `Source/StellarEra/Public/CosmosSimulationSubsystem.h`
- Modify: `Source/StellarEra/Private/CosmosSimulationSubsystem.cpp`
- Create: `Source/StellarEra/Private/StarMapTests.cpp`

**Interfaces:**
- Consumes: 无（本任务是最底层）
- Produces: `void SetSelectedBody(AActor*)` / `AActor* GetSelectedBody() const` / `void GetRegisteredBodies(TArray<AActor*>&) const` / `bool IsStarMapActive() const` / `void SetStarMapActive(bool)`，全部在 `UCosmosSimulationSubsystem` 上。T2 / T3 / T4 都依赖它们。

- [ ] **Step 1: 写失败的测试**

新建 `Source/StellarEra/Private/StarMapTests.cpp`。

⚠️ **这个文件含中文，建完必须补 BOM**（见 Step 4）。

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "CosmosOrbitComponent.h"
#include "CosmosSimulationSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

// 断言写法说明：指针一律用 TestTrue/TestFalse 比较。TestEqual 的泛型重载要走
// LexToString，而指针没有可靠的 ToString —— 会编不过或打出没用的东西。

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraStarMapSelectionTest,
	"StellarEra.StarMap.Selection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraStarMapSelectionTest::RunTest(const FString& Parameters)
{
	// 裸实例就够：这几个方法都不碰世界（NewObject 不会调 Initialize）。
	UCosmosSimulationSubsystem* Subsystem = NewObject<UCosmosSimulationSubsystem>();
	if (!TestNotNull(TEXT("子系统裸实例"), Subsystem))
	{
		return false;
	}

	// ── 选中状态往返 ──
	TestTrue(TEXT("初始未选中"), Subsystem->GetSelectedBody() == nullptr);

	AActor* Body = NewObject<AActor>();
	Subsystem->SetSelectedBody(Body);
	TestTrue(TEXT("选中往返"), Subsystem->GetSelectedBody() == Body);

	Subsystem->SetSelectedBody(nullptr);
	TestTrue(TEXT("取消选中"), Subsystem->GetSelectedBody() == nullptr);

	// ── 星图激活标志 ──
	TestFalse(TEXT("初始星图未激活"), Subsystem->IsStarMapActive());
	Subsystem->SetStarMapActive(true);
	TestTrue(TEXT("设上后激活"), Subsystem->IsStarMapActive());
	Subsystem->SetStarMapActive(false);
	TestFalse(TEXT("清掉后不激活"), Subsystem->IsStarMapActive());

	// ── 天体枚举 ──
	{
		TArray<AActor*> Out;
		Out.Add(Body);            // 先塞一个脏元素，验 Out 会被 Reset
		Subsystem->GetRegisteredBodies(Out);
		TestEqual(TEXT("没注册天体时枚举为空"), Out.Num(), 0);
	}

	AActor* PlanetActor = NewObject<AActor>();
	UCosmosOrbitComponent* Orbit = NewObject<UCosmosOrbitComponent>(PlanetActor);
	Subsystem->RegisterBody(Orbit);

	// 一个 Outer 不是 Actor 的组件：GetOwner() 返回 nullptr，应当被跳过。
	UCosmosOrbitComponent* Orphan = NewObject<UCosmosOrbitComponent>();
	Subsystem->RegisterBody(Orphan);

	// nullptr 注册是空操作，不该进表。
	Subsystem->RegisterBody(nullptr);

	{
		TArray<AActor*> Out;
		Out.Add(Body);            // 脏元素
		Subsystem->GetRegisteredBodies(Out);
		TestEqual(TEXT("枚举到 1 个（跳过没有 Owner 的组件与 nullptr）"), Out.Num(), 1);
		TestTrue(TEXT("枚举到的是组件的 Owner"), Out.Num() == 1 && Out[0] == PlanetActor);
	}

	Subsystem->UnregisterBody(Orbit);
	{
		TArray<AActor*> Out;
		Subsystem->GetRegisteredBodies(Out);
		TestEqual(TEXT("注销后枚举为空"), Out.Num(), 0);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
```

- [ ] **Step 2: 构建，确认测试编不过**

（编辑器要先关掉，见 Global Constraints。）

```bash
(cd /tmp && "/d/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" \
  "/d/Application/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" \
  StellarEraEditor Win64 Development \
  -Project="F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -WaitMutex > /tmp/starmap-t1-build.log 2>&1); grep -E "error|Result:" /tmp/starmap-t1-build.log | head -20
```

预期：**FAIL** —— `error C2039: "GetSelectedBody": 不是 "UCosmosSimulationSubsystem" 的成员`（以及另外四个）。

> 若编辑器关不掉，跳过本步，直接做 Step 3。本步的价值只是「先看见红」，不是非做不可。

- [ ] **Step 3: 实现**

`Source/StellarEra/Public/CosmosSimulationSubsystem.h` —— 在 `double GetOrbitalPeriodYears(int32 SimulatorIndex) const;`（第 70 行）之后、`//////` 的配置块之前插入：

```cpp
	//////////////////////////////////////////////////////////////////////////
	// 星图模式
	//
	// 选中状态放这里而不是放星图 Pawn 上：本子系统已经是天体的全局注册表
	// （Bodies），而且是**唯一**知道全部天体的地方。轨迹组件每帧单向读它决定
	// 高亮，没有回写、没有生命周期问题。

	/** 选中一颗天体。传 nullptr 取消选中。存弱引用，天体被销毁后自动失效。 */
	void SetSelectedBody(AActor* Body);

	/** 当前选中的天体。未选中、或它已被销毁时返回 nullptr。 */
	AActor* GetSelectedBody() const;

	/**
	 * 把所有已注册天体的 Actor 写进 Out（先 Reset）。
	 *
	 * 跳过两种：组件本身失效的，和拿不到 Owner 的（组件的 Outer 不是 Actor）。
	 * 拾取遍历用它，所以**顺序必须是确定的** —— 拾取在等距时取先出现的。
	 * 实际顺序是 `Bodies` 的当前顺序：`FinalizeRegistration`（第一次 `Tick` 时跑）
	 * 会按质量降序排一次，质量大的在前；此后注册的追加到末尾。
	 * **不是注册顺序**，但确定。
	 */
	void GetRegisteredBodies(TArray<AActor*>& Out) const;

	/** 星图模式是否激活。轨迹组件的显示门控读它。 */
	bool IsStarMapActive() const { return bStarMapActive; }

	/**
	 * 由 AStellarEraPlayerController::ToggleStarMap 的进入/退出分支调用。
	 *
	 * **不放星图 Pawn 的 BeginPlay/EndPlay** —— 那个 Pawn 是懒创建并复用的，
	 * BeginPlay 一辈子只跑一次，第二次进入时标志不会被重新设上。
	 */
	void SetStarMapActive(bool bActive) { bStarMapActive = bActive; }
```

同文件，在私有区 `TArray<TObjectPtr<UCosmosOrbitComponent>> Bodies;`（第 155 行）之后插入：

```cpp
	/** 星图模式下选中的天体。弱引用 —— 天体被销毁后自动变 nullptr。 */
	TWeakObjectPtr<AActor> SelectedBody;

	/** 星图模式是否激活。 */
	bool bStarMapActive = false;
```

> `AActor` 在本头文件里**不用**新加 include 或前向声明：`CarryRiders(AActor* ...)`（第 153 行）已经在用 `AActor*`，靠的是 `Engine` 模块的 SharedPCH。

`Source/StellarEra/Private/CosmosSimulationSubsystem.cpp` —— 在 `double UCosmosSimulationSubsystem::GetOrbitalPeriodYears(int32 SimulatorIndex) const`（第 331-334 行）之后插入：

```cpp
void UCosmosSimulationSubsystem::SetSelectedBody(AActor* Body)
{
	SelectedBody = Body;
}

AActor* UCosmosSimulationSubsystem::GetSelectedBody() const
{
	return SelectedBody.Get();
}

void UCosmosSimulationSubsystem::GetRegisteredBodies(TArray<AActor*>& Out) const
{
	Out.Reset();

	for (const TObjectPtr<UCosmosOrbitComponent>& Component : Bodies)
	{
		// 组件可能在关卡卸载的间隙里失效；Outer 不是 Actor 的组件也拿不到 Actor。
		if (!Component)
		{
			continue;
		}

		if (AActor* OwnerActor = Component->GetOwner())
		{
			Out.Add(OwnerActor);
		}
	}
}
```

- [ ] **Step 4: 补 BOM 并重建**

```bash
for f in Source/StellarEra/Private/StarMapTests.cpp \
         Source/StellarEra/Public/CosmosSimulationSubsystem.h \
         Source/StellarEra/Private/CosmosSimulationSubsystem.cpp ; do
  printf '%-62s %s\n' "$f" "$(head -c3 "$f" | xxd -p)"
done
```

三个都必须是 `efbbbf`。缺的用 `printf '\xef\xbb\xbf' | cat - "$f" > "$f.tmp" && mv "$f.tmp" "$f"` 补上，然后重建：

```bash
(cd /tmp && "/d/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" \
  "/d/Application/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" \
  StellarEraEditor Win64 Development \
  -Project="F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -WaitMutex > /tmp/starmap-t1-build.log 2>&1); grep -E "error|Result:" /tmp/starmap-t1-build.log | head -20
```

预期：`Result: Succeeded`。

> **注意**：本项目里 `Result: Failed` 也可能是「编译成功、只有 DLL 链接失败」。要判真伪，看日志里有没有 `error C` 开头的**编译**错误 —— 只有 `LNK` 或「无法写入 DLL」而没有任何 `error C`，说明源码是好的。

- [ ] **Step 5: 跑测试，确认通过**

```bash
"/d/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra.StarMap; Quit" \
  -unattended -nopause -nullrhi -log \
  > /tmp/starmap-t1-test.log 2>&1; grep -E "Result=\{|test\(s\)" /tmp/starmap-t1-test.log | head -20
```

预期：`StellarEra.StarMap.Selection` 的 `Result={Success}`，且没有 `Failed`。

> 无头跑自动化测试是可行的（本项目实测过，约 25 秒干净退出），**编辑器开着也能跑** —— 只有 build 需要关编辑器。

- [ ] **Step 6: 提交**

```bash
git add \
  Source/StellarEra/Public/CosmosSimulationSubsystem.h \
  Source/StellarEra/Private/CosmosSimulationSubsystem.cpp \
  Source/StellarEra/Private/StarMapTests.cpp

git commit -m "$(cat <<'EOF'
feat(星图): 子系统加选中状态与天体枚举

选中状态放子系统而不是放星图 Pawn 上：子系统已经是天体的全局注册表，
而且是唯一知道全部天体的地方。轨迹组件每帧单向读它决定高亮，没有回写、
没有生命周期问题。

GetRegisteredBodies 跳过失效组件与拿不到 Owner 的组件；顺序是 Bodies 的当前顺序
（FinalizeRegistration 之后为质量降序），确定但不是注册顺序。

配套测试 StellarEra.StarMap.Selection。

Co-Authored-By: Claude Code <noreply@anthropic.com>
EOF
)"
```

---

### Task 2: 轨迹组件：显示门控与选中高亮

**Files:**
- Modify: `Source/StellarEra/Public/CosmosOrbitComponent.h`
- Modify: `Source/StellarEra/Private/CosmosOrbitComponent.cpp`
- Modify: `Source/StellarEra/Public/CosmosTrajectoryComponent.h`
- Modify: `Source/StellarEra/Private/CosmosTrajectoryComponent.cpp`
- Modify: `Source/StellarEra/Private/StarMapTests.cpp`

**Interfaces:**
- Consumes: T1 的 `UCosmosSimulationSubsystem::IsStarMapActive()`、`GetSelectedBody()`
- Produces: `static FColor UCosmosTrajectoryComponent::BrightenTowardWhite(const FColor& In, float Amount)`；`UCosmosOrbitComponent::bShowOutsideStarMap`（`bool`，默认 `false`）

- [ ] **Step 1: 写失败的测试**

在 `StarMapTests.cpp` 的 `#endif` **之前**追加：

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraStarMapHighlightTest,
	"StellarEra.StarMap.Highlight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraStarMapHighlightTest::RunTest(const FString& Parameters)
{
	// 具体数值的来源：T = 0.45f 的 float32 值是 0.44999998807907104，
	// 结果 = In.C + T × (255 − In.C)，再按 FMath::RoundToInt32 取整。
	//   90 + 0.45 × 165 = 164.249998 → 164
	//  170 + 0.45 ×  85 = 208.249999 → 208
	//   20 + 0.45 × 235 = 125.749997 → 126   ← 截断会得 125，所以取整是必需的
	const FColor Prediction(90, 170, 255);
	const FColor Trail(20, 50, 110);

	// ── Amount = 0 → 原样 ──
	{
		const FColor Result = UCosmosTrajectoryComponent::BrightenTowardWhite(Prediction, 0.f);
		TestTrue(TEXT("0：R 不变"), Result.R == Prediction.R);
		TestTrue(TEXT("0：G 不变"), Result.G == Prediction.G);
		TestTrue(TEXT("0：B 不变"), Result.B == Prediction.B);
		TestTrue(TEXT("0：A 不变"), Result.A == Prediction.A);
	}

	// ── Amount = 1 → 纯白，alpha 保留 ──
	{
		const FColor Result = UCosmosTrajectoryComponent::BrightenTowardWhite(FColor(20, 50, 110, 128), 1.f);
		TestTrue(TEXT("1：R 是 255"), Result.R == 255);
		TestTrue(TEXT("1：G 是 255"), Result.G == 255);
		TestTrue(TEXT("1：B 是 255"), Result.B == 255);
		TestTrue(TEXT("1：alpha 保留"), Result.A == 128);
	}

	// ── Amount = 0.45 → 具体数值 ──
	{
		const FColor Result = UCosmosTrajectoryComponent::BrightenTowardWhite(Prediction, 0.45f);
		TestEqual(TEXT("预测色 0.45：R"), int32(Result.R), 164);
		TestEqual(TEXT("预测色 0.45：G"), int32(Result.G), 208);
		TestEqual(TEXT("预测色 0.45：B"), int32(Result.B), 255);
	}
	{
		const FColor Result = UCosmosTrajectoryComponent::BrightenTowardWhite(Trail, 0.45f);
		TestEqual(TEXT("拖尾色 0.45：R"), int32(Result.R), 126);
		TestEqual(TEXT("拖尾色 0.45：G"), int32(Result.G), 142);
		TestEqual(TEXT("拖尾色 0.45：B"), int32(Result.B), 175);
	}

	// ── 单调不减：每一档都只会更亮，不会更暗 ──
	{
		const FColor Low = UCosmosTrajectoryComponent::BrightenTowardWhite(Trail, 0.2f);
		const FColor High = UCosmosTrajectoryComponent::BrightenTowardWhite(Trail, 0.8f);
		TestTrue(TEXT("0.2 ≥ 原色：R"), Low.R >= Trail.R);
		TestTrue(TEXT("0.2 ≥ 原色：G"), Low.G >= Trail.G);
		TestTrue(TEXT("0.2 ≥ 原色：B"), Low.B >= Trail.B);
		TestTrue(TEXT("0.8 ≥ 0.2：R"), High.R >= Low.R);
		TestTrue(TEXT("0.8 ≥ 0.2：G"), High.G >= Low.G);
		TestTrue(TEXT("0.8 ≥ 0.2：B"), High.B >= Low.B);
	}

	// ── 越界输入被夹住，不炸 ──
	{
		const FColor Neg = UCosmosTrajectoryComponent::BrightenTowardWhite(Trail, -3.f);
		const FColor Over = UCosmosTrajectoryComponent::BrightenTowardWhite(Trail, 7.f);
		TestTrue(TEXT("-3 等同于 0"), Neg.R == Trail.R && Neg.G == Trail.G && Neg.B == Trail.B);
		TestTrue(TEXT("7 等同于 1"), Over.R == 255 && Over.G == 255 && Over.B == 255);
	}

	return true;
}
```

- [ ] **Step 2: 构建，确认测试编不过**

同 T1 Step 2 的命令（日志换 `/tmp/starmap-t2-build.log`）。

预期：**FAIL** —— `error C2039: "BrightenTowardWhite": 不是 "UCosmosTrajectoryComponent" 的成员`。

- [ ] **Step 3: 加 `bShowOutsideStarMap`，并改 `bDrawTrajectory` 的注释**

`Source/StellarEra/Public/CosmosOrbitComponent.h`。

**先改第 90 行的注释**（它是「注释声称的事实」，语义变了必须同改）：

```cpp
	/**
	 * 是否画这个天体的轨迹（预测 + 拖尾）。
	 *
	 * 它只门控「画」—— 拖尾采样**不**受它影响，一直在跑（拖尾是真实历史，
	 * 采样无条件进行，成本有界：保留点数 ≤ TrailSamples + 1 = 513）。
	 * 关掉省下的是预测重算与画线，那是开销的大头。
	 *
	 * 注意默认它开着，但轨迹**默认仍然看不见** —— 可见性还要过
	 * bShowOutsideStarMap 或星图模式那一关。见下面那条属性。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory")
	bool bDrawTrajectory = true;
```

**再在第 92 行的 `bDrawTrajectory` 之后插入新属性**：

```cpp
	/**
	 * 是否在**星图模式之外**也画这个天体的轨迹。默认关。
	 *
	 * 默认关 = 轨迹只在星图模式里显示。星图模式本身（含按 M 进出的开关）由 T4 接线 ——
	 * 在那之前，把它设成 false 的效果就是「看不到轨迹」。
	 * 想在普通游戏画面里也看到就把它勾上 —— 这是逐天体开关，与 bDrawTrajectory 是「与」的关系。
	 *
	 * 放在 Orbit 组件上而不是轨迹组件上：轨迹组件是**哑渲染器**，它自己的头注释
	 * 写着「不存任何面向用户的参数，参数全从兄弟 UCosmosOrbitComponent 上读」。
	 * 这条是面向用户的，同理。
	 *
	 * ⚠ 它同样只门控「画」。拖尾采样在两种模式下都一直跑 —— 这正是「一进星图
	 * 就是完整拖尾、不用等它从零长起来」的前提。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory")
	bool bShowOutsideStarMap = false;
```

**再改 `Source/StellarEra/Private/CosmosOrbitComponent.cpp:47-48` 的注释。**

那两行写着「无条件创建，即使 `bDrawTrajectory` 为假 —— 这样运行期把它打开能立刻生效，
代价只是关掉时多一次空 Tick」。**采样上移之后这句就假了**：`bDrawTrajectory = false` 的那一帧
仍要跑子系统查询 → `ResolvePredictionHorizonYears` → `GetSimulatedTimeYears` → 拖尾采样与裁剪，
然后才被 `bVisible` 挡下。改成如实描述：

```cpp
	// 无条件创建，即使 bDrawTrajectory 为假 —— 这样运行期把它打开能立刻生效。
	// 代价是关掉时那一帧仍要走完采样：子系统查询、预测窗口解析、模拟时间读取，
	// 以及拖尾的采样与裁剪（拖尾是真实历史，两种模式下都一直在攒）。省下的是
	// 预测重算与画线 —— 那是开销的大头，见 UCosmosTrajectoryComponent::TickComponent。
```

> 这是**同一类错误的另一处：注释声称的事实**。本项目里它和代码错误是两类，只核一类会漏。

- [ ] **Step 4: 加 `BrightenTowardWhite` 与 `ClearDrawnLines` 的声明**

`Source/StellarEra/Public/CosmosTrajectoryComponent.h`。

在 `public:` 区、`void Initialize(UCosmosOrbitComponent* InOrbit);`（第 42 行）之后插入：

```cpp
	/**
	 * 把颜色朝白色插值 Amount（0 = 原色，1 = 纯白）。alpha 不动。
	 *
	 * 星图里选中天体的高亮就是它 —— **只改颜色，不加粗**。加粗要传非零线宽，
	 * 而行批把「线宽 == 0」走成真正的 1 像素 GPU 线、非零走成世界单位的四边形
	 * （见 .cpp 里 LineThickness 的注释），等于把选中的那条线切到另一条渲染路径。
	 * 那条路径只能人眼验，不值得为一个高亮引入一个只能靠肉眼发现的失败面。
	 */
	static FColor BrightenTowardWhite(const FColor& In, float Amount);
```

在 `private:` 区、`void AppendVelocityArrow(const FLinearColor& Color);`（第 57 行）之后插入：

```cpp
	/**
	 * 把已经画上去的线清一次（bDrewLastFrame 为真时才真清）。
	 *
	 * 线用 LifeTime = 0 画上去就永不过期，所以「这一帧不画了」必须显式清，
	 * 否则最后一帧的线会永远留在屏幕上。两个显示门控都要用它，抽出来避免重复。
	 */
	void ClearDrawnLines();
```

- [ ] **Step 5: 实现 `BrightenTowardWhite`**

`Source/StellarEra/Private/CosmosTrajectoryComponent.cpp`，在匿名 namespace 的末尾（`AutoTrailHorizonFraction` 那条之后、`}` 之前）加常量：

```cpp
	/** 星图里选中天体时，颜色朝白插值的比例。 */
	constexpr float HighlightWhiteLerp = 0.45f;
```

在文件末尾（`AppendVelocityArrow` 之后）加实现：

```cpp
FColor UCosmosTrajectoryComponent::BrightenTowardWhite(const FColor& In, float Amount)
{
	// 全部按 float 算：FMath::RoundToInt32 的 float 重载和 double 重载结果一致，
	// 但混着写容易让人以为这里在做双精度插值。
	const float T = FMath::Clamp(Amount, 0.f, 1.f);

	return FColor(
		uint8(FMath::RoundToInt32(In.R + T * (255.f - In.R))),
		uint8(FMath::RoundToInt32(In.G + T * (255.f - In.G))),
		uint8(FMath::RoundToInt32(In.B + T * (255.f - In.B))),
		In.A);
}
```

- [ ] **Step 6: 重排 `TickComponent`：采样前移、门控只门控「画」、高亮**

`Source/StellarEra/Private/CosmosTrajectoryComponent.cpp`。

**把第 86-201 行整段（`TickComponent` 的函数体）替换成下面这版。** 替换时**逐字保留**原来那几段解释性注释（下面用「原注释整段保留」标出的地方）—— 它们记录的是已经复审过的论证，不是装饰。

```cpp
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!LineBatch)
	{
		return;
	}

	// Initialize 先赋 Orbit 再建 LineBatch，所以 !Orbit 蕴含 !LineBatch。
	// 这条实际上已被上面那条覆盖，留着只为把「Orbit 可能为空」写明白。
	if (!Orbit)
	{
		ClearDrawnLines();
		return;
	}

	const UCosmosSimulationSubsystem* Subsystem =
		GetWorld() ? GetWorld()->GetSubsystem<UCosmosSimulationSubsystem>() : nullptr;
	if (!Subsystem)
	{
		return;
	}

	// 预测窗口拖尾和绘制都要用，先算。
	const double Horizon = ResolvePredictionHorizonYears(*Subsystem);

	// ── 拖尾：采样和裁剪**都**按模拟时间，都不按帧 ──
	// 原注释整段保留（「只『按时间采样』是不够的…」那一段）。
	const double SimulatedTime = Subsystem->GetSimulatedTimeYears();

	const double TrailSpan = Orbit->TrailYears > 0.0
		? Orbit->TrailYears
		: Horizon * AutoTrailHorizonFraction;
	const double TrailInterval = TrailSpan / double(TrailSamples);

	const AActor* OwnerActor = GetOwner();
	const FVector CurrentLocation = OwnerActor
		? OwnerActor->GetActorLocation()
		: FVector::ZeroVector;

	if (Trail.Num() == 0)
	{
		// 开局先放一个点，否则拖尾要等到第一个采样时刻才开始长，缺开头一小段。
		Trail.Add(CurrentLocation);
		TrailTimes.Add(SimulatedTime);
		LastTrailSampleTime = SimulatedTime;
	}
	else if (TrailInterval > 0.0 && SimulatedTime - LastTrailSampleTime >= TrailInterval)
	{
		// 一帧最多补一个点：掉帧时宁可让采样间隔临时变长，也不要在一帧里塞进
		// 几十个几乎重合的点。
		Trail.Add(CurrentLocation);
		TrailTimes.Add(SimulatedTime);
		LastTrailSampleTime = SimulatedTime;

		// 原注释整段保留（「按**模拟时间**裁剪，不按点数…」那一段）。
		const double TrailStartTime = SimulatedTime - TrailSpan;
		int32 DropCount = 0;
		while (DropCount < TrailTimes.Num() && TrailTimes[DropCount] < TrailStartTime)
		{
			++DropCount;
		}
		if (DropCount > 0)
		{
			// EAllowShrinking::No = 保留容量，别每次裁剪都把分配器折腾一遍。
			Trail.RemoveAt(0, DropCount, EAllowShrinking::No);
			TrailTimes.RemoveAt(0, DropCount, EAllowShrinking::No);
		}
	}

	// ── 门控：只门控「画」 ──
	// 采样在上面**无条件**跑完了 —— 拖尾是真实历史。星图模式之外看不见它，
	// 但历史一直在攒，所以一进星图就是完整的，不是从零开始长。
	const bool bVisible = Orbit->bDrawTrajectory
		&& (Subsystem->IsStarMapActive() || Orbit->bShowOutsideStarMap);
	if (!bVisible)
	{
		ClearDrawnLines();
		return;
	}

	// ── 预测：每帧重算 ──
	// 原注释整段保留（「不做节流。理由有两条…」那一段）。
	PredictedWorld.Reset();
	if (Orbit->SimulatorIndex != INDEX_NONE)
	{
		Subsystem->PredictTrajectoryWorld(Orbit->SimulatorIndex, Horizon,
			PredictionSamples, PredictedWorld);
	}

	// ── 高亮：选中的天体两种颜色都向白提 ──
	// 只改颜色不改线宽 —— 理由见 BrightenTowardWhite 的声明处。
	const bool bSelected = (OwnerActor != nullptr)
		&& (Subsystem->GetSelectedBody() == OwnerActor);
	const FLinearColor TrailColor = bSelected
		? FLinearColor(BrightenTowardWhite(Orbit->TrailColor, HighlightWhiteLerp))
		: FLinearColor(Orbit->TrailColor);
	const FLinearColor PredictionColor = bSelected
		? FLinearColor(BrightenTowardWhite(Orbit->PredictionColor, HighlightWhiteLerp))
		: FLinearColor(Orbit->PredictionColor);

	Lines.Reset();

	if (Trail.Num() >= 2)
	{
		AppendPolyline(Trail, TrailColor);
	}

	if (PredictedWorld.Num() >= 2)
	{
		AppendPolyline(PredictedWorld, PredictionColor);
	}

	if (Orbit->bDrawInitialVelocity)
	{
		AppendVelocityArrow(FLinearColor(Orbit->InitialVelocityColor));
	}

	// Flush 是必须的：线用 LifeTime = 0 画上去就永不过期，不清的话每帧的线会
	// 无限叠加（这是 spec §7.1 对着引擎源码确认过的行为）。
	LineBatch->Flush();
	if (Lines.Num() > 0)
	{
		LineBatch->DrawLines(Lines);
	}
	bDrewLastFrame = Lines.Num() > 0;
```

并加 `ClearDrawnLines` 的实现（放在 `TickComponent` 之后）：

```cpp
void UCosmosTrajectoryComponent::ClearDrawnLines()
{
	// 关掉时把已画的线清一次就够，不必每帧 MarkRenderStateDirty。
	if (bDrewLastFrame)
	{
		LineBatch->Flush();
		bDrewLastFrame = false;
	}
}
```

> **三处行为变化，都是有意为之，复审时按这三条核：**
> 1. 拖尾采样搬到了显示门控**之前**，所以现在「关掉轨迹」也照常采样。
> 2. `bDrawTrajectory` 从「整段早退」变成「只是 `bVisible` 的一个与项」。
> 3. 新增 `bShowOutsideStarMap`（默认 `false`）→ **普通模式下默认看不到轨迹了**，
>    这是 spec §5.4 明确写下的观感变化。

- [ ] **Step 7: 构建**

同 T1 Step 2 的命令（日志 `/tmp/starmap-t2-build.log`）。预期 `Result: Succeeded`，无 `error C`。

- [ ] **Step 8: 跑测试**

同 T1 Step 5 的命令（日志 `/tmp/starmap-t2-test.log`）。

预期：`StellarEra.StarMap.Selection` 与 `StellarEra.StarMap.Highlight` **两个都** `Result={Success}`。

> 若 `Highlight` 的具体数值对不上，**先别改测试**。把失败的那一行读出来，自己按
> `In.C + T × (255 − In.C)` 算一遍 float32 —— 本计划里的六个数字是这样算出来的：
> `164 / 208 / 255` 和 `126 / 142 / 175`。是测试写错了就改测试，是公式写错了就改公式，
> 但**改完要在报告里说明改的是哪一边、为什么**。

- [ ] **Step 9: 提交**

```bash
git add \
  Source/StellarEra/Public/CosmosOrbitComponent.h \
  Source/StellarEra/Public/CosmosTrajectoryComponent.h \
  Source/StellarEra/Private/CosmosTrajectoryComponent.cpp \
  Source/StellarEra/Private/StarMapTests.cpp

git commit -m "$(cat <<'EOF'
feat(星图): 轨迹显示门控与选中高亮

拖尾采样搬到显示门控之前：门控只决定「画不画」，采样一直跑。拖尾是真实
历史，这样一进星图就是完整的一条，而不是从零开始长。成本有界 —— 裁剪
仍按模拟时间，保留点数 ≤ TrailSamples + 1 = 513。

新增 UCosmosOrbitComponent::bShowOutsideStarMap（默认 false），于是普通
模式下默认看不到轨迹；要保留原观感就勾上它。bDrawTrajectory 的注释同步
改成如实描述（它现在也只门控「画」，关掉省的是预测重算与画线）。

高亮只改颜色不加粗：加粗要传非零线宽，会把选中的线切到行批的另一条渲染
路径，而那条路径只能人眼验。配套测试 StellarEra.StarMap.Highlight。

Co-Authored-By: Claude Code <noreply@anthropic.com>
EOF
)"
```

---

### Task 3: 星图相机 Pawn 与拾取纯函数

**Files:**
- Create: `Source/StellarEra/Public/StarMapCameraPawn.h`
- Create: `Source/StellarEra/Private/StarMapCameraPawn.cpp`
- Modify: `Source/StellarEra/Private/StarMapTests.cpp`

**Interfaces:**
- Consumes: T1 的 `GetSelectedBody()`
- Produces:
  - `struct FStarMapScreenCandidate { AActor* Actor; FVector2D ScreenPosition; }`
  - `class AStarMapCameraPawn : public APawn`，带 `void EnterFrom(const FVector& CameraLocation, const FRotator& CameraRotation)` / `void OrbitLook(float DeltaYaw, float DeltaPitch)` / `void AddZoom(float Value)` / `AActor* ResolveSelectedBody() const` / `static AActor* PickBodyAtScreenPosition(TArrayView<const FStarMapScreenCandidate>, const FVector2D&, float)` / `virtual void CalcCamera(float, FMinimalViewInfo&) override`
  - 三个可调属性（都在 Pawn 上）：`ZoomStepFactor = 1.15`、`MinOrbitDistanceCm = 2.0e4`、`MaxOrbitDistanceCm = 5.0e7`

- [ ] **Step 1: 写失败的测试**

在 `StarMapTests.cpp` 的 `#endif` **之前**追加：

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraStarMapPickingTest,
	"StellarEra.StarMap.Picking",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraStarMapPickingTest::RunTest(const FString& Parameters)
{
	// 拾取是纯数学，不碰世界、不碰碰撞 —— 所以能无头测。投影留在调用方。
	AActor* A = NewObject<AActor>();
	AActor* B = NewObject<AActor>();
	AActor* C = NewObject<AActor>();

	// ── 空列表 ──
	{
		TArray<FStarMapScreenCandidate> Empty;
		TestTrue(TEXT("空列表返回 nullptr"),
			AStarMapCameraPawn::PickBodyAtScreenPosition(
				Empty, FVector2D(100.0, 100.0), 20.f) == nullptr);
	}

	// ── 命中最近者 ──
	{
		TArray<FStarMapScreenCandidate> Candidates;
		Candidates.Add({ A, FVector2D(100.0, 100.0) });   // 距点 10
		Candidates.Add({ B, FVector2D(130.0, 100.0) });   // 距点 20（正好在边界上）
		Candidates.Add({ C, FVector2D(200.0, 100.0) });   // 距点 90，超出容差

		TestTrue(TEXT("取最近的（C 被容差挡掉，A 比 B 近）"),
			AStarMapCameraPawn::PickBodyAtScreenPosition(
				Candidates, FVector2D(110.0, 100.0), 20.f) == A);
	}

	// ── 边界含：恰好等于半径算命中，小一点点就不算 ──
	{
		TArray<FStarMapScreenCandidate> Candidates;
		Candidates.Add({ B, FVector2D(130.0, 100.0) });   // 与 (110,100) 的距离正好 20

		TestTrue(TEXT("距离恰好等于半径 → 命中（边界含）"),
			AStarMapCameraPawn::PickBodyAtScreenPosition(
				Candidates, FVector2D(110.0, 100.0), 20.f) == B);
		TestTrue(TEXT("半径小一点点 → 不命中"),
			AStarMapCameraPawn::PickBodyAtScreenPosition(
				Candidates, FVector2D(110.0, 100.0), 19.9f) == nullptr);
	}

	// ── 等距：取**先出现**的那个（换个顺序结果跟着换）──
	{
		TArray<FStarMapScreenCandidate> First;
		First.Add({ A, FVector2D(90.0, 100.0) });         // 距点 10
		First.Add({ B, FVector2D(110.0, 100.0) });        // 距点 10

		TArray<FStarMapScreenCandidate> Reversed;
		Reversed.Add({ B, FVector2D(110.0, 100.0) });
		Reversed.Add({ A, FVector2D(90.0, 100.0) });

		TestTrue(TEXT("等距取先出现的"),
			AStarMapCameraPawn::PickBodyAtScreenPosition(
				First, FVector2D(100.0, 100.0), 20.f) == A);
		TestTrue(TEXT("换序后取新的先出现者（证明它认的是顺序，不是 A）"),
			AStarMapCameraPawn::PickBodyAtScreenPosition(
				Reversed, FVector2D(100.0, 100.0), 20.f) == B);
	}

	// ── Actor 为空的候选被跳过，哪怕它更近 ──
	{
		TArray<FStarMapScreenCandidate> Candidates;
		Candidates.Add({ nullptr, FVector2D(100.0, 100.0) });   // 距点 0，但 Actor 为空
		Candidates.Add({ C, FVector2D(105.0, 100.0) });         // 距点 5

		TestTrue(TEXT("跳过 Actor 为空的候选"),
			AStarMapCameraPawn::PickBodyAtScreenPosition(
				Candidates, FVector2D(100.0, 100.0), 20.f) == C);
	}

	// ── 全都不在容差内 ──
	{
		TArray<FStarMapScreenCandidate> Candidates;
		Candidates.Add({ A, FVector2D(500.0, 500.0) });
		TestTrue(TEXT("都不在容差内返回 nullptr"),
			AStarMapCameraPawn::PickBodyAtScreenPosition(
				Candidates, FVector2D(0.0, 0.0), 20.f) == nullptr);
	}

	return true;
}
```

- [ ] **Step 2: 构建，确认测试编不过**

同 T1 Step 2 的命令（日志 `/tmp/starmap-t3-build.log`）。

预期：**FAIL** —— `error C1083: 无法打开包括文件: "StarMapCameraPawn.h"`。

- [ ] **Step 3: 建 Pawn 的头文件**

新建 `Source/StellarEra/Public/StarMapCameraPawn.h`。**这个文件含中文，建完必须补 BOM。**

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "StarMapCameraPawn.generated.h"

class UCameraComponent;
class UCosmosSimulationSubsystem;

/**
 * 一个屏幕空间候选：某个天体的 Actor 和它投影到屏幕上的位置。
 *
 * 单独拎成一个结构体而不是用两个平行数组（Actors + ScreenPositions）：平行数组
 * 靠下标约定对齐，结构体靠类型对齐。下标错位是编译器看不出来的错。
 */
struct FStarMapScreenCandidate
{
	AActor* Actor = nullptr;
	FVector2D ScreenPosition = FVector2D::ZeroVector;
};

/**
 * 星图模式的相机。进入星图时由 AStellarEraPlayerController 创建并 Possess —— 该接线由 T4 完成。
 *
 * **相机不 tick，位置在 CalcCamera 里现算。** 这是必需的，不是风格选择：
 * UWorld::Tick 里的顺序是「各 Actor tick 组 → FTickableGameObject::TickObjects
 * （子系统在这里推进求解器、把位置写回 Actor）→ UpdateCameraManager（CalcCamera
 * 在这里被调用）→ TG_PostUpdateWork → TG_LastDemotable」。
 * 任何 tick 组都不落在中间那个窗口里：排在前面的会读到天体上一帧的位置，排在
 * TG_PostUpdateWork 的则相机已经算完了。两条路都差一帧，CalcCamera 零滞后。
 *
 * Pawn 自己的 Actor 变换**不影响相机** —— 它只是为了让这个 Pawn 在 PIE 里
 * 看得见、点得中。
 */
UCLASS()
class AStarMapCameraPawn : public APawn
{
	GENERATED_BODY()

public:
	AStarMapCameraPawn();

	/** 进入星图模式时由控制器调用：把相机原地钉住，并重置为「未选中」。 */
	void EnterFrom(const FVector& InCameraLocation, const FRotator& InCameraRotation);

	/** 鼠标位移 → 绕焦点转视角。单位是度，来自控制器的 LookSensitivity。 */
	void OrbitLook(float DeltaYaw, float DeltaPitch);

	/** 滚轮 → 按倍率缩放与焦点的距离。Value 每格 ±1。 */
	void AddZoom(float Value);

	/** 当前选中的天体（每帧从子系统读，不自己存）。 */
	AActor* ResolveSelectedBody() const;

	/**
	 * 从屏幕坐标里挑一颗天体。纯函数：不碰世界、不碰碰撞，所以能无头测。
	 *
	 * 返回离 ScreenPoint 最近、且距离 ≤ PixelRadius 的候选。都不在容差内、
	 * 或候选全为空时返回 nullptr。**边界含**：距离恰好等于 PixelRadius 算命中。
	 *
	 * 等距时取**先出现**的那个，所以结果依赖 Candidates 的顺序 —— 调用方给的
	 * 顺序必须是确定的（本项目里就是 `GetRegisteredBodies` 的返回顺序，
	 * 即 `Bodies` 的当前顺序：`FinalizeRegistration` 之后为质量降序）。
	 */
	static AActor* PickBodyAtScreenPosition(
		TArrayView<const FStarMapScreenCandidate> Candidates,
		const FVector2D& ScreenPoint,
		float PixelRadius);

	//~ Begin AActor Interface
	virtual void CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult) override;
	//~ End AActor Interface

protected:
	/** 相机，同时是根组件。不挂 SpringArm —— 太空里没有东西要避。 */
	UPROPERTY(VisibleAnywhere, Category = "StarMap")
	TObjectPtr<UCameraComponent> Camera;

	// ── 运行时状态 ──

	/** 从哪个方向看（yaw / pitch）。相机位置 = 焦点 − 这个方向 × 距离。 */
	UPROPERTY(Transient, VisibleAnywhere, Category = "StarMap|State")
	FRotator OrbitRotation = FRotator::ZeroRotator;

	/** 相机到焦点的距离（厘米）。 */
	UPROPERTY(Transient, VisibleAnywhere, Category = "StarMap|State")
	double OrbitDistanceCm = 0.0;

	/**
	 * 相机当前的世界位置。
	 *
	 * 选中有效时每帧重算成「焦点 − 视线方向 × 距离」；未选中时**不更新** ——
	 * 这就是「冻结在取消选中那一刻的位置」本身。
	 */
	UPROPERTY(Transient, VisibleAnywhere, Category = "StarMap|State")
	FVector CameraLocation = FVector::ZeroVector;

	/**
	 * 上一帧看到的选中天体，用来检出「选中状态变了」。
	 *
	 * 检出变化时从**当前** CameraLocation 反推 OrbitRotation / OrbitDistanceCm，
	 * 于是相机只是「开始跟着」新焦点，位置不变、朝向重算（spec §5.2）。
	 */
	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> LastSeenSelected;

	// ── 可调参数 ──

	/**
	 * 每格滚轮的距离倍率。
	 *
	 * **倍率而不是线性步进**，这里和飞船的 AddZoom 不一样，是有意的：飞船的摇臂
	 * 长度只在几十厘米的量级内变，星图的距离要跨好几个数量级（从贴着一颗行星到
	 * 看整个星系），线性步进在 AU 尺度上等于没反应。
	 */
	UPROPERTY(EditAnywhere, Category = "StarMap")
	double ZoomStepFactor = 1.15;

	/**
	 * 最近距离（厘米）。世界尺度：1 AU = CentimetersPerAu = 1e6 厘米；
	 * 默认体素行星的半径 = Radius 100 体素 × VoxelSize 100 厘米 = 1e4 厘米。
	 * 所以 2e4 = 两颗行星半径，够近但不会钻进地里。
	 */
	UPROPERTY(EditAnywhere, Category = "StarMap")
	double MinOrbitDistanceCm = 2.0e4;

	/** 最远距离（厘米）。5e7 厘米 = 50 AU，够从一颗行星看到整个星系。 */
	UPROPERTY(EditAnywhere, Category = "StarMap")
	double MaxOrbitDistanceCm = 5.0e7;
};
```

> **不加 `STELLARERA_API`**：这个类只被同一个模块 `StellarEra` 引用。该模块现有的类都没有导出宏，保持一致。

- [ ] **Step 4: 建 Pawn 的实现**

新建 `Source/StellarEra/Private/StarMapCameraPawn.cpp`。**含中文，建完必须补 BOM。**

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "StarMapCameraPawn.h"

#include "Camera/CameraComponent.h"
#include "CosmosSimulationSubsystem.h"
#include "Engine/World.h"

AStarMapCameraPawn::AStarMapCameraPawn()
{
	// 不 tick：位置在 CalcCamera 里现算，见类注释。
	PrimaryActorTick.bCanEverTick = false;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("StarMapCamera"));
	SetRootComponent(Camera);

	// 视线方向由本 Pawn 自己管（OrbitRotation），不走控制器的旋转。
	Camera->bUsePawnControlRotation = false;
}

void AStarMapCameraPawn::EnterFrom(const FVector& InCameraLocation, const FRotator& InCameraRotation)
{
	// 「原地不动、不选中」：把进入前的视角直接拿来当基准，于是进入星图时画面不变。
	CameraLocation = InCameraLocation;
	OrbitRotation = InCameraRotation;
	LastSeenSelected = nullptr;

	// 顺手把 Actor 摆过去，方便在 PIE 里看它、点它。它不影响相机。
	SetActorLocationAndRotation(InCameraLocation, InCameraRotation, /*bSweep=*/false);
}

void AStarMapCameraPawn::OrbitLook(float DeltaYaw, float DeltaPitch)
{
	// 只动朝向不动距离 —— 下一帧 CalcCamera 按新朝向重算位置，看起来就是「绕着焦点转」。
	OrbitRotation.Yaw += DeltaYaw;
	// 夹到 ±89°：到 90° 时视线与 up 平行，Rotation 的 roll 会翻掉，画面会打转。
	OrbitRotation.Pitch = FMath::Clamp(OrbitRotation.Pitch + DeltaPitch, -89.0, 89.0);
	OrbitRotation.Roll = 0.0;
}

void AStarMapCameraPawn::AddZoom(float Value)
{
	// 夹的是**目标距离本身**，不是最终算出来的相机位置 —— 这条经验来自飞船的
	// AddZoom（SpaceshipPawn.cpp:469-471）：夹最终值的话，玩家滚过头会攒下一堆
	// 被 Clamp 吃掉的无效偏移，往回滚要先「还债」，前几格看起来像没反应。
	//
	// 取负号：Value > 0 是滚轮上滚 = 拉近 = 距离变小。
	const double Factor = FMath::Pow(ZoomStepFactor, -double(Value));
	OrbitDistanceCm = FMath::Clamp(
		OrbitDistanceCm * Factor, MinOrbitDistanceCm, MaxOrbitDistanceCm);
}

AActor* AStarMapCameraPawn::ResolveSelectedBody() const
{
	UWorld* World = GetWorld();
	UCosmosSimulationSubsystem* Subsystem = World
		? World->GetSubsystem<UCosmosSimulationSubsystem>()
		: nullptr;
	return Subsystem ? Subsystem->GetSelectedBody() : nullptr;
}

void AStarMapCameraPawn::CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult)
{
	// 先让父类从相机组件取 FOV / 后处理等，再把位置和朝向覆盖掉。
	Super::CalcCamera(DeltaTime, OutResult);

	AActor* Selected = ResolveSelectedBody();
	if (Selected != LastSeenSelected.Get())
	{
		// 选中状态变了。从**当前**相机位置反推绕行参数：视线方向 = 相机→焦点，
		// 距离 = 两者实际距离。下面重算出的位置因此与当前位置重合 —— 不跳变。
		LastSeenSelected = Selected;
		if (Selected)
		{
			const FVector ToFocus = Selected->GetActorLocation() - CameraLocation;
			const double Distance = ToFocus.Size();

			// 夹距离本身（不是夹算出来的位置）。夹是必要的：相机不能钻进星体里。
			// 真夹到时会有一次跳变，这是唯一允许跳变的场合。
			OrbitDistanceCm = FMath::Clamp(Distance, MinOrbitDistanceCm, MaxOrbitDistanceCm);

			if (!ToFocus.IsNearlyZero())
			{
				OrbitRotation = ToFocus.Rotation();
			}
		}
	}

	if (Selected)
	{
		CameraLocation = Selected->GetActorLocation()
			- OrbitRotation.Vector() * OrbitDistanceCm;
	}
	// 未选中：CameraLocation 保持不动 —— 这就是「冻结」。

	OutResult.Location = CameraLocation;
	OutResult.Rotation = OrbitRotation;
}

AActor* AStarMapCameraPawn::PickBodyAtScreenPosition(
	TArrayView<const FStarMapScreenCandidate> Candidates,
	const FVector2D& ScreenPoint,
	float PixelRadius)
{
	const double RadiusSq = double(PixelRadius) * double(PixelRadius);
	double BestDistSq = TNumericLimits<double>::Max();
	AActor* Best = nullptr;

	for (const FStarMapScreenCandidate& Candidate : Candidates)
	{
		if (!Candidate.Actor)
		{
			continue;
		}

		const double DistSq = FVector2D::DistSquared(Candidate.ScreenPosition, ScreenPoint);
		if (DistSq > RadiusSq)
		{
			// 超出容差。边界含：恰好等于半径不算「超出」。
			continue;
		}

		if (DistSq < BestDistSq)
		{
			// 严格小于 —— 等距时先出现的赢，结果确定。
			BestDistSq = DistSq;
			Best = Candidate.Actor;
		}
	}

	return Best;
}
```

- [ ] **Step 5: 补 BOM 并构建**

```bash
for f in Source/StellarEra/Public/StarMapCameraPawn.h \
         Source/StellarEra/Private/StarMapCameraPawn.cpp \
         Source/StellarEra/Private/StarMapTests.cpp ; do
  printf '%-62s %s\n' "$f" "$(head -c3 "$f" | xxd -p)"
done
```

都必须是 `efbbbf`。然后构建（日志 `/tmp/starmap-t3-build.log`），预期 `Result: Succeeded`、无 `error C`。

- [ ] **Step 6: 跑测试**

同 T1 Step 5 的命令（日志 `/tmp/starmap-t3-test.log`）。

预期：三个测试（`Selection` / `Highlight` / `Picking`）全部 `Result={Success}`。

- [ ] **Step 7: 提交**

```bash
git add \
  Source/StellarEra/Public/StarMapCameraPawn.h \
  Source/StellarEra/Private/StarMapCameraPawn.cpp \
  Source/StellarEra/Private/StarMapTests.cpp

git commit -m "$(cat <<'EOF'
feat(星图): 相机 Pawn 与屏幕空间拾取

相机不 tick，位置在 CalcCamera 里现算。UWorld::Tick 的顺序是「Actor tick 组
→ TickObjects（子系统在这里推进天体）→ UpdateCameraManager（CalcCamera 在
这里被调用）→ TG_PostUpdateWork」，任何 tick 组都不在那个窗口里，只有
CalcCamera 零滞后。

选中状态变化时从当前相机位置反推绕行参数，于是选中/换选中**位置不变、朝向重算**
（朝向被整个替换掉，只有原本正对该天体时才不变）；未选中
时 CameraLocation 不更新，就是「冻结在原地」。

拾取是纯函数：候选给成一个结构体数组（不是两个平行数组），投影留在调用方。
边界含、等距取先出现的、跳过空 Actor —— 六组断言覆盖。

Co-Authored-By: Claude Code <noreply@anthropic.com>
EOF
)"
```

---

### Task 4: 控制器接线

**Files:**
- Modify: `Source/StellarEra/Public/StellarEraPlayerController.h`
- Modify: `Source/StellarEra/Private/StellarEraPlayerController.cpp`

**Interfaces:**
- Consumes: T1 的五个子系统方法；T3 的 `AStarMapCameraPawn`（`EnterFrom` / `OrbitLook` / `AddZoom` / `ResolveSelectedBody` / `PickBodyAtScreenPosition`）与 `FStarMapScreenCandidate`
- Produces: 无（本任务是最上层）

> **本任务无法无头验证。** 相机观感、点击手感、高亮够不够明显都只能人眼看 ——
> 按本项目惯例归验收步骤的用户，**不要派子代理去「验」**（它要么报「验不了」白跑，
> 要么编一个结论）。本任务的可验证部分只有「编译通过」。

- [ ] **Step 1: 改头文件**

`Source/StellarEra/Public/StellarEraPlayerController.h`。

第 10 行 `class ASpaceshipPawn;` 之前加：

```cpp
class AStarMapCameraPawn;
```

在 `protected:` 区的 `void OnZoom(const FInputActionValue& Value);`（第 83 行）之后加：

```cpp
	/**
	 * 进入 / 退出星图模式。
	 *
	 * 方向由「当前 Possess 的是不是星图 Pawn」判定，**不用单独的状态标志** ——
	 * 幂等，也不会和实际 Possess 的 Pawn 不同步。
	 *
	 * 做成无参是为了直接绑给 Enhanced Input：FEnhancedInputActionHandlerSignature
	 * 就是一个无参委托（EnhancedInputComponent.h:490 的 DEFINE_BIND_ACTION）。
	 */
	void ToggleStarMap();

	/** 左键按下：记下位置，用来区分「点击」和「拖拽」。 */
	void OnStarMapSelectStarted(const FInputActionValue& Value);

	/** 左键松开：位移小于阈值算点击（拾取），否则算拖拽结束（不改变选中）。 */
	void OnStarMapSelectCompleted(const FInputActionValue& Value);

	/** 把已注册天体投到屏幕，交给纯函数挑出光标下的那颗。没有就是 nullptr。 */
	AActor* PickBodyUnderCursor() const;
```

在 `private:` 区、`bool bLookHeld = false;`（第 119 行）之后加：

```cpp
	/**
	 * 星图里左键是否按住。与 bLookHeld 同理：换 Pawn 时必须清零，否则按着左键
	 * 进出星图之后，松开事件送到新 Pawn 身上，这个标志就永远停在 true。
	 */
	bool bStarMapSelectHeld = false;

	/** 左键按下的屏幕位置（像素）。用 double 存 —— 与 GetMousePosition 的 double 重载对齐。 */
	FVector2D StarMapPressPosition = FVector2D::ZeroVector;

	/** 拾取的像素容差。20 像素保证远处的星球也点得中。 */
	UPROPERTY(EditAnywhere, Category = "StarMap")
	float SelectionPixelRadius = 20.f;

	/** 星图相机 Pawn。懒创建并复用，不每次进出都 Spawn/Destroy。 */
	UPROPERTY(Transient)
	TObjectPtr<AStarMapCameraPawn> StarMapPawn;

	/** 进入星图前 Possess 的 Pawn。退出时换回去。弱引用 —— 它可能已经没了。 */
	TWeakObjectPtr<APawn> PreviousPawn;
```

在 `IA_Interact`（第 168 行）之后加：

```cpp
	/** 星图模式开关（M）。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> IA_StarMapToggle;

	/** 星图里选中天体 / 拖拽转视角（鼠标左键）。按下与松开的位移区分两者。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> IA_StarMapSelect;
```

- [ ] **Step 2: 加 include 与匿名 namespace 常量**

`Source/StellarEra/Private/StellarEraPlayerController.cpp`。

第 5-7 行的项目头之后加：

```cpp
#include "CosmosSimulationSubsystem.h"
#include "StarMapCameraPawn.h"
```

匿名 namespace 里（`MapKey` 之后）加：

```cpp
	/**
	 * 左键「点击」与「拖拽」的分界（像素）。
	 *
	 * 同一个左键既选中又转视角，靠按下与松开的位移区分：小于它算点击（拾取），
	 * 否则算拖拽结束（不改选中）。太空里天体在动，手抖几个像素很正常，
	 * 阈值太小会把「想选中」判成「想转视角」。
	 */
	constexpr double ClickMoveThresholdPx = 5.0;
```

- [ ] **Step 3: 构造两个输入动作并映射按键**

`BuildInputAssets()` 里，`IA_Interact = MakeAction(...)`（第 202 行）之后加：

```cpp
	IA_StarMapToggle = MakeAction(this, TEXT("IA_StarMapToggle"), EInputActionValueType::Boolean);
	IA_StarMapSelect = MakeAction(this, TEXT("IA_StarMapSelect"), EInputActionValueType::Boolean);
```

`MapKey(MappingContext, IA_Interact, EKeys::E);`（第 234 行）之后加：

```cpp
	MapKey(MappingContext, IA_StarMapToggle, EKeys::M);
	MapKey(MappingContext, IA_StarMapSelect, EKeys::LeftMouseButton);
```

- [ ] **Step 4: 绑定回调**

`SetupInputComponent()` 里，`IA_Zoom` 那条绑定（第 99 行）之后加：

```cpp
	EnhancedInput->BindAction(IA_StarMapToggle, ETriggerEvent::Started,
		this, &AStellarEraPlayerController::ToggleStarMap);
	EnhancedInput->BindAction(IA_StarMapSelect, ETriggerEvent::Started,
		this, &AStellarEraPlayerController::OnStarMapSelectStarted);
	EnhancedInput->BindAction(IA_StarMapSelect, ETriggerEvent::Completed,
		this, &AStellarEraPlayerController::OnStarMapSelectCompleted);
```

> `IA_Look`（`Mouse2D`）与 `IA_Zoom`（`MouseWheelAxis`）**直接复用**，不新建 ——
> 它们本来就按 Pawn 类型分派，各加一个星图分支即可（Step 6 / 7）。

- [ ] **Step 5: `ApplyMouseModeForPawn` 加第三个分支**

**这一步是必需的，不是优化。** 不改的话星图 Pawn 会掉进 `else`（被当成飞船）→
光标隐藏 + 永久捕获 → **点不了任何东西**。

把第 125-178 行的函数体改成：

```cpp
	// 换 Pawn 时把两个「按住」标志都清零。按着键登船/离船/进星图的话，松开事件
	// 会送到新 Pawn 身上，标志就永远停在 true —— 角色一下船就自己转视角，
	// 或者星图里没按左键也在转视角。
	bLookHeld = false;
	bStarMapSelectHeld = false;

	const bool bWalking = Cast<APlanetCharacter>(InPawn) != nullptr;
	const bool bStarMap = Cast<AStarMapCameraPawn>(InPawn) != nullptr;
	bShowMouseCursor = bWalking || bStarMap;

	UGameViewportClient* ViewportClient = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
	if (!ViewportClient)
	{
		return;
	}

	if (bWalking)
	{
		SetInputMode(FInputModeGameAndUI()
			.SetLockMouseToViewportBehavior(EMouseLockMode::LockOnCapture)
			.SetHideCursorDuringCapture(false));
		ViewportClient->SetMouseCaptureMode(EMouseCaptureMode::CaptureDuringRightMouseDown);
	}
	else if (bStarMap)
	{
		// 星图：光标要可见可点，而且**必须有捕获** —— SceneViewport 里 MouseDelta 的
		// 累加整段包在 if (bViewportHasCapture) 里（SceneViewport.cpp:905 判定、
		// :923-933 累加），没有捕获就收不到任何位移，左键拖拽等于没接上。
		// CaptureDuringMouseDown 正是为此存在的枚举：「按住鼠标时捕获，松开释放」
		// （EngineBaseTypes.h:49）。角色用的是右键版，星图要任意键版。
		SetInputMode(FInputModeGameAndUI()
			.SetLockMouseToViewportBehavior(EMouseLockMode::LockOnCapture)
			.SetHideCursorDuringCapture(false));
		ViewportClient->SetMouseCaptureMode(EMouseCaptureMode::CaptureDuringMouseDown);
	}
	else
	{
		SetInputMode(FInputModeGameOnly());
		ViewportClient->SetHideCursorDuringCapture(true);
	}
```

- [ ] **Step 6: `OnLook` 加星图分支**

在 `OnLook` 里 `else if (ASpaceshipPawn* Ship = ...)` 那条（第 263-267 行）**之后**加：

```cpp
	else if (AStarMapCameraPawn* StarMap = Cast<AStarMapCameraPawn>(GetPawn()))
	{
		// 星图里左键拖拽就是转视角，不设 bLookHeld 那道门 —— 那道门是给角色
		// 「不按右键就别动视角」用的；星图的相机除了转视角没有别的职责。
		StarMap->OrbitLook(DeltaYaw, DeltaPitch);
	}
```

- [ ] **Step 7: `OnZoom` 加星图分支**

在 `OnZoom` 里 `else if (ASpaceshipPawn* Ship = ...)` 那条（第 374-377 行）**之后**加：

```cpp
	else if (AStarMapCameraPawn* StarMap = Cast<AStarMapCameraPawn>(GetPawn()))
	{
		StarMap->AddZoom(Value.Get<float>());
	}
```

- [ ] **Step 8: `PlayerTick` 加星图提示**

在 `PlayerTick` 里 `else if (ASpaceshipPawn* Ship = ...)` 那条（第 460-468 行）**之后**加：

```cpp
	else if (AStarMapCameraPawn* StarMap = Cast<AStarMapCameraPawn>(GetPawn()))
	{
		if (AActor* Selected = StarMap->ResolveSelectedBody())
		{
			ShowPrompt(FString::Printf(
				TEXT("%s　左键拖拽绕行　滚轮缩放　再点它取消选中　M 退出"),
				*Selected->GetName()));
		}
		else
		{
			ShowPrompt(TEXT("左键点击选中天体　拖拽转视角　滚轮缩放　M 退出星图"));
		}
	}
```

- [ ] **Step 9: 实现 `ToggleStarMap` / 两个回调 / `PickBodyUnderCursor`**

在 `ShowPrompt`（第 541 行）**之前**插入：

```cpp
void AStellarEraPlayerController::ToggleStarMap()
{
	AStarMapCameraPawn* CurrentStarMapPawn = Cast<AStarMapCameraPawn>(GetPawn());

	UCosmosSimulationSubsystem* Subsystem =
		GetWorld() ? GetWorld()->GetSubsystem<UCosmosSimulationSubsystem>() : nullptr;
	if (!Subsystem)
	{
		return;
	}

	if (CurrentStarMapPawn)
	{
		// ── 退出 ──
		Subsystem->SetSelectedBody(nullptr);
		Subsystem->SetStarMapActive(false);

		// PreviousPawn 可能已经没了（比如角色在星图里被销毁）。那时**不 Possess** ——
		// 留在相机上，再按 M 还能进，且不会崩。
		if (APawn* ReturnPawn = PreviousPawn.Get())
		{
			Possess(ReturnPawn);
		}
		PreviousPawn.Reset();
		return;
	}

	// ── 进入 ──
	if (!StarMapPawn)
	{
		// 懒创建，之后复用：每次进出都 Spawn/Destroy 没必要。
		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = this;
		SpawnParams.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		StarMapPawn = GetWorld()->SpawnActor<AStarMapCameraPawn>(
			AStarMapCameraPawn::StaticClass(), FTransform::Identity, SpawnParams);
	}
	if (!StarMapPawn)
	{
		return;
	}

	// 「进入时相机原地不动、不选中」：把进入前的视角直接交给它当基准。
	// 用 GetPlayerViewPoint 而不是读 Pawn 的变换 —— 相机在 tick 组之后才更新，
	// Pawn 的变换不等于玩家此刻看到的画面。
	FVector ViewLocation;
	FRotator ViewRotation;
	GetPlayerViewPoint(ViewLocation, ViewRotation);
	StarMapPawn->EnterFrom(ViewLocation, ViewRotation);

	PreviousPawn = GetPawn();
	Subsystem->SetSelectedBody(nullptr);
	Subsystem->SetStarMapActive(true);
	Possess(StarMapPawn);
}

void AStellarEraPlayerController::OnStarMapSelectStarted(const FInputActionValue& Value)
{
	if (!Cast<AStarMapCameraPawn>(GetPawn()))
	{
		return;
	}

	// 用 double 重载：引擎两个重载都提供了（PlayerController.h:1623 float /
	// :1624 double），屏幕坐标没有理由退到 float。
	double MouseX = 0.0;
	double MouseY = 0.0;
	if (GetMousePosition(MouseX, MouseY))
	{
		StarMapPressPosition = FVector2D(MouseX, MouseY);
		bStarMapSelectHeld = true;
	}
}

void AStellarEraPlayerController::OnStarMapSelectCompleted(const FInputActionValue& Value)
{
	if (!bStarMapSelectHeld)
	{
		return;
	}
	bStarMapSelectHeld = false;

	if (!Cast<AStarMapCameraPawn>(GetPawn()))
	{
		return;
	}

	double MouseX = 0.0;
	double MouseY = 0.0;
	if (!GetMousePosition(MouseX, MouseY))
	{
		return;
	}

	// 按下与松开的位移大于阈值 → 这是「拖拽转视角」的收尾，不改选中。
	const double MovedSq = FVector2D::DistSquared(
		StarMapPressPosition, FVector2D(MouseX, MouseY));
	if (MovedSq > ClickMoveThresholdPx * ClickMoveThresholdPx)
	{
		return;
	}

	// 点空白什么都不做（spec §5.2：取消选中会让相机停止跟随、丢失目标，
	// 绑在误点很容易触发的动作上不划算）。要取消就明确地点回它自己。
	AActor* Picked = PickBodyUnderCursor();
	if (!Picked)
	{
		return;
	}

	UCosmosSimulationSubsystem* Subsystem =
		GetWorld() ? GetWorld()->GetSubsystem<UCosmosSimulationSubsystem>() : nullptr;
	if (!Subsystem)
	{
		return;
	}

	Subsystem->SetSelectedBody(
		Subsystem->GetSelectedBody() == Picked ? nullptr : Picked);
}

AActor* AStellarEraPlayerController::PickBodyUnderCursor() const
{
	UCosmosSimulationSubsystem* Subsystem =
		GetWorld() ? GetWorld()->GetSubsystem<UCosmosSimulationSubsystem>() : nullptr;
	if (!Subsystem)
	{
		return nullptr;
	}

	TArray<AActor*> Bodies;
	Subsystem->GetRegisteredBodies(Bodies);

	// 投影留在调用方，拾取函数才能是纯函数、才能无头测。
	TArray<FStarMapScreenCandidate> Candidates;
	Candidates.Reserve(Bodies.Num());
	for (AActor* Body : Bodies)
	{
		FVector2D ScreenPosition;
		if (Body && ProjectWorldLocationToScreen(Body->GetActorLocation(), ScreenPosition))
		{
			Candidates.Add({ Body, ScreenPosition });
		}
	}

	double MouseX = 0.0;
	double MouseY = 0.0;
	if (!GetMousePosition(MouseX, MouseY))
	{
		return nullptr;
	}

	return AStarMapCameraPawn::PickBodyAtScreenPosition(
		Candidates, FVector2D(MouseX, MouseY), SelectionPixelRadius);
}
```

> **注意 C4458**：`OnStarMapSelectStarted` / `Completed` 里的局部变量**不能**叫
> `Character` —— `AController` 有同名成员，UE 5.8 把它当硬错误。上面的写法没有这个问题。

- [ ] **Step 10: 构建**

同 T1 Step 2 的命令（日志 `/tmp/starmap-t4-build.log`）。预期 `Result: Succeeded`、无 `error C`。

- [ ] **Step 11: 跑回归测试**

同 T1 Step 5 的命令（日志 `/tmp/starmap-t4-test.log`）。预期三个测试仍全 `Result={Success}`（本任务没动它们的代码，这一步是确认没有意外破坏）。

- [ ] **Step 12: 提交**

```bash
git add \
  Source/StellarEra/Public/StellarEraPlayerController.h \
  Source/StellarEra/Private/StellarEraPlayerController.cpp

git commit -m "$(cat <<'EOF'
feat(星图): 控制器接线 —— M 键进出、左键选中与拖拽、滚轮缩放

进入时 Possess 一个新建的相机 Pawn。控制器里现有的 15 个输入回调里 13 个按
Pawn 类型分派，换 Pawn 后自动失效；剩下 2 个只翻转 bLookHeld，而 bLookHeld
唯一的读点在 OnLook 的角色分支里 —— 星图分支不读它、换 Pawn 时也会清零，
所以同样无影响。一行现有代码都不用改。

ApplyMouseModeForPawn 加第三个分支是必需的：星图 Pawn 否则会掉进「当成飞船」
的 else，光标隐藏 + 永久捕获，点不了任何东西。用 CaptureDuringMouseDown
而不是角色的 CaptureDuringRightMouseDown —— MouseDelta 的累加整段包在
if (bViewportHasCapture) 里，没有捕获就收不到位移。

同一个左键既选中又转视角，靠按下与松开的 5 像素阈值区分。点空白不改选中，
点已选中的那颗取消选中。

Co-Authored-By: Claude Code <noreply@anthropic.com>
EOF
)"
```

---

### Task 5: 收尾 —— BOM 复查、spec 状态、端到端验收

**Files:**
- Modify: `docs/superpowers/specs/2026-10-17-starmap-mode-design.md`

**Interfaces:**
- Consumes: 前四个任务的全部产物
- Produces: 无

- [ ] **Step 1: 逐项 BOM 复查**

```bash
for f in \
  Source/StellarEra/Private/StarMapTests.cpp \
  Source/StellarEra/Public/StarMapCameraPawn.h \
  Source/StellarEra/Private/StarMapCameraPawn.cpp \
  Source/StellarEra/Public/CosmosSimulationSubsystem.h \
  Source/StellarEra/Private/CosmosSimulationSubsystem.cpp \
  Source/StellarEra/Public/CosmosOrbitComponent.h \
  Source/StellarEra/Public/CosmosTrajectoryComponent.h \
  Source/StellarEra/Private/CosmosTrajectoryComponent.cpp \
  Source/StellarEra/Public/StellarEraPlayerController.h \
  Source/StellarEra/Private/StellarEraPlayerController.cpp ; do
  printf '%-62s %s\n' "$f" "$(head -c3 "$f" | xxd -p)"
done
```

每一行都必须是 `efbbbf`。有缺的补上并重建。

- [ ] **Step 2: 用户端到端验收（只能人看，不派子代理）**

在 `UniverseMap`（有体素行星的那个关卡）里 PIE。**先确认编辑器窗口在前台** ——
编辑器不在前台时整个进程被硬限到 3 FPS，看到的卡顿是假的。

| # | 做什么 | 预期 |
|---|---|---|
| 1 | 进入后按 M | 画面**不跳变**（相机原地不动），光标出现 |
| 2 | 左键点一颗行星 | 它被选中：它的预测线与拖尾**变亮**（深蓝 → 浅蓝）；相机开始跟着它 |
| 3 | 按住左键拖 | 视角绕着它转，不改变选中 |
| 4 | 滚轮上下 | 拉近 / 拉远。拉到底不会钻进星球里 |
| 5 | 左键点另一颗行星 | 换选中，**相机位置不变、朝向重算**（方向与距离从当前位置重新推导） |
| 6 | 再点已选中的那颗 | 取消选中；相机**停在原地**不再跟随 |
| 7 | 点空白 | 什么都不发生（选中不变） |
| 8 | 按 M 退出 | 回到进入前的 Pawn（角色或飞船），鼠标模式恢复（角色能看到光标、飞船光标隐藏） |
| 9 | 普通模式下（没进星图） | **看不到任何轨迹** —— 这是 spec §5.4 明确写下的观感变化 |
| 10 | 某颗行星勾上 `bShowOutsideStarMap` | 普通模式下又能看到**它**的轨迹，其它天体不变 |
| 11 | 重复按 M 若干次 | 每次都对（方向由当前 Pawn 判定，幂等），不崩 |
| 12 | 进星图后角色所在的行星 | 角色照常被星球携带（`CarryRiders` 与 Possess 无关），只是没有输入 |

在 `NewMap`（没有天体）里也按一次 M：拾取遍历空列表 → 点不中 → 相机原地，**不崩**。

- [ ] **Step 3: 性能检查**

PIE 里 `stat unit`，看 **Game** 项。与「不进星图」时对比，进入星图后的增量应当在
**1 毫秒以内**。同时确认帧率没有明显回归。

若超标，按这个顺序调（**每次只动一个**，改完重测）：

1. 拾取与帧率无关（它只在点击那一帧跑一次），**不要动 `SelectionPixelRadius`**
2. 预测是每帧重算的大头 —— 先在**所有**天体上取消勾选 `bDrawTrajectory`，
   看 Game 时间是否掉回去，以确认增量确实来自预测与画线，而不是别的东西

> **不要拿帧率当结论。** 编辑器不在前台时 3 FPS 硬限会把任何测量都变成噪声。

- [ ] **Step 4: 更新 spec 状态**

把 `docs/superpowers/specs/2026-10-17-starmap-mode-design.md` 第 5 行的
`- **状态**：设计已确认，待实现` 改成 `- **状态**：已实现`。

**必须等 Step 2 通过之后再做** —— 别把没验过的「已实现」写进文档。

- [ ] **Step 5: 提交（等用户发话）**

本项目规则是只在用户明确要求时提交。用户说「提交」之后：

```bash
git add \
  Source/StellarEra/Public/CosmosSimulationSubsystem.h \
  Source/StellarEra/Private/CosmosSimulationSubsystem.cpp \
  Source/StellarEra/Private/StarMapTests.cpp \
  Source/StellarEra/Public/CosmosOrbitComponent.h \
  Source/StellarEra/Public/CosmosTrajectoryComponent.h \
  Source/StellarEra/Private/CosmosTrajectoryComponent.cpp \
  Source/StellarEra/Public/StarMapCameraPawn.h \
  Source/StellarEra/Private/StarMapCameraPawn.cpp \
  Source/StellarEra/Public/StellarEraPlayerController.h \
  Source/StellarEra/Private/StellarEraPlayerController.cpp \
  docs/superpowers/specs/2026-10-17-starmap-mode-design.md \
  docs/superpowers/plans/2026-10-17-starmap-mode.md

git commit -m "$(cat <<'EOF'
docs(星图): 标记设计已实现

星图模式已按 spec 实现并经 PIE 验收。修正 spec 里的三处：相机改成在
CalcCamera 里现算（任何 tick 组都不在「子系统推进天体」与
「UpdateCameraManager 取相机」之间）、拾取候选改成一个结构体数组、
补上 bDrawTrajectory 注释必须同改的说明。

Co-Authored-By: Claude Code <noreply@anthropic.com>
EOF
)"
```

---

## 与 spec 的三处有意偏差

计划按 spec 写，但有三处**改动了 spec**（spec 文件已同步更正，此处留档）：

| # | spec 原文 | 改成 | 为什么 |
|---|---|---|---|
| 1 | §5.1：相机 Pawn 的 tick group 设 `TG_PostUpdateWork`，并说「滞后一帧肉眼看不出来」 | 相机不 tick，位置在 `CalcCamera` 里现算 | 去核了 `LevelTick.cpp`：`FTickableGameObject::TickObjects`（子系统推进天体）在前、`UpdateCameraManager`（调用 `CalcCamera`）紧跟其后、`TG_PostUpdateWork` 在其**后**。所以 `TG_PostUpdateWork` 会让相机读到自己已算完的位置，`TG_PrePhysics` 会读到天体上一帧的位置 —— **两条路都差一帧**，而 `CalcCamera` 零滞后。既然有零滞后的钩子，就没有理由接受一帧滞后 |
| 2 | §9：拾取函数收「`TArrayView<AActor* const>` + `TArray<FVector2D>`」两个平行数组 | 收一个 `TArrayView<const FStarMapScreenCandidate>` | 平行数组靠下标约定对齐，结构体靠类型对齐；下标错位编译器看不见 |
| 3 | §5.4：只说了采样前移，没说 `bDrawTrajectory` 的注释要跟着改 | 明确写进 Task 2 Step 3 | 采样上移之后「关掉可省下它的全部开销」这句注释就假了。本项目里**注释声称的事实**和代码是两类错误，只核一类会漏 |

---

## 自审记录

**1. Spec 覆盖检查**

| spec 章节 | 落在哪个 Task |
|---|---|
| §2 需求 1（M 进出） | Task 4 Step 1/3/4/9 |
| §2 需求 2（点击选中） | Task 3（拾取函数）+ Task 4 Step 9（调用） |
| §2 需求 3（鼠标改变视角） | Task 3（`OrbitLook` / `AddZoom` / `CalcCamera`）+ Task 4 Step 6/7 |
| §2 需求 4（轨迹只在此模式显示） | Task 2 Step 3（`bShowOutsideStarMap`）+ Step 6（门控） |
| §3 六个决策 | 决策 1 → Task 3；决策 2 → Task 2 Step 6；决策 3 → Task 2 Step 6（高亮）；决策 4 → Task 4 Step 3；决策 5 → Task 3 `EnterFrom` + Task 4 Step 9；决策 6 → Task 2 Step 4/5 |
| §4.1 新 Pawn 而非切视图目标 | Task 3 + Task 4 Step 9 |
| §4.2 屏幕空间拾取 | Task 3 Step 3/4（`PickBodyAtScreenPosition`）+ Task 4 Step 9（`PickBodyUnderCursor`） |
| §4.3 选中状态放子系统 | Task 1 |
| §5.1 Pawn 状态与鼠标操作 | Task 3 Step 3/4 |
| §5.2 选中/取消选中：位置不变、朝向重算 | Task 3 Step 4（`CalcCamera` 里的转换分支） |
| §5.3 子系统五个方法 | Task 1 Step 3 |
| §5.4 改动 A（采样前移） | Task 2 Step 6 |
| §5.4 改动 B（`bShowOutsideStarMap`） | Task 2 Step 3 |
| §5.5 高亮只改颜色 | Task 2 Step 4/5/6 |
| §6.1 两个新动作 | Task 4 Step 3 |
| §6.2 回调分派表 | Task 4 Step 4/6/7/9 |
| §6.3 鼠标模式第三分支 | Task 4 Step 5 |
| §7 退出与边界 | Task 4 Step 9（`PreviousPawn` 失效不 Possess）+ Task 5 Step 2 第 11/12 行 |
| §8 提示 | Task 4 Step 8 |
| §9 测试 | Task 1/2/3 各自的 Step 1；「验不了、必须人看」归 Task 5 Step 2 |
| §10 已知取舍 | 取舍 1/2 → Task 4 Step 9 与 Task 5 Step 2；取舍 3 → Task 2 Step 4；取舍 4/5 → Task 2 Step 6 |
| §11 涉及文件 | 「文件结构」表逐条对上；`StellarEra.Build.cs` 未列入 = 不动 |
| §12 引擎事实 | Task 2 Step 6（行批）、Task 4 Step 5（捕获模式）、Task 4 Step 9（`GetMousePosition` 双精度重载）；`ProjectWorldLocationToScreen` 用在 Task 4 Step 9 |

**2. 占位符扫描**

无 TBD / TODO /「稍后实现」/「类似 Task N」。每个代码步骤都是完整可粘贴的代码。

Task 2 Step 6 里出现「原注释整段保留」的标记 —— 那不是占位符，是**明确要求保留
已复审过的既有注释**，且逐条指出了是哪几段（各段首句都给了）。实施者只需替换
函数骨架，不动那几段文字。

**3. 类型一致性**

- `SetSelectedBody(AActor*)` / `GetSelectedBody() const → AActor*` —— Task 1 定义，Task 2/3/4 调用，一致
- `GetRegisteredBodies(TArray<AActor*>&) const` —— Task 1 定义，Task 4 Step 9 调用，一致
- `IsStarMapActive() const → bool` / `SetStarMapActive(bool)` —— Task 1 定义，Task 2 Step 6 与 Task 4 Step 9 调用，一致
- `BrightenTowardWhite(const FColor&, float) → FColor` —— Task 2 Step 4 声明、Step 5 实现、Step 6 调用、Step 1 测试，四处一致
- `ClearDrawnLines()` —— Task 2 Step 4 声明、Step 6 实现（两处调用），一致
- `bShowOutsideStarMap` —— Task 2 Step 3 定义、Step 6 在 `bVisible` 里读，一致
- `FStarMapScreenCandidate { AActor* Actor; FVector2D ScreenPosition; }` —— Task 3 Step 3 定义、Step 4 实现里读、Task 3 Step 1 测试里构造、Task 4 Step 9 构造，字段名一致
- `PickBodyAtScreenPosition(TArrayView<const FStarMapScreenCandidate>, const FVector2D&, float) → AActor*` —— Task 3 Step 3 声明、Step 4 实现、Step 1 测试、Task 4 Step 9 调用，四处一致
- `EnterFrom(const FVector&, const FRotator&)` —— Task 3 Step 3 声明、Step 4 实现、Task 4 Step 9 调用，一致
- `OrbitLook(float, float)` / `AddZoom(float)` —— Task 3 定义，Task 4 Step 6/7 调用，一致
- `ResolveSelectedBody() const → AActor*` —— Task 3 定义，Task 4 Step 8 调用，一致
- `ToggleStarMap()` 无参 —— Task 4 Step 1 声明、Step 9 实现、Step 4 绑定。无参是被 `FEnhancedInputActionHandlerSignature` 支持的（已核 `EnhancedInputComponent.h:490`）
- 属性名逐字核对：`ZoomStepFactor` / `MinOrbitDistanceCm` / `MaxOrbitDistanceCm` / `SelectionPixelRadius` / `StarMapPressPosition` / `bStarMapSelectHeld` / `StarMapPawn` / `PreviousPawn` / `IA_StarMapToggle` / `IA_StarMapSelect` —— 声明处与使用处一致
- `ClickMoveThresholdPx` —— Task 4 Step 2 定义、Step 9 使用，一致

**4. 依赖顺序**

T1 → T2（需要 `IsStarMapActive` / `GetSelectedBody`）；T1 → T3（需要 `GetSelectedBody`）；
T3 → T4（需要 Pawn 与拾取函数）。**无环，无并行分支**，必须按 1→2→3→4→5 顺序做。

**5. 已知的下游风险（不在本计划内修）**

`CosmosTrajectoryComponent::TickComponent` 里 `!Subsystem` 那条早退**不 Flush**
（T5 留下的既有缺口，spec §5.4 第 2 步明确说「本轮不动」）。本计划的重排没有
让它变好也没有变坏 —— 但重排之后它前面多了一段采样，复审时别把它当成新引入的缺陷。
