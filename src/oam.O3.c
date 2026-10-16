#include "common.h"
#include "math.h"
#include "oam.h"
#include "ringLink.h"

const int sObjHalfWidth[3][4] = { { 4, 8, 16, 32 }, { 8, 16, 16, 32 }, { 4, 4, 8, 16 } };
const int sObjHalfHeight[3][4] = { { 4, 8, 16, 32 }, { 4, 4, 8, 16 }, { 8, 16, 16, 32 } };

static int sBufferOffset;
static int sNextAffineId;
static u16 sBuffer[128 * 4];
static OamCellListEntry sCellListEntries[32];
#ifdef MKSC_RING
extern OamCellListEntry* ring_sOamCells;
#define OAM_CELL_ENTRIES (ring_sOamCells ? ring_sOamCells : sCellListEntries)
#define OAM_CELL_LAST (ring_sOamCells ? 127 : 31)
#else
#define OAM_CELL_ENTRIES sCellListEntries
#define OAM_CELL_LAST 31
#endif
static OamCellListEntry* sCellListHead;
static OamCellListEntry* sCellListTail;
static int sCellListEntryCount;
static bool32 sCopyDisabled;
static bool32 oam_3002188;
static bool32 oam_300218C;

bool32 oam_renderCell(OamCell* cell);
void oam_renderCellData(const u16* cellData, Vec2s16* position, int scaleX, int scaleY, int rotation, struc_6* a6);
#ifdef MKSC_RING
OamCellListEntry* oam_802FC48(OamCellListEntry* a1) __attribute__((section(".ring_irq")));
#else
OamCellListEntry* oam_802FC48(OamCellListEntry* a1);
#endif
void oam_init(void);
void oam_update(void);
void oam_reset(void);
void oam_dmaCopyToHw(void);
void oam_cpuCopyToHw(void);

inline void oam_802FE4C(bool32 value)
{
    oam_3002188 = value;
}

void oam_renderMipmapCellDataUniform(const OamMipmapCellData* a1, Vec2s16* position, int scale, struc_6* a4);
void oam_renderMipmapCellData(const OamMipmapCellData* a1, Vec2s16* position, int mipmapScale, int scaleX, int scaleY,
                              int rotation, struc_6* a7);
void oam_renderCellDataSimple(const u16* cellData, int x, int y, int scale, struc_6* a6);
bool32 sub_802FF58(Vec2s16* a1, s16 a2);

inline int oam_bufferCellAffineMtx(OamCell* cell)
{
    if (sNextAffineId > 31)
        return -1;

    (sBuffer + 3)[0x10 * sNextAffineId] = cell->affinePa;
    (sBuffer + 7)[0x10 * sNextAffineId] = cell->affinePb;
    (sBuffer + 11)[0x10 * sNextAffineId] = cell->affinePc;
    (sBuffer + 15)[0x10 * sNextAffineId] = cell->affinePd;
    return sNextAffineId++;
}

#ifdef MKSC_RING
static void ring_renderAllCells(void);
#endif

inline void oam_renderAllCells(void)
{
    OamCellListEntry* listEntry;
#ifdef MKSC_RING
    if (ring_active()) { ring_renderAllCells(); return; }
#endif

    if (!sCellListHead)
        return;

    listEntry = sCellListHead;
    while (1)
    {
        oam_renderCell(&listEntry->cell);
        if (!listEntry->next)
            break;
        listEntry = listEntry->next;
    }
}

#ifndef NONMATCHING
asm_unified(".include \"nonmatching/text0802F6BC.s\"");
#ifdef MKSC_RING
bool32 ring_stockRenderCell(OamCell* cell);
s32 ring_spriteDivide(s32 numerator, s32 denominator);
extern u8 ring_balloonAffineIndex[8];
typedef struct
{
    u16 scaleX, scaleY;
    s16 x, y;
} BalloonOffsetCache;

/* Most scaled game objects contain one sprite. Avoid the general renderer's
 * repeated buffer/mask loads while retaining its native coordinate rounding. */
