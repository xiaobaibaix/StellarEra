# 开启物理尺度 M4b 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让 `bPhysicalScale` 可以安全打开：远场天体用代理渲染、深空原点跟飞船、预测线/预览线走同一映射、组合配置有警告，最后在 PIE 里用真实天体距离验证角直径与切换。

**Architecture:** `AVoxelPlanetActor` 增加一个隐藏的球代理与 `SetFarFieldMode()`；子系统在写回时按近/远切换它，深空时把渲染原点转移到飞船；轨迹预测点统一经 `ToRenderWorld`。开关仍默认关，验证通过后再在配置里打开。

**Tech Stack:** UE 5.8 C++、UAutomationTest、UBT（捆绑 dotnet 直调）、PIE + MCP。

**Spec:** `docs/superpowers/specs/2026-11-22-universe-render-mapping-design.md`

## Global Constraints

- 不新增模块；中文 `.h`/`.cpp` 保持 UTF-8 BOM；测试在 `Src/Tests/`。
- `bPhysicalScale` 默认 `false`；关闭时行为不变。
- 提交带 pathspec；构建/测试命令同前。

---

### Task 1: 行星远场代理

**Files:**
- Modify: `Source/StellarEraPlanet/Src/VoxelPlanetActor.h`
- Modify: `Source/StellarEraPlanet/Src/VoxelPlanetActor.cpp`
- Test: `Source/StellarEraPlanet/Src/Tests/PlanetFarProxyTests.cpp`

**Interfaces:**
- Produces: `void AVoxelPlanetActor::SetFarFieldMode(bool bFar)`；`bool IsFarFieldMode() const`。

- [ ] **Step 1: 写失败测试**

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraPlanetFarProxyTest,
	"StellarEra.Planet.Geo.FarProxy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraPlanetFarProxyTest::RunTest(const FString& Parameters)
{
	AVoxelPlanetActor* Planet = NewObject<AVoxelPlanetActor>(GetTransientPackage());
	if (!TestNotNull(TEXT("行星"), Planet)) { return false; }

	TestFalse(TEXT("默认近场"), Planet->IsFarFieldMode());
	Planet->SetFarFieldMode(true);
	TestTrue(TEXT("进入远场"), Planet->IsFarFieldMode());
	TestFalse(TEXT("远场关碰撞"), Planet->GetActorEnableCollision());
	Planet->SetFarFieldMode(false);
	TestTrue(TEXT("回到近场开碰撞"), Planet->GetActorEnableCollision());

	return true;
}
```

- [ ] **Step 2: 跑测试确认失败**

过滤 `StellarEra.Planet.Geo.FarProxy`，Expected: 编译失败。

- [ ] **Step 3: 实现**

`VoxelPlanetActor.h`（public 段）：

```cpp
	/** 远场代理：一个禁用碰撞与阴影的球，按物理半径缩放，只在远场可见。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Planet")
	TObjectPtr<UStaticMeshComponent> FarProxy;

	/** 远场模式：隐藏体素与其它图元，只留代理；关闭碰撞。 */
	void SetFarFieldMode(bool bFar);
	bool IsFarFieldMode() const { return bFarFieldMode; }
