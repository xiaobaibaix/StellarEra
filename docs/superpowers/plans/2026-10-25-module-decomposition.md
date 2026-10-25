# StellarEra 11 模块拆分实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把现在 4 个按层划分的 UE 模块拆成 11 个按领域划分的模块，玩法、数值、资产与类名全部不变。

**Architecture:** 每个任务独立创建/搬移一组类，改 API 宏、改 `Build.cs`、补 `[CoreRedirects]`，然后构建 + 跑 `StellarEra.*` 自动化测试 + 提交。任务顺序按依赖方向从顶层往下：先拆 Cosmos、UI，再拆 PlanetEdit / Character / Spaceship，然后把行星组件挪到蓝图（破环），最后拆 Geology / Atmosphere / Minerals 并把 `StellarEraPlanet` 改名为 `StellarEraTerrain`。

**Tech Stack:** Unreal Engine 5.8.3、C++、UBT（捆绑 dotnet 直调）、UE Automation Test、Git LFS。

**Spec:** `docs/superpowers/specs/2026-10-25-module-decomposition-design.md`

## Global Constraints

- **构建前必须关闭编辑器**（Live Coding 会锁 DLL）。构建命令（下文记作 **BUILD**）：

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" \
  "D:/Application/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" \
  StellarEraEditor Win64 Development \
  -Project="F:/workspace/ue/StellarEra/StellarEra.uproject" -WaitMutex -FromMsBuild
```
预期末行 `Result: Succeeded`。**不要用 `Build.bat`**（锁机制会让 UBT 报找不到 .uproject）。

- **自动化测试**（编辑器开着也能跑，下文记作 **TEST**；约 25 秒）：

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout \
  -abslog="F:/workspace/ue/StellarEra/Saved/Logs/ModuleSplit.log"
grep -E "Result: (Passed|Failed)" F:/workspace/ue/StellarEra/Saved/Logs/ModuleSplit.log | sort | uniq -c
```
预期：出现 `Result: Passed`，**没有** `Result: Failed`。注意 editor world 不执行 `BeginPlay`，涉及 BeginPlay 的验证只能放到 Task 13 的 PIE。

- **提交必须带 pathspec。** 索引里有常驻暂存文件，裸 `git commit` 会吞掉全部改动。每次先 `git add <本次新增文件>`，再
  `git commit -m "<消息>" -- <本次涉及的路径>`。消息末尾加 `Co-Authored-By: Claude Code <noreply@anthropic.com>`。

- **编码：** 含中文的 `.h` / `.cpp` / `.cs` 必须是 UTF-8 **with BOM**。新文件写完后执行
  `bom <文件>`，其中：

```bash
bom() { f="$1"; [ "$(head -c 3 "$f" | od -An -tx1 | tr -d ' \n')" = "efbbbf" ] || { printf '\xef\xbb\xbf' | cat - "$f" > "$f.bom" && mv "$f.bom" "$f"; }; }
```

- **不要统一换行符。** 仓库本来就是 CRLF/LF 混合，`autocrlf=true` 下 CRLF 是稳定态。
- **API 宏必须与模块名一致。** 类的宏和它所在模块不匹配时，链接期报 `unresolved external` 或 DLL 导入不匹配。
- **模块名 = 目录名 = `Build.cs` 类名**，`IMPLEMENT_MODULE` 的第二个参数也是它。
- **primary game module 保持 `StellarEra`**，`Source/StellarEra/StellarEra.cpp` 的 `IMPLEMENT_PRIMARY_GAME_MODULE` 不动。
- **`[CoreRedirects]` 是永久兼容层**，只加不删。
- **不要修改** `Plugins/VoxelFree`、`Plugins/VibeUE`。
- 任何一步如果构建报循环依赖，先停下核对本任务和上一任务的 `Build.cs`，不要靠加 Public 依赖硬压。

## 模块依赖时间线

每个任务的 `Build.cs` 改动都能在这张表里查到"此刻应该是什么"。

| 任务完成后 | 模块 → 依赖（Public / Private） |
|---|---|
| T2 | `StellarEraCosmos`：Pub Core, CoreUObject, Engine, StellarEraCore；Priv StellarEraPlanet, StellarEraVehicle（临时） |
| T4 | `StellarEraUI`：Pub Core, CoreUObject, Engine；Priv StellarEraCore, StellarEraCosmos, StellarEraPlanet, StellarEraVehicle, Voxel（临时） |
| T5 | `StellarEraPlanetEdit`：Pub Core, CoreUObject, Engine；Priv StellarEraPlanet, Voxel |
| T6 | `StellarEraCharacter`：Pub Core, CoreUObject, Engine；Priv StellarEraPlanet, StellarEraPlanetEdit |
| T7 | `StellarEraSpaceship`：Pub Core, CoreUObject, Engine；Priv StellarEraPlanet, StellarEraCharacter；`StellarEraVehicle` 删除 |
| T9 | `StellarEraGeology`：Pub Core, CoreUObject, Voxel；Priv Json |
| T10 | `StellarEraAtmosphere`：Pub Core, CoreUObject, Engine；Priv StellarEraPlanet |
| T11 | `StellarEraMinerals`：Pub Core, CoreUObject, Engine, StellarEraGeology；Priv StellarEraPlanet, StellarEraAtmosphere, GeometryCore, GeometryFramework |
| T12 | `StellarEraTerrain`（原 Planet）：Pub Core, CoreUObject, Engine, Voxel, StellarEraGeology；Priv AssetRegistry |

---

### Task 1: 基线（提交 + 构建 + 测试）

**Files:** 无代码改动。

**Interfaces:**
- Consumes: 当前工作区状态
- Produces: 一个干净的基线 commit hash，后续每个任务的 diff 只含模块移动

- [ ] **Step 1: 查看工作区**

```bash
cd F:/workspace/ue/StellarEra && git status --porcelain | wc -l
```

- [ ] **Step 2: 若数量 > 0，把当前工作区提交为基线**

```bash
cd F:/workspace/ue/StellarEra && git add -A . && git commit -m "chore: 模块拆分前的基线

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- .
```

预期：`git status --porcelain | wc -l` 输出 `0`。若本来就是 0，跳过这步。

- [ ] **Step 3: 关掉编辑器，跑 BUILD**

预期 `Result: Succeeded`。若失败，先修到通过再继续 —— 基线必须绿。

- [ ] **Step 4: 跑 TEST**

预期：`Result: Passed` 若干，无 `Result: Failed`。

- [ ] **Step 5: 记录基线**

```bash
cd F:/workspace/ue/StellarEra && git rev-parse --short HEAD
```
把输出记进任务记录，后续每个任务的提交都要能 `git log --oneline <基线>..HEAD` 逐条对上。

---

### Task 2: 拆出 `StellarEraCosmos`

**Files:**
- Create: `Source/StellarEraCosmos/StellarEraCosmos.Build.cs`、`StellarEraCosmos.h`、`StellarEraCosmos.cpp`
- Move: `Source/StellarEra/Public/CosmosOrbitComponent.h`、`Public/CosmosSimulationSubsystem.h`、`Public/CosmosTrajectoryComponent.h`、`Private/CosmosOrbitComponent.cpp`、`Private/CosmosOrbitComponentTests.cpp`、`Private/CosmosSimulationSubsystem.cpp`、`Private/CosmosTrajectoryComponent.cpp`、`Private/CosmosTrajectoryComponentTests.cpp`
- Modify: `Source/StellarEra/StellarEra.Build.cs`、`StellarEra.uproject`、`Source/StellarEra.Target.cs`、`Source/StellarEraEditor.Target.cs`、`Config/DefaultEngine.ini`

**Interfaces:**
- Consumes: 基线（Task 1）
- Produces: 模块 `StellarEraCosmos`；API 宏 `STELLARERACOSMOS_API`；类 `UCosmosOrbitComponent`、`UCosmosSimulationSubsystem`、`UCosmosTrajectoryComponent` 从此在这个模块里

- [ ] **Step 1: 建目录与模块骨架**

```bash
cd F:/workspace/ue/StellarEra && mkdir -p Source/StellarEraCosmos/Public Source/StellarEraCosmos/Private
```

`Source/StellarEraCosmos/StellarEraCosmos.h`：

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
```

`Source/StellarEraCosmos/StellarEraCosmos.cpp`：

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "StellarEraCosmos.h"
#include "Modules/ModuleManager.h"

// 非 primary 模块。primary game module 全项目只能有一个，它留在 StellarEra.cpp。
IMPLEMENT_MODULE(FDefaultModuleImpl, StellarEraCosmos);
```

- [ ] **Step 2: 移动 8 个源文件**

```bash
cd F:/workspace/ue/StellarEra
git mv Source/StellarEra/Public/CosmosOrbitComponent.h        Source/StellarEraCosmos/Public/
git mv Source/StellarEra/Public/CosmosSimulationSubsystem.h  Source/StellarEraCosmos/Public/
git mv Source/StellarEra/Public/CosmosTrajectoryComponent.h  Source/StellarEraCosmos/Public/
git mv Source/StellarEra/Private/CosmosOrbitComponent.cpp        Source/StellarEraCosmos/Private/
git mv Source/StellarEra/Private/CosmosOrbitComponentTests.cpp   Source/StellarEraCosmos/Private/
git mv Source/StellarEra/Private/CosmosSimulationSubsystem.cpp   Source/StellarEraCosmos/Private/
git mv Source/StellarEra/Private/CosmosTrajectoryComponent.cpp   Source/StellarEraCosmos/Private/
git mv Source/StellarEra/Private/CosmosTrajectoryComponentTests.cpp Source/StellarEraCosmos/Private/
```

- [ ] **Step 3: 换 API 宏**

```bash
cd F:/workspace/ue/StellarEra && grep -rl "STELLARERA_API" Source/StellarEraCosmos | xargs sed -i 's/STELLARERA_API/STELLARERACOSMOS_API/g'
grep -rn "_API" Source/StellarEraCosmos/Public
```
预期：3 个头文件都是 `STELLARERACOSMOS_API`。

- [ ] **Step 4: 写 `StellarEraCosmos.Build.cs`**

```csharp
// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

// 万有引力层：N 体编排（组件 + 世界子系统）与轨迹可视化。
//
// Core 是 **Public**：CosmosSimulationSubsystem.h 按值持有 FNBodySimulator。
// Planet / Vehicle 是 **Private**：公开头只用前置声明接触它们。
// Vehicle 这两行是临时依赖，Task 6 / 7 会分别换成 Character / Spaceship。
public class StellarEraCosmos : ModuleRules
{
	public StellarEraCosmos(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "StellarEraCore",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"StellarEraPlanet",
			"StellarEraVehicle",
		});
	}
}
```

- [ ] **Step 5: 注册模块**

`StellarEra.uproject` 的 `Modules` 数组里，在 `StellarEra` 条目之后插入：

```json
		{
			"Name": "StellarEraCosmos",
			"Type": "Runtime",
			"LoadingPhase": "Default"
		},
```

`Source/StellarEra.Target.cs` 与 `Source/StellarEraEditor.Target.cs` 的 `ExtraModuleNames` 数组里各加一行：