static bool32 ring_renderSingleScaledCell(OamCell* cell, BalloonOffsetCache* cache, bool32 defaultMasks)
{
    const u16* data = cell->cellData + 1;
    u32 attr0 = data[0], attr1 = data[1], attr2 = data[2];
    int offset = sBufferOffset, affine = sNextAffineId;
    int shape = attr0 >> 14, size = attr1 >> 14;
    int halfWidth = sObjHalfWidth[shape][size], halfHeight = sObjHalfHeight[shape][size];
    int x = attr1 & 511, y = (s8)(attr0 & 255), flags, reused = 0, id = -1;
    u32 packed;
    if (offset > 511) return -1;
    if (attr1 & 0x1000) { cell->affinePa = -cell->affinePa; cell->affinePb = -cell->affinePb; }
    if (attr1 & 0x2000) { cell->affinePc = -cell->affinePc; cell->affinePd = -cell->affinePd; }
    if ((attr2 & 1023) >= 0x3E8 && (attr2 & 1023) < 0x3F0 && (attr2 >> 12) < 8)
    {
        int previous;
        id = attr2 >> 12;
        previous = ring_balloonAffineIndex[id];
        if (previous < sNextAffineId &&
            sBuffer[3 + 16 * previous] == (u16)cell->affinePa &&
            sBuffer[7 + 16 * previous] == (u16)cell->affinePb &&
            sBuffer[11 + 16 * previous] == (u16)cell->affinePc &&
            sBuffer[15 + 16 * previous] == (u16)cell->affinePd)
        {
            affine = previous;
            reused = 1;
        }
    }
    if (affine > 31) return -1;
    if (!reused)
    {
        sBuffer[3 + 16 * affine] = cell->affinePa;
        sBuffer[7 + 16 * affine] = cell->affinePb;
        sBuffer[11 + 16 * affine] = cell->affinePc;
        sBuffer[15 + 16 * affine] = cell->affinePd;
        sNextAffineId = affine + 1;
        if (id >= 0) ring_balloonAffineIndex[id] = affine;
    }
    flags = cell->scaleX < 256 || cell->scaleY < 256 ? 0x300 : 0x100;
    if (cache && id >= 0 && cache[id].scaleX == cell->scaleX && cache[id].scaleY == cell->scaleY)
    {
        x = cache[id].x; y = cache[id].y;
    }
    else
    {
        if (x > 255) x = -(x ^ 511);
        x += halfWidth;
        y += halfHeight;
        x = (attr1 & 511) + ring_spriteDivide(x * 256, cell->scaleX) - x;
        y = (attr0 & 255) + ring_spriteDivide(y * 256, cell->scaleY) - y;
        if (flags == 0x300) { x -= halfWidth; y -= halfHeight; }
        if (cache && id >= 0)
        {
            cache[id].scaleX = cell->scaleX; cache[id].scaleY = cell->scaleY;
            cache[id].x = x; cache[id].y = y;
        }
    }
    attr0 = ((y + cell->offsetY) & 255) | (attr0 & 0xFF00);
    attr1 = ((x + cell->offsetX) & 511) | (affine << 9) | (attr1 & 0xC000);
    packed = attr0 | (attr1 << 16);
    if (defaultMasks)
    {
        *(u32*)&sBuffer[offset] = (packed & ~0x300) | flags;
        sBuffer[offset + 2] = attr2;
    }
    else
    {
        *(u32*)&sBuffer[offset] = (((packed & cell->attr01Mask) | cell->attr01Bits) & ~0x300) | flags;
        sBuffer[offset + 2] = attr2;
        *(u32*)&sBuffer[offset + 2] = (*(u32*)&sBuffer[offset + 2] & cell->attr2Mask) | cell->attr2Bits;
    }
    sBufferOffset = offset + 4;
    return 0;
}

