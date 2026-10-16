#pragma once
#include "common.h"

/* Cold menu/session records live above native EWRAM scratch. Keep the input
 * transport, audio service and frame publication state in IWRAM. */
struct SpmState;
struct NativeMenuExtras;
extern u8 sCharacters[8], sPlaces[8], sBattlePlaces[8], sBattleHudIds[4];
extern u16 sRoundScores[8], sGPPoints[8];
extern u8 sGPRace;
extern u32 sGPCupComplete;
extern struct SpmState *sNativeMenuContext, *sNativeCharacterMenu, *sNativeCourseMenu;
extern struct NativeMenuExtras* sNativeMenuExtras;
extern u8 sNativeCharacterChoices[8], sNativeCharactersReady;
extern u8 sNativeCharacterConfirm, sNativeCharacterBack;
extern u16 sNativeHostKeys[3];
extern bool8 sNativeHostKeysSaved;
extern vu16* sMenuPixels;
extern volatile bool32 sMenuReady;

extern u16 ring_sDriverInputs[8];
extern u8 ring_selectedCharacters[8];
extern u8 sSession, sResume;
extern u8 sEliminated;
extern int sGame, sBattle, sEngine, sCourse, sRetro;
extern bool8 sGpBannersInitialized;
extern u32 ring_frameTiming[5];
extern u32 sRenderStart;
extern u32 sTickEpochFrame, sTickEpochRound;
extern bool32 sTickEpochStarted;
extern bool32 sBalloonArtReady;

extern u16 ring_identityBackup[24][64], ring_identityTiles[24];
extern u32 ring_identityAllocated, ring_identityOverflows, ring_identityNative8bpp;
extern u32 ring_identityPublishLines, ring_identityRestoreLines;
extern u16 ring_identityOldTiles[24];
extern u32 ring_identityPendingRestore, ring_identityPendingUpload;
extern u32 ring_identityResident[16];
extern u32 ring_identityFirstTile;

extern u32 ring_identityUploads, ring_identitySlowPrepares;
