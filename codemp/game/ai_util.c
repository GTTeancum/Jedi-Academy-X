#include "g_local.h"
#include "q_shared.h"
#include "botlib.h"
#include "ai_main.h"

#ifdef BOT_ZMALLOC
#define MAX_BALLOC 8192

void *BAllocList[MAX_BALLOC];
#endif

//char gBotChatBuffer[MAX_CLIENTS][MAX_CHAT_BUFFER_SIZE];

void *B_TempAlloc(int size)
{
	return BG_TempAlloc(size);
}

void B_TempFree(int size)
{
	BG_TempFree(size);
}


void *B_Alloc(int size)
{
#ifdef BOT_ZMALLOC
	void *ptr = NULL;
	int i = 0;

#ifdef BOTMEMTRACK
	int free = 0;
	int used = 0;

	while (i < MAX_BALLOC)
	{
		if (!BAllocList[i])
		{
			free++;
		}
		else
		{
			used++;
		}

		i++;
	}

	G_Printf("Allocations used: %i\nFree allocation slots: %i\n", used, free);

	i = 0;
#endif

	ptr = trap_BotGetMemoryGame(size);

	while (i < MAX_BALLOC)
	{
		if (!BAllocList[i])
		{
			BAllocList[i] = ptr;
			break;
		}
		i++;
	}

	if (i == MAX_BALLOC)
	{
		//If this happens we'll have to rely on this chunk being freed manually with B_Free, which it hopefully will be
#ifdef DEBUG
		G_Printf("WARNING: MAXIMUM B_ALLOC ALLOCATIONS EXCEEDED\n");
#endif
	}

	return ptr;
#else

	return BG_Alloc(size);

#endif
}

void B_Free(void *ptr)
{
#ifdef BOT_ZMALLOC
	int i = 0;

#ifdef BOTMEMTRACK
	int free = 0;
	int used = 0;

	while (i < MAX_BALLOC)
	{
		if (!BAllocList[i])
		{
			free++;
		}
		else
		{
			used++;
		}

		i++;
	}

	G_Printf("Allocations used: %i\nFree allocation slots: %i\n", used, free);

	i = 0;
#endif

	while (i < MAX_BALLOC)
	{
		if (BAllocList[i] == ptr)
		{
			BAllocList[i] = NULL;
			break;
		}

		i++;
	}

	if (i == MAX_BALLOC)
	{
		//Likely because the limit was exceeded and we're now freeing the chunk manually as we hoped would happen
#ifdef DEBUG
		G_Printf("WARNING: Freeing allocation which is not in the allocation structure\n");
#endif
	}

	trap_BotFreeMemoryGame(ptr);
#endif
}

void B_InitAlloc(void)
{
#ifdef BOT_ZMALLOC
	memset(BAllocList, 0, sizeof(BAllocList));
#endif

	memset(gWPArray, 0, sizeof(gWPArray));
	gWPNum = 0;
#if defined(STEFX_ELITE_FORCE_MP)
	G_Printf( "STEFX_HM: bot waypoint state reset\n" );
#endif
}

void B_CleanupAlloc(void)
{
	int i;

	for (i = 0; i < MAX_WPARRAY_SIZE; i++) {
		if(gWPArray[i]) {
			Z_Free(gWPArray[i]);
			gWPArray[i] = NULL;
		}
	}
	gWPNum = 0;
#ifdef BOT_ZMALLOC
	i = 0;
	while (i < MAX_BALLOC)
	{
		if (BAllocList[i])
		{
			trap_BotFreeMemoryGame(BAllocList[i]);
			BAllocList[i] = NULL;
		}

		i++;
	}
#endif
}

int GetValueGroup(char *buf, char *group, char *outbuf)
{
	char *place, *placesecond;
	int iplace;
	int failure;
	int i;
	int startpoint, startletter;
	int subg = 0;

	i = 0;

	iplace = 0;

	place = strstr(buf, group);

	if (!place)
	{
		return 0;
	}

	startpoint = place - buf + strlen(group) + 1;
	startletter = (place - buf) - 1;

	failure = 0;

	while (buf[startpoint+1] != '{' || buf[startletter] != '\n')
	{
		placesecond = strstr(place+1, group);

		if (placesecond)
		{
			startpoint += (placesecond - place);
			startletter += (placesecond - place);
			place = placesecond;
		}
		else
		{
			failure = 1;
			break;
		}
	}

	if (failure)
	{
		return 0;
	}

	//we have found the proper group name if we made it here, so find the opening brace and read into the outbuf
	//until hitting the end brace

	while (buf[startpoint] != '{')
	{
		startpoint++;
	}

	startpoint++;

	while (buf[startpoint] != '}' || subg)
	{
		if (buf[startpoint] == '{')
		{
			subg++;
		}
		else if (buf[startpoint] == '}')
		{
			subg--;
		}
		outbuf[i] = buf[startpoint];
		i++;
		startpoint++;
	}
	outbuf[i] = '\0';

	return 1;
}

