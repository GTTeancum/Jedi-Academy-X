// leave this line at the top for all g_xxxx.cpp files...
#include "g_headers.h"


#include "g_local.h"
#include "g_functions.h"
#ifdef _XBOX
#include "../win32/xb_log.h"
#endif

extern void	InitMover( gentity_t *ent );

extern gentity_t	*G_TestEntityPosition( gentity_t *ent );

#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
typedef struct stefxScriptTargetCache_s
{
	char target[MAX_QPATH];
	qboolean referenced;
} stefxScriptTargetCache_t;

static stefxScriptTargetCache_t s_stefxScriptTargetCache[128];
static int s_stefxScriptTargetCacheCount = 0;
static char s_stefxScriptTargetMap[MAX_QPATH] = "";

static qboolean STEFX_IsScriptTokenBoundary( int c )
{
	if ( c >= 'a' && c <= 'z' )
	{
		return qfalse;
	}
	if ( c >= 'A' && c <= 'Z' )
	{
		return qfalse;
	}
	if ( c >= '0' && c <= '9' )
	{
		return qfalse;
	}
	return ( c == '_' ) ? qfalse : qtrue;
}

static qboolean STEFX_BufferContainsScriptToken( const char *buffer, int len, const char *token )
{
	int tokenLen;
	int i;

	if ( !buffer || len <= 0 || !token || !token[0] )
	{
		return qfalse;
	}

	tokenLen = strlen( token );
	if ( tokenLen <= 0 || tokenLen > len )
	{
		return qfalse;
	}

	for ( i = 0; i <= len - tokenLen; ++i )
	{
		if ( memcmp( buffer + i, token, tokenLen ) )
		{
			continue;
		}
		if ( i > 0 && !STEFX_IsScriptTokenBoundary( (unsigned char)buffer[i - 1] ) )
		{
			continue;
		}
		if ( i + tokenLen < len && !STEFX_IsScriptTokenBoundary( (unsigned char)buffer[i + tokenLen] ) )
		{
			continue;
		}
		return qtrue;
	}

	return qfalse;
}

static qboolean STEFX_ScriptFileReferencesTarget( const char *scriptPath, const char *targetname )
{
	int len;
	char *buffer;
	qboolean referenced;

	len = gi.FS_ReadFile( scriptPath, (void **)&buffer );
	if ( len <= 0 || !buffer )
	{
		return qfalse;
	}

	referenced = STEFX_BufferContainsScriptToken( buffer, len, targetname );
	gi.FS_FreeFile( buffer );
	if ( referenced )
	{
		XBLF("STEFX: func_usable script target referenced target='%s' script='%s'", targetname, scriptPath);
	}
	return referenced;
}

static qboolean STEFX_LevelScriptsReferenceTarget( const char *targetname )
{
	char listbuf[4096];
	char scriptDir[MAX_QPATH];
	char scriptPath[MAX_QPATH];
	char *cursor;
	static const char *directScripts[] =
	{
		"intro.IBI",
		"intro2.IBI",
		"setupworld.IBI"
	};
	int numFiles;
	int i;

	if ( !targetname || !targetname[0] )
	{
		return qfalse;
	}

	if ( Q_stricmp( s_stefxScriptTargetMap, level.mapname ) )
	{
		s_stefxScriptTargetCacheCount = 0;
		Q_strncpyz( s_stefxScriptTargetMap, level.mapname, sizeof(s_stefxScriptTargetMap) );
		XBLF("STEFX: func_usable script target cache reset map='%s'", level.mapname);
	}

	for ( i = 0; i < s_stefxScriptTargetCacheCount; ++i )
	{
		if ( !Q_stricmp( s_stefxScriptTargetCache[i].target, targetname ) )
		{
			return s_stefxScriptTargetCache[i].referenced;
		}
	}

	Com_sprintf( scriptDir, sizeof(scriptDir), "real_scripts/%s", level.mapname );
	for ( i = 0; i < (int)(sizeof(directScripts) / sizeof(directScripts[0])); ++i )
	{
		Com_sprintf( scriptPath, sizeof(scriptPath), "%s/%s", scriptDir, directScripts[i] );
		if ( STEFX_ScriptFileReferencesTarget( scriptPath, targetname ) )
		{
			if ( s_stefxScriptTargetCacheCount < (int)(sizeof(s_stefxScriptTargetCache) / sizeof(s_stefxScriptTargetCache[0])) )
			{
				Q_strncpyz( s_stefxScriptTargetCache[s_stefxScriptTargetCacheCount].target, targetname, sizeof(s_stefxScriptTargetCache[s_stefxScriptTargetCacheCount].target) );
				s_stefxScriptTargetCache[s_stefxScriptTargetCacheCount].referenced = qtrue;
				++s_stefxScriptTargetCacheCount;
			}
			return qtrue;
		}
	}

	numFiles = gi.FS_GetFileList( scriptDir, ".IBI", listbuf, sizeof(listbuf) );
	cursor = listbuf;
	for ( i = 0; i < numFiles; ++i )
	{
		Com_sprintf( scriptPath, sizeof(scriptPath), "%s/%s", scriptDir, cursor );
		if ( STEFX_ScriptFileReferencesTarget( scriptPath, targetname ) )
		{
			if ( s_stefxScriptTargetCacheCount < (int)(sizeof(s_stefxScriptTargetCache) / sizeof(s_stefxScriptTargetCache[0])) )
			{
				Q_strncpyz( s_stefxScriptTargetCache[s_stefxScriptTargetCacheCount].target, targetname, sizeof(s_stefxScriptTargetCache[s_stefxScriptTargetCacheCount].target) );
				s_stefxScriptTargetCache[s_stefxScriptTargetCacheCount].referenced = qtrue;
				++s_stefxScriptTargetCacheCount;
			}
			return qtrue;
		}
		cursor += strlen( cursor ) + 1;
	}

	if ( s_stefxScriptTargetCacheCount < (int)(sizeof(s_stefxScriptTargetCache) / sizeof(s_stefxScriptTargetCache[0])) )
	{
		Q_strncpyz( s_stefxScriptTargetCache[s_stefxScriptTargetCacheCount].target, targetname, sizeof(s_stefxScriptTargetCache[s_stefxScriptTargetCacheCount].target) );
		s_stefxScriptTargetCache[s_stefxScriptTargetCacheCount].referenced = qfalse;
		++s_stefxScriptTargetCacheCount;
	}
	return qfalse;
}

