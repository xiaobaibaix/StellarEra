"""创建 / 修补矿物剖面所需的资产。幂等：重复运行只打 already patched。

用命令头执行（编辑器必须关闭）：
  UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script="<abs path>" \
    -EnablePlugins=PythonScriptPlugin -unattended -nopause -nosplash -stdout -abslog=<log>

设计依据见 docs/superpowers/plans/2026-10-24-mineral-cutaway-editor.md 的裁定 1 / 2。
"""

import unreal

AT = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary

MAT_DIR = "/Game/Materials"
ORIGIN_MPC_PATH = MAT_DIR + "/MPC_CutawayOrigin"
NORMAL_MPC_PATH = MAT_DIR + "/MPC_CutawayNormal"
SECTION_MAT_PATH = MAT_DIR + "/M_PlanetSection"
PLANET_MAT_PATH = MAT_DIR + "/M_PlanetVertexColor"
DEFAULT_TEXTURE_PATH = "/Engine/EngineResources/DefaultTexture"

# 本次创建的节点都带这个 desc 前缀，用于幂等判定与排错。
TAG = "CUTAWAY_"

ERRORS = []


def log(message):
    unreal.log("[Cutaway] " + message)


def fail(message):
    ERRORS.append(message)
    unreal.log_error("[Cutaway] " + message)


def load(path):
    if not EAL.does_asset_exist(path):
        return None
    return EAL.load_asset(path)


def save(asset, label):
    if not asset:
        fail("save: 资产为空 " + label)
        return False
    if not EAL.save_loaded_asset(asset):
        fail("save 失败 " + label)
        return False
    log("saved: " + label)
    return True


# ---------------------------------------------------------------- MPC

def ensure_mpc(path, parameter_name, default_value):
    if EAL.does_asset_exist(path):
        mpc = EAL.load_asset(path)
        params = mpc.get_editor_property("vector_parameters")
        if len(params) != 1 or str(params[0].get_editor_property("parameter_name")) != parameter_name:
            fail("MPC 结构不符预期: %s（应恰有一个 %s）" % (path, parameter_name))
        else:
            log("exists: " + path)
        return mpc

    package_path, asset_name = path.rsplit("/", 1)
    factory = unreal.MaterialParameterCollectionFactoryNew()
    mpc = AT.create_asset(asset_name, package_path, unreal.MaterialParameterCollection, factory)
    if not mpc:
        fail("create_asset 失败 " + path)
        return None

    parameter = unreal.CollectionVectorParameter()
    parameter.set_editor_property("parameter_name", unreal.Name(parameter_name))
    parameter.set_editor_property("default_value", default_value)
    mpc.set_editor_property("vector_parameters", [parameter])
    save(mpc, path)

    params = mpc.get_editor_property("vector_parameters")
    if len(params) != 1:
        fail("MPC 参数数不是 1: " + path)
    return mpc


# ---------------------------------------------------------------- 节点工具

def node(material, expression_class, x, y, tag=None):
    expression = MEL.create_material_expression(material, expression_class, x, y)
    if not expression:
        fail("create_material_expression 失败: %s" % expression_class)
        return None
    if tag:
        expression.set_editor_property("desc", TAG + tag)
    return expression


def link(from_node, from_output, to_node, to_input):
    if not from_node or not to_node:
        fail("link 缺节点: %s -> %s" % (from_output, to_input))
        return False
    if not MEL.connect_material_expressions(from_node, from_output, to_node, to_input):
        fail("connect_material_expressions 失败: %s -> %s" % (from_output, to_input))
        return False
    return True


def const(node_obj, value):
    node_obj.set_editor_property("const_b", value)


# ---------------------------------------------------------------- 剖面材质