```csharp
			"StellarEraCosmos",    // 万有引力
```

- [ ] **Step 6: 让主模块依赖它**

`Source/StellarEra/StellarEra.Build.cs` 的 `PrivateDependencyModuleNames` 里加：

```csharp
			"StellarEraCosmos",
```

- [ ] **Step 7: 加重定向**

`Config/DefaultEngine.ini` 的 `[CoreRedirects]` 段末尾追加 3 条：

```ini
+ClassRedirects=(OldName="/Script/StellarEra.CosmosOrbitComponent",NewName="/Script/StellarEraCosmos.CosmosOrbitComponent")
+ClassRedirects=(OldName="/Script/StellarEra.CosmosSimulationSubsystem",NewName="/Script/StellarEraCosmos.CosmosSimulationSubsystem")
+ClassRedirects=(OldName="/Script/StellarEra.CosmosTrajectoryComponent",NewName="/Script/StellarEraCosmos.CosmosTrajectoryComponent")
```

同时把该段落里「未列出的类留在原名模块 StellarEra 里，路径没变，因此不需要 redirect」那句改成只提 `StellarEraGameMode` / `StellarEraPlayerController` —— Cosmos 三个类已经搬走，不再属于这类。

- [ ] **Step 8: BUILD**

预期 `Result: Succeeded`。

- [ ] **Step 9: TEST**

预期 `Result: Passed`，无 `Result: Failed`。

- [ ] **Step 10: 提交**

```bash
cd F:/workspace/ue/StellarEra && git add Source/StellarEraCosmos Source/StellarEra StellarEra.uproject Source/StellarEra.Target.cs Source/StellarEraEditor.Target.cs Config/DefaultEngine.ini && git commit -m "refactor(模块): 拆出 StellarEraCosmos（万有引力）

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- Source/StellarEraCosmos Source/StellarEra StellarEra.uproject Source/StellarEra.Target.cs Source/StellarEraEditor.Target.cs Config/DefaultEngine.ini
```

---

### Task 3: 引入 `IStarMapUIInputs`（仍在 `StellarEra` 模块内）

拆 UI 之前先把「UI 反查控制器」这条边切断，否则 UI 依赖 Game 会成环。这一步不移动任何文件。

**Files:**
- Create: `Source/StellarEra/Public/StarMapUIInputs.h`
- Modify: `Source/StellarEra/Public/StellarEraPlayerController.h`、`Source/StellarEra/Private/StellarEraPlayerController.cpp`、`Source/StellarEra/Public/StarMapUISystem.h`、`Source/StellarEra/Private/StarMapUISystem.cpp`、`Source/StellarEra/Private/StellarEraHUD.cpp`

**Interfaces:**
- Consumes: `UStarMapUISystem::Draw(AStellarEraHUD&)`
- Produces: `UINTERFACE UStarMapUIInputs` / `IStarMapUIInputs`，四个纯虚函数：
  `bool GetStarMapMousePositionPx(FVector2D&) const`、`AActor* PickBodyUnderScreenPosition(const FVector2D&) const`、`bool ProjectBodyToScreen(AActor*, FVector2D&, float&) const`、`UPlanetMineralViewComponent* GetActiveMineralView() const`；
  `UStarMapUISystem::Draw(AStellarEraHUD& Hud, IStarMapUIInputs& Inputs)`；
  `AStellarEraPlayerController` 实现该接口

- [ ] **Step 1: 新建接口头**

`Source/StellarEra/Public/StarMapUIInputs.h`：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.
// 星图 UI 每帧要问玩家控制器的四件事。

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "StarMapUIInputs.generated.h"

class AActor;
class UPlanetMineralViewComponent;

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UStarMapUIInputs : public UInterface
{
	GENERATED_BODY()
};

/**
 * 星图 UI 的输入来源。
 *
 * 声明在 UI 侧、由 Game 模块的 AStellarEraPlayerController 实现：依赖方向是
 * UI ← Game。UI 因此不需要 include 控制器（那会让 UI → Game → UI 成环）。
 *
 * 只放 UI 真正用到的查询。控制器自己的输入处理、上下船、模式切换都不在这里。
 */
class STELLARERA_API IStarMapUIInputs
{
	GENERATED_BODY()

public:
	/** 鼠标的视口像素坐标；光标不可用（未捕获 / 不可见）时返回 false。 */
	virtual bool GetStarMapMousePositionPx(FVector2D& OutPosition) const = 0;

	/** 任意屏幕点下的已注册天体。悬停与点击共用。 */
	virtual AActor* PickBodyUnderScreenPosition(const FVector2D& ScreenPosition) const = 0;

	/** 把一颗天体投到屏幕；成功时 OutCenter 为中心、OutRadiusPx 为轮廓半径（像素）。 */
	virtual bool ProjectBodyToScreen(AActor* Body, FVector2D& OutCenter, float& OutRadiusPx) const = 0;

	/** 当前激活的矿物透视组件；未激活时为空。 */
	virtual UPlanetMineralViewComponent* GetActiveMineralView() const = 0;
};
```

- [ ] **Step 2: 控制器实现接口**

`Source/StellarEra/Public/StellarEraPlayerController.h`：

1. 在 `#include "StellarEraPlayerController.generated.h"` 之前加 `#include "StarMapUIInputs.h"`。
2. 类声明改成：

```cpp
class AStellarEraPlayerController : public APlayerController, public IStarMapUIInputs
```

3. 三个已有方法加 `virtual` + `override`：

```cpp
	virtual bool ProjectBodyToScreen(AActor* Body, FVector2D& OutCenter, float& OutRadiusPx) const override;
```
```cpp
	virtual AActor* PickBodyUnderScreenPosition(const FVector2D& ScreenPosition) const override;
```
```cpp
	virtual UPlanetMineralViewComponent* GetActiveMineralView() const override { return ActiveMineralView; }
```

4. 在 `GetActiveMineralView` 声明下面补一条：

```cpp
	virtual bool GetStarMapMousePositionPx(FVector2D& OutPosition) const override;
```

- [ ] **Step 3: 实现 `GetStarMapMousePositionPx`**

`Source/StellarEra/Private/StellarEraPlayerController.cpp` 里，`ProjectBodyToScreen` 的定义之后加：

```cpp
bool AStellarEraPlayerController::GetStarMapMousePositionPx(FVector2D& OutPosition) const
{
	double MouseX = 0.0;
	double MouseY = 0.0;
	if (!GetMousePosition(MouseX, MouseY))
	{
		return false;
	}
	OutPosition = FVector2D(MouseX, MouseY);
	return true;
}
```

- [ ] **Step 4: 改 `UStarMapUISystem::Draw` 的签名**

`Source/StellarEra/Public/StarMapUISystem.h`：在文件顶部的 `class AStellarEraHUD;` 旁加 `class IStarMapUIInputs;`，并把

```cpp
	/** 画一帧星图 UI。只在星图模式下由 HUD 调用。 */
	void Draw(AStellarEraHUD& Hud);
```
改成
```cpp
	/** 画一帧星图 UI。只在星图模式下由 HUD 调用。 */
	void Draw(AStellarEraHUD& Hud, IStarMapUIInputs& Inputs);
```

- [ ] **Step 5: 改 `StarMapUISystem.cpp` 的取数方式**

`Source/StellarEra/Private/StarMapUISystem.cpp`：

1. 把 `#include "StellarEraPlayerController.h"` 换成 `#include "StarMapUIInputs.h"`。
2. 函数签名改成 `void UStarMapUISystem::Draw(AStellarEraHUD& Hud, IStarMapUIInputs& Inputs)`。
3. 删掉这段：

```cpp
	AStellarEraPlayerController* PlayerController =
		Cast<AStellarEraPlayerController>(Hud.GetOwningPlayerController());
	if (!PlayerController)
	{
		return;
	}
```

4. 把鼠标那段

```cpp
	double MouseX = 0.0;
	double MouseY = 0.0;
	const bool bHasMouse = PlayerController->GetMousePosition(MouseX, MouseY);
	const FVector2D MousePosition(MouseX, MouseY);
```
换成
```cpp
	FVector2D MousePosition = FVector2D::ZeroVector;
	const bool bHasMouse = Inputs.GetStarMapMousePositionPx(MousePosition);
```

5. 另外三处调用改成接口：
   - `PlayerController->PickBodyUnderScreenPosition(MousePosition)` → `Inputs.PickBodyUnderScreenPosition(MousePosition)`
   - `PlayerController->ProjectBodyToScreen(Selected, Center, RadiusPx)` → `Inputs.ProjectBodyToScreen(Selected, Center, RadiusPx)`
   - `PlayerController->GetActiveMineralView()` → `Inputs.GetActiveMineralView()`

6. 确认文件里再没有 `PlayerController` 标识符：

```bash
cd F:/workspace/ue/StellarEra && grep -n "PlayerController" Source/StellarEra/Private/StarMapUISystem.cpp
```
预期：无输出。

- [ ] **Step 6: 改 HUD 的调用点**

`Source/StellarEra/Private/StellarEraHUD.cpp`：加 `#include "StarMapUIInputs.h"`，并把

```cpp
			StarMapUI->Draw(*this);
```
改成
```cpp
			if (IStarMapUIInputs* Inputs = Cast<IStarMapUIInputs>(GetOwningPlayerController()))
			{
				StarMapUI->Draw(*this, *Inputs);
			}
```

- [ ] **Step 7: BUILD** — 预期 `Result: Succeeded`

- [ ] **Step 8: TEST** — 预期 `Result: Passed`，无 `Result: Failed`

- [ ] **Step 9: 提交**

```bash
cd F:/workspace/ue/StellarEra && git add Source/StellarEra && git commit -m "refactor(UI): 星图 UI 改用 IStarMapUIInputs 取数，切断对控制器的依赖

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- Source/StellarEra
```

---

### Task 4: 拆出 `StellarEraUI`

**Files:**
- Create: `Source/StellarEraUI/StellarEraUI.Build.cs`、`StellarEraUI.h`、`StellarEraUI.cpp`
- Move: `Source/StellarEra/Public/` 的 `StarMapCameraPawn.h`、`StarMapUISystem.h`、`StarMapUIInputs.h`、`StellarEraHUD.h`、`PlanetEditUI.h`；`Source/StellarEra/Private/` 的 `StarMapCameraPawn.cpp`、`StarMapCameraInvokerTests.cpp`、`StarMapUISystem.cpp`、`StarMapUISystemTests.cpp`、`StellarEraHUD.cpp`、`StellarEraHUDTests.cpp`、`MineralLegendTests.cpp`、`PlanetEditUI.cpp`
- Keep: `StarMapTests.cpp` **留在** `StellarEra`（它 include `StellarEraPlayerController.h`）
- Modify: `Source/StellarEra/StellarEra.Build.cs`、`StellarEra.uproject`、两个 `Target.cs`、`Config/DefaultEngine.ini`、`PlanetEditUI.h`

