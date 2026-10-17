# 行星携带角色（Planet Rider）Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让角色能稳定站在行星表面行走而不被甩出去 —— 关掉引擎 movement base 的两条运动通路，把角色的搬运完全交给 `CarryRiders`。

**Architecture:** 引擎的 movement base 保留「我脚下是什么」这层语义（`MOVE_Walking`、地板判定、摩擦、台阶、跳跃都不动），只关掉它的两条**运动**行为：(1) 离开底座时的速度注入，靠四个 `bImpartBase*` 开关关掉；(2) 首次贴上底座时的瞬移，靠重写 `UpdateBasedMovement` 为空实现关掉。两条都关掉后 `CarryRiders` 成为唯一搬运者，不会出现「引擎搬一次 + 我们搬一次」的双重搬运。

**Tech Stack:** Unreal Engine 5.8 / C++ / 模块 `StellarEraVehicle`；验证靠编辑器 MCP（Epic 官方 MCP，`http://127.0.0.1:8000/mcp`）在活体 PIE 里读属性。

**Spec:** `docs/superpowers/specs/2026-10-17-planet-rider-design.md`

## Global Constraints

- **引擎根目录**：`D:\Application\Epic Games\UE_5.8\`。项目：`F:\workspace\ue\StellarEra\StellarEra.uproject`。
- **构建必须关掉编辑器**，并且必须直调 UBT，不要用 `Build.bat`（它的锁机制会让 UBT 报找不到 `.uproject`）。固定用：
  ```
  powershell -NoProfile -ExecutionPolicy Bypass -File "C:\Users\23080\AppData\Local\Temp\ue_mcp\ue_build.ps1"
  ```
  该脚本内容：设 `DOTNET_ROOT` 到引擎自带 dotnet，`Set-Location 'C:\'`，直调 `UnrealBuildTool.dll StellarEraEditor Win64 Development -project=... -WaitMutex`。
- **Live Coding 对构造函数默认值不生效** —— 本计划两处改动都在构造函数/新类里，必须完整重编译。
- **自定义代码只进项目自己的 `Source/StellarEra*` 模块**，绝不写进 `Plugins/` 下任何第三方插件目录。
- **新建源文件必须存成 UTF-8 带 BOM**（`.h` / `.cpp` 都是）。项目里有中文注释，无 BOM 会被按 GBK 误读，IntelliSense 会报满屏假红波浪线。判真假一律以构建输出为准。
- **行尾保持混用现状，不要「统一成 LF」**。`core.autocrlf=true` 下 CRLF 才是稳定态。
- **不要提交、不要推送**，除非用户明确要求。
- 判「代码有没有真的编进去」要看构建产物 mtime + 符号，不要只看源码改动。
- 本计划没有单元测试框架可用：**每个任务的「测试」就是编辑器 MCP 在活体 PIE 里的属性读数**，判据写在任务里。

---

### Task 1: 零重编译确认「通路 1」是不是发射的主因

这是**诊断**任务，不改项目代码，目的是在动 C++ 之前先证伪或证实根因推断（规格 §6 第 1 步）。规格 §3 记录：行星 B 实测速率 1.0×10⁵–2.6×10⁵ cm/s，而实测落地速度 1.89×10⁶ cm/s 是它的 7–19 倍 —— 「触发一次」解释不了，所以必须实测确认。

**Files:**
- Create: `C:\Users\23080\AppData\Local\Temp\ue_mcp\probe_rider.ps1`（临时探针，不进项目仓库）

**Interfaces:**
- Consumes: 无
- Produces: `probe_rider.ps1`，两个模式：
  - `-Mode SetFlags` —— 把角色 CMC 的四个 `bImpartBase*` 设成 `false`
  - `-Mode Sample` —— 把角色摆到行星 B 表面上方，然后采样 `MovementMode` / `Velocity` / 到球心距离

- [ ] **Step 1: 启动编辑器**

用户手动启动，或：

```
powershell -NoProfile -Command "Start-Process 'D:\Application\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe' -ArgumentList 'F:\workspace\ue\StellarEra\StellarEra.uproject'"
```

