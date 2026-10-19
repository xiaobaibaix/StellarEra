# 球面角色运动动画设计

日期：2026-10-19
状态：已评审（设计批准）
相关模块：`StellarEraVehicle`、`Content/Code`、`Content/Anims`

## 1. 背景与目标

`APlanetCharacter`（`BP_PlanetCharacter`）当前使用 Pomni 模型 `Object_14`，骨架
`Object_14_Skeleton` 共 117 根骨骼，但没有动画：项目内不存在任何 `AnimSequence` /
`AnimBlueprint` / `BlendSpace`，`ArtSource/pomni_3d_model*.glb` 的 `animations`
数组为空。角色在游戏里保持参考姿势（T-pose 变体）。

目标：给角色加一套能跟随移动状态切换的运动动画（Idle / Walk / Run / Air），
不引入外部动画资源，不改变现有移动、相机、N 体逻辑。

## 2. 方案概述

```
APlanetCharacter
   └─ Mesh (UPlanetCharacterAnimInstance 驱动)
         ├─ GroundSpeed   速度投影到重力平面的大小（cm/s）
         └─ bIsFalling    来自 UCharacterMovementComponent
                 ↓
        ABP_PlanetCharacter
           └─ 状态机 Locomotion: Idle / Walk / Run / Air
                 ↓
        AS_Pomni_Idle / Walk / Run / Air    （程序化生成的 30 fps 序列）
```

- 动画状态只由「速度」和「是否离地」决定，与相机、朝向、输入无关。
- 角色朝向仍由 `bOrientRotationToMovement` 负责，动画只做原地前向循环。
- 状态切换用 crossfade，不做逐帧 BlendSpace。

## 3. 已确认的资产事实

| 项 | 值 |
|---|---|
| 角色网格 | `/Game/Fab/Blender_3D｜Pomni_3D_Model/pomni_3d_model/SkeletalMeshes/Object_14` |
| 骨架 | `.../SkeletalMeshes/Object_14_Skeleton`，117 根骨骼 |
| 参考姿势 | T-pose；手臂沿组件 ±X 展开，腿沿 -Z 垂下 |
| 组件空间轴向 | 右 = -X，前 = +Y，上 = +Z（由参考姿势骨骼全局位置实测） |
| 骨骼命名 | 导入器把 glTF 的 `.` 换成 `_`：`Leg_L_4`、`Knee_L_3`、`Ankle_L_2`、`Foot_L_0`、`Arm_L_70`、`Elbow_L_69`、`Wrist_L_68` 等 |
| 现有速度 | `WalkSpeed = 600 cm/s`，`SprintSpeed = 1200 cm/s` |
| Mesh 变换 | `MeshRelativeLocation = (0,0,-88)`，`MeshRelativeRotation = (0,-90,0)`，`MeshScale = 0.7` |

## 4. 动画资产

保存目录：`/Game/Anims/PlanetCharacter/`，文件名使用 Allar 前缀 `AS_`。

| 资产 | 时长 | 循环 | 内容 |
|---|---|---|---|
| `AS_Pomni_Idle` | 3.0 s | 是 | 呼吸起伏、重心轻微左右、手臂自然下垂微摆 |
| `AS_Pomni_Walk` | 1.0 s | 是 | 两步循环；腿摆 ±25°，手臂反向摆 ±20°，髋部 bob |
| `AS_Pomni_Run` | 0.7 s | 是 | 两步循环；腿摆 ±40°，躯干前倾 8–12°，手臂摆 ±35° |
| `AS_Pomni_Air` | 1.0 s | 是 | 收腿、抬臂、轻微飘动 |

约束：

- 全部片段都带「手臂从 T-pose 放下」的基础偏移（左臂绕组件 +Y 约 +85°，
  右臂约 -85°），否则角色会张开手臂行走。
- 只给以下骨骼打关键帧，其余骨骼保持参考姿势：
  - 躯干：`Hips_102`、`Spine_1_96`、`Spine_2_95`、`Spine_3_93`、`Spine_4_92`、
    `Neck_1_52`、`Neck_2_51`、`Head_50`
  - 左腿：`Leg_L_4`、`Knee_L_3`、`Ankle_L_2`、`Foot_L_0`
  - 右腿：`Leg_R_101`、`Knee_R_100`、`Ankle_R_99`、`Foot_R_97`
  - 左臂：`Shoulder_L_71`、`Arm_L_70`、`Elbow_L_69`、`Wrist_L_68`
  - 右臂：`Shoulder_R_91`、`Arm_R_90`、`Elbow_R_89`、`Wrist_R_88`
- 只有 `Hips_102` 带位移关键帧（上下 bob 与左右重心偏移），其余骨骼只写旋转。
- 帧率 30 fps；片段首尾帧姿势一致，保证循环无缝。

## 5. 关键帧计算

生成器从骨架参考姿势读取每根骨骼的局部旋转 `R0` 与局部位移 `t0`，用组件空间的
增量旋转 `D` 计算目标局部旋转：

```
R' = S_parent⁻¹ · D · S_bone
```

其中 `S_*` 是参考姿势的组件空间全局旋转。`D` 只用三个解剖轴：

