#include "common.h"
#ifdef MKSC_RING
#include "ringSessionState.h"
#include "oam.h"
OamCellListEntry* ring_sOamCells;
u16 ring_identityBackup[24][64], ring_identityTiles[24];
u32 ring_identityAllocated, ring_identityOverflows, ring_identityNative8bpp;
u32 ring_identityPublishLines, ring_identityRestoreLines;
u16 ring_identityOldTiles[24];
u32 ring_identityPendingRestore, ring_identityPendingUpload;
u32 ring_identityResident[16];
u32 ring_identityFirstTile;
u8 sCharacters[8], sPlaces[8], sBattlePlaces[8], sBattleHudIds[4];
u16 sRoundScores[8], sGPPoints[8];
u8 sGPRace;
u32 sGPCupComplete;
struct SpmState *sNativeMenuContext, *sNativeCharacterMenu, *sNativeCourseMenu;
struct NativeMenuExtras* sNativeMenuExtras;
u8 sNativeCharacterChoices[8], sNativeCharactersReady;
u8 sNativeCharacterConfirm, sNativeCharacterBack;
u16 sNativeHostKeys[3];
bool8 sNativeHostKeysSaved;
vu16* sMenuPixels;
volatile bool32 sMenuReady;
u16 ring_sDriverInputs[8];
u8 ring_selectedCharacters[8];
u8 sSession, sResume;
u8 sEliminated;
int sGame, sBattle, sEngine, sCourse, sRetro;
bool8 sGpBannersInitialized;
u32 ring_frameTiming[5];
u32 sRenderStart;
u32 sTickEpochFrame, sTickEpochRound;
bool32 sTickEpochStarted;
bool32 sBalloonArtReady;
#endif

/* Surface samples at the eight authored Battle markers, for validation. */
u16 ring_battleSpawnTerrain[8];

#ifdef MKSC_RING
u32 ring_identityUploads, ring_identitySlowPrepares;
#endif
