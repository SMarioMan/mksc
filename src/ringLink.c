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

/* Serial transport owns timer 2 only. Audio owns timers 0/1; stock cable
 * transports keep timer 3. All software IDs travel through the cable. */
#define RING_DATA (*(vu32*)0x04000120)
#define RING_CONTROL 0x5000
#define RING_START 0x80
#define RING_INTERNAL 1
#define RING_INVALID 0xFF
#define RING_TURNAROUND 4096

enum { DISCOVER = 1, ANNOUNCE, FRAME, INPUT, COMMIT, ABORT, READY };
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
} RingState;
static volatile RingState sRing;
u16 ring_sControlWords[8];
u16 ring_sDriverInputs[8];
u8 ring_selectedCharacters[8];
extern u8 gSelectedCharacters[8];
extern u16 sio2_sKeyBuf[8];
enum { GAME, ENGINE, CHARACTERS, CUP, COURSE, ARENA };
static u8 sSession, sResume, sCharacters[8], sPlaces[8];
static u16 sWins[8];
static u8 sBattlePlaces[8], sEliminated;
static int sBattle, sEngine, sCourse, sRetro;
static bool32 results_main(void);
static bool32 connectionError_main(void);


static u8 checksum(u32 value)
{
    u32 byte, bit, c = 0;
    for (byte = 3; byte > 0; byte--)
    {
        c ^= (value >> (byte * 8)) & 0xFF;
        for (bit = 0; bit < 8; bit++)
            c = ((c & 0x80) ? (c << 1) ^ 7 : c << 1) & 0xFF;
    }
    return c;
}

static u32 packet(u32 type, u32 node, u32 sequence, u32 payload)
{
    u32 word = type << 28 | (node & 7) << 25 | (sequence & 15) << 21 | (payload & 8191) << 8;
    return word | checksum(word);
}

static void enqueue(u32 word)
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

static u32 dequeue(void)
{
    u32 word;
    if (!sRing.queued)
        return 0;
    word = sRing.queue[sRing.head];
    sRing.head = (sRing.head + 1) & 15;
    sRing.queued--;
    return word;
}

static void insertInput(void)
{
    u32 id = sRing.id;
    sRing.staged[id] = (~REG_KEYINPUT & 1023) | (*(vu16*)0x03004F50 & 0x1C00);
    sRing.seen |= 1 << id;
    enqueue(packet(INPUT, id, sRing.sequence, sRing.staged[id]));
}

static void beginFrame(void)
{
    sRing.seen = sRing.ownReturned = sRing.commitSent = sRing.complete = 0;
    sRing.startVideoFrame = gSceneState.field14;
    sRing.frameStarted = TRUE;
    enqueue(packet(FRAME, 0, sRing.sequence, 0));
    insertInput();
    sRing.requested = FALSE;
}

