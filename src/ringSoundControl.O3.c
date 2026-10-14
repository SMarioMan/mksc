#include "common.h"
#ifdef MKSC_RING
#include "gba/m4a_internal.h"

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
