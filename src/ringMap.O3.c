#include "common.h"
#ifdef MKSC_RING
#include "scene.h"
#include "oam.h"
#include "palette.h"
#include "vcount.h"
#include "map.h"

extern const u16 ring_playerColors[8];
int sub_8032D0C(RaceState* state);
static u16* sMapCourseTiles;
static bool32 sMapCaptureFailed;
static u16 sMapCoursePalette[16] ALIGNED(4);

static const u16 sMapLabels[8][64] ALIGNED(4) = {
#include "ringMapLabels.inc"
};

void ring_resetMapGraphics(void)
{
    sMapCourseTiles = 0;
    sMapCaptureFailed = FALSE;
}

/* Perspective road tiles are a moving cache, rather than a complete course.
 * Preserve the native 64x64 overview before results graphics reuse its VRAM. */
static bool32 cacheCourseOverview(RaceState* state)
{
    const vu16* source = (const vu16*)0x06002000;
    u32 i;
    sMapCourseTiles = frmheap_calloc(&state->frameHeap, 65, 64);
    if (!sMapCourseTiles) return FALSE;
    for (i = 0; i < 1024; i++)
    {
        u32 packed = source[i], a = packed & 15, b = (packed >> 4) & 15;
        u32 c = (packed >> 8) & 15, d = packed >> 12;
        sMapCourseTiles[i * 2] = (a ? 176 + a : 0) | ((b ? 176 + b : 0) << 8);
        sMapCourseTiles[i * 2 + 1] = (c ? 176 + c : 0) | ((d ? 176 + d : 0) << 8);
    }
    /* Native frmheap_calloc clears only part of a reused allocation. The
     * padding tile must stay transparent even on later courses. */
    for (i = 2048; i < 2080; i++) sMapCourseTiles[i] = 0;
    CpuCopy16(pltt_getBuffer(0) + 176, sMapCoursePalette, sizeof(sMapCoursePalette));
    return TRUE;
}

/* Called immediately after the stock HUD loader decompresses this course's
 * overview. A prior course's HUD cells survive heap resets, so they cannot
 * serve as evidence that the next overview has reached VRAM. */
void ring_captureMapOverview(void)
{
    extern bool32 ring_isMapDisplay(void);
    if (!ring_isMapDisplay()) return;
    if (!sMapCourseTiles && !cacheCourseOverview(&gSceneState.raceState))
        sMapCaptureFailed = TRUE;
}

/* Display the complete native course overview with one north-up affine
 * transform. A 114/256 source step enlarges 64 pixels to almost 144 pixels. */
static void mapDisplayVblank(void)
{
    vu8* map = (vu8*)0x0600B000;
    u32 i;
    if (!sMapCourseTiles) return;
    DmaStop(0);
    REG_DISPSTAT_STRUCT->vcountIrq = FALSE;
    REG_IF = IRQ_MASK_VCOUNT;
    REG_BG2CNT = 0x1687;
    REG_BG2PA = 114;
    REG_BG2PB = 0;
    REG_BG2PC = 0;
    REG_BG2PD = 114;
    REG_BG2X = -(48 * 114);
    REG_BG2Y = -(8 * 114);
    REG_BLDCNT = 0;
    REG_WININ = REG_WINOUT = 0x3F3F;
    REG_DISPCNT = 0x1441;
    DmaCopy32(3, sMapCourseTiles, (void*)0x06004000, 65 * 64);
    /* Native result rows reuse this palette as well as the overview tiles. */
    DmaCopy16(3, sMapCoursePalette, (void*)0x05000160, sizeof(sMapCoursePalette));
    /* VRAM byte writes duplicate onto both lanes, so construct paired
     * affine map entries and write halfwords. Tile 64 is transparent. */
    for (i = 0; i < 256; i += 2)
    {
        u32 row = i / 16, column = i & 15;
        u32 tile = row < 8 && column < 8 ? row * 8 + column : 64;
        u32 next = row < 8 && column + 1 < 8 ? row * 8 + column + 1 : 64;
        *(vu16*)(map + i) = tile | (next << 8);
    }
    *(vu16*)PLTT = 0;
    /* Native HUD uploads can reuse these tiles. Restore the dedicated map
     * labels after the scene's DMA queue, without reserving racer graphics. */
    DmaCopy32(3, sMapLabels, (void*)(OBJ_VRAM0 + 0x7C00), sizeof(sMapLabels));
}

bool32 ring_prepareMapGraphics(void)
{
    RaceState* state = &gSceneState.raceState;
    u16* palette = pltt_getBuffer(1);
    u32 i, minimapShift;
    if (!state->trackData || !state->driverCount) return TRUE;
    if (sMapCaptureFailed) return FALSE;
    if (!sMapCourseTiles) return TRUE;
    /* Retire the perspective callback before resetting its list. Merely
     * clearing DISPSTAT leaves a pending VCOUNT IRQ able to call a null
     * list entry after VBlank. Keep the library's interrupt mask in sync. */
    REG_IME = 0;
    REG_IF = IRQ_MASK_VCOUNT;
    vcount_reset();
    irq_updateMask(IRQ_UPDATE_MODE_AND, ~IRQ_MASK_VCOUNT);
    minimapShift = 19 + sub_8032D0C(state);
    oam_update();
    oam_reset();
    for (i = 0; i < state->driverCount; i++)
    {
        Driver* driver = state->drivers[i];
        u16 cell[4];
        Vec2s16 position;
        if (!driver) continue;
        position.x = 48 + Div(driver->actor.posX >> (minimapShift - 8), 114) - 7;
        position.y = 8 + Div(driver->actor.posY >> (minimapShift - 8), 114) - 13;
        cell[0] = 1;
        cell[1] = 0;
        cell[2] = 0x4000;
        cell[3] = 0xF3E0 + 4 * i;
        oam_renderCellData(cell, &position, 0, 0, 0, 0);
        palette[15 * 16 + i + 1] = ring_playerColors[i];
    }
    palette[15 * 16 + 9] = 0;
    pltt_setDirtyFlag(TRUE);
    oam_update();
    scene_setVBlankFunc(mapDisplayVblank);
    return TRUE;
}
#endif