**Interfaces:**
- Consumes: Task 3 的 `IStarMapUIInputs`
- Produces: 模块 `StellarEraUI`；API 宏 `STELLARERAUI_API`；类 `AStarMapCameraPawn`、`UStarMapUISystem`、`AStellarEraHUD`、`IStarMapUIInputs`；命名空间 `PlanetEditUI`（两个函数带 `STELLARERAUI_API`）

- [ ] **Step 1: 建目录与骨架**

```bash
cd F:/workspace/ue/StellarEra && mkdir -p Source/StellarEraUI/Public Source/StellarEraUI/Private
```

`Source/StellarEraUI/StellarEraUI.h`：

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
```

`Source/StellarEraUI/StellarEraUI.cpp`：

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "StellarEraUI.h"
#include "Modules/ModuleManager.h"

// 非 primary 模块。primary game module 全项目只能有一个，它留在 StellarEra.cpp。
IMPLEMENT_MODULE(FDefaultModuleImpl, StellarEraUI);
```

- [ ] **Step 2: 移动 13 个文件**

```bash
cd F:/workspace/ue/StellarEra
git mv Source/StellarEra/Public/StarMapCameraPawn.h  Source/StellarEraUI/Public/
git mv Source/StellarEra/Public/StarMapUISystem.h    Source/StellarEraUI/Public/
git mv Source/StellarEra/Public/StarMapUIInputs.h    Source/StellarEraUI/Public/
git mv Source/StellarEra/Public/StellarEraHUD.h      Source/StellarEraUI/Public/
git mv Source/StellarEra/Public/PlanetEditUI.h       Source/StellarEraUI/Public/
git mv Source/StellarEra/Private/StarMapCameraPawn.cpp        Source/StellarEraUI/Private/
git mv Source/StellarEra/Private/StarMapCameraInvokerTests.cpp Source/StellarEraUI/Private/
git mv Source/StellarEra/Private/StarMapUISystem.cpp          Source/StellarEraUI/Private/
git mv Source/StellarEra/Private/StarMapUISystemTests.cpp     Source/StellarEraUI/Private/
git mv Source/StellarEra/Private/StellarEraHUD.cpp            Source/StellarEraUI/Private/
git mv Source/StellarEra/Private/StellarEraHUDTests.cpp       Source/StellarEraUI/Private/
git mv Source/StellarEra/Private/MineralLegendTests.cpp       Source/StellarEraUI/Private/
git mv Source/StellarEra/Private/PlanetEditUI.cpp             Source/StellarEraUI/Private/
```

- [ ] **Step 3: 加 API 宏**

这四个类型原来在主模块、没有宏。逐个改类声明：

`Source/StellarEraUI/Public/StarMapCameraPawn.h` 的类声明行：
```cpp
class STELLARERAUI_API AStarMapCameraPawn : public APawn
```

`Source/StellarEraUI/Public/StarMapUISystem.h` 的类声明行：
```cpp
class STELLARERAUI_API UStarMapUISystem : public UObject
```

`Source/StellarEraUI/Public/StellarEraHUD.h` 的类声明行：
```cpp
class STELLARERAUI_API AStellarEraHUD : public AHUD
```

`Source/StellarEraUI/Public/StarMapUIInputs.h` 里的接口类声明改成 `class STELLARERAUI_API IStarMapUIInputs`。

`Source/StellarEraUI/Public/PlanetEditUI.h` 的两个函数各加宏前缀：

```cpp
	STELLARERAUI_API void GetBrushButtonRects(const FVector2D& ViewportSize,
		FVector2D& OutSphereTopLeft, FVector2D& OutCubeTopLeft);
```
```cpp
	STELLARERAUI_API bool HitTestBrushButton(const FVector2D& ViewportSize, const FVector2D& ScreenPosition,
		int32& OutButtonIndex);
```

同一文件里那条「本头是 StellarEra 的公开头，而该枚举属于私有依赖 StellarEraVehicle」的注释改成：

```cpp
	 * 返回 int32 而不是 EPlanetBrushShape：本头是 StellarEraUI 的公开头，
	 * 而该枚举属于 StellarEraPlanetEdit。调用方在 .cpp 里映射。
```

> `StellarEraPlanetEdit` 此时还不存在（Task 5 才建），这段只是注释，不影响构建。

- [ ] **Step 4: 写 `StellarEraUI.Build.cs`**

```csharp
// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

// 显示与控制层：HUD、星图相机、星图面板、矿物图例、编辑按钮布局。
//
// 这是唯一允许同时依赖多个功能模块的模块。反向依赖一律禁止：
// 功能模块不得 include 本模块的头。
//
// 本模块**不能**依赖 StellarEra（primary game module）—— Game 依赖 UI，
// UI 再依赖 Game 就成环。需要控制器数据的地方走 IStarMapUIInputs。
//
// StellarEraPlanet / StellarEraVehicle 现在是临时依赖，Task 6 / 7 / 12 会换成
// Terrain / Character / Spaceship / Minerals / Geology。
public class StellarEraUI : ModuleRules
{
	public StellarEraUI(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"StellarEraCore",    // CosmosTypes.h（StarMapUISystemTests.cpp）
			"StellarEraCosmos",  // 选中天体、轨迹、星图状态
			"StellarEraPlanet",  // 行星 Actor、地质场、矿物观察（临时）
			"StellarEraVehicle", // 角色、飞船、编辑组件（临时）
			"Voxel",             // StarMapCameraInvokerTests.cpp 的体素调用器
		});
	}
}
```

- [ ] **Step 5: 注册模块并让主模块依赖它**

- `StellarEra.uproject` 的 `Modules` 里在 `StellarEraCosmos` 之后插入 `{"Name": "StellarEraUI", "Type": "Runtime", "LoadingPhase": "Default"}`。
- 两个 `Target.cs` 的 `ExtraModuleNames` 各加 `"StellarEraUI",    // 显示与控制`。
- `Source/StellarEra/StellarEra.Build.cs` 的 `PrivateDependencyModuleNames` 加 `"StellarEraUI",`。

- [ ] **Step 6: 加重定向**

```ini
+ClassRedirects=(OldName="/Script/StellarEra.StarMapCameraPawn",NewName="/Script/StellarEraUI.StarMapCameraPawn")
+ClassRedirects=(OldName="/Script/StellarEra.StarMapUISystem",NewName="/Script/StellarEraUI.StarMapUISystem")
+ClassRedirects=(OldName="/Script/StellarEra.StellarEraHUD",NewName="/Script/StellarEraUI.StellarEraHUD")
```

- [ ] **Step 7: BUILD** — 预期 `Result: Succeeded`

若报 `未定义标识符 AStellarEraPlayerController`，说明还有文件在 UI 里 include 控制器 —— 用 `grep -rn "StellarEraPlayerController" Source/StellarEraUI` 找出来，按 Task 3 的做法走接口，不要加 UI → Game 的依赖。

- [ ] **Step 8: TEST** — 预期 `Result: Passed`，无 `Result: Failed`

- [ ] **Step 9: 提交**

```bash
cd F:/workspace/ue/StellarEra && git add Source/StellarEraUI Source/StellarEra StellarEra.uproject Source/StellarEra.Target.cs Source/StellarEraEditor.Target.cs Config/DefaultEngine.ini && git commit -m "refactor(模块): 拆出 StellarEraUI（显示与控制）

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- Source/StellarEraUI Source/StellarEra StellarEra.uproject Source/StellarEra.Target.cs Source/StellarEraEditor.Target.cs Config/DefaultEngine.ini
```

---

### Task 5: 拆出 `StellarEraPlanetEdit`

**Files:**
- Create: `Source/StellarEraPlanetEdit/StellarEraPlanetEdit.Build.cs`、`.h`、`.cpp`
- Move: `Source/StellarEraVehicle/Public/PlanetEditComponent.h`、`PlanetEditBrush.h`、`PlanetBrushApplicator.h`；`Source/StellarEraVehicle/Private/PlanetEditComponent.cpp`、`PlanetEditBrush.cpp`、`PlanetBrushApplicator.cpp`
- Modify: `Source/StellarEraVehicle/StellarEraVehicle.Build.cs`、`Source/StellarEra/StellarEra.Build.cs`、`StellarEra.uproject`、两个 `Target.cs`、`Config/DefaultEngine.ini`

**Interfaces:**
- Consumes: `AVoxelPlanetActor`（`StellarEraPlanet`）、Voxel 插件
- Produces: 模块 `StellarEraPlanetEdit`；API 宏 `STELLARERAPLANETEDIT_API`；类 `UPlanetEditComponent`；命名空间 `PlanetEditBrush`、`PlanetBrushApplicator`；枚举 `EPlanetBrushShape`

- [ ] **Step 1: 建目录与骨架**

`Source/StellarEraPlanetEdit/StellarEraPlanetEdit.h`：

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
```

`Source/StellarEraPlanetEdit/StellarEraPlanetEdit.cpp`：

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "StellarEraPlanetEdit.h"
#include "Modules/ModuleManager.h"

// 非 primary 模块。primary game module 全项目只能有一个，它留在 StellarEra.cpp。
IMPLEMENT_MODULE(FDefaultModuleImpl, StellarEraPlanetEdit);
```

```bash
cd F:/workspace/ue/StellarEra && mkdir -p Source/StellarEraPlanetEdit/Public Source/StellarEraPlanetEdit/Private
```

- [ ] **Step 2: 移动 6 个文件**

```bash
cd F:/workspace/ue/StellarEra
git mv Source/StellarEraVehicle/Public/PlanetEditComponent.h   Source/StellarEraPlanetEdit/Public/
git mv Source/StellarEraVehicle/Public/PlanetEditBrush.h       Source/StellarEraPlanetEdit/Public/
git mv Source/StellarEraVehicle/Public/PlanetBrushApplicator.h Source/StellarEraPlanetEdit/Public/
git mv Source/StellarEraVehicle/Private/PlanetEditComponent.cpp   Source/StellarEraPlanetEdit/Private/
git mv Source/StellarEraVehicle/Private/PlanetEditBrush.cpp       Source/StellarEraPlanetEdit/Private/
git mv Source/StellarEraVehicle/Private/PlanetBrushApplicator.cpp Source/StellarEraPlanetEdit/Private/
```

- [ ] **Step 3: 换宏**

```bash
cd F:/workspace/ue/StellarEra && grep -rl "STELLARERAVEHICLE_API" Source/StellarEraPlanetEdit | xargs sed -i 's/STELLARERAVEHICLE_API/STELLARERAPLANETEDIT_API/g'
```

- [ ] **Step 4: 写 `StellarEraPlanetEdit.Build.cs`**

```csharp
// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

// 行星编辑层：编辑组件、笔刷几何、笔刷 → 体素工具的适配。
//
// Planet 与 Voxel 是 **Private**：公开头只用前置声明接触行星类型，
// 完整定义（VoxelPlanetActor.h、VoxelIntBox.h、VoxelTools/*）只在 .cpp 里 include。
public class StellarEraPlanetEdit : ModuleRules
{
	public StellarEraPlanetEdit(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"StellarEraPlanet",
			"Voxel",
		});
	}
}
```

- [ ] **Step 5: 注册模块 + 更新依赖**