int GetPairedValue(char *buf, char *key, char *outbuf)
{
	char *place, *placesecond;
	int startpoint, startletter;
	int i, found;

	if (!buf || !key || !outbuf)
	{
		return 0;
	}

	i = 0;

	while (buf[i] && buf[i] != '\0')
	{
		if (buf[i] == '/')
		{
			if (buf[i+1] && buf[i+1] != '\0' && buf[i+1] == '/')
			{
				while (buf[i] != '\n')
				{
					buf[i] = '/';
					i++;
				}
			}
		}
		i++;
	}

	place = strstr(buf, key);

	if (!place)
	{
		return 0;
	}
	//tab == 9
	startpoint = place - buf + strlen(key);
	startletter = (place - buf) - 1;

	found = 0;

	while (!found)
	{
		if (startletter == 0 || !buf[startletter] || buf[startletter] == '\0' || buf[startletter] == 9 || buf[startletter] == ' ' || buf[startletter] == '\n')
		{
			if (buf[startpoint] == '\0' || buf[startpoint] == 9 || buf[startpoint] == ' ' || buf[startpoint] == '\n')
			{
				found = 1;
				break;
			}
		}

		placesecond = strstr(place+1, key);

		if (placesecond)
		{
			startpoint += placesecond - place;
			startletter += placesecond - place;
			place = placesecond;
		}
		else
		{
			place = NULL;
			break;
		}

	}

	if (!found || !place || !buf[startpoint] || buf[startpoint] == '\0')
	{
		return 0;
	}

	while (buf[startpoint] == ' ' || buf[startpoint] == 9 || buf[startpoint] == '\n')
	{
		startpoint++;
	}

	i = 0;

	while (buf[startpoint] && buf[startpoint] != '\0' && buf[startpoint] != '\n')
	{
		outbuf[i] = buf[startpoint];
		i++;
		startpoint++;
	}

	outbuf[i] = '\0';

	return 1;
}

/*
int BotDoChat(bot_state_t *bs, char *section, int always)
{
	char *chatgroup;
	int rVal;
	int inc_1;
	int inc_2;
	int inc_n;
	int lines;
	int checkedline;
	int getthisline;
	gentity_t *cobject;

	if (!bs->canChat)
	{
		return 0;
	}

	if (bs->doChat)
	{ //already have a chat scheduled
		return 0;
	}

	if (trap_Cvar_VariableIntegerValue("se_language"))
	{ //no chatting unless English.
		return 0;
	}

	if (Q_irand(1, 10) > bs->chatFrequency && !always)
	{
		return 0;
	}

	bs->chatTeam = 0;

	chatgroup = (char *)B_TempAlloc(MAX_CHAT_BUFFER_SIZE);

	rVal = GetValueGroup(gBotChatBuffer[bs->client], section, chatgroup);

	if (!rVal) //the bot has no group defined for the specified chat event
	{
		B_TempFree(MAX_CHAT_BUFFER_SIZE); //chatgroup
		return 0;
	}

	inc_1 = 0;
	inc_2 = 2;

	while (chatgroup[inc_2] && chatgroup[inc_2] != '\0')
	{
		if (chatgroup[inc_2] != 13 && chatgroup[inc_2] != 9)
		{
			chatgroup[inc_1] = chatgroup[inc_2];
			inc_1++;
		}
		inc_2++;
	}
	chatgroup[inc_1] = '\0';

	inc_1 = 0;

	lines = 0;

	while (chatgroup[inc_1] && chatgroup[inc_1] != '\0')
	{
		if (chatgroup[inc_1] == '\n')
		{
			lines++;
		}
		inc_1++;
	}

	if (!lines)
	{
		B_TempFree(MAX_CHAT_BUFFER_SIZE); //chatgroup
		return 0;
	}

	getthisline = Q_irand(0, (lines+1));

	if (getthisline < 1)
	{
		getthisline = 1;
	}
	if (getthisline > lines)
	{
		getthisline = lines;
	}

	checkedline = 1;

	inc_1 = 0;

	while (checkedline != getthisline)
	{
		if (chatgroup[inc_1] && chatgroup[inc_1] != '\0')
		{
			if (chatgroup[inc_1] == '\n')
			{
				inc_1++;
				checkedline++;
			}
		}

		if (checkedline == getthisline)
		{
			break;
		}

		inc_1++;
	}

	//we're at the starting position of the desired line here
	inc_2 = 0;

	while (chatgroup[inc_1] != '\n')
	{
		chatgroup[inc_2] = chatgroup[inc_1];
		inc_2++;
		inc_1++;
	}
	chatgroup[inc_2] = '\0';

	//trap_EA_Say(bs->client, chatgroup);
	inc_1 = 0;
	inc_2 = 0;

	if (strlen(chatgroup) > MAX_CHAT_LINE_SIZE)
	{
		B_TempFree(MAX_CHAT_BUFFER_SIZE); //chatgroup
		return 0;
	}

	while (chatgroup[inc_1])
	{
		if (chatgroup[inc_1] == '%' && chatgroup[inc_1+1] != '%')
		{
			inc_1++;

			if (chatgroup[inc_1] == 's' && bs->chatObject)
			{
				cobject = bs->chatObject;
			}
			else if (chatgroup[inc_1] == 'a' && bs->chatAltObject)
			{
				cobject = bs->chatAltObject;
			}
			else
			{
				cobject = NULL;
			}

			if (cobject && cobject->client)
			{
				inc_n = 0;

				while (cobject->client->pers.netname[inc_n])
				{
					bs->currentChat[inc_2] = cobject->client->pers.netname[inc_n];
					inc_2++;
					inc_n++;
				}
				inc_2--; //to make up for the auto-increment below
			}
		}
		else
		{
			bs->currentChat[inc_2] = chatgroup[inc_1];
		}
		inc_2++;
		inc_1++;
	}
	bs->currentChat[inc_2] = '\0';

	if (strcmp(section, "GeneralGreetings") == 0)
	{
		bs->doChat = 2;
	}
	else
	{
		bs->doChat = 1;
	}
	bs->chatTime_stored = (strlen(bs->currentChat)*45)+Q_irand(1300, 1500);
	bs->chatTime = level.time + bs->chatTime_stored;

	B_TempFree(MAX_CHAT_BUFFER_SIZE); //chatgroup

	return 1;
}
*/