def ensure_section_material():
    if EAL.does_asset_exist(SECTION_MAT_PATH):
        log("exists: " + SECTION_MAT_PATH)
        return EAL.load_asset(SECTION_MAT_PATH)

    factory = unreal.MaterialFactoryNew()
    material = AT.create_asset("M_PlanetSection", MAT_DIR, unreal.Material, factory)
    if not material:
        fail("创建 M_PlanetSection 失败")
        return None

    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", True)
    material.set_editor_property("opacity_mask_clip_value", 0.333)

    # Rel = WorldPos − ObjectPos（世界位置相对圆盘中心）。
    world_pos = node(material, unreal.MaterialExpressionWorldPosition, -1400, 0, "WorldPos")
    object_pos = node(material, unreal.MaterialExpressionObjectPositionWS, -1400, 120, "ObjectPos")
    relative = node(material, unreal.MaterialExpressionSubtract, -1200, 60, "Rel")
    link(world_pos, "XYZ", relative, "A")
    link(object_pos, "", relative, "B")

    axis_u = node(material, unreal.MaterialExpressionVectorParameter, -1200, 240, "AxisU")
    axis_u.set_editor_property("parameter_name", unreal.Name("SectionAxisU"))
    axis_u.set_editor_property("default_value", unreal.LinearColor(1.0, 0.0, 0.0, 1.0))

    axis_v = node(material, unreal.MaterialExpressionVectorParameter, -1200, 360, "AxisV")
    axis_v.set_editor_property("parameter_name", unreal.Name("SectionAxisV"))
    axis_v.set_editor_property("default_value", unreal.LinearColor(0.0, 1.0, 0.0, 1.0))

    half_extent = node(material, unreal.MaterialExpressionScalarParameter, -1200, 480, "HalfExtent")
    half_extent.set_editor_property("parameter_name", unreal.Name("SectionHalfExtentCm"))
    half_extent.set_editor_property("default_value", 100.0)

    def axis_coord(axis_node, y, tag):
        # UV 分量 = dot(Rel, Axis) / R * 0.5 + 0.5
        dot = node(material, unreal.MaterialExpressionDotProduct, -1000, y, tag + "Dot")
        link(relative, "", dot, "A")
        link(axis_node, "", dot, "B")

        divide = node(material, unreal.MaterialExpressionDivide, -820, y, tag + "Div")
        link(dot, "", divide, "A")
        link(half_extent, "", divide, "B")

        scale = node(material, unreal.MaterialExpressionMultiply, -640, y, tag + "Scale")
        link(divide, "", scale, "A")
        const(scale, 0.5)

        bias = node(material, unreal.MaterialExpressionAdd, -460, y, tag + "Bias")
        link(scale, "", bias, "A")
        const(bias, 0.5)
        return bias

    coord_u = axis_coord(axis_u, 240, "CoordU")
    coord_v = axis_coord(axis_v, 380, "CoordV")

    uv = node(material, unreal.MaterialExpressionAppendVector, -280, 300, "UV")
    link(coord_u, "", uv, "A")
    link(coord_v, "", uv, "B")

    texture = node(material, unreal.MaterialExpressionTextureSampleParameter2D, -60, 300, "Texture")
    texture.set_editor_property("parameter_name", unreal.Name("SectionTexture"))
    default_texture = load(DEFAULT_TEXTURE_PATH)
    if default_texture:
        texture.set_editor_property("texture", default_texture)
    else:
        fail("默认纹理缺失: " + DEFAULT_TEXTURE_PATH)
    link(uv, "", texture, "UVs")

    if not MEL.connect_material_property(texture, "RGB", unreal.MaterialProperty.MP_EMISSIVE_COLOR):
        fail("连接 Emissive 失败")
    if not MEL.connect_material_property(texture, "A", unreal.MaterialProperty.MP_OPACITY_MASK):
        fail("连接 OpacityMask 失败")

    # RecompileMaterial 返回的是错误字符串数组，不是 bool —— 成功时返回空列表，
    # 写成 `if not ...` 会把成功当失败。
    errors = MEL.recompile_material(material)
    if errors:
        fail("M_PlanetSection 编译失败: %s" % "; ".join(str(e) for e in errors))
    save(material, SECTION_MAT_PATH)
    return material


