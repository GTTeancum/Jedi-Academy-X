#include "../game/q_shared.h"
#include "../qcommon/qcommon.h"
#include "../server/server.h"
#include "../win32/xb_log.h"
#include "botlib/stefx_botlib_compat.h"
#include "botlib/botlib.h"
#include "botlib/be_ai_goal.h"
#include "botlib/be_ai_move.h"
#include "stefx_holomatch_bot_bridge.h"

#include <stdarg.h>

extern int FS_Read2(void *buffer, int len, fileHandle_t file);

static botlib_export_t *s_stefxBotlib;
static int s_stefxBotlibSetup;
static int s_stefxBotlibFrameLogBudget;
static int s_stefxBotCommandLogBudget;
static int s_stefxBotAreaLogBudget;
static int s_stefxBotGoalLogBudget;
static int s_stefxBotMoveLogBudget;
static int s_stefxBotInputLogBudget;

static float STEFX_HolomatchIntAsFloat(int value)
{
	union {
		int i;
		float f;
	} bits;
	bits.i = value;
	return bits.f;
}

static int STEFX_HolomatchFloatAsInt(float value)
{
	union {
		int i;
		float f;
	} bits;
	bits.f = value;
	return bits.i;
}

static void QDECL STEFX_HolomatchBotPrint(int type, char *format, ...)
{
	char text[2048];
	va_list args;
	va_start(args, format);
	_vsnprintf(text, sizeof(text) - 1, format, args);
	va_end(args);
	text[sizeof(text) - 1] = '\0';

	if (type == PRT_EXIT)
	{
		XBLog_WriteCriticalf("STEFX_HM_BOTLIB: exit '%s'", text);
		Com_Error(ERR_DROP, "Bot library: %s", text);
		return;
	}
	if (type == PRT_ERROR || type == PRT_FATAL)
	{
		XBLog_WriteCriticalf("STEFX_HM_BOTLIB: error type=%d '%s'", type, text);
	}
	else
	{
		Com_Printf("STEFX_HM_BOTLIB: %s", text);
	}
}

static void STEFX_HolomatchCopyBotTrace(bsp_trace_t *destination,
	const trace_t *source)
{
	memset(destination, 0, sizeof(*destination));
	destination->allsolid = source->allsolid;
	destination->startsolid = source->startsolid;
	destination->fraction = source->fraction;
	VectorCopy(source->endpos, destination->endpos);
	destination->plane.dist = source->plane.dist;
	VectorCopy(source->plane.normal, destination->plane.normal);
	destination->plane.signbits = source->plane.signbits;
	destination->plane.type = source->plane.type;
	destination->surface.flags = source->surfaceFlags;
	destination->ent = source->entityNum;
}

static void STEFX_HolomatchBotTrace(bsp_trace_t *result, vec3_t start,
	vec3_t mins, vec3_t maxs, vec3_t end, int passEntityNum, int contentMask)
{
	trace_t trace;
	SV_Trace(&trace, start, mins, maxs, end, passEntityNum, contentMask);
	STEFX_HolomatchCopyBotTrace(result, &trace);
}

static void STEFX_HolomatchBotEntityTrace(bsp_trace_t *result, vec3_t start,
	vec3_t mins, vec3_t maxs, vec3_t end, int entityNum, int contentMask)
{
	trace_t trace;
	SV_ClipToEntity(&trace, start, mins, maxs, end, entityNum, contentMask);
	STEFX_HolomatchCopyBotTrace(result, &trace);
}

static int STEFX_HolomatchBotPointContents(vec3_t point)
{
	return SV_PointContents(point, -1);
}

static int STEFX_HolomatchBotInPVS(vec3_t point1, vec3_t point2)
{
	return SV_inPVS(point1, point2);
}

static char *STEFX_HolomatchBotBSPEntityData(void)
{
	return CM_EntityString();
}

static void STEFX_HolomatchBotModelBounds(int modelNum, vec3_t angles,
	vec3_t mins, vec3_t maxs, vec3_t origin)
{
	SV_BotModelBounds(modelNum, angles, mins, maxs, origin);
}

static void STEFX_HolomatchBotClientCommand(int clientNum, char *command)
{
	if (!svs.clients || clientNum < 0 || clientNum >= MAX_CLIENTS || !command)
	{
		return;
	}
	SV_ExecuteClientCommand(&svs.clients[clientNum], command);
}

static void *STEFX_HolomatchBotGetMemory(int size)
{
	return Z_Malloc(size, TAG_BOTLIB, qtrue);
}

static void STEFX_HolomatchBotFreeMemory(void *memory)
{
	Z_Free(memory);
}

static int STEFX_HolomatchBotAvailableMemory(void)
{
	const int limit = 8 * 1024 * 1024;
	return limit - Z_MemSize(TAG_BOTLIB);
}

static void *STEFX_HolomatchBotHunkAlloc(int size)
{
	if (Hunk_CheckMark())
	{
		Com_Error(ERR_DROP, "Holomatch bot allocation while a hunk mark is active");
	}
	return Hunk_Alloc(size);
}

static int STEFX_HolomatchDebugLineCreate(void)
{
	return 0;
}

static void STEFX_HolomatchDebugLineDelete(int line)
{
	(void)line;
}

static void STEFX_HolomatchDebugLineShow(int line, vec3_t start, vec3_t end,
	int color)
{
	(void)line;
	(void)start;
	(void)end;
	(void)color;
}

static int STEFX_HolomatchDebugPolygonCreate(int color, int numPoints,
	vec3_t *points)
{
	(void)color;
	(void)numPoints;
	(void)points;
	return 0;
}

static void STEFX_HolomatchDebugPolygonDelete(int id)
{
	(void)id;
}

static qboolean STEFX_HolomatchEnsureBotlib(void)
{
	botlib_import_t imports;
	if (s_stefxBotlib)
	{
		return qtrue;
	}

	memset(&imports, 0, sizeof(imports));
	imports.Print = STEFX_HolomatchBotPrint;
	imports.Trace = STEFX_HolomatchBotTrace;
	imports.EntityTrace = STEFX_HolomatchBotEntityTrace;
	imports.PointContents = STEFX_HolomatchBotPointContents;
	imports.inPVS = STEFX_HolomatchBotInPVS;
	imports.BSPEntityData = STEFX_HolomatchBotBSPEntityData;
	imports.BSPModelMinsMaxsOrigin = STEFX_HolomatchBotModelBounds;
	imports.BotClientCommand = STEFX_HolomatchBotClientCommand;
	imports.GetMemory = STEFX_HolomatchBotGetMemory;
	imports.FreeMemory = STEFX_HolomatchBotFreeMemory;
	imports.AvailableMemory = STEFX_HolomatchBotAvailableMemory;
	imports.HunkAlloc = STEFX_HolomatchBotHunkAlloc;
	imports.FS_FOpenFile = FS_FOpenFileByMode;
	imports.FS_Read = FS_Read2;
	imports.FS_Write = FS_Write;
	imports.FS_FCloseFile = FS_FCloseFile;
	imports.FS_Seek = FS_Seek;
	imports.DebugLineCreate = STEFX_HolomatchDebugLineCreate;
	imports.DebugLineDelete = STEFX_HolomatchDebugLineDelete;
	imports.DebugLineShow = STEFX_HolomatchDebugLineShow;
	imports.DebugPolygonCreate = STEFX_HolomatchDebugPolygonCreate;
	imports.DebugPolygonDelete = STEFX_HolomatchDebugPolygonDelete;

	s_stefxBotlib = GetBotLibAPI(BOTLIB_API_VERSION, &imports);
	XBLog_WriteCriticalf("STEFX_HM_BOTLIB: API attach export=%p version=%d",
		s_stefxBotlib, BOTLIB_API_VERSION);
	return s_stefxBotlib ? qtrue : qfalse;
}

