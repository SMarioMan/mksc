#include "common.h"
#include "scene.h"

/* Native race audio and opening transitions query the loaded definition,
 * rather than the selected cup. These compile identically with agbcc. */
u8 track_getRaceMaterial(RaceState* state)
{
    return state->trackDef->material;
}

u8 track_getRaceMusic(RaceState* state)
{
    return state->trackDef->musicID;
}

int* track_getRaceTargetTable(RaceState* state)
{
    return state->trackDef->randomTargetTable;
}
