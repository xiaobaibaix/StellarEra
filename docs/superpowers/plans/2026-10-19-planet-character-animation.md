# 球面角色运动动画实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 给 `BP_PlanetCharacter` 加一套由移动状态驱动的 Idle / Walk / Run / Air 动画，不引入外部动画资源。

**Architecture:** 编辑器 Python 脚本用 VibeUE 的 `AnimSequenceService` 依据骨架参考姿势程序化生成四个 `AnimSequence`；`UPlanetCharacterAnimInstance` 每帧把重力平面上的速度和 `IsFalling` 写进 `GroundSpeed` / `bIsFalling`；`ABP_PlanetCharacter` 的 `Locomotion` 状态机按这两个属性切换片段。动画类通过新增的 `CharacterAnimClass` 属性在 `ApplyCharacterMesh()` 里接线（`CharacterMesh0` 是原生继承组件，蓝图 SCS 工具改不到它）。

**Tech Stack:** Unreal Engine 5.8.3 C++、VibeUE 编辑器 Python API（`AnimSequenceService` / `AnimGraphService` / `BlueprintService`）、Unreal Automation Test。

**Spec:** `docs/superpowers/specs/2026-10-19-planet-character-animation-design.md`

> 计划与 spec 的差异（已回写 spec）：spec §7 原写「用 `BlueprintService.SetComponentProperty` 设置 `CharacterMesh0` 的 Anim Class」，实测该 API 只遍历 SCS 节点，找不到 `ACharacter` 的原生组件，故改为 C++ 属性 `CharacterAnimClass` + `SetAnimInstanceClass`；§9 的断言随之改为检查 `BP_PlanetCharacter` CDO 属性（editor world 不跑 BeginPlay，无法断言运行期 Mesh 上的 AnimClass）。

## Global Constraints

- 只改这四个模块与 `Content/`；**不得修改** `Plugins/VoxelFree`、`Plugins/VibeUE`。
- `StellarEraVehicle` 不得依赖 `StellarEra`；本计划不新增模块依赖。
- 含中文的 `.h` / `.cpp` 必须 **UTF-8 with BOM**；新建文件后按 Task 2 Step 7 的脚本补 BOM。
- 不要统一换行符；仓库本来就是 CRLF/LF 混合。
- 构建前必须**关闭编辑器**（Live Coding 锁 DLL）。用捆绑 dotnet 直调 UBT，cwd 在项目外；不要用 `Build.bat`。
- 自动化测试用 `UnrealEditor-Cmd`，编辑器开着也能跑；editor world **不执行 BeginPlay**。
- 提交必须带 pathspec（`git commit -m "..." -- <paths>`），索引里有常驻暂存文件，裸 `git commit` 会吞掉全部。
- 资产用 Allar 前缀；新资产全部落在 `/Game/Anims/PlanetCharacter/` 与 `/Game/Code/`。
- `UniverseMap` 是唯一能验球面行走与体素地面的关卡；编辑器测性能/看画面前必须在前台（后台整进程限 3 FPS）。
- 本轮不要动 `PlanetCharacterMovementComponent`、`UCosmosSimulationSubsystem`、飞船与 N 体代码。

---

## 文件结构

**新建**

| 文件 | 职责 |
|---|---|
| `Tools/Anim/GeneratePlanetCharacterAnims.py` | 读参考姿势、按正弦节奏生成四个 `AnimSequence`，幂等重建 |
| `Tools/Anim/BuildPlanetCharacterAnimBP.py` | 创建 `ABP_PlanetCharacter`，搭 `Locomotion` 状态机并接 Output Pose |
| `Source/StellarEraVehicle/Public/PlanetCharacterAnimInstance.h` | 动画实例：`GroundSpeed` / `bIsFalling` |
| `Source/StellarEraVehicle/Private/PlanetCharacterAnimInstance.cpp` | 每帧从 `APlanetCharacter` 采样 |
| `Source/StellarEraVehicle/Private/PlanetCharacterAnimTests.cpp` | `StellarEra.Vehicle.AnimationAssets` 自动化测试 |

**修改**

| 文件 | 位置 | 改动 |
|---|---|---|
| `Source/StellarEraVehicle/Public/PlanetCharacter.h` | 第 10–15 行、第 177 行后、第 194 行后 | 前向声明、`CharacterAnimClass` / `FaceAttachBone`、`AttachFaceMeshesToHead()` |
| `Source/StellarEraVehicle/Private/PlanetCharacter.cpp` | 第 13 行后、第 153 行后、第 175 行后 | include、`SetAnimInstanceClass`、`BeginPlay` 调用、改挂面部网格 |

**生成资产（不手写）**

- `Content/Anims/PlanetCharacter/AS_Pomni_{Idle,Walk,Run,Air}.uasset`
- `Content/Code/ABP_PlanetCharacter.uasset`
- `Content/Code/BP_PlanetCharacter.uasset`（只改 CDO 的一个属性）

---

### Task 1: 程序化生成四个动画序列

**Files:**
- Create: `Tools/Anim/GeneratePlanetCharacterAnims.py`
- Generate: `Content/Anims/PlanetCharacter/AS_Pomni_Idle.uasset`、`AS_Pomni_Walk.uasset`、`AS_Pomni_Run.uasset`、`AS_Pomni_Air.uasset`

**Interfaces:**
- Produces: 四个 `AnimSequence` 资产，供 Task 3 的 `build_state_machine` 按路径引用：
  - `/Game/Anims/PlanetCharacter/AS_Pomni_Idle`（3.0 s）
  - `/Game/Anims/PlanetCharacter/AS_Pomni_Walk`（1.0 s）
  - `/Game/Anims/PlanetCharacter/AS_Pomni_Run`（0.7 s）
  - `/Game/Anims/PlanetCharacter/AS_Pomni_Air`（1.0 s）

- [ ] **Step 1: 写生成器脚本**

创建 `Tools/Anim/GeneratePlanetCharacterAnims.py`（UTF-8，不需要 BOM）：