void ParseEmotionalAttachments(bot_state_t *bs, char *buf)
{
	int i = 0;
	int i_c = 0;
	char tbuf[16];

	while (buf[i] && buf[i] != '}')
	{
		while (buf[i] == ' ' || buf[i] == '{' || buf[i] == 9 || buf[i] == 13 || buf[i] == '\n')
		{
			i++;
		}

		if (buf[i] && buf[i] != '}')
		{
			i_c = 0;
			while (buf[i] != '{' && buf[i] != 9 && buf[i] != 13 && buf[i] != '\n')
			{
				bs->loved[bs->lovednum].name[i_c] = buf[i];
				i_c++;
				i++;
			}
			bs->loved[bs->lovednum].name[i_c] = '\0';

			while (buf[i] == ' ' || buf[i] == '{' || buf[i] == 9 || buf[i] == 13 || buf[i] == '\n')
			{
				i++;
			}

			i_c = 0;

			while (buf[i] != '{' && buf[i] != 9 && buf[i] != 13 && buf[i] != '\n')
			{
				tbuf[i_c] = buf[i];
				i_c++;
				i++;
			}
			tbuf[i_c] = '\0';

			bs->loved[bs->lovednum].level = atoi(tbuf);

			bs->lovednum++;
		}
		else
		{
			break;
		}

		if (bs->lovednum >= MAX_LOVED_ONES)
		{
			return;
		}

		i++;
	}
}

/*
int ReadChatGroups(bot_state_t *bs, char *buf)
{
	char *cgroupbegin;
	int cgbplace;
	int i;

	cgroupbegin = strstr(buf, "BEGIN_CHAT_GROUPS");

	if (!cgroupbegin)
	{
		return 0;
	}

	if (strlen(cgroupbegin) >= MAX_CHAT_BUFFER_SIZE)
	{
		G_Printf(S_COLOR_RED "Error: Personality chat section exceeds max size\n");
		return 0;
	}

	cgbplace = cgroupbegin - buf+1;

	while (buf[cgbplace] != '\n')
	{
		cgbplace++;
	}

	i = 0;

	while (buf[cgbplace] && buf[cgbplace] != '\0')
	{
		gBotChatBuffer[bs->client][i] = buf[cgbplace];
		i++;
		cgbplace++;
	}

	gBotChatBuffer[bs->client][i] = '\0';

	return 1;
}
*/

#if defined(STEFX_ELITE_FORCE_MP)
static qboolean STEFX_HM_IsWhiteSpace( int c )
{
	return ( c == ' ' || c == '\t' || c == '\r' || c == '\n' );
}

static float STEFX_HM_ClampFloat( float value, float minValue, float maxValue )
{
	if ( value < minValue )
	{
		return minValue;
	}

	if ( value > maxValue )
	{
		return maxValue;
	}

	return value;
}

