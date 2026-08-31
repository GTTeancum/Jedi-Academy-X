#!/usr/bin/env python3
"""Verify that Holomatch MP uses the shared EF/SP UI path and DDS-only assets."""

from __future__ import annotations

import argparse
import hashlib
import json
import posixpath
import re
import struct
import sys
import zipfile
import zlib
from pathlib import Path
from xml.etree import ElementTree


SOURCE_EXTS = {".c", ".cc", ".cpp", ".cxx", ".asm", ".vsh", ".psh"}
ORIGINAL_IMAGE_EXTS = {".jpg", ".jpeg", ".tga", ".png"}

OFFICIAL_MULTIPLAYER_MAPS = {
    "ctf_and1",
    "ctf_breach",
    "ctf_dn1",
    "ctf_for1",
    "ctf_kln1",
    "ctf_kln2",
    "ctf_neptune",
    "ctf_oldwest",
    "ctf_reservoir",
    "ctf_singularity",
    "ctf_spyglass2",
    "ctf_stasis",
    "ctf_voy1",
    "ctf_voy2",
    "hm_altar",
    "hm_blastradius",
    "hm_borg1",
    "hm_borg2",
    "hm_borg3",
    "hm_borgattack",
    "hm_cam",
    "hm_dn1",
    "hm_dn2",
    "hm_for1",
    "hm_for2",
    "hm_kln1",
    "hm_noon",
    "hm_raven",
    "hm_scav1",
    "hm_temple",
    "hm_voy1",
    "hm_voy2",
    "hm_voy3",
}

REQUIRED_SP_CONTROL_FILES = {
    "client/cl_input.cpp",
    "client/cl_input_hotswap.cpp",
    "client/cl_input_hotswap.h",
    "client/cl_keys.cpp",
    "client/client_ui.h",
    "client/keycodes.h",
    "client/keys.h",
    "qcommon/xb_settings.cpp",
    "qcommon/xb_settings.h",
    "win32/win_input.h",
    "win32/win_input_console.cpp",
    "win32/win_input_rumble.cpp",
    "win32/win_input_xbox.cpp",
}

REQUIRED_SP_CONTROL_BRIDGES = {
    "../client/cl_input_hotswap_sp_bridge.cpp",
    "../client/cl_input_sp_bridge.cpp",
    "../client/cl_keys_sp_bridge.cpp",
    "../client/sp_controls_compat.cpp",
    "../qcommon/xb_settings_sp_bridge.cpp",
    "../win32/win_input_console_sp_bridge.cpp",
    "../win32/win_input_rumble_sp_bridge.cpp",
    "../win32/win_input_xbox.cpp",
}

REQUIRED_SP_SOUND_FILES = {
    "client/snd_ambient.cpp",
    "client/snd_ambient.h",
    "client/snd_dma.cpp",
    "client/snd_dma_console.cpp",
    "client/snd_local.h",
    "client/snd_local_console.h",
    "client/snd_mem.cpp",
    "client/snd_mem_console.cpp",
    "client/snd_mix.cpp",
    "client/snd_music.cpp",
    "client/snd_music.h",
    "client/snd_public.h",
    "win32/snd_fx_img.h",
    "win32/win_qal_xbox.cpp",
    "win32/win_snd.cpp",
    "win32/win_stream_dx8.cpp",
    "xbox_re/re_sound.cpp",
}

REQUIRED_SP_SOUND_PROJECT_SOURCES = {
    "../client/snd_ambient.cpp",
    "../client/snd_dma_console_sp_bridge.cpp",
    "../client/snd_mem_console.cpp",
    "../client/snd_music_sp_bridge.cpp",
    "../client/sp_sound_compat.cpp",
    "../win32/win_qal_xbox_sp_bridge.cpp",
    "../win32/win_stream_dx8.cpp",
    "../xbox_re/re_sound.cpp",
}

FORBIDDEN_DIRECT_SP_SOUND_PROJECT_SOURCES = {
    "../client/snd_dma_console.cpp",
    "../client/snd_music.cpp",
    "../win32/win_qal_xbox.cpp",
}

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
REQUIRED_RENDERER_SOLID_FILL_MARKERS = {
    "code/win32/openjkdf2/fakeglx.cpp": [
        "stefxOverlayActive",
        "D3DRS_FILLMODE, D3DFILL_SOLID",
        "D3DRS_BACKFILLMODE, D3DFILL_SOLID",
        "STEFX_OVERLAY_DEVICE_STATE",
    ],
}
REQUIRED_RENDERER_DDS_UPLOAD_MARKERS = {
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
    "codemp/game/ef_ai/ai_main.c": [
        "int BotAISetupClient(int client, struct bot_settings_s *settings)",
        "trap_BotLoadCharacter(settings->characterfile, settings->skill)",
        "trap_BotLibLoadMap( mapname.string )",
        "errnum = BotInitLibrary();",
        "int BotAIStartFrame(int time)",
    ],
    "codemp/game/ef_game/g_bot.c": [
        "void G_CheckMinimumPlayers( void )",
        "void G_CheckBotSpawn( void )",
        "qboolean G_BotConnect( int clientNum, qboolean restart )",
        'Info_SetValueForKey( userinfo, "characterfile", Info_ValueForKey( botinfo, "aifile" ) );',
        "void G_InitBots( qboolean restart )",
    ],
    "codemp/game/ef_game/g_bot_xbox.cpp": [
        '#include "g_bot.c"',
        "void G_RemoveQueuedBotBegin( int clientNum )",
        "void G_InitBotMetadataOnly( qboolean restart )",
    ],
    "codemp/game/ef_ai_xbox_support.c": [
        "STEFX_HM: official EF bot AI allocation state initialized",
        "STEFX_HM: JA waypoint loader retired; official EF AAS route active",
    ],
    "codemp/server/sv_game.cpp": [
        "STEFX_HM: official EF bot usercmd client=",
        "cmd->forwardmove || cmd->rightmove || cmd->upmove",
        "cmd->buttons & BUTTON_ALT_ATTACK",
    ],
    "codemp/game/ef_ai_compat.h": [
        "static void EF_AI_MirrorCarrierAmmoForOfficialBot(playerState_t *state)",
        "weaponData[weapons[i]].ammoIndex",
        "state->ammo[weapons[i]] = ammoValues[i];",
        "STEFX_HM: official EF bot ammo view mirrored from carrier ammo buckets",
        "static int EF_AI_OfficialWeaponToCarrier(int weapon)",
        "if (weapon >= 11 && weapon <= 19)",
        "return alt ? base + WP_NUM_WEAPONS : base;",
        "static int EF_AI_CarrierWeaponToOfficial(int weapon)",
        "return alt ? base + 10 : base;",
        "static void EF_AI_EA_SelectWeapon(int client, int weapon)",
    ],
}

OFFICIAL_EF_AI_SHA256 = {
    "ai_chat.c": "f9c33a49fa21c0ad203fc4da865fca5bbbbf71b446e5b3ce5ec020943c35eb1f",
    "ai_chat.h": "a6f6790be9fd6e3b9ed9221cd94da5cdbcc9c32ac88cb5de538469c6bfde33e7",
    "ai_cmd.c": "73f24bfa4286aaec88540a786fc95b4077ba3dba7b451a5435309900d21aa94d",
    "ai_cmd.h": "ec7a75c66ec3187e12c2a719243e8b932ca1fb3b73189175d0ce4dc1fc156293",
    "ai_dmnet.c": "1da7940f184a807ba284c48f0cddfc4175eddb4d99a1c10fede2b371f5f4d171",
    "ai_dmnet.h": "ecf04ce9492491a1e2a9881711dbcb6793a3ba93a1d40c6d60d510b6e2682cce",
    "ai_dmq3.c": "b841530903a135e63eb56106fa1bbad682912c83b6add1fdf99e7a41f1ff020d",
    "ai_dmq3.h": "32a88bfcf2cc788a44179165fc7b19d69463e2bdb6c0f25741b6e4ae3a62e974",
    "ai_main.c": "6f0efaa939115a2027230cfb266b94688400373d3436cddd7423f4270567a482",
    "ai_main.h": "6815a64483be95bb371443d0ed5eb6596fd9221ad63d1e9661b51d3262ebbe1e",
    "ai_team.c": "f96421f8c16595a3d0a74ae9211914b9849e73a2ca1c441a92326721125173f0",
    "ai_team.h": "4f9003f804011a0ba4b174e636dcc6d96f528a555b8511f0e73c208e2a8210ca",
    "chars.h": "28e2c0f22d9203e1f237a0ec46de0f20d90acd0a13928132bfc10de0d43b3598",
    "inv.h": "31f2f58791b9e47adf911dae1d5441236dc9d43dff2685fd13f2ab4e3a680b62",
    "match.h": "c7880bd4c2b653514a8aaf406493aa19af5e3377fe61312f1552bfe0bce0f438",
    "syn.h": "9b8e2cb96c979b3a2ded6d0f9a70d39d9af531f596baea5a4d042411771ac831",
}

OFFICIAL_EF_GAME_SHA256 = {
    "g_bot.c": "aa096bbc319b6dfeb1bb07210b046319993a0b93beeae1e9af4e1915638c661c",
}

OFFICIAL_EF_COMBAT_SHA256 = {
    "g_combat.c": "13a5ae0a1f68d9bd99c8f1b5ff2e6b2a1e7b8917d34373a6a1a49ba5d45b375e",
}

OFFICIAL_EF_WEAPON_SHA256 = {
    "g_weapon.c": "20f716a53d5b6d7718886c384665367082c8baf8269e39f231796593da317137",
}

OFFICIAL_EF_MISSILE_SHA256 = {
    "g_missile.c": "f748840109ac1dc78e714c4afce69b464f890dc2a1ca875ddd29da60275e0d12",
}

OFFICIAL_EF_ACTIVE_SHA256 = {
    "g_active.c": "b23c6a54d0ed84b8424a394db9cfb7f4db524deff85648eb36bc8016c9a6485a",
}

OFFICIAL_EF_ITEMS_SHA256 = {
    "g_items.c": "694376d9d1e00e0e0eeedd15c76acdee9f57ab3b7a2af18646bf1b170ed41b8a",
}

OFFICIAL_EF_SHARED_SHA256 = {
    "bg_misc.c": "af1159688f0789fdaa0b3a9e2b340e1202262917bca4f489468304fce8399db4",
}

OFFICIAL_EF_PLAYER_CLASSES = {
    "PC_NOCLASS": 0,
    "PC_INFILTRATOR": 1,
    "PC_SNIPER": 2,
    "PC_HEAVY": 3,
    "PC_DEMO": 4,
    "PC_MEDIC": 5,
    "PC_TECH": 6,
    "PC_BORG": 7,
    "PC_VIP": 8,
    "PC_ACTIONHERO": 9,
}

REQUIRED_OFFICIAL_EF_AI_PROJECT_SOURCES = {
    "../game/ef_ai/ai_main.c",
    "../game/ef_ai/ai_chat.c",
    "../game/ef_ai/ai_cmd.c",
    "../game/ef_ai/ai_dmnet.c",
    "../game/ef_ai/ai_dmq3.c",
    "../game/ef_ai/ai_team.c",
    "../game/ef_ai_xbox_support.c",
    "../game/ef_game/g_bot_xbox.cpp",
    "../game/ef_game/g_combat_xbox.cpp",
    "../game/ef_game/g_weapon_xbox.cpp",
    "../game/ef_game/g_missile_xbox.cpp",
    "../game/ef_game/g_active_xbox.cpp",
    "../game/ef_game/g_items_xbox.cpp",
}

FORBIDDEN_JA_AI_PROJECT_SOURCES = {
    "../game/ai_main.c",
    "../game/ai_util.c",
    "../game/ai_wpnav.c",
    "../game/g_bot.c",
    "../game/g_combat.c",
    "../game/g_weapon.c",
    "../game/g_missile.c",
    "../game/g_active.c",
    "../game/g_items.c",
}

REQUIRED_OFFICIAL_EF_SHARED_PROJECT_SOURCES = {
    "../game/ef_shared/bg_misc_xbox.cpp",
}

FORBIDDEN_JA_SHARED_PROJECT_SOURCES = {
    "../game/bg_misc.c",
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
        "STEFX: IN_Init gamepad mask=",
        "STEFX: first gamepad state port=",
    ],
}