```python
# -*- coding: utf-8 -*-
"""程序化生成 BP_PlanetCharacter 的四个运动动画片段。

在编辑器内执行（VibeUE 的 AnimSequenceService 必须可用）：

    import unreal
    exec(compile(open(r"F:/workspace/ue/StellarEra/Tools/Anim/GeneratePlanetCharacterAnims.py",
                      "r", encoding="utf-8").read(),
                 "GeneratePlanetCharacterAnims.py", "exec"))

重复运行会先删除同名资产再重建，脚本是幂等的。
"""

import math
import unreal

SKELETON_PATH = (
    "/Game/Fab/Blender_3D｜Pomni_3D_Model/pomni_3d_model/SkeletalMeshes/Object_14_Skeleton"
)
SAVE_PATH = "/Game/Anims/PlanetCharacter"
FRAME_RATE = 30.0

# glTF 层级。Root_142 不在 Object_14_Skeleton 的 117 根骨骼里，Hips 实际就是根骨。
BONE_PARENT = {
    "Hips_102": "Root_142",
    "Spine_1_96": "Hips_102",
    "Spine_2_95": "Spine_1_96",
    "Spine_3_93": "Spine_2_95",
    "Spine_4_92": "Spine_3_93",
    "Neck_1_52": "Spine_4_92",
    "Neck_2_51": "Neck_1_52",
    "Head_50": "Neck_2_51",
    "Leg_L_4": "Hips_102",
    "Knee_L_3": "Leg_L_4",
    "Ankle_L_2": "Knee_L_3",
    "Foot_L_0": "Ankle_L_2",
    "Leg_R_101": "Hips_102",
    "Knee_R_100": "Leg_R_101",
    "Ankle_R_99": "Knee_R_100",
    "Foot_R_97": "Ankle_R_99",
    "Shoulder_L_71": "Spine_4_92",
    "Arm_L_70": "Shoulder_L_71",
    "Elbow_L_69": "Arm_L_70",
    "Wrist_L_68": "Elbow_L_69",
    "Shoulder_R_91": "Spine_4_92",
    "Arm_R_90": "Shoulder_R_91",
    "Elbow_R_89": "Arm_R_90",
    "Wrist_R_88": "Elbow_R_89",
}

ANIMATED_BONES = [
    "Hips_102",
    "Spine_1_96", "Spine_2_95", "Spine_3_93", "Spine_4_92",
    "Neck_1_52", "Neck_2_51", "Head_50",
    "Leg_L_4", "Knee_L_3", "Ankle_L_2", "Foot_L_0",
    "Leg_R_101", "Knee_R_100", "Ankle_R_99", "Foot_R_97",
    "Shoulder_L_71", "Arm_L_70", "Elbow_L_69", "Wrist_L_68",
    "Shoulder_R_91", "Arm_R_90", "Elbow_R_89", "Wrist_R_88",
]


def axis_quat(axis, degrees):
    """绕组件空间某轴转 degrees 度。用手工半角构造，避免 Quat(Vector, angle) 的绑定问题。"""
    half = math.radians(degrees) * 0.5
    s = math.sin(half)
    return unreal.Quat(axis[0] * s, axis[1] * s, axis[2] * s, math.cos(half))


def quat_mul(a, b):
    return unreal.MathLibrary.multiply_quat_quat(a, b)


def quat_inv(q):
    return unreal.MathLibrary.quat_inversed(q)


def quat_rotate(q, x, y, z):
    return unreal.MathLibrary.quat_rotate_vector(q, unreal.Vector(x, y, z))


class ReferencePose:
    """骨架参考姿势的局部/组件空间全局旋转。"""

    def __init__(self, skeleton_path):
        self.local = {
            pose.bone_name: pose.transform
            for pose in unreal.AnimSequenceService.get_reference_pose(skeleton_path)
        }
        self._global = {}

    def global_rotation(self, bone):
        if bone not in self._global:
            parent = BONE_PARENT.get(bone)
            if parent in self.local:
                self._global[bone] = quat_mul(
                    self.global_rotation(parent), self.local[bone].rotation)
            else:
                self._global[bone] = self.local[bone].rotation
        return self._global[bone]

    def parent_rotation(self, bone):
        parent = BONE_PARENT.get(bone)
        if parent in self.local:
            return self.global_rotation(parent)
        return unreal.Quat(0.0, 0.0, 0.0, 1.0)


def make_keyframe(ref, bone, angles, hips_offset, frame, time):
    """R' = S_parent^-1 * D * S_bone；Hips 额外加 S_parent^-1 * Δ 的位移。"""
    local = ref.local[bone]
    rx, ry, rz = angles.get(bone, (0.0, 0.0, 0.0))
    parent_rotation = ref.parent_rotation(bone)

    if rx == 0.0 and ry == 0.0 and rz == 0.0:
        # 没动的骨骼直接沿用参考姿势，避免浮点误差。
        rotation = local.rotation
    else:
        delta = quat_mul(
            quat_mul(axis_quat((1.0, 0.0, 0.0), rx),
                     axis_quat((0.0, 1.0, 0.0), ry)),
            axis_quat((0.0, 0.0, 1.0), rz))
        rotation = quat_mul(
            quat_inv(parent_rotation), quat_mul(delta, ref.global_rotation(bone)))

    position = local.translation
    if bone == "Hips_102" and hips_offset != (0.0, 0.0, 0.0):
        offset = quat_rotate(quat_inv(parent_rotation), *hips_offset)
        position = unreal.Vector(
            position.x + offset.x, position.y + offset.y, position.z + offset.z)

    return unreal.AnimKeyframe(
        frame=frame,
        time=time,
        position=position,
        rotation=rotation,
        scale=local.scale3d,
    )


# ── 节奏参数 ────────────────────────────────────────────────────────────────
# 符号约定（已按参考姿势数值推导）：
#   腿向前摆 = 绕 +X 正角；膝盖弯曲 = 绕 +X 负角；脊柱前倾 = 绕 +X 负角；
#   左臂下垂 = 绕 +Y +85°；右臂下垂 = 绕 +Y -85°；手臂前摆 = 绕 +X 正角；
#   肘部弯曲 = 绕 +X 正角。

def clip_idle(p):
    w = 2.0 * math.pi * p
    breath = math.sin(w)
    angles = {
        "Spine_2_95": (-0.8 * breath, 0.0, 0.0),
        "Spine_3_93": (-0.4 * breath, 0.0, 0.0),
        "Head_50": (0.6 * math.sin(w + 0.6), 0.0, 0.0),
        "Arm_L_70": (3.0 * breath, 85.0, 0.0),
        "Arm_R_90": (3.0 * breath, -85.0, 0.0),
        "Elbow_L_69": (8.0, 0.0, 0.0),
        "Elbow_R_89": (8.0, 0.0, 0.0),
        "Leg_L_4": (1.5, 0.0, 0.0),
        "Leg_R_101": (1.5, 0.0, 0.0),
        "Knee_L_3": (-3.0, 0.0, 0.0),
        "Knee_R_100": (-3.0, 0.0, 0.0),
        "Hips_102": (0.0, 1.0 * breath, 0.0),
    }
    hips_offset = (0.8 * breath, 0.0, 0.4 * math.sin(2.0 * w))
    return angles, hips_offset


def clip_walk(p):
    w = 2.0 * math.pi * p
    s = math.sin(w)
    c = math.cos(w)
    angles = {
        "Hips_102": (0.0, 2.0 * s, 4.0 * s),
        "Spine_1_96": (-3.0, 0.0, 0.0),
        "Spine_2_95": (-1.0, 0.0, -3.0 * s),
        "Neck_1_52": (2.0, 0.0, 1.5 * s),
        "Head_50": (0.0, 0.0, -2.0 * s),
        "Leg_L_4": (25.0 * s, 0.0, 0.0),
        "Knee_L_3": (-(18.0 + 17.0 * c), 0.0, 0.0),
        "Leg_R_101": (-25.0 * s, 0.0, 0.0),
        "Knee_R_100": (-(18.0 - 17.0 * c), 0.0, 0.0),
        "Arm_L_70": (-20.0 * s, 85.0, 0.0),
        "Arm_R_90": (20.0 * s, -85.0, 0.0),
        "Elbow_L_69": (15.0, 0.0, 0.0),
        "Elbow_R_89": (15.0, 0.0, 0.0),
    }
    hips_offset = (1.5 * s, 0.0, -1.2 * math.cos(2.0 * w))
    return angles, hips_offset


def clip_run(p):
    w = 2.0 * math.pi * p
    s = math.sin(w)
    c = math.cos(w)
    angles = {
        "Hips_102": (0.0, 2.0 * s, 6.0 * s),
        "Spine_1_96": (-10.0, 0.0, 0.0),
        "Spine_2_95": (-3.0, 0.0, -4.0 * s),
        "Neck_1_52": (6.0, 0.0, 2.0 * s),
        "Head_50": (4.0, 0.0, -3.0 * s),
        "Leg_L_4": (40.0 * s, 0.0, 0.0),
        "Knee_L_3": (-(10.0 + 55.0 * (0.5 + 0.5 * c)), 0.0, 0.0),
        "Leg_R_101": (-40.0 * s, 0.0, 0.0),
        "Knee_R_100": (-(10.0 + 55.0 * (0.5 - 0.5 * c)), 0.0, 0.0),
        "Ankle_L_2": (8.0 * s, 0.0, 0.0),
        "Ankle_R_99": (-8.0 * s, 0.0, 0.0),
        "Arm_L_70": (-35.0 * s, 78.0, 0.0),
        "Arm_R_90": (35.0 * s, -78.0, 0.0),
        "Elbow_L_69": (70.0, 0.0, 0.0),
        "Elbow_R_89": (70.0, 0.0, 0.0),
    }
    hips_offset = (1.2 * s, 0.0, -2.5 * math.cos(2.0 * w))
    return angles, hips_offset


def clip_air(p):
    w = 2.0 * math.pi * p
    s = math.sin(w)
    angles = {
        "Hips_102": (0.0, 2.0 * s, 0.0),
        "Spine_1_96": (-5.0, 0.0, 0.0),
        "Head_50": (3.0, 0.0, 2.0 * s),
        "Leg_L_4": (32.0 + 2.0 * s, 0.0, 0.0),
        "Knee_L_3": (-55.0, 0.0, 0.0),
        "Ankle_L_2": (12.0, 0.0, 0.0),
        "Leg_R_101": (-14.0 - 2.0 * s, 0.0, 0.0),
        "Knee_R_100": (-38.0, 0.0, 0.0),
        "Ankle_R_99": (8.0, 0.0, 0.0),
        "Arm_L_70": (-8.0 + 3.0 * s, 65.0, 0.0),
        "Arm_R_90": (-8.0 - 3.0 * s, -65.0, 0.0),
        "Elbow_L_69": (35.0, 0.0, 0.0),
        "Elbow_R_89": (35.0, 0.0, 0.0),
    }
    hips_offset = (0.0, 0.0, 2.0 * math.sin(w))
    return angles, hips_offset


CLIP_SPECS = [
    ("AS_Pomni_Idle", 3.0, clip_idle),
    ("AS_Pomni_Walk", 1.0, clip_walk),
    ("AS_Pomni_Run", 0.7, clip_run),
    ("AS_Pomni_Air", 1.0, clip_air),
]


def build_animation(ref, name, duration, clip):
    frame_count = int(round(duration * FRAME_RATE))
    keys_by_bone = {bone: [] for bone in ANIMATED_BONES}
    for frame in range(frame_count + 1):
        # 最后一帧强制回到第 0 帧的姿势，保证循环无缝。
        phase = 0.0 if frame == frame_count else frame / float(frame_count)
        angles, hips_offset = clip(phase)
        time = frame / FRAME_RATE
        for bone in ANIMATED_BONES:
            keys_by_bone[bone].append(
                make_keyframe(ref, bone, angles, hips_offset, frame, time))

    tracks = [
        unreal.BoneTrackData(bone_name=bone, keyframes=keys_by_bone[bone])
        for bone in ANIMATED_BONES
    ]

    asset_path = f"{SAVE_PATH}/{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        unreal.EditorAssetLibrary.delete_asset(asset_path)

    created = unreal.AnimSequenceService.create_anim_sequence(
        SKELETON_PATH, name, SAVE_PATH, duration, FRAME_RATE, tracks)
    if not created:
        raise RuntimeError(f"CreateAnimSequence failed: {name}")

    length = unreal.AnimSequenceService.get_animation_length(created)
    frames = unreal.AnimSequenceService.get_animation_frame_count(created)
    bones = unreal.AnimSequenceService.get_animated_bones(created)
    print(f"[anim] {name}: path={created} length={length:.3f}s "
          f"frames={frames} bone_tracks={len(bones)}")

    if abs(length - duration) > 1e-3:
        raise RuntimeError(f"{name}: length {length} != {duration}")
    if len(bones) < len(ANIMATED_BONES):
        raise RuntimeError(f"{name}: only {len(bones)} bone tracks")
    return created


def main():
    ref = ReferencePose(SKELETON_PATH)
    missing = [bone for bone in ANIMATED_BONES if bone not in ref.local]
    if missing:
        raise RuntimeError(f"reference pose missing bones: {missing}")
    for name, duration, clip in CLIP_SPECS:
        build_animation(ref, name, duration, clip)
    print("[anim] all clips generated")


main()
```