static void STEFX_HM_CopyUnquotedValue( const char *value, char *out, int outSize )
{
	const char *scan;
	int i;

	if ( !out || outSize < 1 )
	{
		return;
	}

	out[0] = '\0';

	if ( !value )
	{
		return;
	}

	scan = value;
	while ( *scan && STEFX_HM_IsWhiteSpace( *scan ) )
	{
		scan++;
	}

	if ( *scan == '"' )
	{
		scan++;
	}

	i = 0;
	while ( *scan && *scan != '"' && *scan != '\r' && *scan != '\n' && i < outSize - 1 )
	{
		out[i] = *scan;
		i++;
		scan++;
	}

	out[i] = '\0';
}

static qboolean STEFX_HM_FindSkillGroup( char *buf, int skill, char *outbuf )
{
	char *place;
	char *scan;
	int skillNum;
	int depth;
	int i;

	if ( !buf || !outbuf )
	{
		return qfalse;
	}

	place = buf;
	while ( ( place = strstr( place, "skill" ) ) != NULL )
	{
		if ( place != buf && !STEFX_HM_IsWhiteSpace( *( place - 1 ) ) )
		{
			place += 5;
			continue;
		}

		scan = place + 5;
		while ( *scan && STEFX_HM_IsWhiteSpace( *scan ) )
		{
			scan++;
		}

		if ( *scan < '0' || *scan > '9' )
		{
			place += 5;
			continue;
		}

		skillNum = atoi( scan );
		while ( *scan >= '0' && *scan <= '9' )
		{
			scan++;
		}

		if ( !STEFX_HM_IsWhiteSpace( *scan ) && *scan != '{' )
		{
			place += 5;
			continue;
		}

		if ( skillNum != skill )
		{
			place += 5;
			continue;
		}

		while ( *scan && STEFX_HM_IsWhiteSpace( *scan ) )
		{
			scan++;
		}

		if ( *scan != '{' )
		{
			place += 5;
			continue;
		}

		scan++;
		depth = 0;
		i = 0;

		while ( *scan )
		{
			if ( *scan == '{' )
			{
				depth++;
			}
			else if ( *scan == '}' )
			{
				if ( !depth )
				{
					break;
				}
				depth--;
			}

			outbuf[i] = *scan;
			i++;
			scan++;
		}

		outbuf[i] = '\0';
		return qtrue;
	}

	return qfalse;
}

static qboolean STEFX_HM_SelectSkillGroup( char *buf, float requestedSkillFloat, char *group, int *selectedSkill )
{
	int requestedSkill;
	int skill;

	requestedSkill = (int)( requestedSkillFloat + 0.5f );
	if ( requestedSkill < 1 )
	{
		requestedSkill = 1;
	}
	else if ( requestedSkill > 5 )
	{
		requestedSkill = 5;
	}

	if ( STEFX_HM_FindSkillGroup( buf, requestedSkill, group ) )
	{
		*selectedSkill = requestedSkill;
		return qtrue;
	}

	for ( skill = requestedSkill - 1; skill >= 1; skill-- )
	{
		if ( STEFX_HM_FindSkillGroup( buf, skill, group ) )
		{
			*selectedSkill = skill;
			return qtrue;
		}
	}

	for ( skill = requestedSkill + 1; skill <= 5; skill++ )
	{
		if ( STEFX_HM_FindSkillGroup( buf, skill, group ) )
		{
			*selectedSkill = skill;
			return qtrue;
		}
	}

	return qfalse;
}

static float STEFX_HM_ReadCharacteristicFloat( char *group, char *key, char *scratch, float fallback )
{
	if ( GetPairedValue( group, key, scratch ) )
	{
		return (float)atof( scratch );
	}

	return fallback;
}

static qboolean STEFX_HM_ReadWeight( bot_state_t *bs, char *buf, char *scratch, char *key, int weapon, int *loggedWeight )
{
	if ( GetPairedValue( buf, key, scratch ) )
	{
		*loggedWeight = atoi( scratch );
		bs->botWeaponWeights[weapon] = *loggedWeight;
		return qtrue;
	}

	return qfalse;
}

