#include "common.h"
#include "frameHeap.h"
#ifdef MKSC_RING
void ring_resetSpriteHeap(FrameHeap* heap);
#endif

void frmheap_init(FrameHeap* heap, void* address, u32 size)
{
    u32 i;
#ifdef MKSC_RING
    ring_resetSpriteHeap(heap);
#endif
    heap->head = address;
    heap->ptr = address;
    heap->size = size;
    for (i = 0; i < (heap->size >> 2); i++)
        *(u32*)&heap->head[i << 2] = 0;
}

void frmheap_reset(FrameHeap* heap)
{
#ifdef MKSC_RING
    ring_resetSpriteHeap(heap);
#endif
    heap->ptr = heap->head;
}

void* frmheap_calloc(FrameHeap* heap, int elements, int elemSize)
{
    void* result;
    u32 size, i;
    u8* ptr;

    result = NULL;
    size = elements * elemSize;
    if (heap->ptr + size <= heap->head + heap->size)
    {
        result = heap->ptr;
        heap->ptr += size;
        ptr = result;
        for (i = 0; i< (size>>2); i++)
            *(u32*)ptr++ = 0; // bug, increment is 1 instead of 4
        for (i = 0; i < (size & 3); i++)
            *ptr++ = 0;
    }
    return result;
}
