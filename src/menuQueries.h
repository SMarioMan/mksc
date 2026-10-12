#pragma once
#include "common.h"
s32 spm_getTrackSelection(void);
void spm_setTrackSelection(s32 selection);
u32 spm_getPageSelection(void);
void spm_setPageSelection(u32 selection);
void spm_previousPageSelection(void);
void spm_setState(void* state);
s32 spm_getSavedPlayerCount(void);