- [ ] **Step 2: 在编辑器里运行生成器**

通过 `mcp__unreal-mcp__execute_python_code` 执行（`auto_save` 保持默认 true）：

```python
import unreal
exec(compile(open(r"F:/workspace/ue/StellarEra/Tools/Anim/GeneratePlanetCharacterAnims.py",
                  "r", encoding="utf-8").read(),
             "GeneratePlanetCharacterAnims.py", "exec"))
```

Expected stdout（四条，数值必须一致）：

```text
[anim] AS_Pomni_Idle: path=/Game/Anims/PlanetCharacter/AS_Pomni_Idle length=3.000s frames=91 bone_tracks=24
[anim] AS_Pomni_Walk: path=/Game/Anims/PlanetCharacter/AS_Pomni_Walk length=1.000s frames=31 bone_tracks=24
[anim] AS_Pomni_Run: path=/Game/Anims/PlanetCharacter/AS_Pomni_Run length=0.700s frames=22 bone_tracks=24
[anim] AS_Pomni_Air: path=/Game/Anims/PlanetCharacter/AS_Pomni_Air length=1.000s frames=31 bone_tracks=24
[anim] all clips generated
```

`frames` 是 `GetAnimationFrameCount` 的返回值，也就是**采样键数**，不是帧间隔数：
`NumFrames = round(Duration × 30)` 个间隔对应 `NumFrames + 1` 个键（0 .. NumFrames）。
所以 3.0 s → 91、1.0 s → 31、0.7 s → 22。

若 `CreateAnimSequence failed`：先确认 `AnimSequenceService` 可用（`unreal.AnimSequenceService.get_reference_pose(SKELETON_PATH)` 返回 117 项）。若 `bone_tracks` 小于 24，说明 `BoneTrackData` 的骨骼名与骨架不匹配，打印返回值核对。

- [ ] **Step 3: 读回验证时长、帧数与循环闭合**

```python
import unreal

PATHS = [
    "/Game/Anims/PlanetCharacter/AS_Pomni_Idle",
    "/Game/Anims/PlanetCharacter/AS_Pomni_Walk",
    "/Game/Anims/PlanetCharacter/AS_Pomni_Run",
    "/Game/Anims/PlanetCharacter/AS_Pomni_Air",
]
EXPECTED = {
    PATHS[0]: (3.0, 91),
    PATHS[1]: (1.0, 31),
    PATHS[2]: (0.7, 22),
    PATHS[3]: (1.0, 31),
}

for path in PATHS:
    length = unreal.AnimSequenceService.get_animation_length(path)
    frames = unreal.AnimSequenceService.get_animation_frame_count(path)
    bones = unreal.AnimSequenceService.get_animated_bones(path)
    expected_length, expected_frames = EXPECTED[path]
    assert abs(length - expected_length) < 1e-3, (path, length)
    assert frames == expected_frames, (path, frames)
    assert "Leg_L_4" in bones and "Arm_R_90" in bones, (path, bones)

    # 采样键是 0 .. frames-1；最后一键就是时间轴上的 Duration。
    first = {p.bone_name: p.transform for p in
             unreal.AnimSequenceService.get_pose_at_frame(path, 0)}
    last = {p.bone_name: p.transform for p in
            unreal.AnimSequenceService.get_pose_at_frame(path, frames - 1)}
    for bone in ["Hips_102", "Leg_L_4", "Arm_L_70"]:
        q0, q1 = first[bone].rotation, last[bone].rotation
        dot = abs(q0.x * q1.x + q0.y * q1.y + q0.z * q1.z + q0.w * q1.w)
        assert dot > 0.999, (path, bone, "rotation not closed", dot)
        delta = first[bone].translation - last[bone].translation
        dist = (delta.x ** 2 + delta.y ** 2 + delta.z ** 2) ** 0.5
        assert dist < 0.01, (path, bone, "translation not closed", dist)
    print(f"[verify] {path}: OK ({length:.3f}s, {frames} frames, {len(bones)} tracks)")
```

Expected: 四行 `[verify] ... OK`，无 AssertionError。

- [ ] **Step 4: 提交脚本与四个资产**

