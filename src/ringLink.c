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
u16 ring_sDriverInputs[8];
u8 ring_balloonAffineIndex[8];
u8 ring_selectedCharacters[8];
extern u8 gSelectedCharacters[8];
extern u16 sio2_sKeyBuf[8];
void sub_804AE58(RaceState* state, int character, int human);
int spm_menuCharIdxToCharId(int menuIndex);
enum { GAME, ENGINE, CHARACTERS, CUP, COURSE, ARENA };
static u8 sSession, sResume, sCharacters[8], sPlaces[8];
static u16 sWins[8];
static u8 sBattlePlaces[8], sEliminated;
static u8 sBattleHudIds[4];
enum { VS_MODE, BATTLE_MODE, GP_MODE };
static int sGame, sBattle, sEngine, sCourse, sRetro;
static u16 sGPPoints[8];
static u8 sGPRace;
static u32 sGPCupComplete;
static bool8 sGpBannersInitialized;
u32 ring_frameTiming[5];
static u32 sRenderStart;
static u32 ring_cycles(void)
{
    u32 frame, line;
    u16 ime = REG_IME;
    REG_IME = 0;
    frame = gSceneState.field14;
    line = REG_VCOUNT;
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
    extern void sub_801D2A4(int stagger);
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
    sub_801D2A4(1);
}

static u32 sLastAudioFrame;
static volatile bool32 sGraphicsReady;
static volatile u32 sVideoTick;
static u32 sTickEpochFrame, sTickEpochRound;
static bool32 sTickEpochStarted;

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

void ring_serviceAudio(void)
{
    u32 frame = gSceneState.field14;
    if (frame != sLastAudioFrame)
    {
        u32 started = ring_cycles();
        sLastAudioFrame = frame;
#ifdef MKSC_RING_MUSIC_SHARED
        ring_updateSharedMusic();
#endif
        m4aSoundMain();
        ring_frameTiming[4] = ring_cycles() - started;
    }
}

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

s32 ring_spriteDivide(s32 numerator, s32 denominator)
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
    receive(word);
    if (sRing.count && sRing.requested && !sRing.frameStarted && !sRing.ownReturned && !(sRing.packets & 15))
        enqueue(packet(READY, sRing.id, sRing.sequence, sRing.staged[sRing.id]));
    if (sRing.owner && sRing.frameStarted && !sRing.complete &&
        sRing.packets - sRing.commitPacket > sRing.count + 1 &&
        sRing.queued + sRing.count < 16 && !(sRing.packets & 15))
    {
        u32 i;
        /* Replay the collected vector as well as COMMIT. Peers that already
         * completed this round forward these words without applying them. */
        for (i = 0; i < sRing.count; i++)
            enqueue(packet(READY, i, sRing.sequence, sRing.staged[i]));
        enqueue(packet(COMMIT, 0, sRing.sequence, 0));
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
            delay = RING_TURNAROUND * 2;
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
static bool32 sBalloonArtReady;

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

static vu16* sMenuPixels;
static volatile bool32 sMenuReady;
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
    *(vu32*)0x03000008 = course;
    sRing.inGame = TRUE;
    scene_setMainFunc(race_main);
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
            if (trigger & (DPAD_UP | DPAD_DOWN))
            {
                mode = (mode + ((trigger & DPAD_UP) ? 2 : 1)) % 3;
                course = cup = 0;
            }
            if (trigger & (A_BUTTON | START_BUTTON)) phase = mode == BATTLE_MODE ? CHARACTERS : ENGINE;
            screen("CHOOSE A GAME");
            text(30, 45, "VS", mode == VS_MODE ? 0x03FF : 0x7FFF);
            text(30, 70, "BATTLE", mode == BATTLE_MODE ? 0x03FF : 0x7FFF);
            text(30, 95, "GRAND PRIX", mode == GP_MODE ? 0x03FF : 0x7FFF);
            break;
        case ENGINE:
            if ((trigger & DPAD_UP) && engine > 0) engine--;
            if ((trigger & DPAD_DOWN) && engine < 2) engine++;
            if (trigger & A_BUTTON) phase = CHARACTERS;
            screen("CHOOSE ENGINE CLASS");
            text(30, 45, "50 CC", engine == 0 ? 0x03FF : 0x7FFF);
            text(30, 70, "100 CC", engine == 1 ? 0x03FF : 0x7FFF);
            text(30, 95, "150 CC", engine == 2 ? 0x03FF : 0x7FFF);
            break;
        case CHARACTERS:
            if ((trigger & B_BUTTON) && !(ready & 1))
            {
                ready = 0;
                phase = mode == BATTLE_MODE ? GAME : ENGINE;
                continue;
            }
            for (i = 0; i < sRing.count; i++)
            {
                u16 input = gKeyTriggerBuf[i];
                u32 j;
                if (input & B_BUTTON) ready &= ~(1 << i);
                if (!(ready & (1 << i)))
                {
                    if (input & (DPAD_LEFT | DPAD_UP)) characters[i] = (characters[i] + 7) & 7;
                    if (input & (DPAD_RIGHT | DPAD_DOWN)) characters[i] = (characters[i] + 1) & 7;
                    if (input & A_BUTTON)
                    {
                        for (j = 0; j < sRing.count; j++)
                            if (j != i && (ready & (1 << j)) && characters[j] == characters[i]) break;
                        if (j == sRing.count) ready |= 1 << i;
                    }
                }
            }
            screen("CHOOSE A CHARACTER");
            for (i = 0; i < sRing.count; i++)
            {
                int x = (i & 1) ? 125 : 12;
                int y = 36 + (i / 2) * 26;
                number(x, y, i + 1, sPlayerColors[i]);
                portrait(x + 14, y, characters[i]);
                text(x + 34, y, sCharacterNames[characters[i]], sPlayerColors[i]);
                if (ready & (1 << i)) text(x + 34, y + 10, "READY", 0x7FFF);
            }
            if (ready == (1 << sRing.count) - 1) phase = mode == BATTLE_MODE ? ARENA : CUP;
            break;
        case CUP:
            if ((trigger & DPAD_UP) && cup > 0) cup--;
            if ((trigger & DPAD_DOWN) && cup < 4) cup++;
            if (trigger & SELECT_BUTTON) retro ^= 1;
            if (trigger & A_BUTTON)
            {
                course = cup * 4;
                if (mode == GP_MODE)
                {
                    sGPRace = 0;
                    sGPCupComplete = 0;
                    for (i = 0; i < 8; i++) sGPPoints[i] = 0;
                    goto launch;
                }
                phase = COURSE;
            }
            screen("CHOOSE A CUP");
            for (i = 0; i < 5; i++) text(24, 35 + i * 18, sCupNames[i], i == cup ? 0x03FF : 0x7FFF);
            text(144, 132, retro ? "RETRO" : "SUPER", 0x7FFF);
            text(78, 148, "SELECT SUPER/RETRO", 0x7FFF);
            break;
        case COURSE:
            if (trigger & SELECT_BUTTON) retro ^= 1;
            if ((trigger & DPAD_UP) && course > cup * 4) course--;
            if ((trigger & DPAD_DOWN) && course < cup * 4 + 3) course++;
            screen("CHOOSE A TRACK");
            for (i = 0; i < 4; i++)
            {
                text(20, 40 + i * 22, (retro ? sRetroTracks : sTracks)[cup * 4 + i], cup * 4 + i == course ? 0x03FF : 0x7FFF);
            }
            text(144, 132, retro ? "RETRO" : "SUPER", 0x7FFF);
            text(78, 148, "SELECT SUPER/RETRO", 0x7FFF);
            if (trigger & A_BUTTON) goto launch;
            break;
        case ARENA:
            if (course < 0 || course > 3) course = 0;
            if ((trigger & DPAD_UP) && course > 0) course--;
            if ((trigger & DPAD_DOWN) && course < 3) course++;
            screen("CHOOSE A BATTLE COURSE");
            for (i = 0; i < 4; i++) number(40, 40 + i * 22, i + 1, i == course ? 0x03FF : 0x7FFF);
            if (trigger & A_BUTTON) goto launch;
            break;
        }
        sMenuReady = TRUE;
    }
