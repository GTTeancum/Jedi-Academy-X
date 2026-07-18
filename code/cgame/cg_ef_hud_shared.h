#ifndef STEFX_CGAME_EF_HUD_SHARED_H
#define STEFX_CGAME_EF_HUD_SHARED_H

/*
 * Shared Elite Force Xbox interface HUD layout.
 * These values are taken from the EF cgame HUD table and are consumed by
 * Holomatch MP so the live HUD does not fork into a separate JA-style layout.
 */

typedef enum
{
	STEFX_HUD_OFF,
	STEFX_HUD_VAR,
	STEFX_HUD_GRAPHIC,
	STEFX_HUD_NUMBER
} stefxHolomatchHudType_t;

typedef struct
{
	stefxHolomatchHudType_t type;
	int x;
	int y;
	int width;
	int height;
	const char *fileName;
	qhandle_t graphic;
	int max;
	int color;
	int style;
	float timer;
} stefxHolomatchHudGraphic_t;

typedef enum
{
	STEFX_HUD_GROW,

	STEFX_HUD_HEALTH_START,
	STEFX_HUD_HEALTH_BEGINCAP,
	STEFX_HUD_HEALTH_BOX1,
	STEFX_HUD_HEALTH_SLIDERFULL,
	STEFX_HUD_HEALTH_SLIDEREMPTY,
	STEFX_HUD_HEALTH_ENDCAP,
	STEFX_HUD_HEALTH_COUNT,
	STEFX_HUD_HEALTH_END,

	STEFX_HUD_ARMOR_START,
	STEFX_HUD_ARMOR_BEGINCAP,
	STEFX_HUD_ARMOR_BOX1,
	STEFX_HUD_ARMOR_SLIDERFULL,
	STEFX_HUD_ARMOR_SLIDEREMPTY,
	STEFX_HUD_ARMOR_ENDCAP,
	STEFX_HUD_ARMOR_COUNT,
	STEFX_HUD_ARMOR_END,

	STEFX_HUD_AMMO_START,
	STEFX_HUD_AMMO_UPPER_BEGINCAP,
	STEFX_HUD_AMMO_UPPER_ENDCAP,
	STEFX_HUD_AMMO_LOWER_BEGINCAP,
	STEFX_HUD_AMMO_SLIDERFULL,
	STEFX_HUD_AMMO_SLIDEREMPTY,
	STEFX_HUD_AMMO_LOWER_ENDCAP,
	STEFX_HUD_AMMO_COUNT,
	STEFX_HUD_AMMO_END,

	STEFX_HUD_MAX
} stefxHolomatchHudIndex_t;

static stefxHolomatchHudGraphic_t stefxHolomatchHud[STEFX_HUD_MAX] =
{
	{ STEFX_HUD_VAR,     0,   0,   0,  0,  NULL,                         0, 0,   CT_NONE,      0 },

	{ STEFX_HUD_VAR,     0,   0,   0,  0,  NULL,                         0, 0,   CT_NONE,      0 },
	{ STEFX_HUD_GRAPHIC, 5,   429, 32, 64, "gfx/interface/healthcap1",   0, 0,   CT_DKBROWN1,  0 },
	{ STEFX_HUD_GRAPHIC, 64,  429, 6,  25, "gfx/interface/ammobar",      0, 0,   CT_DKBROWN1,  0 },
	{ STEFX_HUD_GRAPHIC, 72,  429, 0,  25, "gfx/interface/ammobar",      0, 0,   CT_LTBROWN1,  0 },
	{ STEFX_HUD_GRAPHIC, 0,   429, 0,  25, "gfx/interface/ammobar",      0, 0,   CT_DKBROWN1,  0 },
	{ STEFX_HUD_GRAPHIC, 72,  429, 16, 32, "gfx/interface/healthcap2",   0, 147, CT_DKBROWN1,  0 },
	{ STEFX_HUD_NUMBER,  23,  425, 16, 32, NULL,                         0, 0,   CT_LTBROWN1,  NUM_FONT_BIG },
	{ STEFX_HUD_VAR,     0,   0,   0,  0,  NULL,                         0, 0,   CT_NONE,      0 },

	{ STEFX_HUD_VAR,     0,   0,   0,  0,  NULL,                         0, 0,   CT_NONE,      0 },
	{ STEFX_HUD_GRAPHIC, 20,  458, 32, 16, "gfx/interface/armorcap1",    0, 0,   CT_DKPURPLE1, 0 },
	{ STEFX_HUD_GRAPHIC, 64,  458, 6,  12, "gfx/interface/ammobar",      0, 0,   CT_DKPURPLE1, 0 },
	{ STEFX_HUD_GRAPHIC, 72,  458, 0,  12, "gfx/interface/ammobar",      0, 0,   CT_LTPURPLE1, 0 },
	{ STEFX_HUD_GRAPHIC, 0,   458, 0,  12, "gfx/interface/ammobar",      0, 0,   CT_DKPURPLE1, 0 },
	{ STEFX_HUD_GRAPHIC, 72,  458, 16, 16, "gfx/interface/armorcap2",    0, 147, CT_DKPURPLE1, 0 },
	{ STEFX_HUD_NUMBER,  44,  458, 16, 16, NULL,                         0, 0,   CT_LTPURPLE1, NUM_FONT_SMALL },
	{ STEFX_HUD_VAR,     0,   0,   0,  0,  NULL,                         0, 0,   CT_NONE,      0 },

	{ STEFX_HUD_VAR,     0,   0,   0,  0,  NULL,                         0, 0,   CT_NONE,      0 },
	{ STEFX_HUD_GRAPHIC, 613, 429, 32, 64, "gfx/interface/ammouppercap1",0, 0,   CT_LTPURPLE2, 0 },
	{ STEFX_HUD_GRAPHIC, 607, 429, 16, 32, "gfx/interface/ammouppercap2",0, 572, CT_LTPURPLE2, 0 },
	{ STEFX_HUD_GRAPHIC, 613, 458, 16, 16, "gfx/interface/ammolowercap1",0, 0,   CT_LTPURPLE2, 0 },
	{ STEFX_HUD_GRAPHIC, 578, 458, 0,  12, "gfx/interface/ammobar",      0, 0,   CT_LTPURPLE1, 0 },
	{ STEFX_HUD_GRAPHIC, 0,   458, 0,  12, "gfx/interface/ammobar",      0, 0,   CT_DKPURPLE1, 0 },
	{ STEFX_HUD_GRAPHIC, 607, 458, 16, 16, "gfx/interface/ammolowercap2",0, 572, CT_LTPURPLE2, 0 },
	{ STEFX_HUD_NUMBER,  573, 425, 16, 32, NULL,                         0, 0,   CT_LTPURPLE1, NUM_FONT_BIG },
	{ STEFX_HUD_VAR,     0,   0,   0,  0,  NULL,                         0, 0,   CT_NONE,      0 }
};

#endif