static qboolean STEFX_ShouldBroadcastFuncUsable( const gentity_t *ent )
{
	if ( !ent || !ent->model || ent->model[0] != '*' )
	{
		return qfalse;
	}
	if ( ent->svFlags & SVF_NOCLIENT )
	{
		return qfalse;
	}
	if ( ent->s.eFlags & EF_NODRAW )
	{
		return qfalse;
	}
	return STEFX_LevelScriptsReferenceTarget( ent->targetname );
}

static void STEFX_BroadcastFuncUsable( gentity_t *ent, const char *reason )
{
	static int s_xboxFuncUsableBroadcastBudget = 96;

	if ( !STEFX_ShouldBroadcastFuncUsable( ent ) )
	{
		return;
	}

	ent->svFlags |= ( SVF_BROADCAST | SVF_USE_CURRENT_ORIGIN );
	if ( s_xboxFuncUsableBroadcastBudget > 0 )
	{
		XBLF("STEFX: func_usable broadcast reason=%s ent=%d class='%s' model='%s' target='%s' script='%s' spawnflags=0x%x sv=0x%x eFlags=0x%x solid=0x%x contents=0x%x count=%d origin=(%g,%g,%g)",
			reason ? reason : "<null>",
			ent->s.number,
			ent->classname ? ent->classname : "<null>",
			ent->model ? ent->model : "<null>",
			ent->targetname ? ent->targetname : "<null>",
			ent->script_targetname ? ent->script_targetname : "<null>",
			ent->spawnflags,
			ent->svFlags,
			ent->s.eFlags,
			ent->s.solid,
			ent->contents,
			ent->count,
			ent->currentOrigin[0], ent->currentOrigin[1], ent->currentOrigin[2]);
		--s_xboxFuncUsableBroadcastBudget;
	}
}

static void STEFX_LogFuncUsableState( gentity_t *ent, const char *reason )
{
	if ( !ent || !ent->model || ent->model[0] != '*' )
	{
		return;
	}
	XBLF("STEFX: func_usable %s ent=%d model='%s' target='%s' spawnflags=0x%x sv=0x%x eFlags=0x%x solid=0x%x contents=0x%x count=%d linked=%d origin=(%g,%g,%g)",
		reason ? reason : "<null>",
		ent->s.number,
		ent->model ? ent->model : "<null>",
		ent->targetname ? ent->targetname : "<null>",
		ent->spawnflags,
		ent->svFlags,
		ent->s.eFlags,
		ent->s.solid,
		ent->contents,
		ent->count,
		ent->linked ? 1 : 0,
		ent->currentOrigin[0], ent->currentOrigin[1], ent->currentOrigin[2]);
}
#endif

