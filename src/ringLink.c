#include "common.h"
#ifdef MKSC_RING
#include "ringLink.h"
#include "main.h"
#include "sio2.h"
#include "scene.h"
#include "title.h"
#include "libunk/irq.h"
#include "oam.h"
#include "palette.h"
#include "track.h"
#include "map.h"
#include "ringFont.h"
#include "vcount.h"
#include "transition.h"
#include "mp2000/m4a.h"
#include "nativeGameInfo.h"
#include "singleplayer.h"
#include "menuQueries.h"
#include "ringSessionState.h"
#include "songs.h"

/* Serial transport owns timer 2 only. Audio owns timers 0/1; stock cable
 * transports keep timer 3. All software IDs travel through the cable. */
#define RING_DATA (*(vu32*)0x04000120)
/* Standard normal-32 mode at 2 MHz. Keep the IRQ forwarding turnaround;
 * the slower 256 kHz clock needlessly stretched eight-player barriers. */
#define RING_CONTROL 0x5002
#define RING_START 0x80
#define RING_INTERNAL 1
#define RING_INVALID 0xFF
#define RING_TURNAROUND 512
#define RING_IRQ __attribute__((section(".ring_irq")))

enum { DISCOVER = 1, ANNOUNCE = 2, COMMIT = 5, ABORT = 6, READY = 7 };
typedef struct
{
    u32 queue[16];
    u16 staged[8];
    u32 rounds, packets, errors, timeouts;
    u32 startVideoFrame;
    u16 lastShift;
    u8 head, tail, queued, seen, id, count, sequence;
    u8 enabled, owner, inFlight, complete, requested, ownReturned;
    u8 commitSent, failed, inGame, readySeen, frameStarted;
    u32 rejectedWord, rejectionReason;
    u16 committed[8];
    u8 barrierWaiting, commitPending;
    u32 commitPacket;
} RingState;
static volatile RingState sRing __attribute__((section(".ring_state")));
u16 ring_sControlWords[8];
extern u16 ring_sDriverInputs[8];
u8 ring_balloonAffineIndex[8];
extern u8 ring_selectedCharacters[8];
extern u8 gSelectedCharacters[8];
extern u16 sio2_sKeyBuf[8];
void sub_804AE58(RaceState* state, int character, int human);
int spm_menuCharIdxToCharId(int menuIndex);
/* Zero means a fresh discovery; every resume destination must be nonzero. */
enum { GAME = 1, ENGINE, CHARACTERS, CUP, COURSE, ARENA };
extern u8 sSession, sResume;
extern u8 sEliminated;
enum { VS_MODE, BATTLE_MODE, GP_MODE };
extern int sGame, sBattle, sEngine, sCourse, sRetro;
extern bool8 sGpBannersInitialized;
extern u32 ring_frameTiming[5];
/* Diagnostic counts: accepted starts on each of eleven native players,
 * mixer services, missed video-frame services, longest service gap,
 * recovered segments and segments prepared after their playback deadline. */
extern u32 ring_audioTiming[16];
extern u32 ring_audioMenuStarts[11];
extern u32 ring_audioDmaOverride;
/* Preserve the queued PCM destination across a VBlank interrupt during the
 * music-player pass; the assembler applies the configured queue depth. */
u32 ring_audioMixCounter(struct SoundInfo* sound)
{
    if (!sRing.enabled) return 0;
    return ring_audioDmaOverride ? ring_audioDmaOverride : sound->pcmDmaCounter + 1;
}

void ring_noteAudioStart(struct MusicPlayerInfo* player)
{
    u32 i;
    u32* starts = sRing.inGame ? ring_audioTiming : ring_audioMenuStarts;
    if (!sRing.enabled) return;
    for (i = 0; i < 11; i++)
        if (player == gMPlayTable[i].info) { starts[i]++; break; }
}
extern u32 sRenderStart;
static u32 ring_cycles(void)
{
    u32 frame, line;
    u16 ime = REG_IME;
    REG_IME = 0;
    frame = gSceneState.field14;
    line = REG_VCOUNT;
    /* IF may already be pending before the VBlank handler increments the
     * software frame. Include it while interrupts are masked for this read. */
    if (REG_IF & IRQ_MASK_VBLANK) frame++;
    REG_IME = ime;
    return frame * 280896 + (line >= 160 ? line - 160 : line + 68) * 1232;
}

void ring_profileRender(void)
{
    ring_frameTiming[1] = ring_cycles() - sRenderStart;
}

#ifdef MKSC_RING_MUSIC_SHARED
static u8 sMusicRacing, sSharedSong, sSharedFinalLap, sCourseMusicStarted;
int ring_sharedMusicActive(void)
{
    return sRing.enabled && sRing.inGame && sMusicRacing;
}
int ring_sharedMusicMuted(void)
{
    return sRing.enabled && sRing.count && sRing.id != 0;
}
/* The stock mixer dedicates players 1/2 to the local engine/voice and
 * players 3/4 to remote racers. Keeping only local channels lets eight
 * consoles be mixed without playing each racer eight times. */
int ring_sharedAudioMuted(struct MusicPlayerInfo* player, struct SongHeader* song)
{
    extern const u16 _080ECB78[8];
    extern const u16 _080ECBC8[8][4], _080ECC08[8][4];
    extern const u16 _080ECC48[8][2], _080ECC88[8][2];
    u32 i, character;
    Driver* local;
    if (!sRing.enabled) return FALSE;
    if (player == gMPlayTable[0].info && ring_sharedMusicActive() && !sBattle &&
        (song == gSongTable[5].header || song == gSongTable[6].header || song == gSongTable[7].header))
    {
        RaceState* state = &gSceneState.raceState;
        u32 finished = 0;
        for (i = 0; i < state->driverCount; i++)
            if (state->drivers[i]->lap >= (sGame == GP_MODE ? ((u8*)state)[0x7E7] : 0x83)) finished++;
        /* One console finishing must not replace the other racers' music. */
        return finished < state->driverCount - 1;
    }
    if (!sRing.inGame)
    {
        /* Mode, course and results pages replay the same host input on all
         * peers. Emit their common menu effects once. Character selection
         * keeps each console's own navigation/confirmation sounds. */
        return sRing.count && sRing.id != 0 && !sNativeCharacterMenu;
    }
    if (player == gMPlayTable[3].info || player == gMPlayTable[4].info) return TRUE;
    if (player != gMPlayTable[2].info && player != gMPlayTable[6].info) return FALSE;
    local = gSceneState.raceState.drivers[sRing.id];
    if (!local) return FALSE;
    character = *(u32*)((u8*)local + 0x4C) & 7;
    if (player == gMPlayTable[2].info)
    {
        /* Native voice selection sometimes follows the spectator camera.
         * Every song on this mixer player is one of these eight local clips;
         * retain this console's character even when the camera changes. */
        for (i = 0; i < 2; i++)
            if (song == gSongTable[_080ECBC8[character][i]].header ||
                song == gSongTable[_080ECC08[character][i]].header ||
                song == gSongTable[_080ECC48[character][i]].header ||
                song == gSongTable[_080ECC88[character][i]].header) return FALSE;
        return TRUE;
    }
    for (i = 0; i < 8; i++)
        if (song == gSongTable[_080ECB78[i]].header)
            return i != character;
    return FALSE;
}

u16 ring_sharedMusicSong(u16 song)
{
    /* Let the native countdown request the first course song. Physics and
     * the introductory fanfare both begin before Lakitu gives the start. */
    if (ring_sharedMusicActive() &&
        song == (sBattle ? SONG_BATTLE : track_getRaceMusic(&gSceneState.raceState)))
        sCourseMusicStarted = TRUE;
    return ring_sharedMusicActive() && song >= 5 && song <= 7 ? 5 : song;
}

int ring_keepSharedRaceMusic(void)
{
    RaceState* state = &gSceneState.raceState;
    u32 i, finished = 0;
    if (!ring_sharedMusicActive() || ring_sharedMusicMuted() || !sCourseMusicStarted || sBattle ||
        (((u8*)state)[0x7D4] & 0x40)) return FALSE;
    for (i = 0; i < state->driverCount; i++)
        if (state->drivers[i]->lap >= (sGame == GP_MODE ? ((u8*)state)[0x7E7] : 0x83)) finished++;
    /* Native local-finish cleanup stops the BGM before requesting a result
     * song. Preserve its playback position while other humans are racing. */
    return finished && finished < state->driverCount - 1;
}
/* Derive BGM from the whole race, including shared power-up themes. Native
 * final-lap flag is bit 3. Latch it: finish/spectator setup may clear it. */
static void ring_updateSharedMusic(void)
{
    extern void m4aMPlayTempoControl(struct MusicPlayerInfo*, u16);
    RaceState* state = &gSceneState.raceState;
    extern u16 ring_raceEffectMusic(const RaceState*);
    u32 i, fast = 0, music, effect, finished = 0, alive = 0;
    if (ring_sharedMusicMuted())
    {
        if (!(gMPlayTable[0].info->status & MUSICPLAYER_STATUS_PAUSE))
            m4aMPlayStop(gMPlayTable[0].info);
        sSharedSong = 0; sSharedFinalLap = 0;
        return;
    }
    if (!ring_sharedMusicActive()) { sSharedSong = 0; sSharedFinalLap = 0; return; }
    if (!sCourseMusicStarted) return;
    if (((u8*)state)[0x7D4] & 0x40) return;
    /* Physics begins during the native opening fanfare. Do not treat its
     * first tick as permission to replace a still-playing start song. Both
     * GP and linked races use player zero for this finite introductory cue. */
    if ((gMPlayTable[0].info->songHeader == gSongTable[3].header ||
         gMPlayTable[0].info->songHeader == gSongTable[4].header) &&
        (gMPlayTable[0].info->status & MUSICPLAYER_STATUS_TRACK) &&
        !(gMPlayTable[0].info->status & MUSICPLAYER_STATUS_PAUSE)) return;
    for (i = 0; i < state->driverCount; i++)
    {
        u8* driver = (u8*)state->drivers[i];
        if (*(u32*)(driver + 0x48) & 8) fast = 1;
        if (driver[0xC7] >= (sGame == GP_MODE ? ((u8*)state)[0x7E7] : 0x83)) finished++;
        if (driver[0x17D]) alive++;
    }
    /* Preserve the common result jingle during native end-of-race cleanup. */
    if ((!sBattle && finished >= state->driverCount - 1) || (sBattle && alive <= 1)) return;
    fast = sBattle ? 0 : fast || sSharedFinalLap;
    music = sBattle ? 18 : track_getRaceMusic(state);
    if (music == 36) music = 28;
    /* Native timers belong to every racer, including CPUs. Star takes
     * priority over lightning until the last protected racer loses it. */
    effect = ring_raceEffectMusic(state);
    if (effect) music = effect;
    if (gMPlayTable[0].info->songHeader == gSongTable[19].header &&
        (gMPlayTable[0].info->status & MUSICPLAYER_STATUS_TRACK)) return;
    if (sSharedSong != music || sSharedFinalLap != fast)
    {
        if (fast && !sSharedFinalLap)
        {
            m4aSongNumStart(19);
            sSharedSong = music; sSharedFinalLap = fast;
            return;
        }
        MPlayStart(gMPlayTable[0].info, gSongTable[music].header);
        sSharedSong = music; sSharedFinalLap = fast;
    }
    /* A finite final-lap cue finishes before returning to the common theme. */
    if (gMPlayTable[0].info->songHeader == gSongTable[19].header &&
        (gMPlayTable[0].info->status & MUSICPLAYER_STATUS_TRACK)) return;
    if (gMPlayTable[0].info->songHeader != gSongTable[music].header)
        MPlayStart(gMPlayTable[0].info, gSongTable[music].header);
    m4aMPlayTempoControl(gMPlayTable[gSongTable[music].ms].info, fast ? 320 : 256);
}
#endif

bool32 ring_ignoreLocalMusic(void)
{
#ifdef MKSC_RING_MUSIC_SHARED
    return ring_sharedMusicActive() && sCourseMusicStarted;
#else
    return FALSE;
#endif
}

void ring_prepareGpGrid(RaceState* state)
{
    u32 i, rank;
    Driver** order = (Driver**)((u8*)state + 0xC38);
    if (!sRing.enabled || sGame != GP_MODE) return;
    /* Native GP assumes one human. Recreate our shared roster after its
     * single-player CPU setup, then assign every human a separate grid slot. */
    state->driverCount = sRing.count;
    for (i = sRing.count; i < 8; i++)
        sub_804AE58(state, spm_menuCharIdxToCharId(sCharacters[i]), 0);
    for (i = 0; i < 8; i++)
    {
        if (sGPRace)
        {
            for (rank = 0; rank < 8 && sPlaces[rank] != i; rank++);
            if (rank == 8) rank = i;
        }
        else rank = i < sRing.count ? 8 - sRing.count + i : i - sRing.count;
        *(u16*)((u8*)state->drivers[i] + 0xBA) = rank * 2;
        ((u8*)state->drivers[i])[0xC4] = sGPPoints[i];
        order[rank] = state->drivers[i];
    }
}

