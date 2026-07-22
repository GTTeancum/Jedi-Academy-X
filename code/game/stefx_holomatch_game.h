#ifndef STEFX_HOLOMATCH_GAME_H
#define STEFX_HOLOMATCH_GAME_H

int STEFX_IsHolomatchMap(const char *mapname);
void STEFX_HolomatchGameInit(const char *mapname);
void STEFX_HolomatchGameFrame(int levelTime);
void STEFX_HolomatchBotFrame(int levelTime);

#endif