void func_wait_return_solid( gentity_t *self )
{
	//once a frame, see if it's clear.
	self->clipmask = CONTENTS_BODY;//|CONTENTS_MONSTERCLIP|CONTENTS_BOTCLIP;
	if ( !(self->spawnflags&16) || G_TestEntityPosition( self ) == NULL )
	{
		gi.SetBrushModel( self, self->model );
		VectorCopy( self->currentOrigin, self->pos1 );
		InitMover( self );
		/*
		VectorCopy( self->s.origin, self->s.pos.trBase );
		VectorCopy( self->s.origin, self->currentOrigin );
		*/
		//if we moved, we want the *current* origin, not our start origin!
		VectorCopy( self->currentOrigin, self->s.pos.trBase );
		self->svFlags &= ~SVF_NOCLIENT;
		self->s.eFlags &= ~EF_NODRAW;
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
		STEFX_BroadcastFuncUsable( self, "return_solid" );
#endif
		gi.linkentity( self );
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
		STEFX_LogFuncUsableState( self, "return_solid_linked" );
#endif
		self->e_UseFunc = useF_func_usable_use;
		self->clipmask = 0;
		if ( self->target2 && self->target2[0] )
		{
			G_UseTargets2( self, self->activator, self->target2 );
		}
		if ( self->s.eFlags & EF_ANIM_ONCE )
		{//Start our anim
			self->s.frame = 0;
		}
		//NOTE: be sure to reset the brushmodel before doing this or else CONTENTS_OPAQUE may not be on when you call this
		if ( !(self->spawnflags&1) )
		{//START_OFF doesn't effect area portals
			gi.AdjustAreaPortalState( self, qfalse );
		}
	}
	else
	{
		self->clipmask = 0;
		self->e_ThinkFunc = thinkF_func_wait_return_solid;
		self->nextthink = level.time + FRAMETIME;
	}
}

void func_usable_think( gentity_t *self )
{
	if ( self->spawnflags & 8 )
	{
		self->svFlags |= SVF_PLAYER_USABLE;	//Replace the usable flag
		self->e_UseFunc = useF_func_usable_use;
		self->e_ThinkFunc = thinkF_NULL;
	}
}

qboolean G_EntIsRemovableUsable( int entNum )
{
	gentity_t *ent = &g_entities[entNum];
	if ( ent->classname && !Q_stricmp( "func_usable", ent->classname ) )
	{
		if ( !(ent->s.eFlags&EF_SHADER_ANIM) && !(ent->spawnflags&8) && ent->targetname )
		{//not just a shader-animator and not ALWAYS_ON, so it must be removable somehow
			return qtrue;
		}
	}
	return qfalse;
}

void func_usable_use( gentity_t *self, gentity_t *other, gentity_t *activator )
{//Toggle on and off
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
	STEFX_LogFuncUsableState( self, "use_begin" );
#endif
	if ( other == activator )
	{//directly used by use button trace
		if ( (self->spawnflags&32) )
		{//only usable by NPCs
			if ( activator->NPC == NULL )
			{//Not an NPC
				return;
			}
		}
	}

	G_ActivateBehavior( self, BSET_USE );
	if ( self->s.eFlags & EF_SHADER_ANIM )
	{//animate shader when used
		self->s.frame++;//inc frame
		if ( self->s.frame > self->endFrame )
		{//wrap around
			self->s.frame = 0;
		}
		if ( self->target && self->target[0] )
		{
			G_UseTargets( self, activator );
		}
	}
	else if ( self->spawnflags & 8 )
	{//ALWAYS_ON
		//Remove the ability to use the entity directly
		self->svFlags &= ~SVF_PLAYER_USABLE;
		//also remove ability to call any use func at all!
		self->e_UseFunc = useF_NULL;
		
		if(self->target && self->target[0])
		{
			G_UseTargets(self, activator);
		}
		
		if ( self->wait )
		{
			self->e_ThinkFunc = thinkF_func_usable_think;
			self->nextthink = level.time + ( self->wait * 1000 );
		}

		return;
	}
	else if ( !self->count )
	{//become solid again
		self->count = 1;
		self->activator = activator;
		func_wait_return_solid( self );
	}
	else
	{
		//NOTE: MUST do this BEFORE clearing contents, or you may not open the area portal!!!
		if ( !(self->spawnflags&1) )
		{//START_OFF doesn't effect area portals
			gi.AdjustAreaPortalState( self, qtrue );
		}
		self->s.solid = 0;
		self->contents = 0;
		self->clipmask = 0;
		self->svFlags |= SVF_NOCLIENT;
		self->s.eFlags |= EF_NODRAW;
		self->count = 0;
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
		STEFX_LogFuncUsableState( self, "use_hidden" );
#endif

		if(self->target && self->target[0])
		{
			G_UseTargets(self, activator);
		}
		self->e_ThinkFunc = thinkF_NULL;
		self->nextthink = -1;
	}
}

