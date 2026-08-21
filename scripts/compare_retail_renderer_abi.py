#!/usr/bin/env python3
"""Compare shared renderer type sizes with the clean retail Xbox donor."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
from pathlib import Path


TYPES = (
    "dlight_t",
    "trRefEntity_t",
    "trRefdef_t",
    "orientationr_t",
    "image_t",
    "waveForm_t",
    "deformStage_t",
    "texModInfo_t",
    "surfaceSprite_t",
    "textureBundle_t",
    "shaderStage_t",
    "skyParms_t",
    "fogParms_t",
    "shader_t",
    "skinSurface_t",
    "skin_t",
    "fog_t",
    "viewParms_t",
    "drawSurf_t",
    "srfPoly_t",
    "srfDisplayList_t",
    "srfFlare_t",
    "srfGridMesh_t",
    "srfSurfaceFace_t",
    "srfTriangles_t",
    "msurface_t",
    "mnode_t",
    "bmodel_t",
    "mgrid_t",
    "world_t",
    "model_t",
    "frontEndCounters_t",
    "glstate_t",
    "backEndCounters_t",
    "backEndState_t",
    "srfTerrain_t",
    "trGlobals_t",
    "stageVars_t",
    "shaderCommands_t",
    "renderCommandList_t",
    "setColorCommand_t",
    "drawBufferCommand_t",
    "subImageCommand_t",
    "swapBuffersCommand_t",
    "endFrameCommand_t",
    "stretchPicCommand_t",
    "rotatePicCommand_t",
    "drawSurfsCommand_t",
    "renderCommand_t",
    "backEndData_t",
    "DDS_PIXELFORMAT",
    "DDS_HEADER",
)

# Total size equality can hide ABI drift when fields move but padding keeps the
# record the same size.  Keep this list to records shared by both Xbox trees;
# Elite Force-specific extensions are deliberately excluded.
FIELDS = (
    ("dlight_t", "mType"),
    ("dlight_t", "origin"),
    ("dlight_t", "mProjOrigin"),
    ("dlight_t", "color"),
    ("dlight_t", "radius"),
    ("dlight_t", "mProjRadius"),
    ("dlight_t", "additive"),
    ("dlight_t", "transformed"),
    ("dlight_t", "mProjTransformed"),
    ("dlight_t", "mDirection"),
    ("dlight_t", "mBasis2"),
    ("dlight_t", "mBasis3"),
    ("dlight_t", "mTransDirection"),
    ("dlight_t", "mTransBasis2"),
    ("dlight_t", "mTransBasis3"),
    ("orientationr_t", "origin"),
    ("orientationr_t", "axis"),
    ("orientationr_t", "viewOrigin"),
    ("orientationr_t", "modelMatrix"),
    ("image_t", "imgCode"),
    ("image_t", "width"),
    ("image_t", "height"),
    ("image_t", "texnum"),
    ("image_t", "internalFormat"),
    ("image_t", "isLightmap"),
    ("image_t", "isSystem"),
    ("image_t", "mipcount"),
    ("image_t", "iLastLevelUsedOn"),
    ("waveForm_t", "func"),
    ("waveForm_t", "base"),
    ("waveForm_t", "amplitude"),
    ("waveForm_t", "phase"),
    ("waveForm_t", "frequency"),
    ("deformStage_t", "deformation"),
    ("deformStage_t", "moveVector"),
    ("deformStage_t", "deformationWave"),
    ("deformStage_t", "deformationSpread"),
    ("deformStage_t", "bulgeWidth"),
    ("deformStage_t", "bulgeHeight"),
    ("deformStage_t", "bulgeSpeed"),
    ("texModInfo_t", "type"),
    ("texModInfo_t", "wave"),
    ("texModInfo_t", "matrix"),
    ("texModInfo_t", "translate"),
    ("surfaceSprite_t", "surfaceSpriteType"),
    ("surfaceSprite_t", "width"),
    ("surfaceSprite_t", "height"),
    ("surfaceSprite_t", "density"),
    ("surfaceSprite_t", "wind"),
    ("surfaceSprite_t", "windIdle"),
    ("surfaceSprite_t", "fadeDist"),
    ("surfaceSprite_t", "fadeMax"),
    ("surfaceSprite_t", "fadeScale"),
    ("surfaceSprite_t", "fxAlphaStart"),
    ("surfaceSprite_t", "fxAlphaEnd"),
    ("surfaceSprite_t", "fxDuration"),
    ("surfaceSprite_t", "vertSkew"),
    ("surfaceSprite_t", "variance"),
    ("surfaceSprite_t", "fxGrow"),
    ("surfaceSprite_t", "facing"),
    ("textureBundle_t", "image"),
    ("textureBundle_t", "tcGenVectors"),
    ("textureBundle_t", "texMods"),
    ("textureBundle_t", "numTexMods"),
    ("textureBundle_t", "numImageAnimations"),
    ("textureBundle_t", "imageAnimationSpeed"),
    ("textureBundle_t", "tcGen"),
    ("textureBundle_t", "isLightmap"),
    ("textureBundle_t", "oneShotAnimMap"),
    ("textureBundle_t", "vertexLightmap"),
    ("shaderStage_t", "active"),
    ("shaderStage_t", "isDetail"),
    ("shaderStage_t", "isEnvironment"),
    ("shaderStage_t", "isBumpMap"),
    ("shaderStage_t", "index"),
    ("shaderStage_t", "lightmapStyle"),
    ("shaderStage_t", "alphaGen"),
    ("shaderStage_t", "rgbGen"),
    ("shaderStage_t", "adjustColorsForFog"),
    ("shaderStage_t", "mGLFogColorOverride"),
    ("shaderStage_t", "bundle"),
    ("shaderStage_t", "rgbWave"),
    ("shaderStage_t", "alphaWave"),
    ("shaderStage_t", "constantColor"),
    ("shaderStage_t", "stateBits"),
    ("shaderStage_t", "ss"),
    ("shader_t", "name"),
    ("shader_t", "lightmapIndex"),
    ("shader_t", "styles"),
    ("shader_t", "index"),
    ("shader_t", "sortedIndex"),
    ("shader_t", "sort"),
    ("shader_t", "surfaceFlags"),
    ("shader_t", "contentFlags"),
    ("shader_t", "defaultShader"),
    ("shader_t", "explicitlyDefined"),
    ("shader_t", "entityMergable"),
    ("shader_t", "isBumpMap"),
    ("shader_t", "sky"),
    ("shader_t", "fogParms"),
    ("shader_t", "portalRange"),
    ("shader_t", "multitextureEnv"),
    ("shader_t", "cullType"),
    ("shader_t", "polygonOffset"),
    ("shader_t", "noMipMaps"),
    ("shader_t", "needsNormal"),
    ("shader_t", "needsTangent"),
    ("shader_t", "fogPass"),
    ("shader_t", "deforms"),
    ("shader_t", "numDeforms"),
    ("shader_t", "numUnfoggedPasses"),
    ("shader_t", "stages"),
    ("shader_t", "timeOffset"),
    ("shader_t", "remappedShader"),
    ("shader_t", "next"),
    ("fog_t", "originalBrushNumber"),
    ("fog_t", "bounds"),
    ("fog_t", "colorInt"),
    ("fog_t", "tcScale"),
    ("fog_t", "parms"),
    ("fog_t", "hasSurface"),
    ("fog_t", "surface"),
    ("drawSurf_t", "sort"),
    ("drawSurf_t", "surface"),
    ("srfPoly_t", "surfaceType"),
    ("srfPoly_t", "hShader"),
    ("srfPoly_t", "fogIndex"),
    ("srfPoly_t", "numVerts"),
    ("srfPoly_t", "verts"),
    ("srfDisplayList_t", "surfaceType"),
    ("srfDisplayList_t", "listNum"),
    ("srfFlare_t", "surfaceType"),
    ("srfFlare_t", "origin"),
    ("srfFlare_t", "normal"),
    ("srfFlare_t", "color"),
    ("srfFlare_t", "number"),
    ("srfFlare_t", "visible"),
    ("frontEndCounters_t", "c_sphere_cull_patch_in"),
    ("frontEndCounters_t", "c_box_cull_patch_in"),
    ("frontEndCounters_t", "c_sphere_cull_md3_in"),
    ("frontEndCounters_t", "c_box_cull_md3_in"),
    ("frontEndCounters_t", "c_leafs"),
    ("frontEndCounters_t", "c_dlightSurfaces"),
    ("frontEndCounters_t", "c_dlightSurfacesCulled"),
    ("glstate_t", "currenttextures"),
    ("glstate_t", "currenttmu"),
    ("glstate_t", "finishCalled"),
    ("glstate_t", "texEnv"),
    ("glstate_t", "faceCulling"),
    ("glstate_t", "glStateBits"),
    ("backEndCounters_t", "c_surfaces"),
    ("backEndCounters_t", "c_shaders"),
    ("backEndCounters_t", "c_vertexes"),
    ("backEndCounters_t", "c_indexes"),
    ("backEndCounters_t", "c_totalIndexes"),
    ("backEndCounters_t", "c_overDraw"),
    ("backEndCounters_t", "c_dlightVertexes"),
    ("backEndCounters_t", "c_dlightIndexes"),
    ("backEndCounters_t", "c_flareAdds"),
    ("backEndCounters_t", "c_flareTests"),
    ("backEndCounters_t", "c_flareRenders"),
    ("backEndCounters_t", "msec"),
    ("stageVars_t", "colors"),
    ("stageVars_t", "texcoords"),
    ("shaderCommands_t", "indexes"),
    ("shaderCommands_t", "xyz"),
    ("shaderCommands_t", "normal"),
    ("shaderCommands_t", "tangent"),
    ("shaderCommands_t", "texCoords"),
    ("shaderCommands_t", "vertexColors"),
    ("shaderCommands_t", "vertexAlphas"),
    ("shaderCommands_t", "vertexDlightBits"),
    ("shaderCommands_t", "svars"),
    ("shaderCommands_t", "shader"),
    ("shaderCommands_t", "shaderTime"),
    ("shaderCommands_t", "fogNum"),
    ("shaderCommands_t", "dlightBits"),
    ("shaderCommands_t", "numIndexes"),
    ("shaderCommands_t", "numVertexes"),
    ("shaderCommands_t", "numPasses"),
    ("shaderCommands_t", "currentPass"),
    ("shaderCommands_t", "setTangents"),
    ("shaderCommands_t", "currentStageIteratorFunc"),
    ("shaderCommands_t", "xstages"),
    ("shaderCommands_t", "registration"),
    ("shaderCommands_t", "SSInitializedWind"),
    ("shaderCommands_t", "fading"),
    ("setColorCommand_t", "commandId"),
    ("setColorCommand_t", "color"),
    ("drawBufferCommand_t", "commandId"),
    ("drawBufferCommand_t", "buffer"),
    ("subImageCommand_t", "commandId"),
    ("subImageCommand_t", "image"),
    ("subImageCommand_t", "width"),
    ("subImageCommand_t", "height"),
    ("subImageCommand_t", "data"),
    ("swapBuffersCommand_t", "commandId"),
    ("endFrameCommand_t", "commandId"),
    ("endFrameCommand_t", "buffer"),
    ("stretchPicCommand_t", "commandId"),
    ("stretchPicCommand_t", "shader"),
    ("stretchPicCommand_t", "x"),
    ("stretchPicCommand_t", "y"),
    ("stretchPicCommand_t", "w"),
    ("stretchPicCommand_t", "h"),
    ("stretchPicCommand_t", "s1"),
    ("stretchPicCommand_t", "t1"),
    ("stretchPicCommand_t", "s2"),
    ("stretchPicCommand_t", "t2"),
    ("rotatePicCommand_t", "commandId"),
    ("rotatePicCommand_t", "shader"),
    ("rotatePicCommand_t", "x"),
    ("rotatePicCommand_t", "y"),
    ("rotatePicCommand_t", "w"),
    ("rotatePicCommand_t", "h"),
    ("rotatePicCommand_t", "s1"),
    ("rotatePicCommand_t", "t1"),
    ("rotatePicCommand_t", "s2"),
    ("rotatePicCommand_t", "t2"),
    ("rotatePicCommand_t", "a"),
)

# Numeric enum values are part of the renderer ABI too: they cross parser,
# command-buffer, surface-dispatch, and backend boundaries. Probe every value
# shared by the Elite Force tree and the shipping Xbox donor.
ENUM_VALUES = (
    "DLIGHT_VERTICAL", "DLIGHT_PROJECTED",
    "SS_BAD", "SS_PORTAL", "SS_ENVIRONMENT", "SS_OPAQUE", "SS_DECAL",
    "SS_SEE_THROUGH", "SS_BANNER", "SS_INSIDE", "SS_MID_INSIDE",
    "SS_MIDDLE", "SS_MID_OUTSIDE", "SS_OUTSIDE", "SS_FOG",
    "SS_UNDERWATER", "SS_BLEND0", "SS_BLEND1", "SS_BLEND2", "SS_BLEND3",
    "SS_BLEND6", "SS_STENCIL_SHADOW", "SS_ALMOST_NEAREST", "SS_NEAREST",
    "GF_NONE", "GF_SIN", "GF_SQUARE", "GF_TRIANGLE", "GF_SAWTOOTH",
    "GF_INVERSE_SAWTOOTH", "GF_NOISE", "GF_RAND",
    "DEFORM_NONE", "DEFORM_WAVE", "DEFORM_NORMALS", "DEFORM_BULGE",
    "DEFORM_MOVE", "DEFORM_PROJECTION_SHADOW", "DEFORM_AUTOSPRITE",
    "DEFORM_AUTOSPRITE2", "DEFORM_TEXT0", "DEFORM_TEXT1", "DEFORM_TEXT2",
    "DEFORM_TEXT3", "DEFORM_TEXT4", "DEFORM_TEXT5", "DEFORM_TEXT6",
    "DEFORM_TEXT7",
    "AGEN_IDENTITY", "AGEN_SKIP", "AGEN_ENTITY", "AGEN_ONE_MINUS_ENTITY",
    "AGEN_VERTEX", "AGEN_ONE_MINUS_VERTEX", "AGEN_LIGHTING_SPECULAR",
    "AGEN_WAVEFORM", "AGEN_PORTAL", "AGEN_BLEND", "AGEN_CONST", "AGEN_DOT",
    "AGEN_ONE_MINUS_DOT",
    "CGEN_BAD", "CGEN_IDENTITY_LIGHTING", "CGEN_IDENTITY", "CGEN_ENTITY",
    "CGEN_ONE_MINUS_ENTITY", "CGEN_EXACT_VERTEX", "CGEN_VERTEX",
    "CGEN_ONE_MINUS_VERTEX", "CGEN_WAVEFORM", "CGEN_LIGHTING_DIFFUSE",
    "CGEN_LIGHTING_DIFFUSE_ENTITY", "CGEN_FOG", "CGEN_CONST",
    "CGEN_LIGHTMAPSTYLE",
    "TCGEN_BAD", "TCGEN_IDENTITY", "TCGEN_LIGHTMAP", "TCGEN_LIGHTMAP1",
    "TCGEN_LIGHTMAP2", "TCGEN_LIGHTMAP3", "TCGEN_TEXTURE",
    "TCGEN_ENVIRONMENT_MAPPED", "TCGEN_FOG", "TCGEN_VECTOR",
    "ACFF_NONE", "ACFF_MODULATE_RGB", "ACFF_MODULATE_RGBA",
    "ACFF_MODULATE_ALPHA",
    "GLFOGOVERRIDE_NONE", "GLFOGOVERRIDE_BLACK", "GLFOGOVERRIDE_WHITE",
    "GLFOGOVERRIDE_MAX",
    "TMOD_NONE", "TMOD_TRANSFORM", "TMOD_TURBULENT", "TMOD_SCROLL",
    "TMOD_SCALE", "TMOD_STRETCH", "TMOD_ROTATE", "TMOD_ENTITY_TRANSLATE",
    "CT_FRONT_SIDED", "CT_BACK_SIDED", "CT_TWO_SIDED",
    "FP_NONE", "FP_EQUAL", "FP_LE", "FP_GLFOG",
    "SF_BAD", "SF_SKIP", "SF_FACE", "SF_GRID", "SF_TRIANGLES", "SF_POLY",
    "SF_TERRAIN", "SF_MD3", "SF_MDX", "SF_FLARE", "SF_ENTITY",
    "SF_DISPLAY_LIST", "SF_NUM_SURFACE_TYPES",
    "MOD_BAD", "MOD_BRUSH", "MOD_MESH", "MOD_MDXM", "MOD_MDXA",
    "RC_END_OF_LIST", "RC_SET_COLOR", "RC_STRETCH_PIC", "RC_ROTATE_PIC",
    "RC_ROTATE_PIC2", "RC_DRAW_SURFS", "RC_DRAW_BUFFER", "RC_SWAP_BUFFERS",
    "RC_WORLD_EFFECTS",
)

# Packed state masks cross the shader parser/backend boundary in stateBits.
# Treat them as ABI values even though the original headers spell them as
# preprocessor constants rather than enum members.
STATE_VALUES = (
    "GLS_SRCBLEND_ZERO", "GLS_SRCBLEND_ONE", "GLS_SRCBLEND_DST_COLOR",
    "GLS_SRCBLEND_ONE_MINUS_DST_COLOR", "GLS_SRCBLEND_SRC_ALPHA",
    "GLS_SRCBLEND_ONE_MINUS_SRC_ALPHA", "GLS_SRCBLEND_DST_ALPHA",
    "GLS_SRCBLEND_ONE_MINUS_DST_ALPHA", "GLS_SRCBLEND_ALPHA_SATURATE",
    "GLS_SRCBLEND_BITS", "GLS_DSTBLEND_ZERO", "GLS_DSTBLEND_ONE",
    "GLS_DSTBLEND_SRC_COLOR", "GLS_DSTBLEND_ONE_MINUS_SRC_COLOR",
    "GLS_DSTBLEND_SRC_ALPHA", "GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA",
    "GLS_DSTBLEND_DST_ALPHA", "GLS_DSTBLEND_ONE_MINUS_DST_ALPHA",
    "GLS_DSTBLEND_BITS", "GLS_DEPTHMASK_TRUE", "GLS_POLYMODE_LINE",
    "GLS_DEPTHTEST_DISABLE", "GLS_DEPTHFUNC_EQUAL", "GLS_ATEST_GT_0",
    "GLS_ATEST_LT_80", "GLS_ATEST_GE_80", "GLS_ATEST_GE_C0",
    "GLS_ATEST_BITS", "GLS_DEFAULT", "GLS_ALPHA",
)

# Elite Force owns one additional runtime surface implementation. Common
# surface IDs still match retail exactly; only the terminal count is extended.
EXPECTED_ENUM_DELTAS = {"SF_NUM_SURFACE_TYPES": 1}

FUNCTION_RE = re.compile(
    r"^_?(stefx_(?:size|offset|enum|state)_[0-9]+)\s*(?:\(.*\))?:$"
)
MOV_RE = re.compile(r"\bmov\s+eax,([0-9A-Fa-f]+)h?\b", re.IGNORECASE)


def run(command: list[str], cwd: Path) -> str:
    result = subprocess.run(
        command,
        cwd=cwd,
        capture_output=True,
        text=True,
        errors="replace",
    )
    if result.returncode != 0:
        output = "\n".join(part for part in (result.stdout, result.stderr) if part)
        raise RuntimeError(
            f"command failed ({result.returncode}): {' '.join(command)}\n{output}"
        )
    return result.stdout


def build_probe(
    name: str,
    source_root: Path,
    include_path: str,
    compiler: Path,
    linker: Path,
    output_root: Path,
    defines: tuple[str, ...],
    preinclude: str | None = None,
) -> tuple[
    dict[str, int],
    dict[tuple[str, str], int],
    dict[str, int],
    dict[str, int],
]:
    probe_dir = output_root / name
    probe_dir.mkdir(parents=True, exist_ok=True)
    source = probe_dir / "renderer_abi_probe.cpp"
    obj = probe_dir / "renderer_abi_probe.obj"

    lines = ["#include <stddef.h>"]
    if preinclude:
        lines.append(f'#include "{preinclude}"')
    lines.extend((f'#include "{include_path}"', ""))
    for index, type_name in enumerate(TYPES):
        lines.append(
            f'extern "C" __declspec(noinline) unsigned __cdecl '
            f'stefx_size_{index:03d}(void) {{ return sizeof({type_name}); }}'
        )
    for index, (type_name, field_name) in enumerate(FIELDS):
        lines.append(
            f'extern "C" __declspec(noinline) unsigned __cdecl '
            f'stefx_offset_{index:03d}(void) '
            f'{{ return 0x10000u + offsetof({type_name}, {field_name}); }}'
        )
    for index, enum_name in enumerate(ENUM_VALUES):
        lines.append(
            f'extern "C" __declspec(noinline) unsigned __cdecl '
            f'stefx_enum_{index:03d}(void) '
            f'{{ return 0x20000u + (unsigned)({enum_name}); }}'
        )
    for index, state_name in enumerate(STATE_VALUES):
        lines.append(
            f'extern "C" __declspec(noinline) unsigned __cdecl '
            f'stefx_state_{index:03d}(void) '
            f'{{ return 0x40000000u + (unsigned)({state_name}); }}'
        )
    source.write_text("\n".join(lines) + "\n", encoding="ascii")

    command = [
        str(compiler),
        "/nologo",
        "/c",
        "/Od",
        "/Ob0",
        "/MT",
        "/W3",
        "/Z7",
        f"/I{source_root}",
        f"/I{compiler.parents[2] / 'include'}",
        f"/I{compiler.parents[3] / 'include'}",
        f"/Fo{obj}",
    ]
    command.extend(f"/D{define}" for define in defines)
    command.append(str(source))
    run(command, probe_dir)

    disassembly = run(
        [str(linker), "/dump", "/disasm:nobytes", str(obj)], probe_dir
    )
    values: dict[str, int] = {}
    current: str | None = None
    for line in disassembly.splitlines():
        function = FUNCTION_RE.match(line)
        if function:
            current = function.group(1)
            continue
        if current is None:
            continue
        immediate = MOV_RE.search(line)
        if immediate:
            values[current] = int(immediate.group(1), 16)
            current = None

    expected = [f"stefx_size_{index:03d}" for index in range(len(TYPES))]
    expected.extend(f"stefx_offset_{index:03d}" for index in range(len(FIELDS)))
    expected.extend(f"stefx_enum_{index:03d}" for index in range(len(ENUM_VALUES)))
    expected.extend(f"stefx_state_{index:03d}" for index in range(len(STATE_VALUES)))
    missing = [function for function in expected if function not in values]
    if missing:
        raise RuntimeError(f"{name} probe did not expose ABI values for: {', '.join(missing)}")
    sizes = {
        type_name: values[f"stefx_size_{index:03d}"]
        for index, type_name in enumerate(TYPES)
    }
    offsets = {
        field: values[f"stefx_offset_{index:03d}"] - 0x10000
        for index, field in enumerate(FIELDS)
    }
    enums = {
        enum_name: values[f"stefx_enum_{index:03d}"] - 0x20000
        for index, enum_name in enumerate(ENUM_VALUES)
    }
    states = {
        state_name: values[f"stefx_state_{index:03d}"] - 0x40000000
        for index, state_name in enumerate(STATE_VALUES)
    }
    return sizes, offsets, enums, states


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", type=Path, required=True)
    parser.add_argument("--donor-root", type=Path, required=True)
    parser.add_argument("--xdk-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    repo_root = args.repo_root.resolve()
    donor_root = args.donor_root.resolve()
    xdk_root = args.xdk_root.resolve()
    output = args.output.resolve()
    compiler = xdk_root / "xbox" / "bin" / "vc71" / "cl.exe"
    linker = xdk_root / "xbox" / "bin" / "vc71" / "link.exe"

    common = ("NDEBUG", "_XBOX", "WIN32", "VV_LIGHTING", "FINAL_BUILD", "_FINAL")
    current, current_offsets, current_enums, current_states = build_probe(
        "current",
        repo_root / "code",
        "renderer/tr_local.h",
        compiler,
        linker,
        output.parent / "retail_renderer_abi_probe",
        common + ("_JK2EXE", "STEFX_ELITE_FORCE_SP", "_XBOX_VC71_MIGRATION"),
    )
    donor, donor_offsets, donor_enums, donor_states = build_probe(
        "donor",
        donor_root,
        "renderer/tr_local.h",
        compiler,
        linker,
        output.parent / "retail_renderer_abi_probe",
        common + ("_WIN32", "_JK2", "_JK2MP"),
        "qcommon/exe_headers.h",
    )

    rows = [
        {
            "type": type_name,
            "current": current[type_name],
            "donor": donor[type_name],
            "delta": current[type_name] - donor[type_name],
            "match": current[type_name] == donor[type_name],
        }
        for type_name in TYPES
    ]
    payload = {
        "summary": {
            "types": len(rows),
            "matching": sum(row["match"] for row in rows),
            "different": sum(not row["match"] for row in rows),
        },
        "types": rows,
        "field_summary": {
            "fields": len(FIELDS),
            "matching": sum(current_offsets[field] == donor_offsets[field] for field in FIELDS),
            "different": sum(current_offsets[field] != donor_offsets[field] for field in FIELDS),
        },
        "fields": [
            {
                "type": type_name,
                "field": field_name,
                "current": current_offsets[(type_name, field_name)],
                "donor": donor_offsets[(type_name, field_name)],
                "delta": current_offsets[(type_name, field_name)] - donor_offsets[(type_name, field_name)],
                "match": current_offsets[(type_name, field_name)] == donor_offsets[(type_name, field_name)],
            }
            for type_name, field_name in FIELDS
        ],
        "enum_summary": {
            "values": len(ENUM_VALUES),
            "matching": sum(current_enums[name] == donor_enums[name] for name in ENUM_VALUES),
            "expected_extensions": sum(
                current_enums[name] - donor_enums[name] == EXPECTED_ENUM_DELTAS.get(name)
                for name in EXPECTED_ENUM_DELTAS
            ),
            "unexpected_differences": sum(
                current_enums[name] != donor_enums[name]
                and current_enums[name] - donor_enums[name] != EXPECTED_ENUM_DELTAS.get(name)
                for name in ENUM_VALUES
            ),
        },
        "enums": [
            {
                "name": name,
                "current": current_enums[name],
                "donor": donor_enums[name],
                "delta": current_enums[name] - donor_enums[name],
                "match": current_enums[name] == donor_enums[name],
                "expected_extension": (
                    current_enums[name] - donor_enums[name]
                    == EXPECTED_ENUM_DELTAS.get(name)
                ),
            }
            for name in ENUM_VALUES
        ],
        "state_summary": {
            "values": len(STATE_VALUES),
            "matching": sum(current_states[name] == donor_states[name] for name in STATE_VALUES),
            "different": sum(current_states[name] != donor_states[name] for name in STATE_VALUES),
        },
        "states": [
            {
                "name": name,
                "current": current_states[name],
                "donor": donor_states[name],
                "delta": current_states[name] - donor_states[name],
                "match": current_states[name] == donor_states[name],
            }
            for name in STATE_VALUES
        ],
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(payload, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