static void ring_renderAllCells(void)
{
    BalloonOffsetCache cache[8];
    OamCellListEntry* entry = sCellListHead;
    u32 i;
    for (i = 0; i < 8; i++) { cache[i].scaleX = 0; cache[i].scaleY = 0; }
    ring_serviceRenderAudio();
    while (entry)
    {
        OamCell* cell = &entry->cell;
        const u16* data = cell->cellData;
        if ((cell->scaleX != 256 || cell->scaleY != 256 || cell->affinePa != 256 || cell->affinePb) &&
            data[0] == 1 && (data[1] >> 14) < 3)
            /* Queued cells are created only by oam_renderCellData, which
             * initializes their masks to all ones and their bits to zero.
             * Sorting changes links and priority, never these masks. */
            ring_renderSingleScaledCell(cell, cache, TRUE);
        else if (cell->scaleX == 256 && cell->scaleY == 256 && cell->affinePa == 256 && !cell->affinePb)
        {
            int count = *data++, offset = sBufferOffset;
            /* Queued cells have the factory's default masks. Emit flat
             * markers/particles directly, preserving affine matrix words. */
            while (count-- > 0 && offset <= 511)
            {
                u32 attr0 = *data++, attr1 = *data++;
                *(u32*)&sBuffer[offset] = ((((attr0 + cell->offsetY) & 255) | (attr0 & 0xFF00)) |
                    ((((attr1 + cell->offsetX) & 511) | (attr1 & 0xFE00)) << 16)) & ~0x300;
                sBuffer[offset + 2] = *data++;
                offset += 4;
            }
            sBufferOffset = offset;
        }
        else oam_renderCell(cell);
        entry = entry->next;
    }
}
bool32 oam_renderCell(OamCell* cell)
{
    const u16* data = cell->cellData;
    int count, offset = sBufferOffset;
    if (!ring_active()) return ring_stockRenderCell(cell);
    if (cell->scaleX != 256 || cell->scaleY != 256 || cell->affinePa != 256 || cell->affinePb)
    {
        if (data[0] == 1 && (data[1] >> 14) < 3)
            return ring_renderSingleScaledCell(cell, 0, FALSE);
        return ring_stockRenderCell(cell);
    }
    if (offset > 511) return -1;
    count = *data++;
    while (count-- > 0 && offset <= 511)
    {
        u32 attr0 = *data++, attr1 = *data++, attr2 = *data++;
        u32 packed = (((attr0 + cell->offsetY) & 255) | (attr0 & 0xFF00))
                   | ((((attr1 + cell->offsetX) & 511) | (attr1 & 0xFE00)) << 16);
        *(u32*)&sBuffer[offset] = ((packed & cell->attr01Mask) | cell->attr01Bits) & ~0x300;
        sBuffer[offset + 2] = attr2;
        *(u32*)&sBuffer[offset + 2] = (*(u32*)&sBuffer[offset + 2] & cell->attr2Mask) | cell->attr2Bits;
        offset += 4;
    }
    sBufferOffset = offset;
    return 0;
}
#endif