void func_usable_pain(gentity_t *self, gentity_t *inflictor, gentity_t *attacker, const vec3_t point, int damage, int mod,int hitLoc)
{
	if ( self->paintarget )
	{
		G_UseTargets2 (self, self->activator, self->paintarget);
	}
	else
	{
		GEntity_UseFunc( self, attacker, attacker );
	}
}

void func_usable_die(gentity_t *self, gentity_t *inflictor, gentity_t *attacker, int damage, int mod,int dFlags,int hitLoc)
{
	self->takedamage = qfalse;
	GEntity_UseFunc( self, inflictor, attacker );
}

/*QUAKED func_usable (0 .5 .8) ? STARTOFF AUTOANIMATE ANIM_ONCE ALWAYS_ON BLOCKCHECK NPC_USE PLAYER_USE INACTIVE
START_OFF - the wall will not be there
AUTOANIMATE - if a model is used it will animate				
ANIM_ONCE - When turned on, goes through anim once
ALWAYS_ON - Doesn't toggle on and off when used, just runs usescript and fires target
NPC_ONLY - Only NPCs can directly use this
PLAYER_USE - Player can use it with the use button
BLOCKCHECK - Will not turn on while something is inside it

A bmodel that just sits there, doing nothing.  Can be used for conditional walls and models.
"targetname" - When used, will toggle on and off
"target"	Will fire this target every time it is toggled OFF
"target2"	Will fire this target every time it is toggled ON
"model2"	.md3 model to also draw
"modelAngles" md3 model's angles <pitch yaw roll> (in addition to any rotation on the part of the brush entity itself)
"color"		constantLight color
"light"		constantLight radius
"usescript" script to run when turned on
"deathscript"  script to run when turned off
"wait"		amount of time before the object is usable again (only valid with ALWAYS_ON flag)
"health"	if it has health, it will be used whenever shot at/killed - if you want it to only be used once this way, set health to 1
"endframe"	Will make it animate to next shader frame when used, not turn on/off... set this to number of frames in the shader, minus 1
"forcevisible" - When you turn on force sight (any level), you can see these draw through the entire level...
*/

void SP_func_usable( gentity_t *self ) 
{
	gi.SetBrushModel( self, self->model );
	InitMover( self );
	VectorCopy( self->s.origin, self->s.pos.trBase );
	VectorCopy( self->s.origin, self->currentOrigin );
	VectorCopy( self->s.origin, self->pos1 );

	self->count = 1;
	if (self->spawnflags & 1)
	{
		self->spawnContents = self->contents;	// It Navs can temporarly turn it "on"
		self->s.solid = 0;
		self->contents = 0;
		self->clipmask = 0;
		self->svFlags |= SVF_NOCLIENT;
		self->s.eFlags |= EF_NODRAW;
		self->count = 0;
	}
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
	STEFX_LogFuncUsableState( self, "spawn_after_startoff" );
#endif

	if (self->spawnflags & 2)
	{
		self->s.eFlags |= EF_ANIM_ALLFAST;
	}

	if (self->spawnflags & 4)
	{//FIXME: need to be able to do change to something when it's done?  Or not be usable until it's done?
		self->s.eFlags |= EF_ANIM_ONCE;
	}

	self->e_UseFunc = useF_func_usable_use;

	if ( self->health )
	{
		self->takedamage = qtrue;
		self->e_DieFunc = dieF_func_usable_die;
		self->e_PainFunc = painF_func_usable_pain;
	}

	if ( self->endFrame > 0 )
	{
		self->s.frame = self->startFrame = 0;
		self->s.eFlags |= EF_SHADER_ANIM;
	}

	gi.linkentity (self);

#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
	if ( !(self->svFlags & SVF_NOCLIENT) && !(self->s.eFlags & EF_NODRAW) )
	{
		STEFX_BroadcastFuncUsable( self, "spawn_visible" );
	}
	else
	{
		STEFX_LogFuncUsableState( self, "spawn_hidden_waiting_for_script" );
	}
	STEFX_LogFuncUsableState( self, "spawn_linked" );
#endif

	int forceVisible = 0;
	G_SpawnInt( "forcevisible", "0", &forceVisible );
	if ( forceVisible )
	{//can see these through walls with force sight, so must be broadcast
		if ( VectorCompare( self->s.origin, vec3_origin ) )
		{//no origin brush
			self->svFlags |= SVF_BROADCAST;
		}
		self->s.eFlags |= EF_FORCE_VISIBLE;
	}
}
