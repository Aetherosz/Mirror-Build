"""Makes the materials in /MirrorBuild/Materials. Run it with the editor's Python commandlet.

Each one is the game's decal look (three textures, referenced, none copied) plus two fixes
that only act on an instance with a negative scale:
 - logos: the logo's part of the atlas is read right to left, in the pixel shader;
 - words made of single letters: the whole word is reflected about its own centre, in the vertex shader.
"""
import unreal

FOLDER = "/MirrorBuild/Materials"
ATLAS = "/Game/FactoryGame/Buildable/-Shared/Texture/ColorAtlas_"
LIB = unreal.MaterialEditingLibrary
MP = unreal.MaterialProperty
TYPE = unreal.CustomMaterialOutputType

# Word bounds in mesh space, from the shipped meshes: plane, its position, along from/to, height from/to.
# "x" = the word lies in a plane of constant X and runs along Y. "y" = the other way round.
LABELS = {
    "Assembler": (True, [("y", -596.1, -3.5, 48.2, 205.7, 226.6)]),
    "Constructor": (False, [
        ("y", -400.2, 186.9, 291.5, 25.0, 33.3),
        ("y", 350.3, -304.4, -199.8, 26.6, 34.9),
        ("x", -213.7, -225.1, -153.5, 243.9, 252.2),
        ("x", 213.9, 153.3, 224.9, 243.9, 252.2)]),
    "Foundry": (True, [
        ("y", 387.3, -136.1, -41.8, 447.2, 456.8),
        ("x", -261.0, -17.9, 42.7, 123.1, 132.8),
        ("x", -261.0, -218.7, -158.0, 123.1, 132.8),
        ("y", -322.6, -196.2, -135.5, 123.1, 132.8),
        ("y", -265.9, -311.1, -216.8, 88.6, 98.2),
        ("x", -332.0, 169.0, 263.2, 88.6, 98.2),
        ("x", 332.0, 169.9, 264.2, 87.4, 97.0)]),
    "Biomass": (False, [
        ("y", 331.6, -80.0, -3.1, 296.9, 305.1),
        ("x", 175.8, -107.0, -22.7, 14.3, 22.5),
        ("y", -44.0, 10.0, 87.0, 105.6, 115.7)]),
    "Coal": (False, [("x", 402.9, 985.1, 1042.8, 177.4, 197.0)]),
    "Packager": (True, [
        ("x", 324.2, -58.0, -10.2, 370.7, 381.0),
        ("y", 294.5, 218.0, 265.8, 369.1, 379.3),
        ("x", -324.1, -284.8, -237.0, 277.0, 287.3)]),
    "Smelter": (True, [
        ("x", -118.9, -15.1, 65.3, 75.9, 84.5),
        ("x", 121.8, 294.4, 374.8, 75.9, 84.5)]),
    "Logo": (True, []),
}

LOGO_CODE = """
if (Mirrored > 0.5 && UV.x >= 0.8075 && UV.y >= 0.4405 && UV.y <= 0.5555)
    UV.x = clamp(1.813 - UV.x, 0.8115, 0.9995);
return UV;
"""


def word_code(words):
    lines = ["if (Mirrored < 0.5 || UV.x > 0.625 || UV.y < 0.745 || UV.y > 0.88) return float4(0, 0, 0, 0);"]
    for plane, at, start, end, low, high in words:
        fixed, along = ("x", "y") if plane == "x" else ("y", "x")
        # Reflecting p about the centre c moves it by 2 * (c - p).
        move = "%.2f - 2 * P.%s" % (start + end, along)
        offset = ("0, %s, 0" if along == "y" else "%s, 0, 0") % move
        lines.append(
            "if (abs(P.%s - (%.1f)) < 2 && P.%s > %.1f && P.%s < %.1f && P.z > %.1f && P.z < %.1f) return float4(%s, 1);"
            % (fixed, at, along, start - 1.5, along, end + 1.5, low - 1.5, high + 1.5, offset))
    lines.append("return float4(0, 0, 0, 0);")
    return "\n".join(lines)