void ring_updateGpFinishBanners(void)
{
    NativeFinishBanner* banners = sub_800EDDC()->rows.banners;
    extern void race_updateFinishBanners(int stagger);
    u32 i;
    if (!sRing.enabled || sGame != GP_MODE) return;
    /* Stock multiplayer initialization leaves character 8 in these shared
     * records. GP expects -1 until each racer finishes; 8 reads past its
     * eight-name table into compressed course art and corrupts the tilemap.
     * Initialize once at rendering time, after native menu setup. Result
     * rows reuse the same memory between races, so even an in-range character
     * cannot identify a valid banner on the next race's first frame. Let the
     * original updater add newly finished racers with normal timing. */
    for (i = 0; i < 8; i++)
        if (!sGpBannersInitialized || banners[i].character < -1 || banners[i].character >= 8)
        {
            banners[i].column = banners[i].slideTick = banners[i].delay = 0;
            banners[i].character = -1;
        }
    sGpBannersInitialized = TRUE;
    race_updateFinishBanners(1);
}

static u32 sLastAudioFrame;
static volatile bool32 sGraphicsReady;
static volatile u32 sVideoTick;
extern u32 sTickEpochFrame, sTickEpochRound;
extern bool32 sTickEpochStarted;

void ring_newVideoFrame(void)
{
    sVideoTick = gSceneState.field14 + 1;
}

void ring_queueGraphics(void)
{
    ring_prepareIdentityGraphics();
    sGraphicsReady = TRUE;
}

bool32 ring_canPublishGraphics(void)
{
    return !sRing.enabled || !sRing.inGame || (sGraphicsReady && !sRing.frameStarted &&
        (!sTickEpochStarted || sRing.rounds - sTickEpochRound <= sVideoTick - sTickEpochFrame));
}

bool32 ring_canPublishLateGraphics(void)
{
    /* Menus and native load/countdown handshakes retain one VBlank barrier.
     * Only the initialized physics loop may consume the late-publish path. */
    return sRing.enabled && sRing.inGame && sTickEpochStarted && ring_canPublishGraphics();
}

void ring_graphicsPublished(void)
{
    sGraphicsReady = FALSE;
}

void ring_waitGraphics(void)
{
    extern bool32 main_tryPublishRingGraphics(void);
    while (sRing.enabled && sGraphicsReady)
    {
        if (main_tryPublishRingGraphics()) break;
        VBlankIntrWait();
        /* Start the input train before mixing the newly published frame.
         * Timer 2 can then clock it while SoundMain runs in the main thread. */
        if (sGraphicsReady) ring_serviceAudio();
    }
}

static bool32 ring_mixAudioSegment(u32 counter, u32 deadline)
{
    bool32 available = SOUND_INFO_PTR->ident == ID_NUMBER;
    ring_audioDmaOverride = counter + 1;
    m4aSoundMain();
    if (sRing.enabled && available && gSceneState.field14 >= deadline)
    {
        ring_audioTiming[15]++;
        return FALSE;
    }
    return available;
}

static void ring_mixFrameAudio(void)
{
    struct SoundInfo* sound = SOUND_INFO_PTR;
    u16 ime = REG_IME;
    u32 frame, counter, started, gap;
    REG_IME = 0;
    frame = gSceneState.field14;
    counter = sound->pcmDmaCounter;
    REG_IME = ime;
    if (frame == sLastAudioFrame) return;
    started = ring_cycles();
    gap = frame - sLastAudioFrame;
    if (sRing.enabled)
    {
        ring_audioTiming[11]++;
        if (gap > 1) ring_audioTiming[12] += gap - 1;
        if (gap > ring_audioTiming[13]) ring_audioTiming[13] = gap;
    }
    sLastAudioFrame = frame;
#ifdef MKSC_RING_MUSIC_SHARED
    ring_updateSharedMusic();
#endif
#ifdef MKSC_RING_MUSIC_SHARED
    if (sRing.enabled && gap > 1)
    {
        /* A one-frame overrun still leaves the queued segment's entire
         * playback deadline available. Advance the native mixer for
         * that elapsed frame before preparing the current frame. */
        u32 previous = counter + 1;
        if (previous > sound->pcmDmaPeriod) previous = 1;
        if (ring_mixAudioSegment(previous, frame + 1)) ring_audioTiming[14]++;
#ifdef MKSC_RING_MUSIC_SHARED
        ring_updateSharedMusic();
#endif
    }
    ring_mixAudioSegment(counter, frame + 2);
#else
    if (sRing.enabled && gap > 1)
    {
        u32 missing = gap - 1, offset;
        if (missing > 2) missing = 2;
        for (offset = missing; offset; offset--)
        {
            u32 previous = counter + offset;
            if (previous > sound->pcmDmaPeriod) previous -= sound->pcmDmaPeriod;
            if (ring_mixAudioSegment(previous, frame + 3 - offset)) ring_audioTiming[14]++;
        }
    }
    ring_mixAudioSegment(counter, frame + 3);
#endif
    ring_audioDmaOverride = 0;
    ring_frameTiming[4] = ring_cycles() - started;
}

/* The serial wait loop and each driver update check this frequently. Keep
 * the common already-serviced path separate from the mixer's register/stack
 * setup, and snapshot the frame/counter atomically only when mixing. */
void ring_serviceAudio(void)
{
    if (gSceneState.field14 != sLastAudioFrame) ring_mixFrameAudio();
}

#ifndef MKSC_RING_MUSIC_SHARED
/* Routine mixing belongs inside the post-publication input train, where
 * serial clocks and PCM synthesis overlap. The two-segment queue still
 * requires an early rescue if rendering crosses an additional video frame. */
void ring_serviceRenderAudio(void)
{
    if (gSceneState.field14 - sLastAudioFrame > 2) ring_mixFrameAudio();
}
#endif

static bool32 results_main(void);

bool32 ring_preserveRaceDisplay(void)
{
    return sRing.enabled && gSceneState.sceneMainFunc == results_main;
}
/* Background driver updates may finish the last racer after the page opens.
 * Those native race cues must not replace the results theme. */
bool32 ring_resultsAudioBlocked(struct MusicPlayerInfo* player, struct SongHeader* song)
{
    extern const struct Song gSongTable[];
    return ring_preserveRaceDisplay() && player == gMPlayTable[0].info &&
        song != gSongTable[sGame == GP_MODE ? SONG_014 : SONG_017].header;
}
static bool32 connectionError_main(void);


static const u8 sCrc8[256] = {
    0x00, 0x07, 0x0E, 0x09, 0x1C, 0x1B, 0x12, 0x15, 0x38, 0x3F, 0x36, 0x31, 0x24, 0x23, 0x2A, 0x2D,
    0x70, 0x77, 0x7E, 0x79, 0x6C, 0x6B, 0x62, 0x65, 0x48, 0x4F, 0x46, 0x41, 0x54, 0x53, 0x5A, 0x5D,
    0xE0, 0xE7, 0xEE, 0xE9, 0xFC, 0xFB, 0xF2, 0xF5, 0xD8, 0xDF, 0xD6, 0xD1, 0xC4, 0xC3, 0xCA, 0xCD,
    0x90, 0x97, 0x9E, 0x99, 0x8C, 0x8B, 0x82, 0x85, 0xA8, 0xAF, 0xA6, 0xA1, 0xB4, 0xB3, 0xBA, 0xBD,
    0xC7, 0xC0, 0xC9, 0xCE, 0xDB, 0xDC, 0xD5, 0xD2, 0xFF, 0xF8, 0xF1, 0xF6, 0xE3, 0xE4, 0xED, 0xEA,
    0xB7, 0xB0, 0xB9, 0xBE, 0xAB, 0xAC, 0xA5, 0xA2, 0x8F, 0x88, 0x81, 0x86, 0x93, 0x94, 0x9D, 0x9A,
    0x27, 0x20, 0x29, 0x2E, 0x3B, 0x3C, 0x35, 0x32, 0x1F, 0x18, 0x11, 0x16, 0x03, 0x04, 0x0D, 0x0A,
    0x57, 0x50, 0x59, 0x5E, 0x4B, 0x4C, 0x45, 0x42, 0x6F, 0x68, 0x61, 0x66, 0x73, 0x74, 0x7D, 0x7A,
    0x89, 0x8E, 0x87, 0x80, 0x95, 0x92, 0x9B, 0x9C, 0xB1, 0xB6, 0xBF, 0xB8, 0xAD, 0xAA, 0xA3, 0xA4,
    0xF9, 0xFE, 0xF7, 0xF0, 0xE5, 0xE2, 0xEB, 0xEC, 0xC1, 0xC6, 0xCF, 0xC8, 0xDD, 0xDA, 0xD3, 0xD4,
    0x69, 0x6E, 0x67, 0x60, 0x75, 0x72, 0x7B, 0x7C, 0x51, 0x56, 0x5F, 0x58, 0x4D, 0x4A, 0x43, 0x44,
    0x19, 0x1E, 0x17, 0x10, 0x05, 0x02, 0x0B, 0x0C, 0x21, 0x26, 0x2F, 0x28, 0x3D, 0x3A, 0x33, 0x34,
    0x4E, 0x49, 0x40, 0x47, 0x52, 0x55, 0x5C, 0x5B, 0x76, 0x71, 0x78, 0x7F, 0x6A, 0x6D, 0x64, 0x63,
    0x3E, 0x39, 0x30, 0x37, 0x22, 0x25, 0x2C, 0x2B, 0x06, 0x01, 0x08, 0x0F, 0x1A, 0x1D, 0x14, 0x13,
    0xAE, 0xA9, 0xA0, 0xA7, 0xB2, 0xB5, 0xBC, 0xBB, 0x96, 0x91, 0x98, 0x9F, 0x8A, 0x8D, 0x84, 0x83,
    0xDE, 0xD9, 0xD0, 0xD7, 0xC2, 0xC5, 0xCC, 0xCB, 0xE6, 0xE1, 0xE8, 0xEF, 0xFA, 0xFD, 0xF4, 0xF3,
};

/* Scaled OAM coordinates use small numerators and positive scales. The
 * reciprocal estimate is at most one below truncation toward zero; testing
 * the remainder restores the exact BIOS quotient without invoking a SWI. */
extern s32 __divsi3(s32 numerator, s32 denominator);
static const u16 sSpriteReciprocal[] = {
#include "ringReciprocal.inc"
};

s32 RING_IRQ ring_spriteDivide(s32 numerator, s32 denominator)
{
    u32 magnitude, quotient;
    if (denominator == 1) return numerator;
    if (denominator < 2 || denominator > 4096 || numerator < -65536 || numerator > 65536)
        return __divsi3(numerator, denominator);
    magnitude = numerator < 0 ? -numerator : numerator;
    quotient = (magnitude * sSpriteReciprocal[denominator - 2]) >> 16;
    if (magnitude - quotient * denominator >= denominator) quotient++;
    return numerator < 0 ? -quotient : quotient;
}

static inline u8 RING_IRQ checksum(u32 value)
{
    u32 c = sCrc8[value >> 24];
    c = sCrc8[c ^ ((value >> 16) & 255)];
    return sCrc8[c ^ ((value >> 8) & 255)];
}

static u32 packet(u32 type, u32 node, u32 sequence, u32 payload)
{
    u32 word = type << 28 | (node & 7) << 25 | (sequence & 15) << 21 | (payload & 8191) << 8;
    return word | checksum(word);
}

static void RING_IRQ enqueue(u32 word)
{
    u16 previousIme = REG_IME;
    REG_IME = 0;
    if (sRing.queued == 16)
    {
        sRing.rejectedWord = word;
        sRing.rejectionReason = 1;
        sRing.errors++;
        sRing.failed = TRUE;
        REG_IME = previousIme;
        return;
    }
    sRing.queue[sRing.tail] = word;
    sRing.tail = (sRing.tail + 1) & 15;
    sRing.queued++;
    REG_IME = previousIme;
}

static u32 RING_IRQ dequeue(void)
{
    u32 word;
    if (!sRing.queued)
        return 0;
    word = sRing.queue[sRing.head];
    sRing.head = (sRing.head + 1) & 15;
    sRing.queued--;
    return word;
}

static void beginCommit(void)
{
    /* READY already carries each input. FIFO forwarding places COMMIT after
     * every READY at the owner, so all inputs precede it at every receiver. */
    sRing.ownReturned = sRing.complete = 0;
    sRing.commitSent = TRUE;
    sRing.startVideoFrame = gSceneState.field14;
    sRing.frameStarted = TRUE;
    sRing.commitPacket = sRing.packets;
    enqueue(packet(COMMIT, 0, sRing.sequence, 0));
    sRing.requested = FALSE;
}

/* Discovery runs once per connection, outside the hot IRQ text region. */
static bool32 receiveDiscovery(u32 word, u32 type, u32 payload)
{
    if (type == DISCOVER)
    {
        if (sRing.owner)
        {
            if (payload < 2 || payload > 8) return FALSE;
            sRing.count = payload;
            enqueue(packet(ANNOUNCE, 0, 0, payload));
        }
        else
        {
            if (payload >= 8 || sRing.inGame) return FALSE;
            sRing.head = sRing.tail = sRing.queued = sRing.seen = 0;
            sRing.sequence = sRing.count = 0;
            sRing.id = payload;
            enqueue(packet(DISCOVER, 0, 0, payload + 1));
        }
    }
    else
    {
        if (payload < 2 || payload > 8 || sRing.id >= payload) return FALSE;
        sRing.count = payload;
        sio2_sMPlayerId.mPlayerId = sRing.id;
        sio2_sMPlayerId.playerCount = payload;
        sio2_sMPlayerId.field_16 = 0x8001;
        if (!sRing.owner) enqueue(word);
        else sRing.complete = TRUE;
    }
    return TRUE;
}