等编辑器完全起来、MCP 端点可用（`http://127.0.0.1:8000/mcp` 能连上）。开局地图是 `UniverseMap`（`Config/DefaultEngine.ini` 的 `EditorStartupMap`）。

- [ ] **Step 2: 写探针脚本**

写入 `C:\Users\23080\AppData\Local\Temp\ue_mcp\probe_rider.ps1`：

```powershell
param(
    [ValidateSet('SetFlags','Sample')] [string]$Mode = 'Sample',
    [int]$Samples = 20
)
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$call = "C:\Users\23080\AppData\Local\Temp\ue_mcp\mcp_call.ps1"
$AT = "editor_toolset.toolsets.actor.ActorTools"
$OT = "editor_toolset.toolsets.object.ObjectTools"
$ST = "editor_toolset.toolsets.scene.SceneTools"

function T([string]$ts, [string]$tool, [string]$argJson) {
    $o = ''
    for ($i = 0; $i -lt 4; $i++) {
        $o = (& $call -Toolset $ts -Tool $tool -ArgsJson $argJson) -join ''
        if ($o -match '^\s*[\{\[]') { return $o }
        Start-Sleep -Seconds 2
    }
    return $o
}
function Xf([string]$p) {
    $j = T $AT 'get_actor_transform' ('{"actor":{"refPath":"' + $p + '"}}')
    try { return ($j | ConvertFrom-Json).returnValue.location } catch { return $null }
}
function Props([string]$p, [string]$names) {
    return (T $OT 'get_properties' ('{"instance":{"refPath":"' + $p + '"},"properties":[' + $names + ']}'))
}
function SetProps([string]$p, [hashtable]$kv) {
    $inner = ($kv | ConvertTo-Json -Compress)
    $esc   = ($inner | ConvertTo-Json -Compress)
    return (T $OT 'set_properties' ('{"instance":{"refPath":"' + $p + '"},"values":' + $esc + '}'))
}
function Dist($a,$b) {
    if ($null -eq $a -or $null -eq $b) { return -1 }
    [math]::Sqrt([math]::Pow($a.x-$b.x,2)+[math]::Pow($a.y-$b.y,2)+[math]::Pow($a.z-$b.z,2))
}

# --- 定位角色与行星 B ---
$cs = T $ST 'find_actors' '{"name":"","tag":"","collision_channels":[],"actor_type":{"refPath":"/Script/StellarEraVehicle.PlanetCharacter"}}'
$char = ''
try { $char = ($cs | ConvertFrom-Json).returnValue[0].refPath } catch { }
if (-not $char) { Write-Output ("NO CHARACTER: " + $cs); exit 1 }
$cmc = $char + '.CharMoveComp'

$ps = T $ST 'find_actors' '{"name":"","tag":"","collision_channels":[],"actor_type":{"refPath":"/Script/StellarEraPlanet.VoxelPlanetActor"}}'
$planets = @()
try { $planets = @(($ps | ConvertFrom-Json).returnValue | ForEach-Object { $_.refPath }) } catch { }
if ($planets.Count -lt 2) { Write-Output ("NEED 2 PLANETS, got: " + $ps); exit 1 }
$pB = $planets[1]

Write-Output ("char = " + $char)
Write-Output ("B    = " + $pB)

if ($Mode -eq 'SetFlags') {
    Write-Output ""
    Write-Output "=== 设四个 bImpartBase* 为 false ==="
    Write-Output (SetProps $cmc @{
        bImpartBaseVelocityX       = $false
        bImpartBaseVelocityY       = $false
        bImpartBaseVelocityZ       = $false
        bImpartBaseAngularVelocity = $false
    })
    Write-Output "=== 读回确认 ==="
    Write-Output (Props $cmc '"bImpartBaseVelocityX","bImpartBaseVelocityY","bImpartBaseVelocityZ","bImpartBaseAngularVelocity"')
    exit 0
}

# --- Sample 模式：把角色摆到行星 B 表面正上方，然后采样 ---
$b = Xf $pB
$c = Xf $char
$up = @{ x = ($c.x - $b.x); y = ($c.y - $b.y); z = ($c.z - $b.z) }
$upLen = [math]::Sqrt($up.x*$up.x + $up.y*$up.y + $up.z*$up.z)
if ($upLen -lt 1) { $up = @{x=1;y=0;z=0} } else { $up = @{x=$up.x/$upLen; y=$up.y/$upLen; z=$up.z/$upLen} }
# 表面半径 75,000 cm + 胶囊半高 88 + 300 cm 余量
$place = @{
    location = @{
        x = $b.x + $up.x * 75388
        y = $b.y + $up.y * 75388
        z = $b.z + $up.z * 75388
    }
}
Write-Output ""
Write-Output "=== 传送到表面上方 ==="
Write-Output (T $AT 'set_actor_transform' ('{"actor":{"refPath":"' + $char + '"},"xform":' + ($place | ConvertTo-Json -Compress -Depth 6) + '}'))

Write-Output ""
Write-Output "判据：MovementMode 稳定停在 MOVE_Walking；|Velocity| 始终 10^3 量级；"
Write-Output "      到球心距离稳定在 75,088 cm 附近。出现 10^5 以上就是没修好。"
$t0 = Get-Date
for ($i = 0; $i -lt $Samples; $i++) {
    $cc = Xf $char
    $bb = Xf $pB
    $t = ((Get-Date) - $t0).TotalSeconds
    Write-Output ([string]::Format("t={0,5:N1}s dB={1,12:N0}cm", $t, (Dist $cc $bb)))
    Write-Output ("   vm = " + (Props $cmc '"Velocity","MovementMode"'))
}
```

