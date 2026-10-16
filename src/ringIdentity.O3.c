#include "common.h"
#ifdef MKSC_RING
#include "ringLink.h"
#include "palette.h"
#include "libunk/irq.h"
#include "dmaQueue.h"
#include "ringSessionState.h"
#include "ringIdentityArt.h"

extern u16* oam_ringBuffer(void);
extern unsigned oam_ringObjectCount(void);

/* Virtual tile IDs occur only in freshly built identity cells. Native kart
 * bases are 0x200,0x240,...,0x3C0 and their texture slots stay untouched. */
static int identityIndex(u32 attr2)
{
    unsigned tag = (attr2 & 1023) - 0x3E0;
    if (tag >= 32 || attr2 >> 12 >= 8) return -1;
    return tag >= 16 ? tag - 8 : tag;
}

static void restoreTiles(u32 mask, const u16* offsets)
{
    unsigned index;
    for (index = 0; mask; index++, mask >>= 1)
        if (mask & 1)
            CpuFastSet(ring_identityBackup[index], (void*)(OBJ_VRAM0 + offsets[index] * 32), index < 16 ? 32 : 16);
}

void ring_restoreIdentityTiles(void)
{
    u16 ime;
    if (!ring_identityAllocated && !ring_identityPendingRestore) return;
    if ((REG_IE & IRQ_MASK_VBLANK) && REG_VCOUNT < 160) VBlankIntrWait();
    ime = REG_IME; REG_IME = 0;
    {
        vu16* oam = (vu16*)OAM;
        u16* original = pltt_getBuffer(PLTT_BUFFER_OBJ);
        unsigned object, color;
        for (object = 0; object < 128; object++)
            if ((oam[object * 4] & 0x2000) && oam[object * 4 + 2] >> 12 < 8) oam[object * 4] = 0x200;
        for (color = 1; color <= 10; color++) ((vu16*)OBJ_PLTT)[color * 16] = original[color * 16];
        pltt_setDirtyFlag(TRUE);
    }
    restoreTiles(ring_identityPendingRestore, ring_identityOldTiles);
    restoreTiles(ring_identityAllocated & ~ring_identityPendingUpload, ring_identityTiles);
    ring_identityAllocated = ring_identityPendingRestore = ring_identityPendingUpload = 0;
    REG_IME = ime;
}

/* A cached atlas costs no copying on stable frames. Predict only the pending
 * transfers when deciding whether a late publication still fits in VBlank. */
u32 ring_identityPublicationCost(void)
{
    u32 mask = ring_identityPendingUpload, cycles = 0;
    while (mask) { if (mask & 1) cycles += 1024; mask >>= 1; }
    mask = ring_identityPendingRestore;
    while (mask) { if (mask & 1) cycles += 512; mask >>= 1; }
    return cycles;
}

static void markTiles(u32* used, unsigned first, unsigned count)
{
    unsigned end;
    if (first >= 512 || !count) return;
    end = first + count;
    if (end > 512) end = 512;
    while (first < end)
    {
        unsigned word = first >> 5, limit = (word + 1) * 32;
        u32 bits = ~0U << (first & 31);
        if (limit > end) { limit = end; bits &= ~0U >> ((32 - end) & 31); }
        used[word] |= bits;
        first = limit;
    }
}

/* Stable frames only check for a native object/animation claiming a cache
 * tile. The full allocation pass is reserved for actual cache misses. */
static int intersectsResident(unsigned first, unsigned count)
{
    unsigned end;
    if (first >= 512 || !count) return FALSE;
    end = first + count;
    if (end > 512) end = 512;
    while (first < end)
    {
        unsigned word = first >> 5, limit = (word + 1) * 32;
        u32 bits = ~0U << (first & 31);
        if (limit > end) { limit = end; bits &= ~0U >> ((32 - end) & 31); }
        if (ring_identityResident[word] & bits) return TRUE;
        first = limit;
    }
    return FALSE;
}

static void patchIdentityObjects(u16* oam, unsigned count)
{
    unsigned object;
    for (object = 0; object < count; object++)
    {
        u16* cell = oam + object * 4;
        int identity = identityIndex(cell[2]);
        if (identity < 0 || (cell[0] & 0x300) == 0x200) continue;
        if (!(ring_identityAllocated & (1 << identity))) { cell[0] = 0x200; continue; }
        cell[0] |= 0x2000;
        cell[2] = (cell[2] & ~1023) | ring_identityTiles[identity];
    }
}

/* Prepare the finished OAM list in the game thread. Only a real cache miss
 * enters the larger allocator and its temporary bitmap. */
static const u8 widths[3][4] = {{1,2,4,8},{2,4,4,8},{1,1,2,4}};
static const u8 heights[3][4] = {{1,2,4,8},{1,1,2,4},{2,4,4,8}};