static void RING_IRQ receive(u32 word)
{
    u32 type, node, seq, payload, i;
    /* Unarmed/open neighbours shift their idle level. No valid packet uses
     * either word; COMMIT replay recovers a transient missed shift. */
    if (!word || word == 0xFFFFFFFF)
        return;
    if ((u8)word != checksum(word))
    {
        sRing.rejectedWord = word;
        sRing.rejectionReason = 2;
        sRing.errors++;
        sRing.failed = TRUE;
        return;
    }
    type = word >> 28;
    node = (word >> 25) & 7;
    seq = (word >> 21) & 15;
    payload = (word >> 8) & 8191;
    switch (type)
    {
    case DISCOVER:
    case ANNOUNCE:
        if (!receiveDiscovery(word, type, payload)) goto invalid;
        break;
    case READY:
        /* Probes can be lost during graphics DMA; repeat the same sampled
         * input until all participants have entered this barrier. */
        if (sRing.rounds && seq == ((sRing.sequence + 15) & 15))
        {
            if (!sRing.owner && node != sRing.id) enqueue(word);
            break;
        }
        if (seq != sRing.sequence || !sRing.count || node >= sRing.count)
            break;
        if ((sRing.seen & (1 << node)) && sRing.staged[node] != payload)
            goto invalid;
        sRing.staged[node] = payload;
        sRing.seen |= 1 << node;
        /* A returning own READY proves that every node has forwarded this
         * immutable input. Its sender can stop repeating it while the last
         * renderer finishes; COMMIT replay still repairs missing vectors. */
        if (!sRing.frameStarted && node == sRing.id) sRing.ownReturned = TRUE;
        if (sRing.owner)
        {
            sRing.readySeen |= 1 << node;
        }
        if (node != sRing.id) enqueue(word);
        if (sRing.owner && sRing.requested && !sRing.frameStarted && sRing.ownReturned && sRing.readySeen == (1 << sRing.count) - 1)
            beginCommit();
        if (!sRing.owner && sRing.commitPending && sRing.seen == (1 << sRing.count) - 1)
        {
            word = packet(COMMIT, 0, sRing.sequence, 0);
            goto applyCommit;
        }
        break;
    case COMMIT:
        /* A completed peer forwards a replay without applying it twice.
         * This lets the owner recover a word missed during an IRQ/DMA. */
        if (sRing.rounds && seq == ((sRing.sequence + 15) & 15))
        {
            if (!sRing.owner) enqueue(word);
            break;
        }
        if (!sRing.count || seq != sRing.sequence || node || payload)
            goto invalid;
        /* A peer can miss an earlier READY during its last graphics DMA.
         * Hold COMMIT until repeated READY packets fill that same vector;
         * never apply a partial vector or disconnect an otherwise intact ring. */
        if (sRing.seen != (1 << sRing.count) - 1)
        {
            sRing.commitPending = TRUE;
            break;
        }
applyCommit:
        sRing.commitPending = FALSE;
        for (i = 0; i < 8; i++) sRing.committed[i] = sRing.staged[i];
        sRing.seen = sRing.readySeen = 0;
        sRing.rounds++;
        sRing.sequence = (sRing.sequence + 1) & 15;
        sRing.complete = TRUE;
        sRing.requested = sRing.frameStarted = FALSE;
        if (!sRing.owner)
            enqueue(word);
        break;
    case ABORT:
        sRing.failed = TRUE;
        if (!sRing.owner)
            enqueue(word);
        break;
    default:
        goto invalid;
    }
    return;
invalid:
    sRing.rejectedWord = word;
    sRing.rejectionReason = 3;
    sRing.errors++;
    sRing.failed = TRUE;
}

void irq_setHandler(int id, IrqHandler handler);

/* Recovery is rare; keep packet reconstruction outside the hot IWRAM
 * interrupt body while preserving the complete immutable replay vector. */
static void replayCommit(void)
{
    u32 i;
    for (i = 0; i < sRing.count; i++)
        enqueue(packet(READY, i, sRing.sequence, sRing.staged[i]));
    enqueue(packet(COMMIT, 0, sRing.sequence, 0));
}

/* Forward from serial completion IRQs, including while the main thread mixes
 * audio or draws a menu. An armed receiver must not depend on its polling rate. */
static void RING_IRQ ring_serialHandler(void)
{
    u16 control = RING_CONTROL | (sRing.owner ? RING_INTERNAL : 0);
    u32 word;
    if (!sRing.enabled || !sRing.inFlight) return;
    word = RING_DATA;
    sRing.inFlight = FALSE;
    sRing.lastShift = REG_TM2CNT_L;
    sRing.packets++;
    /* Idle polling clocks have no packet to decode. Avoid the decoder's
     * register saves while peers are still rendering or mixing audio. */
    if (word) receive(word);
    if (sRing.count && sRing.requested && !sRing.frameStarted && !sRing.ownReturned && !(sRing.packets & 15))
        enqueue(packet(READY, sRing.id, sRing.sequence, sRing.staged[sRing.id]));
    if (sRing.owner && sRing.frameStarted && !sRing.complete &&
        sRing.packets - sRing.commitPacket > sRing.count + 1 &&
        sRing.queued + sRing.count < 16 && !(sRing.packets & 15))
    {
        /* Replay the collected vector as well as COMMIT. Peers that already
         * completed this round forward these words without applying them. */
        replayCommit();
    }
    if (!sRing.owner && !sRing.failed)
    {
        REG_SIOCNT = control;
        RING_DATA = dequeue();
        sRing.inFlight = TRUE;
        REG_SIOCNT = control | RING_START;
    }
    else if (sRing.owner && !sRing.failed && !sRing.complete)
    {
        u32 delay = RING_TURNAROUND;
        /* Once our READY has circled, idle clocks only probe peers still
         * rendering. Avoid flooding their audio/physics with NOP IRQs; keep
         * full-rate forwarding whenever data or COMMIT is in circulation. */
        if (!word && !sRing.queued && sRing.count >= 5 && sRing.inGame &&
            sTickEpochStarted && sRing.ownReturned && !sRing.frameStarted)
            delay = RING_TURNAROUND * 8;
        REG_TM2CNT_H = 0;
        REG_TM2CNT_L = 65536 - delay;
        REG_TM2CNT_H = 0xC0;
    }
}

static void RING_IRQ poll(void)
{
    u16 control = RING_CONTROL | (sRing.owner ? RING_INTERNAL : 0);
    u16 ime = REG_IME;
    REG_IME = 0;
    if (sRing.inFlight || sRing.failed) goto done;
    if (sRing.owner)
    {
        if (sRing.complete) goto done;
        if (REG_TM2CNT_H & 0x80) goto done;
    }
    REG_SIOCNT = control;
    RING_DATA = dequeue();
    sRing.inFlight = TRUE;
    REG_SIOCNT = control | RING_START;
done:
    REG_IME = ime;
}

static void RING_IRQ ring_timerHandler(void)
{
    REG_TM2CNT_H = 0;
    poll();
}

static void waitForLinkInterrupt(void)
{
    /* Serial and VBlank IRQs wake HALT even with IME masked. Check and sleep
     * atomically so a COMMIT between the check and HALT cannot strand a peer.
     * The clock owner can sleep too: an active transfer completes through
     * SIO, and its forwarding turnaround is driven by the Timer 2 IRQ. */
    REG_IME = 0;
    if (sRing.enabled && !sRing.complete && !sRing.failed &&
        (sRing.inFlight || (sRing.owner && (REG_TM2CNT_H & 0x80))))
        *(vu8*)0x04000301 = 0;
    REG_IME = 1;
}

u16* ring_peerWords(Sio2State* nativeState)
{
    return sRing.enabled ? ring_sControlWords : (u16*)nativeState + 1;
}

/* Match the native action bit layout, including Up/Down and L's trigger. */
static inline u16 ring_translateInput(u16 held, u16 trigger)
{
    if (!(held | trigger)) return 0;
    return ((held & 0x30) << 4) | ((held & 0x40) << 5) | ((held & 0x80) << 3)
         | ((held & 2) << 13) | ((held & 1) << 15)
         | ((held & 256) >> 4) | ((held & 512) >> 3)
         | ((trigger & 256) >> 6) | ((trigger & 512) >> 2)
         | ((trigger & 4) << 11) | ((trigger & 8) << 9);
}

void ring_extendDriverInputs(void)
{
    extern u16 gKeyBuf[8], gKeyTriggerBuf[8];
    u32 i;
    if (sRing.enabled && !sTickEpochStarted)
    {
        /* Start the fixed video-rate tick budget at the native physics loop,
         * after loading/countdown setup. A missed presentation may catch up
         * inside VBlank, but cannot advance the whole race faster than video. */
        sTickEpochFrame = sVideoTick;
        sTickEpochRound = sRing.rounds;
        sTickEpochStarted = TRUE;
    }
#ifdef MKSC_RING_MUSIC_SHARED
    sMusicRacing = sRing.enabled && sRing.inGame;
#endif
    for (i = sRing.enabled ? 0 : 4; i < 8; i++)
        ring_sDriverInputs[i] = sRing.enabled && i < sRing.count
            ? ring_translateInput(gKeyBuf[i], gKeyTriggerBuf[i]) : 0;
}

void sio2_stockVblank(void);
void sio2_vblank(void)
{
    if (!sRing.enabled)
        sio2_stockVblank();
}

bool32 ring_active(void)
{
    return sRing.enabled;
}

u32 ring_renderFrame(void)
{
    /* Native remote texture budgets stay balanced when an input barrier
     * consumes extra video frames. Preserve the GP update budget. */
    return sRing.enabled ? sRing.rounds : gSceneState.field10;
}

void loadtrack_stockStartGrid(RaceState* state);
void sub_8031ED8(RaceState* state);
int sub_8032990(RaceState* state, int x, int y, int layer);
void loadtrack_8031F9C(RaceState* state)
{
    u32 i;

    /* Stock Vs. generates only two, three or four start positions. Tracks
     * already contain the eight-position Grand Prix grid. */
    if (sRing.enabled && sRing.count > 4 && (state->curRaceStateUnknown & 0x400))
        sub_8031ED8(state);
    else
        loadtrack_stockStartGrid(state);
    if (sRing.enabled && (state->curRaceStateUnknown & 0x800))
    {
        /* Battle records already contain eight distinct 0x81..0x88 markers.
         * The stock four-player generator ignores half of them. Use their
         * authored positions directly, independent of participant count. */
        extern int sub_8032BC8(int header);
        const u8* data = (const u8*)state->trackData;
        u32 offset = *(const u32*)(data + 0x98);
        const u8* marker = data + offset;
        s32 centerX = 0, centerY = 0;
        u32 seen = 0;
        if (!offset) { sRing.failed = TRUE; return; }
        while (marker[0])
        {
            u32 slot = marker[0] - 0x81;
            if (slot < 8)
            {
                s32* position = (s32*)((u8*)state + 0x24 + slot * 16);
                position[0] = (marker[1] * 8 + (sub_8032BC8(state->headerTableIdx) ? 0 : 4)) << 16;
                position[1] = (marker[2] * 8 + 4) << 16;
                position[3] = 0;
                seen |= 1 << slot;
            }
            marker += 4;
        }
        if (seen != 255) { sRing.failed = TRUE; return; }
        for (i = 0; i < 8; i++)
        {
            s32* position = (s32*)((u8*)state + 0x24 + i * 16);
            centerX += position[0] >> 16;
            centerY += position[1] >> 16;
        }
        centerX /= 8; centerY /= 8;
        for (i = 0; i < 8; i++)
        {
            s32* position = (s32*)((u8*)state + 0x24 + i * 16);
            s32 dx = centerX - (position[0] >> 16);
            s32 dy = centerY - (position[1] >> 16);
            /* Driver yaw zero faces north; the BIOS angle starts at east.
             * Looking inward fixes unused native directions that otherwise
             * read past the stock four-entry yaw table into cup numbers. */
            extern u16 ring_battleSpawnTerrain[8];
            position[2] = ArcTan2(-dy, dx);
            ring_battleSpawnTerrain[i] = sub_8032990(state, position[0] >> 16, position[1] >> 16, 0);
        }
    }
}

/* The native 32-cell sorted draw queue is too small for eight karts,
 * 24 balloons, identity markers and arena objects. Keep the hardware OAM
 * limit, but allow one queued cell per possible hardware sprite. The queue
 * belongs to the scene heap and is invalidated whenever that heap resets. */
void ring_resetSpriteHeap(FrameHeap* heap)
{
    extern OamCellListEntry* ring_sOamCells;
    if (heap == &gSceneState.raceState.frameHeap) ring_sOamCells = 0;
}

void ring_allocateSpriteQueue(void)
{
    extern OamCellListEntry* ring_sOamCells;
    if (!ring_sOamCells)
        ring_sOamCells = frmheap_calloc(&gSceneState.raceState.frameHeap, 96, sizeof(OamCellListEntry));
    if (!ring_sOamCells) sRing.failed = TRUE;
}

void ring_stop(void)
{
    if (!sRing.enabled)
        return;
    irq_updateMask(IRQ_UPDATE_MODE_AND, ~(IRQ_MASK_SIO | IRQ_MASK_TIMER2));
    REG_SIOCNT = 0;
    REG_IF = IRQ_MASK_SIO;
    irq_setHandler(IRQ_ID_SIO, main_dummyIrqHandler);
    irq_setHandler(IRQ_ID_TIMER2, main_dummyIrqHandler);
    REG_TM2CNT_H = 0;
    sRing.enabled = FALSE;
    sTickEpochStarted = FALSE;
    sio2_sMPlayerId.mPlayerId = -1;
    sio2_sMPlayerId.playerCount = 0;
    sio2_sMPlayerId.field_16 = 0;
}

