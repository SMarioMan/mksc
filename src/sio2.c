#include "common.h"
#include "sio2.h"

extern const u8 _080000A0[];

bool32 sio2_isMaster(void)
{
    Sio2State* state = &sio2_sMPlayerId;
    bool32 result = FALSE;
    if (state->mPlayerId == 0)
        result = TRUE;
    return result;
}

bool32 sio2_isSlave(void)
{
    Sio2State* state = &sio2_sMPlayerId;
    bool32 result = FALSE;
    if (state->mPlayerId > 0)
        result = TRUE;
    return result;
}

u16 sio2_computeGameChecksum(void)
{
    u16 checksum = 0xCD4B;
    const u8* header = _080000A0;
    int i;

    for (i = 15; i >= 0; i--)
        checksum += *header++;

    return checksum;
}
