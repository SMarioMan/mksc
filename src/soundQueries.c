#include "common.h"

typedef struct { s32 x, y; } SoundPosition;

/* Native positional attenuation: fixed-point world coordinates become whole
 * pixels before squared distance is mapped to a 0..256 mixer volume. */
u16 sound_getDistanceVolume(void* state, const SoundPosition* position)
{
    SoundPosition* origin;
    s32 x, y, distance;
    state = (u8*)state + 0x824;
    asm("" : "+r"(state)); /* Preserve the native address calculation. */
    origin = state;
    x = (position->x - *(s32*)((u8*)origin + 0x18)) / 65536;
    y = (position->y - *(s32*)((u8*)origin + 0x1C)) / 65536;
    distance = x * x + y * y;
    if (distance > 0x3FFFF) return 0;
    return 256 - distance / 1024;
}