void ring_initCode(void)
{
    extern u32 __ring_irq_start[], __ring_irq_end[], __ring_irq_load[];
    CpuCopy32(__ring_irq_load, __ring_irq_start, (u8*)__ring_irq_end - (u8*)__ring_irq_start);
}

static void start(bool32 owner)
{
    u32 i;
    u8* bytes = (u8*)&sRing;
    CpuFill32(0, ring_audioTiming, sizeof(ring_audioTiming));
    ring_audioDmaOverride = 0;
    ring_initCode();
    for (i = 0; i < sizeof(sRing); i++)
        bytes[i] = 0;
    irq_setHandler(IRQ_ID_SIO, ring_serialHandler);
    irq_setHandler(IRQ_ID_TIMER2, ring_timerHandler);
    REG_IF = IRQ_MASK_SIO | IRQ_MASK_TIMER2;
    sRing.enabled = TRUE;
    sRing.owner = owner;
    sRing.id = owner ? 0 : RING_INVALID;
    sRing.startVideoFrame = gSceneState.field14;
    REG_RCNT = 0;
    REG_TM2CNT_H = 0;
    REG_TM2CNT_L = 0;
    irq_updateMask(IRQ_UPDATE_MODE_OR, IRQ_MASK_SIO | IRQ_MASK_TIMER2 | IRQ_MASK_IME);
    if (owner)
        enqueue(packet(DISCOVER, 0, 0, 1));
}

bool32 ring_exchange(void)
{
    u32 started = ring_cycles();
    if (sRing.failed)
        return TRUE;
    if (sRing.count == 1)
    {
        u32 i;
        sRing.barrierWaiting = TRUE;
        sRing.rounds++;
        for (i = 0; i < 8; i++)
        {
            ring_sControlWords[i] = i ? 1023 : (REG_KEYINPUT & 1023) ^ (*(vu16*)0x03004F50 & 0x1C00);
            sio2_sKeyBuf[i] = ring_sControlWords[i] & 1023;
            if (i < 4) ((u16*)&sio2_sMPlayerId)[i + 1] = ring_sControlWords[i];
        }
        sRenderStart = ring_cycles();
        sRing.barrierWaiting = FALSE;
        return FALSE;
    }
    /* A reproducible snapshot may stop here without withholding an already
     * sampled input or suspending an active clock train. */
    sRing.barrierWaiting = TRUE;
    REG_IME = 0;
    sRing.barrierWaiting = FALSE;
    sRing.complete = FALSE;
    sRing.requested = TRUE;
    sRing.ownReturned = FALSE;
    sRing.staged[sRing.id] = (~REG_KEYINPUT & 1023) | (*(vu16*)0x03004F50 & 0x1C00);
    sRing.seen |= 1 << sRing.id;
    sRing.readySeen |= 1 << sRing.id;
    sRing.frameStarted = FALSE;
    sRing.startVideoFrame = gSceneState.field14;
    if (sRing.count)
        enqueue(packet(READY, sRing.id, sRing.sequence, sRing.staged[sRing.id]));
    REG_IME = 1;
    do
    {
        poll();
        ring_serviceAudio();
        if (gSceneState.field14 - sRing.startVideoFrame >= (sRing.frameStarted ? 30 : 180))
        {
            sRing.timeouts++;
            sRing.failed = TRUE;
        }
        waitForLinkInterrupt();
    } while (!sRing.complete && !sRing.failed);
    if (!sRing.failed)
    {
        u32 i;
        /* Publish only in the main thread. The COMMIT IRQ must rearm the
         * cable quickly; game input is consumed after this barrier anyway. */
        for (i = 0; i < 8; i++)
        {
            ring_sControlWords[i] = i < sRing.count ? sRing.committed[i] ^ 1023 : 1023;
            sio2_sKeyBuf[i] = ring_sControlWords[i] & 1023;
            if (i < 4) ((u16*)&sio2_sMPlayerId)[i + 1] = ring_sControlWords[i];
        }
    }
    sRenderStart = ring_cycles();
    ring_frameTiming[0] = sRenderStart - started;
    ring_frameTiming[2] = gSceneState.field14;
    return sRing.failed;
}

static const u16 sPlayerColors[8] = { 0x001F, 0x03E0, 0x03FF, 0x7C1F, 0x7C00, 0x021F, 0x7FE0, 0x5014 };

/* Virtual identity tiles are mapped to unused OBJ memory at publication. */
static const u16 sKartMarkerCells[8][4] = {
    {1,0x40F6,0x01F8,0x07E0},{1,0x40F6,0x01F8,0x17E1},
    {1,0x40F6,0x01F8,0x27E2},{1,0x40F6,0x01F8,0x37E3},
    {1,0x40F6,0x01F8,0x47E4},{1,0x40F6,0x01F8,0x57E5},
    {1,0x40F6,0x01F8,0x67E6},{1,0x40F6,0x01F8,0x77E7}
};
const u16* ring_playerMarker(unsigned id)
{
    return sRing.enabled && id < (sGame == GP_MODE ? 8 : sRing.count) ? sKartMarkerCells[id] : 0;
}
static const u16 sBalloonCells[8][4] = {
    {1,0x80D7,0x01FC,0x07E8},{1,0x80D7,0x01FC,0x17E9},
    {1,0x80D7,0x01FC,0x27EA},{1,0x80D7,0x01FC,0x37EB},
    {1,0x80D7,0x01FC,0x47EC},{1,0x80D7,0x01FC,0x57ED},
    {1,0x80D7,0x01FC,0x67EE},{1,0x80D7,0x01FC,0x77EF}
};
const u16* ring_balloonCell(Driver* driver)
{
    extern const u16 _0807B534[];
    unsigned id = driver->playerId;
    return sRing.enabled && id < 8 ? sBalloonCells[id] : _0807B534;
}
static const u16 sBalloonHudCells[8][4] = {
    {1,0x8000,0,0x03F0},{1,0x8000,0,0x13F1},
    {1,0x8000,0,0x23F2},{1,0x8000,0,0x33F3},
    {1,0x8000,0,0x43F4},{1,0x8000,0,0x53F5},
    {1,0x8000,0,0x63F6},{1,0x8000,0,0x73F7}
};
const u16* ring_balloonHudCell(unsigned id)
{
    extern const u16 _0807B9A8[];
    if (sRing.enabled && id < 4) id = sBattleHudIds[id];
    return sRing.enabled && id < 8 ? sBalloonHudCells[id] : _0807B9A8;
}
bool32 ring_showRaceIdentity(void)
{
    return sRing.enabled && sRing.inGame;
}
u32 ring_identityWanted(void)
{
    u32 players = (1 << (sGame == GP_MODE ? 8 : sRing.count)) - 1;
    return players | (sBattle ? players << 8 : 0) | players << 16;
}

void ring_stockBattleHud(RaceState* state);
void ring_stockVsHud(RaceState* state);
void ring_stockMapMarker(Driver* driver, int local);
int sub_8032D0C(RaceState* state);

void sub_804DCB0(RaceState* state)
{
    if (sRing.enabled)
    {
        u8 order[8];
        u32 i, j;
        for (i = 0; i < sRing.count; i++) order[i] = i;
        for (i = 1; i < sRing.count; i++)
        {
            u8 id = order[i];
            u32 balloons = ((u8*)state->drivers[id])[0x17D];
            j = i;
            while (j && ((u8*)state->drivers[order[j - 1]])[0x17D] < balloons)
            { order[j] = order[j - 1]; j--; }
            order[j] = id;
        }
        for (i = 0; i < 4; i++) sBattleHudIds[i] = order[i < sRing.count ? i : 0];
    }
    ring_stockBattleHud(state);
}

Driver* ring_battleHudDriver(RaceState* state, unsigned row)
{
    return state->drivers[sRing.enabled && row < 4 ? sBattleHudIds[row] : row];
}

unsigned ring_battleHudRow(unsigned id)
{
    unsigned row;
    if (!sRing.enabled) return id;
    for (row = 0; row < 4; row++) if (sBattleHudIds[row] == id) break;
    return row;
}

void sub_804F2C0(RaceState* state)
{
    /* The native GP path selects four portraits by current race position. */
    ring_stockVsHud(state);
}

void sub_804E7EC(Driver* driver, int local)
{
    if (!sRing.enabled)
        ring_stockMapMarker(driver, local);
    else
    {
        const u8* bytes = (const u8*)driver;
        u16 cell[4];
        Vec2s16 position;
        int scale = sub_8032D0C(&gSceneState.raceState) + 19;
        s32* xyz = (s32*)driver;
        u32 id = bytes[0xEC];
        if (id >= (sGame == GP_MODE ? 8 : sRing.count)) return;
        position.x = 168 + (xyz[0] >> scale);
        position.y = 88 + (xyz[1] >> scale);
        cell[0] = 1;
        /* Native 8x8 marker: offset 4 plus its central pixels at 3/4.
         * Our 2x2 dot needs offset 7 for the identical pixel center. */
        cell[1] = cell[2] = 7;
        cell[3] = (0x3F8 + id) | id << 12;
        oam_renderCellData(cell, &position, 0, 0, 0, 0);
    }
}
static const char* const sTracks[20] = {
    "PEACH CIRCUIT", "SHY GUY BEACH", "RIVERSIDE PARK", "BOWSER CASTLE 1",
    "MARIO CIRCUIT", "BOO LAKE", "CHEESE LAND", "BOWSER CASTLE 2",
    "LUIGI CIRCUIT", "SKY GARDEN", "CHEEP CHEEP ISLAND", "SUNSET WILDS",
    "SNOW LAND", "RIBBON ROAD", "YOSHI DESERT", "BOWSER CASTLE 3",
    "LAKESIDE PARK", "BROKEN PIER", "BOWSER CASTLE 4", "RAINBOW ROAD"
};

static const char* const sRetroTracks[20] = {
    "MARIO CIRCUIT 1", "DONUT PLAINS 1", "GHOST VALLEY 1", "BOWSER CASTLE 1",
    "MARIO CIRCUIT 2", "CHOCO ISLAND 1", "GHOST VALLEY 2", "DONUT PLAINS 2",
    "BOWSER CASTLE 2", "MARIO CIRCUIT 3", "KOOPA BEACH 1", "CHOCO ISLAND 2",
    "VANILLA LAKE 1", "BOWSER CASTLE 3", "MARIO CIRCUIT 4", "DONUT PLAINS 3",
    "KOOPA BEACH 2", "GHOST VALLEY 3", "VANILLA LAKE 2", "RAINBOW ROAD"
};

int spm_menuCharIdxToCharId(int menuIndex);
extern const u8 _080EE6FA[];

static unsigned glyph(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= '0' && c <= '9') return c - '0' + 26;
    if (c == '/') return 37;
    return 36;
}

static void resetMenuDisplay(void);

static void menuVblank(void)
{
    if (sMenuReady)
    {
        REG_DISPCNT ^= 0x10;
        sMenuReady = FALSE;
    }
}

static void menuPixel(int x, int y, u32 color)
{
    u32 at = y * 120 + x / 2;
    u32 shift = (x & 1) * 8;
    /* Bitmap VRAM does not support byte stores. */
    sMenuPixels[at] = (sMenuPixels[at] & ~(255 << shift)) | color << shift;
}

static u32 menuColor(u16 color)
{
    u32 i;
    for (i = 0; i < 8; i++) if (color == sPlayerColors[i]) return 16 + i;
    return color == 0x7FFF ? 10 : 11;
}

static void text(int x, int y, const char* string, u16 color)
{
    u32 index = menuColor(color);
    int row, col;
    while (*string)
    {
        const u8* bitmap = sFont[glyph(*string++)];
        for (row = 0; row < 7; row++)
            for (col = 0; col < 5; col++)
                if (bitmap[row] & (16 >> col))
                    menuPixel(x + col, y + row, index);
        ring_serviceAudio();
        x += 6;
    }
}

/* Draw the same sixteen-pixel portraits used by the native multiplayer HUD. */
static void portrait(int x, int y, int menuCharacter)
{
    extern const u8 _08062834[];
    extern const u16 _08062774[];
    u32 row, col;
    u32 character = spm_menuCharIdxToCharId(menuCharacter);
    u32 firstTile = 0x80 + _080EE6FA[character] * 4;
    for (row = 0; row < 16; row++)
        for (col = 0; col < 16; col++)
        {
            u32 tile = firstTile + (row / 8) * 2 + col / 8;
            u32 pixel = _08062834[tile * 32 + (row % 8) * 4 + (col % 8) / 2];
            pixel = (pixel >> ((col & 1) * 4)) & 15;
            if (pixel) menuPixel(x + col, y + row, 32 + pixel);
            if (col == 15) ring_serviceAudio();
        }
}

static void screen(const char* heading)
{
    sMenuReady = FALSE;
    sMenuPixels = (vu16*)((REG_DISPCNT & 0x10) ? 0x06000000 : 0x0600A000);
    DMA_FILL(3, 0x0808, (void*)sMenuPixels, 240 * 26, 16);
    DMA_FILL(3, 0x0909, (void*)(sMenuPixels + 120 * 26), 240 * (160 - 26), 16);
    text(12, 10, heading, 0x001F);
    text(6, 148, "A OK B BACK", 0x7FFF);
}