- [ ] **Step 3: 起 PIE**

在编辑器里点 Play（或让用户点）。等角色生成。

- [ ] **Step 4: 先跑一次 Sample 作为基线**

```
powershell -NoProfile -ExecutionPolicy Bypass -File "C:\Users\23080\AppData\Local\Temp\ue_mcp\probe_rider.ps1" -Mode Sample -Samples 20
```

预期：复现规格 §1 的现象 —— 落地瞬间 `Velocity` 跳到 10⁶ 量级，随后 dB 冲到 10⁶–10⁷ cm。

**如果这里没有复现**，说明现象不稳定，先别往下走，把读数贴出来讨论。

- [ ] **Step 5: 设掉四个开关，再跑一次 Sample**

```
powershell -NoProfile -ExecutionPolicy Bypass -File "C:\Users\23080\AppData\Local\Temp\ue_mcp\probe_rider.ps1" -Mode SetFlags
powershell -NoProfile -ExecutionPolicy Bypass -File "C:\Users\23080\AppData\Local\Temp\ue_mcp\probe_rider.ps1" -Mode Sample -Samples 20
```

判读：

- `MovementMode` 稳定停在 `MOVE_Walking`、`Velocity` 回到 10³ 量级 → **通路 1 是发射主因**，根因推断成立。
- 仍然被抛飞 → 通路 1 只是部分原因，通路 2（`LinearDelta` 瞬移）也在贡献。**这不影响 Task 2 的实施**（Task 2 两条都关），但要把读数记进规格 §6。

**若 `SetFlags` 被 `set_properties` 拒绝**（返回值里带 `ERR`）：不要在这上面纠缠，直接跳到 Task 2 —— 四个开关在 C++ 里同样能设，Task 3 的端到端验证才是最终判据。把拒绝信息记下来即可。

- [ ] **Step 6: 记录结果**

把两次 `Sample` 的关键读数（落地那一刻的 `Velocity`、`MovementMode`、dB 峰值）记进规格 §6 第 1 步的位置。**不提交**。

---

### Task 2: 关掉引擎 movement base 的两条运动通路

**Files:**
- Create: `Source/StellarEraVehicle/Public/PlanetCharacterMovementComponent.h`
- Create: `Source/StellarEraVehicle/Private/PlanetCharacterMovementComponent.cpp`
- Modify: `Source/StellarEraVehicle/Public/PlanetCharacter.h:30`
- Modify: `Source/StellarEraVehicle/Private/PlanetCharacter.cpp`（includes、构造函数签名、四个开关）