#else
bool32 oam_renderCell(OamCell* cell)
{
    int offsetX;
    int offsetY;
    int v34;
    int scaleX;
    int scaleY;
    int v37;
    const u16* cellData;
    u16 new_var11;
    int new_var18;
    u32 new_var12;
    u32 new_var17;
    int v3_3;
    const u16* v2;
    int v3;
    int v3_2;
    vu32* v5;
    vu32* v5_2;
    vu32* v5_4;
    vu32* v5_3;
    int v8;
    int v9;
    u32 new_var7;
    int shape;
    int v11;
    int size;
    int halfHeight;
    int v21;
    int v22;
    int v24;
    int halfWidth;
    int v26;
    int new_var16;
    int new_var14;
    int new_var6;
    u32 attr0;
    int new_var5;
    u32 attr2;
    int new_var8;
    int new_var3;
    u32 attr1_2;
    int new_var10;
    u32 attr1_3;
    int attr2_2;
    int new_var13;
    int new_var2;
    int new_var9;
    u16* ptr5;
    unsigned int new_var15;
    int x;
    s16 x2;
    u32 attr1;
    int y;
    int y2;
    int y3;
    int y6;
    int x3;
    s16 y4;
    u32 foo;
    int tmp2;
    u16* new_var4;
    u16 tmp;
    OamCell* new_var;
    v2 = cell->cellData;
    offsetX = cell->offsetX;
    offsetY = cell->offsetY;
    v34 = *(v2++);
    scaleX = cell->scaleX;
    scaleY = cell->scaleY;
    if (sBufferOffset > 511)
    {
        return -1;
    }

    if ((((scaleX == 256) && (scaleY == 256)) && (cell->affinePa == 256)) && (cell->affinePb == 0))
    {
        for (; v34 > 0; v34--)
        {
            if (sBufferOffset > 511)
            {
                return 0;
            }

            attr1 = *(v2++);
            sBuffer[sBufferOffset] = ((attr1 + offsetY) & 0xFF) | (attr1 & 0xFF00);
            attr1 = *(v2++);
            (sBuffer + 1)[sBufferOffset] = ((attr1 + offsetX) & 0x1FF) | (attr1 & 0xFE00);
            ((vu32*)sBuffer)[sBufferOffset / 2] &= cell->attr01Mask;
            ((vu32*)sBuffer)[sBufferOffset / 2] |= cell->attr01Bits;
            ((vu32*)sBuffer)[sBufferOffset / 2] &= ~0x300;
            attr1 = *(v2++);
            (sBuffer + 2)[sBufferOffset] = attr1;
            ((vu32*)(sBuffer + 2))[sBufferOffset / 2] &= cell->attr2Mask;
            ((vu32*)(sBuffer + 2))[sBufferOffset / 2] |= cell->attr2Bits;
            sBufferOffset += 4;
        }
    }
    else
    {
        attr1 = v2[1];
        v8 = (attr1 & 0x1000) ? (-1) : (1);
        v9 = (attr1 & 0x2000) ? (-1) : (1);
        cell->affinePa *= v8;
        cell->affinePb *= v8;
        cell->affinePc *= v9;
        cell->affinePd *= v9;
        v3_3 = oam_bufferCellAffineMtx(cell);
        if (v3_3 < 0)
        {
            return -1;
        }

        if ((scaleX < 256) || (scaleY < 256))
        {
            new_var17 = 768;
        }
        else
        {
            new_var17 = 256;
        }

        for (; v34 > 0; v34--)
        {
            shape = v2[0] >> 14;
            size = v2[1] >> 14;
            if (sBufferOffset > 511)
            {
                return 0;
            }

            attr1 = *(v2++);
            y2 = attr1;
            y2 = y2 & 0xFF;
            v22 = (s8)y2;
            y3 = v22 + sObjHalfHeight[shape][size];
            halfHeight = sObjHalfHeight[shape][size];
            v22 = y3 << 8;
            v22 = Div(v22, scaleY);
            v22 = y2 + (v22 - y3);
            if (new_var17 != 256)
            {
                v22 -= halfHeight;
            }

            y4 = offsetY;
            *(&sBuffer[sBufferOffset]) = ((v22 + y4) & 0xFF) | (attr1 & (~0xFF));
            attr1 = *(v2++);
            y2 = attr1 & 511;
            v22 = (y2 > 0xFF) ? (-(y2 ^ 511)) : (y2);
            y3 = v22 + sObjHalfWidth[shape][size];
            halfHeight = sObjHalfWidth[shape][size];
            v22 = y3 << 8;
            v22 = Div(v22, scaleX);
            v22 = y2 + (v22 - y3);
            if (new_var17 != 256)
            {
                v22 -= halfHeight;
            }

            y4 = offsetX;
            (sBuffer + 1)[sBufferOffset] = (((y4 + v22) & 0x1FF) | ((v3_3 << 9) & 0x3E00)) | (attr1 & 0xC000);
            ((vu32*)sBuffer)[sBufferOffset / 2] &= cell->attr01Mask;
            ((vu32*)sBuffer)[sBufferOffset / 2] |= cell->attr01Bits;
            ((vu32*)sBuffer)[sBufferOffset / 2] &= ~0x300;
            ((vu32*)sBuffer)[sBufferOffset / 2] |= new_var17;
            (sBuffer + 2)[sBufferOffset] = *(v2++);
            ((vu32*)(sBuffer + 2))[sBufferOffset / 2] &= cell->attr2Mask;
            ((vu32*)(sBuffer + 2))[sBufferOffset / 2] |= cell->attr2Bits;
            sBufferOffset += 4;
        }
    }

    return 0;
}
#endif

#ifdef MKSC_RING
/* Keep the queued world-sprite path free of the immediate renderer's large
 * stack-local OamCell. Most Battle objects need no rotation and write directly
 * into their final stable-sort entry. Rotated/UI cells retain the general path. */
static void oam_renderCellDataGeneral(const u16*, Vec2s16*, int, int, int, struc_6*) __attribute__((noinline));