/* Sample the stock lettering already loaded by title_init. Keeping palette
 * indices preserves its outline, shading, and selected/unselected colors. */
static u32 titlePixel(const u16* cells, int x, int y)
{
    const vu8* tiles = (const vu8*)OBJ_VRAM0;
    static const u8 widths[3][4] = {{8,16,32,64},{16,32,32,64},{8,8,16,32}};
    static const u8 heights[3][4] = {{8,16,32,64},{8,8,16,32},{16,32,32,64}};
    u32 i;
    for (i = 0; i < cells[0]; i++)
    {
        const u16* cell = cells + 1 + i * 3;
        int left = cell[1] & 511;
        int top = cell[0] & 255;
        int shape = cell[0] >> 14;
        int size = cell[1] >> 14;
        int width = widths[shape][size];
        int height = heights[shape][size];
        int px, py;
        u32 tile, value;
        if (left >= 256) left -= 512;
        if (top >= 128) top -= 256;
        px = x - left; py = y - top;
        if (px < 0 || px >= width || py < 0 || py >= height) continue;
        tile = (cell[2] & 1023) + (py / 8) * (width / 8) + px / 8;
        value = tiles[tile * 32 + (py & 7) * 4 + (px & 7) / 2];
        return (value >> ((px & 1) * 4)) & 15;
    }
    return 0;
}

void ring_initTitleOption(void)
{
    extern const u16* off_80D8E4C[];
    /* R from PLAYER, ING from SINGLE, LINK from SINGLE-PAK LINK. */
    /* Include each glyph's outline, excluding the adjacent E/L columns.
     * The former crops picked up the preceding E beside R and the following
     * L beside ING, producing stray edges when the fragments were joined. */
    static const s16 slices[3][4] = { {0,32,6,3}, {0,-33,18,10}, {5,25,25,33} };
    u16 tiles[256];
    int i, row, col, x, offset;
    for (i = 0; i < 256; i++) tiles[i] = 0;
    for (i = 0; i < 3; i++)
        for (row = 0; row < 16; row++)
            for (col = 0; col < slices[i][2]; col++)
            {
                u32 pixel = titlePixel(off_80D8E4C[slices[i][0]], slices[i][1] + col, row - 8);
                x = slices[i][3] + col;
                offset = (x / 32) * 128 + (row / 8) * 64 + ((x % 32) / 8) * 16 + (row % 8) * 2 + (x % 8) / 4;
                tiles[offset] |= pixel << ((x % 4) * 4);
            }
    CpuCopy16(tiles, (void*)(OBJ_VRAM0 + 0x7E00), sizeof(tiles));
}

void ring_drawTitleOption(int y, bool32 selected)
{
    static const u16 cellsSelected[] = { 2, 0x40F8, 0x81E0, 0x93F0, 0x40F8, 0x8000, 0x93F8 };
    static const u16 cellsNormal[] = { 2, 0x40F8, 0x81E0, 0xA3F0, 0x40F8, 0x8000, 0xA3F8 };
    Vec2s16 position;
    position.x = 120;
    position.y = y;
    oam_renderCellData(selected ? cellsSelected : cellsNormal, &position, 0, 0, 0, 0);
}

void sub_8048C74(RaceState* state);
void sub_804AE58(RaceState* state, int character, int human);
void sub_8015268(int value);
void sub_801528C(int value);
void sub_80152F8(int value);
void sub_80152D4(int value);
u8 track_getBattleTrack(int arena);
u32 track_getRetroBattleTrack(int arena);
u8 track_getMKSCTrack(int track);
u8 track_getRetroTrack(int track);
bool32 race_main(void);
int spm_menuCharIdxToCharId(int menuIndex);

static void prepareRace(const u8* characters, int mode, int engine, int course, int retro)
{
    RaceState* state = &gSceneState.raceState;
    u32 i;
    for (i = 0; i < 8; i++)
    {
        sCharacters[i] = characters[i];
        sBattlePlaces[i] = 0xFF;
    }
    sEliminated = 0;
    sBalloonArtReady = FALSE;
    sGpBannersInitialized = FALSE;
#ifdef MKSC_RING_MUSIC_SHARED
    sMusicRacing = sSharedSong = sSharedFinalLap = sCourseMusicStarted = FALSE;
#endif
    sTickEpochStarted = FALSE;
    sGame = mode; sBattle = mode == BATTLE_MODE; sEngine = engine; sCourse = course; sRetro = retro;
    if (sBattle && (course < 0 || course > 3)) { sRing.failed = TRUE; return; }
    if (!sBattle && (course < 0 || course > 19)) { sRing.failed = TRUE; return; }
    scene_setVBlankFunc(0);
    sub_8048C74(state);
    *(vu8*)0x0300000C = 3;
    sub_8015268(0);
    sub_801528C(0);
    sub_80152F8(0);
    sub_80152D4(0);
    state->humanCount = sRing.count;
    state->localPlayerId = sRing.id;
    state->curRaceStateUnknown = sBattle ? 0x800 : mode == GP_MODE ? 0x100 : 0x400;
    state->engineClass = engine;
    for (i = 0; i < sRing.count; i++)
    {
        int character = spm_menuCharIdxToCharId(characters[i]);
        gSelectedCharacters[i] = character;
        sub_804AE58(state, character, 1);
    }
    if (mode == GP_MODE)
    {
        u32 used = 0, character;
        for (i = 0; i < sRing.count; i++) used |= 1 << sCharacters[i];
        for (i = sRing.count; i < 8; i++)
        {
            for (character = 0; used & (1 << character); character++);
            used |= 1 << character;
            sCharacters[i] = character;
            sub_804AE58(state, spm_menuCharIdxToCharId(character), 0);
        }
    }
    state->headerTableIdx = sBattle ? (retro ? track_getRetroBattleTrack(course) : track_getBattleTrack(course)) :
                           retro ? track_getRetroTrack(course) : track_getMKSCTrack(course);
    spm_setTrackSelection(course);
    sRing.inGame = TRUE;
    scene_setMainFunc(race_main);
}

typedef struct
{
    const u32* tiles;
    const u16* palette;
} NativeMenuLabel;
extern const NativeMenuLabel _080D8E9C[4], _080D8EBC[4];
typedef struct NativeMenuExtras
{
    u8 playerLabel[580];
    u8 countLabel[292];
    u16 playerPalette[16];
    u16 countPalette[16];
    u16 cursorTiles[256];
    u32 nativeMarkerTiles[128];
    u16 extendedCursorTiles[1024];
    bool32 extendedCursorReady;
    u8 randomCharacters;
    bool32 randomPortraitsDone;
} NativeMenuExtras;

static unsigned nativeLabelPixel(const u8* tiles, int width, int x, int y)
{
    int at = (y / 8 * (width / 8) + x / 8) * 32 + (y & 7) * 4 + (x & 7) / 2;
    return (tiles[at] >> ((x & 1) * 4)) & 15;
}

static void nativeLabelSetPixel(u8* tiles, int width, int x, int y, int color)
{
    int at = (y / 8 * (width / 8) + x / 8) * 32 + (y & 7) * 4 + (x & 7) / 2;
    int shift = (x & 1) * 4;
    tiles[at] = (tiles[at] & ~(15 << shift)) | color << shift;
}

/* New digits preserve the stock label's border, lettering and background.
 * Only the unavailable player/count numeral is replaced. Literal LZ blocks
 * let the original menu loaders consume the generated art without overrides. */
static void nativeLabelNumber(u8* tiles, int width, int number)
{
    u16 raster[18];
    u16* rows = raster + 2;
    int row, col;
    int player = width == 64;
    int left = player ? 39 : 7;
    int top = player ? 2 : 3;
    int height = player ? 13 : 10;
    int background = player ? 13 : 12;
    /* Sampling the old digit's neighbour painted bevel pixels across whole
     * rows. Clear only its rectangle, keeping the native border and kart. */
    for (row = player ? 1 : 2; row < (player ? 16 : 14); row++)
        for (col = player ? 38 : 6; col < (player ? 48 : 16); col++)
            nativeLabelSetPixel(tiles, width, col, row, background);
    /* Scale the glyph once. Neighbour tests then use row masks, avoiding
     * thousands of software divides while the menu audio queue is active. */
    for (row = 0; row < 18; row++) raster[row] = 0;
    for (row = 0; row < height; row++)
    {
        u32 bits = sFont[26 + number][row * 7 / height];
        for (col = 0; col < 8; col++)
            if (bits & (16 >> (col * 5 / 8))) rows[row] |= 1 << (col + 1);
    }
    /* Match the native numeral's dark outline, inner shadow and highlight. */
    for (row = -1; row <= height; row++)
    {
        u32 neighbours = rows[row - 1] | rows[row] | rows[row + 1];
        u32 outline = neighbours | neighbours << 1 | neighbours >> 1;
        u32 shadow = rows[row - 1] << 1;
        int ink = player ? 10 : 1 + row * 6 / height;
        for (col = -1; col <= 8; col++)
        {
            u32 pixel = 1 << (col + 1);
            if (left + col >= (player ? 48 : 16) || top + row >= 16) continue;
            if (outline & pixel) nativeLabelSetPixel(tiles, width, left + col, top + row, player ? 12 : 15);
            if (shadow & pixel) nativeLabelSetPixel(tiles, width, left + col, top + row, player ? 11 : 14);
            if (rows[row] & pixel) nativeLabelSetPixel(tiles, width, left + col, top + row, ink);
        }
    }
}

static void nativeTintPalette(const u16* source, u16* output, u16 color, int limit)
{
    int i;
    for (i = 0; i < 16; i++)
    {
        u32 r = source[i] & 31, g = source[i] >> 5 & 31, b = source[i] >> 10 & 31;
        u32 low = r < g ? r : g, high = r > g ? r : g;
        if (i >= limit) { output[i] = source[i]; continue; }
        if (b < low) low = b;
        if (b > high) high = b;
        r = low + (high - low) * (color & 31) / 31;
        g = low + (high - low) * (color >> 5 & 31) / 31;
        b = low + (high - low) * (color >> 10 & 31) / 31;
        output[i] = r | g << 5 | b << 10;
    }
}

static void nativeLabelCompress(const u8* tiles, int bytes, u8* output)
{
    int i;
    *(u32*)output = 0x10 | bytes << 8;
    output += 4;
    for (i = 0; i < bytes; i++)
    {
        if (!(i & 7)) *output++ = 0;
        *output++ = tiles[i];
    }
}

static void ring_initNativeLabels(SpmState* menu)
{
    u32 raw[128];
    sNativeMenuContext = menu;
    sNativeMenuExtras = frmheap_calloc(&gSceneState.raceState.frameHeap, 1, sizeof(NativeMenuExtras));
    if (sRing.id >= 4)
    {
        LZ77UnCompWram(_080D8E9C[3].tiles, raw);
        nativeLabelNumber((u8*)raw, 64, sRing.id + 1);
        nativeLabelCompress((u8*)raw, 512, sNativeMenuExtras->playerLabel);
        nativeTintPalette(_080D8E9C[3].palette, sNativeMenuExtras->playerPalette,
                          sPlayerColors[sRing.id], 16);
    }
    if (sRing.count > 4)
    {
        LZ77UnCompWram(_080D8EBC[3].tiles, raw);
        nativeLabelNumber((u8*)raw, 32, sRing.count);
        nativeLabelCompress((u8*)raw, 256, sNativeMenuExtras->countLabel);
        nativeTintPalette(_080D8EBC[3].palette, sNativeMenuExtras->countPalette,
                          sPlayerColors[sRing.count - 1], 10);
    }

}

/* Native pages intentionally share their state and graphics cache. Resetting
 * the heap or bitmap VRAM between them destroys the next page's inputs. */
static SpmState* ring_getNativeMenu(void)
{
    FrameHeap* heap;
    SpmState* menu;
    if (sNativeMenuContext) return sNativeMenuContext;
    heap = &gSceneState.raceState.frameHeap;
    frmheap_init(heap, (void*)0x0202A400, 0x8000);
    menu = frmheap_calloc(heap, 1, sizeof(SpmState));
    spm_setState(menu);
    ring_initNativeLabels(menu);
    return menu;
}

const u32* ring_nativePlayerLabel(const u32* original)
{
    if (!sNativeMenuContext) return original;
    return sRing.id < 4 ? _080D8E9C[sRing.id].tiles : (const u32*)sNativeMenuExtras->playerLabel;
}

const u16* ring_nativePlayerPalette(const u16* original)
{
    if (!sNativeMenuContext) return original;
    return sRing.id < 4 ? _080D8E9C[sRing.id].palette : sNativeMenuExtras->playerPalette;
}

const u32* ring_nativeCountLabel(const u32* original)
{
    if (!sNativeMenuContext) return original;
    return sRing.count <= 4 ? _080D8EBC[sRing.count - 1].tiles : (const u32*)sNativeMenuExtras->countLabel;
}

const u16* ring_nativeCountPalette(const u16* original)
{
    if (!sNativeMenuContext) return original;
    return sRing.count <= 4 ? _080D8EBC[sRing.count - 1].palette : sNativeMenuExtras->countPalette;
}

typedef struct
{
    s16 x, y;
    s8 up, down, left, right;
} NativeCharacterNavigation;
extern const NativeCharacterNavigation _080D9274[9];

