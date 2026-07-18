#!/usr/bin/env python3
"""Verify that Holomatch MP uses the shared EF/SP UI path and DDS-only assets."""

from __future__ import annotations

import argparse
import json
import re
import struct
import sys
import zipfile
from pathlib import Path
from xml.etree import ElementTree


SOURCE_EXTS = {".c", ".cc", ".cpp", ".cxx", ".asm", ".vsh", ".psh"}
ORIGINAL_IMAGE_EXTS = {".jpg", ".jpeg", ".tga", ".png"}

REQUIRED_X_UI_SOURCES = {
    "../ui/ui_stefx_spbridge.cpp",
    "../../code/ui/ui_atoms.cpp",
    "../../code/ui/ui_ef_frontend.cpp",
    "../../code/ui/ui_ef_lifecycle.cpp",
    "../../code/ui/ui_ef_pause.cpp",
    "../../code/ui/ui_ef_qmenu.cpp",
    "../../code/ui/ui_main.cpp",
    "../../code/ui/ui_shared.cpp",
}

REQUIRED_X_UI_HEADERS = {
    "../../code/ui/ui_ef_qmenu_shared.h",
}

REQUIRED_SP_SHARED_UI_SOURCES = {
    "../ui/ui_ef_frontend.cpp",
    "../ui/ui_ef_pause.cpp",
    "../ui/ui_ef_qmenu.cpp",
}

REQUIRED_SP_UI_FRAMEWORK_SOURCES = {
    "../ui/ui_atoms.cpp",
    "../ui/ui_main.cpp",
    "../ui/ui_shared.cpp",
}

LEGACY_X_UI_SOURCES = {
    "../ui/ui_atoms.c",
    "../ui/ui_force.c",
    "../ui/ui_gameinfo.c",
    "../ui/ui_main.c",
    "../ui/ui_players.c",
    "../ui/ui_saber.c",
    "../ui/ui_shared.c",
    "../ui/ui_syscalls.c",
    "../ui/ui_util.c",
    "../ui/ui_stefx_stub.c",
}

REQUIRED_CGAME_SOURCE = "../cgame/cg_stefx_ui_shim.c"
LEGACY_CGAME_SOURCES = {
    "../cgame/cg_newdraw.c",
    "../cgame/cg_stefx_menu_stub.c",
}

LEGACY_UI_GUARD_SOURCES = {
    "codemp/ui/ui_atoms.c",
    "codemp/ui/ui_force.c",
    "codemp/ui/ui_gameinfo.c",
    "codemp/ui/ui_main.c",
    "codemp/ui/ui_players.c",
    "codemp/ui/ui_saber.c",
    "codemp/ui/ui_shared.c",
    "codemp/ui/ui_syscalls.c",
    "codemp/ui/ui_util.c",
}

LEGACY_CGAME_GUARD_SOURCES = {
    "codemp/cgame/cg_newDraw.c",
}

LEGACY_UI_PROJECT_ARTIFACTS = {
    "codemp/ui/ui.bat",
    "codemp/ui/ui.def",
    "codemp/ui/ui.dsp",
    "codemp/ui/ui.vcproj",
    "codemp/ui/ui_force.h",
    "codemp/ui/ui_syscalls.asm",
}

RETIRED_UI_LOCAL_HEADER = "codemp/ui/ui_local.h"
REQUIRED_RETIRED_UI_LOCAL_MARKERS = {
    "Retired JA MP UI-local header.",
    "Holomatch UI behavior is owned by the shared EF/SP code/ui framework",
    "#error codemp/ui/ui_local.h is dead for Holomatch MP",
}
FORBIDDEN_RETIRED_UI_LOCAL_MARKERS = {
    "extern vmCvar_t",
    "Menu_Cache(",
    "UI_LoadMenus(",
    "UI_xboxErrorPopup",
}
FORBIDDEN_RUNTIME_UI_LOCAL_INCLUDES = {
    "codemp/xbox/XBLive_MM.cpp": {"../ui/ui_local.h", "..\\ui\\ui_local.h"},
    "codemp/xbox/XBLive.cpp": {"../ui/ui_local.h", "..\\ui\\ui_local.h"},
}

FORBIDDEN_MP_PROJECT_UI_PATHS = {
    "../../ui/menudef.h",
}

MP_PROJECTS_FOR_UI_MANDATE = (
    "codemp/x_ui/x_ui.vcproj",
    "codemp/x_jk2cgame/x_jk2cgame.vcproj",
    "codemp/x_jk2game/x_jk2game.vcproj",
    "codemp/x_exe/x_exe.vcproj",
    "codemp/cgame/JK2_cgame.vcproj",
    "codemp/game/JK2_game.vcproj",
)

FORBIDDEN_ANY_MP_PROJECT_PATHS = FORBIDDEN_MP_PROJECT_UI_PATHS | {
    "../ui/ui_shared.c",
    "./cg_newdraw.c",
}

FORBIDDEN_ACTIVE_MP_SOURCE_UI_INCLUDES = {
    '#include "../../ui/menudef.h"',
    "#include \"../../ui/menudef.h\"",
}

FORBIDDEN_SOURCE_UI_PATHS = {
    "base/ui/jahud.txt",
    "base/ui/jampmenus.txt",
    "base/ui/jampingame.txt",
    "base/ui/testhud.menu",
}

REQUIRED_CGAME_HUD_MARKERS = {
    "CG_STEFXDrawHolomatch2D();\n\treturn;",
    "STEFX_HM: cgame Holomatch 2D using SP interface-only path; legacy cgame parser HUD bypassed",
    "STEFX_HM: cgame HUD menu system disabled for EF MP; using EF SP interface HUD",
    "STEFX_HM: cgame ignored deprecated MP HUD menu load request",
    "STEFX_HM: cgame skipped inherited menu asset cache for shared SP UI path",
    "STEFX_HM: cgame skipped inherited parser vehicle HUD; EF Holomatch HUD owns status",
    "STEFX_HM: cgame skipped inherited parser stats HUD; EF Holomatch HUD owns status",
    "STEFX_HM: cgame EF SP interface HUD using DDS native UV draw path",
    "STEFX_HM: cgame EF SP interface HUD V-corrected direct DDS draw path",
    "STEFX_HM: cgame skipped ad hoc MP weapon/score HUD text; EF interface and score overlay own status",
    "STEFX_HM: cgame skipped legacy renderer font registration; EF prop-font atlas owns Holomatch text",
    "STEFX_HM: cgame EF prop fonts loaded=",
    "STEFX_HM: cgame EF prop text draw",
}

FORBIDDEN_CGAME_HUD_MARKERS = {
    '"%s  SCORE %d"',
    "CG_STEFXHolomatchWeaponName",
}

REQUIRED_CGAME_UI_SHIM_MARKERS = {
    "STEFX_HM: cgame UI parser shim is reject-only; shared SP UI owns menus",
    "Menus_FindByName",
    "return NULL;",
    "Menu_PaintAll",
}

REQUIRED_CGAME_DEAD_MENU_ABI_MARKERS = {
    'codemp/ui/ui_shared.h': [
        '#include "../../code/ui/menudef.h"',
        "STEFX_HM: cgame dead menu ABI uses shared SP code/ui menudef constants",
        "runtime parser/menu behavior is disabled by cg_stefx_ui_shim.c",
    ],
}

REQUIRED_CGAME_SCOREBOARD_MARKERS = {
    "STEFX_HM: cgame scoreboard uses EF Holomatch overlay; parser scoreboard bypassed",
    "return CG_STEFXDrawHolomatchScores();",
    "STEFX_HM: cgame ignored parser scoreboard scroll; EF score overlay owns scores",
}

REQUIRED_CGAME_PARSER_DEAD_MARKERS = {
    "STEFX_HM: cgame ignored deprecated MP loadmenu block; shared SP UI owns menus",
    "STEFX_HM: cgame score selection stayed data-only; parser menu feeders ignored",
    "STEFX_HM: cgame parser entry points are dead; shared SP UI owns menus",
}

REQUIRED_CGAME_FLAG_TAG_SKIP_MARKER = "STEFX_HM: cgame skipped inherited flag-carrier tag probe for EF Holomatch players"