static int STEFX_HolomatchBotSnapshotEntity(int clientNum, int sequence)
{
	int i;
	int visibleSequence = 0;
	(void)clientNum;
	if (!ge || !ge->gentities || sequence < 0)
	{
		return -1;
	}
	for (i = 0; i < ge->num_entities; ++i)
	{
		gentity_t *entity = &ge->gentities[i];
		if (!entity->inuse || !entity->linked || (entity->svFlags & SVF_NOCLIENT))
		{
			continue;
		}
		if (visibleSequence++ == sequence)
		{
			return entity->s.number;
		}
	}
	return -1;
}

static int STEFX_HolomatchBotConsoleMessage(int clientNum, char *message,
	int messageSize)
{
	client_t *client;
	int index;
	if (!svs.clients || clientNum < 0 || clientNum >= MAX_CLIENTS ||
		!message || messageSize <= 0)
	{
		return qfalse;
	}
	client = &svs.clients[clientNum];
	client->lastPacketTime = sv.time;
	if (client->reliableAcknowledge == client->reliableSequence)
	{
		return qfalse;
	}
	++client->reliableAcknowledge;
	index = client->reliableAcknowledge & (MAX_RELIABLE_COMMANDS - 1);
	if (!client->reliableCommands[index] || !client->reliableCommands[index][0])
	{
		return qfalse;
	}
	Q_strncpyz(message, client->reliableCommands[index], messageSize);
	return qtrue;
}

typedef struct stefx_hm_bot_usercmd_s {
	int serverTime;
	byte buttons;
	byte weapon;
	int angles[3];
	signed char forwardmove;
	signed char rightmove;
	signed char upmove;
} stefx_hm_bot_usercmd_t;

static int STEFX_HolomatchOfficialButtonsToSP(byte buttons)
{
	int result = buttons & (1 | 2 | 4 | 8 | 16);
	if (buttons & 64) result |= BUTTON_USE;
	if (buttons & 32) result |= BUTTON_ALT_ATTACK;
	if (buttons & 128) result |= BUTTON_ANY;
	return result;
}

static void STEFX_HolomatchBotUserCommand(int clientNum,
	const stefx_hm_bot_usercmd_t *officialCommand)
{
	client_t *client;
	usercmd_t command;
	if (!svs.clients || clientNum < 0 || clientNum >= MAX_CLIENTS ||
		!officialCommand)
	{
		return;
	}
	client = &svs.clients[clientNum];
	if (client->state != CS_ACTIVE)
	{
		return;
	}
	memset(&command, 0, sizeof(command));
	command.serverTime = officialCommand->serverTime;
	command.buttons = STEFX_HolomatchOfficialButtonsToSP(officialCommand->buttons);
	command.weapon = officialCommand->weapon;
	command.angles[0] = officialCommand->angles[0];
	command.angles[1] = officialCommand->angles[1];
	command.angles[2] = officialCommand->angles[2];
	command.forwardmove = officialCommand->forwardmove;
	command.rightmove = officialCommand->rightmove;
	command.upmove = officialCommand->upmove;
	if (s_stefxBotCommandLogBudget > 0 &&
		(command.forwardmove || command.rightmove || command.upmove || command.buttons))
	{
		XBLog_WriteCriticalf("STEFX_HM_BOTCMD: client=%d time=%d move=(%d,%d,%d) buttons=0x%x weapon=%d",
			clientNum, command.serverTime, command.forwardmove, command.rightmove,
			command.upmove, command.buttons, command.weapon);
		--s_stefxBotCommandLogBudget;
	}
	SV_ClientThink(client, &command);
}

void STEFX_HolomatchBotReset(void)
{
	s_stefxBotlib = NULL;
	s_stefxBotlibSetup = qfalse;
	s_stefxBotlibFrameLogBudget = 12;
	s_stefxBotCommandLogBudget = 96;
	s_stefxBotAreaLogBudget = 96;
	s_stefxBotGoalLogBudget = 96;
	s_stefxBotMoveLogBudget = 96;
	s_stefxBotInputLogBudget = 96;
}