void ring_restoreNativeMenuKeys(void)
{
    extern u16 gKeyBuf[8], gKeyTriggerBuf[8], gKeyRepeatTriggerBuf[8];
    if (!sNativeHostKeysSaved) return;
    gKeyBuf[0] = sNativeHostKeys[0];
    gKeyTriggerBuf[0] = sNativeHostKeys[1];
    gKeyRepeatTriggerBuf[0] = sNativeHostKeys[2];
    sNativeHostKeysSaved = FALSE;
}


/* Every console retains the native local animation slot. The ring roster
 * extends confirmation/duplicate checks to eight players, while the original
 * chooser owns selection voices, portrait fades, roulette and the host OK. */
typedef struct
{
    u8 flags, dirty;
    u16 animation, frame, selectedStyle;
    u8 remaining[12];
} NativeCharacterPortrait;

void ring_switchNativeBattlePage(SpmState* menu);

int ring_nativeCharactersPending(int pending)
{
    if (!sNativeCharacterMenu) return pending;
    return sNativeCharactersReady != (1 << sRing.count) - 1;
}

int ring_nativeRandomCharacter(int original)
{
    return sNativeCharacterMenu ? sNativeCharacterChoices[sRing.id] : original;
}

static void ring_selectRandomCharacters(void)
{
    u32 i, j, used = 0;
    sNativeMenuExtras->randomCharacters = 0;
    for (i = 0; i < sRing.count; i++)
        if (sNativeCharacterChoices[i] != 4) used |= 1 << sNativeCharacterChoices[i];
    for (i = 0; i < sRing.count; i++)
        if (sNativeCharacterChoices[i] == 4)
        {
            u32 first = (sRing.rounds + i * 3) & 7;
            for (j = 0; j < 8; j++)
            {
                u32 candidate = (first + j) & 7;
                candidate += candidate >= 4;
                if (!(used & (1 << candidate)))
                {
                    sNativeCharacterChoices[i] = candidate;
                    used |= 1 << candidate;
                    break;
                }
            }
            sNativeMenuExtras->randomCharacters |= 1 << i;
        }
}

static void ring_animateRemoteConfirmation(unsigned id, bool32 selected)
{
    extern const u16 _080D92D6[9];
    NativeCharacterPortrait* portraits = (NativeCharacterPortrait*)((u8*)sNativeCharacterMenu + 0x5E4);
    u32 choice = sNativeCharacterChoices[id];
    if (choice == 4 || id == sRing.id) return;
    portraits[choice].dirty = 1;
    portraits[choice].animation = selected ? 1 : 2;
    portraits[choice].selectedStyle = selected ? 1 : 0;
#ifndef MKSC_RING_MUSIC_SHARED
    m4aSongNumStart(selected ? _080D92D6[choice] : 0x90);
#endif
    /* Shared mixing gets this voice from the selecting console's native
     * local animation, rather than duplicating it on every remote screen. */
}

void ring_nativeMenuFrame(void)
{
    extern u16 gKeyBuf[8], gKeyTriggerBuf[8], gKeyRepeatTriggerBuf[8];
    u16 held, localTrigger;
    u32 i, j, readyBefore, allReady;
    int phase;
    if (!sNativeCharacterMenu && !sNativeCourseMenu) return;
    sNativeHostKeys[0] = gKeyBuf[0];
    sNativeHostKeys[1] = gKeyTriggerBuf[0];
    sNativeHostKeys[2] = gKeyRepeatTriggerBuf[0];
    sNativeHostKeysSaved = TRUE;
    if (sNativeCourseMenu)
    {
        if (sNativeCourseMenu->gamemode == GAMEMODE_BATTLE &&
            (gKeyTriggerBuf[0] & (L_BUTTON | R_BUTTON)))
        {
            ring_switchNativeBattlePage(sNativeCourseMenu);
            gKeyTriggerBuf[0] &= ~(L_BUTTON | R_BUTTON);
            gKeyRepeatTriggerBuf[0] &= ~(L_BUTTON | R_BUTTON);
        }
        return;
    }
    held = gKeyBuf[sRing.id];
    localTrigger = gKeyTriggerBuf[sRing.id];
    readyBefore = sNativeCharactersReady;
    allReady = (1 << sRing.count) - 1;
    phase = sNativeCharacterMenu->characterPhases[0];
    if (!sNativeCharacterConfirm && !sNativeCharacterBack)
    {
        for (i = 0; i < sRing.count; i++)
        {
            u16 input = gKeyTriggerBuf[i];
            const NativeCharacterNavigation* choice = &_080D9274[sNativeCharacterChoices[i]];
            if (input & B_BUTTON)
            {
                if (!i && !(sNativeCharactersReady & 1)) sNativeCharacterBack = TRUE;
                if (sNativeCharactersReady & (1 << i)) ring_animateRemoteConfirmation(i, FALSE);
                sNativeCharactersReady &= ~(1 << i);
            }
            if (!(sNativeCharactersReady & (1 << i)))
            {
                u16 direction = gKeyRepeatTriggerBuf[i];
                int next = direction & DPAD_UP ? choice->up :
                           direction & DPAD_DOWN ? choice->down :
                           direction & DPAD_LEFT ? choice->left :
                           direction & DPAD_RIGHT ? choice->right : sNativeCharacterChoices[i];
                if (next >= 0) sNativeCharacterChoices[i] = next;
                if (input & (A_BUTTON | START_BUTTON))
                {
                    /* Multiple players may reserve Random. Resolve the
                     * common permutation only after the host accepts OK. */
                    for (j = 0; j < sRing.count; j++)
                        if (next != 4 && j != i && (sNativeCharactersReady & (1 << j)) &&
                            sNativeCharacterChoices[j] == next) break;
                    if (j == sRing.count)
                    {
                        sNativeCharactersReady |= 1 << i;
                        ring_animateRemoteConfirmation(i, TRUE);
                    }
                }
            }
        }
        if (readyBefore == allReady && sNativeCharactersReady == allReady &&
            (gKeyTriggerBuf[0] & (A_BUTTON | START_BUTTON)))
        {
            ring_selectRandomCharacters();
            sNativeCharacterConfirm = TRUE;
        }
    }
    if (phase == 2)
    {
        int choice = sNativeCharacterChoices[sRing.id];
        if (sNativeCharacterMenu->characterCells[0] != choice)
        {
            extern const u8* _080D93D0[9];
            int character = choice - (choice > 4);
            sNativeCharacterMenu->characterCells[0] = choice;
            sNativeCharacterMenu->portraitChangeState = 2;
            sNativeCharacterMenu->portraitPaletteChangeState = 2;
            LZ77UnCompWram((const u32*)_080D93D0[character], (void*)0x02014000);
            sNativeCharacterMenu->nameTilesDirty = 1;
            m4aSongNumStart(0x8D);
        }
    }
    if (phase >= 7 && sNativeCharacterConfirm && !sNativeMenuExtras->randomPortraitsDone)
    {
        for (i = 0; i < sRing.count; i++)
            if (sNativeMenuExtras->randomCharacters & (1 << i)) ring_animateRemoteConfirmation(i, TRUE);
        sNativeMenuExtras->randomPortraitsDone = TRUE;
    }
    gKeyBuf[0] = held & ~(A_BUTTON | B_BUTTON | START_BUTTON);
    gKeyTriggerBuf[0] = localTrigger & (L_BUTTON | R_BUTTON | SELECT_BUTTON);
    gKeyRepeatTriggerBuf[0] = 0;
    if (phase == 2 && (sNativeCharactersReady & (1 << sRing.id))) gKeyTriggerBuf[0] |= A_BUTTON;
    if ((phase == 3 || phase == 4) && (readyBefore & (1 << sRing.id)) &&
        !(sNativeCharactersReady & (1 << sRing.id))) gKeyTriggerBuf[0] |= B_BUTTON;
    if (phase == 4 && sNativeCharacterConfirm) gKeyTriggerBuf[0] |= A_BUTTON;
    if (sNativeCharacterBack)
    {
        sNativeCharacterMenu->characterPhases[0] = 2;
        gKeyTriggerBuf[0] = B_BUTTON;
    }
}

static unsigned ring_animatedNativeCharacter(unsigned id)
{
    extern const u8 _080D947C[9];
    unsigned choice = sNativeCharacterChoices[id];
    if (sNativeCharacterMenu->characterPhases[0] == 6 &&
        (sNativeMenuExtras->randomCharacters & (1 << id)))
    {
        /* Each console runs the original local roulette slot. Use its
         * native step counter to animate the other confirmed markers too. */
        unsigned steps = *(u32*)((u8*)sNativeCharacterMenu + 0x6C0);
        if (steps < 25) while (steps--) choice = _080D947C[choice];
    }
    return choice;
}

/* Native 4-bpp sprites never display palette-zero entries. Reserve those
 * entries for the extra 8-bpp cursors instead of borrowing animated portrait
 * colors. Each triple preserves the native highlight/body/shadow contrast. */
#define CURSOR_RGB(r,g,b) ((r) | ((g) << 5) | ((b) << 10))
static const u16 sExtraCursorPalette[14] = {
    CURSOR_RGB(20,20,31),CURSOR_RGB(8,8,27),CURSOR_RGB(2,2,16),
    CURSOR_RGB(31,25,20),CURSOR_RGB(27,17,8),CURSOR_RGB(16,9,2),
    CURSOR_RGB(20,31,31),CURSOR_RGB(8,27,27),CURSOR_RGB(2,16,16),
    CURSOR_RGB(27,20,27),CURSOR_RGB(20,8,20),CURSOR_RGB(11,2,11),
    0x7FFF,0
};
#undef CURSOR_RGB
void ring_publishNativeMenuPalette(void)
{
    unsigned color;
    if (!sRing.enabled || sRing.count <= 4 || !sNativeCharacterMenu || !sNativeMenuExtras->extendedCursorReady) return;
    for (color = 0; color < 14; color++) ((vu16*)OBJ_PLTT)[(color + 1) * 16] = sExtraCursorPalette[color];
    pltt_setDirtyFlag(TRUE);
}

/* Reuse the original multipak cursor frames and confirmed-player badges.
 * The native chooser still owns only one safe local animation slot. */
bool32 ring_drawNativeCharacterCursor(SpmState* menu, int slot, int x, int y)
{
    extern const u16* _080D93A8[4];
    extern const u16* _080D93B8[4];
    static const s8 offsets[8][2] = {
        {-12,12},{12,12},{-12,-12},{12,-12},
        {0,-16},{16,0},{0,16},{-16,0}
    };
    u32 i;
    Vec2s16 position;
    u16 cells[13];
    if (menu != sNativeCharacterMenu) return FALSE;
    if (sRing.count > 4 && !sNativeMenuExtras->extendedCursorReady)
    {
        static const u8 colors[16] = {0,208,32,224,16,32,48,64,80,96,112,128,144,160,176,192};
        u8 lookup[16];
        u8* output = (u8*)sNativeMenuExtras->extendedCursorTiles;
        u32 pixel;
        for (pixel = 0; pixel < 16; pixel++) lookup[pixel] = colors[pixel];
        lookup[2] = 32 + (sRing.id & 3) * 48;
        for (pixel = 0; pixel < 2048; pixel++)
        {
            const u8* source;
            u32 offset;
            if (pixel < 1024)
            {
                source = (const u8*)OBJ_VRAM0;
                offset = (_080D93A8[sRing.id & 3][3 + pixel / 256 * 3] & 1023) * 64 + pixel % 256;
            }
            else
            {
                source = (const u8*)sNativeMenuExtras->cursorTiles;
                offset = pixel - 1024;
            }
            output[pixel] = lookup[(source[offset / 2] >> ((offset & 1) * 4)) & 15];
        }
        CpuCopy16(output, (void*)0x06017800, sizeof(sNativeMenuExtras->extendedCursorTiles));
        sNativeMenuExtras->extendedCursorReady = TRUE;
    }
    if (slot) return TRUE;
    position.x = x; position.y = y;
    for (i = 0; i < 13; i++) cells[i] = _080D93A8[sRing.id & 3][i];
    if (sRing.id >= 4)
        for (i = 0; i < 4; i++)
        {
            cells[1 + i * 3] |= 0x2000;
            cells[3 + i * 3] = 0x3C0 + i * 8;
        }
    oam_renderCellData(cells, &position, 0, 0, 0, 0);
    for (i = 0; i < sRing.count; i++)
    {
        const NativeCharacterNavigation* choice;
        if (i == sRing.id || !(sNativeCharactersReady & (1 << i))) continue;
        choice = &_080D9274[ring_animatedNativeCharacter(i)];
        position.x = choice->x + offsets[i][0];
        position.y = choice->y + offsets[i][1];
        if (i < 4) oam_renderCellData(_080D93B8[i], &position, 0, 0, 0, 0);
        else
        {
            u16 badge[4];
            badge[0] = 1; badge[1] = 0x20F8; badge[2] = 0x41F8;
            badge[3] = 0x3E0 + (i - 4) * 8;
            oam_renderCellData(badge, &position, 0, 0, 0, 0);
        }
    }
    return TRUE;
}

