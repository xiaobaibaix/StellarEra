# N 体初态改由关卡摆放决定 — 设计变更

- **日期**：2026-10-17
- **项目**：StellarEra（Unreal Engine 5.8）
- **状态**：已实现（构建通过；填上初速度后在 PIE 验证）
- **取代**：`2026-10-15-nbody-multi-planet-design.md` §5.2 / §5.3 / §6.1 中关于
  `Orbit` / `CentralMassSolar` / `bApplyOrbitInit` 的部分，以及
  `2026-10-15-nbody-multi-planet.md` Task 3 / Task 4 / Task 6 中的同名步骤。

---

## 1. 动机

旧模型里天体初态由组件**生成**：`bApplyOrbitInit` 为真时，
`FNBodySimulator::ApplyOrbitInit` 按「半长轴 + 偏心率 + 相位」算出位置与速度，
**无条件覆盖** Actor 在关卡里摆放的位置。代价是：

- 关卡里摆的位置不算数 —— 想调轨道得改数值，不能在视口里直接拖；
- 组件成了初态的**作者**而不是读者，多出一层「轨道根数 ↔ 摆放」的对应关系要维护。
  Task 6 甚至要求行星**必须摆在近日点位置上**，否则开局选星球的规则会翻车。

新模型把这件事反过来：**关卡里摆成什么样，运行后就从什么样开始跑。**

## 2. 新模型

一个天体的初态只有两项，全部由外部给定：

| 初态 | 来源 |
|---|---|
| 位置 | `AActor::GetActorLocation()` —— 组件**不碰** |
| 速度 | `UCosmosOrbitComponent::InitialVelocity`（AU/年） |

组件不再生成任何初态，也不再在开局把 Actor 搬到别处。

`UCosmosOrbitComponent` 最终只剩三个可编辑属性（全部在 `Cosmos` / `Body` 分类下）：

| 属性 | 默认 | 说明 |
|---|---|---|
| `MassSolar` | `3.0034893e-6` | 质量，M☉ |
| `InitialVelocity` | `(0,0,0)` | 初速度，AU/年。留零即静止 |
| `bSimulated` | `true` | 是否每帧把求解器算出的位置写回 Actor |

## 3. 删除清单

| 符号 | 位置 | 原因 |
|---|---|---|
| `FCosmosOrbitInit` | `CosmosTypes.h` | 「轨道根数」这一整层概念没了 |
| `ECosmosOrbitPreset` | `CosmosTypes.h` | 同上（旧实现里它本来就被忽略） |
| `UCosmosOrbitComponent::Orbit` | `CosmosOrbitComponent.h` | 同上 |
| `UCosmosOrbitComponent::CentralMassSolar` | `CosmosOrbitComponent.h` | 只服务于「由根数推速度」 |
| `UCosmosOrbitComponent::bApplyOrbitInit` | `CosmosOrbitComponent.h` | 不再有「覆盖摆放位置」这回事 |
| `FNBodySimulator::ApplyOrbitInit` | `NBodySimulator.h` / `.cpp` | 同上 |

**`bSimulated` 保留** —— 它管的是「每帧写不写回 Actor」，与初态无关，
两者从来就是两件事（2026-10-15 设计文档 §5.2 已论证过不能合并）。

## 4. 两处连带改动

### 4.1 删掉开局的那次位置写回

`FinalizeRegistration` 原本在注册完每个天体后立刻做一次

```cpp
BodyActor->SetActorLocation(Simulator.ToWorldLocation(State.Position), /*bSweep=*/false);
```

那是为了掩盖「根数算出的位置 ≠ 摆放位置」造成的开局跳变。

根数删掉后，求解器里的位置就是摆放位置，这次写回只剩副作用：厘米 → AU → 厘米的
往返会把摆放位置按 float 精度挪动一点 —— 正是本次要消灭的「组件改变初始状态」。
**已删除。**

（删掉之后第一次 Tick 的写回仍会做同一趟往返 —— 求解器只认 AU —— 但那是亚厘米级
的取整，方向是「Actor 向求解器对齐」，不是「组件改写初态」。）

### 4.2 自动步长改由实际几何反推

原来读 `Orbit.SemiMajorAxis` 算 `T = 2π√(a³/GM)`。半长轴没了，改成对
**每一对天体** (i, j)，用它们的**实际间距** `r` 当圆轨道半径：

```
T_ij = 2π√( r³ / (G·(m_i + m_j)) )
步长 = min(T_ij) / StepsPerOrbit
```

取最短的那条 —— 它是系统里最快的动力学时标，积分步长必须跟着它走。
`StepsPerOrbit`（默认 20000）与 `MaxStepsPerFrame` 的含义不变。

原关卡几何（恒星 1 M☉ 在原点、行星在 0.718 AU 近日点）算出来是
**最短周期 ≈ 0.6085 年 / 步长 ≈ 3.04e-05 年**，与旧公式的
`0.6148 年 / 3.07e-05 年`基本一致 —— 差别只是把 `a` 换成了实际摆放距离。

## 5. 后果

1. **每个天体都要手填 `InitialVelocity`。** 留零的天体会直接掉向中心天体。
   圆轨道速度 `v = 2π√(M/r)`：`M` 是中心天体质量（M☉）、`r` 是到它的距离（AU），
   方向与「天体 → 中心天体」的连线垂直。
2. **「母星 = 离世界原点最近的行星」这条规则不再需要摆位配合。** 旧模型要求行星
   必须摆在近日点位置上，因为开局选星球发生在 `FinalizeRegistration` **之前**，
   那一刻看到的还是手工摆放的位置。现在位置从头到尾不变，这条约束自然消失。
3. 关卡里已存的 `Orbit` / `CentralMassSolar` / `bApplyOrbitInit` 数据会被引擎忽略
   （加载时可能报 unknown property 警告），保存一次关卡即清掉。

## 6. 涉及文件

- `Source/StellarEra/Public/CosmosTypes.h`
- `Source/StellarEra/Public/CosmosOrbitComponent.h`
- `Source/StellarEra/Public/NBodySimulator.h`
- `Source/StellarEra/Private/NBodySimulator.cpp`
- `Source/StellarEra/Public/CosmosSimulationSubsystem.h`
- `Source/StellarEra/Private/CosmosSimulationSubsystem.cpp`
