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
