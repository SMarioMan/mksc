#include "common.h"
#ifdef MKSC_RING
#include "scene.h"
#include "ringLink.h"

void ring_stockTargetResponse(Driver* driver);
void sub_803D214(Driver* driver, int response);
void ring_stockLookAsideAnimation(Driver* driver, int right);
u8 sub_803CA78(Driver* driver);

static void applyLookAsideAnimation(Driver* driver, int right)
{
    extern const u8 _080F2C14[];
    driver->animationCode = (driver->animationCode & 0xF0) | (right ? 9 : 8);
    driver->animationPhase = (driver->animationPhase & 1) | (_080F2C14[1] << 1);
    if (right) driver->animationFlags |= 1;
    else driver->animationFlags &= ~1;
}

/* The camera query is pure. Check the original animation gates first. */
void driver_requestLookAsideAnimation(Driver* driver, int right)
{
    u8 facing;
    if (!ring_active()) { ring_stockLookAsideAnimation(driver, right); return; }
    if ((driver->animationCode & 15) || driver->actor.drawScale > 511) return;
    facing = sub_803CA78(driver);
    if (facing && facing != 127) return;
    applyLookAsideAnimation(driver, right);
}

/* The animation gate has already established an inactive animation. For a
 * driver other than the camera owner, the native query is just a signed yaw
 * difference; its local drift/speed corrections cannot apply. */
static u8 inactiveLookAsideFacing(Driver* driver)
{
    if (driver != gSceneState.raceState.playerDriver || (driver->field_48 & 0x20))
    {
        s16 cameraYaw = *(s16*)((u8*)&gSceneState + 0x85C);
        u16 driverYaw = driver->viewYaw;
        return ((cameraYaw - driverYaw) >> 9) & 127;
    }
    return sub_803CA78(driver);
}

/* Responses 8/9 only request a look-aside animation. A failed camera-facing
 * test can repeat for every neighbor; reuse that pure result within this
 * scan. Any successful animation makes the native gate reject later calls. */
static void respond(Driver* driver, Driver* other, int* currentFacing)
{
    Driver* target = driver->actor.drawPos.y < other->actor.drawPos.y ? driver : other;
    Driver* behind = target == driver ? other : driver;
    int facing;
    if ((target->characterFlags & 8) || (target->animationCode & 15) || target->actor.drawScale > 511) return;
    if (target == driver)
    {
        if (*currentFacing < 0) *currentFacing = inactiveLookAsideFacing(target);
        facing = *currentFacing;
    }
    else facing = inactiveLookAsideFacing(target);
    if (facing && facing != 127) return;
    applyLookAsideAnimation(target, target->actor.positionS16.x > behind->actor.positionS16.x);
}

void race_updateNearbyTargets(Driver* driver)
{
    RaceState* state = &gSceneState.raceState;
    Driver* nearest = 0;
    unsigned i, distance, best = 4096;
    int currentFacing = -1;
    bool32 currentResponded = FALSE;
    unsigned cp = driver->checkpoint & 127;
    unsigned prev = (cp ? cp - 1 : (u8)state->nrCheckpoints - 1) & 255;
    unsigned next = cp == state->nrCheckpoints - 1 ? 0 : cp + 1;
    int depth = driver->actor.drawPos.y;
    if (!ring_active()) { ring_stockTargetResponse(driver); return; }
    /* Service audio between native driver updates as well as at graphics
     * publication. A heavy item/terrain update can cross a video boundary. */
    ring_serviceRenderAudio();
    for (i = 0; i < state->driverCapacity; i++)
    {
        Driver* other = state->drivers[i];
        unsigned peer = other->checkpoint & 127;
        int delta;
        if (other == driver || (peer != cp && peer != prev && peer != next)) continue;
        delta = (s8)((u8)depth - (u8)other->actor.drawPos.y);
        distance = delta < 0 ? -delta : delta;
        if (depth < other->actor.drawPos.y)
        {
            /* This branch always targets the current driver. A failed
             * native gate/facing test stays failed throughout this scan;
             * a successful request activates its animation and rejects
             * all later requests. The first qualifying neighbor therefore
             * decides the complete native effect. */
            if (distance <= 55 && !currentResponded)
            {
                respond(driver, other, &currentFacing);
                currentResponded = TRUE;
            }
        }
        else if (distance < best) { best = distance; nearest = other; }
    }
    if (nearest) respond(driver, nearest, &currentFacing);
}
#endif