static qboolean STEFX_HM_LoadEFWeaponWeights( bot_state_t *bs, const char *weightsValue )
{
	fileHandle_t f;
	char weightsName[MAX_FILEPATH];
	char lowerName[MAX_FILEPATH];
	char weightsPath[MAX_FILEPATH];
	char buf[4096];
	char scratch[1024];
	int len;
	int found;
	int phaser;
	int compression;
	int imod;
	int scavenger;
	int tetrion;
	int dreadnought;

	STEFX_HM_CopyUnquotedValue( weightsValue, weightsName, sizeof( weightsName ) );
	if ( !weightsName[0] )
	{
		G_Printf( "STEFX_HM: bot EF weapon weights missing name; using hm defaults\n" );
		return qfalse;
	}

	Com_sprintf( weightsPath, sizeof( weightsPath ), "botfiles/%s", weightsName );
	len = trap_FS_FOpenFile( weightsPath, &f, FS_READ );

	if ( !f )
	{
		Q_strncpyz( lowerName, weightsName, sizeof( lowerName ) );
		Q_strlwr( lowerName );
		Com_sprintf( weightsPath, sizeof( weightsPath ), "botfiles/%s", lowerName );
		len = trap_FS_FOpenFile( weightsPath, &f, FS_READ );
	}

	if ( !f )
	{
		G_Printf( "STEFX_HM: bot EF weapon weights file '%s' not found; using hm defaults\n",
			weightsName );
		return qfalse;
	}

	if ( len <= 0 )
	{
		trap_FS_FCloseFile( f );
		G_Printf( "STEFX_HM: bot EF weapon weights file '%s' is empty; using hm defaults\n",
			weightsName );
		return qfalse;
	}

	if ( len >= (int)sizeof( buf ) )
	{
		G_Printf( "STEFX_HM: bot EF weapon weights file='%s' truncated from %d bytes for parser\n",
			weightsPath,
			len );
		len = (int)sizeof( buf ) - 1;
	}

	trap_FS_Read( buf, len, f );
	buf[len] = '\0';

	found = 0;
	phaser = -1;
	compression = -1;
	imod = -1;
	scavenger = -1;
	tetrion = -1;
	dreadnought = -1;

	if ( STEFX_HM_ReadWeight( bs, buf, scratch, "W_PHASER", WP_BRYAR_PISTOL, &phaser ) )
	{
		found++;
	}
	if ( STEFX_HM_ReadWeight( bs, buf, scratch, "W_COMPRESSION", WP_BLASTER, &compression ) )
	{
		found++;
	}
	if ( STEFX_HM_ReadWeight( bs, buf, scratch, "W_IMOD", WP_DEMP2, &imod ) )
	{
		found++;
	}
	if ( STEFX_HM_ReadWeight( bs, buf, scratch, "W_SCAVENGER", WP_BOWCASTER, &scavenger ) )
	{
		found++;
	}
	if ( STEFX_HM_ReadWeight( bs, buf, scratch, "W_TETRION", WP_DISRUPTOR, &tetrion ) )
	{
		found++;
	}
	if ( STEFX_HM_ReadWeight( bs, buf, scratch, "W_DREADNOUGHT", WP_ROCKET_LAUNCHER, &dreadnought ) )
	{
		found++;
	}

	G_Printf( "STEFX_HM: bot EF weapon weights file='%s' phaser=%d compression=%d imod=%d scavenger=%d tetrion=%d dreadnought=%d mapped=%d\n",
		weightsPath,
		phaser,
		compression,
		imod,
		scavenger,
		tetrion,
		dreadnought,
		found );

	trap_FS_FCloseFile( f );

	return ( found > 0 ) ? qtrue : qfalse;
}

