#include "common.h"

extern const s32 _080D9790[8];
extern const s32 _080D97B0[8];

int spm_menuCharIdxToCharId(int menuIndex)
{
    return _080D9790[menuIndex];
}

int sub_800EDB0(int character)
{
    const s32* table = _080D97B0;
    return table[character & 7];
}