REQUIRED_HOLOMATCH_COMBAT_MARKERS = {
    "codemp/win32/win_main_console.cpp": [
        "+set stefx_hm_directSlice 1",
        "+set g_spawnInvulnerability 0",
        "+map hm_borg1",
        "STEFX_HM: direct Holomatch startup bypasses menus; loading hm_borg1 from command line",
    ],
    "codemp/game/ef_ai/ai_dmq3.c": [
        "void BotChooseWeapon(bot_state_t *bs)",
        "trap_BotChooseBestFightWeapon(bs->ws, bs->inventory, BotUseMeleeWeapon(bs))",
        "void BotDeathmatchAI(bot_state_t *bs, float thinktime)",
        "trap_EA_Attack(bs->client);",
    ],
    "codemp/game/ef_game/g_weapon.c": [
        "#define\tPHASER_DAMAGE",
        "void WP_FirePhaser( gentity_t *ent, qboolean alt_fire )",
        "void FireWeapon( gentity_t *ent, qboolean alt_fire )",
    ],
    "codemp/game/ef_game/g_weapon_xbox.cpp": [
        '#include "g_weapon.c"',
        "STEFX_HM_OfficialDamage",
        "STEFX_HM: official EF weapon dispatcher active",
    ],
    "codemp/game/ef_game/g_missile.c": [
        "void tripwireThink ( gentity_t *ent )",
        "void G_MissileImpact( gentity_t *ent, trace_t *trace )",
        "void G_RunMissile( gentity_t *ent )",
    ],
    "codemp/game/ef_game/g_missile_xbox.cpp": [
        '#include "g_missile.c"',
        "STEFX_HM_OfficialDamage",
        "STEFX_HM: official EF missile simulation active",
    ],
    "codemp/game/ef_game/g_active.c": [
        "void ClientEvents( gentity_t *ent, int oldEventSequence )",
        "void ClientThink_real( gentity_t *ent )",
        "void ClientEndFrame( gentity_t *ent )",
    ],
    "codemp/game/ef_game/g_active_xbox.cpp": [
        '#include "g_active.c"',
        "STEFX_HM_OfficialDamage",
        "STEFX_HM: official EF client activity active with Xbox usercmd boundary",
    ],
    "codemp/game/ef_game/g_items.c": [
        "void Touch_Item (gentity_t *ent, gentity_t *other, trace_t *trace)",
        "void FinishSpawningItem( gentity_t *ent )",
        "void G_RunItem( gentity_t *ent )",
    ],
    "codemp/game/ef_game/g_items_xbox.cpp": [
        '#include "g_items.c"',
        "STEFX_HM_OfficialSpawnItem",
        "STEFX_HM: official EF item lifecycle active",
        "STEFX_HM: retired JA carrier item hook invoked name=",
    ],
    "codemp/game/ef_shared/bg_misc.c": [
        "gitem_t\tbg_itemlist[]",
        "qboolean\tBG_CanItemBeGrabbed( const entityState_t *ent, const playerState_t *ps )",
        "void BG_PlayerStateToEntityState( playerState_t *ps, entityState_t *s, qboolean snap )",
    ],
    "codemp/game/ef_shared/bg_misc_xbox.cpp": [
        '#include "bg_misc.c"',
        "STEFX_HM_OfficialCanItemBeGrabbed",
        "STEFX_HM: official EF shared item table and pickup rules active",
    ],
    "codemp/game/bg_public.h": [
        "#if defined(STEFX_ELITE_FORCE_MP)",
        "char\t\t*world_model;",
        "char\t\t*pickup_name;",
        "BG_CanItemBeGrabbed( const entityState_t *ent, const playerState_t *ps )",
    ],
    "codemp/game/ef_game/g_combat.c": [
        "void AddScore( gentity_t *ent, int score )",
        "void player_die( gentity_t *self, gentity_t *inflictor, gentity_t *attacker, int damage, int meansOfDeath )",
        "void G_Damage( gentity_t *targ, gentity_t *inflictor, gentity_t *attacker,",
        "qboolean G_RadiusDamage ( vec3_t origin, gentity_t *attacker, float damage, float radius,",
    ],
    "codemp/game/ef_game/g_combat_xbox.cpp": [
        '#include "g_combat.c"',
        "STEFX_HM_TranslateDamageFlags",
        "STEFX_HM_OfficialDamage",
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

FORBIDDEN_SYNTHETIC_COMBAT_MARKERS = {
    "ef_mp_smoke_proof.txt",
    "STEFX_HolomatchVerticalSliceProof",
    "STEFX_HolomatchSmokeProofEnabled",
    "STEFX_HolomatchProofClientReady",
    "vertical slice proof applying real server damage",
    "vertical slice proof forcing normal respawn",
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
    "STEFX_HM: SP EF UI lifecycle initialized from code/ui; no script menu cache",
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

AAS_IDENT = b"EAAS"
FORBIDDEN_MP_EFFECTS_IMAGE = "sound/dsstdfx.bin"

REQUIRED_EF_BOTLIB_ROOT_FILES = {
    "botfiles/chars.h",
    "botfiles/fw_items.c",
    "botfiles/fw_weap.c",
    "botfiles/inv.h",
    "botfiles/items.c",
    "botfiles/match.c",
    "botfiles/match.h",
    "botfiles/rnd.c",
    "botfiles/syn.c",
    "botfiles/syn.h",
    "botfiles/teamplay.h",
    "botfiles/weapons.c",
}

OFFICIAL_EF_WEAPON_CONFIG_IDS = {
    "WEAPONINDEX_PHASER": 1,
    "WEAPONINDEX_COMPRESSION": 2,
    "WEAPONINDEX_IMOD": 3,
    "WEAPONINDEX_SCAVENGER": 4,
    "WEAPONINDEX_STASIS": 5,
    "WEAPONINDEX_GRENADELAUNCHER": 6,
    "WEAPONINDEX_TETRION": 7,
    "WEAPONINDEX_QUANTUM": 8,
    "WEAPONINDEX_DREADNOUGHT": 9,
    "WEAPONINDEX_PHASER_ALT": 11,
    "WEAPONINDEX_COMPRESSION_ALT": 12,
    "WEAPONINDEX_IMOD_ALT": 13,
    "WEAPONINDEX_SCAVENGER_ALT": 14,
    "WEAPONINDEX_STASIS_ALT": 15,
    "WEAPONINDEX_GRENADELAUNCHER_ALT": 16,
    "WEAPONINDEX_TETRION_ALT": 17,
    "WEAPONINDEX_QUANTUM_ALT": 18,
    "WEAPONINDEX_DREADNOUGHT_ALT": 19,
}

OFFICIAL_EF_CARRIER_WEAPON_MAP = {
    1: "WP_BRYAR_PISTOL",
    2: "WP_BLASTER",
    3: "WP_DEMP2",
    4: "WP_BOWCASTER",
    5: "WP_FLECHETTE",
    6: "WP_THERMAL",
    7: "WP_DISRUPTOR",
    8: "WP_REPEATER",
    9: "WP_ROCKET_LAUNCHER",
}

OFFICIAL_EF_BOT_ACTION_FLAGS = {
    "ACTION_ATTACK": 0x0001,
    "ACTION_USE": 0x0002,
    "ACTION_RESPAWN": 0x0004,
    "ACTION_JUMP": 0x0008,
    "ACTION_MOVEUP": 0x0008,
    "ACTION_CROUCH": 0x0010,
    "ACTION_MOVEDOWN": 0x0010,
    "ACTION_MOVEFORWARD": 0x0020,
    "ACTION_MOVEBACK": 0x0040,
    "ACTION_MOVELEFT": 0x0080,
    "ACTION_MOVERIGHT": 0x0100,
    "ACTION_DELAYEDJUMP": 0x0200,
    "ACTION_TALK": 0x0400,
    "ACTION_GESTURE": 0x0800,
    "ACTION_WALK": 0x1000,
    "ACTION_ALT_ATTACK": 0x2000,
}

REQUIRED_HOLOMATCH_BOTFILE_ALIASES = {
    "botfiles/bots/long_i.c": "botfiles/bots/biessman_i.c",
}

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


def verify_baseef_source_routing(repo_root: Path) -> dict[str, object]:
    files_header = (repo_root / "codemp" / "qcommon" / "files.h").read_text(
        encoding="utf-8", errors="ignore"
    )
    startup = (repo_root / "codemp" / "win32" / "win_main_console.cpp").read_text(
        encoding="utf-8", errors="ignore"
    )
    boundary = (repo_root / "codemp" / "win32" / "win_sp_renderer_boundary.cpp").read_text(
        encoding="utf-8", errors="ignore"
    )

    required = {
        "codemp/qcommon/files.h": (
            '#if defined(STEFX_ELITE_FORCE_MP)',
            '#define\tBASEGAME\t\t\t"BaseEF"',
        ),
        "codemp/win32/win_main_console.cpp": (
            "+set fs_game BaseEF",
        ),
        "codemp/win32/win_sp_renderer_boundary.cpp": (
            'return va("D:\\\\BaseEF\\\\%s", filename + 5);',
        ),
    }
    texts = {
        "codemp/qcommon/files.h": files_header,
        "codemp/win32/win_main_console.cpp": startup,
        "codemp/win32/win_sp_renderer_boundary.cpp": boundary,
    }
    missing = [
        f"{rel}: {marker}"
        for rel, markers in required.items()
        for marker in markers
        if marker not in texts[rel]
    ]
    if missing:
        fail("Holomatch runtime must route through BaseEF: " + ", ".join(missing))

    return {
        "baseGame": "BaseEF",
        "startupFsGame": "BaseEF",
        "spRelativeReadsRemappedToBaseEF": True,
    }


def verify_sp_renderer_wholesale(repo_root: Path) -> dict[str, object]:
    sp_dir = repo_root / "code" / "renderer"
    mp_dir = repo_root / "codemp" / "renderer"
    common_text = (repo_root / "codemp" / "qcommon" / "common.cpp").read_text(
        encoding="utf-8", errors="ignore"
    )
    boundary_text = (repo_root / "codemp" / "win32" / "win_sp_renderer_boundary.cpp").read_text(
        encoding="utf-8", errors="ignore"
    )
    sp_files = sorted(path.relative_to(sp_dir) for path in sp_dir.rglob("*") if path.is_file())
    missing = [rel.as_posix() for rel in sp_files if not (mp_dir / rel).is_file()]
    mismatched = [
        rel.as_posix()
        for rel in sp_files
        if (mp_dir / rel).is_file() and (sp_dir / rel).read_bytes() != (mp_dir / rel).read_bytes()
    ]
    if missing or mismatched:
        fail(
            "Holomatch renderer must be a byte-for-byte SP copy; "
            f"missing={missing[:16]} mismatched={mismatched[:16]}"
        )
    if 'ScanAndLoadShaderFiles( "scripts", false );' in common_text:
        fail("Holomatch still compiles the inherited JA early shader preload route")
    if "void ScanAndLoadShaderFiles(const char *path, bool doHash)" in boundary_text:
        fail("Holomatch still carries the no-op JA shader preload boundary")

    return {
        "rendererWholesaleFiles": len(sp_files),
        "rendererWholesaleByteExact": True,
        "rendererLegacyEarlyShaderPreloadDead": True,
    }


def verify_sp_controls_wholesale(repo_root: Path) -> dict[str, object]:
    missing: list[str] = []
    mismatched: list[str] = []
    for rel in sorted(REQUIRED_SP_CONTROL_FILES):
        sp_path = repo_root / "code" / rel
        mp_path = repo_root / "codemp" / rel
        if not sp_path.is_file() or not mp_path.is_file():
            missing.append(rel)
        elif sp_path.read_bytes() != mp_path.read_bytes():
            mismatched.append(rel)

    if missing or mismatched:
        fail(
            "Holomatch controls must be byte-for-byte SP copies; "
            f"missing={missing} mismatched={mismatched}"
        )

    exe_sources = set(active_project_sources(repo_root / "codemp" / "x_exe" / "x_exe.vcproj"))
    missing_bridges = sorted(REQUIRED_SP_CONTROL_BRIDGES - exe_sources)
    if missing_bridges:
        fail("Holomatch executable is missing SP control bridge source(s): " + ", ".join(missing_bridges))

    exact_functions = {
        "Con_ToggleConsole_f": ("code/client/cl_console.cpp", "codemp/client/cl_console.cpp"),
        "Sys_QuickStart": ("code/win32/win_main_console.cpp", "codemp/win32/win_main_console.cpp"),
    }
    mismatched_functions = [
        name
        for name, (sp_rel, mp_rel) in exact_functions.items()
        if extract_cpp_function((repo_root / sp_rel).read_text(encoding="utf-8", errors="ignore"), name)
        != extract_cpp_function((repo_root / mp_rel).read_text(encoding="utf-8", errors="ignore"), name)
    ]
    if mismatched_functions:
        fail("Holomatch SP control function transplant drifted: " + ", ".join(mismatched_functions))

    lifecycle_markers = {
        "codemp/qcommon/common.cpp": {
            'inSplashMenu = Cvar_Get( "inSplashMenu", "1", 0 );',
            'controllerOut= Cvar_Get( "ControllerOutNum", "-1", 0);',
        },
        "codemp/win32/win_main_console.cpp": {
            "+set inSplashMenu 0 +set ControllerOutNum -1",
        },
        "codemp/qcommon/xb_settings_sp_bridge.cpp": {
            'CopyFileA( "D:\\\\BaseEF\\\\media\\\\settings.xbx", destination, failIfExists )',
        },
    }
    missing_lifecycle = [
        f"{rel}: {marker}"
        for rel, markers in lifecycle_markers.items()
        for marker in sorted(markers)
        if marker not in (repo_root / rel).read_text(encoding="utf-8", errors="ignore")
    ]
    if missing_lifecycle:
        fail("Holomatch SP control lifecycle boundary is incomplete: " + ", ".join(missing_lifecycle))

    return {
        "controlsWholesaleFiles": len(REQUIRED_SP_CONTROL_FILES),
        "controlsWholesaleByteExact": True,
        "controlsBoundarySources": sorted(REQUIRED_SP_CONTROL_BRIDGES),
        "controlsExactFunctionTransplants": sorted(exact_functions),
        "controlsCvarLifecycleInitialized": True,
    }


def verify_sp_sound_wholesale(repo_root: Path) -> dict[str, object]:
    missing: list[str] = []
    mismatched: list[str] = []
    for rel in sorted(REQUIRED_SP_SOUND_FILES):
        sp_path = repo_root / "code" / rel
        mp_path = repo_root / "codemp" / rel
        if not sp_path.is_file() or not mp_path.is_file():
            missing.append(rel)
        elif sp_path.read_bytes() != mp_path.read_bytes():
            mismatched.append(rel)

    sp_mp3_dir = repo_root / "code" / "mp3code"
    mp_mp3_dir = repo_root / "codemp" / "mp3code"
    sp_mp3_files = sorted(path.relative_to(sp_mp3_dir) for path in sp_mp3_dir.rglob("*") if path.is_file())
    missing_mp3 = [rel.as_posix() for rel in sp_mp3_files if not (mp_mp3_dir / rel).is_file()]
    mismatched_mp3 = [
        rel.as_posix()
        for rel in sp_mp3_files
        if (mp_mp3_dir / rel).is_file()
        and (sp_mp3_dir / rel).read_bytes() != (mp_mp3_dir / rel).read_bytes()
    ]
    if missing or mismatched or missing_mp3 or mismatched_mp3:
        fail(
            "Holomatch sound must be byte-for-byte SP copies; "
            f"missing={missing} mismatched={mismatched} "
            f"missingMp3={missing_mp3} mismatchedMp3={mismatched_mp3}"
        )

    exe_sources = set(active_project_sources(repo_root / "codemp" / "x_exe" / "x_exe.vcproj"))
    missing_sources = sorted(REQUIRED_SP_SOUND_PROJECT_SOURCES - exe_sources)
    forbidden_sources = sorted(FORBIDDEN_DIRECT_SP_SOUND_PROJECT_SOURCES & exe_sources)
    if missing_sources or forbidden_sources:
        fail(
            "Holomatch executable sound source routing is incomplete; "
            f"missing={missing_sources} directUnbridged={forbidden_sources}"
        )

    bridge_markers = {
        "codemp/client/snd_dma_console_sp_bridge.cpp": {
            "#define Z_Free STEFX_SoundZFree",
            '#include "snd_dma_console.cpp"',
            "const int bytes = Z_Size( ptr );",
        },
        "codemp/client/snd_music_sp_bridge.cpp": {
            '#include "../qcommon/genericparser2.h"',
            '#include "snd_music.cpp"',
        },
        "codemp/client/sp_sound_compat.cpp": {
            'Com_sprintf( out, sizeof( ospath[0] ), "d:\\\\BaseEF\\\\%s", qpath );',
            'g_sex = Cvar_Get( "sex", "f", CVAR_USERINFO | CVAR_ARCHIVE );',
            "S_BeginRegistration();",
        },
        "codemp/win32/win_qal_xbox_sp_bridge.cpp": {
            "STEFX_CaptureEffectsImageDesc(imageDesc)",
            '#include "win_qal_xbox.cpp"',
            "s_pState->m_Stream.m_Queue.clear();",
        },
        "codemp/game/g_local.h": {
            "char\t\tmapname[MAX_QPATH];\t\t// SP sound state expects the current server map",
        },
        "codemp/game/g_main.c": {
            "Q_strncpyz( level.mapname, mapname.string, sizeof( level.mapname ) );",
        },
    }
    missing_markers = [
        f"{rel}: {marker}"
        for rel, markers in bridge_markers.items()
        for marker in sorted(markers)
        if marker not in (repo_root / rel).read_text(encoding="utf-8", errors="ignore")
    ]
    if missing_markers:
        fail("Holomatch SP sound boundary is incomplete: " + ", ".join(missing_markers))

    build_script = (repo_root / "scripts" / "build_xbox.ps1").read_text(
        encoding="utf-8", errors="ignore"
    ).replace("/", "\\").lower()
    missing_decoder_sources = []
    for rel in sp_mp3_files:
        decoder_path = "codemp\\mp3code\\" + rel.as_posix().replace("/", "\\")
        if rel.suffix.lower() == ".c" and decoder_path.lower() not in build_script:
            missing_decoder_sources.append(rel.as_posix())
    if missing_decoder_sources:
        fail("Holomatch MP build is missing SP MP3 decoder source(s): " + ", ".join(missing_decoder_sources))

    return {
        "soundWholesaleFiles": len(REQUIRED_SP_SOUND_FILES),
        "soundWholesaleByteExact": True,
        "soundMp3WholesaleFiles": len(sp_mp3_files),
        "soundMp3WholesaleByteExact": True,
        "soundBoundarySources": sorted(REQUIRED_SP_SOUND_PROJECT_SOURCES),
        "soundBaseGame": "BaseEF",
    }


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


def dds_payload_error(data: bytes) -> str | None:
    if len(data) < 128 or data[:4] != b"DDS ":
        return "invalid header"

    header_size = struct.unpack_from("<I", data, 4)[0]
    pixel_format_size = struct.unpack_from("<I", data, 76)[0]
    height = struct.unpack_from("<I", data, 12)[0]
    width = struct.unpack_from("<I", data, 16)[0]
    mip_count = struct.unpack_from("<I", data, 28)[0] or 1
    fmt = dds_format(data)

    if header_size != 124 or pixel_format_size != 32:
        return f"invalid header sizes ({header_size}, {pixel_format_size})"
    if width == 0 or height == 0:
        return f"invalid dimensions {width}x{height}"
    if mip_count > max(width, height).bit_length():
        return f"invalid mip count {mip_count} for {width}x{height}"

    if fmt in {"DXT1", "DXT5"}:
        block_bytes = 8 if fmt == "DXT1" else 16
        expected = 0
        level_width = width
        level_height = height
        for _ in range(mip_count):
            expected += (
                max(1, (level_width + 3) // 4)
                * max(1, (level_height + 3) // 4)
                * block_bytes
            )
            level_width = max(1, level_width // 2)
            level_height = max(1, level_height // 2)
    elif fmt in {"BGRA32", "RGB565"}:
        bytes_per_pixel = 4 if fmt == "BGRA32" else 2
        expected = 0
        level_width = width
        level_height = height
        for _ in range(mip_count):
            expected += level_width * level_height * bytes_per_pixel
            level_width = max(1, level_width // 2)
            level_height = max(1, level_height // 2)
    else:
        return None

    actual = len(data) - 128
    if actual != expected:
        return (
            f"{fmt} {width}x{height} mips={mip_count} "
            f"payload={actual} expected={expected}"
        )

    if fmt == "BGRA32":
        masks = struct.unpack_from("<4I", data, 92)
        expected_masks = (0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)
        if masks != expected_masks:
            return (
                "BGRA32 channel masks="
                + ",".join(f"0x{mask:08x}" for mask in masks)
            )
    elif fmt == "RGB565":
        masks = struct.unpack_from("<4I", data, 92)
        expected_masks = (0x0000F800, 0x000007E0, 0x0000001F, 0)
        if masks != expected_masks:
            return (
                "RGB565 channel masks="
                + ",".join(f"0x{mask:08x}" for mask in masks)
            )

    return None


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
    baseef_routing = verify_baseef_source_routing(repo_root)
    renderer_wholesale = verify_sp_renderer_wholesale(repo_root)
    controls_wholesale = verify_sp_controls_wholesale(repo_root)
    sound_wholesale = verify_sp_sound_wholesale(repo_root)

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

    game_project = repo_root / "codemp" / "x_jk2game" / "x_jk2game.vcproj"
    game_project_paths = project_paths(game_project)
    game_project_text = game_project.read_text(encoding="utf-8", errors="ignore")
    if "/DEF_AI_STATE_COPY_BOUNDARY=1" not in game_project_text:
        fail("official EF ai_main.c must compile with the copied-player-state ammo boundary")
    missing_official_ai_sources = sorted(
        norm_path(path) for path in REQUIRED_OFFICIAL_EF_AI_PROJECT_SOURCES
        if norm_path(path) not in game_project_paths
    )
    if missing_official_ai_sources:
        fail(
            "Holomatch game project is missing official EF 1.2 bot AI source(s): "
            + ", ".join(missing_official_ai_sources)
        )

    compiled_ja_ai_sources = sorted(
        norm_path(path) for path in FORBIDDEN_JA_AI_PROJECT_SOURCES
        if norm_path(path) in game_project_paths
    )
    if compiled_ja_ai_sources:
        fail(
            "Holomatch game project still compiles inherited JA bot AI source(s): "
            + ", ".join(compiled_ja_ai_sources)
        )

    cgame_project = repo_root / "codemp" / "x_jk2cgame" / "x_jk2cgame.vcproj"
    cgame_project_paths = project_paths(cgame_project)
    for project_name, paths in (
        ("game", game_project_paths),
        ("cgame", cgame_project_paths),
    ):
        missing_shared = sorted(
            norm_path(path) for path in REQUIRED_OFFICIAL_EF_SHARED_PROJECT_SOURCES
            if norm_path(path) not in paths
        )
        if missing_shared:
            fail(
                f"Holomatch {project_name} project is missing official EF shared source(s): "
                + ", ".join(missing_shared)
            )
        compiled_ja_shared = sorted(
            norm_path(path) for path in FORBIDDEN_JA_SHARED_PROJECT_SOURCES
            if norm_path(path) in paths
        )
        if compiled_ja_shared:
            fail(
                f"Holomatch {project_name} project still compiles inherited JA shared source(s): "
                + ", ".join(compiled_ja_shared)
            )

    official_ai_dir = repo_root / "codemp" / "game" / "ef_ai"
    bad_official_ai_hashes: list[str] = []
    for filename, expected_hash in sorted(OFFICIAL_EF_AI_SHA256.items()):
        path = official_ai_dir / filename
        if not path.is_file():
            bad_official_ai_hashes.append(f"{filename}: missing")
            continue
        actual_hash = hashlib.sha256(path.read_bytes()).hexdigest()
        if actual_hash != expected_hash:
            bad_official_ai_hashes.append(f"{filename}: {actual_hash}")
    if bad_official_ai_hashes:
        fail(
            "official EF 1.2 bot AI source must remain byte-for-byte unchanged: "
            + ", ".join(bad_official_ai_hashes)
        )

    official_game_dir = repo_root / "codemp" / "game" / "ef_game"
    bad_official_game_hashes: list[str] = []
    for filename, expected_hash in sorted(OFFICIAL_EF_GAME_SHA256.items()):
        path = official_game_dir / filename
        if not path.is_file():
            bad_official_game_hashes.append(f"{filename}: missing")
            continue
        actual_hash = hashlib.sha256(path.read_bytes()).hexdigest()
        if actual_hash != expected_hash:
            bad_official_game_hashes.append(f"{filename}: {actual_hash}")
    if bad_official_game_hashes:
        fail(
            "official EF 1.2 bot lifecycle source must remain byte-for-byte unchanged: "
            + ", ".join(bad_official_game_hashes)
        )

    bad_official_combat_hashes: list[str] = []
    for filename, expected_hash in sorted(OFFICIAL_EF_COMBAT_SHA256.items()):
        path = official_game_dir / filename
        if not path.is_file():
            bad_official_combat_hashes.append(f"{filename}: missing")
            continue
        actual_hash = hashlib.sha256(path.read_bytes()).hexdigest()
        if actual_hash != expected_hash:
            bad_official_combat_hashes.append(f"{filename}: {actual_hash}")
    if bad_official_combat_hashes:
        fail(
            "official EF 1.2 combat source must remain byte-for-byte unchanged: "
            + ", ".join(bad_official_combat_hashes)
        )

    bad_official_weapon_hashes: list[str] = []
    for filename, expected_hash in sorted(OFFICIAL_EF_WEAPON_SHA256.items()):
        path = official_game_dir / filename
        if not path.is_file():
            bad_official_weapon_hashes.append(f"{filename}: missing")
            continue
        actual_hash = hashlib.sha256(path.read_bytes()).hexdigest()
        if actual_hash != expected_hash:
            bad_official_weapon_hashes.append(f"{filename}: {actual_hash}")
    if bad_official_weapon_hashes:
        fail(
            "official EF 1.2 weapon source must remain byte-for-byte unchanged: "
            + ", ".join(bad_official_weapon_hashes)
        )

    bad_official_missile_hashes: list[str] = []
    for filename, expected_hash in sorted(OFFICIAL_EF_MISSILE_SHA256.items()):
        path = official_game_dir / filename
        if not path.is_file():
            bad_official_missile_hashes.append(f"{filename}: missing")
            continue
        actual_hash = hashlib.sha256(path.read_bytes()).hexdigest()
        if actual_hash != expected_hash:
            bad_official_missile_hashes.append(f"{filename}: {actual_hash}")
    if bad_official_missile_hashes:
        fail(
            "official EF 1.2 missile source must remain byte-for-byte unchanged: "
            + ", ".join(bad_official_missile_hashes)
        )

    bad_official_active_hashes: list[str] = []
    for filename, expected_hash in sorted(OFFICIAL_EF_ACTIVE_SHA256.items()):
        path = official_game_dir / filename
        if not path.is_file():
            bad_official_active_hashes.append(f"{filename}: missing")
            continue
        actual_hash = hashlib.sha256(path.read_bytes()).hexdigest()
        if actual_hash != expected_hash:
            bad_official_active_hashes.append(f"{filename}: {actual_hash}")
    if bad_official_active_hashes:
        fail(
            "official EF 1.2 client activity source must remain byte-for-byte unchanged: "
            + ", ".join(bad_official_active_hashes)
        )

    bad_official_items_hashes: list[str] = []
    for filename, expected_hash in sorted(OFFICIAL_EF_ITEMS_SHA256.items()):
        path = official_game_dir / filename
        if not path.is_file():
            bad_official_items_hashes.append(f"{filename}: missing")
            continue
        actual_hash = hashlib.sha256(path.read_bytes()).hexdigest()
        if actual_hash != expected_hash:
            bad_official_items_hashes.append(f"{filename}: {actual_hash}")
    if bad_official_items_hashes:
        fail(
            "official EF 1.2 item lifecycle source must remain byte-for-byte unchanged: "
            + ", ".join(bad_official_items_hashes)
        )

    official_shared_dir = repo_root / "codemp" / "game" / "ef_shared"
    bad_official_shared_hashes: list[str] = []
    for filename, expected_hash in sorted(OFFICIAL_EF_SHARED_SHA256.items()):
        path = official_shared_dir / filename
        if not path.is_file():
            bad_official_shared_hashes.append(f"{filename}: missing")
            continue
        actual_hash = hashlib.sha256(path.read_bytes()).hexdigest()
        if actual_hash != expected_hash:
            bad_official_shared_hashes.append(f"{filename}: {actual_hash}")
    if bad_official_shared_hashes:
        fail(
            "official EF 1.2 shared source must remain byte-for-byte unchanged: "
            + ", ".join(bad_official_shared_hashes)
        )

    ef_ai_compat = "\n".join(
        path.read_text(encoding="utf-8", errors="ignore")
        for path in (
            repo_root / "codemp" / "game" / "ef_shared_compat.h",
            repo_root / "codemp" / "game" / "ef_ai_compat.h",
        )
    )
    bad_player_classes = sorted(
        f"{name}={value}"
        for name, value in OFFICIAL_EF_PLAYER_CLASSES.items()
        if not re.search(rf"#define\s+{name}\s+{value}(?:\s|$)", ef_ai_compat)
    )
    if bad_player_classes:
        fail(
            "official EF player-class IDs must remain exact at the carrier boundary: "
            + ", ".join(bad_player_classes)
        )
    bad_weapon_boundary: list[str] = []
    for official_id, carrier_id in OFFICIAL_EF_CARRIER_WEAPON_MAP.items():
        forward = rf"case\s+{official_id}\s*:\s*base\s*=\s*{carrier_id}\s*;"
        reverse = rf"case\s+{carrier_id}\s*:\s*base\s*=\s*{official_id}\s*;"
        if not re.search(forward, ef_ai_compat):
            bad_weapon_boundary.append(f"{official_id} -> {carrier_id}")
        if not re.search(reverse, ef_ai_compat):
            bad_weapon_boundary.append(f"{carrier_id} -> {official_id}")
    if bad_weapon_boundary:
        fail(
            "official EF bot weapon IDs must map bidirectionally at the carrier boundary: "
            + ", ".join(bad_weapon_boundary)
        )

    botlib_header = (repo_root / "codemp" / "game" / "botlib.h").read_text(
        encoding="utf-8", errors="ignore"
    )
    action_block_match = re.search(
        r"#if defined\(STEFX_ELITE_FORCE_MP\)(.*?)#else", botlib_header, re.DOTALL
    )
    if not action_block_match:
        fail("Holomatch botlib.h is missing its official EF action-flag block")
    action_defines = {
        name: value
        for name, value in re.findall(
            r"^#define\s+(ACTION_[A-Z0-9_]+)\s+([^\s/]+)",
            action_block_match.group(1),
            re.MULTILINE,
        )
    }
    resolved_action_flags: dict[str, int] = {}
    for name in OFFICIAL_EF_BOT_ACTION_FLAGS:
        value = action_defines.get(name, "")
        seen: set[str] = set()
        while value.startswith("ACTION_") and value not in seen:
            seen.add(value)
            value = action_defines.get(value, "")
        try:
            resolved_action_flags[name] = int(value, 0)
        except ValueError:
            resolved_action_flags[name] = -1
    bad_action_flags = {
        name: resolved_action_flags.get(name)
        for name, expected in OFFICIAL_EF_BOT_ACTION_FLAGS.items()
        if resolved_action_flags.get(name) != expected
    }
    if bad_action_flags:
        fail(
            "Holomatch botlib action flags must match official EF, including move-up/jump and move-down/crouch aliases: "
            + json.dumps(bad_action_flags, sort_keys=True)
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
    missing_dds_upload_markers = []
    for rel, markers in REQUIRED_RENDERER_DDS_UPLOAD_MARKERS.items():
        text = (repo_root / rel).read_text(encoding="utf-8", errors="ignore")
        for marker in markers:
            if marker not in text:
                missing_dds_upload_markers.append(f"{rel}: {marker}")
    if missing_dds_upload_markers:
        fail(
            "Holomatch renderer support must keep the DDS-only Xbox upload path active: "
            + ", ".join(missing_dds_upload_markers)
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
    synthetic_combat_sources = {
        "codemp/game/g_main.c": (repo_root / "codemp" / "game" / "g_main.c").read_text(
            encoding="utf-8", errors="ignore"
        ),
        "scripts/smoke_cxbx_mp_log.ps1": (
            repo_root / "scripts" / "smoke_cxbx_mp_log.ps1"
        ).read_text(encoding="utf-8", errors="ignore"),
    }
    synthetic_combat_hits = sorted(
        f"{rel}: {marker}"
        for rel, text in synthetic_combat_sources.items()
        for marker in FORBIDDEN_SYNTHETIC_COMBAT_MARKERS
        if marker in text
    )
    if synthetic_combat_hits:
        fail(
            "Holomatch combat proof must come from organic bot play; remove synthetic damage/respawn marker(s): "
            + ", ".join(synthetic_combat_hits)
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
        **baseef_routing,
        **renderer_wholesale,
        **controls_wholesale,
        **sound_wholesale,
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
        "rendererSolidFillReset": True,
        "rendererDdsOnlyUpload": True,
        "holomatchDefaultPlayerModel": "munro/default",
        "officialEfBotAiFiles": len(OFFICIAL_EF_AI_SHA256),
        "officialEfBotAiByteExact": True,
        "officialEfBotLifecycleFiles": len(OFFICIAL_EF_GAME_SHA256),
        "officialEfBotLifecycleByteExact": True,
        "officialEfCombatFiles": len(OFFICIAL_EF_COMBAT_SHA256),
        "officialEfCombatByteExact": True,
        "officialEfWeaponFiles": len(OFFICIAL_EF_WEAPON_SHA256),
        "officialEfWeaponByteExact": True,
        "officialEfMissileFiles": len(OFFICIAL_EF_MISSILE_SHA256),
        "officialEfMissileByteExact": True,
        "officialEfActiveFiles": len(OFFICIAL_EF_ACTIVE_SHA256),
        "officialEfActiveByteExact": True,
        "officialEfItemsFiles": len(OFFICIAL_EF_ITEMS_SHA256),
        "officialEfItemsByteExact": True,
        "officialEfSharedFiles": len(OFFICIAL_EF_SHARED_SHA256),
        "officialEfSharedByteExact": True,
        "officialEfSharedItemAbi": True,
        "officialEfPlayerClasses": len(OFFICIAL_EF_PLAYER_CLASSES),
        "officialEfBotWeaponCarrierMappings": len(OFFICIAL_EF_CARRIER_WEAPON_MAP),
        "officialEfBotActionFlags": len(OFFICIAL_EF_BOT_ACTION_FLAGS),
        "compiledJaBotAiSources": 0,
        "compiledJaCombatSources": 0,
        "inputSpEarlyDeviceInit": True,
        "inputJoyDeadzoneDefault": "0.18",
        "combatPhaserDamageProof": True,
        "syntheticCombatProofDead": True,
        **dead_code,
    }


def verify_pk3(pk3: Path | None, direct_map: str = "hm_borg1") -> dict[str, object]:
    if pk3 is None:
        return {"checked": False}
    if not pk3.is_file():
        fail(f"Holomatch UI package check failed; missing PK3: {pk3}")
    if pk3.name.lower() != "xbox1.pk3" or pk3.parent.name != "BaseEF":
        fail(f"Holomatch runtime package must be BaseEF/xbox1.pk3, not {pk3}")

    with zipfile.ZipFile(pk3, "r") as zf:
        actual_names = zf.namelist()
        name_lookup = {norm_path(name): name for name in actual_names}
        names = [norm_path(name) for name in actual_names]
        duplicate_names = sorted(
            name for name in set(names) if names.count(name) > 1
        )
        if duplicate_names:
            fail(
                "xbox1.pk3 contains duplicate normalized entries: "
                + ", ".join(duplicate_names[:16])
            )
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

        required_holomatch_aas = f"maps/{direct_map.lower()}.aas"
        missing_nav = sorted({required_holomatch_aas} - set(names))
        if missing_nav:
            fail("xbox1.pk3 is missing official EF Holomatch navigation file(s): " + ", ".join(missing_nav))

        if FORBIDDEN_MP_EFFECTS_IMAGE in name_lookup:
            fail(
                "xbox1.pk3 must not override the SP dry-audio setup with an MP-only "
                "DirectSound effects image"
            )

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
        invalid_payloads: list[str] = []
        bgra32_gfx: list[str] = []
        for dds_entry in dds_entries:
            dds_data = zf.read(name_lookup[dds_entry])
            fmt = dds_format(dds_data[:128])
            dds_formats[fmt] = dds_formats.get(fmt, 0) + 1
            if fmt == "invalid":
                invalid_formats.append(dds_entry)
            elif fmt not in {"DXT1", "DXT5", "BGRA32", "RGB565"}:
                forbidden_formats.append(f"{dds_entry}={fmt}")
            payload_error = dds_payload_error(dds_data)
            if payload_error:
                invalid_payloads.append(f"{dds_entry}: {payload_error}")
            if dds_entry.startswith("gfx/") and fmt == "BGRA32":
                bgra32_gfx.append(dds_entry)

        if invalid_formats:
            fail("xbox1.pk3 contains invalid DDS file(s): " + ", ".join(invalid_formats[:16]))
        if forbidden_formats:
            fail(
                "xbox1.pk3 contains unsupported DDS format(s); OG Xbox Holomatch allows DXT1, DXT5, RGB565, BGRA32 only: "
                + ", ".join(forbidden_formats[:16])
            )
        if invalid_payloads:
            fail(
                "xbox1.pk3 contains DDS payload(s) incompatible with the native Xbox uploader: "
                + ", ".join(invalid_payloads[:16])
            )
        if bgra32_gfx:
            fail(
                "xbox1.pk3 gfx/ UI/HUD DDS entries must use retail-compatible DXT formats, not linear BGRA32: "
                + ", ".join(bgra32_gfx[:16])
            )

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
            "scripts/_console_arena_list_": ["arenas.txt", "xpack.arena"],
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
            if manifest.get("alphaTextureFormat") != "dxt5":
                fail("xbox1.pk3 manifest must use retail-compatible DXT5 alpha textures")
            if manifest.get("preservedOriginalTextures"):
                fail("xbox1.pk3 manifest reports preserved original texture fallback(s)")
            if manifest.get("skippedAlphaTextures"):
                fail("xbox1.pk3 manifest reports skipped alpha texture(s)")
            if manifest.get("effectsImage") is not None:
                fail("xbox1.pk3 manifest reports an MP-only DirectSound effects image")
            patched_aas = manifest.get("patchedAasChecksums") or {}

            if manifest.get("bspMaps") == "multiplayer":
                optimized_by_map = {
                    item.get("map"): item
                    for item in manifest.get("bspOptimizations", [])
                    if isinstance(item, dict) and isinstance(item.get("map"), str)
                }
                optimized_maps = set(optimized_by_map)
                if optimized_maps != OFFICIAL_MULTIPLAYER_MAPS:
                    fail(
                        "xbox1.pk3 optimized multiplayer map inventory is incomplete; "
                        f"missing={sorted(OFFICIAL_MULTIPLAYER_MAPS - optimized_maps)} "
                        f"extra={sorted(optimized_maps - OFFICIAL_MULTIPLAYER_MAPS)}"
                    )

                for map_name in sorted(OFFICIAL_MULTIPLAYER_MAPS):
                    required_entries = {
                        f"maps/xbox/{map_name}.lmpdds",
                        f"maps/{map_name}/checksum.mle",
                        f"maps/{map_name}/misc.mle",
                        f"maps/{map_name}/shaders.mle",
                        f"maps/{map_name}/verts.mle",
                        f"maps/{map_name}/visibility.mle",
                        f"maps/{map_name}.aas",
                    }
                    missing_entries = sorted(required_entries - set(names))
                    if missing_entries:
                        fail(
                            f"xbox1.pk3 is missing optimized {map_name} runtime entries: "
                            + ", ".join(missing_entries)
                        )

                    map_aas_path = f"maps/{map_name}.aas"
                    map_aas_checksum = patched_aas.get(map_aas_path)
                    map_bsp_checksum = optimized_by_map[map_name].get("xboxBspChecksum")
                    if not isinstance(map_aas_checksum, int) or map_aas_checksum <= 0:
                        fail(f"xbox1.pk3 manifest does not report patched {map_name} AAS checksum")
                    if map_aas_checksum != map_bsp_checksum:
                        fail(
                            f"xbox1.pk3 manifest AAS checksum does not match optimized {map_name} BSP checksum: "
                            f"aas={map_aas_checksum} bsp={map_bsp_checksum}"
                        )

                    map_aas_data = zf.read(name_lookup[map_aas_path])
                    if len(map_aas_data) < 12:
                        fail(f"xbox1.pk3 {map_name} AAS file is too small for a header")
                    aas_ident, aas_version, aas_header_checksum = struct.unpack_from(
                        "<4sII", map_aas_data, 0
                    )
                    if aas_ident != AAS_IDENT or aas_version not in (4, 5):
                        fail(
                            f"xbox1.pk3 {map_name} AAS header is not an Elite Force AAS file: "
                            f"ident={aas_ident!r} version={aas_version}"
                        )
                    if aas_version == 5:
                        decoded_checksum_bytes = bytearray(map_aas_data[8:12])
                        for index in range(len(decoded_checksum_bytes)):
                            decoded_checksum_bytes[index] ^= (index * 119) & 0xFF
                        aas_header_checksum = struct.unpack("<I", decoded_checksum_bytes)[0]
                    if aas_header_checksum != map_aas_checksum:
                        fail(
                            f"xbox1.pk3 {map_name} AAS header checksum does not match manifest: "
                            f"header={aas_header_checksum} manifest={map_aas_checksum}"
                        )

            aas_checksum = patched_aas.get(required_holomatch_aas)
            if not isinstance(aas_checksum, int) or aas_checksum <= 0:
                fail(f"xbox1.pk3 manifest does not report patched {direct_map} AAS checksum")
            bsp_checksums = [
                item.get("xboxBspChecksum")
                for item in manifest.get("bspOptimizations", [])
                if item.get("map") == direct_map.lower()
            ]
            if not bsp_checksums or bsp_checksums[0] != aas_checksum:
                fail(
                    f"xbox1.pk3 manifest AAS checksum does not match optimized {direct_map} BSP checksum: "
                    f"aas={aas_checksum} bsp={bsp_checksums[:1]}"
                )
            aas_data = zf.read(name_lookup[required_holomatch_aas])
            if len(aas_data) < 12:
                fail(f"xbox1.pk3 {direct_map} AAS file is too small for a header")
            aas_ident, aas_version, aas_header_checksum = struct.unpack_from("<4sII", aas_data, 0)
            if aas_ident != AAS_IDENT or aas_version not in (4, 5):
                fail(
                    f"xbox1.pk3 {direct_map} AAS header is not an Elite Force AAS file: "
                    f"ident={aas_ident!r} version={aas_version}"
                )
            if aas_version == 5:
                decoded_checksum_bytes = bytearray(aas_data[8:12])
                for index in range(len(decoded_checksum_bytes)):
                    decoded_checksum_bytes[index] ^= (index * 119) & 0xFF
                aas_header_checksum = struct.unpack("<I", decoded_checksum_bytes)[0]
            if aas_header_checksum != aas_checksum:
                fail(
                    f"xbox1.pk3 {direct_map} AAS header checksum does not match manifest: "
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

        botfile_entries = {
            name
            for name in names
            if name.startswith("botfiles/") and Path(name).suffix.lower() in {".c", ".h"}
        }
        missing_botlib_root = sorted(REQUIRED_EF_BOTLIB_ROOT_FILES - botfile_entries)
        if missing_botlib_root:
            fail(
                "xbox1.pk3 is missing official EF botlib root file(s): "
                + ", ".join(missing_botlib_root)
            )

        declared_bot_aifiles: set[str] = set()
        for match in re.finditer(
            r'^\s*aifile\s+(?:"([^"]+)"|([^\s}]+))', bots_text, re.MULTILINE
        ):
            bot_path = norm_path(match.group(1) or match.group(2))
            if not bot_path.startswith("botfiles/"):
                bot_path = "botfiles/" + bot_path
            declared_bot_aifiles.add(bot_path)
        if not declared_bot_aifiles:
            fail("scripts/bots.txt does not declare any official EF bot AI files")

        missing_declared_bots = sorted(declared_bot_aifiles - botfile_entries)
        if missing_declared_bots:
            fail(
                "xbox1.pk3 is missing bot AI file(s) declared by scripts/bots.txt: "
                + ", ".join(missing_declared_bots[:24])
            )

        missing_botfile_refs: set[str] = set()
        botfile_reference_count = 0
        botfiles_to_check = set(REQUIRED_EF_BOTLIB_ROOT_FILES | declared_bot_aifiles)
        checked_botfiles: set[str] = set()
        while botfiles_to_check:
            botfile_rel = min(botfiles_to_check)
            botfiles_to_check.remove(botfile_rel)
            if botfile_rel in checked_botfiles:
                continue
            checked_botfiles.add(botfile_rel)
            text = zf.read(name_lookup[botfile_rel]).decode("ascii", errors="ignore")
            for include_ref in re.findall(r'^\s*#include\s+"([^"]+)"', text, re.MULTILINE):
                include_ref = norm_path(include_ref)
                candidates = {
                    norm_path(posixpath.normpath(posixpath.join(posixpath.dirname(botfile_rel), include_ref))),
                    norm_path(posixpath.normpath(posixpath.join("botfiles", include_ref))),
                }
                botfile_reference_count += 1
                resolved = candidates.intersection(botfile_entries)
                if not resolved:
                    missing_botfile_refs.add(f"{botfile_rel} -> {include_ref}")
                else:
                    botfiles_to_check.add(min(resolved))

            for asset_ref in re.findall(
                r'CHARACTERISTIC_(?:WEAPONWEIGHTS|ITEMWEIGHTS|CHAT_FILE)\s+"([^"]+)"',
                text,
            ):
                referenced = norm_path(posixpath.normpath(posixpath.join("botfiles", asset_ref)))
                botfile_reference_count += 1
                if referenced not in botfile_entries:
                    missing_botfile_refs.add(f"{botfile_rel} -> {asset_ref}")
                else:
                    botfiles_to_check.add(referenced)

        if missing_botfile_refs:
            fail(
                "xbox1.pk3 has unresolved official EF botfile reference(s): "
                + ", ".join(sorted(missing_botfile_refs)[:24])
            )

        invalid_botfile_aliases: list[str] = []
        for alias_rel, source_rel in sorted(REQUIRED_HOLOMATCH_BOTFILE_ALIASES.items()):
            if alias_rel not in name_lookup:
                invalid_botfile_aliases.append(f"{alias_rel}: missing")
                continue
            if source_rel not in name_lookup:
                invalid_botfile_aliases.append(f"{source_rel}: missing source")
                continue
            if zf.read(name_lookup[alias_rel]) != zf.read(name_lookup[source_rel]):
                invalid_botfile_aliases.append(f"{alias_rel}: differs from {source_rel}")
        if invalid_botfile_aliases:
            fail(
                "xbox1.pk3 official EF botfile compatibility alias is invalid: "
                + ", ".join(invalid_botfile_aliases)
            )

        weapon_config_text = zf.read(name_lookup["botfiles/weapons.c"]).decode(
            "ascii", errors="ignore"
        )
        weapon_config_ids = {
            name: int(value)
            for name, value in re.findall(
                r'^\s*#define\s+(WEAPONINDEX_[A-Z0-9_]+)\s+(\d+)\b',
                weapon_config_text,
                re.MULTILINE,
            )
        }
        invalid_weapon_ids = {
            name: weapon_config_ids.get(name)
            for name, expected in OFFICIAL_EF_WEAPON_CONFIG_IDS.items()
            if weapon_config_ids.get(name) != expected
        }
        if invalid_weapon_ids:
            fail(
                "xbox1.pk3 official EF weapons.c ID contract is invalid: "
                + json.dumps(invalid_weapon_ids, sort_keys=True)
            )

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
        "optimizedMultiplayerMapCount": len(OFFICIAL_MULTIPLAYER_MAPS),
        "effectsImage": manifest.get("effectsImage", {}),
        "directBotModels": sorted(DIRECT_HOLOMATCH_BOTS),
        "officialBotfileCount": len(botfile_entries),
        "officialBotAifileCount": len(declared_bot_aifiles),
        "officialBotfileReachableCount": len(checked_botfiles),
        "officialBotfileReferenceCount": botfile_reference_count,
        "officialBotfileAliases": REQUIRED_HOLOMATCH_BOTFILE_ALIASES,
        "officialWeaponConfigIds": weapon_config_ids,
        "directBotSkinTextureCount": len(direct_bot_skin_textures),
        "directWeaponModelCount": len(DIRECT_HOLOMATCH_WEAPON_MODEL_FILES),
        "directPickupModelCount": len(DIRECT_HOLOMATCH_PICKUP_MODEL_FILES),
    }


def verify_stage(
    stage_baseef: Path | None,
    allow_original_images: bool,
    allow_map_overrides: bool,
) -> dict[str, object]:
    if stage_baseef is None:
        return {"checked": False}
    if stage_baseef.name != "BaseEF":
        fail(f"Holomatch runtime stage directory must be named BaseEF, not {stage_baseef}")

    soundbank = verify_soundbank(stage_baseef)
    ui_dir = stage_baseef / "ui"
    scripts = (
        sorted(
            path
            for path in ui_dir.rglob("*")
            if path.is_file() and path.suffix.lower() in {".menu", ".txt"}
        )
        if ui_dir.is_dir()
        else []
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

    maps_dir = stage_baseef / "maps"
    loose_mp_map_overrides = (
        sorted(
            path
            for path in maps_dir.iterdir()
            if path.is_file()
            and path.suffix.lower() in {".aas", ".bsp"}
            and path.stem.lower() in OFFICIAL_MULTIPLAYER_MAPS
        )
        if maps_dir.is_dir()
        else []
    )
    if loose_mp_map_overrides and not allow_map_overrides:
        rel = [
            norm_path(path.relative_to(stage_baseef).as_posix())
            for path in loose_mp_map_overrides[:16]
        ]
        fail(
            "staged BaseEF contains loose official MP map override(s) that supersede xbox1.pk3: "
            + ", ".join(rel)
        )
    return {
        "checked": True,
        "stageUiScripts": 0,
        "stageOriginalImages": len(original_images),
        "stageOriginalImagesAllowed": allow_original_images,
        "stageLooseMpMapOverrides": len(loose_mp_map_overrides),
        "stageLooseMpMapOverridesAllowed": allow_map_overrides,
        "soundbank": soundbank,
    }


def verify_soundbank(baseef: Path) -> dict[str, object]:
    soundbank_dir = baseef / "soundbank"
    bank_path = soundbank_dir / "sound.bnk"
    table_path = soundbank_dir / "sound.tbl"
    manifest_path = soundbank_dir / "soundbank_manifest.json"
    missing = [
        path.relative_to(baseef).as_posix()
        for path in (bank_path, table_path, manifest_path)
        if not path.is_file()
    ]
    if missing:
        fail("Holomatch BaseEF is missing SP soundbank file(s): " + ", ".join(missing))

    try:
        manifest = json.loads(manifest_path.read_text(encoding="ascii"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        fail(f"Holomatch soundbank manifest is invalid: {exc}")

    table_data = table_path.read_bytes()
    record_size = 13
    if not table_data or len(table_data) % record_size:
        fail(f"Holomatch sound.tbl size must be a non-zero multiple of {record_size}, got {len(table_data)}")

    records = [
        struct.unpack_from("<IIIb", table_data, offset)
        for offset in range(0, len(table_data), record_size)
    ]
    if len(records) > 131072:
        fail(f"Holomatch sound.tbl exceeds the SP streamer's 131072-record limit: {len(records)}")

    codes = [record[0] for record in records]
    if codes != sorted(codes):
        fail("Holomatch sound.tbl CRC records are not sorted for the SP fixed-map lookup")
    if len(set(codes)) != len(codes):
        fail("Holomatch sound.tbl contains duplicate runtime CRC keys")

    bank_bytes = bank_path.stat().st_size
    by_offset = sorted(records, key=lambda record: record[1])
    expected_offset = 0
    xbadpcm_records = 0
    pcm_records = 0
    with bank_path.open("rb") as bank:
        for code, offset, size, flags in by_offset:
            if offset != expected_offset or size < 12 or offset + size > bank_bytes:
                fail(
                    "Holomatch sound.tbl contains an invalid bank range "
                    f"crc=0x{code:08x} offset={offset} size={size} bankBytes={bank_bytes}"
                )
            if flags != 0:
                fail(f"Holomatch sound.tbl contains unsupported flags={flags} for crc=0x{code:08x}")
            bank.seek(offset)
            header = bank.read(12)
            if header[:4] != b"RIFF" or header[8:12] != b"WAVE":
                fail(f"Holomatch sound.bnk record 0x{code:08x} is not a WAV stream")

            cursor = 12
            wave_format: tuple[int, int, int] | None = None
            while cursor + 8 <= size:
                bank.seek(offset + cursor)
                chunk_header = bank.read(8)
                chunk_size = struct.unpack_from("<I", chunk_header, 4)[0]
                padded_size = chunk_size + (chunk_size & 1)
                if cursor + 8 + padded_size > size:
                    fail(f"Holomatch sound.bnk record 0x{code:08x} contains an invalid WAV chunk")
                if chunk_header[:4] == b"fmt ":
                    if chunk_size < 16:
                        fail(f"Holomatch sound.bnk record 0x{code:08x} has a truncated WAV format")
                    format_data = bank.read(16)
                    format_tag, channels, _rate, _byte_rate, _block_align, bits = struct.unpack(
                        "<HHIIHH", format_data
                    )
                    wave_format = (format_tag, channels, bits)
                    break
                cursor += 8 + padded_size

            if wave_format is None:
                fail(f"Holomatch sound.bnk record 0x{code:08x} has no WAV format chunk")
            format_tag, channels, bits = wave_format
            if format_tag == 0x0069 and channels in {1, 2} and bits == 4:
                xbadpcm_records += 1
            elif format_tag == 0x0001 and channels in {1, 2} and bits in {8, 16}:
                pcm_records += 1
            else:
                fail(
                    "Holomatch sound.bnk record uses a format unsupported by the wholesale SP loader "
                    f"crc=0x{code:08x} tag=0x{format_tag:04x} channels={channels} bits={bits}"
                )
            expected_offset += size
    if expected_offset != bank_bytes:
        fail(f"Holomatch sound.tbl covers {expected_offset} bytes but sound.bnk is {bank_bytes} bytes")

    sounds = manifest.get("sounds")
    if not isinstance(sounds, list) or len(sounds) != len(records):
        fail("Holomatch soundbank manifest record count does not match sound.tbl")
    if manifest.get("format") != "stefx-wav-bank-v2" or manifest.get("encoding") != "xbadpcm":
        fail("Holomatch soundbank manifest must describe the SP Xbox ADPCM bank format")
    if manifest.get("bank") != "soundbank/sound.bnk" or manifest.get("table") != "soundbank/sound.tbl":
        fail("Holomatch soundbank manifest uses an unexpected runtime path")
    if manifest.get("records") != len(records) or manifest.get("bytes") != bank_bytes:
        fail("Holomatch soundbank manifest sizes do not match the runtime files")
    if manifest.get("encodedRecords") != xbadpcm_records:
        fail("Holomatch soundbank manifest Xbox ADPCM count does not match sound.bnk")
    if manifest.get("preservedPcmRecords") != pcm_records:
        fail("Holomatch soundbank manifest PCM count does not match sound.bnk")

    table_rows = set(records)
    manifest_rows: set[tuple[int, int, int, int]] = set()
    for sound in sounds:
        qpath = norm_path(str(sound.get("path", "")))
        if not qpath.startswith("sound/") or not qpath.endswith(".wav"):
            fail(f"Holomatch soundbank manifest contains an invalid sound path: {qpath!r}")
        try:
            manifest_code = int(str(sound.get("crc", "")), 16)
            row = (
                manifest_code,
                int(sound["offset"]),
                int(sound["size"]),
                int(sound["flags"]),
            )
            runtime_path = ("d:\\BaseEF\\" + qpath.replace("/", "\\")).lower().encode("ascii")
        except (KeyError, TypeError, ValueError, UnicodeEncodeError) as exc:
            fail(f"Holomatch soundbank manifest contains an invalid record for {qpath!r}: {exc}")
        runtime_code = zlib.crc32(runtime_path) & 0xFFFFFFFF
        if manifest_code != runtime_code:
            fail(
                f"Holomatch soundbank CRC for {qpath} is 0x{manifest_code:08x}; "
                f"the SP BaseEF runtime hashes it as 0x{runtime_code:08x}"
            )
        manifest_rows.add(row)
    if manifest_rows != table_rows:
        fail("Holomatch soundbank manifest records do not match sound.tbl")

    return {
        "bankBytes": bank_bytes,
        "tableBytes": len(table_data),
        "records": len(records),
        "encodedRecords": xbadpcm_records,
        "preservedPcmRecords": pcm_records,
        "runtimeCrcRoot": "d:\\BaseEF\\",
        "allRecordsWave": True,
        "allFormatsSupportedBySp": True,
        "rangesContiguous": True,
        "crcKeysSortedUnique": True,
    }


def verify_xbe(xbe: Path | None) -> dict[str, object]:
    if xbe is None:
        return {"checked": False}
    if not xbe.is_file():
        fail(f"Holomatch UI XBE check failed; missing XBE: {xbe}")

    data = xbe.read_bytes()
    lower = data.lower()
    if b"d:\\base\\" in lower or re.search(rb"\+set fs_game base(?:\x00|\s)", lower):
        fail("efmp.xbe contains an active legacy base filesystem route; Holomatch must use BaseEF")
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
    synthetic_combat_hits = sorted(
        value
        for value in FORBIDDEN_SYNTHETIC_COMBAT_MARKERS
        if value.encode("ascii") in data
    )
    if synthetic_combat_hits:
        fail(
            "efmp.xbe contains synthetic combat proof marker(s): "
            + ", ".join(synthetic_combat_hits)
        )

    required = {
        b"+set fs_game BaseEF",
        b"D:\\BaseEF\\",
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
        b"STEFX_HM: BGRA32 DDS direct Xbox upload",
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
        b"BotAISetupClient: client %d already setup",
        b"couldn't load skill %d from %s",
        b"STEFX_HM: official EF bot AI allocation state initialized",
        b"STEFX_HM: official EF bot ammo view mirrored from carrier ammo buckets",
        b"STEFX_HM: official EF bot usercmd client=",
        b"STEFX_HM: JA waypoint loader retired; official EF AAS route active",
        b"STEFX_HM: official EF bot metadata-only init begin restart=",
        b"STEFX_HM: input Plan-B XInitDevices completed before D3D init",
        b"STEFX_HM: MP using SP-style fakegl pushbuffer path; main skipped legacy Direct3D_SetPushBufferSize",
        b"STEFX: IN_Init gamepad mask=",
        b"STEFX: first gamepad state port=",
        b"STEFX_HM_SPLIT_PAD_ASSIGN: slot=",
        b"STEFX_HM_SPLIT_PAD_CMD: slot=",
        b"JA: Xbox real S_BeginRegistration listeners=",
        b"STEFX: QAL effects image sound/dsstdfx.bin missing; continuing dry audio",
        b"STEFX: QAL downloaded effects image bytes=",
        b"STEFX: QAL MP3 stream open name=",
        b"EF: Sys_StreamInitialize soundbank records=",
        b"STEFX_HM: official EF weapon dispatcher active",
        b"STEFX_HM: official EF missile simulation active",
        b"STEFX_HM: official EF client activity active with Xbox usercmd boundary",
        b"STEFX_HM: official EF item lifecycle active",
        b"STEFX_HM: retired JA carrier item hook invoked name=",
        b"STEFX_HM: official EF shared item table and pickup rules active",
        b"STEFX_HM: retired JA carrier combat hook invoked name=",
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

    return {
        "checked": True,
        "xbe": str(xbe),
        "baseGame": "BaseEF",
        "officialBotAmmoBoundary": True,
        "legacyBaseRouteCount": 0,
        "legacyStringCount": 0,
    }


def verify_frontend_map_label_layout(repo_root: Path) -> dict[str, object]:
    frontend = repo_root / "code" / "ui" / "ui_ef_frontend.cpp"
    if not frontend.is_file():
        fail(f"EF frontend source is missing: {frontend}")

    text = frontend.read_text(encoding="utf-8", errors="ignore")
    label_pattern = re.compile(
        r'EFFe_DrawPs2TextColor\('
        r'([-0-9.]+)f,\s*'
        r'([-0-9.]+)f,\s*'
        r'"(Gamma|Alpha|Delta|Beta)",\s*'
        r'EF_FRONTEND_FONT_MEDIUM,\s*'
        r'(UI_\w+)'
    )
    labels = [
        {
            "label": match.group(3),
            "x": float(match.group(1)),
            "y": float(match.group(2)),
            "style": match.group(4),
        }
        for match in label_pattern.finditer(text)
    ]
    if [record["label"] for record in labels] != ["Gamma", "Alpha", "Delta", "Beta"]:
        fail("EF frontend quadrant labels must be Gamma, Alpha, Delta, Beta in source order")
    if any(record["style"] != "UI_CENTER" for record in labels):
        fail("EF frontend quadrant labels must be center-aligned on the galaxy center bar")

    x_positions = [float(record["x"]) for record in labels]
    y_positions = [float(record["y"]) for record in labels]

    # The PS2 layout stacks each quadrant pair against the outer U edges.  It
    # does not distribute all four labels across the horizontal center rule.
    if abs(x_positions[0] - x_positions[1]) > 0.01:
        fail("EF frontend Gamma and Alpha labels must share the outer-left anchor")
    if abs(x_positions[2] - x_positions[3]) > 0.01:
        fail("EF frontend Delta and Beta labels must share the outer-right anchor")
    if abs(y_positions[0] - y_positions[2]) > 0.01:
        fail("EF frontend Gamma and Delta labels must share the upper baseline")
    if abs(y_positions[1] - y_positions[3]) > 0.01:
        fail("EF frontend Alpha and Beta labels must share the lower baseline")
    if y_positions[0] >= y_positions[1]:
        fail("EF frontend quadrant upper baseline must precede the lower baseline")

    centerline_x = 1342.0
    left_anchor = x_positions[0]
    right_anchor = x_positions[2]
    if abs((centerline_x - left_anchor) - (right_anchor - centerline_x)) > 0.01:
        fail("EF frontend quadrant anchors must balance around the galaxy map centerline")

    return {
        "checked": True,
        "source": norm_path(frontend.relative_to(repo_root).as_posix()),
        "centerlineX": centerline_x,
        "labels": labels,
        "leftAnchor": left_anchor,
        "rightAnchor": right_anchor,
        "upperBaseline": y_positions[0],
        "lowerBaseline": y_positions[1],
    }


def verify_code_only(repo_root: Path, xbe: Path | None, direct_map: str) -> dict[str, object]:
    """Validate the SP-hosted development target without consulting codemp."""
    source_extensions = {
        ".asm",
        ".c",
        ".cpp",
        ".cxx",
        ".def",
        ".h",
        ".hpp",
        ".inl",
        ".rc",
        ".sln",
        ".vcproj",
    }
    source_files = sorted(
        path
        for path in (repo_root / "code").rglob("*")
        if path.is_file() and path.suffix.lower() in source_extensions
    )
    codemp_hits = []
    for path in source_files:
        text = path.read_text(encoding="utf-8", errors="ignore")
        if re.search(r"(?i)(?:\bcodemp\b|codemp[\\/])", text):
            codemp_hits.append(norm_path(path.relative_to(repo_root).as_posix()))
    if codemp_hits:
        fail("SP-hosted Holomatch source under code/ still references codemp: " + ", ".join(codemp_hits[:16]))

    required_sources = {
        "code/qcommon/stefx_launch.h": {
            "#define STEFX_HOLOMATCH_SETUP_VERSION 4",
            "#define STEFX_HOLOMATCH_MAX_LOCAL_PLAYERS 4",
            "stefxHolomatchPlayerSetup_t",
            "char modelName[MAX_QPATH];",
            "char skinName[MAX_QPATH];",
            "int controlStyle;",
            "int autoswitchMode;",
            "int autoaimMode;",
            "int crosshair;",
            "int vibration;",
            "int invertPitch;",
            "int humanPlayers;",
            "stefxHolomatchPlayerSetup_t player[STEFX_HOLOMATCH_MAX_LOCAL_PLAYERS];",
            "int diagnosticVirtualControls;",
            "int diagnosticVirtualControlsP1;",
        },
        "code/qcommon/common.cpp": {
            "STEFX_HM_P1_SP_CONFIG: source=embedded-canonical-xbox",
            'Cbuf_AddText ("seta m_pitch -0.022\\n");',
            'Cbuf_AddText ("bind JOY1 datapad\\n");',
            'Cbuf_AddText ("bind JOY4 uimenu\\n");',
            'Cbuf_AddText ("bind JOY9 weapnext\\n");',
            'Cbuf_AddText ("bind JOY10 weapprev\\n");',
            'Cbuf_AddText ("bind JOY11 +altattack\\n");',
            'Cbuf_AddText ("bind JOY12 +attack\\n");',
            'Cbuf_AddText ("bind JOY14 +movedown\\n");',
            'Cbuf_AddText ("bind JOY15 +moveup\\n");',
            'Cbuf_AddText ("bind JOY16 +use\\n");',
        },
        "code/win32/win_main_console.cpp": {
            "STEFX_SP_HOSTED_MP",
            "STEFX: applying Holomatch XBE launch intent",
            "STEFX_HM_SPLIT_LAUNCH",
            "STEFX_HM_BOT_POPULATION: source=xbe",
            "STEFX_HM_INTRO_POLICY: source=xbe holoIntro=0 immediateLocalInput=1 applies3P=1 applies4P=1",
            "XBE Holomatch launch intent",
            "stefxHolomatchLaunchSetup_t",
            "STEFX_HM_MENU_HANDOFF",
			"STEFX_HM_DIRECT_BOOT: no handoff or marker; production frontend path",
			"STEFX_HM_MENU_HANDOFF: rejected invalid setup; returning to frontend",
			"s_stefxLaunchIntent = STEFX_LAUNCH_INTENT_FRONTEND;",
            "stefxHolomatchLaunchSetupMustFitLaunchData",
            "Sys_STEFXNormalizeHolomatchPlayerSettings",
            "Sys_STEFXQueueHolomatchPlayerSettings",
            'Cbuf_AddText(va("set bot_minplayers %d\\n", s_stefxHolomatchLaunchSetup.players));',
            'Cbuf_AddText("set g_holoIntro 0\\n");',
            "STEFX_HM_PLAYER_HANDOFF",
            "STEFX_HM_PLAYER_HANDOFF_QUEUE",
            "STEFX_HM_PLAYER_HANDOFF_APPLY",
            'Cbuf_AddText(va("set cg_autoswitch %d\\n", setup->player[0].autoswitchMode));',
            "Q_strncpyz(startupMap, s_stefxHolomatchLaunchSetup.mapName",
            'Cbuf_AddText("set fs_game BaseEF\\n");',
            'Cbuf_AddText("set stefx_splitScreen 1\\n");',
            'Cbuf_AddText(va("set stefx_splitScreenPlayers %d\\n"',
            'Cbuf_AddText("set stefx_splitScreenMode holomatch\\n");',
            'Cbuf_AddText(va("set stefx_hmLocalPlayers %d\\n"',
            'Cbuf_AddText(va("set stefx_hmHumanPlayers %d\\n"',
            'Cbuf_AddText(va("set r_splitScreenEconomy %d\\n"',
            'Cbuf_AddText(va("set stefx_hm_split_virtual_controls %d\\n"',
            'Cbuf_AddText(va("set stefx_hm_split_virtual_controls_p1 %d\\n"',
            'Cvar_Set("stefx_hmLocalPlayers", va("%d", players));',
            'Cvar_Set("stefx_hmHumanPlayers", va("%d", humanPlayers));',
            'Cvar_Set("r_splitScreenEconomy", players >= 2 ? "1" : "0");',
            'Cvar_Set("stefx_hm_split_virtual_controls", virtualControls ? "1" : "0");',
            'Cvar_Set("stefx_hm_split_virtual_controls_p1", virtualControlsP1 ? "1" : "0");',
            "s_stefxHolomatchLaunchSetup.diagnosticVirtualControls",
            "s_stefxHolomatchLaunchSetup.diagnosticVirtualControlsP1",
            'Cbuf_AddText(va("%s %s\\n", useDevMap ? "devmap" : "map", startupMap));',
            'path = "d:\\\\efmp.xbe";',
            'path = "d:\\\\default.xbe";',
            "STEFX_LAUNCH_INTENT_HOLOMATCH",
            "STEFX_LAUNCH_INTENT_COOP",
        },
        "code/ui/ui_ef_frontend.cpp": {
            'Sys_Reboot("multiplayer", &setup);',
            'Sys_Reboot("singleplayer", NULL);',
            'Sys_XboxQueueMenuMap(setup.mapName, "holomatch", setup.players);',
            "STEFX_HM_MENU_LAUNCH",
            "EF_SCREEN_HOLOMATCH_ADVANCED",
            "EF_SCREEN_HOLOMATCH_PLAYERS",
            "STEFX_HM_PLAYER_SETUP",
            "EFFe_FillHolomatchPlayerSettings(&setup);",
            "EFFe_ApplyHolomatchPlayerSettings(&setup);",
            'ui.Cvar_Set("cg_autoswitch", cvarValue);',
            "EF_SCREEN_CREW_BIO",
            "STEFX_CREW_VOICE",
            "sound/voice/computer/misc/janeway.mp3",
            "ext_data/sp_normaltext",
            'ui.Cvar_Set("stefx_hmLocalPlayers", number);',
            'ui.Cvar_Set("stefx_hmHumanPlayers", number);',
            'ui.Cvar_Set("r_splitScreenEconomy", setup.players >= 2 ? "1" : "0");',
            'setup.diagnosticVirtualControls = EFFe_HolomatchMenuSmokeUsesVirtualPads() ? 1 : 0;',
            'setup.diagnosticVirtualControlsP1 = setup.diagnosticVirtualControls;',
        's_holomatchMenuVirtualPads =',
        '(qboolean)(!Q_stricmp(s_menuSmokeTarget, "holomatch-players-options"))',
        'holomatchPortraits[EF_FRONTEND_HOLOMATCH_PLAYABLE_CHARACTERS][3]',
        'models/players2/%s/icon_%s.jpg',
        'EFFe_HolomatchCharacterIndexForCrewMember',
            'ui.Cvar_Set("stefx_hm_split_virtual_controls", setup.diagnosticVirtualControls ? "1" : "0");',
            'ui.Cvar_Set("stefx_hm_split_virtual_controls_p1", setup.diagnosticVirtualControlsP1 ? "1" : "0");',
            "UI_EFMainMenu_WantsControllerInput",
            "UI_EFMainMenu_ControllerKeyEvent",
            "CL_STEFX_SplitScreen_LocalSlotForPad(controller)",
            "EFFe_HandleHolomatchPlayerKey(player, key)",
            "STEFX_HM_PLAYER_READY: launch-all-ready",
            "EFFe_HolomatchHumanPlayerCount",
            '"BOT"',
            '"VIEWPORT"',
            "EFFe_HolomatchPadForPlayer(player) >= 0 && !s_holomatchPlayerReady[player]",
            "static int s_holomatchPlayerCursors[EF_FRONTEND_HOLOMATCH_LOCAL_PLAYERS]",
        },
        "code/server/sv_game.cpp": {"STEFX_SP_HOSTED_MP", "STEFX_HolomatchHostAfterGameInit"},
        "code/holomatch/official/game/g_main.c": {
            "STEFX_HM_INTRO_POLICY: runtime holoIntro=%d introMsec=%d immediateLocalInput=%d",
        },
        "code/server/sv_main.cpp": {"STEFX_SP_HOSTED_MP", "STEFX_HolomatchHostRunFrame"},
        "code/server/stefx_holomatch_host.cpp": {
            "STEFX_HolomatchGetSplitHudState",
            "g_SPXBHMPlayerSetupProof[40]",
            'Com_sprintf(cvarName, sizeof(cvarName), "hm_model_%d", player);',
            'Com_sprintf(cvarName, sizeof(cvarName), "joy_pitchsensitivity_%d", player);',
            "s_stefxHolomatchSplitStateLastLogTime",
            "sample=%d interval=500",
            "player->stats[STAT_HEALTH]",
            "player->persistant[PERS_SCORE]",
            "STEFX_HolomatchCycleOwnedWeapon",
            "(unsigned int)player->stats[STAT_WEAPONS] & (1u << candidate)",
            "CL_STEFX_SplitScreen_BuildHolomatchUsercmd(clientNum",
            "STEFX_HM_CONTROL_ROUTE: slot=%d port=%d source=%s players=%d",
            "STEFX_HM_CONTROL_ROUTING: players=%d connectedMask=0x%x uniquePorts=%d valid=%d",
            'slot == 0 ? "native_loopback" : "split_bridge"',
            'Cvar_VariableIntegerValue("stefx_hm_split_virtual_controls")',
            'Cvar_VariableIntegerValue("stefx_hm_split_virtual_controls_p1")',
            'STEFX_HolomatchViewportPlayerCount',
            'Cvar_VariableIntegerValue("stefx_hmHumanPlayers")',
            "STEFX_HolomatchAdjustNativeP1Usercmd",
        },
        "code/win32/xb_log.cpp": {
            "g_SPXBHMPlayerSetupProof[40]",
            "0x48345046",
            "g_SPXBHMSplitHudPlayers",
            "g_SPXBHMSplitHudDividerVerticalX",
            "g_SPXBHMSplitHudDividerHorizontalH",
        },
        "code/client/cl_main.cpp": {
            "STEFX_HW_FPS_SAMPLE:",
            "players=%d humans=%d bots=%d source=%s virtual=%d/%d",
            'Cvar_VariableString("stefx_hm_launch_source")',
        },
        "scripts/ja_xemu_smoke.py": {
            '"_g_SPXBHMPlayerSetupProof"',
            '"_g_SPXBHMSplitHudPlayers"',
            '"_g_SPXBHMSplitHudDividerVerticalX"',
            '"_g_SPXBHMSplitHudDividerHorizontalH"',
            "hm_split_hud_players",
            "STEFX_HM_PLAYER_HANDOFF_PROOF:",
            "STEFX_HM_PLAYER_SETUP_PROOF:",
        },
        "code/client/cl_input.cpp": {
            "STEFX_SplitRealPadForLocalSlot",
            "CL_STEFX_SplitScreen_PadForLocalSlot",
            'Cvar_VariableIntegerValue( "stefx_splitScreenPlayers" ) >= 3',
            "CL_STEFX_SplitScreen_BuildHolomatchUsercmd",
            "STEFX_HM_SPLIT_PAD_ASSIGN: slot=%d port=%d previous=%d connectedMask=0x%x",
            "STEFX_HM_SPLIT_PAD_CMD: slot=%d port=%d style=%d time=%d",
            "BUTTON_ATTACK",
            "BUTTON_ALT_ATTACK",
            "BUTTON_USE",
            "profile->jump",
            "profile->crouch",
            "profile->attack",
            "profile->altAttack",
            "profile->use",
            "STEFX_HM_P1_PROFILED_CMD: owner=native_loopback",
            'CL_STEFX_SplitScreen_BuildHolomatchUsercmd( 0,',
            'Cvar_Set( "stefx_hm_p1_profile_controls", "1" );',
            'Cbuf_AddText( "weapnext\\n" );',
            "STEFX_HM_SPLIT_PAD_RESEED:",
        },
        "code/win32/win_input_console.cpp": {
            "UI_EFMainMenu_WantsControllerInput",
            "UI_EFMainMenu_ControllerKeyEvent",
            "STEFX_HM_MENU_PAD_INPUT",
            "IN_STEFX_UpdateFrontendThumbstick(_padInfo.padId, qtrue)",
            "STEFX_HM_P1_PROFILE_BIND_BYPASS:",
            "STEFX_HM_GLOBAL_ACTION: slot=%d port=%d action=scoreboard",
            "STEFX_HM_GLOBAL_ACTION: slot=%d port=%d action=pause",
        },
        "code/win32/win_input_xbox.cpp": {
            "#define IN_ANALOG_BUTTON_THRESHOLD XINPUT_GAMEPAD_MAX_CROSSTALK",
            "A_JOY15, // A",
            "A_JOY14, // B",
            "A_JOY16, // X",
            "A_JOY13, // Y",
            "A_JOY11, // Left trigger",
            "A_JOY12  // Right trigger",
            "STEFX_HM_P1_BINDINGS:",
            "STEFX_HM_P1_ANALOG_SAMPLE:",
            "STEFX_HM_P1_ANALOG_EDGE:",
        },
        "code/win32/win_qgl_dx8.cpp": {
            "STEFX_D3D8_SetVertexShaderTracked",
            "backEnd.viewParms.stefxSplitThreePlusEconomy",
            "glw_state->shaderMaskValid",
            "glw_state->shaderMask == shader",
            "STEFX_HW_VERTEX_SHADER_CACHE:",
            "STEFX_D3D8_SetStreamSourceZeroTracked",
            "glw_state->streamSourceZeroStride == stride",
            "STEFX_HW_STREAM_SOURCE_CACHE:",
            "policy=three-plus requests=%u emitted=%u skipped=%u skipPct=%u",
        },
        "code/server/sv_snapshot.cpp": {
            "SVF_NOTSINGLECLIENT",
            "STEFX_HM_EVENT_ROUTE: reject viewer=",
        },
        "code/client/cl_cgame.cpp": {
            "CL_STEFX_DrawHolomatchSplitStatusOverlay",
            "STEFX_HolomatchGetSplitHudState",
            "STEFX_HM_SPLIT_HUD_STATUS",
            "STEFX_HM_SPLIT_HUD_DIVIDER",
            "g_SPXBHMSplitHudPlayers = (unsigned int)players",
            "g_SPXBHMSplitHudDividerVerticalH",
            "g_SPXBHMSplitHudDividerHorizontalH",
            "Do not paint a",
            "vertical=(320,%g 0x%g) horizontal=(0,240 640x0)",
            "*x = slot == 2 ? 320.0f : 0.0f",
            "*x = (slot & 1) ? 320.0f : 0.0f",
        },
        "code/game/g_public.h": {"singleClient", "SVF_NOTSINGLECLIENT"},
        "code/game/stefx_holomatch_game.cpp": {"STEFX_HM_SP: game boundary init", "STEFX_HM_SP: game boundary frame"},
        "code/game/q_shared.h": {
            "MAX_PS_EVENTS\t\t\t4",
            "official Elite Force Holomatch prediction ring",
        },
        "code/qcommon/msg.cpp": {"PSF(events[2])", "PSF(events[3])", "PSF(eventParms[3])"},
        "code/holomatch/stefx_mp_game_api.cpp": {
            "Holomatch player state remains authoritative",
            "read-only projection",
        },
        "code/holomatch/official/cgame/cg_view.c": {
            'stefx_hm_p1_profile_controls',
            'stefx_hm_p1_profile_zoom_fov',
            'cg.zoomed = (qboolean)( profileZoomFov < 89.95f );',
        },
        "code/ui/ui_ef_lifecycle.cpp": {
            "STEFX_HM: UI mandate active; uniform SP code/ui owns Holomatch UI",
            "STEFX_HM: SP EF UI lifecycle initialized from code/ui; no script menu cache",
        },
        "code/renderer/retail_xbox/tr_scene_retail.cpp": {
            'Cvar_VariableIntegerValue( "r_splitScreenEconomy" )',
            'Cvar_VariableIntegerValue( "stefx_splitScreen" )',
            'Cvar_VariableIntegerValue( "stefx_splitScreenPlayers" )',
            "s_stefxSplitViewportLogBudget",
            "STEFX_HM_SPLIT_RENDER: slot=%d external=%d externalClient=%d",
            "STEFX_HM_SPLIT_RENDER_DONE: slot=%d external=%d externalClient=%d",
            "STEFX_HM_SPLIT_TILING:",
            "overlapPixels=%d gapPixels=%d separators=0 valid=%d",
            "w = slot == 2 ? ( sourceRefdef->width - halfW ) : halfW",
            "w = ( slot & 1 ) ? ( sourceRefdef->width - halfW ) : halfW",
            "STEFX_HM_QUALITY: players=%d rendererFog=0 nonMdrLodBias=1 mdrLod=0 firstPersonLodBias=0 impactMarks=0 spawnerParticles=0.4 gameplayPortraits=2D vertexShaderCache=tracked streamSourceCache=tracked policy=three-plus",
        },
        "code/renderer/tr_mesh.cpp": {
            "tr.viewParms.stefxSplitThreePlusEconomy",
            "tr.currentModel->type != MOD_MDR",
            "if ( tr.currentModel->type == MOD_MDR )",
            "!( ent->e.renderfx & ( RF_FIRST_PERSON | RF_DEPTHHACK ) )",
        },
        "code/holomatch/official/cgame/cg_localents.c": {
            "LEF_STEFX_PARTICLE_CULLED",
            "STEFX_HM_QUALITY_PARTICLES:",
            "phase >= 2",
        },
        "code/holomatch/official/cgame/cg_draw.c": {
            "CG_STEFX_Use3DGameplayIcons",
            "CG_STEFX_ThreePlusEconomyActive()",
            "STEFX_HM_QUALITY_PORTRAITS:",
            "gameplayPortraits=2D extraRenderScenes=0 applies3P=1 applies4P=1",
        },
        "scripts/verify_holomatch_split_log.py": {
            "--require-full-p1-actions",
            "--require-full-control-actions",
            "missing P1 previous/next weapon edge proof",
            "missing scoreboard press/release by local slot",
            "STEFX_HM_P1_PROFILE_BIND_BYPASS:",
        },
    }
    missing_sources = []
    for rel, markers in required_sources.items():
        path = repo_root / rel
        if not path.is_file():
            missing_sources.append(f"{rel}: missing")
            continue
        text = path.read_text(encoding="utf-8", errors="ignore")
        missing = sorted(marker for marker in markers if marker not in text)
        if missing:
            missing_sources.append(f"{rel}: {', '.join(missing)}")
    if missing_sources:
        fail("SP-hosted Holomatch direct-map boundary is incomplete: " + "; ".join(missing_sources))

    forbidden_virtual_inference = {
        "code/win32/win_main_console.cpp": (
            'Cvar_Set("stefx_hm_split_virtual_controls", players >= 4',
            'Cvar_Set("stefx_hm_split_virtual_controls_p1", players >= 4',
        ),
        "code/ui/ui_ef_frontend.cpp": (
            'ui.Cvar_Set("stefx_hm_split_virtual_controls", setup.players >= 4',
            'ui.Cvar_Set("stefx_hm_split_virtual_controls_p1", setup.players >= 4',
        ),
    }
    inferred_virtual_controls = []
    for rel, markers in forbidden_virtual_inference.items():
        text = (repo_root / rel).read_text(encoding="utf-8", errors="ignore")
        inferred_virtual_controls.extend(f"{rel}: {marker}" for marker in markers if marker in text)
    if inferred_virtual_controls:
        fail(
            "Production Holomatch still infers diagnostic virtual controls from player count: "
            + "; ".join(inferred_virtual_controls)
        )

    xemu_harness_source = (repo_root / "scripts/ja_xemu_smoke.py").read_text(
        encoding="utf-8", errors="ignore"
    )
    forbidden_hud_mirror_hardcodes = (
        "players=4 shared=0 shader=1",
        "players=4 vertical=(318,0 4x480) horizontal=(0,238 640x4)",
    )
    retained_hud_mirror_hardcodes = [
        marker for marker in forbidden_hud_mirror_hardcodes if marker in xemu_harness_source
    ]
    if retained_hud_mirror_hardcodes:
        fail(
            "XEMU Holomatch HUD mirror still hardcodes four-player layout state: "
            + "; ".join(retained_hud_mirror_hardcodes)
        )

    win_main_source = (repo_root / "code/win32/win_main_console.cpp").read_text(
        encoding="utf-8", errors="ignore"
    )
    forbidden_direct_boot_fallbacks = (
        "no handoff or marker; hardwired hm_borg1",
        "missing or invalid setup; using hm_borg1 defaults",
    )
    retained_direct_boot_fallbacks = [
        marker for marker in forbidden_direct_boot_fallbacks if marker in win_main_source
    ]
    if retained_direct_boot_fallbacks:
        fail(
            "Production efmp.xbe still contains the one-pad direct-boot hardwire: "
            + "; ".join(retained_direct_boot_fallbacks)
        )

    frontend_source = (repo_root / "code/ui/ui_ef_frontend.cpp").read_text(
        encoding="utf-8", errors="ignore"
    )
    if "{ qfalse, qtrue, qtrue, qtrue }" in frontend_source:
        fail("Holomatch player setup still pre-readies P2-P4 instead of waiting for each controller")

    game_adapter = (repo_root / "code/holomatch/stefx_mp_game_api.cpp").read_text(
        encoding="utf-8", errors="ignore"
    )
    if "STEFX_HolomatchCopySPPlayerStateToOfficial" in game_adapter:
        fail(
            "SP-hosted Holomatch adapter still copies the reduced SP player state back over "
            "the authoritative EF state"
        )

    if xbe is None:
        xbe_result = {"checked": False}
    else:
        if not xbe.is_file():
            fail(f"SP-hosted Holomatch XBE check failed; missing XBE: {xbe}")
        data = xbe.read_bytes()
        required_xbe_markers = {
            b"STEFX: applying Holomatch XBE launch intent",
            b"STEFX_HM_SPLIT_LAUNCH",
            b"XBE Holomatch launch intent",
            b"STEFX_HM_SP: SP host game init",
            b"STEFX_HM_SP: game boundary init",
            b"STEFX_HM_SPLIT_HUD_STATUS",
            b"STEFX_HM_SPLIT_HUD_DIVIDER",
            b"d:\\default.xbe",
            b"d:\\efmp.xbe",
            b"BaseEF",
            b"set stefx_splitScreen 1",
            b"set stefx_splitScreenPlayers %d",
            b"set stefx_splitScreenMode holomatch",
            b"set stefx_hmLocalPlayers %d",
            b"set r_splitScreenEconomy %d",
            b"set stefx_hm_split_virtual_controls %d",
            b"set stefx_hm_split_virtual_controls_p1 %d",
            b"STEFX_HM_MENU_HANDOFF",
            b"STEFX_HM_PLAYER_HANDOFF_APPLY",
            b"STEFX_HM_PLAYER_SETUP",
            b"STEFX_CREW_VOICE",
            b"sound/voice/computer/misc/janeway.mp3",
            direct_map.encode("ascii"),
        }
        missing_xbe = sorted(
            marker.decode("ascii", errors="replace")
            for marker in required_xbe_markers
            if marker not in data
        )
        if missing_xbe:
            fail("efmp.xbe is missing SP-hosted XBE-handoff marker(s): " + ", ".join(missing_xbe))
        xbe_result = {
            "checked": True,
            "xbe": str(xbe),
            "baseGame": "BaseEF",
            "directBootMap": direct_map,
            "codempDependency": False,
        }

    return {
        "architecture": "sp-hosted-code-only",
        "codempDependency": False,
        "codeSourceFilesChecked": len(source_files),
        "holomatchLaunchMap": direct_map,
        "modeHandoff": "default.xbe <-> efmp.xbe",
        "rendererAudioInputOwner": "code/ SP engine",
        "uiOwner": "code/ui SP framework",
        "frontendMapLabelLayout": verify_frontend_map_label_layout(repo_root),
        "xbe": xbe_result,
    }


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
    parser.add_argument(
        "--allow-stage-map-overrides",
        action="store_true",
        help="Allow loose source BSP/AAS files in a non-runtime BaseEF workspace; never use for CXBX-R stage checks.",
    )
    parser.add_argument("--xbe", type=Path)
    parser.add_argument("--direct-map", default="hm_borg1")
    parser.add_argument(
        "--code-only",
        action="store_true",
        help="Validate the SP-hosted efmp.xbe path without reading or requiring codemp.",
    )
    args = parser.parse_args(argv)

    repo_root = args.repo_root.resolve()
    if args.code_only:
        summary = {
            "uiPolicy": "shared-sp-code-ui",
            "projects": verify_code_only(
                repo_root,
                args.xbe.resolve() if args.xbe else None,
                args.direct_map,
            ),
            "package": verify_pk3(
                args.pk3.resolve() if args.pk3 else None,
                args.direct_map,
            ),
            "stage": verify_stage(
                args.stage_baseef.resolve() if args.stage_baseef else None,
                args.allow_stage_original_images,
                args.allow_stage_map_overrides,
            ),
        }
        print(json.dumps(summary, indent=2, sort_keys=True))
        return 0

    summary = {
        "uiPolicy": "mandated-shared-ef-sp-code-ui-owns-behavior",
        "projects": verify_solution(repo_root),
        "package": verify_pk3(
            args.pk3.resolve() if args.pk3 else None,
            args.direct_map,
        ),
        "stage": verify_stage(
            args.stage_baseef.resolve() if args.stage_baseef else None,
            args.allow_stage_original_images,
            args.allow_stage_map_overrides,
        ),
        "xbe": verify_xbe(args.xbe.resolve() if args.xbe else None),
    }
    print(json.dumps(summary, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