enum stefx_hm_bot_command_e {
	HM_BOTLIB_SETUP = 200,
	HM_BOTLIB_SHUTDOWN,
	HM_BOTLIB_LIBVAR_SET,
	HM_BOTLIB_LIBVAR_GET,
	HM_BOTLIB_DEFINE,
	HM_BOTLIB_START_FRAME,
	HM_BOTLIB_LOAD_MAP,
	HM_BOTLIB_UPDATE_ENTITY,
	HM_BOTLIB_TEST,
	HM_BOTLIB_GET_SNAPSHOT_ENTITY,
	HM_BOTLIB_GET_CONSOLE_MESSAGE,
	HM_BOTLIB_USER_COMMAND,
	HM_BOTLIB_AAS_ENTITY_VISIBLE = 300,
	HM_BOTLIB_AAS_IN_FIELD_OF_VISION,
	HM_BOTLIB_AAS_VISIBLE_CLIENTS,
	HM_BOTLIB_AAS_ENTITY_INFO,
	HM_BOTLIB_AAS_INITIALIZED,
	HM_BOTLIB_AAS_PRESENCE_TYPE_BOUNDING_BOX,
	HM_BOTLIB_AAS_TIME,
	HM_BOTLIB_AAS_POINT_AREA_NUM,
	HM_BOTLIB_AAS_TRACE_AREAS,
	HM_BOTLIB_AAS_POINT_CONTENTS,
	HM_BOTLIB_AAS_NEXT_BSP_ENTITY,
	HM_BOTLIB_AAS_VALUE_FOR_BSP_EPAIR_KEY,
	HM_BOTLIB_AAS_VECTOR_FOR_BSP_EPAIR_KEY,
	HM_BOTLIB_AAS_FLOAT_FOR_BSP_EPAIR_KEY,
	HM_BOTLIB_AAS_INT_FOR_BSP_EPAIR_KEY,
	HM_BOTLIB_AAS_AREA_REACHABILITY,
	HM_BOTLIB_AAS_AREA_TRAVEL_TIME_TO_GOAL_AREA,
	HM_BOTLIB_AAS_SWIMMING,
	HM_BOTLIB_AAS_PREDICT_CLIENT_MOVEMENT,
	HM_BOTLIB_EA_SAY = 400,
	HM_BOTLIB_EA_SAY_TEAM,
	HM_BOTLIB_EA_USE_ITEM,
	HM_BOTLIB_EA_DROP_ITEM,
	HM_BOTLIB_EA_USE_INV,
	HM_BOTLIB_EA_DROP_INV,
	HM_BOTLIB_EA_GESTURE,
	HM_BOTLIB_EA_COMMAND,
	HM_BOTLIB_EA_SELECT_WEAPON,
	HM_BOTLIB_EA_TALK,
	HM_BOTLIB_EA_ATTACK,
	HM_BOTLIB_EA_USE,
	HM_BOTLIB_EA_RESPAWN,
	HM_BOTLIB_EA_JUMP,
	HM_BOTLIB_EA_DELAYED_JUMP,
	HM_BOTLIB_EA_CROUCH,
	HM_BOTLIB_EA_MOVE_UP,
	HM_BOTLIB_EA_MOVE_DOWN,
	HM_BOTLIB_EA_MOVE_FORWARD,
	HM_BOTLIB_EA_MOVE_BACK,
	HM_BOTLIB_EA_MOVE_LEFT,
	HM_BOTLIB_EA_MOVE_RIGHT,
	HM_BOTLIB_EA_MOVE,
	HM_BOTLIB_EA_VIEW,
	HM_BOTLIB_EA_END_REGULAR,
	HM_BOTLIB_EA_GET_INPUT,
	HM_BOTLIB_EA_RESET_INPUT,
	HM_BOTLIB_EA_ALT_ATTACK,
	HM_BOTLIB_AI_LOAD_CHARACTER = 500,
	HM_BOTLIB_AI_FREE_CHARACTER,
	HM_BOTLIB_AI_CHARACTERISTIC_FLOAT,
	HM_BOTLIB_AI_CHARACTERISTIC_BFLOAT,
	HM_BOTLIB_AI_CHARACTERISTIC_INTEGER,
	HM_BOTLIB_AI_CHARACTERISTIC_BINTEGER,
	HM_BOTLIB_AI_CHARACTERISTIC_STRING,
	HM_BOTLIB_AI_ALLOC_CHAT_STATE,
	HM_BOTLIB_AI_FREE_CHAT_STATE,
	HM_BOTLIB_AI_QUEUE_CONSOLE_MESSAGE,
	HM_BOTLIB_AI_REMOVE_CONSOLE_MESSAGE,
	HM_BOTLIB_AI_NEXT_CONSOLE_MESSAGE,
	HM_BOTLIB_AI_NUM_CONSOLE_MESSAGE,
	HM_BOTLIB_AI_INITIAL_CHAT,
	HM_BOTLIB_AI_REPLY_CHAT,
	HM_BOTLIB_AI_CHAT_LENGTH,
	HM_BOTLIB_AI_ENTER_CHAT,
	HM_BOTLIB_AI_STRING_CONTAINS,
	HM_BOTLIB_AI_FIND_MATCH,
	HM_BOTLIB_AI_MATCH_VARIABLE,
	HM_BOTLIB_AI_UNIFY_WHITE_SPACES,
	HM_BOTLIB_AI_REPLACE_SYNONYMS,
	HM_BOTLIB_AI_LOAD_CHAT_FILE,
	HM_BOTLIB_AI_SET_CHAT_GENDER,
	HM_BOTLIB_AI_SET_CHAT_NAME,
	HM_BOTLIB_AI_RESET_GOAL_STATE,
	HM_BOTLIB_AI_RESET_AVOID_GOALS,
	HM_BOTLIB_AI_PUSH_GOAL,
	HM_BOTLIB_AI_POP_GOAL,
	HM_BOTLIB_AI_EMPTY_GOAL_STACK,
	HM_BOTLIB_AI_DUMP_AVOID_GOALS,
	HM_BOTLIB_AI_DUMP_GOAL_STACK,
	HM_BOTLIB_AI_GOAL_NAME,
	HM_BOTLIB_AI_GET_TOP_GOAL,
	HM_BOTLIB_AI_GET_SECOND_GOAL,
	HM_BOTLIB_AI_CHOOSE_LTG_ITEM,
	HM_BOTLIB_AI_CHOOSE_NBG_ITEM,
	HM_BOTLIB_AI_TOUCHING_GOAL,
	HM_BOTLIB_AI_ITEM_GOAL_IN_VIS_BUT_NOT_VISIBLE,
	HM_BOTLIB_AI_GET_LEVEL_ITEM_GOAL,
	HM_BOTLIB_AI_AVOID_GOAL_TIME,
	HM_BOTLIB_AI_INIT_LEVEL_ITEMS,
	HM_BOTLIB_AI_UPDATE_ENTITY_ITEMS,
	HM_BOTLIB_AI_LOAD_ITEM_WEIGHTS,
	HM_BOTLIB_AI_FREE_ITEM_WEIGHTS,
	HM_BOTLIB_AI_SAVE_GOAL_FUZZY_LOGIC,
	HM_BOTLIB_AI_ALLOC_GOAL_STATE,
	HM_BOTLIB_AI_FREE_GOAL_STATE,
	HM_BOTLIB_AI_RESET_MOVE_STATE,
	HM_BOTLIB_AI_MOVE_TO_GOAL,
	HM_BOTLIB_AI_MOVE_IN_DIRECTION,
	HM_BOTLIB_AI_RESET_AVOID_REACH,
	HM_BOTLIB_AI_RESET_LAST_AVOID_REACH,
	HM_BOTLIB_AI_REACHABILITY_AREA,
	HM_BOTLIB_AI_MOVEMENT_VIEW_TARGET,
	HM_BOTLIB_AI_ALLOC_MOVE_STATE,
	HM_BOTLIB_AI_FREE_MOVE_STATE,
	HM_BOTLIB_AI_INIT_MOVE_STATE,
	HM_BOTLIB_AI_CHOOSE_BEST_FIGHT_WEAPON,
	HM_BOTLIB_AI_GET_WEAPON_INFO,
	HM_BOTLIB_AI_LOAD_WEAPON_WEIGHTS,
	HM_BOTLIB_AI_ALLOC_WEAPON_STATE,
	HM_BOTLIB_AI_FREE_WEAPON_STATE,
	HM_BOTLIB_AI_RESET_WEAPON_STATE,
	HM_BOTLIB_AI_GENETIC_PARENTS_AND_CHILD_SELECTION,
	HM_BOTLIB_AI_INTERBREED_GOAL_FUZZY_LOGIC,
	HM_BOTLIB_AI_MUTATE_GOAL_FUZZY_LOGIC,
	HM_BOTLIB_AI_GET_NEXT_CAMP_SPOT_GOAL,
	HM_BOTLIB_AI_GET_MAP_LOCATION_GOAL,
	HM_BOTLIB_AI_NUM_INITIAL_CHATS,
	HM_BOTLIB_AI_GET_CHAT_MESSAGE,
	HM_BOTLIB_AI_REMOVE_FROM_AVOID_GOALS,
	HM_BOTLIB_AI_PREDICT_VISIBLE_POSITION
};