static void prepareIdentityCache(u16* oam, unsigned objects, u32 wanted) __attribute__((noinline));
static void prepareIdentityCache(u16* oam, unsigned objects, u32 wanted)
{
    u32 used[16], needed = 0, native8 = 0;
    DmaQueueEntry* dma;
    unsigned object, index;
    int cursor = 512;
    ring_identitySlowPrepares++;
    for (index = 0; index < 16; index++) used[index] = 0;
    for (object = 0; object < objects; object++)
    {
        u16* cell = oam + object * 4;
        u32 attr0 = cell[0], attr1 = cell[1], attr2 = cell[2];
        unsigned shape, size, count;
        int identity;
        if ((attr0 & 0x300) == 0x200) continue;
        identity = identityIndex(attr2);
        if (identity >= 0) { needed |= 1 << identity; continue; }
        if (attr0 & 0x2000) native8++;
        shape = attr0 >> 14; size = attr1 >> 14;
        if (shape >= 3) continue;
        count = widths[shape][size] * heights[shape][size];
        if (attr0 & 0x2000) count *= 2;
        markTiles(used, attr2 & 1023, count);
    }
    if (!needed && !ring_identityAllocated) return;
    ring_identityNative8bpp += native8;
    /* A native animation can DMA into an unused sprite slot before it is
     * drawn. Invalidate that cache entry too, preserving the new native art. */
    dma = (DmaQueueEntry*)dmaq_getVBlankDmaQueue()->list.head;
    while (dma)
    {
        u32 address = (u32)dma->dst, count = dma->config & 0xFFFF;
        u32 bytes = (count ? count : 65536) << ((dma->config & (DMA_32BIT << 16)) ? 2 : 1);
        if (address < OBJ_VRAM1 && address + bytes > OBJ_VRAM0)
        {
            u32 start = address < OBJ_VRAM0 ? OBJ_VRAM0 : address;
            u32 end = address + bytes;
            if (end > OBJ_VRAM1) end = OBJ_VRAM1;
            markTiles(used, (start - OBJ_VRAM0) / 32, (end - OBJ_VRAM0 + 31) / 32 - (start - OBJ_VRAM0) / 32);
        }
        dma = (DmaQueueEntry*)dma->link.next;
    }
    for (index = 0; index < 24; index++)
    {
        unsigned tile, count;
        u32 bits;
        if (!(ring_identityAllocated & (1 << index))) continue;
        tile = ring_identityTiles[index]; count = index < 16 ? 4 : 2;
        bits = ((1U << count) - 1) << (tile & 31);
        if (used[tile >> 5] & bits)
        {
            ring_identityPendingRestore |= 1 << index;
            ring_identityOldTiles[index] = tile;
            ring_identityAllocated &= ~(1 << index);
        }
        else used[tile >> 5] |= bits;
    }
    for (index = 0; index < 24; index++)
    {
        unsigned count = index < 16 ? 4 : 2;
        u32 bits;
        if (!needed || !(wanted & (1 << index)) || (ring_identityAllocated & (1 << index))) continue;
        cursor = (cursor - count) & ~(count - 1);
        while (cursor >= 0)
        {
            bits = ((1U << count) - 1) << (cursor & 31);
            if (!(used[cursor >> 5] & bits)) break;
            cursor -= count;
        }
        if (cursor < 0) { ring_identityOverflows++; continue; }
        ring_identityTiles[index] = cursor;
        ring_identityAllocated |= 1 << index;
        ring_identityPendingUpload |= 1 << index;
        used[cursor >> 5] |= bits;
    }
    ring_identityFirstTile = 512;
    for (index = 0; index < 16; index++) ring_identityResident[index] = 0;
    for (index = 0; index < 24; index++)
        if (ring_identityAllocated & (1 << index))
        {
            if (ring_identityTiles[index] < ring_identityFirstTile) ring_identityFirstTile = ring_identityTiles[index];
            markTiles(ring_identityResident, ring_identityTiles[index], index < 16 ? 4 : 2);
        }
    patchIdentityObjects(oam, objects);
}

/* Keep the DMA walk out of the sprite loop's register set. */
static int identityDmaCollision(void) __attribute__((noinline));
static int identityDmaCollision(void)
{
    DmaQueueEntry* dma = (DmaQueueEntry*)dmaq_getVBlankDmaQueue()->list.head;
    while (dma)
    {
        u32 address = (u32)dma->dst, count = dma->config & 0xFFFF;
        u32 bytes = (count ? count : 65536) << ((dma->config & (DMA_32BIT << 16)) ? 2 : 1);
        if (address < OBJ_VRAM1 && address + bytes > OBJ_VRAM0)
        {
            u32 start = address < OBJ_VRAM0 ? OBJ_VRAM0 : address;
            u32 end = address + bytes;
            if (end > OBJ_VRAM1) end = OBJ_VRAM1;
            if (intersectsResident((start - OBJ_VRAM0) / 32, (end - OBJ_VRAM0 + 31) / 32 - (start - OBJ_VRAM0) / 32)) return TRUE;
        }
        dma = (DmaQueueEntry*)dma->link.next;
    }
    return FALSE;
}

