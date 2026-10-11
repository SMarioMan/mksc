#pragma once
#ifdef MKSC_RING
#define RING_MAX_PLAYERS 8
#define RING_OPTION_ID 6
bool32 ring_main(void);
bool32 ring_active(void);
bool32 ring_transferring(void);
void ring_stop(void);
bool32 ring_exchange(void);
bool32 ring_checkRoundEnd(void);
bool32 ring_finishRace(void);
void ring_drawTitleOption(int y, bool32 selected);
void ring_initTitleOption(void);
#endif
