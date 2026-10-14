#pragma once
#ifdef MKSC_RING
#define RING_MAX_PLAYERS 8
#define RING_OPTION_ID 6
void ring_initCode(void);
bool32 ring_main(void);
bool32 ring_active(void);
void ring_queueGraphics(void);
void ring_newVideoFrame(void);
bool32 ring_canPublishGraphics(void);
bool32 ring_canPublishLateGraphics(void);
void ring_graphicsPublished(void);
void ring_publishIdentityPalette(void);
void ring_waitGraphics(void);
void ring_stop(void);
void ring_serviceAudio(void);
#ifdef MKSC_RING_MUSIC_SHARED
#define ring_serviceRenderAudio ring_serviceAudio
#else
void ring_serviceRenderAudio(void);
#endif
void ring_profileRender(void);
void ring_nativeMenuFrame(void);
void ring_restoreNativeMenuKeys(void);
void ring_drawNativeMenuCursors(void);
extern u32 ring_frameTiming[5];
bool32 ring_exchange(void);
bool32 ring_checkRoundEnd(void);
bool32 ring_finishRace(void);
void ring_drawTitleOption(int y, bool32 selected);
void ring_initTitleOption(void);
#endif