**Interfaces:**
- Consumes: 无
- Produces:
  - `UPlanetCharacterMovementComponent`，`STELLARERAVEHICLE_API`，继承 `UCharacterMovementComponent`，重写 `virtual void UpdateBasedMovement(float DeltaSeconds) override;` 为空实现
  - `APlanetCharacter(const FObjectInitializer& ObjectInitializer)` —— 构造函数签名变更（原来是 `APlanetCharacter()`）

- [ ] **Step 1: 新建 CMC 子类头文件**

创建 `Source/StellarEraVehicle/Public/PlanetCharacterMovementComponent.h`，**存成 UTF-8 带 BOM**：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "PlanetCharacterMovementComponent.generated.h"

/**
 * 行星角色的移动组件。
 *
 * 存在的唯一理由：把引擎的「移动底座携带」（UCharacterMovementComponent::UpdateBasedMovement）
 * 关掉。
 *
 * 行星以 km/s 移动、且距世界原点约 8×10⁵ cm，引擎那套为「几米/秒的电梯平台」设计的
 * 底座逻辑在这里会踩两个坑（详见
 * docs/superpowers/specs/2026-10-17-planet-rider-design.md §3）：
 *
 *   1. 离开底座时 ApplyImpartedMovementBaseVelocity() 把**底座自己的速度**加进角色的
 *      Velocity（CharacterMovementComponent.cpp:1463 → :2747）。行星速度是 km/s，
 *      角色被发射。这一条靠 APlanetCharacter 构造函数里的四个 bImpartBase* 开关关掉。
 *   2. 底座第一次变有效那一帧，LinearDelta = NewBaseLocation - OldBaseLocation
 *      （CharacterMovementComponent.cpp:2621），而 OldBaseLocation 初值是零向量
 *      （同文件 :832），于是位移量等于底座的绝对世界坐标 —— 单帧把胶囊瞬移约 8×10⁵ cm。
 *      这一条只能靠重写本函数关掉。
 *
 * 两条都关掉之后，角色的搬运全部交给 UCosmosSimulationSubsystem::CarryRiders，恰好一处。
 * 底座本身照旧由 CMC 设置，所以 MOVE_Walking、地板判定、摩擦、台阶、跳跃这些语义
 * **完全不受影响** —— 本类只拿走「底座会移动我」这一件事。
 */
UCLASS()
class STELLARERAVEHICLE_API UPlanetCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	/**
	 * 空实现。引擎靠它把角色跟着移动的底座一起搬走；在行星上这会把角色瞬移或发射出去，
	 * 改由 CarryRiders 承担。
	 *
	 * 不要顺手去动 OldBaseLocation：SaveBaseLocation()（同文件 :3201）在
	 * PostPhysicsTickComponent（同文件 :1834）里紧跟本函数调用，本函数空实现不影响
	 * 它对 OldBaseLocation 的正常维护。
	 */
	virtual void UpdateBasedMovement(float DeltaSeconds) override;
};
```

- [ ] **Step 2: 新建 CMC 子类实现文件**

创建 `Source/StellarEraVehicle/Private/PlanetCharacterMovementComponent.cpp`，**存成 UTF-8 带 BOM**：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetCharacterMovementComponent.h"

void UPlanetCharacterMovementComponent::UpdateBasedMovement(float /*DeltaSeconds*/)
{
	// 故意留空。理由见头文件。
	//
	// 参数不具名是为了避开未使用参数的编译警告。
}
```

- [ ] **Step 3: 改 PlanetCharacter.h 的构造函数签名**

`Source/StellarEraVehicle/Public/PlanetCharacter.h:30`，把：

```cpp
	APlanetCharacter();
```

改成：

```cpp
	APlanetCharacter(const FObjectInitializer& ObjectInitializer);
```

- [ ] **Step 4: 改 PlanetCharacter.cpp 的 include**

`Source/StellarEraVehicle/Private/PlanetCharacter.cpp`，在 `#include "PlanetCharacter.h"`（第 3 行）之后的 include 块里加一行：