```bash
cd F:/workspace/ue/StellarEra
git add Tools/Anim/GeneratePlanetCharacterAnims.py \
        Content/Anims/PlanetCharacter/AS_Pomni_Idle.uasset \
        Content/Anims/PlanetCharacter/AS_Pomni_Walk.uasset \
        Content/Anims/PlanetCharacter/AS_Pomni_Run.uasset \
        Content/Anims/PlanetCharacter/AS_Pomni_Air.uasset
git status --short -- Tools/Anim/GeneratePlanetCharacterAnims.py Content/Anims/PlanetCharacter
git commit -m "feat(动画): 程序化生成角色 Idle/Walk/Run/Air 序列" \
  -m "Co-Authored-By: Claude Code <noreply@anthropic.com>" -- \
  Tools/Anim/GeneratePlanetCharacterAnims.py \
  Content/Anims/PlanetCharacter/AS_Pomni_Idle.uasset \
  Content/Anims/PlanetCharacter/AS_Pomni_Walk.uasset \
  Content/Anims/PlanetCharacter/AS_Pomni_Run.uasset \
  Content/Anims/PlanetCharacter/AS_Pomni_Air.uasset
```

Expected: `git status` 里四个 `.uasset` 显示为 `A`；`git commit` 只包含这五个路径。

---

### Task 2: C++ 动画实例与角色接线

**Files:**
- Create: `Source/StellarEraVehicle/Public/PlanetCharacterAnimInstance.h`
- Create: `Source/StellarEraVehicle/Private/PlanetCharacterAnimInstance.cpp`
- Create: `Source/StellarEraVehicle/Private/PlanetCharacterAnimTests.cpp`
- Modify: `Source/StellarEraVehicle/Public/PlanetCharacter.h`
- Modify: `Source/StellarEraVehicle/Private/PlanetCharacter.cpp`

**Interfaces:**
- Consumes: 无（Task 1 的资产只在测试里按路径加载）
- Produces:
  - `UPlanetCharacterAnimInstance`（`UAnimInstance` 子类），`BlueprintReadOnly` 属性 `GroundSpeed: float`、`bIsFalling: bool`
  - `APlanetCharacter::CharacterAnimClass`（`TSubclassOf<UAnimInstance>`，公开，EditAnywhere）
  - `APlanetCharacter::FaceAttachBone`（`FName`，默认 `Head_50`，公开）
  - 自动化测试 `StellarEra.Vehicle.AnimationAssets`

- [ ] **Step 1: 写自动化测试（先写，等资产与 BP 属性就位后转绿）**

创建 `Source/StellarEraVehicle/Private/PlanetCharacterAnimTests.cpp`：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.
//
// 角色运动动画的资产级验收测试。
//
// 它不跑 PIE：editor world 不执行 BeginPlay，无法断言运行期 Mesh 上的 AnimClass，
// 所以这里断言两件能静态读到的事实：
//   1. 四个 AS_ 序列存在、时长与轨道正确；
//   2. BP_PlanetCharacter 生成类 CDO 的 CharacterAnimClass 已指向 ABP_PlanetCharacter_C。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "UObject/UObjectGlobals.h"
#include "PlanetCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraPlanetCharacterAnimationAssetsTest,
	"StellarEra.Vehicle.AnimationAssets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraPlanetCharacterAnimationAssetsTest::RunTest(const FString& Parameters)
{
	const TCHAR* AnimPaths[] = {
		TEXT("/Game/Anims/PlanetCharacter/AS_Pomni_Idle.AS_Pomni_Idle"),
		TEXT("/Game/Anims/PlanetCharacter/AS_Pomni_Walk.AS_Pomni_Walk"),
		TEXT("/Game/Anims/PlanetCharacter/AS_Pomni_Run.AS_Pomni_Run"),
		TEXT("/Game/Anims/PlanetCharacter/AS_Pomni_Air.AS_Pomni_Air"),
	};

	for (const TCHAR* Path : AnimPaths)
	{
		UAnimSequence* Sequence = LoadObject<UAnimSequence>(nullptr, Path);
		TestNotNull(FString::Printf(TEXT("%s 可加载"), Path), Sequence);
		if (!Sequence)
		{
			continue;
		}

		TestTrue(FString::Printf(TEXT("%s 时长 > 0"), Path), Sequence->GetPlayLength() > 0.f);

		const IAnimationDataModel* Model = Sequence->GetDataModel();
		TestNotNull(FString::Printf(TEXT("%s 有数据模型"), Path), Model);
		if (Model)
		{
			TestTrue(FString::Printf(TEXT("%s 含 Leg_L_4 轨道"), Path),
				Model->IsValidBoneTrackName(TEXT("Leg_L_4")));
			TestTrue(FString::Printf(TEXT("%s 含 Arm_R_90 轨道"), Path),
				Model->IsValidBoneTrackName(TEXT("Arm_R_90")));
		}
	}

	UClass* BlueprintClass = LoadClass<APlanetCharacter>(
		nullptr, TEXT("/Game/Code/BP_PlanetCharacter.BP_PlanetCharacter_C"));
	TestNotNull(TEXT("BP_PlanetCharacter 可加载"), BlueprintClass);
	if (BlueprintClass)
	{
		const APlanetCharacter* CDO =
			Cast<APlanetCharacter>(BlueprintClass->GetDefaultObject());
		TestNotNull(TEXT("BP_PlanetCharacter CDO"), CDO);
		if (CDO)
		{
			TestNotNull(TEXT("CharacterAnimClass 已配置"), CDO->CharacterAnimClass.Get());
			if (CDO->CharacterAnimClass)
			{
				TestEqual(TEXT("CharacterAnimClass 指向 ABP_PlanetCharacter"),
					CDO->CharacterAnimClass->GetName(),
					FString(TEXT("ABP_PlanetCharacter_C")));
			}
		}
	}

	return true;
}

#endif
```

- [ ] **Step 2: 新建动画实例头文件**

创建 `Source/StellarEraVehicle/Public/PlanetCharacterAnimInstance.h`：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.
// 球面角色的运动状态动画实例。

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "PlanetCharacterAnimInstance.generated.h"

/**
 * 把 APlanetCharacter 的移动状态翻译成状态机可读的两个属性。
 *
 * 速度必须投影到重力平面：球面上「前」是切向的，直接用 Velocity.Size()
 * 会把沿半径的跳跃/坠落速度也算成地面速度，Walk/Run 切换会在半空抽风。
 */
UCLASS()
class STELLARERAVEHICLE_API UPlanetCharacterAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

	/** 速度在重力平面上的投影大小（厘米/秒）。 */
	UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
	float GroundSpeed = 0.f;

	/** 角色是否在空中。 */
	UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
	bool bIsFalling = false;
};
```

- [ ] **Step 3: 新建动画实例实现**

创建 `Source/StellarEraVehicle/Private/PlanetCharacterAnimInstance.cpp`：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetCharacterAnimInstance.h"

#include "GameFramework/CharacterMovementComponent.h"
#include "PlanetCharacter.h"

void UPlanetCharacterAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	const APlanetCharacter* Character = Cast<APlanetCharacter>(TryGetPawnOwner());
	if (!Character)
	{
		GroundSpeed = 0.f;
		bIsFalling = false;
		return;
	}

	const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	const FVector Velocity = Character->GetVelocity();
	const FVector GravityDirection =
		Movement ? Movement->GetGravityDirection() : FVector::DownVector;

	GroundSpeed = GravityDirection.IsNearlyZero()
		? Velocity.Size()
		: FVector::VectorPlaneProject(Velocity, GravityDirection).Size();
	bIsFalling = Movement && Movement->IsFalling();
}
```

- [ ] **Step 4: 给 `PlanetCharacter.h` 加前向声明与属性**

在 `Source/StellarEraVehicle/Public/PlanetCharacter.h` 第 10–15 行的前向声明块里加一行（放在 `class UCameraComponent;` 之后）：

```cpp
class UAnimInstance;
```

在第 177 行 `float MeshScale = 0.7f;` 之后插入（注意 `public:` 是必须的：测试文件不是子类，读不到 protected 成员）：

```cpp
public:
	/**
	 * 运动动画蓝图类。留空则角色没有动画（保持参考姿势）。
	 *
	 * 走 C++ 属性而不是在蓝图里直接设 Mesh 的 Anim Class：CharacterMesh0 是
	 * ACharacter 的原生继承组件，不在蓝图的 SCS 节点里，蓝图侧的组件工具找不到它。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character|Animation")
	TSubclassOf<UAnimInstance> CharacterAnimClass;

	/** 面部附属网格挂到身体网格的哪根骨骼上。留空则不改挂。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character|Animation")
	FName FaceAttachBone = TEXT("Head_50");

private:
```

（原有的 `private:` 第 179 行会被这一步替换掉，不要重复保留。）

在第 194 行 `void ApplyCharacterMesh();` 之后插入声明：

```cpp
	/**
	 * 把挂在身体网格下、但使用不同骨架的附属网格（面部）改挂到 FaceAttachBone。
	 *
	 * 面部网格 Object_10 / Object_21 的骨架分别是 Face_30 / GLTF_created_0，
	 * 与身体的 Object_14_Skeleton 不同。它们原本直接挂在 CharacterMesh0 下，
	 * 身体动画不会带着它们走 —— 头一转，脸还留在原地。
	 */
	void AttachFaceMeshesToHead();
```

- [ ] **Step 5: 改 `PlanetCharacter.cpp` 的 include 与 `ApplyCharacterMesh`**

在 `Source/StellarEraVehicle/Private/PlanetCharacter.cpp` 第 13 行
（`#include "GameFramework/SpringArmComponent.h"`）之后加：

```cpp
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
```

`ApplyCharacterMesh()` 末尾（第 153 行 `MeshComponent->SetRelativeScale3D(FVector(MeshScale));` 之后）
应该是下面这样。前四行**已经存在，不要重复添加**，只在它们后面追加最后两行：

```cpp
	MeshComponent->SetSkeletalMesh(MeshToUse);
	MeshComponent->SetRelativeLocation(MeshRelativeLocation);
	MeshComponent->SetRelativeRotation(MeshRelativeRotation);
	MeshComponent->SetRelativeScale3D(FVector(MeshScale));

	// 动画类也在这里套用：它与上面四个属性一样是 EditAnywhere，构造期读不到蓝图值。
	// SetAnimClass 在 5.5 起已弃用，用 SetAnimInstanceClass。
	MeshComponent->SetAnimInstanceClass(CharacterAnimClass.Get());
```

- [ ] **Step 6: 加 `AttachFaceMeshesToHead()` 实现，并在 `BeginPlay` 调用**

在 `ApplyCharacterMesh()` 结束（第 154 行的 `}`）之后插入：

```cpp
void APlanetCharacter::AttachFaceMeshesToHead()
{
	USkeletalMeshComponent* BodyMesh = GetMesh();
	const USkeletalMesh* BodyAsset =
		BodyMesh ? BodyMesh->GetSkeletalMeshAsset() : nullptr;
	const USkeleton* BodySkeleton = BodyAsset ? BodyAsset->GetSkeleton() : nullptr;
	if (!BodySkeleton || FaceAttachBone.IsNone())
	{
		return;
	}

	TArray<USkeletalMeshComponent*> MeshComponents;
	GetComponents<USkeletalMeshComponent>(MeshComponents);
	for (USkeletalMeshComponent* MeshComponent : MeshComponents)
	{
		// 只处理直接挂在身体网格下、且骨架不同的附属网格（面部）。
		if (!MeshComponent || MeshComponent == BodyMesh)
		{
			continue;
		}
		if (MeshComponent->GetAttachParent() != BodyMesh)
		{
			continue;
		}

		const USkeletalMesh* OtherAsset = MeshComponent->GetSkeletalMeshAsset();
		const USkeleton* OtherSkeleton =
			OtherAsset ? OtherAsset->GetSkeleton() : nullptr;
		if (OtherSkeleton == BodySkeleton)
		{
			continue;
		}

		// KeepWorldTransform：改挂瞬间脸不能跳位置。
		MeshComponent->AttachToComponent(
			BodyMesh, FAttachmentTransformRules::KeepWorldTransform, FaceAttachBone);
	}
}
```

在 `BeginPlay()` 里第 175 行 `ApplyCharacterMesh();` 之后加一行：

```cpp
	// 面部网格用不同骨架，必须在身体网格有模型之后才能挂到 Head_50。
	AttachFaceMeshesToHead();
```

- [ ] **Step 7: 补 UTF-8 BOM**

```bash
python - <<'PY'
from pathlib import Path
files = [
    r"F:/workspace/ue/StellarEra/Source/StellarEraVehicle/Public/PlanetCharacterAnimInstance.h",
    r"F:/workspace/ue/StellarEra/Source/StellarEraVehicle/Private/PlanetCharacterAnimInstance.cpp",
    r"F:/workspace/ue/StellarEra/Source/StellarEraVehicle/Private/PlanetCharacterAnimTests.cpp",
]
for name in files:
    p = Path(name)
    data = p.read_bytes()
    if not data.startswith(b"\xef\xbb\xbf"):
        p.write_bytes(b"\xef\xbb\xbf" + data)
        print("BOM added:", name)
PY
```

Expected: 三行 `BOM added:`。然后确认：

```bash
for f in Source/StellarEraVehicle/Public/PlanetCharacterAnimInstance.h \
         Source/StellarEraVehicle/Private/PlanetCharacterAnimInstance.cpp \
         Source/StellarEraVehicle/Private/PlanetCharacterAnimTests.cpp; do
  printf '%s: ' "$f"; head -c 3 "$f" | od -An -tx1
done
```

Expected: 三行都是 ` ef bb bf`。

- [ ] **Step 8: 关闭编辑器并构建**

先关闭编辑器。然后：

```bash
cd F:/workspace/ue
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" \
  "D:/Application/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" \
  StellarEraEditor Win64 Development \
  -Project="F:/workspace/ue/StellarEra/StellarEra.uproject" -WaitMutex -FromMsBuild
```

Expected: 退出码 0，输出含 `Build succeeded`（或 `Result: Succeeded`）。

- 若报 `Unable to write file ... .dll` / 文件被占用：编辑器没关干净，关掉后重跑。
- 若 `Result: Failed`：看日志里第一条 `error C...`；不要把它当成「只是链接失败」继续往下走。
- `SetAnimClass` 弃用警告不算失败；若把它写成 `SetAnimClass` 会得到 C4996，改回 `SetAnimInstanceClass`。

- [ ] **Step 9: 重开编辑器并确认类已进反射表**

重开编辑器：

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject"
```

等 MCP 端点可用（`http://127.0.0.1:8000/mcp`），然后：

```python
import unreal
print("module has class:", hasattr(unreal, "PlanetCharacterAnimInstance"))
print("class:", unreal.PlanetCharacterAnimInstance)
```

Expected: `module has class: True`，并打印出 `<class 'PlanetCharacterAnimInstance'>` 之类的类对象。
若为 `False`，说明 DLL 没重新编译或编辑器没有真正重开 —— 回 Step 8 查构建日志，别继续往下做。

- [ ] **Step 10: 提交 C++ 源码**

```bash
cd F:/workspace/ue/StellarEra
git add Source/StellarEraVehicle/Public/PlanetCharacterAnimInstance.h \
        Source/StellarEraVehicle/Private/PlanetCharacterAnimInstance.cpp \
        Source/StellarEraVehicle/Private/PlanetCharacterAnimTests.cpp \
        Source/StellarEraVehicle/Public/PlanetCharacter.h \
        Source/StellarEraVehicle/Private/PlanetCharacter.cpp
git commit -m "feat(角色): 新增运动动画实例与 CharacterAnimClass 接线" \
  -m "Co-Authored-By: Claude Code <noreply@anthropic.com>" -- \
  Source/StellarEraVehicle/Public/PlanetCharacterAnimInstance.h \
  Source/StellarEraVehicle/Private/PlanetCharacterAnimInstance.cpp \
  Source/StellarEraVehicle/Private/PlanetCharacterAnimTests.cpp \
  Source/StellarEraVehicle/Public/PlanetCharacter.h \
  Source/StellarEraVehicle/Private/PlanetCharacter.cpp
```