void oam_renderCellData(const u16* data, Vec2s16* position, int scaleX, int scaleY, int rotation, struc_6* priority)
{
    if (!priority && !rotation && ((!scaleX || !scaleY) || (scaleX == 256 && scaleY == 256)) && ring_active())
    {
        int count = *data++, offset = sBufferOffset;
        while (count-- > 0 && offset <= 511)
        {
            u32 attr0 = *data++, attr1 = *data++;
            *(u32*)&sBuffer[offset] = ((((attr0 + position->y) & 255) | (attr0 & 0xFF00))
                | ((((attr1 + position->x) & 511) | (attr1 & 0xFE00)) << 16)) & ~0x300;
            sBuffer[offset + 2] = *data++;
            offset += 4;
        }
        sBufferOffset = offset;
        return;
    }
    if (priority && !rotation)
    {
        OamCellListEntry* entry;
        OamCell* cell;
        int index = sCellListEntryCount;
        if (index > OAM_CELL_LAST) return;
        entry = index < 32 ? &sCellListEntries[index] : &ring_sOamCells[index - 32];
        entry->prev = sCellListTail;
        entry->next = 0;
        if (sCellListTail) sCellListTail->next = entry;
        else sCellListHead = entry;
        sCellListTail = entry;
        entry->field8.field0 = priority->field0;
        entry->field8.priority = priority->priority + ((data[3] & 0xC00) << 16);
        if (!scaleX || !scaleY) scaleX = scaleY = 256;
        cell = &entry->cell;
        cell->cellData = data;
        /* Entries are word-aligned. Pair adjacent halfwords so the GBA
         * spends fewer instructions initializing each queued world sprite. */
        *(u32*)&cell->offsetX = (u16)position->x | ((u32)(u16)position->y << 16);
        cell->attr01Bits = cell->attr2Bits = 0;
        cell->attr01Mask = cell->attr2Mask = -1;
        *(u32*)&cell->affinePa = (u16)scaleX;
        *(u32*)&cell->affinePc = (u32)(u16)scaleY << 16;
        *(u32*)&cell->scaleX = (u16)scaleX | ((u32)(u16)scaleY << 16);
        sCellListEntryCount = index + 1;
        return;
    }
    oam_renderCellDataGeneral(data, position, scaleX, scaleY, rotation, priority);
}

static void oam_renderCellDataGeneral(const u16* cellData, Vec2s16* position, int scaleX, int scaleY, int rotation, struc_6* a6)
#else
void oam_renderCellData(const u16* cellData, Vec2s16* position, int scaleX, int scaleY, int rotation, struc_6* a6)
#endif
{

    OamCellListEntry* v12;
    OamCellListEntry* v13;
    OamCellListEntry* v14;
    struc_6* v16;
    OamCell* cell;
    OamCell v20;

    if (!a6)
    {
        cell = &v20;
        cell->cellData = cellData;
        cell->offsetX = position->x;
        cell->offsetY = position->y;
        cell->attr01Bits = 0;
        cell->attr01Mask = -1;
        cell->attr2Bits = 0;
        cell->attr2Mask = -1;
        if (!scaleX || !scaleY)
        {
            scaleY = 256;
            scaleX = 256;
        }
#ifdef MKSC_RING
        if (!rotation)
        {
            cell->affinePa = scaleX;
            cell->affinePb = 0;
            cell->affinePc = 0;
            cell->affinePd = scaleY;
        }
        else
#endif
        {
        cell->affinePa = scaleX * math_cos(rotation) / 0x8000;
        cell->affinePb = scaleX * math_sin(rotation) / 0x8000;
        cell->affinePc = -scaleY * math_sin(rotation) / 0x8000;
        cell->affinePd = scaleY * math_cos(rotation) / 0x8000;
        }
        cell->scaleX = scaleX;
        cell->scaleY = scaleY;
        oam_300218C = 1;
        oam_renderCell(cell);
        oam_300218C = 0;
        return;
    }
    if (sCellListEntryCount <= OAM_CELL_LAST)
    {
#ifdef MKSC_RING
        /* Keep the original fast IWRAM slots. Only overflow cells need the
         * frame-heap allocation; sorting and rendering follow the same links. */
        v12 = sCellListEntryCount < 32 ? &sCellListEntries[sCellListEntryCount]
             : &ring_sOamCells[sCellListEntryCount - 32];
#else
        v12 = &OAM_CELL_ENTRIES[sCellListEntryCount];
#endif
        v13 = sCellListHead;
        if (!v13)
        {
#ifdef MKSC_RING
            sCellListTail = v12;
            sCellListHead = v12;
#else
            sCellListTail = &OAM_CELL_ENTRIES[sCellListEntryCount];
            sCellListHead = &OAM_CELL_ENTRIES[sCellListEntryCount];
#endif
            v12->prev = v13;
            v12->next = v13;
        }
        else
        {
            v14 = sCellListTail;
            v14->next = v12;
            v12->prev = v14;
            v12->next = 0;
            sCellListTail = v12;
        }
#ifdef MKSC_RING
        v16 = &v12->field8;
#else
        v16 = &OAM_CELL_ENTRIES[sCellListEntryCount].field8;
#endif
        v16->field0 = a6->field0;
        v16->priority = a6->priority;
        v16->priority += ((cellData[3] & 0xC00) << 16);
#ifdef MKSC_RING
        cell = &v12->cell;
#else
        cell = &OAM_CELL_ENTRIES[sCellListEntryCount].cell;
#endif
        cell->cellData = cellData;
        cell->offsetX = position->x;
        cell->offsetY = position->y;
        cell->attr01Bits = 0;
        cell->attr01Mask = -1;
        cell->attr2Bits = 0;
        cell->attr2Mask = -1;
        if (!scaleX || !scaleY)
        {
            scaleY = 256;
            scaleX = 256;
        }
#ifdef MKSC_RING
        if (!rotation)
        {
            cell->affinePa = scaleX;
            cell->affinePb = 0;
            cell->affinePc = 0;
            cell->affinePd = scaleY;
        }
        else
#endif
        {
        cell->affinePa = scaleX * math_cos(rotation) / 0x8000;
        cell->affinePb = scaleX * math_sin(rotation) / 0x8000;
        cell->affinePc = -scaleY * math_sin(rotation) / 0x8000;
        cell->affinePd = scaleY * math_cos(rotation) / 0x8000;
        }
        cell->scaleX = scaleX;
        cell->scaleY = scaleY;
        sCellListEntryCount++;
    }
}

