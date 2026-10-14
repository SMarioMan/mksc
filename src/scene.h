#pragma once
#include "libunk/irq.h"
#include "frameHeap.h"
#include "actor.h"
#include "track.h"

#define RACE_CUR_RACE_STATE_UNKNOWN_GP_MODE 0x100
#define RACE_CUR_RACE_STATE_UNKNOWN_MG_MODE 0x800

typedef struct
{
    Actor actor;
    u32 field_44;
    u32 field_48;
    u8 characterFlags;           /* 0x4C: character in bits 0..2, ghost in bit 3. */
    u8 gap4D[3];
    u8 gap50[6];
    u16 viewYaw;                 /* 0x56: orientation used by the sprite camera. */
    u8 gap58[0x15];
    u8 animationFlags;           /* 0x6D: bit 0 selects look-aside direction. */
    u8 animationCode;            /* 0x6E: low nibble is the active animation. */
    u8 animationPhase;           /* 0x6F: native frame/phase bits. */
    u32 poseFlags;               /* 0x70 */
    u8 gap74[8];
    u16 headingYaw;              /* 0x7C: steering heading. */
    u8 gap7E[4];
    s16 driftYaw;                /* 0x82 */
    u8 gap84[4];
    s16 lateralMotion;           /* 0x88 */
    u8 gap8A[6];
    u8 checkpoint;              /* 0x90: low seven bits are the route zone. */
    u8 gap91[0x29];
    u16 gridPosition;            /* 0xBA */
    s32 driftVelocity;           /* 0xBC */
    u8 gapC0[4];
    u8 points;                  /* 0xC4: native GP cumulative points. */
    u8 gapC5[2];
    u8 lap;                     /* 0xC7: high bit encodes the finish target. */
    u8 gapC8[0x24];
    u8 playerId;                /* 0xEC: software player ID. */
    u8 gapED[0x90];
    u8 balloonsRemaining;       /* 0x17D */
    u8 gap17E[0x14];
    u8 drawFlags;               /* 0x192 */
    u8 gap193[0xD];
} Driver;

typedef char DriverSizeCheck[sizeof(Driver) == 0x1A0 ? 1 : -1];

typedef struct
{
    u8 gap0[0x14];
    u16 nrCheckpoints;
    u8 gap16[0x542];
    BgState backgroundState;
    void* trackData;
    TrackDef* trackDef;
    u8 gapwtf[0x164];
    FrameHeap frameHeap;
    u8 gap6EC[0xE8];
    u8 raceOptions;
    u8 gap7D5[2];
    u8 humanCount;
    u8 localPlayerId;
    u8 gap7D9;
    u16 curRaceStateUnknown;
    u8 engineClass;
    u8 headerTableIdx;
    u8 gap7DE[0x12];
    u8 driverCount;
    u8 driverCapacity;
    u8 gap7F2[2];
    Driver* drivers[8];
    Driver* playerDriver;
    //...
} RaceState;

u8 track_getRaceMaterial(RaceState* state);
u8 track_getRaceMusic(RaceState* state);
int* track_getRaceTargetTable(RaceState* state);

typedef bool32 (*SceneMain_fn)(void);
typedef void (*SceneVblank_fn)(void);

typedef struct
{
    SceneMain_fn sceneMainFunc;
    u8 space1[4];
    bool8 byte_3002E28;
    SceneVblank_fn vBlankFunc;
    u32 field10;
    u32 field14;
    u8 space2[8];
    u16 initialRepeatWait;
    u16 nextRepeatWait;
    RaceState raceState;
} SceneState;

extern SceneState gSceneState;

static inline void scene_setVBlankFunc(SceneVblank_fn func)
{
    SceneState* sceneState = &gSceneState;
    irq_updateMask(IRQ_UPDATE_MODE_AND, ~IRQ_MASK_VBLANK);
    sceneState->vBlankFunc = func;
    irq_updateMask(IRQ_UPDATE_MODE_OR, IRQ_MASK_VBLANK | IRQ_MASK_IME);
}

static inline void scene_setMainFunc(SceneMain_fn func)
{
    gSceneState.sceneMainFunc = func;
    gSceneState.byte_3002E28 = (gSceneState.byte_3002E28 + 1) & 7;
}