```

私有成员 `bool bFarFieldMode = false;`。

构造函数末尾：

```cpp
	FarProxy = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FarProxy"));
	FarProxy->SetupAttachment(RootComponent);
	FarProxy->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FarProxy->SetCastShadow(false);
	FarProxy->SetVisibility(false, true);
	if (const UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")))
	{
		FarProxy->SetStaticMesh(Sphere);
	}
```

实现：

```cpp
void AVoxelPlanetActor::SetFarFieldMode(bool bFar)
{
	if (bFarFieldMode == bFar)
	{
		return;
	}
	bFarFieldMode = bFar;

	// BasicShapes/Sphere 半径 50cm；Actor 在远场被缩到 FarScale，代理随 Actor 缩放。
	FarProxy->SetRelativeScale3D(FVector(GetPhysicalRadiusCm() / 50.f));
	FarProxy->SetVisibility(bFar, true);

	TArray<UPrimitiveComponent*> Primitives;
	GetComponents(Primitives);
	for (UPrimitiveComponent* Primitive : Primitives)
	{
		if (Primitive && Primitive != FarProxy)
		{
			Primitive->SetVisibility(!bFar, true);
		}
	}
	SetActorEnableCollision(!bFar);
}
```

- [ ] **Step 4: 构建并跑测试**（新用例通过；`StellarEra.Planet.Geo` 无新增失败）

- [ ] **Step 5: 提交**

```bash
git add <3 个路径>
git commit -m "feat(行星): 远场球代理与体素显隐切换" -- <3 个路径>
```

---

### Task 2: 子系统驱动代理 + 深空原点转移 + 组合警告

**Files:**
- Modify: `Source/StellarEraCosmos/Src/CosmosSimulationSubsystem.cpp`
- Modify: `Source/StellarEraCosmos/Src/CosmosSimulationSubsystem.h`

**Interfaces:**
- Consumes: `SetFarFieldMode`。
- 行为：物理模式下远场行星切代理；深空清空当前天体时把 `RenderAnchorIndex` 转到飞船、钉住其世界位置；`FinalizeRegistration` 对两个危险组合打警告。

- [ ] **Step 1: 写回里驱动代理**

写回循环拿 `AVoxelPlanetActor* Planet = Cast<AVoxelPlanetActor>(BodyActor)`；非空时：

```cpp
		Planet->SetFarFieldMode(bPhysicalScale && !bNear);
```

- [ ] **Step 2: 深空原点转移**

`UpdateCurrentBody` 的清空分支改为：

```cpp
	if (Picked == INDEX_NONE)
	{
		// 全部转远场；原点转钉到飞船，深空坐标不再随旧天体增长。
		RenderAnchorWorld = Simulator.ToWorldLocation(Positions[ShipIndex]);
		RenderAnchorIndex = ShipIndex;
		CurrentBodyIndex = INDEX_NONE;
		return;
	}
```

- [ ] **Step 3: 组合警告**

`FinalizeRegistration()` 里遍历 `Bodies`，对两个危险组合各打一条 Warning：物理开但无人填 `InitialPositionAu`；有人填了但物理没开。

- [ ] **Step 4: 构建并跑 Cosmos 测试**（30 例无回归；关闭开关时 `SetFarFieldMode(false)` 幂等）

- [ ] **Step 5: 提交**

```bash
git add <2 个路径>
git commit -m "feat(宇宙): 远场代理接线、深空原点转飞船、组合警告" -- <2 个路径>
```

---

### Task 3: 预测线经映射

**Files:**
- Modify: `Source/StellarEraCosmos/Src/CosmosSimulationSubsystem.cpp`
- Modify: `Source/StellarEraCosmos/Src/CosmosTrajectoryComponent.cpp`

**Interfaces:**
- 物理模式下，`PredictTrajectoryWorld` / `PredictClosedOrbitWorld` 返回的点与编辑器预览点都经 `ToRenderWorld`（以当前锚点/飞船或预览天体自身为中心）；关闭时保持原样。

- [ ] **Step 1: 子系统预测**：两处 `ToWorldLocation` 按 `bPhysicalScale` 分支到 `ToRenderWorld`。

- [ ] **Step 2: 编辑器预览**：`DrawInEditor` 的预测点以被预览天体自身为锚点与远场中心，经 `ToRenderWorld(..., bNear=false)` 压缩。

- [ ] **Step 3: 构建并跑 Cosmos 测试**（30 例无回归）

- [ ] **Step 4: 提交**

```bash
git add <2 个路径>
git commit -m "feat(宇宙): 轨迹预测与编辑器预览经映射（物理模式下）" -- <2 个路径>
```

---

### Task 4: 开启与 PIE 验证

- [ ] **Step 1: 配置与资产**

`Config/DefaultGame.ini` 开 `bPhysicalScale=True`；用 MCP 给 `UniverseMap` 里的行星与恒星填 `InitialPositionAu`（行星在原点、恒星约 1 AU），飞船放在行星表面附近。

- [ ] **Step 2: PIE 验证**

启动 PIE，截图并核对：
- 恒星角直径 ≈ 0.53°（远场代理 + 映射）；
- 行星近场 1:1，体素可见；远场行星只有代理球；
- 日志有「物理尺度」汇总行与渲染原点切换行；
- 无崩溃、无 `World Memory Leaks`。

- [ ] **Step 3: 关闭开关回归**

`bPhysicalScale` 改回 False，重跑全量测试，确认为 301/1。

- [ ] **Step 4: 勾选并提交文档**

```bash
git add docs/superpowers/plans/2026-11-22-universe-enable-m4b.md
git commit -m "docs(宇宙): M4b 计划勾选" -- docs/superpowers/plans/2026-11-22-universe-enable-m4b.md
```