REQUIRED_SHARED_CGAME_HUD_HEADER = "../../code/cgame/cg_ef_hud_shared.h"
REQUIRED_RENDERER_SP_WHOLESALE_FUNCTIONS = (
    "RB_SetGL2D",
    "RB_StretchPic",
    "RB_RotatePic",
    "RB_RotatePic2",
)
REQUIRED_RENDERER_SP_WHOLESALE_MARKERS = (
    "STEFX: RB_XboxForce2DOverlayState where=",
    "STEFX_FRONTEND_2D_BACKEND shader=",
    "STEFX: RB_StretchPic shader=",
    "backEnd.refdef.time = Sys_Milliseconds();",
    "glOrtho (0, 640, 480, 0, 0, 1);",
)
REQUIRED_RENDERER_SOLID_FILL_MARKERS = {
    "code/win32/openjkdf2/fakeglx.cpp": [
        "g_stefxFakeglOverlayDrawContext",
        "D3DRS_FILLMODE, D3DFILL_SOLID",
        "D3DRS_BACKFILLMODE, D3DFILL_SOLID",
        "STEFX_OVERLAY_DEVICE_STATE",
    ],
}
REQUIRED_RENDERER_MDR_FRAME_CLAMP_MARKERS = {
    "codemp/renderer/tr_animation.cpp": [
        "static void R_STEFX_ClampAnimEntityFrames",
        "static qboolean R_STEFX_IsHolomatchPlayerMDRModel",
        'strstr( model->name, "models/players2/" )',
        "STEFX_HM: renderer clamped EF MDR frame model=",
        "ent->e.frame = frameCount - 1;",
        "ent->e.oldframe = frameCount - 1;",
        "R_STEFX_ClampAnimEntityFrames( ent, tr.currentModel );",
    ],
}
REQUIRED_RENDERER_SHADER_MANIFEST_MARKERS = {
    "codemp/renderer/tr_shader.cpp": [
        "static qboolean R_STEFXLoadShaderManifest",
        "_console_shader_list_",
        "STEFX_HM: renderer loaded shader manifest",
    ],
}
REQUIRED_RENDERER_IMAGE_UPLOAD_MARKERS = {
    "codemp/renderer/tr_image_xbox.cpp": [
        "JkaFakeglSetDDSUploadPicmip(picmip ? r_picmip->integer : 0);",
        "STEFX_HM: renderer using SP-style GL_RGBA screen texture; legacy MP GL_LIN_RGBA8 path disabled",
        'tr.screenImage = R_CreateImage("*screen", data, SCREEN_IMAGE_MAX_WIDTH, SCREEN_IMAGE_MAX_HEIGHT, GL_RGBA',
        "JA: Upload32 image='%s'",
        "JA: Upload32 done image='%s'",
    ],
    "codemp/renderer/tr_model.cpp": [
        "STEFX_HM: renderer skipped inherited zero-size registration StretchPic prime",
        "RE_StretchPic(0, 0, 0, 0, 0, 0, 1, 1, 0);",
    ],
}
REQUIRED_RENDERER_SP_TEXTURE_POLICY_MARKERS = {
    "codemp/renderer/tr_image_xbox.cpp": [
        "STEFX_HM: MP renderer using SP Xbox Upload32 caps",
        "STEFX_HM: MP renderer preserving SP high-fidelity UI font",
        "STEFX_HM: MP renderer SP Borg alpha upload",
        "R_XboxIsBorgAlphaCutoutTexture",
        "R_XboxIsHighFidelityUIFont",
        'strstr(debugName, "models/players2/")',
    ],
    "codemp/renderer/tr_init.cpp": [
        "STEFX_HM: MP renderer using SP Xbox dynamic glow policy off",
        "STEFX_HM: MP renderer using SP Xbox r_picmip=1 policy",
        "STEFX_HM: MP renderer using SP Xbox r_texturebits=0 component upload policy",
        "STEFX_HM: MP renderer using SP Xbox r_subdivisions=64 policy",
        'Cvar_Get( "r_stefxLightmapBoost", "2.5", CVAR_ARCHIVE );',
    ],
    "code/win32/openjkdf2/glteximage_dds.cpp": [
        "STEFX_HM: BGRA32 DDS direct Xbox upload",
        "internalformat == 0x9998 /*GL_DDS_RGBA32_EXT*/",
        "useDirectXboxDDSUpload",
    ],
}
REQUIRED_RENDERER_CAPTURE_MARKERS = {
    "code/win32/openjkdf2/fakeglx.cpp": [
        "STEFX_HM: renderer screenshot render-target XGWrite enabled for MP visual proof",
    ],
}
REQUIRED_HOLOMATCH_FILESYSTEM_MARKERS = {
    "codemp/qcommon/files_console.cpp": [
        "pack->pure_checksum = pack->checksum;",
        "Q_strncpyz(pak->pakGamename, dir, sizeof(pak->pakGamename));",
        "COM_StripExtension(pakName, pak->pakBasename);",
        "pak->referenced |= FS_GENERAL_REF;",
        "const char *FS_LoadedPakChecksums(void)",
        "const char *FS_LoadedPakNames(void)",
        "const char *FS_ReferencedPakChecksums(void)",
        "const char *FS_ReferencedPakNames(void)",
    ],
    "codemp/server/sv_init.cpp": [
        "STEFX_HM: SV pure loaded PK3s checksums=",
    ],
}
REQUIRED_HOLOMATCH_MODEL_MARKERS = {
    "codemp/client/cl_main.cpp": "STEFX_HM: client default userinfo model is EF Holomatch model='munro/default'",
    "codemp/client/cl_data.cpp": 'strcpy(model,"munro/default");',
    "codemp/cgame/cg_players.c": '#define STEFX_HOLOMATCH_DEFAULT_MODEL "munro"',
}

REQUIRED_HOLOMATCH_BOT_SETUP_MARKERS = {
    "codemp/game/ai_main.c": [
        "botlibSetupResult = trap_BotLibSetup();",
        "botlibSetupResult != BLERR_NOERROR",
        "STEFX_HM: BotAISetup trap_BotLibSetup done result=%d",
        "trap_FS_FOpenFile( aasPath, &aasFile, FS_READ );",
        "STEFX_HM: official EF AAS package probe map='%s'",
        "generated waypoint route remains available for Xbox BSP",
        "trap_BotLibVarSet( \"sv_mapChecksum\", ckSum.string );",
        "STEFX_HM: official EF AAS botlib checksum var set value='%s'",
        "botLibMapLoadResult = trap_BotLibLoadMap( mapname.string );",
        "STEFX_HM: official EF AAS botlib load result=%d map='%s'",
    ],
    "codemp/game/ai_wpnav.c": [
        "STEFX_HM: fallback bot waypoint inherited CalculatePaths skipped map='%s'",
        "STEFX_HM: fallback bot waypoint local links done map='%s'",
        "STEFX_HM: fallback bot waypoint trap path calculation skipped map='%s'; local Holomatch links active",
    ],
}

REQUIRED_HOLOMATCH_INPUT_MARKERS = {
    "codemp/win32/win_main_console.cpp": [
        "STEFX_HM: input Plan-B XInitDevices completed before D3D init",
        "STEFX_HM: MP using SP-style fakegl pushbuffer path; main skipped legacy Direct3D_SetPushBufferSize",
        "g_XInitDevicesAlreadyCalled = true;",
    ],
    "codemp/win32/win_input_xbox.cpp": [
        "bool g_XInitDevicesAlreadyCalled = false;",
        "joy_deadzone = Cvar_Get( \"joy_deadzone\", \"0.18\", CVAR_ARCHIVE );",
        "STEFX_HM: input using SP early XInitDevices path; gamepad mask=",
        "STEFX_HM: first gamepad state port=",
        "STEFX_HM: input SP no-controller tracking active",
        "STEFX_HM: input state read failed port=",
        "STEFX_HM: direct-map input gate cleared splash/controller lock",
    ],
}

REQUIRED_HOLOMATCH_SOUND_MARKERS = {
    "codemp/client/snd_dma_console.cpp": [
        "static qboolean S_STEFXValidSfxHandle",
        "sfxHandle >= MAX_SFX",
        "static qboolean S_STEFXValidEntityNum",
        "entityNum >= MAX_GENTITIES",
        "#define\t\tSOUND_REF_DIST_BASE\t1500.f",
        "static int S_STEFXClampListenerCount",
        "STEFX_HM: sound using EF/SP hardened Xbox path; handle/entity guards active",
        "STEFX_HM: sound using SP Xbox attenuation reference distance=",
        "STEFX_HM: sound clamped listener count caller=",
        "STEFX_HM: sound registration active listeners=",
        "STEFX_HM: sound local listener clamped active=",
    ],
    "codemp/win32/win_qal_xbox.cpp": [
        "s_pState->m_ImageDesc = NULL;",
        "STEFX_HM: QAL effects image sound/dsstdfx.bin missing; continuing dry audio",
        "STEFX_HM: QAL downloaded Xbox effects image bytes=",
        "return (ALCdevice*)s_pState->m_SoundObject;",
    ],
}

REQUIRED_HOLOMATCH_COMBAT_MARKERS = {
    "codemp/win32/win_main_console.cpp": [
        "+set stefx_hm_directSlice 1",
        "+set g_spawnInvulnerability 0",
        "+map hm_borg1",
        "STEFX_HM: direct Holomatch startup bypasses menus; loading hm_borg1 from command line",
    ],
    "codemp/game/ai_main.c": [
        "STEFX_HM: direct Holomatch combat bot command client=",
        "BUTTON_ALT_ATTACK",
    ],
    "codemp/game/g_weapon.c": [
        "#define STEFX_HM_PHASER_DAMAGE",
        "#define STEFX_HM_PHASER_MIN_DAMAGE",
        "STEFX_HM: server EF Phaser applied damage attacker=",
    ],
    "codemp/game/g_combat.c": [
        "STEFX_HM: score update client=",
        "STEFX_HM: player death scored",
    ],
    "codemp/game/g_client.c": [
        "STEFX_HM: respawn used EF direct path",
        "STEFX_HM: direct Holomatch combat spawn override client=",
        "ClientManager::splitScreenMode == qtrue",
    ],
    "codemp/cgame/cg_ents.c": [
        "STEFX_HM: cgame skipped EF moving missile dlight on Xbox renderer weapon=%d alt=%d",
        "STEFX_HM: cgame rendered EF alternate missile safe sprite weapon=%d ent=%d radius=%.1f",
        "STEFX_HM: cgame rendered EF missile feedback without inherited projectile model weapon=%d",
    ],
}

FORBIDDEN_HOLOMATCH_COMBAT_MARKERS = {
    "STEFX_HM: bot disabled EF Holomatch alternate fire for vertical-slice stability weapon=",
    "STEFX_HM: bot converted EF Holomatch alternate fire command to primary fire weapon=",
}

REQUIRED_BRIDGE_MARKERS = {
    "return UI_EFSP_VmMain(command, arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9, arg10, arg11);",
}

FORBIDDEN_BRIDGE_MARKERS = {
    "case UI_SET_ACTIVE_MENU:",
    "case UI_CONSOLE_COMMAND:",
    "case UI_REFRESH:",
    "Menu_Cache(",
    "UI_LoadMenus(",
    "Menus_ActivateByName(",
    "Menu_Paint(",
    "uiStatic_t uis;",
    "uiInfo_t uiInfo;",
    "void UI_DrawHandlePic(",
    "void UI_FillRect(",
    "void Text_Paint(",
}