Expected: 提交成功，5 个文件。

---

### Task 3: 创建 AnimBlueprint 与 Locomotion 状态机

**Files:**
- Create: `Tools/Anim/BuildPlanetCharacterAnimBP.py`
- Generate: `Content/Code/ABP_PlanetCharacter.uasset`

**Interfaces:**
- Consumes: `UPlanetCharacterAnimInstance`（Task 2）、四个 `AS_Pomni_*`（Task 1）
- Produces: `ABP_PlanetCharacter`，生成类 `/Game/Code/ABP_PlanetCharacter.ABP_PlanetCharacter_C`；状态机 `Locomotion`，入口 `Idle`

- [ ] **Step 1: 写 ABP 构建脚本**

创建 `Tools/Anim/BuildPlanetCharacterAnimBP.py`（UTF-8）：

```python
# -*- coding: utf-8 -*-
"""创建 /Game/Code/ABP_PlanetCharacter，并搭 Locomotion 状态机。

在编辑器内执行：

    import unreal
    exec(compile(open(r"F:/workspace/ue/StellarEra/Tools/Anim/BuildPlanetCharacterAnimBP.py",
                      "r", encoding="utf-8").read(),
                 "BuildPlanetCharacterAnimBP.py", "exec"))

前置条件：
  - Task 2 的 UPlanetCharacterAnimInstance 已编译并重开过编辑器；
  - /Game/Anims/PlanetCharacter/ 下四个 AS_ 资产存在。
重复运行会删除并重建 ABP。
"""

import json
import unreal

SKELETON_PATH = (
    "/Game/Fab/Blender_3D｜Pomni_3D_Model/pomni_3d_model/SkeletalMeshes/Object_14_Skeleton"
)
MESH_PATH = (
    "/Game/Fab/Blender_3D｜Pomni_3D_Model/pomni_3d_model/SkeletalMeshes/Object_14"
)
ABP_PATH = "/Game/Code/ABP_PlanetCharacter"
ANIM_DIR = "/Game/Anims/PlanetCharacter"

STATES = [
    {"name": "Idle", "animation": f"{ANIM_DIR}/AS_Pomni_Idle", "loop": True, "pos": [0.0, 0.0]},
    {"name": "Walk", "animation": f"{ANIM_DIR}/AS_Pomni_Walk", "loop": True, "pos": [320.0, 0.0]},
    {"name": "Run", "animation": f"{ANIM_DIR}/AS_Pomni_Run", "loop": True, "pos": [640.0, 0.0]},
    {"name": "Air", "animation": f"{ANIM_DIR}/AS_Pomni_Air", "loop": True, "pos": [320.0, 220.0]},
]

# priority 小者优先：离地条件必须压过地面速度条件，否则跳跃瞬间会先切 Walk。
TRANSITIONS = [
    {"from": "Idle", "to": "Walk", "blend": 0.2, "priority": 1,
     "rule": {"type": "comparison", "variable": "GroundSpeed", "op": "greater", "value": 10.0}},
    {"from": "Walk", "to": "Idle", "blend": 0.2, "priority": 1,
     "rule": {"type": "comparison", "variable": "GroundSpeed", "op": "less_equal", "value": 10.0}},
    {"from": "Walk", "to": "Run", "blend": 0.2, "priority": 1,
     "rule": {"type": "comparison", "variable": "GroundSpeed", "op": "greater", "value": 900.0}},
    {"from": "Run", "to": "Walk", "blend": 0.2, "priority": 1,
     "rule": {"type": "comparison", "variable": "GroundSpeed", "op": "less_equal", "value": 900.0}},
    {"from": "Idle", "to": "Air", "blend": 0.1, "priority": 0,
     "rule": {"type": "bool", "variable": "bIsFalling"}},
    {"from": "Walk", "to": "Air", "blend": 0.1, "priority": 0,
     "rule": {"type": "bool", "variable": "bIsFalling"}},
    {"from": "Run", "to": "Air", "blend": 0.1, "priority": 0,
     "rule": {"type": "bool", "variable": "bIsFalling"}},
    {"from": "Air", "to": "Idle", "blend": 0.1, "priority": 0,
     "rule": {"type": "bool", "variable": "bIsFalling", "invert": True}},
]

SPEC = {"states": STATES, "transitions": TRANSITIONS, "entry": "Idle"}


def main():
    if unreal.EditorAssetLibrary.does_asset_exist(ABP_PATH):
        unreal.EditorAssetLibrary.delete_asset(ABP_PATH)

    skeleton = unreal.EditorAssetLibrary.load_asset(SKELETON_PATH)
    mesh = unreal.EditorAssetLibrary.load_asset(MESH_PATH)
    if not skeleton or not mesh:
        raise RuntimeError("skeleton or preview mesh not found")

    factory = unreal.AnimBlueprintFactory()
    factory.set_editor_property("parent_class", unreal.PlanetCharacterAnimInstance)
    factory.set_editor_property("target_skeleton", skeleton)
    factory.set_editor_property("preview_skeletal_mesh", mesh)

    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    abp = asset_tools.create_asset(
        "ABP_PlanetCharacter", "/Game/Code", unreal.AnimBlueprint, factory)
    if not abp:
        raise RuntimeError("failed to create AnimBlueprint")
    unreal.EditorAssetLibrary.save_asset(ABP_PATH)

    # 必须先拿状态机节点的 GUID，build_state_machine 只负责填内容，不接 Output Pose。
    machine_id = unreal.AnimGraphService.add_state_machine(ABP_PATH, "Locomotion", 0.0, 0.0)
    if not machine_id:
        raise RuntimeError("failed to add state machine node")

    report = json.loads(unreal.AnimGraphService.build_state_machine(
        ABP_PATH, "Locomotion", json.dumps(SPEC), 0.0, 0.0))
    print("[abp] build report:", report)
    if not report.get("success"):
        raise RuntimeError(f"BuildStateMachine failed: {report}")

    if not unreal.AnimGraphService.connect_to_output_pose(
            ABP_PATH, "AnimGraph", machine_id, "Pose"):
        raise RuntimeError("failed to connect state machine to Output Pose")

    unreal.BlueprintEditorLibrary.compile_blueprint(abp)
    unreal.EditorAssetLibrary.save_asset(ABP_PATH)

    result = unreal.AnimGraphService.validate_state_machine(ABP_PATH, "Locomotion")
    errors = list(result.get_editor_property("Errors"))
    warnings = list(result.get_editor_property("Warnings"))
    print("[abp] valid:", result.get_editor_property("bIsValid"))
    print("[abp] states:", result.get_editor_property("StateCount"))
    print("[abp] transitions:", result.get_editor_property("TransitionCount"))
    print("[abp] errors:", errors)
    print("[abp] warnings:", warnings)
    if not result.get_editor_property("bIsValid"):
        raise RuntimeError(f"state machine validation failed: {errors}")


main()
```

- [ ] **Step 2: 运行并检查报告**

```python
import unreal
exec(compile(open(r"F:/workspace/ue/StellarEra/Tools/Anim/BuildPlanetCharacterAnimBP.py",
                  "r", encoding="utf-8").read(),
             "BuildPlanetCharacterAnimBP.py", "exec"))
```

Expected:

```text
[abp] build report: {'success': True, 'machine': 'Locomotion', 'states_created': 4, 'transitions_created': 8, 'errors': []}
[abp] valid: True
[abp] states: 4
[abp] transitions: 8
[abp] errors: []
[abp] warnings: []
```