- `StellarEra.uproject` 插入 `StellarEraPlanetEdit` 模块条目。
- 两个 `Target.cs` 各加 `"StellarEraPlanetEdit",  // 行星编辑`。
- `Source/StellarEraVehicle/StellarEraVehicle.Build.cs` 的 `PrivateDependencyModuleNames` 加 `"StellarEraPlanetEdit",`。
- `Source/StellarEra/StellarEra.Build.cs` 的 `PrivateDependencyModuleNames` 加 `"StellarEraPlanetEdit",`。

- [ ] **Step 6: 加重定向**

```ini
+ClassRedirects=(OldName="/Script/StellarEraVehicle.PlanetEditComponent",NewName="/Script/StellarEraPlanetEdit.PlanetEditComponent")
+EnumRedirects=(OldName="/Script/StellarEraVehicle.EPlanetBrushShape",NewName="/Script/StellarEraPlanetEdit.EPlanetBrushShape")
```

- [ ] **Step 7: BUILD** — 预期 `Result: Succeeded`

- [ ] **Step 8: TEST** — 预期 `Result: Passed`，无 `Result: Failed`

- [ ] **Step 9: 提交**

```bash
cd F:/workspace/ue/StellarEra && git add Source/StellarEraPlanetEdit Source/StellarEraVehicle Source/StellarEra StellarEra.uproject Source/StellarEra.Target.cs Source/StellarEraEditor.Target.cs Config/DefaultEngine.ini && git commit -m "refactor(模块): 拆出 StellarEraPlanetEdit（行星编辑）

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- Source/StellarEraPlanetEdit Source/StellarEraVehicle Source/StellarEra StellarEra.uproject Source/StellarEra.Target.cs Source/StellarEraEditor.Target.cs Config/DefaultEngine.ini
```

---

### Task 6: 拆出 `StellarEraCharacter`（同时破 Character ↔ Spaceship 环）

`APlanetCharacter::FindNearestShip()` 让角色模块必须 include 飞船模块，而飞船模块又持有 `APlanetCharacter*`。先把这个方法搬到飞船侧，两个模块才能并存。

**Files:**
- Create: `Source/StellarEraCharacter/StellarEraCharacter.Build.cs`、`.h`、`.cpp`
- Move: `Source/StellarEraVehicle/Public/PlanetCharacter.h`、`PlanetCharacterMovementComponent.h`、`PlanetCharacterAnimInstance.h`；`Source/StellarEraVehicle/Private/PlanetCharacter.cpp`、`PlanetCharacterMovementComponent.cpp`、`PlanetCharacterAnimInstance.cpp`、`PlanetCharacterAnimTests.cpp`
- Modify: `Source/StellarEraVehicle/Public/PlanetCharacter.h`、`Source/StellarEraVehicle/Private/PlanetCharacter.cpp`（移方法）、`Source/StellarEraVehicle/Public/SpaceshipPawn.h`、`Source/StellarEraVehicle/Private/SpaceshipPawn.cpp`、`Source/StellarEra/Private/StellarEraPlayerController.cpp`、各 `Build.cs`、`uproject`、`Target.cs`、`DefaultEngine.ini`

**Interfaces:**
- Consumes: `AVoxelPlanetActor`、`UAlwaysLocalVoxelInvokerComponent`（`StellarEraPlanet`）、`UPlanetEditComponent`（`StellarEraPlanetEdit`）
- Produces: 模块 `StellarEraCharacter`；API 宏 `STELLARERACHARACTER_API`；类 `APlanetCharacter`、`UPlanetCharacterMovementComponent`、`UPlanetCharacterAnimInstance`；`ASpaceshipPawn::FindNearestTo(const UWorld*, const FVector&)` 静态函数

- [ ] **Step 1: 把 `FindNearestShip` 搬到飞船侧**

`Source/StellarEraVehicle/Public/PlanetCharacter.h`：删掉

```cpp
	ASpaceshipPawn* FindNearestShip() const;
```

同文件顶部如果只剩这一处用到 `ASpaceshipPawn`，把 `class ASpaceshipPawn;` 前置声明也删掉：

```bash
cd F:/workspace/ue/StellarEra && grep -n ASpaceshipPawn Source/StellarEraVehicle/Public/PlanetCharacter.h
```

`Source/StellarEraVehicle/Private/PlanetCharacter.cpp`：删掉整个 `APlanetCharacter::FindNearestShip` 定义（当前在 629 行附近），并删掉 `#include "SpaceshipPawn.h"`。

`Source/StellarEraVehicle/Public/SpaceshipPawn.h`：在 `CanBoardFrom` 声明旁边加

```cpp
	/**
	 * 找离 FromLocation 最近的飞船；找不到返回 nullptr。
	 *
	 * 放在飞船侧而不是角色侧：角色模块若不 include 飞船模块就与飞船模块成环
	 * （飞船持有 APlanetCharacter*）。调用方是 AStellarEraPlayerController。
	 */
	static ASpaceshipPawn* FindNearestTo(const UWorld* World, const FVector& FromLocation);
```

`Source/StellarEraVehicle/Private/SpaceshipPawn.cpp`：加实现（该文件第 12 行已 `#include "EngineUtils.h"`，`TActorIterator` 可直接用）：

```cpp
ASpaceshipPawn* ASpaceshipPawn::FindNearestTo(const UWorld* World, const FVector& FromLocation)
{
	if (!World)
	{
		return nullptr;
	}

	ASpaceshipPawn* Nearest = nullptr;
	float NearestDistSq = TNumericLimits<float>::Max();

	for (TActorIterator<ASpaceshipPawn> It(World); It; ++It)
	{
		const float DistSq = FVector::DistSquared(It->GetActorLocation(), FromLocation);
		if (DistSq < NearestDistSq)
		{
			NearestDistSq = DistSq;
			Nearest = *It;
		}
	}
	return Nearest;
}
```

- [ ] **Step 2: 改 4 个调用点**

`Source/StellarEra/Private/StellarEraPlayerController.cpp`：

三处完全相同的文本

```cpp
		ASpaceshipPawn* Ship = PawnCharacter->FindNearestShip();
```
改成
```cpp
		ASpaceshipPawn* Ship = ASpaceshipPawn::FindNearestTo(GetWorld(), PawnCharacter->GetActorLocation());
```

`ResolveStarMapFocusShip` 里

```cpp
		return Character->FindNearestShip();
```
改成
```cpp
		return ASpaceshipPawn::FindNearestTo(Character->GetWorld(), Character->GetActorLocation());
```

核对无残留：

```bash
cd F:/workspace/ue/StellarEra && grep -rn "FindNearestShip" Source
```
预期：无输出。

- [ ] **Step 3: 建目录与骨架、移动 7 个文件**

`Source/StellarEraCharacter/StellarEraCharacter.h`：

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
```

`Source/StellarEraCharacter/StellarEraCharacter.cpp`：

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "StellarEraCharacter.h"
#include "Modules/ModuleManager.h"

// 非 primary 模块。primary game module 全项目只能有一个，它留在 StellarEra.cpp。
IMPLEMENT_MODULE(FDefaultModuleImpl, StellarEraCharacter);
```

```bash
cd F:/workspace/ue/StellarEra && mkdir -p Source/StellarEraCharacter/Public Source/StellarEraCharacter/Private
git mv Source/StellarEraVehicle/Public/PlanetCharacter.h                  Source/StellarEraCharacter/Public/
git mv Source/StellarEraVehicle/Public/PlanetCharacterMovementComponent.h Source/StellarEraCharacter/Public/
git mv Source/StellarEraVehicle/Public/PlanetCharacterAnimInstance.h      Source/StellarEraCharacter/Public/
git mv Source/StellarEraVehicle/Private/PlanetCharacter.cpp                Source/StellarEraCharacter/Private/
git mv Source/StellarEraVehicle/Private/PlanetCharacterMovementComponent.cpp Source/StellarEraCharacter/Private/
git mv Source/StellarEraVehicle/Private/PlanetCharacterAnimInstance.cpp    Source/StellarEraCharacter/Private/
git mv Source/StellarEraVehicle/Private/PlanetCharacterAnimTests.cpp       Source/StellarEraCharacter/Private/
```

- [ ] **Step 4: 换宏**

```bash
cd F:/workspace/ue/StellarEra && grep -rl "STELLARERAVEHICLE_API" Source/StellarEraCharacter | xargs sed -i 's/STELLARERAVEHICLE_API/STELLARERACHARACTER_API/g'
```

- [ ] **Step 5: 写 `StellarEraCharacter.Build.cs`**

```csharp
// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

// 角色控制层：行星角色、球面移动组件、动画实例。
//
// Planet 与 PlanetEdit 是 **Private**：公开头只前置声明 AVoxelPlanetActor /
// UPlanetEditComponent，完整定义只在 .cpp 里 include。
// 不需要显式依赖 Voxel —— Planet 把 Voxel 声明成 Public，路径会传下来。
public class StellarEraCharacter : ModuleRules
{
	public StellarEraCharacter(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"StellarEraPlanet",
			"StellarEraPlanetEdit",
		});
	}
}
```

- [ ] **Step 6: 注册模块 + 更新依赖**

- `StellarEra.uproject` 插入 `StellarEraCharacter` 模块条目。
- 两个 `Target.cs` 各加 `"StellarEraCharacter", // 角色控制`。
- `Source/StellarEraVehicle/StellarEraVehicle.Build.cs` 的 `PrivateDependencyModuleNames` 加 `"StellarEraCharacter",`。
- `Source/StellarEra/StellarEra.Build.cs` 的 `PrivateDependencyModuleNames` 加 `"StellarEraCharacter",`。
- `Source/StellarEraUI/StellarEraUI.Build.cs` 的 `PrivateDependencyModuleNames` 加 `"StellarEraCharacter",`（`StellarEraVehicle` 那一行先留着，Task 7 再删）。

- [ ] **Step 7: 加重定向**

```ini
+ClassRedirects=(OldName="/Script/StellarEra.PlanetCharacter",NewName="/Script/StellarEraCharacter.PlanetCharacter")
+ClassRedirects=(OldName="/Script/StellarEraVehicle.PlanetCharacter",NewName="/Script/StellarEraCharacter.PlanetCharacter")
+ClassRedirects=(OldName="/Script/StellarEraVehicle.PlanetCharacterAnimInstance",NewName="/Script/StellarEraCharacter.PlanetCharacterAnimInstance")
+ClassRedirects=(OldName="/Script/StellarEraVehicle.PlanetCharacterMovementComponent",NewName="/Script/StellarEraCharacter.PlanetCharacterMovementComponent")
```

第一条是改现有条目的 `NewName`（原本指向 `StellarEraVehicle`），第二条是新加的 —— 关卡与 BP 存的是 `StellarEraVehicle` 路径。两条都要指到最终模块，不能指望链接式解析。

- [ ] **Step 8: BUILD** — 预期 `Result: Succeeded`

- [ ] **Step 9: TEST** — 预期 `Result: Passed`，无 `Result: Failed`

- [ ] **Step 10: 提交**

