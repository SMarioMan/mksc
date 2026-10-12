#pragma once
#ifdef MKSC_RING
#define RING_MAX_PLAYERS 8
#define RING_OPTION_ID 6
void ring_initCode(void);
bool32 ring_main(void);
bool32 ring_active(void);
void ring_queueGraphics(void);
bool32 ring_canPublishGraphics(void);
void ring_graphicsPublished(void);
void ring_waitGraphics(void);
void ring_stop(void);
void ring_serviceAudio(void);
void ring_profileRender(void);
extern u32 ring_frameTiming[5];
bool32 ring_exchange(void);
bool32 ring_checkRoundEnd(void);
bool32 ring_finishRace(void);
void ring_drawTitleOption(int y, bool32 selected);
void ring_initTitleOption(void);
#endif
