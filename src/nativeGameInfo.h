#pragma once
#include "common.h"

typedef struct
{
    u16 column;
    u16 slideTick;
    u16 delay;
    s16 character;
} NativeFinishBanner;

typedef struct
{
    s16 place;
    s16 menuCharacter;
    s16 finishTime;
    s16 previousPoints;
    s16 awardedPoints;
    u8 animation[6];
} NativeResultRow;

/* Native race and result scenes reuse these records at different times.
 * Their eight-entry storage is independent of the four-controller limit. */
typedef struct
{
    u8 sceneData[0x34];
    union
    {
        NativeFinishBanner banners[8];
        NativeResultRow results[8];
    } rows;
} NativeGameInfo;

extern NativeGameInfo gNativeGameInfo;
NativeGameInfo* sub_800EDDC(void);
NativeGameInfo* sub_800EDE4(void);
NativeGameInfo* sub_800EDEC(void);
NativeGameInfo* sub_800EDF4(void);
