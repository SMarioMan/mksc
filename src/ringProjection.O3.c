#include "common.h"
#ifdef MKSC_RING
#include "camera.h"
#include "ringLink.h"

void ring_stockProjectActor(Camera*, Actor*, Vec2s16*, s16*);
u32 umul3232H32(u32, u32);

static const u32 sProjectionReciprocal[] = {
#include "ringProjectionReciprocal.inc"
};

static s32 projectScreenCoordinate(s32 value, u32 scale)
{
    u32 magnitude = value < 0 ? -(u32)value : (u32)value;
    u32 divisor = scale * 256;
    u32 quotient = umul3232H32(magnitude, sProjectionReciprocal[scale - 32]);
    /* The reciprocal rounds down. For a 32-bit magnitude its estimate is
     * at most one below the exact quotient, including INT_MIN. */
    if (magnitude - quotient * divisor >= divisor) quotient++;
    return value < 0 ? -(s32)quotient : (s32)quotient;
}

/* Preserve the native fixed-point truncation at every matrix product. The
 * final two quotients can combine their positive divisors exactly:
 * trunc(trunc(value / scale) / 256) == trunc(value / (scale * 256)). */
void cam_CalcProjectActor(Camera* camera, Actor* actor, Vec2s16* position, s16* scale)
{
    s32 x, y, z, screenX, screenY, projectedZ, depth;
    const Vec3s32* matrix = camera->unk5C;
    if (!ring_active()) { ring_stockProjectActor(camera, actor, position, scale); return; }
    x = actor->posX - camera->unk38.x;
    y = actor->posY - camera->unk38.y;
    z = -camera->unk38.z;
    if ((u32)(x + 0x3FFFFFF) > 0x7FFFFFE ||
        (u32)(y + 0x3FFFFFF) > 0x7FFFFFE ||
        (u32)(z + 0x3FFFFFF) > 0x7FFFFFE) goto clipped;
    x /= 128; y /= 128; z /= 128;
    screenX = ((matrix[0].x / 16 * x) / 32) + ((matrix[1].x / 16 * y) / 32);
    screenY = ((matrix[0].y / 16 * x) / 32) + ((matrix[1].y / 16 * y) / 32) + ((matrix[2].y / 16 * z) / 32);
    projectedZ = ((matrix[0].z / 16 * x) / 32) + ((matrix[1].z / 16 * y) / 32) + ((matrix[2].z / 16 * z) / 32);
    depth = camera->elevation - projectedZ;
    if (depth <= 0) goto clipped;
    *scale = depth < 0x200000 ? Div(depth * 256, camera->elevation) : Div(depth, camera->elevation / 256);
    if (*scale < 32 || *scale > 1024) goto clipped;
    position->x = projectScreenCoordinate(screenX, *scale) + camera->screenPos.x;
    position->y = projectScreenCoordinate(screenY, *scale) + camera->screenPos.y + camera->unk380;
    *scale *= 4;
    return;
clipped:
    *scale = -1;
}
#endif