OamCellListEntry* oam_802FC48(OamCellListEntry* listEntry)
{
    OamCellListEntry* v2;
    OamCellListEntry* v4;
    OamCellListEntry* v5;
    OamCellListEntry* v7;
    OamCellListEntry* v6;
    OamCellListEntry* v8;
    OamCellListEntry* v9;
    OamCellListEntry entry;
    if ((!listEntry) || (!listEntry->next))
    {
        return listEntry;
    }

    v4 = listEntry;
    v2 = v4->next;
    while (v2)
    {
        v2 = v2->next;
        if (!v2)
        {
            break;
        }

        v4 = v4->next;
        v2 = v2->next;
    }

    v5 = v4->next;
    v4->next = 0;
    v7 = oam_802FC48(listEntry);
    v8 = oam_802FC48(v5);
    v9 = &entry;
    while (v7 && v8)
    {
        if ((v7->field8.priority > v8->field8.priority) ||
            ((v7->field8.priority == v8->field8.priority) && (v7->field8.field0 > v8->field8.field0)))
        {
            v9->next = v8;
            v9 = v8;
            v8 = v8->next;
        }
        else
        {
            v9->next = v7;
            v9 = v7;
            v7 = v7->next;
        }
    }

    if (!v7)
    {
        v5 = v8;
        v9->next = v5;
    }
    else
    {
        v9->next = v7;
    }

    return entry.next;
}

void oam_init(void)
{
    int i;
    u32* ptr;

    CpuFastFill(0, (void*)OAM, OAM_SIZE);
    sBufferOffset = 0;
#ifdef MKSC_RING
    if (ring_active()) ring_allocateSpriteQueue();
    for (i = 0; i < 8; i++) ring_balloonAffineIndex[i] = 255;
#endif
    sNextAffineId = 0;
    i = 0;
    while (i < sizeof(sBuffer) / 4)
    {
        ptr = (u32*)sBuffer;
        ptr[i++] = 0x200;
        ptr[i++] = 0;
    }
    sCellListHead = sCellListTail = 0;
    sCellListEntryCount = 0;
    oam_802FE4C(1);
    oam_300218C = 0;
}

void oam_update(void)
{
    if (!oam_3002188 || !sCopyDisabled)
        return;
    oam_300218C = 1;
    sCellListHead = oam_802FC48(sCellListHead);
    oam_renderAllCells();
    sCopyDisabled = 0;
    oam_300218C = 0;
}