# ---------------------------------------------------------------- 行星材质裁剪

def find_tagged(material, tag):
    for expression in MEL.get_material_expressions(material):
        try:
            if expression.get_editor_property("desc") == TAG + tag:
                return expression
        except Exception:
            continue
    return None


def patch_planet_material(origin_mpc, normal_mpc):
    material = load(PLANET_MAT_PATH)
    if not material:
        fail("找不到 " + PLANET_MAT_PATH)
        return

    if find_tagged(material, "Mask"):
        log("already patched: " + PLANET_MAT_PATH)
        return

    # --- 集合参数 ---
    origin_node = node(material, unreal.MaterialExpressionCollectionParameter, -1900, -520, "Origin")
    origin_node.set_editor_property("collection", origin_mpc)
    origin_node.set_editor_property("parameter_name", unreal.Name("CutawayOrigin"))

    normal_node = node(material, unreal.MaterialExpressionCollectionParameter, -1900, -320, "Normal")
    normal_node.set_editor_property("collection", normal_mpc)
    normal_node.set_editor_property("parameter_name", unreal.Name("CutawayNormal"))

    origin_xyz = node(material, unreal.MaterialExpressionComponentMask, -1700, -520, "OriginXYZ")
    origin_xyz.set_editor_property("r", True)
    origin_xyz.set_editor_property("g", True)
    origin_xyz.set_editor_property("b", True)
    origin_xyz.set_editor_property("a", False)
    link(origin_node, "", origin_xyz, "")

    origin_w = node(material, unreal.MaterialExpressionComponentMask, -1700, -430, "OriginW")
    origin_w.set_editor_property("r", False)
    origin_w.set_editor_property("g", False)
    origin_w.set_editor_property("b", False)
    origin_w.set_editor_property("a", True)
    link(origin_node, "", origin_w, "")

    normal_xyz = node(material, unreal.MaterialExpressionComponentMask, -1700, -320, "NormalXYZ")
    normal_xyz.set_editor_property("r", True)
    normal_xyz.set_editor_property("g", True)
    normal_xyz.set_editor_property("b", True)
    normal_xyz.set_editor_property("a", False)
    link(normal_node, "", normal_xyz, "")

    normal_w = node(material, unreal.MaterialExpressionComponentMask, -1700, -230, "NormalW")
    normal_w.set_editor_property("r", False)
    normal_w.set_editor_property("g", False)
    normal_w.set_editor_property("b", False)
    normal_w.set_editor_property("a", True)
    link(normal_node, "", normal_w, "")

    # --- Half = saturate(dot(Rel, Normal.xyz) * 10) ---
    world_pos = node(material, unreal.MaterialExpressionWorldPosition, -1900, -660, "WorldPos")
    relative = node(material, unreal.MaterialExpressionSubtract, -1520, -520, "Rel")
    link(world_pos, "XYZ", relative, "A")
    link(origin_xyz, "", relative, "B")

    dot = node(material, unreal.MaterialExpressionDotProduct, -1340, -580, "HalfDot")
    link(relative, "", dot, "A")
    link(normal_xyz, "", dot, "B")

    half_scale = node(material, unreal.MaterialExpressionMultiply, -1160, -580, "HalfScale")
    link(dot, "", half_scale, "A")
    const(half_scale, 10.0)

    half = node(material, unreal.MaterialExpressionSaturate, -980, -580, "Half")
    link(half_scale, "", half, "")

    # --- Inside = saturate((Normal.w − |Rel|) * 0.02) ---
    length = node(material, unreal.MaterialExpressionLength, -1340, -400, "RelLength")
    link(relative, "", length, "")

    inside_delta = node(material, unreal.MaterialExpressionSubtract, -1160, -400, "InsideDelta")
    link(normal_w, "", inside_delta, "A")
    link(length, "", inside_delta, "B")

    inside_scale = node(material, unreal.MaterialExpressionMultiply, -980, -400, "InsideScale")
    link(inside_delta, "", inside_scale, "A")
    const(inside_scale, 0.02)

    inside = node(material, unreal.MaterialExpressionSaturate, -800, -400, "Inside")
    link(inside_scale, "", inside, "")

    # --- Mask = 1 − Half × Inside × Active ---
    clip_product = node(material, unreal.MaterialExpressionMultiply, -620, -500, "ClipProduct")
    link(half, "", clip_product, "A")
    link(inside, "", clip_product, "B")

    active_product = node(material, unreal.MaterialExpressionMultiply, -440, -500, "ActiveProduct")
    link(clip_product, "", active_product, "A")
    link(origin_w, "", active_product, "B")

    mask = node(material, unreal.MaterialExpressionOneMinus, -260, -500, "Mask")
    link(active_product, "", mask, "")

    # --- 与原 OpacityMask 输入相乘 ---
    existing = MEL.get_material_property_input_node(
        material, unreal.MaterialProperty.MP_OPACITY_MASK)
    existing_output = MEL.get_material_property_input_node_output_name(
        material, unreal.MaterialProperty.MP_OPACITY_MASK)
    if not existing:
        fail("M_PlanetVertexColor 的 OpacityMask 没有输入，无法插入裁剪")
        return

    multiply = node(material, unreal.MaterialExpressionMultiply, -80, -500, "MaskedOpacity")
    link(existing, existing_output, multiply, "A")
    link(mask, "", multiply, "B")

    if not MEL.connect_material_property(
            multiply, "", unreal.MaterialProperty.MP_OPACITY_MASK):
        fail("连接行星 OpacityMask 失败")
        return

    errors = MEL.recompile_material(material)
    if errors:
        fail("M_PlanetVertexColor 编译失败: %s" % "; ".join(str(e) for e in errors))
    save(material, PLANET_MAT_PATH)


