#include "menuQueries.h"

/* These fields are shared by the native menu and scene coordinator. Keep
 * their widths and signedness: the page cursor also uses -1 as its sentinel. */
#define TRACK_SELECTION (*(s32*)0x03000008)
#define PAGE_SELECTION (*(u8*)0x0300000C)
#define MENU_STATE (*(void**)0x0203EBE8)
#define SAVED_MENU ((s8*)0x03000010)

s32 spm_getTrackSelection(void)
{
    return TRACK_SELECTION;
}

void spm_setTrackSelection(s32 selection)
{
    TRACK_SELECTION = selection;
}

u32 spm_getPageSelection(void)
{
    return PAGE_SELECTION;
}

void spm_setPageSelection(u32 selection)
{
    PAGE_SELECTION = selection;
}

void spm_previousPageSelection(void)
{
    u8* selection = (u8*)0x0300000C;
    u32 value = *selection;
    if (value) *selection = value - 1;
}

void spm_setState(void* state)
{
    MENU_STATE = state;
}

s32 spm_getSavedPlayerCount(void)
{
    s8* saved = SAVED_MENU;
    s32 index = saved[0x17];
    return saved[index];
}