```bash
cd F:/workspace/ue/StellarEra && git add Source/StellarEraCharacter Source/StellarEraVehicle Source/StellarEra Source/StellarEraUI StellarEra.uproject Source/StellarEra.Target.cs Source/StellarEraEditor.Target.cs Config/DefaultEngine.ini && git commit -m "refactor(模块): 拆出 StellarEraCharacter，FindNearestShip 迁到飞船侧破环

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- Source/StellarEraCharacter Source/StellarEraVehicle Source/StellarEra Source/StellarEraUI StellarEra.uproject Source/StellarEra.Target.cs Source/StellarEraEditor.Target.cs Config/DefaultEngine.ini
```

---

### Task 7: 拆出 `StellarEraSpaceship` 并删除 `StellarEraVehicle`

**Files:**
- Create: `Source/StellarEraSpaceship/StellarEraSpaceship.Build.cs`、`.h`、`.cpp`
- Move: `Source/StellarEraVehicle/Public/SpaceshipPawn.h`；`Source/StellarEraVehicle/Private/SpaceshipPawn.cpp`、`SpaceshipAtmosphereDragTests.cpp`、`SpaceshipCollisionTests.cpp`、`SpaceshipFreeLookTests.cpp`、`SpaceshipLODInvokerTests.cpp`、`SpaceshipSpeedLimitTests.cpp`、`SpaceshipThrustDirectionTests.cpp`
- Delete: `Source/StellarEraVehicle/StellarEraVehicle.h`、`.cpp`、`.Build.cs`（目录清空后整体删除）
- Modify: 各 `Build.cs`、`uproject`、`Target.cs`、`DefaultEngine.ini`

**Interfaces:**
- Consumes: `StellarEraCharacter`、`StellarEraPlanet`、`UPlanetAtmosphereComponent`（此刻还在 `StellarEraPlanet` 里）
- Produces: 模块 `StellarEraSpaceship`；API 宏 `STELLARERASPACESHIP_API`；类 `ASpaceshipPawn`

- [ ] **Step 1: 建目录与骨架、移动 8 个文件**

`Source/StellarEraSpaceship/StellarEraSpaceship.h`：

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
```

`Source/StellarEraSpaceship/StellarEraSpaceship.cpp`：

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "StellarEraSpaceship.h"
#include "Modules/ModuleManager.h"

// 非 primary 模块。primary game module 全项目只能有一个，它留在 StellarEra.cpp。
IMPLEMENT_MODULE(FDefaultModuleImpl, StellarEraSpaceship);
```

```bash
cd F:/workspace/ue/StellarEra && mkdir -p Source/StellarEraSpaceship/Public Source/StellarEraSpaceship/Private
git mv Source/StellarEraVehicle/Public/SpaceshipPawn.h Source/StellarEraSpaceship/Public/
git mv Source/StellarEraVehicle/Private/SpaceshipPawn.cpp                   Source/StellarEraSpaceship/Private/
git mv Source/StellarEraVehicle/Private/SpaceshipAtmosphereDragTests.cpp    Source/StellarEraSpaceship/Private/
git mv Source/StellarEraVehicle/Private/SpaceshipCollisionTests.cpp         Source/StellarEraSpaceship/Private/
git mv Source/StellarEraVehicle/Private/SpaceshipFreeLookTests.cpp          Source/StellarEraSpaceship/Private/
git mv Source/StellarEraVehicle/Private/SpaceshipLODInvokerTests.cpp        Source/StellarEraSpaceship/Private/
git mv Source/StellarEraVehicle/Private/SpaceshipSpeedLimitTests.cpp        Source/StellarEraSpaceship/Private/
git mv Source/StellarEraVehicle/Private/SpaceshipThrustDirectionTests.cpp   Source/StellarEraSpaceship/Private/
```

- [ ] **Step 2: 换宏**

```bash
cd F:/workspace/ue/StellarEra && grep -rl "STELLARERAVEHICLE_API" Source/StellarEraSpaceship | xargs sed -i 's/STELLARERAVEHICLE_API/STELLARERASPACESHIP_API/g'
```

- [ ] **Step 3: 写 `StellarEraSpaceship.Build.cs`**

```csharp
// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

// 飞船层：街机飞船 Pawn。位移由 N 体求解器推进，本模块只给推力与阻尼。
//
// 飞船**不能**依赖 StellarEraCosmos（那会成环）：推力加速度由子系统每帧下来读走。
// 大气组件现在仍在 StellarEraPlanet 里，Task 10 会换成 StellarEraAtmosphere。
public class StellarEraSpaceship : ModuleRules
{
	public StellarEraSpaceship(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"StellarEraPlanet",
			"StellarEraCharacter",
		});
	}
}
```

- [ ] **Step 4: 删除 `StellarEraVehicle` 模块**

```bash
cd F:/workspace/ue/StellarEra && ls Source/StellarEraVehicle/Public Source/StellarEraVehicle/Private
```
预期：两个目录都为空。若有残留文件，先确认它属于哪个新模块并 `git mv` 过去。

```bash
cd F:/workspace/ue/StellarEra
git rm Source/StellarEraVehicle/StellarEraVehicle.h Source/StellarEraVehicle/StellarEraVehicle.cpp Source/StellarEraVehicle/StellarEraVehicle.Build.cs
rmdir Source/StellarEraVehicle/Public Source/StellarEraVehicle/Private Source/StellarEraVehicle
```

- [ ] **Step 5: 注册模块 + 清理依赖**

- `StellarEra.uproject`：删掉 `StellarEraVehicle` 条目，插入 `StellarEraSpaceship` 条目。
- 两个 `Target.cs`：删掉 `"StellarEraVehicle"` 行，加 `"StellarEraSpaceship", // 飞船`。
- `Source/StellarEraUI/StellarEraUI.Build.cs`：把 `"StellarEraVehicle",` 换成 `"StellarEraPlanetEdit", "StellarEraSpaceship",`。
- `Source/StellarEraCosmos/StellarEraCosmos.Build.cs`：把 `"StellarEraVehicle",` 换成 `"StellarEraSpaceship",`。
- `Source/StellarEra/StellarEra.Build.cs`：把 `"StellarEraVehicle",` 换成 `"StellarEraSpaceship",`。

核对全仓无残留：

```bash
cd F:/workspace/ue/StellarEra && grep -rn "StellarEraVehicle" Source StellarEra.uproject Config/DefaultEngine.ini | grep -v "OldName"
```
预期：无输出（`OldName` 的历史路径允许保留）。

- [ ] **Step 6: 加重定向**

改现有条目的 `NewName` 并新增一条：

```ini
+ClassRedirects=(OldName="/Script/StellarEra.SpaceshipPawn",NewName="/Script/StellarEraSpaceship.SpaceshipPawn")
+ClassRedirects=(OldName="/Script/StellarEraVehicle.SpaceshipPawn",NewName="/Script/StellarEraSpaceship.SpaceshipPawn")
```

- [ ] **Step 7: BUILD** — 预期 `Result: Succeeded`

若报循环依赖，说明 Task 6 的迁移没做干净：`grep -rn "SpaceshipPawn.h" Source/StellarEraCharacter` 应为空。

- [ ] **Step 8: TEST** — 预期 `Result: Passed`，无 `Result: Failed`

- [ ] **Step 9: 提交**

```bash
cd F:/workspace/ue/StellarEra && git add Source/StellarEraSpaceship Source/StellarEraVehicle Source/StellarEra Source/StellarEraUI Source/StellarEraCosmos StellarEra.uproject Source/StellarEra.Target.cs Source/StellarEraEditor.Target.cs Config/DefaultEngine.ini && git commit -m "refactor(模块): 拆出 StellarEraSpaceship，删除 StellarEraVehicle

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- Source/StellarEraSpaceship Source/StellarEraVehicle Source/StellarEra Source/StellarEraUI Source/StellarEraCosmos StellarEra.uproject Source/StellarEra.Target.cs Source/StellarEraEditor.Target.cs Config/DefaultEngine.ini
```

---

### Task 8: 大气与矿物观察组件从 `AVoxelPlanetActor` 挪到蓝图

必须先做这一步，Task 10 / 11 才能把两个组件类拆出独立模块 —— 否则 `VoxelPlanetActor.cpp` 会同时 include 两边，形成 Terrain ↔ Atmosphere/Minerals 环。

**Files:**
- Modify: `Source/StellarEraPlanet/Public/VoxelPlanetActor.h`、`Source/StellarEraPlanet/Private/VoxelPlanetActor.cpp`、`Source/StellarEraPlanet/Private/PlanetMineralViewComponent.cpp`、`Source/StellarEraSpaceship/Private/SpaceshipPawn.cpp`、`Source/StellarEra/Private/StellarEraPlayerController.cpp`
- Modify（编辑器内）：`UniverseMap` 里每颗行星的 BP

**Interfaces:**
- Consumes: `AVoxelPlanetActor::GetAtmosphereComponent()`、`GetMineralViewComponent()`
- Produces: 两个 getter 被删除；消费方改用 `AActor::FindComponentByClass<T>()`

- [ ] **Step 1: 从 Actor 头文件删成员与 getter**

`Source/StellarEraPlanet/Public/VoxelPlanetActor.h`：

删掉两行前置声明 `class UPlanetAtmosphereComponent;` 与 `class UPlanetMineralViewComponent;`，并删掉这两块：

```cpp
	/** 大气壳。构造时自动创建，每颗行星一个。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "StellarEra|Atmosphere")
	TObjectPtr<UPlanetAtmosphereComponent> AtmosphereComponent;

	UFUNCTION(BlueprintPure, Category = "StellarEra|Atmosphere")
	UPlanetAtmosphereComponent* GetAtmosphereComponent() const { return AtmosphereComponent; }
```

```cpp
	/** 矿物透视观察组件。构造时自动创建；未激活时不 Tick、不建任何资产。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Planet|MineralView")
	TObjectPtr<UPlanetMineralViewComponent> MineralViewComponent;

	UFUNCTION(BlueprintPure, Category = "Planet|MineralView")
	UPlanetMineralViewComponent* GetMineralViewComponent() const { return MineralViewComponent; }
```

- [ ] **Step 2: 从 Actor 构造函数删创建代码与 include**

`Source/StellarEraPlanet/Private/VoxelPlanetActor.cpp`：

删掉 `#include "PlanetAtmosphereComponent.h"`、`#include "PlanetMineralViewComponent.h"`，并删掉这一段（`EditGrid` 那段不动）：

```cpp
	// 大气壳。挂到根组件上，相对位置为零 —— 球心与行星球心重合。
	// 材质与缩放都在 BeginPlay 里按 GetSurfaceRadiusCm() 定，构造期拿不到最终半径。
	AtmosphereComponent = CreateDefaultSubobject<UPlanetAtmosphereComponent>(TEXT("PlanetAtmosphere"));
	AtmosphereComponent->SetupAttachment(RootComponent);

	// 矿物透视组件：默认不 Tick，Activate 时才采样并 spawn 观察体。
	MineralViewComponent = CreateDefaultSubobject<UPlanetMineralViewComponent>(TEXT("PlanetMineralView"));
	MineralViewComponent->SetupAttachment(RootComponent);
```

