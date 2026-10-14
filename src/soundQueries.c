#include "common.h"

typedef struct { s32 x, y; } SoundPosition;
typedef struct { u8 gap0[0x18]; SoundPosition position; } SoundListener;

/* Native positional attenuation: fixed-point world coordinates become whole
 * pixels before squared distance is mapped to a 0..256 mixer volume. */
u16 sound_getDistanceVolume(void* state, const SoundPosition* position)
{
    SoundListener* listener;
    s32 x, y, distance;
    state = (u8*)state + 0x824;
    asm("" : "+r"(state)); /* Preserve the native address calculation. */
    listener = state;
    x = (position->x - listener->position.x) / 65536;
    y = (position->y - listener->position.y) / 65536;
    distance = x * x + y * y;
    if (distance > 0x3FFFF) return 0;
    return 256 - distance / 1024;
}