static int ring_chooseNativeCharacters(u8* characters, int mode, int engine)
{
    extern bool32 spm_chooseCharacter(SpmState*, int resume, int allowBack);
    extern void sub_8001DDC(void);
    extern void sub_800C680(void);
    u16* tiles;
    u32 i, row, col;
    int accepted;
    SpmState* menu;
    bool32 fresh = !sNativeMenuContext;
    menu = ring_getNativeMenu();
    tiles = sNativeMenuExtras->cursorTiles;
    menu->menuPage = MENU_PAGE_CHARACTER;
    menu->lastPlayer = 0;
    menu->localPlayerId = 0;
    menu->playerCount = sRing.count;
    menu->gamemode = mode == GP_MODE ? GAMEMODE_GP : mode == BATTLE_MODE ? GAMEMODE_BATTLE : GAMEMODE_VS;
    menu->raceSpeed = mode == BATTLE_MODE ? RACE_SPEED_UNSET : engine;
    menu->character = characters[sRing.id];
    menu->cup = menu->track = -1;
    menu->reenteringLinkMenu = 1;
    menu->suppressConnectionCheck = 1;
    for (i = 0; i < 8; i++)
        sNativeCharacterChoices[i] = characters[i] + (characters[i] >= 4);
    {
        extern const u32 _080B042C[];
        u32* native = sNativeMenuExtras->nativeMarkerTiles;
        LZ77UnCompWram(_080B042C, native);
        for (i = 0; i < 4; i++)
        {
            u8* marker = (u8*)tiles + i * 128;
            CpuCopy16(native, marker, 128);
            for (row = 3; row < 13; row++)
                for (col = 4; col < 12; col++)
                    nativeLabelSetPixel(marker, 16, col, row, 4);
            for (row = 0; row < 7; row++)
                for (col = 0; col < 5; col++)
                    if (sFont[31 + i][row] & (16 >> col))
                    {
                        nativeLabelSetPixel(marker, 16, 6 + col, 5 + row, 6);
                        nativeLabelSetPixel(marker, 16, 5 + col, 4 + row, 1);
                    }
            for (row = 0; row < 16; row++)
                for (col = 0; col < 16; col++)
                {
                    u32 color = nativeLabelPixel(marker, 16, col, row);
                    if (i && color >= 4 && color <= 6) color += i * 3;
                    if (i && color == 2) color = 4 + i * 3;
                    nativeLabelSetPixel(marker, 16, col, row, color);
                }
        }
    }
    sNativeMenuExtras->extendedCursorReady = FALSE;
    sNativeMenuExtras->randomCharacters = 0;
    sNativeMenuExtras->randomPortraitsDone = FALSE;
    *(u8*)0x03000019 = sNativeCharacterChoices[sRing.id];
    sNativeCharactersReady = sNativeCharacterConfirm = sNativeCharacterBack = 0;
    sNativeCharacterMenu = menu;
    scene_setVBlankFunc(sub_8001DDC);
    vcount_reset();
    vcount_register(79, sub_800C680);
    irq_updateMask(IRQ_UPDATE_MODE_OR, IRQ_MASK_VCOUNT);
    oam_init();
    trns_init();
    main_setKeyRepeat(20, 5);
    accepted = spm_chooseCharacter(menu, fresh ? 1 : 0, 1);
    sNativeCharacterMenu = 0;
    for (i = 0; i < 8; i++)
        characters[i] = sNativeCharacterChoices[i] - (sNativeCharacterChoices[i] > 4);
    return accepted;
}

/* The stock engine page owns its original art, animations and selection
 * logic. Its local presentation context never contains ring player arrays. */
static int ring_chooseNativeMode(int* mode, int* engine, bool32 chooseMode)
{
    extern bool32 spm_chooseModeAndClass(SpmState*, int resume);
    extern void sub_8001DDC(void);
    extern void sub_800C680(void);
    SpmState* menu;
    bool32 fresh = !sNativeMenuContext;
    menu = ring_getNativeMenu();
    menu->menuPage = MENU_PAGE_MODE;
    /* Class Back can enter game selection inside the same stock routine.
     * Keep its linked two-player widget context even on class-only entry. */
    menu->lastPlayer = 1;
    menu->playerCount = sRing.count;
    menu->localPlayerId = 0;
    menu->selectedPlayerLimit = 0;
    menu->gamemode = *mode == GP_MODE ? GAMEMODE_GP : *mode == BATTLE_MODE ? GAMEMODE_BATTLE : GAMEMODE_VS;
    menu->raceSpeed = *engine;
    menu->character = -1;
    menu->cup = -1;
    menu->track = -1;
    menu->reenteringLinkMenu = 1;
    menu->bgState.DISPCNT = 0;
    menu->suppressConnectionCheck = 1;
    scene_setVBlankFunc(sub_8001DDC);
    vcount_reset();
    vcount_register(79, sub_800C680);
    irq_updateMask(IRQ_UPDATE_MODE_OR, IRQ_MASK_VCOUNT);
    oam_init();
    trns_init();
    main_setKeyRepeat(20, 5);
    ((u8*)0x03000010)[0x17] = 1;
    if (chooseMode || fresh)
    {
        /* Stock two-pak mode already supplies GP, Vs. and Battle widgets.
         * Keep its two-slot presentation separate from ring participants. */
        ((u8*)0x03000010)[1] = *mode == GP_MODE ? 0 : *mode == BATTLE_MODE ? 2 : 1;
    }
    /* Initialize the native mode page before using its class-page resume
     * path when this context has just been allocated. */
    if (!spm_chooseModeAndClass(menu, chooseMode || fresh ? 0 : 1))
    {
            return FALSE;
    }
    *mode = menu->gamemode == GAMEMODE_GP ? GP_MODE : menu->gamemode == GAMEMODE_BATTLE ? BATTLE_MODE : VS_MODE;
    *engine = *mode == BATTLE_MODE ? 0 : menu->raceSpeed;
    return TRUE;
}

/* Resolve the existing Battle thumbnail/palette widgets against the active
 * set. Outside the ring course page this remains the stock GBA lookup. */
u32 ring_nativeBattleTrack(int arena)
{
    extern const u32 gBattleCup[4];
    if (sNativeCourseMenu && sNativeCourseMenu->gamemode == GAMEMODE_BATTLE &&
        sNativeCourseMenu->trackPage) return track_getRetroBattleTrack(arena);
    return gBattleCup[arena];
}

void ring_switchNativeBattlePage(SpmState* menu)
{
    volatile u32* previewPhase = (volatile u32*)0x0203EBFC;
    if (menu != sNativeCourseMenu || menu->gamemode != GAMEMODE_BATTLE) return;
    menu->trackPage ^= 1;
    m4aSongNumStart(0x93);
    /* Schedule the same two-phase preview reload as a native cursor move.
     * State 2 loads graphics, then state 1 publishes graphics and palette.
     * Calling the loader while idle (state 0) does not reload anything. */
    *previewPhase = 2;
}

bool32 ring_unlockNativeMenu(SpmState* menu)
{
    u32 i;
    if (menu != sNativeCourseMenu) return FALSE;
    /* Native tables hold lock bits, rather than availability bits. Ring
     * play exposes all twenty courses in either set, independent of saves. */
    for (i = 0; i < 34; i++) menu->unlockedTracksTable[i] = 0;
    menu->canSelectRetro = 1;
    return TRUE;
}

static int ring_chooseNativeCourse(int mode, int engine, int character,
                                  int* course, int* retro)
{
    extern bool32 spm_chooseCourse(SpmState*, int resume);
    extern void sub_8001DDC(void);
    extern void sub_800C680(void);
    extern void sub_8002E08(SpmState*, int cup);
    extern void sub_8002E50(SpmState*, int track);
    extern void sub_8006E08(SpmState*, int reloadBackground);
    SpmState* menu;
    int accepted;
    bool32 fresh = !sNativeMenuContext;
    menu = ring_getNativeMenu();
    menu->menuPage = MENU_PAGE_TRACK;
    menu->lastPlayer = menu->localPlayerId = 0;
    menu->playerCount = sRing.count;
    menu->gamemode = mode == GP_MODE ? GAMEMODE_GP : mode == BATTLE_MODE ? GAMEMODE_BATTLE : GAMEMODE_VS;
    menu->raceSpeed = mode == BATTLE_MODE ? RACE_SPEED_UNSET : engine;
    menu->character = character;
    menu->cup = mode == BATTLE_MODE ? 0 : *course / 4;
    menu->track = mode == BATTLE_MODE ? *course : *course % 4;
    menu->trackPage = *retro;
    menu->reenteringLinkMenu = 1;
    menu->suppressConnectionCheck = 1;
    sub_8002E08(menu, menu->cup);
    sub_8002E50(menu, menu->track);
    sNativeCourseMenu = menu;
    ring_unlockNativeMenu(menu);
    scene_setVBlankFunc(sub_8001DDC);
    vcount_reset();
    vcount_register(79, sub_800C680);
    irq_updateMask(IRQ_UPDATE_MODE_OR, IRQ_MASK_VCOUNT);
    oam_init();
    trns_init();
    main_setKeyRepeat(20, 5);
    if (fresh) sub_8006E08(menu, 1);
    accepted = spm_chooseCourse(menu, fresh ? 1 : 0);
    if (accepted)
    {
        *course = mode == BATTLE_MODE ? menu->track : menu->cup * 4 + menu->track;
        *retro = menu->trackPage;
    }
    sNativeCourseMenu = 0;
    return accepted;
}

bool32 ring_main(void)
{
    int phase = sResume ? sResume : GAME;
    int mode = sResume ? sGame : VS_MODE, engine = sResume ? sEngine : 0;
    int cup = sResume ? sCourse / 4 : 0, course = sResume ? sCourse : 0;
    int retro = sResume ? sRetro : 0;
    u8 characters[8], ready = 0, ignoreStart = !sResume;
    int drawnPhase = -1;
    u16 previous = 0, keys, trigger;
#ifdef MKSC_RING_MUSIC_SHARED
    bool32 discoveryMusicStopped = FALSE;
#endif
    u32 i;
    extern u16 gKeyTriggerBuf[8], gKeyBuf[8];
    sNativeMenuContext = sNativeCourseMenu = sNativeCharacterMenu = 0;
    if (sResume && ring_active())
    {
        pltt_getBuffer(PLTT_BUFFER_BG)[0] = 0;
        ((vu16*)PLTT)[0] = 0;
        REG_DISPCNT = 0;
        scene_setVBlankFunc(0);
        DmaStop(0);
        vcount_reset();
        oam_init();
        trns_init();
        goto connected;
    }
    resetMenuDisplay();
    sSession = TRUE;
    for (i = 0; i < 8; i++) sRoundScores[i] = 0;
    screen("RING LINK");
    text(12, 42, "CONNECT ALL GBAS IN A RING", 0x7FFF);
    text(12, 60, "PRESS START ON ONE CONSOLE", 0x03FF);
    text(12, 78, "WAITING FOR OTHER PLAYERS", 0x7FFF);
    sMenuReady = TRUE;
    /* Discovery polls without the normal frame loop. Continue servicing
     * MP2000 every VBlank so the last title sample cannot loop indefinitely. */
    m4aMPlayAllStop();
    sLastAudioFrame = gSceneState.field14 - 1;
    start(FALSE);
    m4aSongNumStart(SONG_LINK);
    while (!sRing.count && !sRing.failed)
    {
        ring_serviceAudio();
#ifdef MKSC_RING_MUSIC_SHARED
        /* Before discovery, each standalone console has its own menu music.
         * Once a peer learns its software ID, the chosen host owns the BGM.
         * Stop it here rather than touching the mixer from the serial IRQ. */
        if (!discoveryMusicStopped && sRing.id != RING_INVALID && sRing.id != 0)
        {
            m4aMPlayStop(gMPlayTable[0].info);
            discoveryMusicStopped = TRUE;
        }
#endif
        keys = ~REG_KEYINPUT & 1023;
        trigger = keys & ~previous;
        previous = keys;
        if (trigger & B_BUTTON) goto cancelled;
        if ((trigger & START_BUTTON) && sRing.id == RING_INVALID)
        {
            REG_SIOCNT = RING_CONTROL;
            sRing.inFlight = FALSE;
            sRing.owner = TRUE;
            REG_TM2CNT_H = 0;
            sRing.id = 0;
            sRing.startVideoFrame = gSceneState.field14;
            sRing.lastShift = REG_TM2CNT_L;
            enqueue(packet(DISCOVER, 0, 0, 1));
        }
        poll();
        if (gSceneState.field14 - sRing.startVideoFrame > 180 && sRing.owner)
            sRing.failed = TRUE;
        waitForLinkInterrupt();
    }
    if (sRing.failed) goto lost;
connected:
#ifdef MKSC_RING_MUSIC_SHARED
    /* ANNOUNCE may finish discovery before the loop's next iteration. */
    if (sRing.id != 0) m4aMPlayStop(gMPlayTable[0].info);
#endif
    m4aSongNumStart(SONG_MENU);
    for (i = 0; i < 8; i++) characters[i] = sResume ? sCharacters[i] : i;
    sResume = 0;
    sRing.inGame = FALSE;
    while (1)
    {
        main_frameProc();
        if (!ring_active()) goto lost;
        trigger = gKeyTriggerBuf[0];
        if (drawnPhase == phase)
        {
            for (i = 0; i < sRing.count; i++) if (gKeyTriggerBuf[i]) break;
            if (i == sRing.count) continue;
        }
        drawnPhase = phase;
        /* Starting discovery must not also confirm the first game choice. */
        if (ignoreStart)
        {
            trigger &= ~START_BUTTON;
            if (!(gKeyBuf[0] & START_BUTTON)) ignoreStart = FALSE;
        }
        if (phase != CHARACTERS && (trigger & B_BUTTON))
        {
            if (phase == GAME) goto cancelled;
            if (phase == ENGINE)
            {
                phase = GAME;
            }
            else if (phase == COURSE) phase = CUP;
            else { phase = CHARACTERS; ready = 0; }
            /* Consume Back once. CUP -> CHARACTERS must not process the same
             * B edge a second time in the character handler below. */
            continue;
        }
        switch (phase)
        {
        case GAME:
            {
            int previousMode = mode;
            if (!ring_chooseNativeMode(&mode, &engine, TRUE)) goto cancelled;
            if (mode != previousMode) course = cup = retro = 0;
            phase = CHARACTERS;
            drawnPhase = -1;
            }
            break;
        case ENGINE:
            {
            int previousMode = mode;
            if (ring_chooseNativeMode(&mode, &engine, FALSE))
            {
                if (mode != previousMode) course = cup = retro = 0;
                phase = CHARACTERS;
            }
            else phase = GAME;
            drawnPhase = -1;
            }
            break;
        case CHARACTERS:
            if (ring_chooseNativeCharacters(characters, mode, engine))
                phase = mode == BATTLE_MODE ? ARENA : CUP;
            else phase = mode == BATTLE_MODE ? GAME : ENGINE;
            drawnPhase = -1;
            break;
        case CUP:
        case COURSE:
        case ARENA:
            if (mode == BATTLE_MODE && (course < 0 || course > 3)) course = 0;
            if (ring_chooseNativeCourse(mode, engine, characters[sRing.id], &course, &retro))
            {
                if (mode == GP_MODE)
                {
                    course = course / 4 * 4;
                    sGPRace = sGPCupComplete = 0;
                    for (i = 0; i < 8; i++) sGPPoints[i] = 0;
                }
                goto launch;
            }
            phase = CHARACTERS;
            drawnPhase = -1;
            break;
        }
        sMenuReady = TRUE;
    }
launch:
    sNativeMenuContext = sNativeCourseMenu = sNativeCharacterMenu = 0;
    resetMenuDisplay();
    prepareRace(characters, mode, engine, course, retro);
    return TRUE;
lost:
    sNativeMenuContext = sNativeCourseMenu = sNativeCharacterMenu = 0;
    ring_stop();
    scene_setMainFunc(connectionError_main);
    return TRUE;
cancelled:
    sNativeMenuContext = sNativeCourseMenu = sNativeCharacterMenu = 0;
    sSession = FALSE;
    sResume = 0;
    ring_stop();
    scene_setMainFunc(title_main);
    return TRUE;
}
/* Run after native physics and ranking, before the stock ranking menu can
 * create its four-player rows. Elimination ties use software ID order. */