void oam_reset(void)
{
    int i;
    u32* ptr;

#ifdef MKSC_RING
    /* A ring exchange can span a video frame. Keep a list under construction
     * until oam_update has published it; VBlank must not reset its allocator
     * between selecting a cell and filling that cell. */
    if (ring_active() && sCopyDisabled)
        return;
#endif

    if (!oam_3002188 || oam_300218C)
        return;

    sBufferOffset = 0;
#ifdef MKSC_RING
    for (i = 0; i < 8; i++) ring_balloonAffineIndex[i] = 255;
#endif
    sNextAffineId = 0;
    sCellListHead = 0;
    sCellListTail = 0;
    sCellListEntryCount = 0;
    i = 0;
#ifdef MKSC_RING
    /* Hide the same 128 objects without changing their tile/matrix words.
     * Eight stores per iteration avoid a branch for every single object. */
    while (i < sizeof(sBuffer) / 4)
    {
        u32* objects = (u32*)sBuffer + i;
        objects[0] = objects[2] = objects[4] = objects[6] = 0x200;
        objects[8] = objects[10] = objects[12] = objects[14] = 0x200;
        i += 16;
    }
#else
    while (i < sizeof(sBuffer) / 4)
    {
        ((u32*)sBuffer)[i] = 0x200;
        i += 2;
    }
#endif
    sCopyDisabled = 1;
}

void oam_dmaCopyToHw(void)
{
    if (!sCopyDisabled)
        DmaCopy32(0, sBuffer, (void*)OAM, sizeof(sBuffer));
}

void oam_cpuCopyToHw(void)
{
    if (!sCopyDisabled)
        CpuCopy32(sBuffer, (void*)OAM, sizeof(sBuffer));
}
void oam_renderMipmapCellDataUniform(const OamMipmapCellData* cellData, Vec2s16* position, int scale, struc_6* a4)
{
    if (scale <= 511)
        oam_renderCellData(cellData->level0Data, position, scale >> cellData->level0Shift,
                           scale >> cellData->level0Shift, 0, a4);
    else if (scale <= 1023)
        oam_renderCellData(cellData->level1Data, position, scale >> cellData->level1Shift,
                           scale >> cellData->level1Shift, 0, a4);
    else if (scale <= 2047)
        oam_renderCellData(cellData->level2Data, position, scale >> cellData->level2Shift,
                           scale >> cellData->level2Shift, 0, a4);
    else
        oam_renderCellData(cellData->level3Data, position, scale >> cellData->level3Shift,
                           scale >> cellData->level3Shift, 0, a4);
}

void oam_renderMipmapCellData(const OamMipmapCellData* cellData, Vec2s16* position, int mipmapScale, int scaleX,
                              int scaleY, int rotation, struc_6* a7)
{
    if (mipmapScale <= 511)
        oam_renderCellData(cellData->level0Data, position, scaleX >> cellData->level0Shift,
                           scaleY >> cellData->level0Shift, rotation, a7);
    else if (mipmapScale <= 1023)
        oam_renderCellData(cellData->level1Data, position, scaleX >> cellData->level1Shift,
                           scaleY >> cellData->level1Shift, rotation, a7);
    else if (mipmapScale <= 2047)
        oam_renderCellData(cellData->level2Data, position, scaleX >> cellData->level2Shift,
                           scaleY >> cellData->level2Shift, rotation, a7);
    else
        oam_renderCellData(cellData->level3Data, position, scaleX >> cellData->level3Shift,
                           scaleY >> cellData->level3Shift, rotation, a7);
}

void oam_renderCellDataSimple(const u16* cellData, int x, int y, int scale, struc_6* a6)
{
    Vec2s16 a2;
    x <<= 16;
    y <<= 16;
    *(u32*)&a2 = ((u32)x >> 16) | y;

    oam_renderCellData(cellData, &a2, scale, scale, 0, a6);
}

bool32 sub_802FF58(Vec2s16* a1, s16 a2)
{
    if ((u16)(a1->x + 64) > 0x170 || a1->y < 0 || a1->y > 224 || a2 <= 127 || a2 > 4096)
        return 0;
    else
        return 1;
}

#ifdef MKSC_RING
u16* oam_ringBuffer(void)
{
    return sBuffer;
}
unsigned oam_ringObjectCount(void)
{
    return sBufferOffset >> 2;
}
#endif