launch:
    prepareRace(characters, mode, engine, course, retro);
    return TRUE;
lost:
    ring_stop();
    scene_setMainFunc(connectionError_main);
    return TRUE;
cancelled:
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
    u32 i;
    resetMenuDisplay();
    screen(sBattle ? "BATTLE RESULTS" : sGame == GP_MODE ? "GRAND PRIX POINTS" : "VS RESULTS");
    if (sGame == GP_MODE)
    {
        extern void sub_801E0B8(void);
        NativeResultRow* standings = sub_800EDDC()->rows.results;
        u32 id;
        /* Populate native racer totals and use the original eight-racer
         * standings builder, including its own tie and human/CPU rules. */
        for (i = 0; i < 8; i++)
            ((u8*)gSceneState.raceState.drivers[i])[0xC4] = sGPPoints[i];
        sub_801E0B8();
        for (i = 0; i < 8; i++)
        {
            for (id = 0; id < 8 && sCharacters[id] != standings[i].menuCharacter; id++);
            sPlaces[i] = id;
        }
        text(12, 136, sGPRace == 3 ? "CUP COMPLETE  A CHOOSE CUP" : "A NEXT RACE", 0x7FFF);
    }
    for (i = 0; i < (sGame == GP_MODE ? 8 : sRing.count); i++)
    {
        u32 id = sPlaces[i];
        int x = (i & 1) ? 126 : 8, y = 35 + (i / 2) * 26;
        if (id >= (sGame == GP_MODE ? 8 : sRing.count)) continue;
        number(x, y, i + 1, 0x7FFF);
        text(x + 18, y, sCharacterNames[sCharacters[id]], sPlayerColors[id]);
        text(x + 18, y + 10, sGame == GP_MODE ? "PTS" : "WINS", 0x7FFF);
        number(x + 50, y + 10, sGame == GP_MODE ? sGPPoints[id] : sWins[id], sPlayerColors[id]);
        if (sGame == GP_MODE && id >= sRing.count) text(x + 72, y + 10, "CPU", 0x7FFF);
    }
    sMenuReady = TRUE;
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