若 `BuildStateMachine` 报 `variable 'GroundSpeed' not found`：说明父类属性没有被识别（VibeUE 的已知风险）。
退路（照做，不改设计）：在 ABP 里加两个同名 Blueprint 变量 `GroundSpeed` / `bIsFalling`，
在 Event Graph 的 `Blueprint Update Animation` 里从父类属性拷贝一份。建变量用
`unreal.BlueprintService.add_member_variable(ABP_PATH, "GroundSpeed", "float")` 与
`...("bIsFalling", "bool")`（引擎自带的 `BlueprintTools.add_variable` 也行，两者都只支持基础类型，
这里正好够用）；连节点后重跑 Step 3 的读回验证。

- [ ] **Step 3: 读回状态、过渡与优先级**

```python
import unreal

ABP = "/Game/Code/ABP_PlanetCharacter"
print("parent class:", unreal.AnimGraphService.get_parent_class(ABP))
print("skeleton:", unreal.AnimGraphService.get_skeleton(ABP))

states = unreal.AnimGraphService.list_states_in_machine(ABP, "Locomotion")
print("states:", [(s.get_editor_property("StateName"),
                   s.get_editor_property("StateType")) for s in states])

for source in ["Idle", "Walk", "Run", "Air"]:
    for t in unreal.AnimGraphService.get_state_transitions(ABP, "Locomotion", source):
        print("transition:", t.get_editor_property("SourceState"), "->",
              t.get_editor_property("DestState"),
              "prio", t.get_editor_property("Priority"),
              "blend", t.get_editor_property("BlendDuration"),
              "|", t.get_editor_property("RuleSummary"),
              "has_rule", t.get_editor_property("bHasRule"))

result = unreal.AnimGraphService.validate_state_machine(ABP, "Locomotion")
print("valid:", result.get_editor_property("bIsValid"))
print("errors:", list(result.get_editor_property("Errors")))

machines = unreal.AnimGraphService.list_state_machines(ABP)
print("machines:", [(m.get_editor_property("MachineName"),
                     m.get_editor_property("StateCount")) for m in machines])
```

Expected:

- `parent class: PlanetCharacterAnimInstance`
- 状态列表恰好是 `Idle / Walk / Run / Air` 四个 `State`（`ListStatesInMachine` 会跳过
  `UAnimStateEntryNode`，所以不会出现 `Entry`；`StateType` 为 `AnimStateNode`）
- 8 条 transition，`SourceState/DestState` 与设计表一一对应
- 四条地面 transition `Priority == 1`，四条 Air transition `Priority == 0`
- `blend`：地面 0.2，Air 0.1
- 每条 `has_rule == True`，`RuleSummary` 形如 `GroundSpeed > 10.00` / `bIsFalling == true`
- `valid: True`，`errors: []`，`machines: [('Locomotion', 4)]`

- [ ] **Step 4: 提交脚本与 ABP**

```bash
cd F:/workspace/ue/StellarEra
git add Tools/Anim/BuildPlanetCharacterAnimBP.py Content/Code/ABP_PlanetCharacter.uasset
git commit -m "feat(动画): 新增 ABP_PlanetCharacter 与 Locomotion 状态机" \
  -m "Co-Authored-By: Claude Code <noreply@anthropic.com>" -- \
  Tools/Anim/BuildPlanetCharacterAnimBP.py \
  Content/Code/ABP_PlanetCharacter.uasset
```

Expected: 提交 2 个文件。

---

### Task 4: 把 ABP 接到 BP_PlanetCharacter 并跑自动化

**Files:**
- Modify: `Content/Code/BP_PlanetCharacter.uasset`（只改 CDO 的 `CharacterAnimClass`）

**Interfaces:**
- Consumes: `APlanetCharacter::CharacterAnimClass`（Task 2）、`ABP_PlanetCharacter_C`（Task 3）
- Produces: `BP_PlanetCharacter` CDO 上 `CharacterAnimClass = ABP_PlanetCharacter_C`

- [ ] **Step 1: 设置并读回 CDO 属性**

```python
import unreal

ok = unreal.BlueprintService.set_property(
    "/Game/Code/BP_PlanetCharacter",
    "CharacterAnimClass",
    "/Game/Code/ABP_PlanetCharacter.ABP_PlanetCharacter_C")
print("set_property:", ok)

value = unreal.BlueprintService.get_property(
    "/Game/Code/BP_PlanetCharacter", "CharacterAnimClass")
print("readback:", value)
assert ok, "set_property failed"
assert "ABP_PlanetCharacter" in str(value), value
```

Expected: `set_property: True`，`readback` 含 `ABP_PlanetCharacter_C`。
`BlueprintService.set_property` 内部会加载、ImportText、标记修改、保存、重新加载并读回校验；
若它返回 False，先看编辑器日志 `LogBlueprint` / `LogPython` 里的 ImportText 原因。

- [ ] **Step 2: 跑自动化测试**

编辑器可以保持打开（`UnrealEditor-Cmd` 是独立进程）：

```bash
LOG="$TEMP/stellarera_anim_tests.log"
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout -abslog="$LOG"
echo "exit=$?"
grep -E "Test (Completed|Failed)|StellarEra\.Vehicle\.AnimationAssets|Automation Test" "$LOG" | tail -40
```

Expected:

- `StellarEra.Vehicle.AnimationAssets` 出现在日志中且 `Success`
- 整个 `StellarEra` 测试集全绿（包含 `StellarEra.Core.*`、`StellarEra.Trajectory.*`）
- `exit=0`

若测试失败，按断言消息定位：

| 断言 | 原因 |
|---|---|
| `... 可加载` | 资产路径写错，或 Task 1 没保存成功 |
| `... 含 Leg_L_4 轨道` | 生成器的 `BONE_PARENT` / 骨骼名与骨架不符 |
| `BP_PlanetCharacter 可加载` | BP 路径写错或资产损坏（先查 LFS 指针是否 smudge） |
| `CharacterAnimClass 已配置` | Task 4 Step 1 没写进 CDO |
| `CharacterAnimClass 指向 ABP...` | 指向了别的 ABP 或路径带 `_C` 写错 |

- [ ] **Step 3: 提交 BP 资产**

```bash
cd F:/workspace/ue/StellarEra
git add Content/Code/BP_PlanetCharacter.uasset
git commit -m "feat(动画): BP_PlanetCharacter 使用 ABP_PlanetCharacter" \
  -m "Co-Authored-By: Claude Code <noreply@anthropic.com>" -- \
  Content/Code/BP_PlanetCharacter.uasset
```

Expected: 提交 1 个文件。

---

### Task 5: 视觉验证（编辑器预览 + PIE）

**Files:**
- No repository files. 产物写到 `Saved/Screenshots/Anim/`（不入库）。

**Interfaces:**
- Consumes: Task 1 的四个序列、Task 3 的 ABP、Task 4 的 BP 接线
- Produces: 8 张截图 + 一份人工观察结论

- [ ] **Step 1: 让编辑器前台**

把编辑器窗口切到前台再开始。后台时整个进程被限到 3 FPS，动画预览和体素流送都会失真。

- [ ] **Step 2: 捕获四个片段的代表性姿势**

```python
import os
import unreal

OUT = r"F:/workspace/ue/StellarEra/Saved/Screenshots/Anim"
os.makedirs(OUT, exist_ok=True)

CAPTURES = [
    ("/Game/Anims/PlanetCharacter/AS_Pomni_Idle", 0.5, "idle_0.5s.png"),
    ("/Game/Anims/PlanetCharacter/AS_Pomni_Walk", 0.25, "walk_0.25s.png"),
    ("/Game/Anims/PlanetCharacter/AS_Pomni_Run", 0.175, "run_0.175s.png"),
    ("/Game/Anims/PlanetCharacter/AS_Pomni_Air", 0.5, "air_0.5s.png"),
]

for path, time, filename in CAPTURES:
    ret = unreal.AnimSequenceService.capture_animation_pose(
        path, time, os.path.join(OUT, filename), "three_quarter", 512, 512)
    # C++ 声明是 bool CaptureAnimationPose(..., FAnimationPoseCaptureResult& OutResult)；
    # Unreal Python 通常返回 (bool, Struct)，VibeUE 的 API 文档却写成只返回 Struct。
    # 两种绑定都接住，避免脚本因绑定差异直接崩。
    if isinstance(ret, tuple):
        ok, result = ret
    else:
        result = ret
        ok = result.get_editor_property("bSuccess")
    image = result.get_editor_property("ImagePath")
    print(f"[capture] {filename}: success={bool(ok)} path={image}")
    if not ok:
        raise RuntimeError(result.get_editor_property("ErrorMessage"))
```