/* A rare native tile claim restarts allocation. Undo only objects patched in
 * this pass, so even a future native 8-bpp object is never mistaken for ours. */
static void unpatchIdentityObjects(u16* oam, unsigned count, const u32* patched) __attribute__((noinline));
static void unpatchIdentityObjects(u16* oam, unsigned count, const u32* patched)
{
    unsigned object;
    for (object = 0; object < count; object++)
        if (patched[object >> 5] & (1U << (object & 31)))
        {
            u16* cell = oam + object * 4;
            unsigned shape = cell[0] >> 14, player = cell[2] >> 12;
            unsigned tag = shape == 1 ? 0x3E0 : shape == 2 ? 0x3E8 : 0x3F8;
            cell[0] &= ~0x2000;
            cell[2] = (cell[2] & ~1023) | (tag + player);
        }
}

void ring_prepareIdentityGraphics(void)
{
    u16* oam;
    unsigned object, objects;
    u32 wanted;
    if (!ring_showRaceIdentity()) return;
    oam = oam_ringBuffer(); objects = oam_ringObjectCount();
    wanted = ring_identityWanted();
    if (ring_identityAllocated == wanted && !ring_identityPendingUpload && !ring_identityPendingRestore && !identityDmaCollision())
    {
        u32 patched[4];
        for (object = 0; object < 4; object++) ((volatile u32*)patched)[object] = 0;
        for (object = 0; object < objects; object++)
        {
            u16* cell = oam + object * 4;
            u32 attr0 = cell[0], attr2 = cell[2];
            unsigned shape, count;
            int identity;
            if ((attr0 & 0x300) == 0x200) continue;
            /* No 4-bpp object spans more than 64 tiles. Most native HUD and
             * particle sprites therefore cannot reach this high atlas. */
            if (!(attr0 & 0x2000) && (attr2 & 1023) + 64 <= ring_identityFirstTile) continue;
            identity = identityIndex(attr2);
            if (identity >= 0)
            {
                cell[0] = attr0 | 0x2000;
                cell[2] = (attr2 & ~1023) | ring_identityTiles[identity];
                patched[object >> 5] |= 1U << (object & 31);
                continue;
            }
            if ((attr2 & 1023) >= 512) continue;
            shape = attr0 >> 14;
            if (shape >= 3) continue;
            count = widths[shape][cell[1] >> 14] * heights[shape][cell[1] >> 14];
            if (attr0 & 0x2000) { count *= 2; ring_identityNative8bpp++; }
            if (intersectsResident(attr2 & 1023, count))
            {
                unpatchIdentityObjects(oam, object, patched);
                prepareIdentityCache(oam, objects, wanted);
                return;
            }
        }
        return;
    }
    prepareIdentityCache(oam, objects, wanted);
}

void ring_beginIdentityPublication(void)
{
    u32 started = REG_VCOUNT, elapsed;
    restoreTiles(ring_identityPendingRestore, ring_identityOldTiles);
    ring_identityPendingRestore = 0;
    elapsed = REG_VCOUNT;
    if (elapsed < started) elapsed += 228;
    elapsed -= started;
    if (elapsed > ring_identityRestoreLines) ring_identityRestoreLines = elapsed;
}

void ring_publishIdentityPalette(void)
{
    static const u16 colors[8] = {31,992,1023,31775,31744,543,32736,20500};
    u32 mask = ring_identityPendingUpload, index, started, elapsed;
    vu16* palette;
    if (!ring_showRaceIdentity()) { ring_publishNativeMenuPalette(); return; }
    started = REG_VCOUNT;
    for (index = 0; mask; index++, mask >>= 1)
        if (mask & 1)
        {
            u32 count = index < 16 ? 32 : 16;
            ring_identityUploads++;
            void* target = (void*)(OBJ_VRAM0 + ring_identityTiles[index] * 32);
            CpuFastSet(target, ring_identityBackup[index], count);
            CpuFastSet(sIdentityArt[index], target, count);
        }
    ring_identityPendingUpload = 0;
    palette = (vu16*)OBJ_PLTT;
    /* Nonzero multiples of 16 are opaque for these 8-bpp sprites, and remain
     * transparent for every native 4-bpp sprite. Wheel/kart colors stay native. */
    for (index = 0; index < 8; index++) palette[(index + 1) * 16] = colors[index];
    palette[144] = 0x7FFF; palette[160] = 0;
    pltt_setDirtyFlag(TRUE);
    elapsed = REG_VCOUNT;
    if (elapsed < started) elapsed += 228;
    elapsed -= started;
    if (elapsed > ring_identityPublishLines) ring_identityPublishLines = elapsed;
}
#endif