static qboolean STEFX_HM_LoadEFPersonality( bot_state_t *bs, char *buf, char *group, char *scratch )
{
	char weightsValue[MAX_FILEPATH];
	int selectedSkill;
	float attackSkill;
	float aimSkill;
	float aimAccuracy;
	float viewFactor;
	float viewMaxChange;
	float reactionTime;
	float camper;
	float accuracyBridge;
	qboolean loadedWeights;

	if ( !STEFX_HM_SelectSkillGroup( buf, bs->settings.skill, group, &selectedSkill ) )
	{
		G_Printf( "STEFX_HM: bot EF personality '%s' has no usable skill block for skill %.1f\n",
			bs->settings.personalityfile,
			bs->settings.skill );
		return qfalse;
	}

	attackSkill = STEFX_HM_ReadCharacteristicFloat( group, "CHARACTERISTIC_ATTACK_SKILL", scratch, 0.7f );
	aimSkill = STEFX_HM_ReadCharacteristicFloat( group, "CHARACTERISTIC_AIM_SKILL", scratch, attackSkill );
	aimAccuracy = STEFX_HM_ReadCharacteristicFloat( group, "CHARACTERISTIC_AIM_ACCURACY", scratch, aimSkill );
	viewFactor = STEFX_HM_ReadCharacteristicFloat( group, "CHARACTERISTIC_VIEW_FACTOR", scratch, 0.6f );
	viewMaxChange = STEFX_HM_ReadCharacteristicFloat( group, "CHARACTERISTIC_VIEW_MAXCHANGE", scratch, 300.0f );
	reactionTime = STEFX_HM_ReadCharacteristicFloat( group, "CHARACTERISTIC_REACTIONTIME", scratch, 1.0f );
	camper = STEFX_HM_ReadCharacteristicFloat( group, "CHARACTERISTIC_CAMPER", scratch, 0.0f );

	reactionTime = STEFX_HM_ClampFloat( reactionTime, 0.05f, 20.0f );
	aimAccuracy = STEFX_HM_ClampFloat( aimAccuracy, 0.0f, 1.0f );
	viewFactor = STEFX_HM_ClampFloat( viewFactor, 0.0f, 1.0f );

	bs->skills.reflex = (int)( reactionTime * 100.0f );
	if ( bs->skills.reflex < 10 )
	{
		bs->skills.reflex = 10;
	}

	accuracyBridge = ( 1.0f - aimAccuracy ) * 40.0f;
	bs->skills.accuracy = STEFX_HM_ClampFloat( accuracyBridge, 0.0f, 30.0f );
	bs->skills.turnspeed = 0.006f + ( viewFactor * 0.010f );
	bs->skills.turnspeed_combat = 0.020f + ( viewFactor * 0.050f );
	bs->skills.maxturn = viewMaxChange;
	bs->skills.perfectaim = 0;

	bs->loved_death_thresh = 3;
	bs->isCamper = ( camper >= 0.5f ) ? 1 : 0;
	bs->saberSpecialist = 0;
	Com_sprintf( bs->forceinfo, sizeof( bs->forceinfo ), "%s\0", DEFAULT_FORCEPOWERS );
	bs->lovednum = 0;

	bs->botWeaponWeights[WP_NONE] = 0;
	bs->botWeaponWeights[WP_STUN_BATON] = 0;
	bs->botWeaponWeights[WP_SABER] = 0;
	bs->botWeaponWeights[WP_BRYAR_PISTOL] = 30;
	bs->botWeaponWeights[WP_BLASTER] = 100;
	bs->botWeaponWeights[WP_DISRUPTOR] = 100;
	bs->botWeaponWeights[WP_BOWCASTER] = 100;
	bs->botWeaponWeights[WP_REPEATER] = 0;
	bs->botWeaponWeights[WP_DEMP2] = 100;
	bs->botWeaponWeights[WP_FLECHETTE] = 0;
	bs->botWeaponWeights[WP_ROCKET_LAUNCHER] = 100;
	bs->botWeaponWeights[WP_THERMAL] = 0;
	bs->botWeaponWeights[WP_TRIP_MINE] = 0;
	bs->botWeaponWeights[WP_DET_PACK] = 0;
	bs->botWeaponWeights[WP_MELEE] = 0;

	weightsValue[0] = '\0';
	loadedWeights = qfalse;
	if ( GetPairedValue( group, "CHARACTERISTIC_WEAPONWEIGHTS", scratch ) )
	{
		STEFX_HM_CopyUnquotedValue( scratch, weightsValue, sizeof( weightsValue ) );
		loadedWeights = STEFX_HM_LoadEFWeaponWeights( bs, scratch );
	}

	G_Printf( "STEFX_HM: bot EF personality file='%s' requestedSkill=%.1f selectedSkill=%d attack=%.2f aimSkill=%.2f aimAccuracy=%.2f view=%.2f maxturn=%.0f reaction=%.2f camper=%.2f weights='%s' loadedWeights=%d\n",
		bs->settings.personalityfile,
		bs->settings.skill,
		selectedSkill,
		attackSkill,
		aimSkill,
		aimAccuracy,
		viewFactor,
		viewMaxChange,
		reactionTime,
		camper,
		weightsValue,
		loadedWeights );

	return qtrue;
}