# ---------------------------------------------------------------- 校验

def verify():
    section = load(SECTION_MAT_PATH)
    if not section:
        fail("verify: M_PlanetSection 不存在")
    else:
        emissive = MEL.get_material_property_input_node(
            section, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        if not emissive:
            fail("verify: M_PlanetSection 的 Emissive 没接")
        else:
            log("verify: M_PlanetSection emissive = %s" % emissive.get_class().get_name())

    planet = load(PLANET_MAT_PATH)
    if not planet:
        fail("verify: M_PlanetVertexColor 不存在")
        return
    opacity = MEL.get_material_property_input_node(
        planet, unreal.MaterialProperty.MP_OPACITY_MASK)
    if not opacity:
        fail("verify: M_PlanetVertexColor 的 OpacityMask 没接")
        return
    class_name = opacity.get_class().get_name()
    log("verify: planet opacity mask = %s" % class_name)
    if class_name != "MaterialExpressionMultiply":
        fail("verify: 行星 OpacityMask 顶点不是 Multiply（patch 没生效）")

    for tag in ("Origin", "Normal", "Mask"):
        if not find_tagged(planet, tag):
            fail("verify: 缺少标记节点 " + TAG + tag)


def main():
    origin_mpc = ensure_mpc(
        ORIGIN_MPC_PATH, "CutawayOrigin", unreal.LinearColor(0.0, 0.0, 0.0, 0.0))
    normal_mpc = ensure_mpc(
        NORMAL_MPC_PATH, "CutawayNormal", unreal.LinearColor(0.0, 0.0, 1.0, 0.0))

    ensure_section_material()

    if origin_mpc and normal_mpc:
        patch_planet_material(origin_mpc, normal_mpc)

    verify()

    if ERRORS:
        print("CUTAWAY_ASSETS_FAILED count=%d" % len(ERRORS))
        raise SystemExit(1)
    print("CUTAWAY_ASSETS_OK")


main()