int STEFX_HolomatchBotSyscall(int command, va_list args, int *result)
{
	if (command < HM_BOTLIB_SETUP)
	{
		return qfalse;
	}
	if (!result || !STEFX_HolomatchEnsureBotlib())
	{
		return qtrue;
	}

	*result = 0;
	switch (command)
	{
	case HM_BOTLIB_SETUP:
		*result = s_stefxBotlib->BotLibSetup();
		s_stefxBotlibSetup = (*result == BLERR_NOERROR);
		XBLog_WriteCriticalf("STEFX_HM_BOTLIB: setup result=%d active=%d",
			*result, s_stefxBotlibSetup);
		break;
	case HM_BOTLIB_SHUTDOWN:
		*result = s_stefxBotlib->BotLibShutdown();
		s_stefxBotlibSetup = qfalse;
		XBLog_WriteCriticalf("STEFX_HM_BOTLIB: shutdown result=%d", *result);
		break;
	case HM_BOTLIB_LIBVAR_SET:
		{
			char *name = va_arg(args, char *);
			char *value = va_arg(args, char *);
			*result = s_stefxBotlib->BotLibVarSet(name, value);
		}
		break;
	case HM_BOTLIB_LIBVAR_GET:
		{
			char *name = va_arg(args, char *);
			char *value = va_arg(args, char *);
			int size = va_arg(args, int);
			*result = s_stefxBotlib->BotLibVarGet(name, value, size);
		}
		break;
	case HM_BOTLIB_DEFINE:
		*result = s_stefxBotlib->PC_AddGlobalDefine(va_arg(args, char *));
		break;
	case HM_BOTLIB_START_FRAME:
		{
			float time = STEFX_HolomatchIntAsFloat(va_arg(args, int));
			*result = s_stefxBotlib->BotLibStartFrame(time);
			if (s_stefxBotlibFrameLogBudget > 0)
			{
				XBLog_WriteCriticalf("STEFX_HM_BOTLIB: frame time=%g result=%d aas=%d",
					time, *result, s_stefxBotlib->aas.AAS_Initialized());
				--s_stefxBotlibFrameLogBudget;
			}
		}
		break;
	case HM_BOTLIB_LOAD_MAP:
		{
			char *mapName = va_arg(args, char *);
			char engineChecksum[32] = { 0 };
			char botlibChecksum[32] = { 0 };
			char synchronizedChecksum[32] = { 0 };

			Cvar_VariableStringBuffer("sv_mapChecksum", engineChecksum,
				sizeof(engineChecksum));
			s_stefxBotlib->BotLibVarGet("sv_mapChecksum", botlibChecksum,
				sizeof(botlibChecksum));
			XBLog_WriteCriticalf(
				"STEFX_HM_BOTLIB: load map enter '%s' checksum engine='%s' botlib-before='%s'",
				mapName ? mapName : "", engineChecksum, botlibChecksum);

			// The embedded botlib survives SP-hosted map transitions, so refresh the
			// per-map value that official game code normally supplies at setup.
			s_stefxBotlib->BotLibVarSet("sv_mapChecksum", engineChecksum);
			s_stefxBotlib->BotLibVarGet("sv_mapChecksum", synchronizedChecksum,
				sizeof(synchronizedChecksum));
			*result = s_stefxBotlib->BotLibLoadMap(mapName);
			XBLog_WriteCriticalf(
				"STEFX_HM_BOTLIB: load map exit result=%d aas=%d botlib-after='%s'",
				*result, s_stefxBotlib->aas.AAS_Initialized(),
				synchronizedChecksum);
		}
		break;
	case HM_BOTLIB_UPDATE_ENTITY:
		{
			int entityNum = va_arg(args, int);
			bot_entitystate_t *state = va_arg(args, bot_entitystate_t *);
			*result = s_stefxBotlib->BotLibUpdateEntity(entityNum, state);
		}
		break;
	case HM_BOTLIB_TEST:
		{
			int parm0 = va_arg(args, int);
			char *parm1 = va_arg(args, char *);
			vec3_t *parm2 = va_arg(args, vec3_t *);
			vec3_t *parm3 = va_arg(args, vec3_t *);
			vec3_t zero = { 0.0f, 0.0f, 0.0f };
			*result = s_stefxBotlib->Test(parm0, parm1,
				parm2 ? *parm2 : zero, parm3 ? *parm3 : zero);
		}
		break;
	case HM_BOTLIB_GET_SNAPSHOT_ENTITY:
		{
			int clientNum = va_arg(args, int);
			int sequence = va_arg(args, int);
			*result = STEFX_HolomatchBotSnapshotEntity(clientNum, sequence);
		}
		break;
	case HM_BOTLIB_GET_CONSOLE_MESSAGE:
		{
			int clientNum = va_arg(args, int);
			char *message = va_arg(args, char *);
			int size = va_arg(args, int);
			*result = STEFX_HolomatchBotConsoleMessage(clientNum, message, size);
		}
		break;
	case HM_BOTLIB_USER_COMMAND:
		{
			int clientNum = va_arg(args, int);
			stefx_hm_bot_usercmd_t *userCommand = va_arg(args, stefx_hm_bot_usercmd_t *);
			STEFX_HolomatchBotUserCommand(clientNum, userCommand);
		}
		break;
	case HM_BOTLIB_AAS_ENTITY_VISIBLE:
	case HM_BOTLIB_AAS_IN_FIELD_OF_VISION:
	case HM_BOTLIB_AAS_VISIBLE_CLIENTS:
		break;
	case HM_BOTLIB_AAS_ENTITY_INFO:
		{
			int entityNum = va_arg(args, int);
			struct aas_entityinfo_s *info = va_arg(args, struct aas_entityinfo_s *);
			s_stefxBotlib->aas.AAS_EntityInfo(entityNum, info);
		}
		break;
	case HM_BOTLIB_AAS_INITIALIZED:
		*result = s_stefxBotlib->aas.AAS_Initialized();
		break;
	case HM_BOTLIB_AAS_PRESENCE_TYPE_BOUNDING_BOX:
		{
			int presenceType = va_arg(args, int);
			vec3_t *mins = va_arg(args, vec3_t *);
			vec3_t *maxs = va_arg(args, vec3_t *);
			s_stefxBotlib->aas.AAS_PresenceTypeBoundingBox(presenceType, *mins, *maxs);
		}
		break;
	case HM_BOTLIB_AAS_TIME:
		*result = STEFX_HolomatchFloatAsInt(s_stefxBotlib->aas.AAS_Time());
		break;
	case HM_BOTLIB_AAS_POINT_AREA_NUM:
		{
			vec3_t *point = va_arg(args, vec3_t *);
			*result = s_stefxBotlib->aas.AAS_PointAreaNum(*point);
			if (s_stefxBotAreaLogBudget > 0)
			{
				XBLog_WriteCriticalf("STEFX_HM_BOTLIB: point-area point=(%g,%g,%g) result=%d",
					(*point)[0], (*point)[1], (*point)[2], *result);
				--s_stefxBotAreaLogBudget;
			}
		}
		break;
	case HM_BOTLIB_AAS_TRACE_AREAS:
		{
			vec3_t *start = va_arg(args, vec3_t *);
			vec3_t *end = va_arg(args, vec3_t *);
			int *areas = va_arg(args, int *);
			vec3_t *points = va_arg(args, vec3_t *);
			int maxAreas = va_arg(args, int);
			*result = s_stefxBotlib->aas.AAS_TraceAreas(*start, *end, areas, points, maxAreas);
		}
		break;
	case HM_BOTLIB_AAS_POINT_CONTENTS:
		{
			vec3_t *point = va_arg(args, vec3_t *);
			*result = s_stefxBotlib->aas.AAS_PointContents(*point);
		}
		break;
	case HM_BOTLIB_AAS_NEXT_BSP_ENTITY:
		*result = s_stefxBotlib->aas.AAS_NextBSPEntity(va_arg(args, int));
		break;
	case HM_BOTLIB_AAS_VALUE_FOR_BSP_EPAIR_KEY:
		{
			int entityNum = va_arg(args, int);
			char *key = va_arg(args, char *);
			char *value = va_arg(args, char *);
			int size = va_arg(args, int);
			*result = s_stefxBotlib->aas.AAS_ValueForBSPEpairKey(entityNum, key, value, size);
		}
		break;
	case HM_BOTLIB_AAS_VECTOR_FOR_BSP_EPAIR_KEY:
		{
			int entityNum = va_arg(args, int);
			char *key = va_arg(args, char *);
			vec3_t *value = va_arg(args, vec3_t *);
			*result = s_stefxBotlib->aas.AAS_VectorForBSPEpairKey(entityNum, key, *value);
		}
		break;
	case HM_BOTLIB_AAS_FLOAT_FOR_BSP_EPAIR_KEY:
		{
			int entityNum = va_arg(args, int);
			char *key = va_arg(args, char *);
			float *value = va_arg(args, float *);
			*result = s_stefxBotlib->aas.AAS_FloatForBSPEpairKey(entityNum, key, value);
		}
		break;
	case HM_BOTLIB_AAS_INT_FOR_BSP_EPAIR_KEY:
		{
			int entityNum = va_arg(args, int);
			char *key = va_arg(args, char *);
			int *value = va_arg(args, int *);
			*result = s_stefxBotlib->aas.AAS_IntForBSPEpairKey(entityNum, key, value);
		}
		break;
	case HM_BOTLIB_AAS_AREA_REACHABILITY:
		*result = s_stefxBotlib->aas.AAS_AreaReachability(va_arg(args, int));
		break;
	case HM_BOTLIB_AAS_AREA_TRAVEL_TIME_TO_GOAL_AREA:
		{
			int areaNum = va_arg(args, int);
			vec3_t *origin = va_arg(args, vec3_t *);
			int goalArea = va_arg(args, int);
			int travelFlags = va_arg(args, int);
			*result = s_stefxBotlib->aas.AAS_AreaTravelTimeToGoalArea(areaNum, *origin,
				goalArea, travelFlags);
		}
		break;
	case HM_BOTLIB_AAS_SWIMMING:
		{
			vec3_t *origin = va_arg(args, vec3_t *);
			*result = s_stefxBotlib->aas.AAS_Swimming(*origin);
		}
		break;
	case HM_BOTLIB_AAS_PREDICT_CLIENT_MOVEMENT:
		{
			struct aas_clientmove_s *move = va_arg(args, struct aas_clientmove_s *);
			int entityNum = va_arg(args, int);
			vec3_t *origin = va_arg(args, vec3_t *);
			int presenceType = va_arg(args, int);
			int onGround = va_arg(args, int);
			vec3_t *velocity = va_arg(args, vec3_t *);
			vec3_t *commandMove = va_arg(args, vec3_t *);
			int commandFrames = va_arg(args, int);
			int maxFrames = va_arg(args, int);
			float frameTime = STEFX_HolomatchIntAsFloat(va_arg(args, int));
			int stopEvent = va_arg(args, int);
			int stopArea = va_arg(args, int);
			int visualize = va_arg(args, int);
			*result = s_stefxBotlib->aas.AAS_PredictClientMovement(move, entityNum,
				*origin, presenceType, onGround, *velocity, *commandMove, commandFrames,
				maxFrames, frameTime, stopEvent, stopArea, visualize);
		}
		break;
	case HM_BOTLIB_EA_SAY:
		{
			int clientNum = va_arg(args, int);
			char *text = va_arg(args, char *);
			s_stefxBotlib->ea.EA_Say(clientNum, text);
		}
		break;
	case HM_BOTLIB_EA_SAY_TEAM:
		{
			int clientNum = va_arg(args, int);
			char *text = va_arg(args, char *);
			s_stefxBotlib->ea.EA_SayTeam(clientNum, text);
		}
		break;
	case HM_BOTLIB_EA_USE_ITEM:
		{
			int clientNum = va_arg(args, int);
			char *item = va_arg(args, char *);
			s_stefxBotlib->ea.EA_UseItem(clientNum, item);
		}
		break;
	case HM_BOTLIB_EA_DROP_ITEM:
		{
			int clientNum = va_arg(args, int);
			char *item = va_arg(args, char *);
			s_stefxBotlib->ea.EA_DropItem(clientNum, item);
		}
		break;
	case HM_BOTLIB_EA_USE_INV:
		{
			int clientNum = va_arg(args, int);
			char *inventory = va_arg(args, char *);
			s_stefxBotlib->ea.EA_UseInv(clientNum, inventory);
		}
		break;
	case HM_BOTLIB_EA_DROP_INV:
		{
			int clientNum = va_arg(args, int);
			char *inventory = va_arg(args, char *);
			s_stefxBotlib->ea.EA_DropInv(clientNum, inventory);
		}
		break;
	case HM_BOTLIB_EA_GESTURE:
		s_stefxBotlib->ea.EA_Gesture(va_arg(args, int));
		break;
	case HM_BOTLIB_EA_COMMAND:
		{
			int clientNum = va_arg(args, int);
			char *commandText = va_arg(args, char *);
			s_stefxBotlib->ea.EA_Command(clientNum, commandText);
		}
		break;
	case HM_BOTLIB_EA_SELECT_WEAPON:
		{
			int clientNum = va_arg(args, int);
			int weapon = va_arg(args, int);
			s_stefxBotlib->ea.EA_SelectWeapon(clientNum, weapon);
		}
		break;
	case HM_BOTLIB_EA_TALK:
		s_stefxBotlib->ea.EA_Talk(va_arg(args, int));
		break;
	case HM_BOTLIB_EA_ATTACK:
		s_stefxBotlib->ea.EA_Attack(va_arg(args, int));
		break;
	case HM_BOTLIB_EA_USE:
		s_stefxBotlib->ea.EA_Use(va_arg(args, int));
		break;
	case HM_BOTLIB_EA_RESPAWN:
		s_stefxBotlib->ea.EA_Respawn(va_arg(args, int));
		break;
	case HM_BOTLIB_EA_JUMP:
		s_stefxBotlib->ea.EA_Jump(va_arg(args, int));
		break;
	case HM_BOTLIB_EA_DELAYED_JUMP:
		s_stefxBotlib->ea.EA_DelayedJump(va_arg(args, int));
		break;
	case HM_BOTLIB_EA_CROUCH:
		s_stefxBotlib->ea.EA_Crouch(va_arg(args, int));
		break;
	case HM_BOTLIB_EA_MOVE_UP:
		s_stefxBotlib->ea.EA_MoveUp(va_arg(args, int));
		break;
	case HM_BOTLIB_EA_MOVE_DOWN:
		s_stefxBotlib->ea.EA_MoveDown(va_arg(args, int));
		break;
	case HM_BOTLIB_EA_MOVE_FORWARD:
		s_stefxBotlib->ea.EA_MoveForward(va_arg(args, int));
		break;
	case HM_BOTLIB_EA_MOVE_BACK:
		s_stefxBotlib->ea.EA_MoveBack(va_arg(args, int));
		break;
	case HM_BOTLIB_EA_MOVE_LEFT:
		s_stefxBotlib->ea.EA_MoveLeft(va_arg(args, int));
		break;
	case HM_BOTLIB_EA_MOVE_RIGHT:
		s_stefxBotlib->ea.EA_MoveRight(va_arg(args, int));
		break;
	case HM_BOTLIB_EA_MOVE:
		{
			int clientNum = va_arg(args, int);
			vec3_t *direction = va_arg(args, vec3_t *);
			float speed = STEFX_HolomatchIntAsFloat(va_arg(args, int));
			s_stefxBotlib->ea.EA_Move(clientNum, *direction, speed);
			if (s_stefxBotMoveLogBudget > 0)
			{
				XBLog_WriteCriticalf("STEFX_HM_BOTLIB: ea-move client=%d dir=(%g,%g,%g) speed=%g",
					clientNum, (*direction)[0], (*direction)[1], (*direction)[2], speed);
				--s_stefxBotMoveLogBudget;
			}
		}
		break;
	case HM_BOTLIB_EA_VIEW:
		{
			int clientNum = va_arg(args, int);
			vec3_t *view = va_arg(args, vec3_t *);
			s_stefxBotlib->ea.EA_View(clientNum, *view);
		}
		break;
	case HM_BOTLIB_EA_END_REGULAR:
		{
			int clientNum = va_arg(args, int);
			float thinkTime = STEFX_HolomatchIntAsFloat(va_arg(args, int));
			s_stefxBotlib->ea.EA_EndRegular(clientNum, thinkTime);
		}
		break;
	case HM_BOTLIB_EA_GET_INPUT:
		{
			int clientNum = va_arg(args, int);
			float thinkTime = STEFX_HolomatchIntAsFloat(va_arg(args, int));
			bot_input_t *input = va_arg(args, bot_input_t *);
			s_stefxBotlib->ea.EA_GetInput(clientNum, thinkTime, input);
			if (clientNum > 0 && s_stefxBotInputLogBudget > 0)
			{
				XBLog_WriteCriticalf("STEFX_HM_BOTLIB: ea-input client=%d think=%g dir=(%g,%g,%g) speed=%g flags=0x%x weapon=%d",
					clientNum, thinkTime, input->dir[0], input->dir[1], input->dir[2],
					input->speed, input->actionflags, input->weapon);
				--s_stefxBotInputLogBudget;
			}
		}
		break;
	case HM_BOTLIB_EA_RESET_INPUT:
		s_stefxBotlib->ea.EA_ResetInput(va_arg(args, int));
		break;
	case HM_BOTLIB_EA_ALT_ATTACK:
		s_stefxBotlib->ea.EA_Alt_Attack(va_arg(args, int));
		break;
	case HM_BOTLIB_AI_LOAD_CHARACTER:
		{
			char *characterFile = va_arg(args, char *);
			int skill = va_arg(args, int);
			*result = s_stefxBotlib->ai.BotLoadCharacter(characterFile, (float)skill);
		}
		break;
	case HM_BOTLIB_AI_FREE_CHARACTER:
		s_stefxBotlib->ai.BotFreeCharacter(va_arg(args, int));
		break;
	case HM_BOTLIB_AI_CHARACTERISTIC_FLOAT:
		{
			int character = va_arg(args, int);
			int index = va_arg(args, int);
			*result = STEFX_HolomatchFloatAsInt(
				s_stefxBotlib->ai.Characteristic_Float(character, index));
		}
		break;
	case HM_BOTLIB_AI_CHARACTERISTIC_BFLOAT:
		{
			int character = va_arg(args, int);
			int index = va_arg(args, int);
			float minimum = STEFX_HolomatchIntAsFloat(va_arg(args, int));
			float maximum = STEFX_HolomatchIntAsFloat(va_arg(args, int));
			*result = STEFX_HolomatchFloatAsInt(s_stefxBotlib->ai.Characteristic_BFloat(
				character, index, minimum, maximum));
		}
		break;
	case HM_BOTLIB_AI_CHARACTERISTIC_INTEGER:
		{
			int character = va_arg(args, int);
			int index = va_arg(args, int);
			*result = s_stefxBotlib->ai.Characteristic_Integer(character, index);
		}
		break;
	case HM_BOTLIB_AI_CHARACTERISTIC_BINTEGER:
		{
			int character = va_arg(args, int);
			int index = va_arg(args, int);
			int minimum = va_arg(args, int);
			int maximum = va_arg(args, int);
			*result = s_stefxBotlib->ai.Characteristic_BInteger(character, index, minimum, maximum);
		}
		break;
	case HM_BOTLIB_AI_CHARACTERISTIC_STRING:
		{
			int character = va_arg(args, int);
			int index = va_arg(args, int);
			char *buffer = va_arg(args, char *);
			int size = va_arg(args, int);
			s_stefxBotlib->ai.Characteristic_String(character, index, buffer, size);
		}
		break;
	case HM_BOTLIB_AI_ALLOC_CHAT_STATE:
		*result = s_stefxBotlib->ai.BotAllocChatState();
		break;
	case HM_BOTLIB_AI_FREE_CHAT_STATE:
		s_stefxBotlib->ai.BotFreeChatState(va_arg(args, int));
		break;
	case HM_BOTLIB_AI_QUEUE_CONSOLE_MESSAGE:
		{
			int state = va_arg(args, int);
			int type = va_arg(args, int);
			char *message = va_arg(args, char *);
			s_stefxBotlib->ai.BotQueueConsoleMessage(state, type, message);
		}
		break;
	case HM_BOTLIB_AI_REMOVE_CONSOLE_MESSAGE:
		{
			int state = va_arg(args, int);
			int handle = va_arg(args, int);
			s_stefxBotlib->ai.BotRemoveConsoleMessage(state, handle);
		}
		break;
	case HM_BOTLIB_AI_NEXT_CONSOLE_MESSAGE:
		{
			int state = va_arg(args, int);
			struct bot_consolemessage_s *message = va_arg(args, struct bot_consolemessage_s *);
			*result = s_stefxBotlib->ai.BotNextConsoleMessage(state, message);
		}
		break;
	case HM_BOTLIB_AI_NUM_CONSOLE_MESSAGE:
		*result = s_stefxBotlib->ai.BotNumConsoleMessages(va_arg(args, int));
		break;
	case HM_BOTLIB_AI_INITIAL_CHAT:
		{
			int state = va_arg(args, int);
			char *type = va_arg(args, char *);
			int context = va_arg(args, int);
			char *vars[8];
			int i;
			for (i = 0; i < 8; ++i) vars[i] = va_arg(args, char *);
			s_stefxBotlib->ai.BotInitialChat(state, type, context, vars[0], vars[1],
				vars[2], vars[3], vars[4], vars[5], vars[6], vars[7]);
		}
		break;
	case HM_BOTLIB_AI_REPLY_CHAT:
		{
			int state = va_arg(args, int);
			char *message = va_arg(args, char *);
			int context = va_arg(args, int);
			int variableContext = va_arg(args, int);
			char *vars[8];
			int i;
			for (i = 0; i < 8; ++i) vars[i] = va_arg(args, char *);
			*result = s_stefxBotlib->ai.BotReplyChat(state, message, context,
				variableContext, vars[0], vars[1], vars[2], vars[3], vars[4],
				vars[5], vars[6], vars[7]);
		}
		break;
	case HM_BOTLIB_AI_CHAT_LENGTH:
		*result = s_stefxBotlib->ai.BotChatLength(va_arg(args, int));
		break;
	case HM_BOTLIB_AI_ENTER_CHAT:
		{
			int state = va_arg(args, int);
			int clientNum = va_arg(args, int);
			int sendTo = va_arg(args, int);
			s_stefxBotlib->ai.BotEnterChat(state, clientNum, sendTo);
		}
		break;
	case HM_BOTLIB_AI_STRING_CONTAINS:
		{
			char *string = va_arg(args, char *);
			char *match = va_arg(args, char *);
			int caseSensitive = va_arg(args, int);
			*result = s_stefxBotlib->ai.StringContains(string, match, caseSensitive);
		}
		break;
	case HM_BOTLIB_AI_FIND_MATCH:
		{
			char *string = va_arg(args, char *);
			struct bot_match_s *match = va_arg(args, struct bot_match_s *);
			unsigned long context = va_arg(args, unsigned long);
			*result = s_stefxBotlib->ai.BotFindMatch(string, match, context);
		}
		break;
	case HM_BOTLIB_AI_MATCH_VARIABLE:
		{
			struct bot_match_s *match = va_arg(args, struct bot_match_s *);
			int variable = va_arg(args, int);
			char *buffer = va_arg(args, char *);
			int size = va_arg(args, int);
			s_stefxBotlib->ai.BotMatchVariable(match, variable, buffer, size);
		}
		break;
	case HM_BOTLIB_AI_UNIFY_WHITE_SPACES:
		s_stefxBotlib->ai.UnifyWhiteSpaces(va_arg(args, char *));
		break;
	case HM_BOTLIB_AI_REPLACE_SYNONYMS:
		{
			char *string = va_arg(args, char *);
			unsigned long context = va_arg(args, unsigned long);
			s_stefxBotlib->ai.BotReplaceSynonyms(string, context);
		}
		break;
	case HM_BOTLIB_AI_LOAD_CHAT_FILE:
		{
			int state = va_arg(args, int);
			char *chatFile = va_arg(args, char *);
			char *chatName = va_arg(args, char *);
			*result = s_stefxBotlib->ai.BotLoadChatFile(state, chatFile, chatName);
		}
		break;
	case HM_BOTLIB_AI_SET_CHAT_GENDER:
		{
			int state = va_arg(args, int);
			int gender = va_arg(args, int);
			s_stefxBotlib->ai.BotSetChatGender(state, gender);
		}
		break;
	case HM_BOTLIB_AI_SET_CHAT_NAME:
		{
			int state = va_arg(args, int);
			char *name = va_arg(args, char *);
			s_stefxBotlib->ai.BotSetChatName(state, name);
		}
		break;
	case HM_BOTLIB_AI_RESET_GOAL_STATE:
		s_stefxBotlib->ai.BotResetGoalState(va_arg(args, int));
		break;
	case HM_BOTLIB_AI_RESET_AVOID_GOALS:
		s_stefxBotlib->ai.BotResetAvoidGoals(va_arg(args, int));
		break;
	case HM_BOTLIB_AI_PUSH_GOAL:
		{
			int state = va_arg(args, int);
			struct bot_goal_s *goal = va_arg(args, struct bot_goal_s *);
			s_stefxBotlib->ai.BotPushGoal(state, goal);
		}
		break;
	case HM_BOTLIB_AI_POP_GOAL:
		s_stefxBotlib->ai.BotPopGoal(va_arg(args, int));
		break;
	case HM_BOTLIB_AI_EMPTY_GOAL_STACK:
		s_stefxBotlib->ai.BotEmptyGoalStack(va_arg(args, int));
		break;
	case HM_BOTLIB_AI_DUMP_AVOID_GOALS:
		s_stefxBotlib->ai.BotDumpAvoidGoals(va_arg(args, int));
		break;
	case HM_BOTLIB_AI_DUMP_GOAL_STACK:
		s_stefxBotlib->ai.BotDumpGoalStack(va_arg(args, int));
		break;
	case HM_BOTLIB_AI_GOAL_NAME:
		{
			int number = va_arg(args, int);
			char *name = va_arg(args, char *);
			int size = va_arg(args, int);
			s_stefxBotlib->ai.BotGoalName(number, name, size);
		}
		break;
	case HM_BOTLIB_AI_GET_TOP_GOAL:
		{
			int state = va_arg(args, int);
			struct bot_goal_s *goal = va_arg(args, struct bot_goal_s *);
			*result = s_stefxBotlib->ai.BotGetTopGoal(state, goal);
		}
		break;
	case HM_BOTLIB_AI_GET_SECOND_GOAL:
		{
			int state = va_arg(args, int);
			struct bot_goal_s *goal = va_arg(args, struct bot_goal_s *);
			*result = s_stefxBotlib->ai.BotGetSecondGoal(state, goal);
		}
		break;
	case HM_BOTLIB_AI_CHOOSE_LTG_ITEM:
		{
			int state = va_arg(args, int);
			vec3_t *origin = va_arg(args, vec3_t *);
			int *inventory = va_arg(args, int *);
			int travelFlags = va_arg(args, int);
			*result = s_stefxBotlib->ai.BotChooseLTGItem(state, *origin, inventory,
				travelFlags, qfalse);
			if (s_stefxBotGoalLogBudget > 0)
			{
				XBLog_WriteCriticalf("STEFX_HM_BOTLIB: choose-ltg state=%d origin=(%g,%g,%g) tfl=0x%x result=%d",
					state, (*origin)[0], (*origin)[1], (*origin)[2], travelFlags, *result);
				--s_stefxBotGoalLogBudget;
			}
		}
		break;
	case HM_BOTLIB_AI_CHOOSE_NBG_ITEM:
		{
			int state = va_arg(args, int);
			vec3_t *origin = va_arg(args, vec3_t *);
			int *inventory = va_arg(args, int *);
			int travelFlags = va_arg(args, int);
			struct bot_goal_s *goal = va_arg(args, struct bot_goal_s *);
			float maxTime = STEFX_HolomatchIntAsFloat(va_arg(args, int));
			*result = s_stefxBotlib->ai.BotChooseNBGItem(state, *origin, inventory,
				travelFlags, goal, maxTime, qfalse);
		}
		break;
	case HM_BOTLIB_AI_TOUCHING_GOAL:
		{
			vec3_t *origin = va_arg(args, vec3_t *);
			struct bot_goal_s *goal = va_arg(args, struct bot_goal_s *);
			*result = s_stefxBotlib->ai.BotTouchingGoal(*origin, goal);
		}
		break;
	case HM_BOTLIB_AI_ITEM_GOAL_IN_VIS_BUT_NOT_VISIBLE:
		{
			int viewer = va_arg(args, int);
			vec3_t *eye = va_arg(args, vec3_t *);
			vec3_t *viewAngles = va_arg(args, vec3_t *);
			struct bot_goal_s *goal = va_arg(args, struct bot_goal_s *);
			*result = s_stefxBotlib->ai.BotItemGoalInVisButNotVisible(viewer, *eye,
				*viewAngles, goal);
		}
		break;
	case HM_BOTLIB_AI_GET_LEVEL_ITEM_GOAL:
		{
			int index = va_arg(args, int);
			char *className = va_arg(args, char *);
			struct bot_goal_s *goal = va_arg(args, struct bot_goal_s *);
			*result = s_stefxBotlib->ai.BotGetLevelItemGoal(index, className, goal);
		}
		break;
	case HM_BOTLIB_AI_AVOID_GOAL_TIME:
		{
			int state = va_arg(args, int);
			int goalNumber = va_arg(args, int);
			*result = STEFX_HolomatchFloatAsInt(
				s_stefxBotlib->ai.BotAvoidGoalTime(state, goalNumber));
		}
		break;
	case HM_BOTLIB_AI_INIT_LEVEL_ITEMS:
		s_stefxBotlib->ai.BotInitLevelItems();
		break;
	case HM_BOTLIB_AI_UPDATE_ENTITY_ITEMS:
		s_stefxBotlib->ai.BotUpdateEntityItems();
		break;
	case HM_BOTLIB_AI_LOAD_ITEM_WEIGHTS:
		{
			int state = va_arg(args, int);
			char *filename = va_arg(args, char *);
			*result = s_stefxBotlib->ai.BotLoadItemWeights(state, filename);
		}
		break;
	case HM_BOTLIB_AI_FREE_ITEM_WEIGHTS:
		s_stefxBotlib->ai.BotFreeItemWeights(va_arg(args, int));
		break;
	case HM_BOTLIB_AI_SAVE_GOAL_FUZZY_LOGIC:
		{
			int state = va_arg(args, int);
			char *filename = va_arg(args, char *);
			s_stefxBotlib->ai.BotSaveGoalFuzzyLogic(state, filename);
		}
		break;
	case HM_BOTLIB_AI_ALLOC_GOAL_STATE:
		*result = s_stefxBotlib->ai.BotAllocGoalState(va_arg(args, int));
		break;
	case HM_BOTLIB_AI_FREE_GOAL_STATE:
		s_stefxBotlib->ai.BotFreeGoalState(va_arg(args, int));
		break;
	case HM_BOTLIB_AI_RESET_MOVE_STATE:
		s_stefxBotlib->ai.BotResetMoveState(va_arg(args, int));
		break;
	case HM_BOTLIB_AI_MOVE_TO_GOAL:
		{
			struct bot_moveresult_s *moveResult = va_arg(args, struct bot_moveresult_s *);
			int moveState = va_arg(args, int);
			struct bot_goal_s *goal = va_arg(args, struct bot_goal_s *);
			int travelFlags = va_arg(args, int);
			s_stefxBotlib->ai.BotMoveToGoal(moveResult, moveState, goal, travelFlags);
			if (s_stefxBotMoveLogBudget > 0)
			{
				XBLog_WriteCriticalf("STEFX_HM_BOTLIB: move-goal state=%d goalArea=%d goal=(%g,%g,%g) tfl=0x%x fail=%d blocked=%d travel=%d flags=0x%x dir=(%g,%g,%g)",
					moveState, goal ? goal->areanum : -1,
					goal ? goal->origin[0] : 0.0f, goal ? goal->origin[1] : 0.0f,
					goal ? goal->origin[2] : 0.0f, travelFlags,
					moveResult ? moveResult->failure : -1,
					moveResult ? moveResult->blocked : -1,
					moveResult ? moveResult->traveltype : -1,
					moveResult ? moveResult->flags : 0,
					moveResult ? moveResult->movedir[0] : 0.0f,
					moveResult ? moveResult->movedir[1] : 0.0f,
					moveResult ? moveResult->movedir[2] : 0.0f);
				--s_stefxBotMoveLogBudget;
			}
		}
		break;
	case HM_BOTLIB_AI_MOVE_IN_DIRECTION:
		{
			int moveState = va_arg(args, int);
			vec3_t *direction = va_arg(args, vec3_t *);
			float speed = STEFX_HolomatchIntAsFloat(va_arg(args, int));
			int type = va_arg(args, int);
			*result = s_stefxBotlib->ai.BotMoveInDirection(moveState, *direction, speed, type);
		}
		break;
	case HM_BOTLIB_AI_RESET_AVOID_REACH:
		s_stefxBotlib->ai.BotResetAvoidReach(va_arg(args, int));
		break;
	case HM_BOTLIB_AI_RESET_LAST_AVOID_REACH:
		s_stefxBotlib->ai.BotResetLastAvoidReach(va_arg(args, int));
		break;
	case HM_BOTLIB_AI_REACHABILITY_AREA:
		{
			vec3_t *origin = va_arg(args, vec3_t *);
			int testGround = va_arg(args, int);
			*result = s_stefxBotlib->ai.BotReachabilityArea(*origin, testGround);
			if (s_stefxBotAreaLogBudget > 0)
			{
				XBLog_WriteCriticalf("STEFX_HM_BOTLIB: reach-area origin=(%g,%g,%g) testGround=%d result=%d",
					(*origin)[0], (*origin)[1], (*origin)[2], testGround, *result);
				--s_stefxBotAreaLogBudget;
			}
		}
		break;
	case HM_BOTLIB_AI_MOVEMENT_VIEW_TARGET:
		{
			int moveState = va_arg(args, int);
			struct bot_goal_s *goal = va_arg(args, struct bot_goal_s *);
			int travelFlags = va_arg(args, int);
			float lookAhead = STEFX_HolomatchIntAsFloat(va_arg(args, int));
			vec3_t *target = va_arg(args, vec3_t *);
			*result = s_stefxBotlib->ai.BotMovementViewTarget(moveState, goal,
				travelFlags, lookAhead, *target);
		}
		break;
	case HM_BOTLIB_AI_ALLOC_MOVE_STATE:
		*result = s_stefxBotlib->ai.BotAllocMoveState();
		break;
	case HM_BOTLIB_AI_FREE_MOVE_STATE:
		s_stefxBotlib->ai.BotFreeMoveState(va_arg(args, int));
		break;
	case HM_BOTLIB_AI_INIT_MOVE_STATE:
		{
			int state = va_arg(args, int);
			struct bot_initmove_s *init = va_arg(args, struct bot_initmove_s *);
			s_stefxBotlib->ai.BotInitMoveState(state, init);
		}
		break;
	case HM_BOTLIB_AI_CHOOSE_BEST_FIGHT_WEAPON:
		{
			int state = va_arg(args, int);
			int *inventory = va_arg(args, int *);
			*result = s_stefxBotlib->ai.BotChooseBestFightWeapon(state, inventory, qfalse);
		}
		break;
	case HM_BOTLIB_AI_GET_WEAPON_INFO:
		{
			int state = va_arg(args, int);
			int weapon = va_arg(args, int);
			struct weaponinfo_s *info = va_arg(args, struct weaponinfo_s *);
			s_stefxBotlib->ai.BotGetWeaponInfo(state, weapon, info);
		}
		break;
	case HM_BOTLIB_AI_LOAD_WEAPON_WEIGHTS:
		{
			int state = va_arg(args, int);
			char *filename = va_arg(args, char *);
			*result = s_stefxBotlib->ai.BotLoadWeaponWeights(state, filename);
		}
		break;
	case HM_BOTLIB_AI_ALLOC_WEAPON_STATE:
		*result = s_stefxBotlib->ai.BotAllocWeaponState();
		break;
	case HM_BOTLIB_AI_FREE_WEAPON_STATE:
		s_stefxBotlib->ai.BotFreeWeaponState(va_arg(args, int));
		break;
	case HM_BOTLIB_AI_RESET_WEAPON_STATE:
		s_stefxBotlib->ai.BotResetWeaponState(va_arg(args, int));
		break;
	case HM_BOTLIB_AI_GENETIC_PARENTS_AND_CHILD_SELECTION:
		{
			int rankCount = va_arg(args, int);
			float *ranks = va_arg(args, float *);
			int *parent1 = va_arg(args, int *);
			int *parent2 = va_arg(args, int *);
			int *child = va_arg(args, int *);
			*result = s_stefxBotlib->ai.GeneticParentsAndChildSelection(rankCount,
				ranks, parent1, parent2, child);
		}
		break;
	case HM_BOTLIB_AI_INTERBREED_GOAL_FUZZY_LOGIC:
		{
			int parent1 = va_arg(args, int);
			int parent2 = va_arg(args, int);
			int child = va_arg(args, int);
			s_stefxBotlib->ai.BotInterbreedGoalFuzzyLogic(parent1, parent2, child);
		}
		break;
	case HM_BOTLIB_AI_MUTATE_GOAL_FUZZY_LOGIC:
		{
			int state = va_arg(args, int);
			float range = STEFX_HolomatchIntAsFloat(va_arg(args, int));
			s_stefxBotlib->ai.BotMutateGoalFuzzyLogic(state, range);
		}
		break;
	case HM_BOTLIB_AI_GET_NEXT_CAMP_SPOT_GOAL:
		{
			int index = va_arg(args, int);
			struct bot_goal_s *goal = va_arg(args, struct bot_goal_s *);
			*result = s_stefxBotlib->ai.BotGetNextCampSpotGoal(index, goal);
		}
		break;
	case HM_BOTLIB_AI_GET_MAP_LOCATION_GOAL:
		{
			char *name = va_arg(args, char *);
			struct bot_goal_s *goal = va_arg(args, struct bot_goal_s *);
			*result = s_stefxBotlib->ai.BotGetMapLocationGoal(name, goal);
		}
		break;
	case HM_BOTLIB_AI_NUM_INITIAL_CHATS:
		{
			int state = va_arg(args, int);
			char *type = va_arg(args, char *);
			*result = s_stefxBotlib->ai.BotNumInitialChats(state, type);
		}
		break;
	case HM_BOTLIB_AI_GET_CHAT_MESSAGE:
		{
			int state = va_arg(args, int);
			char *message = va_arg(args, char *);
			int size = va_arg(args, int);
			s_stefxBotlib->ai.BotGetChatMessage(state, message, size);
		}
		break;
	case HM_BOTLIB_AI_REMOVE_FROM_AVOID_GOALS:
		{
			int state = va_arg(args, int);
			int goalNumber = va_arg(args, int);
			s_stefxBotlib->ai.BotRemoveFromAvoidGoals(state, goalNumber);
		}
		break;
	case HM_BOTLIB_AI_PREDICT_VISIBLE_POSITION:
		{
			vec3_t *origin = va_arg(args, vec3_t *);
			int areaNum = va_arg(args, int);
			struct bot_goal_s *goal = va_arg(args, struct bot_goal_s *);
			int travelFlags = va_arg(args, int);
			vec3_t *target = va_arg(args, vec3_t *);
			*result = s_stefxBotlib->ai.BotPredictVisiblePosition(*origin, areaNum,
				goal, travelFlags, *target);
		}
		break;
	default:
		XBLog_WriteCriticalf("STEFX_HM_BOTLIB: unhandled syscall=%d", command);
		break;
	}
	return qtrue;
}