static void receive(u32 word)
{
    u32 type, node, seq, payload, i;
    if (!word || (word == 0xFFFFFFFF && !sRing.frameStarted))
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
        if (sRing.owner)
        {
            if (payload < 2 || payload > 8)
                goto invalid;
            sRing.count = payload;
            enqueue(packet(ANNOUNCE, 0, 0, payload));
        }
        else
        {
            if (payload >= 8 || sRing.inGame)
                goto invalid;
            sRing.head = sRing.tail = sRing.queued = sRing.seen = 0;
            sRing.sequence = sRing.count = 0;
            sRing.id = payload;
            enqueue(packet(DISCOVER, 0, 0, payload + 1));
        }
        break;
    case ANNOUNCE:
        if (payload < 2 || payload > 8 || sRing.id >= payload)
            goto invalid;
        sRing.count = payload;
        sio2_sMPlayerId.mPlayerId = sRing.id;
        sio2_sMPlayerId.playerCount = payload;
        sio2_sMPlayerId.field_16 = 0x8001;
        if (!sRing.owner)
            enqueue(word);
        break;
    case READY:
        /* READY packets may be lost while a console is still rendering.
         * Repeat them until every participant has entered this frame barrier.
         * Only then launch the input train. Old READY packets are harmless. */
        if (seq != sRing.sequence || !sRing.count || node >= sRing.count)
            break;
        if (sRing.owner)
            sRing.readySeen |= 1 << node;
        if (node != sRing.id)
            enqueue(word);
        if (sRing.owner && sRing.requested && !sRing.frameStarted && sRing.readySeen == (1 << sRing.count) - 1)
            beginFrame();
        break;
    case FRAME:
        if (!sRing.count || seq != sRing.sequence)
            goto invalid;
        if (!sRing.owner)
        {
            sRing.seen = sRing.complete = 0;
            sRing.frameStarted = TRUE;
            sRing.startVideoFrame = gSceneState.field14;
            enqueue(word);
            insertInput();
        }
        break;
    case INPUT:
        if (!sRing.count || seq != sRing.sequence || node >= sRing.count)
            goto invalid;
        if (node == sRing.id)
        {
            if (sRing.staged[node] != payload)
                goto invalid;
            if (sRing.owner)
                sRing.ownReturned = TRUE;
        }
        else
        {
            if (sRing.seen & (1 << node))
                goto invalid;
            sRing.staged[node] = payload;
            sRing.seen |= 1 << node;
            enqueue(word);
        }
        break;
    case COMMIT:
        if (!sRing.count || seq != sRing.sequence || sRing.seen != (1 << sRing.count) - 1)
            goto invalid;
        for (i = 0; i < 8; i++)
        {
            ring_sControlWords[i] = i < sRing.count ? sRing.staged[i] ^ 1023 : 1023;
            sio2_sKeyBuf[i] = ring_sControlWords[i] & 1023;
            if (i < 4)
                ((u16*)&sio2_sMPlayerId)[i + 1] = ring_sControlWords[i];
        }
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
    if (sRing.owner && sRing.ownReturned && !sRing.commitSent && sRing.seen == (1 << sRing.count) - 1)
    {
        enqueue(packet(COMMIT, 0, sRing.sequence, 0));
        sRing.commitSent = TRUE;
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
static void ring_serialHandler(void)
{
    u16 control = RING_CONTROL | (sRing.owner ? RING_INTERNAL : 0);
    u32 word;
    if (!sRing.enabled || !sRing.inFlight) return;
    word = RING_DATA;
    sRing.inFlight = FALSE;
    sRing.lastShift = REG_TM2CNT_L;
    sRing.packets++;
    receive(word);
    if (sRing.count && sRing.requested && !sRing.frameStarted && !(sRing.packets & 15))
        enqueue(packet(READY, sRing.id, sRing.sequence, 0));
    if (!sRing.owner && !sRing.failed)
    {
        REG_SIOCNT = control;
        RING_DATA = dequeue();
        sRing.inFlight = TRUE;
        REG_SIOCNT = control | RING_START;
    }
}

static void poll(void)
{
    u16 control = RING_CONTROL | (sRing.owner ? RING_INTERNAL : 0);
    if (sRing.inFlight || sRing.failed) return;
    if (sRing.owner)
    {
        if (sRing.complete) return;
        if ((u16)(REG_TM2CNT_L - sRing.lastShift) < RING_TURNAROUND) return;
    }
    REG_SIOCNT = control;
    RING_DATA = dequeue();
    sRing.inFlight = TRUE;
    REG_SIOCNT = control | RING_START;
}

u16* ring_peerWords(Sio2State* nativeState)
{
    return sRing.enabled ? ring_sControlWords : (u16*)nativeState + 1;
}

void ring_extendDriverInputs(void)
{
    extern u16 gKeyBuf[8], gKeyTriggerBuf[8];
    u32 i;
    static const u16 heldMasks[8] = {16,32,64,128,2,1,256,512};
    static const u16 actionMasks[8] = {256,512,1024,2048,16384,32768,16,64};
    for (i = 4; i < 8; i++)
    {
        u16 value = 0, held = gKeyBuf[i], trigger = gKeyTriggerBuf[i];
        u32 bit;
        if (sRing.enabled && i < sRing.count)
        {
            for (bit = 0; bit < 8; bit++)
                if (held & heldMasks[bit]) value |= actionMasks[bit];
            if (trigger & 256) value |= 4;
            if (trigger & 512) value |= 2;
            if (trigger & 4) value |= 8192;
            if (trigger & 8) value |= 4096;
        }
        ring_sDriverInputs[i] = value;
    }
}

void sio2_stockVblank(void);
void sio2_vblank(void)
{
    if (!sRing.enabled)
        sio2_stockVblank();
}

bool32 ring_transferring(void)
{
    return sRing.enabled && (sRing.requested || sRing.frameStarted);
}

bool32 ring_active(void)
{
    return sRing.enabled;
}

void loadtrack_stockStartGrid(RaceState* state);
void sub_8031ED8(RaceState* state);
int sub_8032990(RaceState* state, int x, int y, int layer);
void loadtrack_8031F9C(RaceState* state)
{
    u32 i, candidate;
    static const s16 offsets[4][2] = { {24,0}, {-24,0}, {0,24}, {0,-24} };
    /* Stock Vs. generates only two, three or four start positions. Tracks
     * already contain the eight-position Grand Prix grid. */
    if (sRing.enabled && sRing.count > 4 && (state->curRaceStateUnknown & 0x400))
        sub_8031ED8(state);
    else
        loadtrack_stockStartGrid(state);
    if (sRing.enabled && sRing.count > 4 && (state->curRaceStateUnknown & 0x800))
    {
        /* Battle arenas have four spawn markers. Place each additional kart
         * beside a stock spawn, rejecting void/lava tiles before using it. */
        for (i = 4; i < sRing.count; i++)
        {
            s32* base = (s32*)((u8*)state + 0x24 + (i - 4) * 16);
            s32* extra = (s32*)((u8*)state + 0x24 + i * 16);
            for (candidate = 0; candidate < 4; candidate++)
            {
                s32 x = base[0] + offsets[candidate][0] * 65536;
                s32 y = base[1] + offsets[candidate][1] * 65536;
                if (!(sub_8032990(state, x >> 16, y >> 16, 0) & 0x80))
                {
                    extra[0] = x;
                    extra[1] = y;
                    extra[2] = base[2];
                    break;
                }
            }
            if (candidate == 4) sRing.failed = TRUE;
        }
    }
}

void ring_stop(void)
{
    if (!sRing.enabled)
        return;
    irq_updateMask(IRQ_UPDATE_MODE_AND, ~IRQ_MASK_SIO);
    REG_SIOCNT = 0;
    REG_IF = IRQ_MASK_SIO;
    irq_setHandler(IRQ_ID_SIO, main_dummyIrqHandler);
    REG_TM2CNT_H = 0;
    sRing.enabled = FALSE;
    sio2_sMPlayerId.mPlayerId = -1;
    sio2_sMPlayerId.playerCount = 0;
    sio2_sMPlayerId.field_16 = 0;
}

static void start(bool32 owner)
{
    u32 i;
    u8* bytes = (u8*)&sRing;
    for (i = 0; i < sizeof(sRing); i++)
        bytes[i] = 0;
    irq_setHandler(IRQ_ID_SIO, ring_serialHandler);
    REG_IF = IRQ_MASK_SIO;
    sRing.enabled = TRUE;
    sRing.owner = owner;
    sRing.id = owner ? 0 : RING_INVALID;
    sRing.startVideoFrame = gSceneState.field14;
    REG_RCNT = 0;
    REG_TM2CNT_H = 0;
    REG_TM2CNT_L = 0;
    REG_TM2CNT_H = 0x80;
    irq_updateMask(IRQ_UPDATE_MODE_OR, IRQ_MASK_SIO | IRQ_MASK_IME);
    if (owner)
        enqueue(packet(DISCOVER, 0, 0, 1));
}

bool32 ring_exchange(void)
{
    u32 lastAudioFrame = gSceneState.field14;
    if (sRing.failed)
        return TRUE;
    REG_IME = 0;
    sRing.complete = FALSE;
    sRing.requested = TRUE;
    sRing.readySeen = 1 << sRing.id;
    sRing.frameStarted = FALSE;
    sRing.startVideoFrame = gSceneState.field14;
    if (sRing.count)
        enqueue(packet(READY, sRing.id, sRing.sequence, 0));
    REG_IME = 1;
    do
    {
        poll();
        if (gSceneState.field14 != lastAudioFrame)
        {
            m4aSoundMain();
            lastAudioFrame = gSceneState.field14;
        }
        if (gSceneState.field14 - sRing.startVideoFrame >= 30)
        {
            sRing.timeouts++;
            sRing.failed = TRUE;
        }
    } while (!sRing.complete && !sRing.failed);
    return sRing.failed;
}

static const u16 sPlayerColors[8] = { 0x001F, 0x03E0, 0x03FF, 0x7C1F, 0x7C00, 0x021F, 0x7FE0, 0x5014 };

/* Keep identity art in the unused BG HUD gap 2B00..2DFF. All eight
 * kart texture slots and both course-object banks remain untouched. */
static void badgeTiles(void)
{
    u16 tiles[256];
    u32 i, row, col;
    u16* palette = pltt_getBuffer(0);
    for (i = 0; i < 256; i++) tiles[i] = 0;
    for (i = 0; i < 8; i++)
    {
        palette[145 + i] = sPlayerColors[i];
        for (row = 0; row < 7; row++)
            for (col = 0; col < 5; col++)
                if (sFont[27 + i][row] & (16 >> col))
                    tiles[i * 16 + row * 2 + col / 4] |= (i + 1) << ((col % 4) * 4);
        for (row = 1; row < 7; row++)
            for (col = 1; col < 6; col++)
                if ((row > 1 && row < 5) || (row == 1 && col > 1 && col < 5) || (row == 5 && col > 1 && col < 5) || (row == 6 && col == 3))
                    tiles[128 + i * 16 + row * 2 + col / 4] |= (i + 1) << ((col % 4) * 4);
    }
    CpuCopy16(tiles, (void*)0x06002B00, sizeof(tiles));
    pltt_setDirtyFlag(0);
    /* Native kart/balloon palettes leave their first two entries reserved. */
    palette = pltt_getBuffer(1);
    for (i = 0; i < 8; i++)
    {
        palette[(8 + i) * 16 + 1] = sPlayerColors[i];
        palette[(8 + i) * 16 + 2] = (sPlayerColors[i] >> 1 & 0x3DEF) + 0x4210;
    }
    pltt_setDirtyFlag(1);
}

static void badge(int id, int x, int y)
{
    if ((unsigned)id < 8)
        *map_getBufferPtr2d(0, x, y) = 0x9158 + id;
}

int sub_8032D0C(RaceState* state);
static u8 sMarkerX[8], sMarkerY[8], sMarkerCount;
static void mapMarkers(RaceState* state)
{
    u16 tiles[128];
    u32 i, j, row, col, count = 0;
    int scale = sub_8032D0C(state) + 19;
    for (i = 0; i < sMarkerCount; i++)
        *map_getBufferPtr2d(0, sMarkerX[i], sMarkerY[i]) = 0x9170;
    for (i = 0; i < 128; i++) tiles[i] = 0;
    for (i = 0; i < sRing.count; i++)
    {
        s32* driver = (s32*)state->drivers[i];
        int x = 168 + (driver[0] >> scale), y = 88 + (driver[1] >> scale);
        if (x < 0 || y < 0 || x >= 240 || y >= 160) continue;
        for (j = 0; j < count; j++)
            if (sMarkerX[j] == x / 8 && sMarkerY[j] == y / 8) break;
        if (j == count)
        {
            sMarkerX[count] = x / 8; sMarkerY[count] = y / 8;
            *map_getBufferPtr2d(0, x / 8, y / 8) = 0x9168 + count++;
        }
        for (row = y & 7; row < 8 && row < (y & 7) + 2; row++)
            for (col = x & 7; col < 8 && col < (x & 7) + 2; col++)
            {
                u32 shift = (col % 4) * 4, at = j * 16 + row * 2 + col / 4;
                tiles[at] = (tiles[at] & ~(15 << shift)) | (i + 1) << shift;
            }
    }
    sMarkerCount = count;
    CpuCopy16(tiles, (void*)0x06002D00, sizeof(tiles));
}

void ring_stockBattleHud(RaceState* state);
void ring_stockVsHud(RaceState* state);
void ring_stockMapMarker(Driver* driver, int local);
extern const u8 _080EE6FA[];
int sub_8032D0C(RaceState* state);

static void playerHud(RaceState* state, int battle)
{
    u32 i, row, col;
    badgeTiles();
    for (i = 0; i < sRing.count; i++)
    {
        Driver* driver = battle ? state->drivers[i] : ((Driver**)((u8*)state + 0xC38))[i];
        u8* bytes = (u8*)driver;
        u32 character = bytes[0x4C] & 7;
        u32 id = bytes[0xEC];
        u32 tile = 0xD080 + _080EE6FA[character] * 4;
        int x = 1 + (i & 1) * 3;
        int y = 3 + (i / 2) * 3;
        for (row = 0; row < 2; row++)
            for (col = 0; col < 2; col++)
                *map_getBufferPtr2d(0, x + col, y + row) = tile++;
        if (!(state->raceOptions & 0x40))
        {
            badge(id, x - 1, y);
            if (battle)
            {
                u32 balloons = bytes[0x17D];
                /* Three small colored pips retain the stock balloon count. */
                for (col = 0; col < 3; col++)
                    *map_getBufferPtr2d(0, x + col, y + 2) = col < balloons ? 0x9160 + id : 0x9170;
            }
        }
    }
    mapMarkers(state);
    map_setBufferDirty(1);
}

void sub_804DCB0(RaceState* state)
{
    if (sRing.enabled) playerHud(state, TRUE);
    else ring_stockBattleHud(state);
}

void sub_804F2C0(RaceState* state)
{
    if (sRing.enabled) playerHud(state, FALSE);
    else ring_stockVsHud(state);
}

void sub_804E7EC(Driver* driver, int local)
{
    if (!sRing.enabled)
    {
        ring_stockMapMarker(driver, local);
        return;
    }
    /* All eight markers are composed together in playerHud. */
}
static const char* const sCharacterNames[8] = { "MARIO", "LUIGI", "PEACH", "TOAD", "DK", "YOSHI", "BOWSER", "WARIO" };
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

static unsigned glyph(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= '0' && c <= '9') return c - '0' + 26;
    return 36;
}

static void text(int x, int y, const char* string, u16 color)
{
    vu16* pixels = (vu16*)0x06000000;
    int row, col;
    while (*string)
    {
        const u8* bitmap = sFont[glyph(*string++)];
        for (row = 0; row < 7; row++)
            for (col = 0; col < 5; col++)
                if (bitmap[row] & (16 >> col))
                    pixels[(y + row) * 240 + x + col] = color;
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
            if (pixel) ((vu16*)0x06000000)[(y + row) * 240 + x + col] = _08062774[48 + pixel];
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
    vu16* pixels = (vu16*)0x06000000;
    DMA_FILL(3, 0x7FA0, (void*)pixels, 240 * 26 * 2, 16);
    DMA_FILL(3, 0x4108, (void*)(pixels + 240 * 26), 240 * (160 - 26) * 2, 16);
    text(12, 10, heading, 0x001F);
    text(12, 148, "A CONFIRM    B BACK", 0x7FFF);
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

void ring_drawTitleOption(int y, bool32 selected)
{
    extern const u16* off_80D8E4C[];
    static const u16 cellsSelected[] = { 2, 0x40F8, 0x81E0, 0x93F0, 0x40F8, 0x8000, 0x93F8 };
    static const u16 cellsNormal[] = { 2, 0x40F8, 0x81E0, 0xA3F0, 0x40F8, 0x8000, 0xA3F8 };
    /* R from PLAYER, ING from SINGLE, LINK from SINGLE-PAK LINK. */
    static const s16 slices[3][4] = { {0,31,8,3}, {0,-32,18,11}, {5,23,27,33} };
    u16 tiles[256];
    int i, row, col, x, offset;
    Vec2s16 position;
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

static void prepareRace(const u8* characters, int battle, int engine, int course, int retro)
{
    RaceState* state = &gSceneState.raceState;
    u32 i;
    for (i = 0; i < 8; i++)
    {
        sCharacters[i] = characters[i];
        sBattlePlaces[i] = 0xFF;
    }
    sEliminated = 0;
    sMarkerCount = 0;
    sBattle = battle; sEngine = engine; sCourse = course; sRetro = retro;
    scene_setVBlankFunc(0);
    sub_8048C74(state);
    *(vu8*)0x0300000C = 3;
    sub_8015268(0);
    sub_801528C(0);
    sub_80152F8(0);
    sub_80152D4(0);
    state->humanCount = sRing.count;
    state->localPlayerId = sRing.id;
    state->curRaceStateUnknown = battle ? 0x800 : 0x400;
    state->engineClass = engine;
    for (i = 0; i < sRing.count; i++)
    {
        int character = spm_menuCharIdxToCharId(characters[i]);
        gSelectedCharacters[i] = character;
        sub_804AE58(state, character, 1);
    }
    state->headerTableIdx = battle ? track_getBattleTrack(course) :
                           retro ? track_getRetroTrack(course) : track_getMKSCTrack(course);
    *(vu32*)0x03000008 = course;
    sRing.inGame = TRUE;
    scene_setMainFunc(race_main);
}

bool32 ring_main(void)
{
    int phase = sResume ? sResume : GAME;
    int battle = sResume ? sBattle : 0, engine = sResume ? sEngine : 0;
    int cup = sResume ? sCourse / 4 : 0, course = sResume ? sCourse : 0;
    int retro = sResume ? sRetro : 0;
    u8 characters[8], ready = 0, ignoreStart = !sResume;
    u16 previous = 0, keys, trigger;
    u32 i, lastAudioFrame;
    extern u16 gKeyTriggerBuf[8], gKeyBuf[8];
    scene_setVBlankFunc(0);
    DmaStop(0);
    vcount_reset();
    irq_updateMask(IRQ_UPDATE_MODE_SET, IRQ_MASK_VBLANK | IRQ_MASK_CARTRIDGE | IRQ_MASK_IME | (sRing.enabled ? IRQ_MASK_SIO : 0));
    oam_802FE4C(FALSE);
    trns_init();
    REG_DISPCNT = 0x403;
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
    if (sResume && ring_active()) goto connected;
    sSession = TRUE;
    for (i = 0; i < 8; i++) sWins[i] = 0;
    screen("RING LINK");
    text(12, 42, "CONNECT ALL GBAS IN A RING", 0x7FFF);
    text(12, 60, "PRESS START ON ONE CONSOLE", 0x03FF);
    text(12, 78, "WAITING FOR OTHER PLAYERS", 0x7FFF);
    /* Discovery polls without the normal frame loop. Continue servicing
     * MP2000 every VBlank so the last title sample cannot loop indefinitely. */
    m4aMPlayAllStop();
    lastAudioFrame = gSceneState.field14;
    start(FALSE);
    while (!sRing.count && !sRing.failed)
    {
        if (gSceneState.field14 != lastAudioFrame)
        {
            m4aSoundMain();
            lastAudioFrame = gSceneState.field14;
        }
        keys = ~REG_KEYINPUT & 1023;
        trigger = keys & ~previous;
        previous = keys;
        if (trigger & B_BUTTON) goto cancelled;
        if ((trigger & START_BUTTON) && sRing.id == RING_INVALID)
        {
            REG_SIOCNT = RING_CONTROL;
            sRing.inFlight = FALSE;
            sRing.owner = TRUE;
            sRing.id = 0;
            sRing.startVideoFrame = gSceneState.field14;
            sRing.lastShift = REG_TM2CNT_L;
            enqueue(packet(DISCOVER, 0, 0, 1));
        }
        poll();
        if (gSceneState.field14 - sRing.startVideoFrame > 180 && sRing.owner)
            sRing.failed = TRUE;
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
        /* Starting discovery must not also confirm the first game choice. */
        if (ignoreStart)
        {
            trigger &= ~START_BUTTON;
            if (!(gKeyBuf[0] & START_BUTTON)) ignoreStart = FALSE;
        }
        if (phase != CHARACTERS && (trigger & B_BUTTON))
        {
            if (phase == GAME) goto cancelled;
            if (phase == ENGINE) phase = GAME;
            else if (phase == COURSE) phase = CUP;
            else { phase = CHARACTERS; ready = 0; }
        }
        switch (phase)
        {
        case GAME:
            if (trigger & (DPAD_UP | DPAD_DOWN)) battle ^= 1;
            if (trigger & (A_BUTTON | START_BUTTON)) phase = battle ? CHARACTERS : ENGINE;
            screen("CHOOSE A GAME");
            text(30, 55, "VS", battle ? 0x7FFF : 0x03FF);
            text(30, 85, "BATTLE", battle ? 0x03FF : 0x7FFF);
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
                phase = battle ? GAME : ENGINE;
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
            if (ready == (1 << sRing.count) - 1) phase = battle ? ARENA : CUP;
            break;
        case CUP:
            if ((trigger & DPAD_UP) && cup > 0) cup--;
            if ((trigger & DPAD_DOWN) && cup < 4) cup++;
            if (trigger & SELECT_BUTTON) retro ^= 1;
            if (trigger & A_BUTTON) { course = cup * 4; phase = COURSE; }
            screen("CHOOSE A CUP");
            for (i = 0; i < 5; i++) text(24, 35 + i * 18, sCupNames[i], i == cup ? 0x03FF : 0x7FFF);
            text(144, 132, retro ? "EXTRA" : "SUPER", 0x7FFF);
            break;
        case COURSE:
            if ((trigger & DPAD_UP) && course > cup * 4) course--;
            if ((trigger & DPAD_DOWN) && course < cup * 4 + 3) course++;
            screen("CHOOSE A TRACK");
            for (i = 0; i < 4; i++)
            {
                if (!retro) text(20, 40 + i * 22, (retro ? sRetroTracks : sTracks)[cup * 4 + i], cup * 4 + i == course ? 0x03FF : 0x7FFF);
                else number(40, 40 + i * 22, cup * 4 + i + 1, cup * 4 + i == course ? 0x03FF : 0x7FFF);
            }
            if (trigger & A_BUTTON) goto launch;
            break;
        case ARENA:
            if ((trigger & DPAD_UP) && course > 0) course--;
            if ((trigger & DPAD_DOWN) && course < 3) course++;
            screen("CHOOSE A BATTLE COURSE");
            for (i = 0; i < 4; i++) number(40, 40 + i * 22, i + 1, i == course ? 0x03FF : 0x7FFF);
            if (trigger & A_BUTTON) goto launch;
            break;
        }
    }
launch:
    prepareRace(characters, battle, engine, course, retro);
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
    for (i = 0; i < sRing.count; i++)
    {
        u8* driver = (u8*)state->drivers[i];
        if (sBattle)
        {
            if (driver[0x17D]) alive++;
            else if (sBattlePlaces[i] == 0xFF)
                sBattlePlaces[i] = sRing.count - 1 - sEliminated++;
        }
        else if (driver[0xC7] >= 0x83) finished++;
    }
    if ((sBattle && alive <= 1) || (!sBattle && finished >= sRing.count - 1))
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
        prepareRace(sCharacters, sBattle, sEngine, sCourse, sRetro);
    else if (choice == 5)
    {
        sSession = FALSE;
        ring_stop();
        scene_setMainFunc(title_main);
    }
    else if (choice == 2 || choice == 3 || choice == 4)
    {
        sResume = choice == 4 ? CHARACTERS : sBattle ? ARENA : choice == 3 ? CUP : COURSE;
        scene_setMainFunc(ring_main);
    }
    else
    {
        for (i = 0; i < sRing.count; i++)
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
        if (sPlaces[0] < sRing.count) sWins[sPlaces[0]]++;
        scene_setMainFunc(results_main);
    }
    return TRUE;
}

static void resetMenuDisplay(void)
{
    scene_setVBlankFunc(0);
    DmaStop(0);
    vcount_reset();
    irq_updateMask(IRQ_UPDATE_MODE_SET, IRQ_MASK_VBLANK | IRQ_MASK_CARTRIDGE | IRQ_MASK_IME | (sRing.enabled ? IRQ_MASK_SIO : 0));
    oam_802FE4C(FALSE);
    trns_init();
    REG_DISPCNT = 0x403;
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
    screen(sBattle ? "BATTLE RESULTS" : "VS RESULTS");
    for (i = 0; i < sRing.count; i++)
    {
        u32 id = sPlaces[i];
        int x = (i & 1) ? 126 : 8, y = 35 + (i / 2) * 26;
        if (id >= sRing.count) continue;
        number(x, y, i + 1, 0x7FFF);
        text(x + 18, y, sCharacterNames[sCharacters[id]], sPlayerColors[id]);
        text(x + 18, y + 10, "WINS", 0x7FFF);
        number(x + 50, y + 10, sWins[id], sPlayerColors[id]);
    }
    while (1)
    {
        main_frameProc();
        if (!ring_active()) { scene_setMainFunc(connectionError_main); return TRUE; }
        if (gKeyTriggerBuf[0] & A_BUTTON)
        {
            sResume = sBattle ? ARENA : COURSE;
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