static void STEFX_HM_BotUtilizeEFPersonality( bot_state_t *bs )
{
	fileHandle_t f;
	int len;
	char *buf;
	char *group;
	char *scratch;

	len = trap_FS_FOpenFile( bs->settings.personalityfile, &f, FS_READ );
	if ( !f )
	{
		G_Printf( S_COLOR_RED "Error: Specified personality not found\n" );
		G_Printf( "STEFX_HM: bot personality missing file='%s'; using base bot defaults\n",
			bs->settings.personalityfile );
		return;
	}

	if ( len <= 0 )
	{
		trap_FS_FCloseFile( f );
		G_Printf( "STEFX_HM: bot EF personality '%s' empty; using base bot defaults\n",
			bs->settings.personalityfile );
		return;
	}

	buf = (char *)B_TempAlloc( len + 1 );
	if ( !buf )
	{
		trap_FS_FCloseFile( f );
		G_Printf( "STEFX_HM: bot EF personality '%s' buffer allocation failed; using base bot defaults\n",
			bs->settings.personalityfile );
		return;
	}

	group = (char *)B_TempAlloc( len + 1 );
	if ( !group )
	{
		B_TempFree( len + 1 );
		trap_FS_FCloseFile( f );
		G_Printf( "STEFX_HM: bot EF personality '%s' group allocation failed; using base bot defaults\n",
			bs->settings.personalityfile );
		return;
	}

	scratch = (char *)B_TempAlloc( 1024 );
	if ( !scratch )
	{
		B_TempFree( len + 1 );
		B_TempFree( len + 1 );
		trap_FS_FCloseFile( f );
		G_Printf( "STEFX_HM: bot EF personality '%s' scratch allocation failed; using base bot defaults\n",
			bs->settings.personalityfile );
		return;
	}

	trap_FS_Read( buf, len, f );
	buf[len] = 0;

	if ( !STEFX_HM_LoadEFPersonality( bs, buf, group, scratch ) )
	{
		G_Printf( "STEFX_HM: bot EF personality '%s' could not be parsed; using base bot defaults\n",
			bs->settings.personalityfile );
	}

	G_Printf( "STEFX_HM: bot EF personality closing file handle client=%d handle=%d\n",
		bs->client,
		f );
	trap_FS_FCloseFile( f );
	B_TempFree( 1024 );
	B_TempFree( len + 1 );
	B_TempFree( len + 1 );
	G_Printf( "STEFX_HM: bot EF personality cleanup done client=%d\n",
		bs->client );
}
#endif