```cpp
#include "PlanetCharacterMovementComponent.h"
```

- [ ] **Step 5: 换掉 CMC 子类**

`Source/StellarEraVehicle/Private/PlanetCharacter.cpp:23`，把：

```cpp
APlanetCharacter::APlanetCharacter()
{
```

改成：

```cpp
// SetDefaultSubobjectClass 必须写在初始化列表里：UObjectGlobals.h:1729 明确 assert
// 「不能在构造函数体内调用」。ACharacter 的构造函数会用 CharacterMovementComponentName
// 这个名字建 CMC，在这里先把它换成我们自己的子类，Super 跑完 GetCharacterMovement()
// 拿到的就是 UPlanetCharacterMovementComponent。
APlanetCharacter::APlanetCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UPlanetCharacterMovementComponent>(
		ACharacter::CharacterMovementComponentName))
{
```

已核实的 API 事实（不要改写）：
- `ACharacter::CharacterMovementComponentName` 是 `static ENGINE_API FName`（`Engine/Source/Runtime/Engine/Classes/GameFramework/Character.h:501`）。
- `ACharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get())`（同文件 :343）。
- `SetDefaultSubobjectClass(FName, const UClass*)` 返回 `const FObjectInitializer&`（`Engine/Source/Runtime/CoreUObject/Public/UObject/UObjectGlobals.h:1459`），可直接喂给 `Super`。

- [ ] **Step 6: 关掉通路 1 的四个开关**

同文件，在构造函数里已有的 CMC 设置之后（现在第 44-47 行是 `MaxStepHeight` / `PerchRadiusThreshold` / `LedgeCheckThreshold`），紧跟着加：

```cpp
	// 关掉「离开底座时把底座自己的速度加进角色 Velocity」。
	//
	// CMC 默认在每次 Walking→Falling 时调 ApplyImpartedMovementBaseVelocity()
	// （CharacterMovementComponent.cpp:1463 → :2747），而它加的就是底座的速度
	// （同文件 :1202 MovementBaseUtility::GetMovementBaseVelocity）。行星速度是 km/s，
	// 角色一离地就被发射出去。而且 FormerBaseVelocityDecayHalfLife 默认 0（同文件
	// CharacterMovementComponent.h:1092），注入值不衰减，直接变成角色自己的速度。
	//
	// 四个开关是 UPROPERTY(EditAnywhere, BlueprintReadWrite) 且在 public 段
	// （CharacterMovementComponent.h:928-954），这里关掉之后
	// GetImpartedMovementBaseVelocity() 恒返回零向量。
	//
	// 注意：走路、摩擦、地板判定、台阶、跳跃都不走这条路，关掉它们不受影响。
	Movement->bImpartBaseVelocityX = false;
	Movement->bImpartBaseVelocityY = false;
	Movement->bImpartBaseVelocityZ = false;
	Movement->bImpartBaseAngularVelocity = false;
```

（`Movement` 是构造函数里第 38 行已有的局部变量 `UCharacterMovementComponent* Movement = GetCharacterMovement();`。）

- [ ] **Step 7: 关掉编辑器，重编译**

**先确认编辑器已关闭**（构造函数默认值 Live Coding 不生效）：

```
powershell -NoProfile -Command "Get-Process -Name UnrealEditor* -ErrorAction SilentlyContinue | Select-Object Id, ProcessName"
```

没有输出才继续。然后：

```
powershell -NoProfile -ExecutionPolicy Bypass -File "C:\Users\23080\AppData\Local\Temp\ue_mcp\ue_build.ps1"
```

预期：`exit 0`。

**注意**：输出里出现 `Result: Failed` **不一定**是编译失败 —— 本项目出现过「编译成功、只有 DLL 链接失败」的情况。判据是看有没有 `error C` / `error LNK` 这类真正的编译错误行。

- [ ] **Step 8: 确认代码真的编进去了**

不要只看源码改动。看构建产物的 mtime：

```
powershell -NoProfile -Command "Get-ChildItem 'F:\workspace\ue\StellarEra\Binaries\Win64' -Filter '*.dll' | Select-Object Name, LastWriteTime"
```

