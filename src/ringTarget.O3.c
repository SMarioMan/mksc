#include "common.h"
#ifdef MKSC_RING
#include "scene.h"
#include "ringLink.h"

void ring_stockTargetResponse(Driver* driver);
void sub_803D214(Driver* driver, int response);

/* Native target/depth response, with the tiny actor comparisons inlined.
 * Eight-player Battle otherwise makes repeated ROM calls for every pair. */
static void respond(Driver* driver, Driver* other)
{
    u8* a = (u8*)driver;
    u8* b = (u8*)other;
    Driver* target = *(s16*)(a + 0x1E) < *(s16*)(b + 0x1E) ? driver : other;
    Driver* behind = target == driver ? other : driver;
    sub_803D214(target, *(s16*)((u8*)target + 0x20) > *(s16*)((u8*)behind + 0x20) ? 9 : 8);
}

void sub_803D00C(Driver* driver)
{
    RaceState* state = &gSceneState.raceState;
    Driver* nearest = 0;
    unsigned i, distance, best = 4096;
    u8* a = (u8*)driver;
    if (!ring_active()) { ring_stockTargetResponse(driver); return; }
    for (i = 0; i < state->driverCapacity; i++)
    {
        Driver* other = state->drivers[i];
        u8* b = (u8*)other;
        unsigned cp = a[0x90] & 127, peer = b[0x90] & 127;
        unsigned prev = (cp ? cp - 1 : (u8)state->nrCheckpoints - 1) & 255;
        unsigned next = cp == state->nrCheckpoints - 1 ? 0 : cp + 1;
        int delta;
        if (other == driver || (peer != cp && peer != prev && peer != next)) continue;
        delta = (s8)((u8)a[0x1E] - (u8)b[0x1E]);
        distance = delta < 0 ? -delta : delta;
        if (*(s16*)(a + 0x1E) < *(s16*)(b + 0x1E))
        {
            if (distance <= 55) respond(driver, other);
        }
        else if (distance < best) { best = distance; nearest = other; }
    }
    if (nearest) respond(driver, nearest);
}
#endif