然后用 Read 工具逐张查看 `Saved/Screenshots/Anim/*.png`，核对：

- 手臂已从 T-pose 放下（不是张开的）
- 膝盖向前弯，不是反关节
- Walk / Run 左右腿前后分开，Run 躯干前倾
- Air 双腿收起、手臂抬起

任何一条不满足，回到 `GeneratePlanetCharacterAnims.py` 调对应 clip 的角度，重跑 Task 1 的 Step 2/3，再回本步截图。

- [ ] **Step 3: 在 `UniverseMap` 里跑 PIE 并驱动移动状态**

先确认当前关卡是 `UniverseMap`：

```python
import unreal
print(unreal.LevelEditorSubsystem().get_current_level().get_path_name())
```

不是的话先 `unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level("/Game/Maps/UniverseMap")`。

启动 PIE，并注册一个带时间表的 slate tick 回调，按顺序驱动站立 → 行走 → 冲刺 → 跳跃：

```python
import time
import unreal

subsys = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
subsys.editor_request_begin_play()

state = {"handle": None, "start": 0.0, "sprinting": False, "jumped": False}


def drive(delta_seconds):
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    if not world:
        return

    characters = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.PlanetCharacter)
    if not characters:
        return
    character = characters[0]
    movement = character.character_movement

    # 第一帧能拿到角色时才开始计时，别让 BeginPlay/体素流送吃掉时间表。
    if state["start"] == 0.0:
        state["start"] = time.monotonic()
        print("[pie] character found, timeline started, mode:", movement.movement_mode)

    # Move() / SetSprinting() 是普通 C++ 方法，不是 UFUNCTION，Python 调不到。
    # 用 BlueprintCallable 的等价接口驱动：
    #   AddMovementInput  = 摇杆方向（CMC 会把世界方向投影到脚下平面）
    #   MaxWalkSpeed      = SetSprinting 唯一做的事
    #   Jump              = 跳
    direction = character.get_actor_forward_vector()
    elapsed = time.monotonic() - state["start"]

    if elapsed < 2.0:
        return  # 站立：不喂输入

    if elapsed < 6.0:
        character.add_movement_input(direction, 1.0, True)   # 行走 600 cm/s
        return

    if elapsed < 10.0:
        if not state["sprinting"]:
            movement.max_walk_speed = 1200.0
            state["sprinting"] = True
            print("[pie] sprint on")
        character.add_movement_input(direction, 1.0, True)   # 冲刺 1200 cm/s
        return

    if not state["jumped"]:
        movement.max_walk_speed = 600.0
        character.jump()
        state["jumped"] = True
        print("[pie] jump")


def start():
    state["handle"] = unreal.register_slate_post_tick_callback(drive)
    print("[pie] drive registered")


start()
```

Expected: `[pie] drive registered`，随后 `[pie] character found, timeline started, mode: MovementMode.MOVE_WALKING`；
PIE 视口里角色先站立、约 2 s 后开始走、约 6 s 后加速跑、约 10 s 起跳。

若 `movement_mode` 不是 `MOVE_WALKING`（比如角色开局就在船上，`MOVE_NONE`），
先结束 PIE、在关卡里把角色摆到地表再重来；登船状态下输入会被 CMC 丢弃。

- [ ] **Step 4: 在四个时间窗内截图**

用 `mcp__unreal-mcp__capture_image`（`source="game"`）依次截图，文件名分别存为：

| 触发时机 | 期望状态 | 文件 |
|---|---|---|
| 回调开始后约 1 s | Idle | `Saved/Screenshots/Anim/pie_idle.png` |
| 开始后约 4 s | Walk | `Saved/Screenshots/Anim/pie_walk.png` |
| 开始后约 8 s | Run | `Saved/Screenshots/Anim/pie_run.png` |
| 开始后约 10.5 s | Air | `Saved/Screenshots/Anim/pie_air.png` |

观察并记录：

- Idle：手臂下垂、随呼吸轻微起伏
- Walk：双腿交替、手臂反向摆
- Run：身体前倾、步幅明显大于 Walk
- Air：双腿收起，不在跑 Walk 循环

- [ ] **Step 5: 结束 PIE 并清理回调**

```python
import unreal

# 不直接引用 state：即使上一条 execute_python_code 的命名空间没保留，
# 这一步也必须能安全执行，否则回调会一直挂在编辑器里。
handle = globals().get("state", {}).get("handle")
if handle is not None:
    unreal.unregister_slate_post_tick_callback(handle)
    if "state" in globals():
        state["handle"] = None

unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
print("[pie] stopped")
```

Expected: `[pie] stopped`；PIE 关闭，回调已注销。

- [ ] **Step 6: 记录结论**

在回复里给出：

- 四张片段截图与四张 PIE 截图的实际观察结果（逐条对应 Step 2 / Step 4 的核对项）
- 任何未通过项与对应参数调整
- 运行过的验证命令（构建、自动化、截图）

本任务不产生提交（截图在 `Saved/` 下，不入库）。

---

## Self-Review

**Spec 覆盖**

| Spec 小节 | 对应任务 |
|---|---|
| §3 资产事实（骨骼名、轴向、速度、Mesh 变换） | Task 1 的 `BONE_PARENT` / `ANIMATED_BONES` / 角度符号；Task 2 不改 Mesh 变换 |
| §4 四个资产、时长、循环、只打 24 根骨骼 | Task 1 Step 1（`CLIP_SPECS` / `ANIMATED_BONES`）、Step 3（循环闭合） |
| §5 关键帧公式 | Task 1 `make_keyframe()`（`R' = S_parent⁻¹ · D · S_bone`、Hips 位移） |
| §6 AnimBP、状态机、阈值、crossfade、入口 | Task 3 `STATES` / `TRANSITIONS` / `SPEC["entry"]` |
| §7 `UPlanetCharacterAnimInstance`、`CharacterAnimClass`、面部改挂 | Task 2 Step 2–6 |
| §8 生成器与可复现性 | Task 1 Step 1–4 |
| §9 验证（读回、预览、PIE、自动化） | Task 1 Step 3、Task 5、Task 4 Step 2 |
| §10 已知取舍（脚滑、VibeUE 风险、ABP 脚本风险、动画质量） | 风险已在 Task 3 Step 2 给出退路；其余为已知取舍，不改实现 |

**占位符扫描**

- 无 `TBD` / `TODO` / “类似 Task N”。
- 每个代码步骤都有完整代码；每条验证命令都有期望输出。

**类型与命名一致性**

- `CharacterAnimClass` 在 Task 2 定义、Task 4 设置、测试读取，名称一致。
- `FaceAttachBone` 默认 `Head_50`，与 spec §7 一致。
- `GroundSpeed` / `bIsFalling` 在 AnimInstance、测试、ABP spec JSON、读回脚本中拼写一致。
- 资产路径 `/Game/Anims/PlanetCharacter/AS_Pomni_*` 在生成器、ABP 脚本、测试中一致。
- `ABP_PlanetCharacter_C` 在 ABP 脚本、BP 设置、测试中一致。

---

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-10-19-planet-character-animation.md`. Two execution options:

**1. Subagent-Driven (recommended)** — 每个 Task 派一个全新 subagent，任务间复审，迭代快。

**2. Inline Execution** — 在当前会话里按 `superpowers:executing-plans` 批量执行，带检查点。

Which approach?
