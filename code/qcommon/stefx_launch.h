#ifndef STEFX_LAUNCH_H
#define STEFX_LAUNCH_H

#define STEFX_HOLOMATCH_SETUP_MAGIC 0x484D4546u
#define STEFX_HOLOMATCH_SETUP_VERSION 4
#define STEFX_HOLOMATCH_MAX_LOCAL_PLAYERS 4

typedef struct stefxHolomatchPlayerSetup_s
{
	char modelName[MAX_QPATH];
	char skinName[MAX_QPATH];
	int controlStyle;
	int autoswitchMode;
	int autoaimMode;
	int crosshair;
	int vibration;
	int invertPitch;
} stefxHolomatchPlayerSetup_t;

typedef struct stefxHolomatchLaunchSetup_s
{
	unsigned int magic;
	int version;
	char mapName[MAX_QPATH];
	int players;
	int humanPlayers;
	int fragLimit;
	int timeLimit;
	int forceRespawn;
	int weaponStay;
	int fallingDamage;
	int teamPlay;
	int friendlyFire;
	int diagnosticVirtualControls;
	int diagnosticVirtualControlsP1;
	stefxHolomatchPlayerSetup_t player[STEFX_HOLOMATCH_MAX_LOCAL_PLAYERS];
} stefxHolomatchLaunchSetup_t;

#endif