REQUIRED_SP_UI_FRAMEWORK_MP_MARKERS = {
    "code/ui/ui_atoms.cpp": "STEFX_HM: SP UI atoms active in efmp.xbe; MP bridge owns only syscall plumbing",
    "code/ui/ui_main.cpp": "STEFX_HM: SP UI main framework active in efmp.xbe; text/UI state owned by shared code/ui",
    "code/ui/ui_shared.cpp": "STEFX_HM: SP UI shared framework compiled in efmp.xbe; parser compatibility is reject-only",
}

REQUIRED_LIFECYCLE_MARKERS = {
    "int UI_EFSP_VmMain(int command",
    "case UI_SET_ACTIVE_MENU:",
    "case UI_CONSOLE_COMMAND:",
    "case UI_REFRESH:",
    "STEFX_HM: UI mandate active; uniform SP code/ui owns Holomatch UI",
    "STEFX_HM: UI mandate enforced; MP legacy menus stay dead and SP code/ui owns all Holomatch UI behavior",
    "STEFX_HM: SP EF UI lifecycle initialized from code/ui; no script menu cache; codemp/ui remains adapter-only",
    "STEFX_HM: SP EF UI cache skipped legacy renderer font; EF prop-font atlas owns Holomatch text",
}

FORBIDDEN_LIFECYCLE_MENU_CACHE_PATTERNS = {
    r"(?<![A-Za-z0-9_])Menu_Cache\s*\(",
    r"(?<![A-Za-z0-9_])UI_LoadMenus\s*\(",
    r"(?<![A-Za-z0-9_])UI_ParseMenu\s*\(",
}

REQUIRED_INTERFACE_HUD_DDS = {
    "gfx/interface/healthcap1.dds",
    "gfx/interface/ammobar.dds",
    "gfx/interface/healthcap2.dds",
    "gfx/interface/armorcap1.dds",
    "gfx/interface/armorcap2.dds",
    "gfx/interface/ammouppercap1.dds",
    "gfx/interface/ammouppercap2.dds",
    "gfx/interface/ammolowercap1.dds",
    "gfx/interface/ammolowercap2.dds",
}

REQUIRED_HOLOMATCH_NAV_FILES = {
    "maps/hm_borg1.aas",
}
REQUIRED_HOLOMATCH_AAS = "maps/hm_borg1.aas"
AAS_IDENT = b"EAAS"
REQUIRED_XBOX_EFFECTS_IMAGE = "sound/dsstdfx.bin"

DIRECT_HOLOMATCH_BOTS = {
    "1_of_12": {
        "model": "models/players2/1_of_12",
        "aifile": "botfiles/bots/1of729_c.c",
    },
    "2_of_3": {
        "model": "models/players2/2_of_3",
        "aifile": "botfiles/bots/2of3_c.c",
    },
}

DIRECT_BOT_MODEL_FILES = {
    "animation.cfg",
    "groups.cfg",
    "head.md3",
    "lower.mdr",
    "upper.mdr",
}

DIRECT_BOT_SKIN_FILES = {
    "head_default.skin",
    "head_red.skin",
    "head_blue.skin",
    "lower_default.skin",
    "lower_red.skin",
    "lower_blue.skin",
    "upper_default.skin",
    "upper_red.skin",
    "upper_blue.skin",
}

DIRECT_BOT_ICON_DDS = {
    "icon_default.dds",
    "icon_red.dds",
    "icon_blue.dds",
}

DIRECT_HOLOMATCH_WEAPON_MODEL_FILES = {
    "models/weapons2/phaser/phaser_w.md3",
    "models/weapons2/phaser/phaser.md3",
    "models/weapons2/phaser/phaser_hand.md3",
    "models/weapons2/phaser/phaser_flash.md3",
    "models/weapons2/prifle/prifle_w.md3",
    "models/weapons2/prifle/prifle.md3",
    "models/weapons2/prifle/prifle_hand.md3",
    "models/weapons2/prifle/prifle_flash.md3",
    "models/weapons2/imod/imod2_w.md3",
    "models/weapons2/imod/imod2.md3",
    "models/weapons2/imod/imod2_hand.md3",
    "models/weapons2/imod/imod2_flash.md3",
    "models/weapons2/scavenger/scavenger_w.md3",
    "models/weapons2/scavenger/scavenger.md3",
    "models/weapons2/scavenger/scavenger_hand.md3",
    "models/weapons2/scavenger/scavenger_flash.md3",
    "models/weapons2/tpd/tpd_w.md3",
    "models/weapons2/tpd/tpd.md3",
    "models/weapons2/tpd/tpd_hand.md3",
    "models/weapons2/tpd/tpd_flash.md3",
    "models/weapons2/tpd/tpd_barrel.md3",
    "models/weapons2/arc_welder/arc_w.md3",
    "models/weapons2/arc_welder/arc.md3",
    "models/weapons2/arc_welder/arc_hand.md3",
    "models/weapons2/arc_welder/arc_flash.md3",
    "models/weapons2/arc_welder/arc_barrel.md3",
}

DIRECT_HOLOMATCH_PICKUP_MODEL_FILES = {
    "models/powerups/trek/prifle_ammo.md3",
    "models/powerups/trek/imod_ammo.md3",
    "models/powerups/trek/scavenger_ammo.md3",
    "models/powerups/trek/tetrion_ammo.md3",
    "models/powerups/trek/arc_ammo.md3",
    "models/powerups/trek/armor.md3",
    "models/powerups/trek/hypo_double.md3",
    "models/powerups/trek/hypo_single.md3",
}

FORBIDDEN_XBE_STRINGS = {
    "ui/jampmenus.txt",
    "ui/jampingame.txt",
    "ui/jamp/main.menu",
    "ui/jk2mpmenus.txt",
    "ui/jk2mpingame.txt",
    "ui/jk2mp/gameinfo.txt",
    "ui/hud.txt",
    "gfx/menus/newFront/SaberLoad",
    "gfx/menus/newFront/GlowLoad",
    "gfx/menus/radar/radar.png",
    "ui/assets/statusbar/selectedhealth.tga",
    "menu/new/bar1.tga",
    "menu/common/warpcore2.jpg",
    "ef_sp_renderprobe.txt",
    "ef_sp_cxbx_present_throttle.txt",
    "inherited MP menu",
    "JA MP menu",
    "%s  SCORE %d",
    "CG_STEFXHolomatchWeaponName",
    "DDS V-corrected UV draw path",
}


def norm_path(value: str) -> str:
    return value.replace("\\", "/").lower()


def fail(message: str) -> None:
    raise SystemExit(message)


def normalize_source_block(value: str) -> str:
    return value.replace("\r\n", "\n").replace("\r", "\n")