def build(name, logos, words):
    path = "%s/M_Labels_%s" % (FOLDER, name)
    mat = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
    if mat:
        LIB.delete_all_material_expressions(mat)
    else:
        mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            "M_Labels_" + name, FOLDER, unreal.Material, unreal.MaterialFactoryNew())

    # The game's own settings for this material.
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    mat.set_editor_property("two_sided", True)
    mat.set_editor_property("opacity_mask_clip_value", 0.3333)
    mat.set_editor_property("used_with_instanced_static_meshes", True)
    mat.set_editor_property("phys_material", unreal.load_asset("/Game/FactoryGame/-Shared/Material/PhysicalMaterial/PhysMat_Metal"))

    def node(cls, **props):
        made = LIB.create_material_expression(mat, cls, 0, 0)
        for key, value in props.items():
            made.set_editor_property(key, value)
        return made

    def link(source, target, target_pin="", source_pin=""):
        if not LIB.connect_material_expressions(source, source_pin, target, target_pin):
            raise RuntimeError("%s: cannot connect %s to %s.%s" % (name, source.get_name(), target.get_name(), target_pin))

    def output(source, source_pin, prop):
        if not LIB.connect_material_property(source, source_pin, prop):
            raise RuntimeError("%s: cannot connect %s" % (name, prop))

    def custom(code, kind, *sources):
        pins = []
        for pin, _, _ in sources:
            custom_input = unreal.CustomInput()
            custom_input.set_editor_property("input_name", pin)
            pins.append(custom_input)
        made = node(unreal.MaterialExpressionCustom, code=code, output_type=kind, inputs=pins)
        for pin, source, source_pin in sources:
            link(source, made, pin, source_pin)
        return made

    def to_world(source):
        made = node(unreal.MaterialExpressionTransform,
                    transform_source_type=unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_INSTANCE,
                    transform_type=unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
        link(source, made)
        return made

    def interpolate(source):
        made = node(unreal.MaterialExpressionVertexInterpolator)
        link(source, made)
        return made

    uv = node(unreal.MaterialExpressionTextureCoordinate)

    # An instance is mirrored when its three axes are left-handed. Worked out per vertex,
    # because instance transforms aren't all available to the pixel shader.
    axes = []
    for pin, axis in (("X", (1, 0, 0)), ("Y", (0, 1, 0)), ("Z", (0, 0, 1))):
        unit = node(unreal.MaterialExpressionConstant3Vector, constant=unreal.LinearColor(axis[0], axis[1], axis[2], 0))
        axes.append((pin, to_world(unit), ""))
    mirrored = custom("return dot(cross(X, Y), Z) < 0 ? 1 : 0;", TYPE.CMOT_FLOAT1, *axes)

    sample_uv = uv
    if logos:
        sample_uv = custom(LOGO_CODE, TYPE.CMOT_FLOAT2, ("UV", uv, ""), ("Mirrored", interpolate(mirrored), ""))

    def sample(suffix, sampler):
        made = node(unreal.MaterialExpressionTextureSample, texture=unreal.load_asset(ATLAS + suffix), sampler_type=sampler)
        link(sample_uv, made, "UVs")
        return made

    colour = sample("Alb", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    rough = sample("Rough", unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
    normal = sample("Nor", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    output(colour, "RGB", MP.MP_BASE_COLOR)
    output(colour, "A", MP.MP_OPACITY_MASK)
    output(rough, "R", MP.MP_METALLIC)
    output(rough, "G", MP.MP_ROUGHNESS)

    if not words:
        output(normal, "RGB", MP.MP_NORMAL)
    else:
        local = node(unreal.MaterialExpressionPreSkinnedPosition)
        word = custom(word_code(words), TYPE.CMOT_FLOAT4, ("P", local, ""), ("UV", uv, ""), ("Mirrored", mirrored, ""))
        offset = node(unreal.MaterialExpressionComponentMask, r=True, g=True, b=True, a=False)
        link(word, offset)
        output(to_world(offset), "", MP.MP_WORLD_POSITION_OFFSET)

        # A reflected word faces backwards, and a two-sided material would light it from behind.
        turned = node(unreal.MaterialExpressionComponentMask, r=False, g=False, b=False, a=True)
        link(word, turned)
        facing = custom("return Turned > 0.5 ? -N : N;", TYPE.CMOT_FLOAT3,
                        ("N", normal, "RGB"), ("Turned", interpolate(turned), ""))
        output(facing, "", MP.MP_NORMAL)

    LIB.recompile_material(mat)
    if not unreal.EditorAssetLibrary.save_asset(path, False):
        raise RuntimeError("cannot save " + path)
    unreal.log("MIRRORLABELS made %s words=%d logos=%d" % (path, len(words), logos))


for label_name, (has_logos, word_list) in LABELS.items():
    build(label_name, has_logos, word_list)
