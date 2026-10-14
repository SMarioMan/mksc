#include "common.h"
#ifdef MKSC_RING
#include "ringSessionState.h"
u8 sCharacters[8], sPlaces[8], sBattlePlaces[8], sBattleHudIds[4];
u16 sWins[8], sGPPoints[8];
u8 sGPRace;
u32 sGPCupComplete;
struct SpmState *sNativeMenuContext, *sNativeCharacterMenu, *sNativeCourseMenu;
struct NativeMenuExtras* sNativeMenuExtras;
u8 sNativeCharacterChoices[8], sNativeCharactersReady;
u8 sNativeCharacterConfirm, sNativeCharacterBack;
u16 sNativeCharacterCursorCells[25], sNativeHostKeys[3];
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