bool32 ring_checkRoundEnd(void)
{
    RaceState* state = &gSceneState.raceState;
    u32 i, finished = 0, alive = 0;
    if (!sRing.enabled || !sRing.inGame) return FALSE;
    for (i = 0; i < state->driverCount; i++)
    {
        u8* driver = (u8*)state->drivers[i];
        if (sBattle)
        {
            if (driver[0x17D]) alive++;
            else if (sBattlePlaces[i] == 0xFF)
                sBattlePlaces[i] = sRing.count - 1 - sEliminated++;
        }
        /* Native GP uses five laps on Retro tracks, three on Super tracks.
         * Keep its initialized finish-lap byte instead of the Vs. constant. */
        else if (driver[0xC7] >= (sGame == GP_MODE ? ((u8*)state)[0x7E7] : 0x83)) finished++;
    }
    if ((sBattle && alive <= 1) || (!sBattle && finished >= state->driverCount - 1))
    {
        *((u8*)state + 0x7E8) = 0;
        return TRUE;
    }
    return FALSE;
}

/* Intercept native four-player post-race routing before it touches the stock
 * character/results arrays. Race physics and the native pause menu are retained. */
bool32 ring_finishRace(void)
{
    RaceState* state = &gSceneState.raceState;
    u32 i, choice;
    if (!sSession || !sRing.inGame) return FALSE;
    choice = *((u8*)state + 0x7E8);
    sRing.inGame = FALSE;
    if (sRing.failed || !sRing.enabled || choice == 12)
    {
        ring_stop();
        scene_setMainFunc(connectionError_main);
    }
    else if (choice == 1)
        prepareRace(sCharacters, sGame, sEngine, sCourse, sRetro);
    else if (choice == 5)
    {
        sSession = FALSE;
        ring_stop();
        scene_setMainFunc(title_main);
    }
    else if (choice == 2 || choice == 3 || choice == 4)
    {
        sResume = choice == 4 ? GAME : choice == 3 ? CHARACTERS : sBattle ? ARENA : sGame == GP_MODE ? CUP : COURSE;
        scene_setMainFunc(ring_main);
    }
    else
    {
        for (i = 0; i < state->driverCount; i++)
        {
            if (sBattle)
            {
                u32 place = sBattlePlaces[i] == 0xFF ? 0 : sBattlePlaces[i];
                sPlaces[place] = i;
            }
            else
            {
                Driver* driver = ((Driver**)((u8*)state + 0xC38))[i];
                sPlaces[i] = ((u8*)driver)[0xEC];
            }
        }
        if (sGame == GP_MODE)
        {
            extern const u8 _080DB0A8[];
            /* Share the native GP awards, including the zero-point places. */
            for (i = 0; i < 8; i++) if (sPlaces[i] < 8) sGPPoints[sPlaces[i]] += _080DB0A8[i + 1];
            if (sGPRace == 3) sGPCupComplete = 1;
        }
        else if (sBattle)
        {
            if (sPlaces[0] < sRing.count) sRoundScores[sPlaces[0]]++;
        }
        else
        {
            extern const u8 _080DB0A8[];
            for (i = 0; i < sRing.count; i++)
                sRoundScores[sPlaces[i]] += _080DB0A8[i + 1];
        }
        scene_setMainFunc(results_main);
    }
    return TRUE;
}

static void resetMenuDisplay(void)
{
    extern const u16 _08062774[];
    u16* palette = pltt_getBuffer(0);
    u32 i;
    ((vu16*)PLTT)[0] = 0;
    REG_DISPCNT = 0;
    sMenuReady = FALSE;
    scene_setVBlankFunc(0);
    DmaStop(0);
    vcount_reset();
    REG_DISPCNT = 0;
    palette[0] = 0;
    for (i = 0; i < 8; i++) palette[16 + i] = sPlayerColors[i];
    palette[8] = 0x7FA0;
    palette[9] = 0x4108;
    palette[10] = 0x7FFF;
    palette[11] = 0x03FF;
    for (i = 0; i < 16; i++) palette[32 + i] = _08062774[48 + i];
    pltt_setDirtyFlag(TRUE);
    pltt_copyBufferToHw();
    DMA_FILL(3, 0x0909, (void*)0x06000000, 240 * 160, 16);
    scene_setVBlankFunc(menuVblank);
    DmaStop(0);
    vcount_reset();
    irq_updateMask(IRQ_UPDATE_MODE_SET, IRQ_MASK_VBLANK | IRQ_MASK_CARTRIDGE | IRQ_MASK_IME | (sRing.enabled ? IRQ_MASK_SIO | IRQ_MASK_TIMER2 : 0));
    oam_802FE4C(FALSE);
    trns_init();
    REG_DISPCNT = 0x404;
    /* agbcc rereads volatile chained assignments; affine registers are write-only. */
    REG_BG2PA = 0x100;
    REG_BG2PD = 0x100;
    REG_BG2PB = 0;
    REG_BG2PC = 0;
    REG_BG2X_L = 0;
    REG_BG2X_H = 0;
    REG_BG2Y_L = 0;
    REG_BG2Y_H = 0;
    REG_BLDCNT = 0;
}

static bool32 connectionError_main(void)
{
    u16 previous = ~REG_KEYINPUT & 1023;
    resetMenuDisplay();
    screen("LINK CONNECTION LOST");
    text(12, 50, "CHECK THE RING CABLES", 0x7FFF);
    text(12, 75, "A OR B RETURNS TO THE MENU", 0x03FF);
    sMenuReady = TRUE;
    while (1)
    {
        u16 keys, trigger;
        main_frameProc();
        keys = ~REG_KEYINPUT & 1023;
        trigger = keys & ~previous;
        previous = keys;
        if (trigger & (A_BUTTON | B_BUTTON)) break;
    }
    sSession = FALSE;
    sResume = 0;
    scene_setMainFunc(title_main);
    return TRUE;
}

/* Keep the original world/driver update sequence behind the native rows.
 * HUD and finish-page coordinators are omitted: their four-controller
 * presentation storage is replaced by the original eight-racer row builder. */
static void ring_updateResultsBackground(void)
{
    RaceState* state = &gSceneState.raceState;
    extern void sub_804978C(RaceState*), sub_804AEFC(RaceState*);
    extern void sub_8049850(RaceState*), sub_804C0A8(RaceState*);
    extern void sub_8049B0C(RaceState*), sub_8049D78(RaceState*);
    extern void sub_8051014(void), sub_80484E0(void);
    extern int sub_8043840(void);
    gSceneState.field10++;
    sub_8043840();
    sub_804978C(state);
    sub_804AEFC(state);
    sub_8051014();
    sub_80484E0();
    sub_8049850(state);
    sub_804C0A8(state);
    sub_8049B0C(state);
    sub_8049D78(state);
}

static void ring_loadResultsHeading(void)
{
    u32 row, column;
    if (!sBattle) return;
    {
        extern const u32 gBattleCup[4];
        const TrackDef* course = gTrackDefTable[gBattleCup[sCourse]];
        u16* map = map_getBufferPtr(0);
        /* GP's cup and class widgets do not belong on a Battle result page.
         * Both arena sets share the native Battle Course 1..4 lettering. */
        for (row = 0; row < 3; row++)
            for (column = 0; column < 18; column++) map[row * 32 + column] = 0;
        LZ77UnCompVram((const u32*)course->trackNameText, (void*)0x06000400);
    }
}

static bool32 results_main(void)
{
    extern u16 gKeyTriggerBuf[8];
    extern void results_loadStandingsGraphics(void);
    extern void results_drawStandingRow(int row, NativeResultRow* result, int kind, u16* map);
    NativeResultRow* rows = sub_800EDDC()->rows.results;
    u32 i, entryRound = sRing.rounds;
    if (sGame == GP_MODE)
    {
        extern void results_buildStandings(void);
        NativeResultRow* standings = sub_800EDDC()->rows.results;
        u32 id;
        /* Populate native racer totals and use the original eight-racer
         * standings builder, including its own tie and human/CPU rules. */
        for (i = 0; i < 8; i++)
            ((u8*)gSceneState.raceState.drivers[i])[0xC4] = sGPPoints[i];
        results_buildStandings();
        for (i = 0; i < 8; i++)
        {
            for (id = 0; id < 8 && sCharacters[id] != standings[i].menuCharacter; id++);
            sPlaces[i] = id;
        }
    }
    else for (i = 0; i < sRing.count; i++)
    {
        u32 id = sPlaces[i];
        rows[i].place = i;
        rows[i].menuCharacter = sCharacters[id];
        rows[i].previousPoints = sRoundScores[id];
        rows[i].awardedPoints = 0;
    }
    /* Native post-race coasting uses the CPU controller for every kart.
     * Leave the already-captured human standings and scores untouched. */
    if (!sBattle)
        for (i = 0; i < gSceneState.raceState.driverCount; i++)
            gSceneState.raceState.drivers[i]->field_48 =
                (gSceneState.raceState.drivers[i]->field_48 & ~0x8000) | 0x2000;
    sRing.inGame = TRUE;
#ifdef MKSC_RING_MUSIC_SHARED
    sMusicRacing = FALSE;
#endif
    results_loadStandingsGraphics();
    ring_loadResultsHeading();
    for (i = 0; i < (sGame == GP_MODE ? 8 : sRing.count); i++)
        results_drawStandingRow(i, &rows[i], 2, map_getBufferPtr(0));
    map_setBufferDestination(0, (void*)0x06003800);
    map_setBufferDirty(1);
    REG_BG0CNT = 0x0700;
    /* Both offsets are write-only. A chained assignment makes agbcc read
     * VOFS back as open bus and scrolls/crops the result heading. */
    REG_BG0HOFS = 0;
    REG_BG0VOFS = 0;
    REG_BLDCNT = 0;
    REG_WININ = REG_WINOUT = 0x3F3F;
    REG_DISPCNT = (REG_DISPCNT & ~0x80) | 0x101;
    /* The finite finish jingle is followed by the native looping result
     * theme, rather than leaving the mixer stopped after that jingle. */
    m4aSongNumStartOrChange(sGame == GP_MODE ? SONG_014 : SONG_017);
    while (1)
    {
        oam_update();
        main_frameProc();
        ring_updateResultsBackground();
        if (!ring_active()) { scene_setMainFunc(connectionError_main); return TRUE; }
        if (sRing.rounds - entryRound >= 120 && (gKeyTriggerBuf[0] & A_BUTTON))
        {
            if (sGame == GP_MODE && sGPRace < 3)
            {
                sGPRace++;
                prepareRace(sCharacters, GP_MODE, sEngine, sCourse + 1, sRetro);
                return TRUE;
            }
            sRing.inGame = FALSE;
            sResume = sBattle ? ARENA : sGame == GP_MODE ? CUP : COURSE;
            scene_setMainFunc(ring_main);
            return TRUE;
        }
        if (sRing.rounds - entryRound >= 120 && (gKeyTriggerBuf[0] & B_BUTTON))
        {
            sSession = FALSE;
            ring_stop();
            scene_setMainFunc(title_main);
            return TRUE;
        }
    }
}

#endif