在 `EditGrid` 那一段前补一行注释：

```cpp
	// 大气壳与矿物观察组件不在这里创建：它们由 BP_VoxelPlanet 挂载。
	// 原生 AVoxelPlanetActor 实例因此不带这两个组件，本项目只用 BP。
```

- [ ] **Step 3: 改矿物观察组件的两处调用**

`Source/StellarEraPlanet/Private/PlanetMineralViewComponent.cpp`：把

```cpp
	if (UPlanetAtmosphereComponent* Atmosphere = InPlanet->GetAtmosphereComponent())
```
改成
```cpp
	if (UPlanetAtmosphereComponent* Atmosphere =
		InPlanet->FindComponentByClass<UPlanetAtmosphereComponent>())
```

把

```cpp
		if (UPlanetAtmosphereComponent* Atmosphere = RestorePlanet->GetAtmosphereComponent())
```
改成
```cpp
		if (UPlanetAtmosphereComponent* Atmosphere =
			RestorePlanet->FindComponentByClass<UPlanetAtmosphereComponent>())
```

- [ ] **Step 4: 改飞船的两处调用**

`Source/StellarEraSpaceship/Private/SpaceshipPawn.cpp` 里两处相同文本：

```cpp
	const UPlanetAtmosphereComponent* Atmosphere = PlanetActor->GetAtmosphereComponent();
```
改成
```cpp
	const UPlanetAtmosphereComponent* Atmosphere =
		PlanetActor->FindComponentByClass<UPlanetAtmosphereComponent>();
```

`PlanetActor` 是 `AVoxelPlanetActor*`；`FindComponentByClass` 是 `const` 成员，两个方法都是 `const`，可直接调。

- [ ] **Step 5: 改控制器的取组件方式**

`Source/StellarEra/Private/StellarEraPlayerController.cpp`：把

```cpp
	UPlanetMineralViewComponent* MineralView =
		Planet ? Planet->GetMineralViewComponent() : nullptr;
```
改成
```cpp
	UPlanetMineralViewComponent* MineralView =
		Planet ? Planet->FindComponentByClass<UPlanetMineralViewComponent>() : nullptr;
```

核对无残留：

```bash
cd F:/workspace/ue/StellarEra && grep -rn "GetAtmosphereComponent\|GetMineralViewComponent" Source
```
预期：无输出。

- [ ] **Step 6: BUILD** — 预期 `Result: Succeeded`

- [ ] **Step 7: 在编辑器里给每颗行星的 BP 挂组件**

1. 打开 `UniverseMap`，确认关卡里每颗 `AVoxelPlanetActor` 实例的实际类。已知行星 BP 在 `Content/Saves/PlanetVoxel/UniverseMap/`：`BP_VoxelPlanet`、`BP_VoxelPlanet2`、`BP_VoxelPlanet3`；`Content/Code/BP_VoxelPlanet` 是另一份，若关卡没用到可不改。
2. 对**每张被关卡使用的**行星 BP：打开，记下 `PlanetAtmosphere` / `PlanetMineralView` 两个组件上的非默认值（构造期组件此刻已消失，以迁移前最后一次保存为准；从未覆盖过就是类默认值，无需搬运）。
3. `Add Component` → `Planet Atmosphere`，命名 **`PlanetAtmosphere`**。
4. `Add Component` → `Planet Mineral View`，命名 **`PlanetMineralView`**。
5. 两个组件都挂在根组件下，相对变换保持零。
6. 把第 2 步记下的属性值填回去。
7. 保存 BP。**不要**在这一步保存 `UniverseMap`（`save_current_level` 会连此前遗留的未保存改动一起落盘）。

- [ ] **Step 8: 保存关卡**

确认 BP 都保存成功后，再保存 `UniverseMap`。

- [ ] **Step 9: TEST** — 预期 `Result: Passed`，无 `Result: Failed`

- [ ] **Step 10: 提交（代码 + BP，同一提交）**

```bash
cd F:/workspace/ue/StellarEra && git add Source/StellarEraPlanet Source/StellarEraSpaceship Source/StellarEra Content/Saves/PlanetVoxel Content/Maps && git commit -m "refactor(行星): 大气壳与矿物观察组件改由 BP_VoxelPlanet 挂载

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- Source/StellarEraPlanet Source/StellarEraSpaceship Source/StellarEra Content/Saves/PlanetVoxel Content/Maps
```

---

### Task 9: 拆出 `StellarEraGeology`

**Files:**
- Create: `Source/StellarEraGeology/StellarEraGeology.Build.cs`、`.h`、`.cpp`
- Move: `Source/StellarEraPlanet/Public/PlanetGeologyField.h`、`PlanetGeologyTypes.h`；`Source/StellarEraPlanet/Private/PlanetGeologyField.cpp`
- Keep: `Source/StellarEraPlanet/Private/PlanetGeologyFieldTests.cpp` **留在** `StellarEraPlanet`（它要实例化 `UVoxelPlanetGenerator` / `UVoxelPlanetNoiseTerrain`，搬进 Geology 会成环）
- Modify: `Source/StellarEraPlanet/StellarEraPlanet.Build.cs`、`uproject`、`Target.cs`

**Interfaces:**
- Consumes: Voxel 插件的 `FVoxelFastNoise`、JSON
- Produces: 模块 `StellarEraGeology`；API 宏 `STELLARERAGEOLOGY_API`；类 `FPlanetGeologyField`；`PlanetGeologyTypes.h` 里的结构体与枚举

- [ ] **Step 1: 建骨架并移动 3 个文件**

`Source/StellarEraGeology/StellarEraGeology.h`：

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
```

`Source/StellarEraGeology/StellarEraGeology.cpp`：

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "StellarEraGeology.h"
#include "Modules/ModuleManager.h"

// 非 primary 模块。primary game module 全项目只能有一个，它留在 StellarEra.cpp。
IMPLEMENT_MODULE(FDefaultModuleImpl, StellarEraGeology);
```

```bash
cd F:/workspace/ue/StellarEra && mkdir -p Source/StellarEraGeology/Public Source/StellarEraGeology/Private
git mv Source/StellarEraPlanet/Public/PlanetGeologyField.h Source/StellarEraGeology/Public/
git mv Source/StellarEraPlanet/Public/PlanetGeologyTypes.h Source/StellarEraGeology/Public/
git mv Source/StellarEraPlanet/Private/PlanetGeologyField.cpp Source/StellarEraGeology/Private/
```

- [ ] **Step 2: 换宏**

```bash
cd F:/workspace/ue/StellarEra && grep -rl "STELLARERAPLANET_API" Source/StellarEraGeology | xargs sed -i 's/STELLARERAPLANET_API/STELLARERAGEOLOGY_API/g'
grep -rn "_API" Source/StellarEraGeology/Public
```
预期：`PlanetGeologyField.h` 是 `STELLARERAGEOLOGY_API`。

- [ ] **Step 3: 写 `StellarEraGeology.Build.cs`**

```csharp
// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

// 地质层：元素表、行星地质场、JSON 预设、噪声采样。
//
// Voxel 是 **Public**：PlanetGeologyField.h 在公开头里 include
// FastNoise/VoxelFastNoise.h（FVoxelFastNoise 按值持有）。
// Json 只在 .cpp 里用。
public class StellarEraGeology : ModuleRules
{
	public StellarEraGeology(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Voxel",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Json",
		});
	}
}
```

- [ ] **Step 4: 注册并让 Planet 依赖它**

- `StellarEra.uproject` 插入 `StellarEraGeology` 模块条目。
- 两个 `Target.cs` 各加 `"StellarEraGeology",   // 地质`。
- `Source/StellarEraPlanet/StellarEraPlanet.Build.cs`：
  - `PublicDependencyModuleNames` 加 `"StellarEraGeology",`（`VoxelPlanetTerrainModule.h` 的公开结构体暴露 `FPlanetGeologyField`）
  - 删掉这一段（连同它上面那行注释 `// JSON 只在 PlanetGeologyField.cpp 里用，不需要向下游传递。`）：
    ```csharp
    		PrivateDependencyModuleNames.AddRange(new string[] { "Json" });
    ```

- [ ] **Step 5: BUILD** — 预期 `Result: Succeeded`

- [ ] **Step 6: TEST** — 预期 `Result: Passed`，无 `Result: Failed`（`PlanetGeologyFieldTests` 此时仍在 Planet，照跑）

- [ ] **Step 7: 提交**

```bash
cd F:/workspace/ue/StellarEra && git add Source/StellarEraGeology Source/StellarEraPlanet StellarEra.uproject Source/StellarEra.Target.cs Source/StellarEraEditor.Target.cs && git commit -m "refactor(模块): 拆出 StellarEraGeology（地质）

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- Source/StellarEraGeology Source/StellarEraPlanet StellarEra.uproject Source/StellarEra.Target.cs Source/StellarEraEditor.Target.cs
```

---

### Task 10: 拆出 `StellarEraAtmosphere`

**Files:**
- Create: `Source/StellarEraAtmosphere/StellarEraAtmosphere.Build.cs`、`.h`、`.cpp`
- Move: `Source/StellarEraPlanet/Public/PlanetAtmosphereComponent.h`、`PlanetAtmosphereParams.h`；`Source/StellarEraPlanet/Private/PlanetAtmosphereComponent.cpp`、`PlanetAtmosphereParams.cpp`、`PlanetAtmosphereParamsTests.cpp`、`PlanetAtmosphereMaterialBuilder.cpp`（**没有** `PlanetAtmosphereMaterialBuilder.h`，材质构建函数只在 `.cpp` 里）
- Modify: `Source/StellarEraSpaceship/StellarEraSpaceship.Build.cs`、`Config/DefaultEngine.ini`、`uproject`、`Target.cs`

**Interfaces:**
- Consumes: `AVoxelPlanetActor`（`StellarEraPlanet`，只 `Cast`/指针，公开头不 include）
- Produces: 模块 `StellarEraAtmosphere`；API 宏 `STELLARERAATMOSPHERE_API`；类 `UPlanetAtmosphereComponent`；命名空间 `PlanetAtmosphere`

- [ ] **Step 1: 建骨架并移动 6 个文件**

`Source/StellarEraAtmosphere/StellarEraAtmosphere.h`：

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
```

`Source/StellarEraAtmosphere/StellarEraAtmosphere.cpp`：

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "StellarEraAtmosphere.h"
#include "Modules/ModuleManager.h"

// 非 primary 模块。primary game module 全项目只能有一个，它留在 StellarEra.cpp。
IMPLEMENT_MODULE(FDefaultModuleImpl, StellarEraAtmosphere);
```

