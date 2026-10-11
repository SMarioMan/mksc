#pragma once

typedef struct
{
    s8 mPlayerId;
    u8 field_1;
    u16 field_2;
    u32 field_4;
    u16 field_8;
    u8 field_A;
    u8 field_B;
    u8 field_C;
    u8 field_D[9];
    u16 field_16;
} struc_71;

extern struc_71 sio2_sMPlayerId;

void sio2_stop(void);
bool32 sio2_isMaster(void);
bool32 sio2_isSlave(void);
u16 sio2_computeGameChecksum(void);