- 前后摆腿 / 摆臂 / 躯干俯仰：绕组件 +X
- 左右倾斜 / 重心横移：绕组件 +Y
- 转身 / 头部偏航：绕组件 +Z

约定（已按参考姿势数值推导）：

- 腿向前摆：绕 +X 正角（腿骨指向 -Z，正角把它推向 +Y）
- 膝盖弯曲：绕 +X 负角（小腿向 -Y 收）
- 脊柱前倾：绕 +X 负角（脊柱指向 +Z）
- 手臂下垂后前摆：绕 +X 正角

`Hips_102` 的局部位移按 `t' = t0 + S_parent⁻¹ · Δ` 计算，`Δ` 是组件空间偏移。

## 6. AnimBP 与状态机

`/Game/Code/ABP_PlanetCharacter`，父类 `UPlanetCharacterAnimInstance`。

状态机 `Locomotion`，四个状态各接一个 Sequence Player（循环）：

| 切换 | 条件 | Crossfade |
|---|---|---|
| Idle → Walk | `GroundSpeed > 10` | 0.2 s |
| Walk → Idle | `GroundSpeed <= 10` | 0.2 s |
| Walk → Run | `GroundSpeed > 900` | 0.2 s |
| Run → Walk | `GroundSpeed <= 900` | 0.2 s |
| Idle → Air | `bIsFalling == true` | 0.1 s |
| Walk → Air | `bIsFalling == true` | 0.1 s |
| Run → Air | `bIsFalling == true` | 0.1 s |
| Air → Idle | `bIsFalling == false` | 0.1 s |

入口状态：`Idle`。落地后允许先进入 Idle，再由速度在下一帧切到 Walk / Run。

## 7. C++ 接线

新增 `UPlanetCharacterAnimInstance : UAnimInstance`（`StellarEraVehicle`）：

```cpp
UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
float GroundSpeed = 0.f;

UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
bool bIsFalling = false;

virtual void NativeUpdateAnimation(float DeltaSeconds) override;
```

`NativeUpdateAnimation` 里取 `TryGetPawnOwner()`，转 `APlanetCharacter` 后：

```
GroundSpeed = FVector::VectorPlaneProject(Velocity, GravityDir).Size()
bIsFalling  = GetCharacterMovement()->IsFalling()
```

不新增 Tick、不订阅委托。

`BP_PlanetCharacter` 的 `CharacterMesh0` 组件 `Anim Class` 指向
`ABP_PlanetCharacter_C`。`APlanetCharacter::ApplyCharacterMesh()` 不动。

## 8. 生成器与可复现性

新增 `Tools/Anim/GeneratePlanetCharacterAnims.py`，由编辑器内 Python 执行：

1. 读取骨架参考姿势（`AnimSequenceService.get_reference_pose`）。
2. 按第 5 节公式与第 4 节的节奏参数生成每个片段的骨骼轨道。
3. 调用 `AnimSequenceService.create_anim_sequence` 创建/覆盖四个资产。
4. 打印每个资产的时长、帧数、骨骼轨道数，供人工核对。

骨骼父子关系在本脚本内显式列出（导入器改名规则固定），不依赖第三方插件。

动画资产（`.uasset`）提交进 Git LFS，与仓库现有内容资产一致。

## 9. 验证

1. **生成器读回**：四个序列时长、帧数、关键骨骼轨道存在，首尾帧姿势一致。
2. **编辑器预览**：逐条打开 Idle / Walk / Run / Air 并截图，确认手臂已放下、
   腿部无反向弯折。
3. **PIE（`UniverseMap`）**：站立、行走、冲刺、跳跃各截图一次；确认状态切换
   与角色移动方向一致。
4. **自动化**：`UnrealEditor-Cmd ... Automation RunTests StellarEra` 全绿；新增
   一条 `StellarEra.Vehicle.AnimationAssets`：
   - 四个 `AS_` 资产可加载，时长 > 0，且含 `Leg_L_4` / `Arm_R_90` 的骨骼轨道
   - `BP_PlanetCharacter` 的 Mesh `Anim Class` 等于 `ABP_PlanetCharacter` 生成类

## 10. 已知取舍与风险

- **脚滑**：角色 600 / 1200 cm/s 的速度远超约 0.8 m 腿长能覆盖的步幅，不接脚部 IK
  必然出现脚滑。街机尺度下可接受；要根治需要降速或后续加 foot IK。
- **VibeUE 变量绑定未验证**：状态机切换条件绑定的是 C++ 父类上的
  `BlueprintReadOnly` 属性，VibeUE 的 `BuildStateMachine` 是否能直接引用继承属性
  尚未实测。若失败，退路是在 ABP 里声明同名 BP 变量，在 Event Graph 每帧从父类
  属性拷贝一份。
- **ABP 脚本创建未验证**：`AnimBlueprintFactory` + `AnimGraphService` 组合创建
  状态机需要在实施第一步验证；失败则改用编辑器手工创建 ABP，其余步骤不变。
- **动画质量**：程序化正弦曲线只能做到卡通级，不追求写实步态；参数集中在生成器
  脚本里，后续可直接调。