```bash
cd F:/workspace/ue/StellarEra && mkdir -p Source/StellarEraAtmosphere/Public Source/StellarEraAtmosphere/Private
git mv Source/StellarEraPlanet/Public/PlanetAtmosphereComponent.h Source/StellarEraAtmosphere/Public/
git mv Source/StellarEraPlanet/Public/PlanetAtmosphereParams.h    Source/StellarEraAtmosphere/Public/
git mv Source/StellarEraPlanet/Private/PlanetAtmosphereComponent.cpp      Source/StellarEraAtmosphere/Private/
git mv Source/StellarEraPlanet/Private/PlanetAtmosphereParams.cpp         Source/StellarEraAtmosphere/Private/
git mv Source/StellarEraPlanet/Private/PlanetAtmosphereParamsTests.cpp    Source/StellarEraAtmosphere/Private/
git mv Source/StellarEraPlanet/Private/PlanetAtmosphereMaterialBuilder.cpp Source/StellarEraAtmosphere/Private/
```

搬完核对：

```bash
cd F:/workspace/ue/StellarEra && ls Source/StellarEraAtmosphere/Public Source/StellarEraAtmosphere/Private
```
预期：Public 2 个头（`PlanetAtmosphereComponent.h`、`PlanetAtmosphereParams.h`），Private 4 个 `.cpp`。

- [ ] **Step 2: 换宏**

```bash
cd F:/workspace/ue/StellarEra && grep -rl "STELLARERAPLANET_API" Source/StellarEraAtmosphere | xargs sed -i 's/STELLARERAPLANET_API/STELLARERAATMOSPHERE_API/g'
```

- [ ] **Step 3: 写 `StellarEraAtmosphere.Build.cs`**

```csharp
// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

// 大气层：球外可见的大气壳、参数推导、壳材质。
//
// Planet 是 **Private**：PlanetAtmosphereComponent.cpp 在 .cpp 里
// Cast<AVoxelPlanetActor>(GetOwner()) 取表面半径，公开头不 include 行星模块。
public class StellarEraAtmosphere : ModuleRules
{
	public StellarEraAtmosphere(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"StellarEraPlanet",
		});
	}
}
```

- [ ] **Step 4: 注册 + 更新飞船依赖**

- `StellarEra.uproject` 插入 `StellarEraAtmosphere` 模块条目。
- 两个 `Target.cs` 各加 `"StellarEraAtmosphere", // 大气`。
- `Source/StellarEraSpaceship/StellarEraSpaceship.Build.cs` 的 `PrivateDependencyModuleNames` 加 `"StellarEraAtmosphere",`（`SpaceshipPawn.cpp` include `PlanetAtmosphereComponent.h`）。

- [ ] **Step 5: 加重定向**

```ini
+ClassRedirects=(OldName="/Script/StellarEraPlanet.PlanetAtmosphereComponent",NewName="/Script/StellarEraAtmosphere.PlanetAtmosphereComponent")
```

- [ ] **Step 6: BUILD** — 预期 `Result: Succeeded`

- [ ] **Step 7: TEST** — 预期 `Result: Passed`，无 `Result: Failed`

- [ ] **Step 8: 提交**

```bash
cd F:/workspace/ue/StellarEra && git add Source/StellarEraAtmosphere Source/StellarEraPlanet Source/StellarEraSpaceship StellarEra.uproject Source/StellarEra.Target.cs Source/StellarEraEditor.Target.cs Config/DefaultEngine.ini && git commit -m "refactor(模块): 拆出 StellarEraAtmosphere（大气）

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- Source/StellarEraAtmosphere Source/StellarEraPlanet Source/StellarEraSpaceship StellarEra.uproject Source/StellarEra.Target.cs Source/StellarEraEditor.Target.cs Config/DefaultEngine.ini
```

---

### Task 11: 拆出 `StellarEraMinerals`

**Files:**
- Create: `Source/StellarEraMinerals/StellarEraMinerals.Build.cs`、`.h`、`.cpp`
- Move: `Source/StellarEraPlanet/Public/PlanetMineralSurface.h`、`PlanetMineralViewActor.h`、`PlanetMineralViewComponent.h`、`PlanetMineralMaterialBuilder.h`；`Source/StellarEraPlanet/Private/PlanetMineralSurface.cpp`、`PlanetMineralSurfaceTests.cpp`、`PlanetMineralViewActor.cpp`、`PlanetMineralViewComponent.cpp`、`PlanetMineralViewTests.cpp`、`PlanetMineralMaterialBuilder.cpp`
- Modify: `Source/StellarEraPlanet/StellarEraPlanet.Build.cs`、`Source/StellarEraUI/StellarEraUI.Build.cs`、`Source/StellarEra/StellarEra.Build.cs`、`uproject`、`Target.cs`、`Config/DefaultEngine.ini`

**Interfaces:**
- Consumes: `StellarEraGeology`（公开头直接 include `PlanetGeologyTypes.h`）、`StellarEraAtmosphere`、`AVoxelPlanetActor`、`GeometryCore`、`GeometryFramework`
- Produces: 模块 `StellarEraMinerals`；API 宏 `STELLARERAMINERALS_API`；`APlanetMineralViewActor`、`UPlanetMineralViewComponent`、命名空间 `PlanetMineralSurface`、`PlanetMineralMaterialBuilder`

- [ ] **Step 1: 建骨架并移动 10 个文件**

`Source/StellarEraMinerals/StellarEraMinerals.h`：

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
```

`Source/StellarEraMinerals/StellarEraMinerals.cpp`：

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "StellarEraMinerals.h"
#include "Modules/ModuleManager.h"

// 非 primary 模块。primary game module 全项目只能有一个，它留在 StellarEra.cpp。
IMPLEMENT_MODULE(FDefaultModuleImpl, StellarEraMinerals);
```

```bash
cd F:/workspace/ue/StellarEra && mkdir -p Source/StellarEraMinerals/Public Source/StellarEraMinerals/Private
git mv Source/StellarEraPlanet/Public/PlanetMineralSurface.h           Source/StellarEraMinerals/Public/
git mv Source/StellarEraPlanet/Public/PlanetMineralViewActor.h         Source/StellarEraMinerals/Public/
git mv Source/StellarEraPlanet/Public/PlanetMineralViewComponent.h     Source/StellarEraMinerals/Public/
git mv Source/StellarEraPlanet/Public/PlanetMineralMaterialBuilder.h   Source/StellarEraMinerals/Public/
git mv Source/StellarEraPlanet/Private/PlanetMineralSurface.cpp        Source/StellarEraMinerals/Private/
git mv Source/StellarEraPlanet/Private/PlanetMineralSurfaceTests.cpp   Source/StellarEraMinerals/Private/
git mv Source/StellarEraPlanet/Private/PlanetMineralViewActor.cpp      Source/StellarEraMinerals/Private/
git mv Source/StellarEraPlanet/Private/PlanetMineralViewComponent.cpp  Source/StellarEraMinerals/Private/
git mv Source/StellarEraPlanet/Private/PlanetMineralViewTests.cpp      Source/StellarEraMinerals/Private/
git mv Source/StellarEraPlanet/Private/PlanetMineralMaterialBuilder.cpp Source/StellarEraMinerals/Private/
```

- [ ] **Step 2: 换宏**

```bash
cd F:/workspace/ue/StellarEra && grep -rl "STELLARERAPLANET_API" Source/StellarEraMinerals | xargs sed -i 's/STELLARERAPLANET_API/STELLARERAMINERALS_API/g'
```

- [ ] **Step 3: 写 `StellarEraMinerals.Build.cs`**

```csharp
// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

// 矿物层：逐元素等值面构建、观察体 Actor、矿体材质。
//
// Geology 是 **Public**：PlanetMineralViewActor.h / PlanetMineralViewComponent.h
// 在公开头里 include PlanetGeologyTypes.h。
// Planet / Atmosphere 只在 .cpp 里用（VoxelPlanetActor.h、PlanetAtmosphereComponent.h）。
public class StellarEraMinerals : ModuleRules
{
	public StellarEraMinerals(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "StellarEraGeology",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"StellarEraPlanet",
			"StellarEraAtmosphere",
			"GeometryCore",      // FMarchingCubes / FDynamicMesh3 / FMeshNormals
			"GeometryFramework", // UDynamicMeshComponent
		});
	}
}
```

- [ ] **Step 4: 注册 + 更新依赖 + 清理 Planet**

- `StellarEra.uproject` 插入 `StellarEraMinerals` 模块条目。
- 两个 `Target.cs` 各加 `"StellarEraMinerals",   // 矿物`。
- `Source/StellarEraPlanet/StellarEraPlanet.Build.cs`：删掉这四段（两行代码 + 各两行注释，注释在代码块上方）：
  ```csharp
  		// 矿物等值面：FMarchingCubes / FDynamicMesh3 / FMeshNormals 都在 GeometryCore
  		// 的运行时模块里（不是编辑器模块）。头文件不向下游暴露这些类型，所以是 Private。
  		PrivateDependencyModuleNames.AddRange(new string[] { "GeometryCore" });

  		// 矿体渲染走 UDynamicMeshComponent（GeometryFramework 的运行时模块）。
  		PrivateDependencyModuleNames.AddRange(new string[] { "GeometryFramework" });
  ```
- `Source/StellarEraUI/StellarEraUI.Build.cs`：`PrivateDependencyModuleNames` 加 `"StellarEraMinerals",` 与 `"StellarEraGeology",`（`StarMapUISystem.cpp` 两个头都直接 include）。
- `Source/StellarEra/StellarEra.Build.cs`：`PrivateDependencyModuleNames` 加 `"StellarEraMinerals",`。

- [ ] **Step 5: 加重定向**

```ini
+ClassRedirects=(OldName="/Script/StellarEraPlanet.PlanetMineralViewActor",NewName="/Script/StellarEraMinerals.PlanetMineralViewActor")
+ClassRedirects=(OldName="/Script/StellarEraPlanet.PlanetMineralViewComponent",NewName="/Script/StellarEraMinerals.PlanetMineralViewComponent")
```

- [ ] **Step 6: BUILD** — 预期 `Result: Succeeded`

- [ ] **Step 7: TEST** — 预期 `Result: Passed`，无 `Result: Failed`

- [ ] **Step 8: 提交**

```bash
cd F:/workspace/ue/StellarEra && git add Source/StellarEraMinerals Source/StellarEraPlanet Source/StellarEraUI Source/StellarEra StellarEra.uproject Source/StellarEra.Target.cs Source/StellarEraEditor.Target.cs Config/DefaultEngine.ini && git commit -m "refactor(模块): 拆出 StellarEraMinerals（矿物）

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- Source/StellarEraMinerals Source/StellarEraPlanet Source/StellarEraUI Source/StellarEra StellarEra.uproject Source/StellarEra.Target.cs Source/StellarEraEditor.Target.cs Config/DefaultEngine.ini
```

---

### Task 12: `StellarEraPlanet` 改名为 `StellarEraTerrain`

**Files:**
- Rename: `Source/StellarEraPlanet/` → `Source/StellarEraTerrain/`；`StellarEraPlanet.h/.cpp/.Build.cs` → `StellarEraTerrain.*`
- Move: `Source/StellarEra/Private/PlanetSurfaceRayTests.cpp` → `Source/StellarEraTerrain/Private/`
- Modify: 全仓 `STELLARERAPLANET_API` → `STELLARERATERRAIN_API`；所有 `Build.cs`、`uproject`、`Target.cs`、`DefaultEngine.ini`

