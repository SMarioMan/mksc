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
enum { GAME, ENGINE, CHARACTERS, CUP, COURSE, ARENA };
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
    if (!sRing.enabled || !sRing.inGame) return;
    for (i = 0; i < 11; i++)
        if (player == gMPlayTable[i].info) { ring_audioTiming[i]++; break; }
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
static u8 sMusicRacing, sSharedSong, sSharedFinalLap;
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
    if (!sRing.enabled || !sRing.inGame) return FALSE;
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
    return ring_sharedMusicActive() && song >= 5 && song <= 7 ? 5 : song;
}
/* Derive BGM from the whole race, ignoring local power-up themes. Native
 * final-lap flag is bit 3; finished drivers keep it set, so tempo never drops. */
static void ring_updateSharedMusic(void)
{
    extern void m4aMPlayTempoControl(struct MusicPlayerInfo*, u16);
    RaceState* state = &gSceneState.raceState;
    u32 i, fast = 0, music, finished = 0, alive = 0;
    if (ring_sharedMusicMuted())
    {
        if (!(gMPlayTable[0].info->status & MUSICPLAYER_STATUS_PAUSE))
            m4aMPlayStop(gMPlayTable[0].info);
        sSharedSong = 0; sSharedFinalLap = 0;
        return;
    }
    if (!ring_sharedMusicActive()) { sSharedSong = 0; sSharedFinalLap = 0; return; }
    /* Physics begins during the native opening fanfare. Do not treat its
     * first tick as permission to replace a still-playing start song. Both
     * GP and linked races use player zero for this finite introductory cue. */
    if (!sSharedSong &&
        (gMPlayTable[0].info->songHeader == gSongTable[3].header ||
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
    if (sBattle) fast = 0;
    music = sBattle ? 18 : track_getRaceMusic(state);
    if (music == 36) music = 28;
    if (sSharedSong != music || sSharedFinalLap != fast)
    {
        if (fast && !sSharedFinalLap)
        {
            m4aSongNumStart(19);
            sSharedSong = music; sSharedFinalLap = fast;
            return;
        }
        m4aSongNumStart(music);
        sSharedSong = music; sSharedFinalLap = fast;
    }
    /* Resume after the common final-lap jingle, never an individual star. */
    if (gMPlayTable[0].info->songHeader == gSongTable[19].header &&
        (gMPlayTable[0].info->status & MUSICPLAYER_STATUS_TRACK)) return;
    if (gMPlayTable[0].info->songHeader != gSongTable[music].header)
        m4aSongNumStart(music);
    m4aMPlayTempoControl(gMPlayTable[gSongTable[music].ms].info, fast ? 320 : 256);
}
#endif

bool32 ring_ignoreLocalMusic(void)
{
#ifdef MKSC_RING_MUSIC_SHARED
    return ring_sharedMusicActive();
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
            delay = RING_TURNAROUND * 4;
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

static void waitReceiver(void)
{
    if (sRing.owner) return;
    /* Serial and VBlank IRQs wake HALT even with IME masked. Check and sleep
     * atomically so a COMMIT between the check and HALT cannot strand a peer.
     * Keep external-clock consoles from spending the whole barrier spinning. */
    REG_IME = 0;
    if (sRing.enabled && sRing.inFlight && !sRing.complete && !sRing.failed)
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
    u32 i, candidate;

    /* Stock Vs. generates only two, three or four start positions. Tracks
     * already contain the eight-position Grand Prix grid. */
    if (sRing.enabled && sRing.count > 4 && (state->curRaceStateUnknown & 0x400))
        sub_8031ED8(state);
    else
        loadtrack_stockStartGrid(state);
    if (sRing.enabled && sRing.count > 4 && (state->curRaceStateUnknown & 0x800))
    {
        /* Pair each new slot with its native marker, symmetrically across
         * the marker's facing direction. Shrink both offsets together when
         * the arena edge would put either racer in a void/lava tile. */
        for (i = 4; i < sRing.count; i++)
        {
            s32* base = (s32*)((u8*)state + 0x24 + (i - 4) * 16);
            s32* extra = (s32*)((u8*)state + 0x24 + i * 16);
            s32 x = base[0], y = base[1];
            s16 yaw = base[2];
            for (candidate = 12; candidate >= 4; candidate -= 4)
            {
                s32 dx = math_cos(yaw) * candidate * 2;
                s32 dy = math_sin(yaw) * candidate * 2;
                if (!(sub_8032990(state, (x + dx) >> 16, (y + dy) >> 16, 0) & 0x80) &&
                    !(sub_8032990(state, (x - dx) >> 16, (y - dy) >> 16, 0) & 0x80))
                {
                    base[0] = x - dx; base[1] = y - dy;
                    extra[0] = x + dx; extra[1] = y + dy;
                    extra[2] = base[2]; extra[3] = base[3];
                    break;
                }
            }
            if (candidate < 4) sRing.failed = TRUE;
        }
    }
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
        waitReceiver();
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

/* The native opponent marker switch has only three entries. Reuse its OBJ
 * tile region for a diamond, with each kart's reserved identity palette. */
static const u16 sKartMarkerCells[8][4] = {
    {1, 0x00F6, 0x01FC, 0x8532}, {1, 0x00F6, 0x01FC, 0x9532},
    {1, 0x00F6, 0x01FC, 0xA532}, {1, 0x00F6, 0x01FC, 0xB532},
    {1, 0x00F6, 0x01FC, 0xC532}, {1, 0x00F6, 0x01FC, 0xD532},
    {1, 0x00F6, 0x01FC, 0xE532}, {1, 0x00F6, 0x01FC, 0xF532}
};

const u16* ring_playerMarker(unsigned id)
{
    return sRing.enabled && id < (sGame == GP_MODE ? 8 : sRing.count) ? sKartMarkerCells[id] : 0;
}

static const u16 sBalloonCells[8][4] = {
    {1, 0x00D7, 0x41F8, 0x849C}, {1, 0x00D7, 0x41F8, 0x949C},
    {1, 0x00D7, 0x41F8, 0xA49C}, {1, 0x00D7, 0x41F8, 0xB49C},
    {1, 0x00D7, 0x41F8, 0xC49C}, {1, 0x00D7, 0x41F8, 0xD49C},
    {1, 0x00D7, 0x41F8, 0xE49C}, {1, 0x00D7, 0x41F8, 0xF49C}
};
extern bool32 sBalloonArtReady;

const u16* ring_balloonCell(Driver* driver)
{
    extern const u16 _0807B534[];
    u32 id = ((u8*)driver)[0xEC];
    return sRing.enabled && id < 8 ? sBalloonCells[id] : _0807B534;
}

static const u16 sBalloonHudCells[8][4] = {
    {1, 0x8000, 0, 0x80E8}, {1, 0x8000, 0, 0x90E8},
    {1, 0x8000, 0, 0xA0E8}, {1, 0x8000, 0, 0xB0E8},
    {1, 0x8000, 0, 0xC0E8}, {1, 0x8000, 0, 0xD0E8},
    {1, 0x8000, 0, 0xE0E8}, {1, 0x8000, 0, 0xF0E8}
};

const u16* ring_balloonHudCell(unsigned id)
{
    extern const u16 _0807B9A8[];
    if (sRing.enabled && id < 4) id = sBattleHudIds[id];
    return sRing.enabled && id < 8 ? sBalloonHudCells[id] : _0807B9A8;
}

static void tintBalloon(vu16* tiles, u32 words, const u16* palette)
{
    u32 used = 0, i, lightest = 0, brightness = 0;
    for (i = 0; i < words; i++)
    {
        u32 bits = tiles[i], shift;
        for (shift = 0; shift < 16; shift += 4) used |= 1 << ((bits >> shift) & 15);
    }
    for (i = 1; i < 16; i++)
    {
        u32 color = palette[i];
        u32 value = (color & 31) + ((color >> 5) & 31) + ((color >> 10) & 31);
        if ((used & (1 << i)) && value > brightness) { brightness = value; lightest = i; }
    }
    for (i = 0; i < words; i++)
    {
        u32 bits = tiles[i], output = 0, shift;
        for (shift = 0; shift < 16; shift += 4)
        {
            u32 pixel = (bits >> shift) & 15;
            if (pixel) output |= (pixel == lightest ? 2 : 1) << shift;
        }
        tiles[i] = output;
    }
}

/* Recolor balloon art in its native slots and use the reserved identity OBJ
 * palettes. BG palette 9 and every native minimap tile remain untouched. */
static void markerTiles(void)
{
    static const u16 pixels[32] = {
        0x1000,0,0x1100,1,0x2110,0x12,0x2211,0x1122,
        0x2110,0x12,0x1100,1,0x1000,0,0,0,
        0x11,0,0x11,0,0,0,0,0,0,0,0,0,0,0,0,0
    };
    u16* palette = pltt_getBuffer(PLTT_BUFFER_OBJ);
    u32 i;
    if (sBattle && !sBalloonArtReady)
    {
        tintBalloon((vu16*)0x06011380, 64, palette + 64);
        tintBalloon((vu16*)0x06011D00, 32, palette + 16);
        sBalloonArtReady = TRUE;
    }
    CpuCopy16(pixels, (void*)0x06012640, sizeof(pixels));
}

/* Course palette animations run after HUD drawing. Restore the two identity
 * entries at publication, after their writes and graphics DMA have finished. */
void ring_publishIdentityPalette(void)
{
    u16* palette;
    u32 i;
    if (!sRing.enabled || !sRing.inGame) return;
    palette = pltt_getBuffer(PLTT_BUFFER_OBJ);
    for (i = 0; i < (sGame == GP_MODE ? 8 : sRing.count); i++)
    {
        palette[(8 + i) * 16 + 1] = sPlayerColors[i];
        palette[(8 + i) * 16 + 2] = (sPlayerColors[i] >> 1 & 0x3DEF) + 0x4210;
    }
    pltt_setDirtyFlag(TRUE);
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
        markerTiles();
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
    if (sRing.enabled) markerTiles();
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
        cell[3] = 0x133 | (8 + id) << 12;
        oam_renderCellData(cell, &position, 0, 0, 0, 0);
    }
}
static const char* const sCharacterNames[8] = { "MARIO", "LUIGI", "PEACH", "TOAD", "YOSHI", "DK", "WARIO", "BOWSER" };
static const char* const sCupNames[5] = { "MUSHROOM CUP", "FLOWER CUP", "LIGHTNING CUP", "STAR CUP", "SPECIAL CUP" };
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
    for (i = 0; i < 8; i++) if (color == sPlayerColors[i]) return i;
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

static void number(int x, int y, unsigned n, u16 color)
{
    char string[3];
    string[0] = '0' + (n / 10) % 10;
    string[1] = '0' + n % 10;
    string[2] = 0;
    text(x, y, string, color);
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
    static const s16 slices[3][4] = { {0,32,7,3}, {0,-33,18,11}, {5,25,25,34} };
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
    sMusicRacing = FALSE;
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
    state->headerTableIdx = sBattle ? track_getBattleTrack(course) :
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
    u16 footerCells[55];
    u16 footerTiles[288];
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
static void nativeLabelNumber(u8* tiles, int width, int left, int number,
                              const u16* palette)
{
    int row, col, white = 0, brightest = -1;
    for (col = 0; col < 16; col++)
    {
        int color = palette[col];
        int brightness = (color & 31) + (color >> 5 & 31) + (color >> 10 & 31);
        if (brightness > brightest) { brightest = brightness; white = col; }
    }
    for (row = 0; row < 16; row++)
    {
        int background = nativeLabelPixel(tiles, width, left - 1, row);
        for (col = 0; col < 14; col++) nativeLabelSetPixel(tiles, width, left + col, row, background);
    }
    for (row = 0; row < 14; row++)
        for (col = 0; col < 10; col++)
            if (sFont[26 + number][row / 2] & (16 >> (col / 2)))
                nativeLabelSetPixel(tiles, width, left + col + 1, row + 1, white);
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
    u32 i, row, col;
    const char* hint = "SELECT";
    sNativeMenuContext = menu;
    sNativeMenuExtras = frmheap_calloc(&gSceneState.raceState.frameHeap, 1, sizeof(NativeMenuExtras));
    if (sRing.id >= 4)
    {
        LZ77UnCompWram(_080D8E9C[3].tiles, raw);
        nativeLabelNumber((u8*)raw, 64, 39, sRing.id + 1, _080D8E9C[3].palette);
        nativeLabelCompress((u8*)raw, 512, sNativeMenuExtras->playerLabel);
        for (i = 0; i < 16; i++)
        {
            u32 source = _080D8E9C[3].palette[i];
            u32 r = source & 31, g = source >> 5 & 31, b = source >> 10 & 31;
            u32 low = r < g ? r : g, high = r > g ? r : g;
            u32 color = sPlayerColors[sRing.id];
            if (b < low) low = b;
            if (b > high) high = b;
            r = low + (high - low) * (color & 31) / 31;
            g = low + (high - low) * (color >> 5 & 31) / 31;
            b = low + (high - low) * (color >> 10 & 31) / 31;
            sNativeMenuExtras->playerPalette[i] = r | g << 5 | b << 10;
        }
    }
    if (sRing.count > 4)
    {
        LZ77UnCompWram(_080D8EBC[3].tiles, raw);
        nativeLabelNumber((u8*)raw, 32, 4, sRing.count, _080D8EBC[3].palette);
        nativeLabelCompress((u8*)raw, 256, sNativeMenuExtras->countLabel);
    }
    /* Two short lines fit between the stock controls and player name. The
     * five packed objects also stay within the native page's OAM budget. */
    for (i = 0; i < 17; i++)
    {
        u32 x, tile;
        if (i == 6) hint = "SUPER/RETRO";
        x = (i < 6 ? i : i - 6) * 6;
        tile = i < 6 ? 0 : 5;
        for (row = 0; row < 7; row++)
            for (col = 0; col < 5; col++)
                if (sFont[glyph(hint[i < 6 ? i : i - 6])][row] & (16 >> col))
                {
                    u32 at = x + col;
                    sNativeMenuExtras->footerTiles[(tile + at / 8) * 16 + row * 2 + (at & 7) / 4] |= 15 << ((at & 3) * 4);
                }
    }
    sNativeMenuExtras->footerCells[0] = 5;
    for (i = 0; i < 5; i++)
    {
        static const u16 offsets[5] = {0, 4, 5, 9, 13};
        static const u16 x[5] = {0x4000, 32, 0x4000, 0x4020, 64};
        sNativeMenuExtras->footerCells[1 + i * 3] = (i == 1 || i == 4 ? 0 : 0x4000) | (i >= 2 ? 8 : 0);
        sNativeMenuExtras->footerCells[2 + i * 3] = x[i];
        sNativeMenuExtras->footerCells[3 + i * 3] = 0x63EE + offsets[i];
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
    return _080D8EBC[sRing.count <= 4 ? sRing.count - 1 : 3].palette;
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


/* Transport commits a complete input vector. The native character page
 * renders one local view; this coordinator owns all eight independent picks
 * and publishes confirmation only when every participant has a unique pick.
 * No native four-controller array is enlarged into adjacent menu widgets. */
void ring_nativeMenuFrame(void)
{
    extern u16 gKeyBuf[8], gKeyTriggerBuf[8], gKeyRepeatTriggerBuf[8];
    u16 held;
    u32 i, j;
    if (!sNativeCharacterMenu && !sNativeCourseMenu) return;
    sNativeHostKeys[0] = gKeyBuf[0];
    sNativeHostKeys[1] = gKeyTriggerBuf[0];
    sNativeHostKeys[2] = gKeyRepeatTriggerBuf[0];
    sNativeHostKeysSaved = TRUE;
    if (sNativeCourseMenu)
    {
        if (gKeyTriggerBuf[0] & SELECT_BUTTON)
            gKeyTriggerBuf[0] = (gKeyTriggerBuf[0] & ~SELECT_BUTTON) | R_BUTTON;
        return;
    }
    held = gKeyBuf[sRing.id];
    if (!sNativeCharacterConfirm && !sNativeCharacterBack)
    {
        for (i = 0; i < sRing.count; i++)
        {
            u16 input = gKeyTriggerBuf[i];
            const NativeCharacterNavigation* choice = &_080D9274[sNativeCharacterChoices[i]];
            if (input & B_BUTTON)
            {
                if (!i && !(sNativeCharactersReady & 1)) sNativeCharacterBack = TRUE;
                sNativeCharactersReady &= ~(1 << i);
            }
            if (!(sNativeCharactersReady & (1 << i)))
            {
                u16 direction = gKeyRepeatTriggerBuf[i];
                int next = direction & DPAD_UP ? choice->up :
                           direction & DPAD_DOWN ? choice->down :
                           direction & DPAD_LEFT ? choice->left :
                           direction & DPAD_RIGHT ? choice->right : sNativeCharacterChoices[i];
                sNativeCharacterChoices[i] = next;
                if (input & A_BUTTON)
                {
                    if (next == 4)
                    {
                        /* Random uses the common input round, never a local
                         * hardware timer, so all peers choose the same roster. */
                        next = sRing.rounds & 7;
                        for (j = 0; j < 8; j++)
                        {
                            u32 other;
                            u8 candidate = (next + j) & 7;
                            candidate += candidate >= 4;
                            for (other = 0; other < sRing.count; other++)
                                if (other != i && (sNativeCharactersReady & (1 << other)) &&
                                    sNativeCharacterChoices[other] == candidate) break;
                            if (other == sRing.count) { next = candidate; break; }
                        }
                        sNativeCharacterChoices[i] = next;
                    }
                    for (j = 0; j < sRing.count; j++)
                        if (j != i && (sNativeCharactersReady & (1 << j)) &&
                            sNativeCharacterChoices[j] == next) break;
                    if (j == sRing.count) sNativeCharactersReady |= 1 << i;
                }
            }
        }
        if (sNativeCharactersReady == (1 << sRing.count) - 1)
            sNativeCharacterConfirm = TRUE;
    }
    /* Let the original page animate the common picks. Writing its one local
     * cursor also makes Random's shared choice use the native portrait/name. */
    if (sNativeCharacterMenu->characterPhases[0] == 2)
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
    gKeyBuf[0] = held & ~(A_BUTTON | B_BUTTON);
    gKeyTriggerBuf[0] = 0;
    gKeyRepeatTriggerBuf[0] = 0;
    if (sNativeCharacterMenu->characterPhases[0] == 2 ||
        sNativeCharacterMenu->characterPhases[0] == 4)
    {
        if (sNativeCharacterBack) gKeyTriggerBuf[0] = B_BUTTON;
        else if (sNativeCharacterConfirm) gKeyTriggerBuf[0] = A_BUTTON;
    }
    /* The common coordinator already applied navigation. The native page
     * must not apply the same edge a second time after the cursor update. */
}

void ring_drawNativeMenuCursors(void)
{
    u32 i;
    u16* cells = sNativeCharacterCursorCells;
    Vec2s16 origin;
    static const s8 offsets[8][2] = {
        {-16,-16},{-4,-16},{8,-16},{16,-4},
        {8,16},{-4,16},{-16,8},{-16,-4}
    };
    if (sNativeCourseMenu && sNativeCourseMenu->gamemode != GAMEMODE_BATTLE)
    {
        CpuCopy16(sNativeMenuExtras->footerTiles, (void*)0x06017DC0, sizeof(sNativeMenuExtras->footerTiles));
        pltt_getBuffer(1)[111] = 0x7FFF;
        pltt_setDirtyFlag(TRUE);
        origin.x = 128; origin.y = 144;
        oam_renderCellData(sNativeMenuExtras->footerCells, &origin, 0, 0, 0, 0);
        return;
    }
    if (!sNativeCharacterMenu || sNativeCharacterConfirm || sNativeCharacterBack) return;
    cells[0] = sRing.count;
    for (i = 0; i < sRing.count; i++)
    {
        const NativeCharacterNavigation* choice = &_080D9274[sNativeCharacterChoices[i]];
        cells[1 + i * 3] = (choice->y + offsets[i][1]) & 255;
        cells[2 + i * 3] = (choice->x + offsets[i][0]) & 511;
        cells[3 + i * 3] = 0x63C0 + i;
    }
    origin.x = origin.y = 0;
    oam_renderCellData(cells, &origin, 0, 0, 0, 0);
}

static int ring_chooseNativeCharacters(u8* characters, int mode, int engine)
{
    extern bool32 spm_chooseCharacter(SpmState*, int resume, int allowBack);
    extern void sub_8001DDC(void);
    extern void sub_800C680(void);
    u16 tiles[128];
    u32 i, row, col;
    int accepted;
    SpmState* menu;
    bool32 fresh = !sNativeMenuContext;
    menu = ring_getNativeMenu();
    menu->menuPage = MENU_PAGE_CHARACTER;
    menu->lastPlayer = 0;
    menu->localPlayerId = 0;
    menu->playerCount = sRing.count;
    menu->gamemode = mode == GP_MODE ? GAMEMODE_GP : mode == BATTLE_MODE ? GAMEMODE_BATTLE : GAMEMODE_VS;
    menu->raceSpeed = engine;
    menu->character = characters[sRing.id];
    menu->cup = menu->track = -1;
    menu->reenteringLinkMenu = 1;
    menu->suppressConnectionCheck = 1;
    for (i = 0; i < 8; i++)
        sNativeCharacterChoices[i] = characters[i] + (characters[i] >= 4);
    for (i = 0; i < 128; i++) tiles[i] = 0;
    for (i = 0; i < 8; i++)
        for (row = 0; row < 7; row++)
            for (col = 0; col < 5; col++)
                if (sFont[27 + i][row] & (16 >> col))
                    tiles[i * 16 + row * 2 + (col + 1) / 4] |= (i + 1) << (((col + 1) & 3) * 4);
    CpuCopy16(tiles, (void*)0x06017800, sizeof(tiles));
    for (i = 0; i < 8; i++) pltt_getBuffer(1)[97 + i] = sPlayerColors[i];
    pltt_setDirtyFlag(TRUE);
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
    /* A solo session reaches the class page without first visiting mode
     * selection. The native resume path requires that earlier page's
     * initialization, so enter normally for a newly allocated context. */
    if (!spm_chooseModeAndClass(menu, chooseMode || fresh ? 0 : 1))
    {
            return FALSE;
    }
    *mode = menu->gamemode == GAMEMODE_GP ? GP_MODE : menu->gamemode == GAMEMODE_BATTLE ? BATTLE_MODE : VS_MODE;
    *engine = *mode == BATTLE_MODE ? 0 : menu->raceSpeed;
    return TRUE;
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
    menu->raceSpeed = engine;
    menu->character = character;
    menu->cup = mode == BATTLE_MODE ? 0 : *course / 4;
    menu->track = mode == BATTLE_MODE ? *course : *course % 4;
    menu->trackPage = mode == BATTLE_MODE ? 0 : *retro;
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
        *retro = mode == BATTLE_MODE ? 0 : menu->trackPage;
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
    u32 i;
    extern u16 gKeyTriggerBuf[8], gKeyBuf[8];
    sNativeMenuContext = sNativeCourseMenu = sNativeCharacterMenu = 0;
    resetMenuDisplay();
    if (sResume && ring_active()) goto connected;
    sSession = TRUE;
    for (i = 0; i < 8; i++) sWins[i] = 0;
    screen("RING LINK");
    text(12, 42, "CONNECT ALL GBAS IN A RING", 0x7FFF);
    text(12, 60, "PRESS START ON ONE CONSOLE", 0x03FF);
    text(12, 78, "WAITING FOR OTHER PLAYERS", 0x7FFF);
    text(12, 100, "SELECT SOLO GRAND PRIX", 0x7FFF);
    sMenuReady = TRUE;
    /* Discovery polls without the normal frame loop. Continue servicing
     * MP2000 every VBlank so the last title sample cannot loop indefinitely. */
    m4aMPlayAllStop();
    sLastAudioFrame = gSceneState.field14 - 1;
    start(FALSE);
    while (!sRing.count && !sRing.failed)
    {
        ring_serviceAudio();
        keys = ~REG_KEYINPUT & 1023;
        trigger = keys & ~previous;
        previous = keys;
        if (trigger & B_BUTTON) goto cancelled;
        if ((trigger & SELECT_BUTTON) && sRing.id == RING_INVALID)
        {
            sRing.id = 0; sRing.count = 1; sRing.owner = TRUE;
            /* Solo bypasses DISCOVER/ASSIGN, but the shared native pages
             * still need the same connected endpoint metadata. */
            sio2_sMPlayerId.mPlayerId = 0;
            sio2_sMPlayerId.playerCount = 1;
            sio2_sMPlayerId.field_16 = 0x8001;
            mode = GP_MODE; phase = ENGINE; ignoreStart = FALSE;
            break;
        }
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
        waitReceiver();
    }
    if (sRing.failed) goto lost;
connected:
    m4aSongNumStart(2);
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
                if (sRing.count == 1) goto cancelled;
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
            if (sRing.count == 1) { mode = GP_MODE; phase = ENGINE; continue; }
            if (!ring_chooseNativeMode(&mode, &engine, TRUE)) goto cancelled;
            course = cup = 0;
            phase = CHARACTERS;
            drawnPhase = -1;
            break;
        case ENGINE:
            if (ring_chooseNativeMode(&mode, &engine, FALSE)) phase = CHARACTERS;
            else phase = GAME;
            drawnPhase = -1;
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
        sResume = choice == 4 ? CHARACTERS : sBattle ? ARENA : sGame == GP_MODE || choice == 3 ? CUP : COURSE;
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
        else if (sPlaces[0] < sRing.count) sWins[sPlaces[0]]++;
        scene_setMainFunc(results_main);
    }
    return TRUE;
}

static void resetMenuDisplay(void)
{
    extern const u16 _08062774[];
    u16* palette = pltt_getBuffer(0);
    u32 i;
    sMenuReady = FALSE;
    scene_setVBlankFunc(0);
    DmaStop(0);
    vcount_reset();
    REG_DISPCNT = 0x80;
    for (i = 0; i < 8; i++) palette[i] = sPlayerColors[i];
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

static bool32 results_main(void)
{
    extern u16 gKeyTriggerBuf[8];
    extern void results_loadStandingsGraphics(void);
    extern void results_drawStandingRow(int row, NativeResultRow* result, int kind, u16* map);
    NativeResultRow* rows = sub_800EDDC()->rows.results;
    u32 i;
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
        rows[i].previousPoints = sWins[id];
        rows[i].awardedPoints = 0;
    }
    scene_setVBlankFunc(0);
    DmaStop(0);
    vcount_reset();
    REG_DISPCNT = 0x80;
    oam_802FE4C(FALSE);
    results_loadStandingsGraphics();
    for (i = 0; i < (sGame == GP_MODE ? 8 : sRing.count); i++)
        results_drawStandingRow(i, &rows[i], 2, map_getBufferPtr(0));
    map_setBufferDestination(0, (void*)0x06003800);
    map_setBufferDirty(1);
    REG_BG0CNT = 0x0700;
    REG_BG0HOFS = REG_BG0VOFS = 0;
    REG_BLDCNT = 0;
    REG_WININ = REG_WINOUT = 0x3F3F;
    REG_DISPCNT = 0x100;
    while (1)
    {
        main_frameProc();
        if (!ring_active()) { scene_setMainFunc(connectionError_main); return TRUE; }
        if (gKeyTriggerBuf[0] & A_BUTTON)
        {
            if (sGame == GP_MODE && sGPRace < 3)
            {
                sGPRace++;
                prepareRace(sCharacters, GP_MODE, sEngine, sCourse + 1, sRetro);
                return TRUE;
            }
            sResume = sBattle ? ARENA : sGame == GP_MODE ? CUP : COURSE;
            scene_setMainFunc(ring_main);
            return TRUE;
        }
        if (gKeyTriggerBuf[0] & B_BUTTON)
        {
            sSession = FALSE;
            ring_stop();
            scene_setMainFunc(title_main);
            return TRUE;
        }
    }
}

#endif
