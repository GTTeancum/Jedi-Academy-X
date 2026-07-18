// Compile the wholesale SP input implementation against MP's active-client pointers.
#include "../server/exe_headers.h"
#include "client.h"
#include "../cgame/cg_local.h"
#include "cl_data.h"
#include "../win32/xb_log.h"

float cl_mSensitivityOverride = 0.0f;
qboolean cl_bUseFighterPitch = qfalse;
short cg_crossHairStatus = 0;
int g_lastFireTime = 0;

#define cl (*cl)
#define clc (*clc)
#define frame snap
#define packetTime spPacketTime
#define packetCmdNumber spPacketCmdNumber
#define CL_WritePacket CL_SPWritePacket
#define CL_SendCmd CL_SPSendCmd
#include "cl_input.cpp"
#undef CL_SendCmd
#undef CL_WritePacket
#undef packetCmdNumber
#undef packetTime
#undef frame
#undef clc
#undef cl

void CL_WritePacket( void ) {
	msg_t		buf;
	byte		data[MAX_MSGLEN];
	int			i, j;
	usercmd_t	*cmd, *oldcmd;
	usercmd_t	nullcmd;
	int			packetNum;
	int			oldPacketNum;
	int			count, key;

#ifdef _XBOX
	if ( cls.state == CA_CINEMATIC ) {
#else
	if ( clc->demoplaying || cls.state == CA_CINEMATIC ) {
#endif
		return;
	}

	Com_Memset( &nullcmd, 0, sizeof(nullcmd) );
	oldcmd = &nullcmd;

	MSG_Init( &buf, data, sizeof(data) );
	MSG_Bitstream( &buf );
	MSG_WriteLong( &buf, cl->serverId );
	MSG_WriteLong( &buf, clc->serverMessageSequence );
	MSG_WriteLong( &buf, clc->serverCommandSequence );

	for ( i = clc->reliableAcknowledge + 1 ; i <= clc->reliableSequence ; i++ ) {
		MSG_WriteByte( &buf, clc_clientCommand );
		MSG_WriteLong( &buf, i );
		MSG_WriteString( &buf, clc->reliableCommands[ i & (MAX_RELIABLE_COMMANDS-1) ] );
	}

	if ( cl_packetdup->integer < 0 ) {
		Cvar_Set( "cl_packetdup", "0" );
	} else if ( cl_packetdup->integer > 5 ) {
		Cvar_Set( "cl_packetdup", "5" );
	}
	oldPacketNum = (clc->netchan.outgoingSequence - 1 - cl_packetdup->integer) & PACKET_MASK;
	count = cl->cmdNumber - cl->outPackets[ oldPacketNum ].p_cmdNumber;
	if ( count > MAX_PACKET_USERCMDS ) {
		count = MAX_PACKET_USERCMDS;
		Com_Printf("MAX_PACKET_USERCMDS\n");
	}
	if ( count >= 1 ) {
		if ( cl_showSend->integer ) {
			Com_Printf( "(%i)", count );
		}

		if ( cl_nodelta->integer || !cl->snap.valid
#ifndef _XBOX
			|| clc->demowaiting
#endif
			|| clc->serverMessageSequence != cl->snap.messageNum ) {
			MSG_WriteByte (&buf, clc_moveNoDelta);
		} else {
			MSG_WriteByte (&buf, clc_move);
		}

		MSG_WriteByte( &buf, count );

		key = clc->checksumFeed;
		key ^= clc->serverMessageSequence;
		key ^= Com_HashKey(clc->serverCommands[ clc->serverCommandSequence & (MAX_RELIABLE_COMMANDS-1) ], 32);

		for ( i = 0 ; i < count ; i++ ) {
			j = (cl->cmdNumber - count + i + 1) & CMD_MASK;
			cmd = &cl->cmds[j];
			MSG_WriteDeltaUsercmdKey (&buf, key, oldcmd, cmd);
			oldcmd = cmd;
		}

		if (cl->gcmdSentValue)
		{
			cl->gcmdSendValue = qfalse;
			cl->gcmdSentValue = qfalse;
			cl->gcmdValue = 0;
		}
	}

	packetNum = clc->netchan.outgoingSequence & PACKET_MASK;
	cl->outPackets[ packetNum ].p_realtime = cls.realtime;
	cl->outPackets[ packetNum ].p_serverTime = oldcmd->serverTime;
	cl->outPackets[ packetNum ].p_cmdNumber = cl->cmdNumber;
	clc->lastPacketSentTime = cls.realtime;

	if ( cl_showSend->integer ) {
		Com_Printf( "%i ", buf.cursize );
	}

	CL_Netchan_Transmit (&clc->netchan, &buf);
	while ( clc->netchan.unsentFragments ) {
		CL_Netchan_TransmitNextFragment( &clc->netchan );
	}
}

void CL_SendCmd( void ) {
	if ( cls.state < CA_CONNECTED ) {
		return;
	}

	if ( com_sv_running->integer && sv_paused->integer && cl_paused->integer ) {
		return;
	}

	CL_CreateNewCommands();

#ifdef _XBOX
	if(ClientManager::splitScreenMode == qtrue)
	{
		CM_START_LOOP();
		if ( !CL_ReadyToSendPacket() ) {
			if ( cl_showSend->integer ) {
				Com_Printf( ". " );
			}
			continue;
		}

		CL_WritePacket();
		CM_END_LOOP();
	}
	else
	{
#endif
	if ( !CL_ReadyToSendPacket() ) {
		if ( cl_showSend->integer ) {
			Com_Printf( ". " );
		}
		return;
	}

	CL_WritePacket();

#ifdef _XBOX
	}
#endif
}