**Interfaces:**
- Consumes: Task 1–11
- Produces: 模块 `StellarEraTerrain`；API 宏 `STELLARERATERRAIN_API`；`StellarEraPlanet` 这个名字从此不存在

- [ ] **Step 1: 改目录名与三个模块文件**

```bash
cd F:/workspace/ue/StellarEra
git mv Source/StellarEraPlanet Source/StellarEraTerrain
git mv Source/StellarEraTerrain/StellarEraPlanet.h        Source/StellarEraTerrain/StellarEraTerrain.h
git mv Source/StellarEraTerrain/StellarEraPlanet.cpp      Source/StellarEraTerrain/StellarEraTerrain.cpp
git mv Source/StellarEraTerrain/StellarEraPlanet.Build.cs Source/StellarEraTerrain/StellarEraTerrain.Build.cs
```

- [ ] **Step 2: 改模块名**

`Source/StellarEraTerrain/StellarEraTerrain.cpp`：`#include "StellarEraTerrain.h"`，并把 `IMPLEMENT_MODULE(FDefaultModuleImpl, StellarEraPlanet);` 改成：

```cpp
IMPLEMENT_MODULE(FDefaultModuleImpl, StellarEraTerrain);
```

`Source/StellarEraTerrain/StellarEraTerrain.Build.cs` 的类名改成：

```csharp
public class StellarEraTerrain : ModuleRules
{
	public StellarEraTerrain(ReadOnlyTargetRules Target) : base(Target)
```

- [ ] **Step 3: 换 API 宏**

```bash
cd F:/workspace/ue/StellarEra && grep -rl "STELLARERAPLANET_API" Source | xargs sed -i 's/STELLARERAPLANET_API/STELLARERATERRAIN_API/g'
grep -rn "STELLARERAPLANET_API" Source
```
预期：无输出。

- [ ] **Step 4: 把 `PlanetSurfaceRayTests.cpp` 搬进 Terrain**

```bash
cd F:/workspace/ue/StellarEra && git mv Source/StellarEra/Private/PlanetSurfaceRayTests.cpp Source/StellarEraTerrain/Private/
```

- [ ] **Step 5: 全仓替换模块名引用**

```bash
cd F:/workspace/ue/StellarEra && grep -rn "StellarEraPlanet" Source StellarEra.uproject
```
把每一处 `"StellarEraPlanet"` 依赖字符串改成 `"StellarEraTerrain"`：`Build.cs` 的 Public/Private 依赖、`uproject` 的 `Name`、两个 `Target.cs` 的 `ExtraModuleNames`。

```bash
cd F:/workspace/ue/StellarEra && grep -rl '"StellarEraPlanet"' Source StellarEra.uproject | xargs sed -i 's/"StellarEraPlanet"/"StellarEraTerrain"/g'
grep -rn "StellarEraPlanet" Source StellarEra.uproject
```
最后一条预期：无输出。

- [ ] **Step 6: 更新重定向**

现有 5 条 `NewName` 指向 `/Script/StellarEraPlanet.*`，全部改成 `/Script/StellarEraTerrain.*`；再为当前资产路径补同样目标的新条目（不能指望 `/Script/StellarEra.X → StellarEraPlanet → StellarEraTerrain` 的链式解析）：

```ini
+ClassRedirects=(OldName="/Script/StellarEra.VoxelPlanetActor",NewName="/Script/StellarEraTerrain.VoxelPlanetActor")
+ClassRedirects=(OldName="/Script/StellarEraPlanet.VoxelPlanetActor",NewName="/Script/StellarEraTerrain.VoxelPlanetActor")
+ClassRedirects=(OldName="/Script/StellarEra.VoxelPlanetNoiseTerrain",NewName="/Script/StellarEraTerrain.VoxelPlanetNoiseTerrain")
+ClassRedirects=(OldName="/Script/StellarEraPlanet.VoxelPlanetNoiseTerrain",NewName="/Script/StellarEraTerrain.VoxelPlanetNoiseTerrain")
+ClassRedirects=(OldName="/Script/StellarEra.VoxelPlanetGenerator",NewName="/Script/StellarEraTerrain.VoxelPlanetGenerator")
+ClassRedirects=(OldName="/Script/StellarEraPlanet.VoxelPlanetGenerator",NewName="/Script/StellarEraTerrain.VoxelPlanetGenerator")
+ClassRedirects=(OldName="/Script/StellarEra.VoxelPlanetSphereTerrain",NewName="/Script/StellarEraTerrain.VoxelPlanetSphereTerrain")
+ClassRedirects=(OldName="/Script/StellarEraPlanet.VoxelPlanetSphereTerrain",NewName="/Script/StellarEraTerrain.VoxelPlanetSphereTerrain")
+ClassRedirects=(OldName="/Script/StellarEra.VoxelPlanetTerrainModule",NewName="/Script/StellarEraTerrain.VoxelPlanetTerrainModule")
+ClassRedirects=(OldName="/Script/StellarEraPlanet.VoxelPlanetTerrainModule",NewName="/Script/StellarEraTerrain.VoxelPlanetTerrainModule")
+ClassRedirects=(OldName="/Script/StellarEraPlanet.PlanetSurfaceGridComponent",NewName="/Script/StellarEraTerrain.PlanetSurfaceGridComponent")
+ClassRedirects=(OldName="/Script/StellarEraPlanet.AlwaysLocalVoxelInvokerComponent",NewName="/Script/StellarEraTerrain.AlwaysLocalVoxelInvokerComponent")
+ClassRedirects=(OldName="/Script/StellarEraPlanet.AlwaysLocalVoxelInvokerWithPredictionComponent",NewName="/Script/StellarEraTerrain.AlwaysLocalVoxelInvokerWithPredictionComponent")
```

- [ ] **Step 7: BUILD** — 预期 `Result: Succeeded`

- [ ] **Step 8: TEST** — 预期 `Result: Passed`，无 `Result: Failed`

- [ ] **Step 9: 提交**

```bash
cd F:/workspace/ue/StellarEra && git add Source StellarEra.uproject Source/StellarEra.Target.cs Source/StellarEraEditor.Target.cs Config/DefaultEngine.ini && git commit -m "refactor(模块): StellarEraPlanet 改名为 StellarEraTerrain

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- Source StellarEra.uproject Source/StellarEra.Target.cs Source/StellarEraEditor.Target.cs Config/DefaultEngine.ini
```

---

### Task 13: 清理、文档与全量验证

**Files:**
- Modify: `CLAUDE.md`（模块结构一节）
- Verify: 全仓

**Interfaces:**
- Consumes: Task 1–12
- Produces: 一个可交付的分支状态

- [ ] **Step 1: 核对模块清单**

```bash
cd F:/workspace/ue/StellarEra && grep '"Name"' StellarEra.uproject
ls -d Source/*/
```
预期：恰好 11 个模块 —— `StellarEra`、`StellarEraCore`、`StellarEraGeology`、`StellarEraTerrain`、`StellarEraAtmosphere`、`StellarEraMinerals`、`StellarEraPlanetEdit`、`StellarEraCharacter`、`StellarEraSpaceship`、`StellarEraCosmos`、`StellarEraUI`。`StellarEraPlanet`、`StellarEraVehicle` 不再出现。

- [ ] **Step 2: 核对依赖方向无环**

```bash
cd F:/workspace/ue/StellarEra && grep -n "StellarEra" Source/*/*.Build.cs
```
逐条核对：
- 除 `StellarEra/StellarEra.Build.cs` 外，**没有**任何 `Build.cs` 依赖 `"StellarEra"`；
- `StellarEraCore` 不依赖任何项目模块；
- `StellarEraGeology` 只依赖 Core / CoreUObject / Voxel / Json；
- `StellarEraTerrain` 只依赖 Core / CoreUObject / Engine / Voxel / StellarEraGeology / AssetRegistry；
- `StellarEraUI` 不依赖 `StellarEra`。

- [ ] **Step 3: 核对重定向覆盖**

```bash
cd F:/workspace/ue/StellarEra && grep -rn "UCLASS\|UENUM" Source/*/Public/*.h
```
对每一个 `UCLASS` / `UENUM`，检查 `Config/DefaultEngine.ini` 的 `[CoreRedirects]` 里是否有条目。允许缺失的只有 `StellarEraGameMode`、`StellarEraPlayerController`（仍在原模块 `StellarEra`）。

- [ ] **Step 4: 关编辑器，全量 BUILD** — 预期 `Result: Succeeded`

- [ ] **Step 5: 全量 TEST** — 预期 `Result: Passed`，无 `Result: Failed`

- [ ] **Step 6: 资产加载验证**

```bash
cd F:/workspace/ue/StellarEra && ls -l Content/Code/BP_VoxelPlanet.uasset Content/Code/BP_PlanetCharacter.uasset Content/Code/BP_SpaceshipPawn.uasset Content/Saves/PlanetVoxel/UniverseMap/*.uasset
```
每个文件都应远大于 1 KB（LFS 指针约 130 字节；若只有一百多字节，先 `git lfs checkout`）。

打开编辑器加载 `UniverseMap`，在 Output Log 里搜 `Failed to find class`、`unknown class`、`Failed to load`。预期：无。

- [ ] **Step 7: PIE 运行时验证（5 项）**

在 `UniverseMap` 里 PIE：
1. 开局能看到大气壳（星球边缘有蓝色辉光）；
2. 按 M 进星图：相机可拉远/旋转，选中面板与悬停面板都出现；
3. 按 H 进矿物透视：矿体出现、图例在左上角、数字键切换元素有效；
4. 按 G 进编辑模式：网格出现、左键挖、右键点放生效；
5. 靠近飞船按 E 登船、停稳后按 E 离船。

任一失败：先看日志有没有重定向/类加载告警，再对照对应任务的 `Build.cs` 与 `[CoreRedirects]`。

- [ ] **Step 8: 更新 `CLAUDE.md`**

把「模块结构（依赖严格分层，无环）」一节整段换成 11 模块的版本（依赖图照抄 spec §5），并补一条：

```markdown
- 模块内部约定：**UI 不依赖 `StellarEra`（Game）**。星图 UI 需要控制器数据时走
  `IStarMapUIInputs`（`Source/StellarEraUI/Public/StarMapUIInputs.h`），由
  `AStellarEraPlayerController` 实现。
```

- [ ] **Step 9: 提交**

```bash
cd F:/workspace/ue/StellarEra && git add CLAUDE.md && git commit -m "docs(架构): CLAUDE.md 同步 11 模块结构

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- CLAUDE.md
```

---

## 验收标准

1. `Source/` 下恰好 11 个模块，依赖方向符合 spec §5，无环。
2. `StellarEraEditor Win64 Development` 构建成功。
3. `StellarEra.*` 自动化测试全部通过。
4. `UniverseMap` 加载无 `Failed to find class` / `unknown class` / `Failed to load`。
5. PIE 五项运行时检查全部通过。
6. `[CoreRedirects]` 覆盖所有搬走的 `UCLASS` 与 `EPlanetBrushShape`，且每个类都有「旧模块」与「当前模块」两个起点。