void BotUtilizePersonality(bot_state_t *bs)
{
	fileHandle_t f;
	int len, rlen;
	int failed;
	int i;
	//char buf[131072];
	char *buf, *readbuf, *group;

#if defined(STEFX_ELITE_FORCE_MP)
	if ( strstr( bs->settings.personalityfile, "botfiles/bots/" ) )
	{
		STEFX_HM_BotUtilizeEFPersonality( bs );
		return;
	}
#endif

	len = trap_FS_FOpenFile(bs->settings.personalityfile, &f, FS_READ);

	failed = 0;

	if (!f)
	{
		G_Printf(S_COLOR_RED "Error: Specified personality not found\n");
#if defined(STEFX_ELITE_FORCE_MP)
		G_Printf( "STEFX_HM: bot personality missing file='%s'; using base bot defaults\n",
			bs->settings.personalityfile );
#endif
		return;
	}

	buf = (char *)B_TempAlloc(len + 1);
	trap_FS_Read(buf, len, f);

	buf[len] = 0;

	readbuf = (char *)B_TempAlloc(1024);
	group = (char *)B_TempAlloc(len + 1);

	if (!GetValueGroup(buf, "GeneralBotInfo", group))
	{
#if defined(STEFX_ELITE_FORCE_MP)
		if ( strstr( bs->settings.personalityfile, "botfiles/bots/" ) )
		{
			if ( STEFX_HM_LoadEFPersonality( bs, buf, group, readbuf ) )
			{
				B_TempFree(len + 1); //group
				B_TempFree(1024); //readbuf
				B_TempFree(len + 1); //buf
				trap_FS_FCloseFile(f);
				return;
			}

			G_Printf( "STEFX_HM: bot EF personality '%s' could not be parsed; using base bot defaults\n",
				bs->settings.personalityfile );
		}
		else
#endif
		G_Printf(S_COLOR_RED "Personality file contains no GeneralBotInfo group\n");
		failed = 1; //set failed so we know to set everything to default values
	}

	if (!failed && GetPairedValue(group, "reflex", readbuf))
	{
		bs->skills.reflex = atoi(readbuf);
	}
	else
	{
		bs->skills.reflex = 100; //default
	}

	if (!failed && GetPairedValue(group, "accuracy", readbuf))
	{
		bs->skills.accuracy = atof(readbuf);
	}
	else
	{
		bs->skills.accuracy = 10; //default
	}

	if (!failed && GetPairedValue(group, "turnspeed", readbuf))
	{
		bs->skills.turnspeed = atof(readbuf);
	}
	else
	{
		bs->skills.turnspeed = 0.01f; //default
	}

	if (!failed && GetPairedValue(group, "turnspeed_combat", readbuf))
	{
		bs->skills.turnspeed_combat = atof(readbuf);
	}
	else
	{
		bs->skills.turnspeed_combat = 0.05f; //default
	}

	if (!failed && GetPairedValue(group, "maxturn", readbuf))
	{
		bs->skills.maxturn = atof(readbuf);
	}
	else
	{
		bs->skills.maxturn = 360; //default
	}

	if (!failed && GetPairedValue(group, "perfectaim", readbuf))
	{
		bs->skills.perfectaim = atoi(readbuf);
	}
	else
	{
		bs->skills.perfectaim = 0; //default
	}
/*
	if (!failed && GetPairedValue(group, "chatability", readbuf))
	{
		bs->canChat = atoi(readbuf);
	}
	else
	{
		bs->canChat = 0; //default
	}

	if (!failed && GetPairedValue(group, "chatfrequency", readbuf))
	{
		bs->chatFrequency = atoi(readbuf);
	}
	else
	{
		bs->chatFrequency = 5; //default
	}
*/
	if (!failed && GetPairedValue(group, "hatelevel", readbuf))
	{
		bs->loved_death_thresh = atoi(readbuf);
	}
	else
	{
		bs->loved_death_thresh = 3; //default
	}

	if (!failed && GetPairedValue(group, "camper", readbuf))
	{
		bs->isCamper = atoi(readbuf);
	}
	else
	{
		bs->isCamper = 0; //default
	}

	if (!failed && GetPairedValue(group, "saberspecialist", readbuf))
	{
		bs->saberSpecialist = atoi(readbuf);
	}
	else
	{
		bs->saberSpecialist = 0; //default
	}

	if (!failed && GetPairedValue(group, "forceinfo", readbuf))
	{
		Com_sprintf(bs->forceinfo, sizeof(bs->forceinfo), "%s\0", readbuf);
	}
	else
	{
		Com_sprintf(bs->forceinfo, sizeof(bs->forceinfo), "%s\0", DEFAULT_FORCEPOWERS);
	}

	i = 0;

/*
	while (i < MAX_CHAT_BUFFER_SIZE)
	{ //clear out the chat buffer for this bot
		gBotChatBuffer[bs->client][i] = '\0';
		i++;
	}

	if (bs->canChat)
	{
		if (!ReadChatGroups(bs, buf))
		{
			bs->canChat = 0;
		}
	}
*/
	if (GetValueGroup(buf, "BotWeaponWeights", group))
	{
		if (GetPairedValue(group, "WP_STUN_BATON", readbuf))
		{
			bs->botWeaponWeights[WP_STUN_BATON] = atoi(readbuf);
			bs->botWeaponWeights[WP_MELEE] = bs->botWeaponWeights[WP_STUN_BATON];
		}

		if (GetPairedValue(group, "WP_SABER", readbuf))
		{
			bs->botWeaponWeights[WP_SABER] = atoi(readbuf);
		}

		if (GetPairedValue(group, "WP_BRYAR_PISTOL", readbuf))
		{
			bs->botWeaponWeights[WP_BRYAR_PISTOL] = atoi(readbuf);
		}

		if (GetPairedValue(group, "WP_BLASTER", readbuf))
		{
			bs->botWeaponWeights[WP_BLASTER] = atoi(readbuf);
		}

		if (GetPairedValue(group, "WP_DISRUPTOR", readbuf))
		{
			bs->botWeaponWeights[WP_DISRUPTOR] = atoi(readbuf);
		}

		if (GetPairedValue(group, "WP_BOWCASTER", readbuf))
		{
			bs->botWeaponWeights[WP_BOWCASTER] = atoi(readbuf);
		}

		if (GetPairedValue(group, "WP_REPEATER", readbuf))
		{
			bs->botWeaponWeights[WP_REPEATER] = atoi(readbuf);
		}

		if (GetPairedValue(group, "WP_DEMP2", readbuf))
		{
			bs->botWeaponWeights[WP_DEMP2] = atoi(readbuf);
		}

		if (GetPairedValue(group, "WP_FLECHETTE", readbuf))
		{
			bs->botWeaponWeights[WP_FLECHETTE] = atoi(readbuf);
		}

		if (GetPairedValue(group, "WP_ROCKET_LAUNCHER", readbuf))
		{
			bs->botWeaponWeights[WP_ROCKET_LAUNCHER] = atoi(readbuf);
		}

		if (GetPairedValue(group, "WP_THERMAL", readbuf))
		{
			bs->botWeaponWeights[WP_THERMAL] = atoi(readbuf);
		}

		if (GetPairedValue(group, "WP_TRIP_MINE", readbuf))
		{
			bs->botWeaponWeights[WP_TRIP_MINE] = atoi(readbuf);
		}

		if (GetPairedValue(group, "WP_DET_PACK", readbuf))
		{
			bs->botWeaponWeights[WP_DET_PACK] = atoi(readbuf);
		}
	}

	bs->lovednum = 0;

	if (GetValueGroup(buf, "EmotionalAttachments", group))
	{
		ParseEmotionalAttachments(bs, group);
	}

	B_TempFree(len + 1); //group
	B_TempFree(1024); //readbuf
	B_TempFree(len + 1); //buf
	trap_FS_FCloseFile(f);
}