def extract_cpp_function(text: str, name: str) -> str:
    normalized = normalize_source_block(text)
    match = re.search(
        r"(?m)^[^\n]*\b" + re.escape(name) + r"\s*\([^;]*\)\s*\{",
        normalized,
    )
    if not match:
        fail(f"Missing C/C++ function body for {name}")

    start = match.start()
    brace_index = normalized.find("{", match.start())
    depth = 0
    for index in range(brace_index, len(normalized)):
        char = normalized[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return normalized[start : index + 1]

    fail(f"Could not find end of C/C++ function body for {name}")


def dds_format(data: bytes) -> str:
    if len(data) < 128 or data[:4] != b"DDS ":
        return "invalid"

    fourcc = data[84:88].rstrip(b"\0")
    rgb_bits = struct.unpack_from("<I", data, 88)[0]
    if fourcc:
        return fourcc.decode("ascii", errors="replace")
    if rgb_bits == 32:
        return "BGRA32"
    if rgb_bits == 16:
        return "RGB565"
    return f"RGB{rgb_bits}"


def normalize_texture_ref(value: str) -> str | None:
    texture = norm_path(value.strip().strip('"'))
    if not texture or texture.startswith("*") or texture in {"nodraw", "none"}:
        return None

    suffix = Path(texture).suffix.lower()
    if suffix in ORIGINAL_IMAGE_EXTS or suffix == ".dds":
        texture = texture[: -len(suffix)]
    return texture + ".dds"


def skin_texture_refs(data: bytes) -> set[str]:
    refs: set[str] = set()
    text = data.decode("ascii", errors="ignore")
    for raw_line in text.replace("\r", "\n").split("\n"):
        line = raw_line.split("//", 1)[0].strip()
        if "," not in line:
            continue
        _, texture = line.split(",", 1)
        ref = normalize_texture_ref(texture)
        if ref:
            refs.add(ref)
    return refs


def active_project_sources(project: Path) -> list[str]:
    root = ElementTree.parse(project).getroot()
    sources: list[str] = []
    for file_node in root.iter("File"):
        rel = file_node.attrib.get("RelativePath", "")
        if not rel:
            continue
        if Path(rel.replace("\\", "/")).suffix.lower() not in SOURCE_EXTS:
            continue

        excluded = False
        for config_node in file_node.iter("FileConfiguration"):
            if config_node.attrib.get("Name") != "Release|Win32":
                continue
            for tool in config_node.iter("Tool"):
                if tool.attrib.get("ExcludedFromBuild", "").lower() == "true":
                    excluded = True
                    break
            if excluded:
                break

        if not excluded:
            sources.append(norm_path(rel))
    return sources


def project_paths(project: Path) -> set[str]:
    root = ElementTree.parse(project).getroot()
    paths: set[str] = set()
    for file_node in root.iter("File"):
        rel = file_node.attrib.get("RelativePath", "")
        if rel:
            paths.add(norm_path(rel))
    return paths


def verify_legacy_source_guards(repo_root: Path) -> dict[str, object]:
    guarded: list[str] = []
    deleted: list[str] = []
    missing_guard: list[str] = []

    for rel in sorted(LEGACY_UI_GUARD_SOURCES):
        path = repo_root / rel
        if not path.is_file():
            deleted.append(rel)
            continue
        text = path.read_text(encoding="utf-8", errors="ignore")
        if (
            "#if defined(STEFX_ELITE_FORCE_MP)" not in text
            or "#error" not in text
            or "shared EF/SP UI path" not in text
        ):
            missing_guard.append(rel)
        else:
            guarded.append(rel)

    for rel in sorted(LEGACY_CGAME_GUARD_SOURCES):
        path = repo_root / rel
        if not path.is_file():
            deleted.append(rel)
            continue
        text = path.read_text(encoding="utf-8", errors="ignore")
        if (
            "#if defined(STEFX_ELITE_FORCE_MP)" not in text
            or "#error" not in text
            or "EF SP interface HUD" not in text
        ):
            missing_guard.append(rel)
        else:
            guarded.append(rel)

    if missing_guard:
        fail(
            "legacy JA UI/menu source must hard-error under STEFX_ELITE_FORCE_MP: "
            + ", ".join(missing_guard)
        )

    stale_legacy_projects = sorted(
        rel for rel in LEGACY_UI_PROJECT_ARTIFACTS if (repo_root / rel).exists()
    )
    if stale_legacy_projects:
        fail(
            "legacy JA UI project/artifact file(s) must be removed for the shared SP UI mandate: "
            + ", ".join(stale_legacy_projects)
        )

    source_ui = repo_root / "base" / "ui"
    stale_source_ui: list[str] = []
    if source_ui.exists():
        for rel in sorted(FORBIDDEN_SOURCE_UI_PATHS):
            path = repo_root / rel
            if path.exists():
                stale_source_ui.append(rel)
        jamp_dir = source_ui / "jamp"
        if jamp_dir.exists():
            stale_source_ui.extend(
                norm_path(path.relative_to(repo_root).as_posix())
                for path in sorted(jamp_dir.rglob("*"))
                if path.is_file()
            )

    if stale_source_ui:
        fail(
            "repo still contains inherited JA UI script artifact(s): "
            + ", ".join(stale_source_ui[:16])
        )

    return {
        "legacyUiDeadCodeGuards": guarded,
        "legacyUiDeletedSources": deleted,
        "legacyUiProjectArtifacts": 0,
        "forbiddenSourceUiArtifacts": 0,
    }


def verify_solution(repo_root: Path) -> dict[str, object]:
    sln = repo_root / "codemp" / "JKA_mp.sln"
    sln_text = sln.read_text(encoding="utf-8", errors="ignore").replace("\\", "/").lower()
    if "ui/ui.vcproj" in sln_text:
        fail(f"{sln} must not reference the legacy MP UI project")

    forbidden_project_paths: list[str] = []
    for project_rel in MP_PROJECTS_FOR_UI_MANDATE:
        project = repo_root / project_rel
        hits = sorted(project_paths(project) & FORBIDDEN_ANY_MP_PROJECT_PATHS)
        forbidden_project_paths.extend(f"{project_rel}: {hit}" for hit in hits)

    if forbidden_project_paths:
        fail(
            "MP projects must not reference inherited JA menu/HUD build artifacts; "
            "Holomatch UI must route through shared code/ui and codemp/ui adapter headers only: "
            + ", ".join(forbidden_project_paths)
        )

    forbidden_source_includes: list[str] = []
    for folder_rel in ("codemp/ui", "codemp/cgame", "codemp/game"):
        folder = repo_root / folder_rel
        for path in sorted(folder.rglob("*")):
            if path.suffix.lower() not in {".c", ".cpp", ".h"}:
                continue
            text = path.read_text(encoding="utf-8", errors="ignore")
            for marker in FORBIDDEN_ACTIVE_MP_SOURCE_UI_INCLUDES:
                if marker in text:
                    rel = norm_path(path.relative_to(repo_root).as_posix())
                    forbidden_source_includes.append(f"{rel}: {marker}")

    if forbidden_source_includes:
        fail(
            "active MP source must not include the orphan root JA menu header; "
            "Holomatch UI constants must route through shared SP code/ui or dead cgame ABI only: "
            + ", ".join(forbidden_source_includes[:16])
        )

    retired_ui_local = repo_root / RETIRED_UI_LOCAL_HEADER
    if not retired_ui_local.is_file():
        fail("missing retired UI-local tripwire header: " + RETIRED_UI_LOCAL_HEADER)
    retired_ui_local_text = retired_ui_local.read_text(encoding="utf-8", errors="ignore")
    missing_retired_ui_local = sorted(
        marker for marker in REQUIRED_RETIRED_UI_LOCAL_MARKERS if marker not in retired_ui_local_text
    )
    if missing_retired_ui_local:
        fail(
            "codemp/ui/ui_local.h must remain a fail-fast tombstone under the uniform SP UI mandate; "
            "missing marker(s): "
            + ", ".join(missing_retired_ui_local)
        )
    forbidden_retired_ui_local = sorted(
        marker for marker in FORBIDDEN_RETIRED_UI_LOCAL_MARKERS if marker in retired_ui_local_text
    )
    if forbidden_retired_ui_local:
        fail(
            "codemp/ui/ui_local.h still contains inherited JA menu declarations instead of a tombstone: "
            + ", ".join(forbidden_retired_ui_local)
        )

    runtime_ui_local_includes: list[str] = []
    for rel, include_markers in FORBIDDEN_RUNTIME_UI_LOCAL_INCLUDES.items():
        text = (repo_root / rel).read_text(encoding="utf-8", errors="ignore")
        for marker in include_markers:
            if marker in text:
                runtime_ui_local_includes.append(f"{rel}: {marker}")
    if runtime_ui_local_includes:
        fail(
            "MP runtime code must not include the retired JA UI-local header; "
            "use shared SP code/ui bridge or dead ABI headers only: "
            + ", ".join(runtime_ui_local_includes)
        )

    x_ui_project = repo_root / "codemp" / "x_ui" / "x_ui.vcproj"
    x_ui_sources = active_project_sources(x_ui_project)
    x_ui_paths = project_paths(x_ui_project)
    missing = sorted(REQUIRED_X_UI_SOURCES - set(x_ui_sources))
    legacy = sorted(set(x_ui_sources) & LEGACY_X_UI_SOURCES)
    if missing:
        fail("x_ui is missing shared EF/SP UI source(s): " + ", ".join(missing))
    if legacy:
        fail("x_ui still compiles inherited MP UI source(s): " + ", ".join(legacy))

    invalid_codemp_ui = sorted(
        source
        for source in x_ui_sources
        if source.startswith("../ui/") and source != "../ui/ui_stefx_spbridge.cpp"
    )
    if invalid_codemp_ui:
        fail(
            "x_ui may only use the MP syscall adapter from codemp/ui; "
            "all UI behavior must come from code/ui: "
            + ", ".join(invalid_codemp_ui)
        )

    non_shared_ui = sorted(
        source
        for source in x_ui_sources
        if source.startswith("../../code/ui/") and source not in REQUIRED_X_UI_SOURCES
    )
    if non_shared_ui:
        fail(
            "x_ui contains unapproved code/ui source(s); add SP/EF shared UI files explicitly: "
            + ", ".join(non_shared_ui)
        )

    missing_headers = sorted(REQUIRED_X_UI_HEADERS - x_ui_paths)
    if missing_headers:
        fail(
            "x_ui is missing shared SP/EF UI framework header(s): "
            + ", ".join(missing_headers)
        )

    cgame_sources = active_project_sources(repo_root / "codemp" / "x_jk2cgame" / "x_jk2cgame.vcproj")
    if REQUIRED_CGAME_SOURCE not in cgame_sources:
        fail(f"x_jk2cgame is missing required EF cgame UI shim: {REQUIRED_CGAME_SOURCE}")
    legacy_cgame = sorted(set(cgame_sources) & LEGACY_CGAME_SOURCES)
    if legacy_cgame:
        fail("x_jk2cgame still compiles inherited cgame menu source(s): " + ", ".join(legacy_cgame))

    cgame_hud_text = (repo_root / "codemp" / "cgame" / "cg_draw.c").read_text(
        encoding="utf-8", errors="ignore"
    ).replace("\r\n", "\n")
    cgame_main_text = (repo_root / "codemp" / "cgame" / "cg_main.c").read_text(
        encoding="utf-8", errors="ignore"
    ).replace("\r\n", "\n")
    cgame_console_text = (repo_root / "codemp" / "cgame" / "cg_consolecmds.c").read_text(
        encoding="utf-8", errors="ignore"
    ).replace("\r\n", "\n")
    cgame_ui_shim_text = (repo_root / "codemp" / "cgame" / "cg_stefx_ui_shim.c").read_text(
        encoding="utf-8", errors="ignore"
    ).replace("\r\n", "\n")
    missing_hud_markers = sorted(
        marker
        for marker in REQUIRED_CGAME_HUD_MARKERS
        if marker not in cgame_hud_text and marker not in cgame_main_text
    )
    if missing_hud_markers:
        fail(
            "cgame Holomatch HUD route must bypass the legacy parser HUD path; missing marker(s): "
            + ", ".join(missing_hud_markers)
        )
    forbidden_hud_markers = sorted(
        marker for marker in FORBIDDEN_CGAME_HUD_MARKERS if marker in cgame_hud_text
    )
    if forbidden_hud_markers:
        fail(
            "cgame Holomatch HUD still contains ad hoc MP status text instead of the shared EF interface path: "
            + ", ".join(forbidden_hud_markers)
        )
    missing_ui_shim_markers = sorted(
        marker for marker in REQUIRED_CGAME_UI_SHIM_MARKERS if marker not in cgame_ui_shim_text
    )
    if missing_ui_shim_markers:
        fail(
            "cgame UI compatibility layer must remain reject-only under the shared SP UI mandate; "
            "missing marker(s): "
            + ", ".join(missing_ui_shim_markers)
        )

    missing_dead_menu_abi_markers = []
    for rel, markers in REQUIRED_CGAME_DEAD_MENU_ABI_MARKERS.items():
        text = (repo_root / rel).read_text(encoding="utf-8", errors="ignore")
        for marker in markers:
            if marker not in text:
                missing_dead_menu_abi_markers.append(f"{rel}: {marker}")
    if missing_dead_menu_abi_markers:
        fail(
            "cgame menu ABI must remain dead compatibility and route constants through shared SP code/ui: "
            + ", ".join(missing_dead_menu_abi_markers)
        )
    missing_scoreboard_markers = sorted(
        marker
        for marker in REQUIRED_CGAME_SCOREBOARD_MARKERS
        if marker not in cgame_hud_text and marker not in cgame_console_text
    )
    if missing_scoreboard_markers:
        fail(
            "Holomatch score display must use the EF score overlay instead of parser scoreboard menus; "
            "missing marker(s): "
            + ", ".join(missing_scoreboard_markers)
        )
    missing_parser_dead_markers = sorted(
        marker for marker in REQUIRED_CGAME_PARSER_DEAD_MARKERS if marker not in cgame_main_text
    )
    if missing_parser_dead_markers:
        fail(
            "Holomatch cgame parser entry points must stay reject/data-only under the shared SP UI mandate; "
            "missing marker(s): "
            + ", ".join(missing_parser_dead_markers)
        )
    cgame_players_text = (repo_root / "codemp" / "cgame" / "cg_players.c").read_text(
        encoding="utf-8", errors="ignore"
    )
    if REQUIRED_CGAME_FLAG_TAG_SKIP_MARKER not in cgame_players_text:
        fail(
            "Holomatch cgame player setup must skip inherited flag-carrier tag probes: "
            + REQUIRED_CGAME_FLAG_TAG_SKIP_MARKER
        )
    shared_hud_header = repo_root / "code" / "cgame" / "cg_ef_hud_shared.h"
    if not shared_hud_header.is_file():
        fail("missing shared EF cgame HUD layout header: code/cgame/cg_ef_hud_shared.h")
    shared_hud_text = shared_hud_header.read_text(encoding="utf-8", errors="ignore")
    if REQUIRED_SHARED_CGAME_HUD_HEADER not in cgame_hud_text:
        fail(
            "Holomatch cgame HUD layout must come from shared code/cgame header: "
            + REQUIRED_SHARED_CGAME_HUD_HEADER
        )
    if "static stefxHolomatchHudGraphic_t stefxHolomatchHud[STEFX_HUD_MAX]" in cgame_hud_text:
        fail("Holomatch cgame HUD layout table must not live directly in codemp/cgame")
    if (
        "static stefxHolomatchHudGraphic_t stefxHolomatchHud[STEFX_HUD_MAX]" not in shared_hud_text
        or "gfx/interface/healthcap1" not in shared_hud_text
        or "gfx/interface/ammolowercap2" not in shared_hud_text
    ):
        fail("shared EF cgame HUD layout header is missing the expected EF interface table")
    if (
        "trap_R_DrawStretchPic( hud->x,\n"
        "\t\thud->y,\n"
        "\t\thud->width,\n"
        "\t\thud->height,\n"
        "\t\t0.0f,\n"
        "\t\t1.0f,\n"
        "\t\t1.0f,\n"
        "\t\t0.0f,\n"
        "\t\thud->graphic );"
    ) not in cgame_hud_text:
        fail("Holomatch EF interface HUD must use V-corrected direct DDS UVs 0,1,1,0")

    renderer_backend_text = (repo_root / "codemp" / "renderer" / "tr_backend.cpp").read_text(
        encoding="utf-8", errors="ignore"
    )
    sp_renderer_backend_text = (repo_root / "code" / "renderer" / "tr_backend.cpp").read_text(
        encoding="utf-8", errors="ignore"
    )
    mismatched_renderer_functions = []
    for function_name in REQUIRED_RENDERER_SP_WHOLESALE_FUNCTIONS:
        mp_function = extract_cpp_function(renderer_backend_text, function_name)
        sp_function = extract_cpp_function(sp_renderer_backend_text, function_name)
        if mp_function != sp_function:
            mismatched_renderer_functions.append(function_name)
    if mismatched_renderer_functions:
        fail(
            "Holomatch renderer 2D backend must copy SP function bodies exactly: "
            + ", ".join(mismatched_renderer_functions)
        )
    for marker in REQUIRED_RENDERER_SP_WHOLESALE_MARKERS:
        if marker not in renderer_backend_text:
            fail(f"Holomatch renderer SP-wholesale marker missing from MP backend: {marker}")
    missing_solid_fill_markers = []
    for rel, markers in REQUIRED_RENDERER_SOLID_FILL_MARKERS.items():
        text = (repo_root / rel).read_text(encoding="utf-8", errors="ignore")
        for marker in markers:
            if marker not in text:
                missing_solid_fill_markers.append(f"{rel}: {marker}")
    if missing_solid_fill_markers:
        fail(
            "Holomatch renderer must force Xbox solid fill mode through the shared fakeGL overlay path: "
            + ", ".join(missing_solid_fill_markers)
        )
    missing_mdr_frame_clamp_markers = []
    for rel, markers in REQUIRED_RENDERER_MDR_FRAME_CLAMP_MARKERS.items():
        text = (repo_root / rel).read_text(encoding="utf-8", errors="ignore")
        for marker in markers:
            if marker not in text:
                missing_mdr_frame_clamp_markers.append(f"{rel}: {marker}")
    if missing_mdr_frame_clamp_markers:
        fail(
            "Holomatch renderer must clamp EF MDR animation frames before culling/drawing bodies: "
            + ", ".join(missing_mdr_frame_clamp_markers)
        )
    missing_shader_manifest_markers = []
    for rel, markers in REQUIRED_RENDERER_SHADER_MANIFEST_MARKERS.items():
        text = (repo_root / rel).read_text(encoding="utf-8", errors="ignore")
        for marker in markers:
            if marker not in text:
                missing_shader_manifest_markers.append(f"{rel}: {marker}")
    if missing_shader_manifest_markers:
        fail(
            "Holomatch renderer must load shader scripts from the packaged console manifest instead of loose list fallback: "
            + ", ".join(missing_shader_manifest_markers)
        )
    missing_image_upload_markers = []
    for rel, markers in REQUIRED_RENDERER_IMAGE_UPLOAD_MARKERS.items():
        text = (repo_root / rel).read_text(encoding="utf-8", errors="ignore")
        for marker in markers:
            if marker not in text:
                missing_image_upload_markers.append(f"{rel}: {marker}")
        if re.search(r"R_CreateImage\s*\([^;]*GL_LIN_RGBA8", text, re.S):
            missing_image_upload_markers.append(f"{rel}: legacy GL_LIN_RGBA8 screen upload path")
    if missing_image_upload_markers:
        fail(
            "Holomatch renderer image upload path must stay aligned with SP/fakegl texture handling: "
            + ", ".join(missing_image_upload_markers)
        )
    missing_sp_texture_policy_markers = []
    for rel, markers in REQUIRED_RENDERER_SP_TEXTURE_POLICY_MARKERS.items():
        text = (repo_root / rel).read_text(encoding="utf-8", errors="ignore")
        for marker in markers:
            if marker not in text:
                missing_sp_texture_policy_markers.append(f"{rel}: {marker}")
    if missing_sp_texture_policy_markers:
        fail(
            "Holomatch renderer must keep the MP texture path aligned with SP Xbox upload policy: "
            + ", ".join(missing_sp_texture_policy_markers)
        )
    missing_capture_markers = []
    for rel, markers in REQUIRED_RENDERER_CAPTURE_MARKERS.items():
        text = (repo_root / rel).read_text(encoding="utf-8", errors="ignore")
        for marker in markers:
            if marker not in text:
                missing_capture_markers.append(f"{rel}: {marker}")
    if missing_capture_markers:
        fail(
            "Holomatch MP visual proof must try the render-target XGWrite path before raw/log fallback: "
            + ", ".join(missing_capture_markers)
        )

    missing_filesystem_markers = []
    for rel, markers in REQUIRED_HOLOMATCH_FILESYSTEM_MARKERS.items():
        text = (repo_root / rel).read_text(encoding="utf-8", errors="ignore")
        for marker in markers:
            if marker not in text:
                missing_filesystem_markers.append(f"{rel}: {marker}")
    if missing_filesystem_markers:
        fail(
            "Holomatch console filesystem must publish loaded PK3 lists for sv_pure: "
            + ", ".join(missing_filesystem_markers)
        )

    missing_model_markers = []
    for rel, marker in REQUIRED_HOLOMATCH_MODEL_MARKERS.items():
        text = (repo_root / rel).read_text(encoding="utf-8", errors="ignore")
        if marker not in text:
            missing_model_markers.append(f"{rel}: {marker}")
    if missing_model_markers:
        fail(
            "Holomatch local/default player model must be EF-native rather than inherited: "
            + ", ".join(missing_model_markers)
        )

    missing_bot_setup_markers = []
    for rel, markers in REQUIRED_HOLOMATCH_BOT_SETUP_MARKERS.items():
        text = (repo_root / rel).read_text(encoding="utf-8", errors="ignore")
        for marker in markers:
            if marker not in text:
                missing_bot_setup_markers.append(f"{rel}: {marker}")
    if missing_bot_setup_markers:
        fail(
            "Holomatch bot setup must treat botlib's BLERR_NOERROR return code as success: "
            + ", ".join(missing_bot_setup_markers)
        )

    missing_input_markers = []
    for rel, markers in REQUIRED_HOLOMATCH_INPUT_MARKERS.items():
        text = (repo_root / rel).read_text(encoding="utf-8", errors="ignore")
        for marker in markers:
            if marker not in text:
                missing_input_markers.append(f"{rel}: {marker}")
    if missing_input_markers:
        fail(
            "Holomatch Xbox input must use the shared SP-style early device-init/deadzone path: "
            + ", ".join(missing_input_markers)
        )
    main_console_text = (repo_root / "codemp" / "win32" / "win_main_console.cpp").read_text(
        encoding="utf-8", errors="ignore"
    )
    if "Direct3D_SetPushBufferSize(1024*1024, 128*1024)" in main_console_text:
        fail("Holomatch MP main must leave pushbuffer sizing to fakegl, matching the SP path")

    missing_sound_markers = []
    for rel, markers in REQUIRED_HOLOMATCH_SOUND_MARKERS.items():
        text = (repo_root / rel).read_text(encoding="utf-8", errors="ignore")
        for marker in markers:
            if marker not in text:
                missing_sound_markers.append(f"{rel}: {marker}")
    if missing_sound_markers:
        fail(
            "Holomatch Xbox sound must keep the EF/SP hardened handle/entity guard path: "
            + ", ".join(missing_sound_markers)
        )
    sound_text = (repo_root / "codemp" / "client" / "snd_dma_console.cpp").read_text(
        encoding="utf-8", errors="ignore"
    )
    if re.search(r"sfxHandle\s*>\s*MAX_SFX", sound_text):
        fail("Holomatch MP sound still permits one-past-end MAX_SFX handles")
    if re.search(r"entityNum\s*>\s*MAX_GENTITIES", sound_text):
        fail("Holomatch MP sound still permits one-past-end MAX_GENTITIES entity numbers")

    missing_combat_markers = []
    for rel, markers in REQUIRED_HOLOMATCH_COMBAT_MARKERS.items():
        text = (repo_root / rel).read_text(encoding="utf-8", errors="ignore")
        for marker in markers:
            if marker not in text:
                missing_combat_markers.append(f"{rel}: {marker}")
    if missing_combat_markers:
        fail(
            "Holomatch bot-match combat must keep Phaser damage, death, scoring, and respawn proof markers: "
            + ", ".join(missing_combat_markers)
        )
    combat_sources = {
        "codemp/game/ai_main.c": (repo_root / "codemp" / "game" / "ai_main.c").read_text(
            encoding="utf-8", errors="ignore"
        ),
        "codemp/cgame/cg_ents.c": (repo_root / "codemp" / "cgame" / "cg_ents.c").read_text(
            encoding="utf-8", errors="ignore"
        ),
    }
    forbidden_combat_markers = sorted(
        f"{rel}: {marker}"
        for rel, text in combat_sources.items()
        for marker in FORBIDDEN_HOLOMATCH_COMBAT_MARKERS
        if marker in text
    )
    if forbidden_combat_markers:
        fail(
            "Holomatch bots must keep alternate fire enabled; remove primary-fire-only workaround marker(s): "
            + ", ".join(forbidden_combat_markers)
        )

    bridge_text = (repo_root / "codemp" / "ui" / "ui_stefx_spbridge.cpp").read_text(
        encoding="utf-8", errors="ignore"
    )
    lifecycle_text = (repo_root / "code" / "ui" / "ui_ef_lifecycle.cpp").read_text(
        encoding="utf-8", errors="ignore"
    )
    missing_bridge = sorted(marker for marker in REQUIRED_BRIDGE_MARKERS if marker not in bridge_text)
    if missing_bridge:
        fail(
            "MP UI bridge must forward VM routing into shared code/ui lifecycle; missing marker(s): "
            + ", ".join(missing_bridge)
        )
    bad_bridge = sorted(marker for marker in FORBIDDEN_BRIDGE_MARKERS if marker in bridge_text)
    if bad_bridge:
        fail(
            "MP UI bridge still owns UI behavior/state instead of shared code/ui: "
            + ", ".join(bad_bridge)
        )
    missing_framework_markers = []
    for rel, marker in REQUIRED_SP_UI_FRAMEWORK_MP_MARKERS.items():
        text = (repo_root / rel).read_text(encoding="utf-8", errors="ignore")
        if marker not in text:
            missing_framework_markers.append(f"{rel}: {marker}")
    if missing_framework_markers:
        fail(
            "MP UI framework responsibilities must live in shared SP code/ui source(s): "
            + ", ".join(missing_framework_markers)
        )
    missing_lifecycle = sorted(marker for marker in REQUIRED_LIFECYCLE_MARKERS if marker not in lifecycle_text)
    if missing_lifecycle:
        fail(
            "shared code/ui lifecycle is missing MP VM command routing marker(s): "
            + ", ".join(missing_lifecycle)
        )
    bad_lifecycle_menu_cache = sorted(
        pattern for pattern in FORBIDDEN_LIFECYCLE_MENU_CACHE_PATTERNS if re.search(pattern, lifecycle_text)
    )
    if bad_lifecycle_menu_cache:
        fail(
            "shared code/ui Holomatch lifecycle must not re-enter script menu cache/loading: "
            + ", ".join(bad_lifecycle_menu_cache)
        )

    dead_code = verify_legacy_source_guards(repo_root)
    sp_sources = active_project_sources(repo_root / "code" / "x_exe" / "x_exe.vcproj")
    missing_sp_shared = sorted(REQUIRED_SP_SHARED_UI_SOURCES - set(sp_sources))
    if missing_sp_shared:
        fail(
            "SP/co-op default.xbe project is missing shared EF UI source(s): "
            + ", ".join(missing_sp_shared)
        )

    missing_sp_framework = sorted(REQUIRED_SP_UI_FRAMEWORK_SOURCES - set(sp_sources))
    if missing_sp_framework:
        fail(
            "SP/co-op default.xbe project is missing SP UI framework source(s): "
            + ", ".join(missing_sp_framework)
        )

    return {
        "solutionHasOnlyXUi": True,
        "uiMandateUniformSpCodeUi": True,
        "mpUiLocalHeaderRetired": True,
        "mpRuntimeUiLocalIncludes": 0,
        "mpCodempUiBehaviorSources": ["../ui/ui_stefx_spbridge.cpp"],
        "mpLegacyUiBehaviorSources": 0,
        "xUiSources": x_ui_sources,
        "xUiSharedHeaders": sorted(REQUIRED_X_UI_HEADERS),
        "spSharedUiSources": sorted(REQUIRED_SP_SHARED_UI_SOURCES),
        "spUiFrameworkSources": sorted(REQUIRED_SP_UI_FRAMEWORK_SOURCES),
        "mpCompiledSpUiFrameworkSources": sorted(REQUIRED_SP_UI_FRAMEWORK_MP_MARKERS),
        "mpVmDispatchInSharedCodeUi": True,
        "cgameUiShim": REQUIRED_CGAME_SOURCE,
        "cgameUiShimRejectOnly": True,
        "cgameHolomatchHudBypass": True,
        "cgameScoreboardParserBypass": True,
        "cgameParserEntryPointsDead": True,
        "sharedEfCgameHudLayout": REQUIRED_SHARED_CGAME_HUD_HEADER,
        "rendererSpStyle2DProjection": True,
        "rendererSp2DWholesaleFunctions": list(REQUIRED_RENDERER_SP_WHOLESALE_FUNCTIONS),
        "rendererSolidFillReset": True,
        "rendererMdrFrameClamp": True,
        "rendererPackagedShaderManifest": True,
        "rendererSpScreenUploadPath": True,
        "rendererSpTexturePolicy": True,
        "holomatchDefaultPlayerModel": "munro/default",
        "inputSpEarlyDeviceInit": True,
        "inputJoyDeadzoneDefault": "0.18",
        "soundEfSpHardenedGuards": True,
        "soundSpAttenuationPolicy": True,
        "combatPhaserDamageProof": True,
        **dead_code,
    }


def verify_pk3(pk3: Path | None) -> dict[str, object]:
    if pk3 is None:
        return {"checked": False}
    if not pk3.is_file():
        fail(f"Holomatch UI package check failed; missing PK3: {pk3}")

    with zipfile.ZipFile(pk3, "r") as zf:
        actual_names = zf.namelist()
        name_lookup = {norm_path(name): name for name in actual_names}
        names = [norm_path(name) for name in actual_names]
        legacy_entries = sorted(
            name
            for name in names
            if name.startswith("ui/jamp/")
            or name in {"ui/jahud.txt", "ui/jampmenus.txt", "ui/jampingame.txt", "ui/testhud.menu"}
            or (name.startswith("ui/") and (name.endswith(".menu") or name.endswith(".txt")))
        )
        if legacy_entries:
            fail("xbox1.pk3 contains UI parser scripts: " + ", ".join(legacy_entries[:16]))

        missing_hud = sorted(REQUIRED_INTERFACE_HUD_DDS - set(names))
        if missing_hud:
            fail("xbox1.pk3 is missing shared EF/SP interface HUD DDS assets: " + ", ".join(missing_hud))

        missing_nav = sorted(REQUIRED_HOLOMATCH_NAV_FILES - set(names))
        if missing_nav:
            fail("xbox1.pk3 is missing official EF Holomatch navigation file(s): " + ", ".join(missing_nav))

        if REQUIRED_XBOX_EFFECTS_IMAGE not in name_lookup:
            fail(
                "xbox1.pk3 is missing Xbox DirectSound effects image: "
                + REQUIRED_XBOX_EFFECTS_IMAGE
            )
        effects_image_data = zf.read(name_lookup[REQUIRED_XBOX_EFFECTS_IMAGE])
        if len(effects_image_data) <= 0:
            fail("xbox1.pk3 contains an empty Xbox DirectSound effects image")

        original_image_entries = sorted(
            name for name in names if Path(name).suffix.lower() in ORIGINAL_IMAGE_EXTS
        )
        if original_image_entries:
            fail(
                "xbox1.pk3 must be DDS-only; found original image entries: "
                + ", ".join(original_image_entries[:16])
            )

        dds_entries = sorted(name for name in names if Path(name).suffix.lower() == ".dds")
        if not dds_entries:
            fail("xbox1.pk3 does not contain any DDS texture entries")

        dds_formats: dict[str, int] = {}
        invalid_formats: list[str] = []
        forbidden_formats: list[str] = []
        non_bgra32_gfx: list[str] = []
        for dds_entry in dds_entries:
            fmt = dds_format(zf.read(name_lookup[dds_entry])[:128])
            dds_formats[fmt] = dds_formats.get(fmt, 0) + 1
            if fmt == "invalid":
                invalid_formats.append(dds_entry)
            elif fmt not in {"DXT1", "BGRA32", "RGB565"}:
                forbidden_formats.append(f"{dds_entry}={fmt}")
            if dds_entry.startswith("gfx/") and fmt != "BGRA32":
                non_bgra32_gfx.append(f"{dds_entry}={fmt}")

        if invalid_formats:
            fail("xbox1.pk3 contains invalid DDS file(s): " + ", ".join(invalid_formats[:16]))
        if forbidden_formats:
            fail(
                "xbox1.pk3 contains unsupported DDS format(s); OG Xbox Holomatch allows DXT1, RGB565, BGRA32 only: "
                + ", ".join(forbidden_formats[:16])
            )
        if non_bgra32_gfx:
            fail("xbox1.pk3 gfx/ UI/HUD DDS entries must be BGRA32: " + ", ".join(non_bgra32_gfx[:16]))

        shader_entries = sorted(
            name.rsplit("/", 1)[-1]
            for name in names
            if name.startswith("scripts/") and name.endswith(".shader")
        )
        shader_list_path = "scripts/_console_shader_list_"
        if shader_entries and shader_list_path not in names:
            fail("xbox1.pk3 contains shader scripts but no scripts/_console_shader_list_")
        if shader_entries:
            list_name = zf.namelist()[names.index(shader_list_path)]
            listed_shaders = sorted(
                line.strip().lower()
                for line in zf.read(list_name).decode("ascii", errors="replace").splitlines()
                if line.strip()
            )
            if listed_shaders != shader_entries:
                missing = sorted(set(shader_entries) - set(listed_shaders))
                extra = sorted(set(listed_shaders) - set(shader_entries))
                fail(
                    "xbox1.pk3 shader list does not match packaged shader scripts; "
                    f"missing={missing[:8]} extra={extra[:8]}"
                )

        required_support_lists = {
            "scripts/_console_bot_list_": ["bots.txt"],
            "scripts/_console_arena_list_": ["arenas.txt"],
        }
        missing_support_lists: list[str] = []
        for list_path, expected_entries in required_support_lists.items():
            if list_path not in names:
                missing_support_lists.append(list_path)
                continue
            list_name = zf.namelist()[names.index(list_path)]
            listed_entries = sorted(
                line.strip().lower()
                for line in zf.read(list_name).decode("ascii", errors="replace").splitlines()
                if line.strip()
            )
            expected = sorted(expected_entries)
            if listed_entries != expected:
                missing_support_lists.append(
                    f"{list_path}: expected={expected} actual={listed_entries}"
                )
        if missing_support_lists:
            fail(
                "xbox1.pk3 is missing Holomatch console file-list support entries: "
                + ", ".join(missing_support_lists)
            )

        manifest = {}
        if "xbox_patch_manifest.json" in names:
            manifest_name = zf.namelist()[names.index("xbox_patch_manifest.json")]
            manifest = json.loads(zf.read(manifest_name).decode("ascii"))
            if manifest.get("uiScriptCount", 0) != 0 or manifest.get("uiScripts"):
                fail("xbox1.pk3 manifest reports packaged UI scripts")
            if manifest.get("ddsOnly") is not True:
                fail("xbox1.pk3 manifest is not DDS-only")
            if manifest.get("alphaTextureFormat") != "bgra32":
                fail("xbox1.pk3 manifest must use bgra32 alpha textures for OG Xbox")
            if manifest.get("preservedOriginalTextures"):
                fail("xbox1.pk3 manifest reports preserved original texture fallback(s)")
            if manifest.get("skippedAlphaTextures"):
                fail("xbox1.pk3 manifest reports skipped alpha texture(s)")
            effects_image = manifest.get("effectsImage") or {}
            if effects_image.get("path") != REQUIRED_XBOX_EFFECTS_IMAGE:
                fail("xbox1.pk3 manifest does not report sound/dsstdfx.bin as the effects image")
            if effects_image.get("bytes") != len(effects_image_data):
                fail(
                    "xbox1.pk3 manifest effects image byte count does not match package: "
                    f"manifest={effects_image.get('bytes')} package={len(effects_image_data)}"
                )
            patched_aas = manifest.get("patchedAasChecksums") or {}
            aas_checksum = patched_aas.get(REQUIRED_HOLOMATCH_AAS)
            if not isinstance(aas_checksum, int) or aas_checksum <= 0:
                fail("xbox1.pk3 manifest does not report patched hm_borg1 AAS checksum")
            bsp_checksums = [
                item.get("xboxBspChecksum")
                for item in manifest.get("bspOptimizations", [])
                if item.get("map") == "hm_borg1"
            ]
            if not bsp_checksums or bsp_checksums[0] != aas_checksum:
                fail(
                    "xbox1.pk3 manifest AAS checksum does not match optimized hm_borg1 BSP checksum: "
                    f"aas={aas_checksum} bsp={bsp_checksums[:1]}"
                )
            aas_data = zf.read(name_lookup[REQUIRED_HOLOMATCH_AAS])
            if len(aas_data) < 12:
                fail("xbox1.pk3 hm_borg1 AAS file is too small for a header")
            aas_ident, aas_version, aas_header_checksum = struct.unpack_from("<4sII", aas_data, 0)
            if aas_ident != AAS_IDENT or aas_version not in (4, 5):
                fail(
                    "xbox1.pk3 hm_borg1 AAS header is not an Elite Force AAS file: "
                    f"ident={aas_ident!r} version={aas_version}"
                )
            if aas_version == 5:
                decoded_checksum_bytes = bytearray(aas_data[8:12])
                for index in range(len(decoded_checksum_bytes)):
                    decoded_checksum_bytes[index] ^= (index * 119) & 0xFF
                aas_header_checksum = struct.unpack("<I", decoded_checksum_bytes)[0]
            if aas_header_checksum != aas_checksum:
                fail(
                    "xbox1.pk3 hm_borg1 AAS header checksum does not match manifest: "
                    f"header={aas_header_checksum} manifest={aas_checksum}"
                )
        else:
            fail("xbox1.pk3 is missing xbox_patch_manifest.json")

        direct_bot_missing: list[str] = []
        direct_bot_skin_textures: set[str] = set()
        bots_entry = "scripts/bots.txt"
        if bots_entry not in name_lookup:
            direct_bot_missing.append(bots_entry)
            bots_text = ""
        else:
            bots_text = zf.read(name_lookup[bots_entry]).decode("ascii", errors="ignore").lower()

        for bot_name, spec in DIRECT_HOLOMATCH_BOTS.items():
            bot_pattern = (
                r"name\s+" + re.escape(bot_name) + r"\b"
                r"(?:(?!\n\{).)*?"
                r"model\s+" + re.escape(bot_name) + r"\b"
                r"(?:(?!\n\{).)*?"
                r"aifile\s+bots/" + re.escape(Path(spec["aifile"]).name)
            )
            if bots_text and not re.search(bot_pattern, bots_text, re.DOTALL):
                direct_bot_missing.append(f"{bots_entry}:{bot_name}")

            model_dir = norm_path(spec["model"])
            required_model_entries = {
                f"{model_dir}/{name}" for name in DIRECT_BOT_MODEL_FILES
            }
            required_model_entries.update(
                f"{model_dir}/{name}" for name in DIRECT_BOT_SKIN_FILES
            )
            required_model_entries.update(
                f"{model_dir}/{name}" for name in DIRECT_BOT_ICON_DDS
            )
            required_model_entries.add(norm_path(spec["aifile"]))

            for rel in sorted(required_model_entries):
                if rel not in name_lookup:
                    direct_bot_missing.append(rel)

            for skin_name in sorted(DIRECT_BOT_SKIN_FILES):
                skin_rel = f"{model_dir}/{skin_name}"
                if skin_rel not in name_lookup:
                    continue
                direct_bot_skin_textures.update(skin_texture_refs(zf.read(name_lookup[skin_rel])))

        for texture in sorted(direct_bot_skin_textures):
            if texture not in name_lookup:
                direct_bot_missing.append(texture)
            elif Path(texture).suffix.lower() != ".dds":
                direct_bot_missing.append(texture)

        if direct_bot_missing:
            fail(
                "xbox1.pk3 is missing direct Holomatch bot asset(s): "
                + ", ".join(sorted(set(direct_bot_missing))[:24])
            )

        missing_weapon_models = sorted(
            rel for rel in DIRECT_HOLOMATCH_WEAPON_MODEL_FILES if rel not in name_lookup
        )
        if missing_weapon_models:
            fail(
                "xbox1.pk3 is missing direct Holomatch weapon model asset(s): "
                + ", ".join(missing_weapon_models[:24])
            )
        missing_pickup_models = sorted(
            rel for rel in DIRECT_HOLOMATCH_PICKUP_MODEL_FILES if rel not in name_lookup
        )
        if missing_pickup_models:
            fail(
                "xbox1.pk3 is missing direct Holomatch pickup model asset(s): "
                + ", ".join(missing_pickup_models[:24])
            )

    return {
        "checked": True,
        "pk3": str(pk3),
        "uiScriptCount": 0,
        "legacyUiEntries": 0,
        "ddsEntries": len(dds_entries),
        "ddsFormats": dds_formats,
        "originalImageEntries": 0,
        "shaderScriptCount": len(shader_entries),
        "consoleSupportLists": sorted(required_support_lists),
        "patchedAasChecksums": manifest.get("patchedAasChecksums", {}),
        "effectsImage": manifest.get("effectsImage", {}),
        "directBotModels": sorted(DIRECT_HOLOMATCH_BOTS),
        "directBotSkinTextureCount": len(direct_bot_skin_textures),
        "directWeaponModelCount": len(DIRECT_HOLOMATCH_WEAPON_MODEL_FILES),
        "directPickupModelCount": len(DIRECT_HOLOMATCH_PICKUP_MODEL_FILES),
    }


def verify_stage(stage_baseef: Path | None, allow_original_images: bool) -> dict[str, object]:
    if stage_baseef is None:
        return {"checked": False}
    ui_dir = stage_baseef / "ui"
    if not ui_dir.is_dir():
        return {"checked": True, "stageUiScripts": 0}

    scripts = sorted(
        path
        for path in ui_dir.rglob("*")
        if path.is_file() and path.suffix.lower() in {".menu", ".txt"}
    )
    if scripts:
        rel = [norm_path(path.relative_to(stage_baseef).as_posix()) for path in scripts[:16]]
        fail("staged BaseEF still contains UI parser scripts: " + ", ".join(rel))

    original_images = sorted(
        path
        for path in stage_baseef.rglob("*")
        if path.is_file() and path.suffix.lower() in ORIGINAL_IMAGE_EXTS
    )
    if original_images and not allow_original_images:
        rel = [norm_path(path.relative_to(stage_baseef).as_posix()) for path in original_images[:16]]
        fail("staged BaseEF must be DDS-only; found loose original image file(s): " + ", ".join(rel))
    return {
        "checked": True,
        "stageUiScripts": 0,
        "stageOriginalImages": len(original_images),
        "stageOriginalImagesAllowed": allow_original_images,
    }


def verify_xbe(xbe: Path | None) -> dict[str, object]:
    if xbe is None:
        return {"checked": False}
    if not xbe.is_file():
        fail(f"Holomatch UI XBE check failed; missing XBE: {xbe}")

    data = xbe.read_bytes()
    lower = data.lower()
    hits = sorted(value for value in FORBIDDEN_XBE_STRINGS if value.lower().encode("ascii") in lower)
    if hits:
        fail("efmp.xbe contains legacy UI/menu marker(s): " + ", ".join(hits))
    forbidden_combat_hits = sorted(
        value
        for value in FORBIDDEN_HOLOMATCH_COMBAT_MARKERS
        if value.encode("ascii") in data
    )
    if forbidden_combat_hits:
        fail(
            "efmp.xbe contains primary-fire-only bot workaround marker(s): "
            + ", ".join(forbidden_combat_hits)
        )

    required = {
        b"STEFX_HM: UI mandate active; uniform SP code/ui owns Holomatch UI",
        b"STEFX_HM: direct Holomatch startup bypasses menus; loading hm_borg1 from command line",
        b"STEFX_HM: UI mandate enforced; MP legacy menus stay dead and SP code/ui owns all Holomatch UI behavior",
        b"STEFX_HM: SP EF UI lifecycle initialized from code/ui",
        b"STEFX_HM: SP EF UI lifecycle initialized from code/ui; no script menu cache; codemp/ui remains adapter-only",
        b"STEFX_HM: SP EF UI cache skipped legacy renderer font; EF prop-font atlas owns Holomatch text",
        b"STEFX_HM: SP EF UI VM dispatch active from shared code/ui",
        b"STEFX_HM: MP UI bridge is syscall adapter only; SP code/ui owns UI framework state and rendering",
        b"STEFX_HM: SP UI atoms active in efmp.xbe; MP bridge owns only syscall plumbing",
        b"STEFX_HM: SP UI main framework active in efmp.xbe; text/UI state owned by shared code/ui",
        b"STEFX_HM: SP UI shared framework compiled in efmp.xbe; parser compatibility is reject-only",
        b"STEFX_HM: SP UI shared framework rejected script menu route op=",
        b"STEFX: RB_XboxForce2DOverlayState where=",
        b"STEFX_FRONTEND_2D_BACKEND shader=",
        b"STEFX: RB_StretchPic shader=",
        b"STEFX_OVERLAY_DEVICE_STATE",
        b"STEFX_HM: renderer clamped EF MDR frame model=",
        b"STEFX_HM: renderer loaded shader manifest",
        b"STEFX_HM: renderer using SP-style GL_RGBA screen texture; legacy MP GL_LIN_RGBA8 path disabled",
        b"STEFX_HM: MP renderer using SP Xbox Upload32 caps",
        b"STEFX_HM: MP renderer using SP Xbox dynamic glow policy off",
        b"STEFX_HM: MP renderer using SP Xbox r_texturebits=0 component upload policy",
        b"STEFX_HM: MP renderer using SP Xbox r_subdivisions=64 policy",
        b"STEFX_HM: BGRA32 DDS direct Xbox upload",
        b"STEFX_HM: renderer skipped inherited zero-size registration StretchPic prime",
        b"STEFX_HM: renderer screenshot render-target XGWrite enabled for MP visual proof",
        b"STEFX_HM: SV pure loaded PK3s checksums=",
        b"STEFX_HM: client default userinfo model is EF Holomatch model='munro/default'",
        b"STEFX_HM: cgame UI parser shim is reject-only; shared SP UI owns menus",
        b"STEFX_HM: cgame HUD menu system disabled for EF MP; using EF SP interface HUD",
        b"STEFX_HM: cgame ignored deprecated MP HUD menu load request",
        b"STEFX_HM: cgame skipped inherited menu asset cache for shared SP UI path",
        b"STEFX_HM: cgame skipped inherited parser vehicle HUD; EF Holomatch HUD owns status",
        b"STEFX_HM: cgame skipped inherited parser stats HUD; EF Holomatch HUD owns status",
        b"STEFX_HM: cgame EF SP interface HUD V-corrected direct DDS draw path",
        b"STEFX_HM: cgame Holomatch 2D using SP interface-only path; legacy cgame parser HUD bypassed",
        b"STEFX_HM: cgame scoreboard uses EF Holomatch overlay; parser scoreboard bypassed",
        b"STEFX_HM: cgame ignored parser scoreboard scroll; EF score overlay owns scores",
        b"STEFX_HM: cgame ignored deprecated MP loadmenu block; shared SP UI owns menus",
        b"STEFX_HM: cgame score selection stayed data-only; parser menu feeders ignored",
        b"STEFX_HM: cgame parser entry points are dead; shared SP UI owns menus",
        b"STEFX_HM: cgame skipped inherited flag-carrier tag probe for EF Holomatch players",
        b"STEFX_HM: cgame skipped legacy renderer font registration; EF prop-font atlas owns Holomatch text",
        b"STEFX_HM: cgame EF prop fonts loaded=",
        b"STEFX_HM: cgame EF prop text draw",
        b"STEFX_HM: BotAISetup trap_BotLibSetup done result=",
        b"STEFX_HM: official EF AAS package probe map=",
        b"STEFX_HM: official EF AAS botlib checksum var set value=",
        b"STEFX_HM: official EF AAS botlib load result=",
        b"STEFX_HM: fallback bot waypoint inherited CalculatePaths skipped map=",
        b"STEFX_HM: fallback bot waypoint local links done map=",
        b"STEFX_HM: fallback bot waypoint trap path calculation skipped map=",
        b"STEFX_HM: input Plan-B XInitDevices completed before D3D init",
        b"STEFX_HM: MP using SP-style fakegl pushbuffer path; main skipped legacy Direct3D_SetPushBufferSize",
        b"STEFX_HM: input using SP early XInitDevices path; gamepad mask=",
        b"STEFX_HM: first gamepad state port=",
        b"STEFX_HM: input SP no-controller tracking active",
        b"STEFX_HM: input state read failed port=",
        b"STEFX_HM: direct-map input gate cleared splash/controller lock",
        b"STEFX_HM: sound using EF/SP hardened Xbox path; handle/entity guards active",
        b"STEFX_HM: sound using SP Xbox attenuation reference distance=",
        b"STEFX_HM: sound clamped listener count caller=",
        b"STEFX_HM: QAL effects image sound/dsstdfx.bin missing; continuing dry audio",
        b"STEFX_HM: QAL downloaded Xbox effects image bytes=",
        b"STEFX_HM: sound registration active listeners=",
        b"STEFX_HM: sound dropped invalid handle",
        b"STEFX_HM: sound dropped invalid entity",
        b"STEFX_HM: sound local listener clamped active=",
        b"STEFX_HM: server EF Phaser applied damage attacker=",
        b"STEFX_HM: cgame skipped EF moving missile dlight on Xbox renderer weapon=",
        b"STEFX_HM: cgame rendered EF alternate missile safe sprite weapon=",
        b"STEFX_HM: EF SP interface HUD startup armed",
        b"STEFX_HM: EF SP interface HUD startup complete",
        b"STEFX_HM: cgame EF SP interface HUD using DDS native UV draw path",
        b"ef_mp_renderprobe.txt",
    }
    missing = sorted(value.decode("ascii") for value in required if value not in data)
    if missing:
        fail("efmp.xbe is missing SP/EF UI proof marker(s): " + ", ".join(missing))

    return {"checked": True, "xbe": str(xbe), "legacyStringCount": 0}


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="Check the Holomatch MP UI mandate")
    parser.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--pk3", type=Path)
    parser.add_argument("--stage-baseef", type=Path)
    parser.add_argument(
        "--allow-stage-original-images",
        action="store_true",
        help="Allow source image files in a non-runtime BaseEF workspace; never use for CXBX-R stage checks.",
    )
    parser.add_argument("--xbe", type=Path)
    args = parser.parse_args(argv)

    repo_root = args.repo_root.resolve()
    summary = {
        "uiPolicy": "mandated-shared-ef-sp-code-ui-owns-behavior",
        "projects": verify_solution(repo_root),
        "package": verify_pk3(args.pk3.resolve() if args.pk3 else None),
        "stage": verify_stage(
            args.stage_baseef.resolve() if args.stage_baseef else None,
            args.allow_stage_original_images,
        ),
        "xbe": verify_xbe(args.xbe.resolve() if args.xbe else None),
    }
    print(json.dumps(summary, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
