#include "common.h"
#ifdef MKSC_RING
#include "gba/m4a_internal.h"
#include "scene.h"
#include "songs.h"

u16 ring_raceEffectMusic(const RaceState* state)
{
    unsigned i;
    bool32 shrunken = FALSE, invisible = FALSE;
    for (i = 0; i < state->driverCount; i++)
    {
        const Driver* driver = state->drivers[i];
        if (driver->starTimer > 0) return SONG_STAR_THEME;
        if (driver->booTimer > 0) invisible = TRUE;
        if (driver->shrinkTimer > 0) shrunken = TRUE;
    }
    /* Native music requests prioritize Star (0x10), Boo (0x08), then
     * lightning (0x04). Combine those states across the entire grid. */
    return invisible ? SONG_BOO_THEME : shrunken ? SONG_LIGHTNING_THEME : 0;
}


/* These native controls only update selected track parameters. Once the
 * selection mask is empty, remaining tracks cannot change. Preserve the
 * player's identification lock and all native byte-sized conversions. */
void m4aMPlayVolumeControl(struct MusicPlayerInfo* player, u16 selected, u16 volume)
{
    struct MusicPlayerTrack* track;
    unsigned remaining;
    if (player->ident != ID_NUMBER) return;
    player->ident++;
    track = player->tracks;
    remaining = player->trackCount;
    while (remaining && selected)
    {
        if ((selected & 1) && (track->flags & MPT_FLG_EXIST))
        {
            track->volX = volume / 4;
            track->flags |= MPT_FLG_VOLCHG;
        }
        selected >>= 1; remaining--; track++;
    }
    player->ident = ID_NUMBER;
}

void m4aMPlayPitchControl(struct MusicPlayerInfo* player, u16 selected, s16 pitch)
{
    struct MusicPlayerTrack* track;
    unsigned remaining;
    if (player->ident != ID_NUMBER) return;
    player->ident++;
    track = player->tracks;
    remaining = player->trackCount;
    while (remaining && selected)
    {
        if ((selected & 1) && (track->flags & MPT_FLG_EXIST))
        {
            track->keyShiftX = pitch >> 8;
            track->pitX = pitch;
            track->flags |= MPT_FLG_PITCHG;
        }
        selected >>= 1; remaining--; track++;
    }
    player->ident = ID_NUMBER;
}
#endif