`UnrealEditor-StellarEraVehicle.dll` 的 `LastWriteTime` 应该是刚才。

- [ ] **Step 9: 重新启动编辑器**

```
powershell -NoProfile -Command "Start-Process 'D:\Application\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe' -ArgumentList 'F:\workspace\ue\StellarEra\StellarEra.uproject'"
```

打开后确认 `BP_PlanetCharacter`（`Content/Code/BP_PlanetCharacter.uasset`）能正常加载、CMC 子对象变成 `UPlanetCharacterMovementComponent`。若蓝图报组件类不匹配，在蓝图里重新保存一次即可。

- [ ] **Step 10: 不提交**

本任务结束时不提交。等 Task 3 验证通过、用户明确要求后再提交。

---

### Task 3: PIE 端到端验证

**Files:**
- Modify: 无（沿用 Task 1 产出的 `C:\Users\23080\AppData\Local\Temp\ue_mcp\probe_rider.ps1`）

**Interfaces:**
- Consumes: Task 1 产出的 `probe_rider.ps1`；Task 2 产出的 `UPlanetCharacterMovementComponent`
- Produces: 验证结论

- [ ] **Step 1: 起 PIE 并采样**

在编辑器里点 Play，等角色生成，然后：

```
powershell -NoProfile -ExecutionPolicy Bypass -File "C:\Users\23080\AppData\Local\Temp\ue_mcp\probe_rider.ps1" -Mode Sample -Samples 25
```

- [ ] **Step 2: 判读（四条全部满足才算通过）**

1. `MovementMode` 能**稳定停在** `MOVE_Walking`，不是偶尔闪一下又掉回 `MOVE_Falling`。
2. `Velocity` 模长始终在 10³ cm/s 量级，**不出现 10⁵ 以上**。
3. 到行星 B 球心的距离**稳定在** 75,000（表面半径）+ 88（胶囊半高）= 75,088 cm 附近。
4. 传送过去之后不再出现 dB 冲到 10⁶ cm 的情况。

任何一条不满足 → 回 Task 2 检查，别在这里改代码。

- [ ] **Step 3: 手动回归 —— 行走**

在 PIE 里按住移动键走一段。判据：角色贴在球面上走，不打滑、不被甩出、不被抛到太空。走到球体背面（相对传送点）相机应当始终以球面法线为上。

- [ ] **Step 4: 手动回归 —— 跳跃**

在 PIE 里按跳。判据：跳起后**落回地面**，而不是被发射出去。

这一条专门盯通路 1 —— 跳跃正是 Walking→Falling 的转换，是 `ApplyImpartedMovementBaseVelocity()` 的原触发点。

- [ ] **Step 5: 手动回归 —— 出生**

退出 PIE 重新点 Play。判据：角色正常生成、能站住。

这一条盯通路 2 —— 出生是「底座第一次变有效」的场合，是 `LinearDelta` 瞬移的原触发点。

- [ ] **Step 6: 记录结论并汇报**

把三条判据的读数记进规格 §6。向用户汇报：
- Task 1 的诊断结论（通路 1 是不是主因）
- Task 3 的四条判据结果
- 是否还有残留现象

**不提交** —— 等用户明确要求。

---

## 附：本计划明确不做的事

（对应规格 §7，避免实施者顺手扩范围）

- **不改整体尺度**：行星轨道速率 km/s 与飞船速度、TimeScale 的关系，用户明确推迟。
- **不做粘性绑定**：`CarryRiders` 每帧重算「最近携带者」、飞船会缓慢漂出判定球被甩下的问题，属独立课题。本计划不改 `CarryRiders` 一行。
- **不解决角色怎么到达地表**：关卡里存在 PlayerStart，`bSpawnedAtPlayerStart = true` 导致 `SnapToPlanetSurface()` 与 Tick 兜底回贴都被跳过，而那个 PlayerStart 距行星 B 约 18 km。独立课题。
- **不动 `LookSensitivity`**（当前 1.2，约快 7 倍）。
- **不碰任何 `Plugins/` 下的第三方插件目录。**
