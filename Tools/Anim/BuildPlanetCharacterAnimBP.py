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
