#ifndef __PSXCOUNTERS_H__
#define __PSXCOUNTERS_H__


#include "common.h"
#include "r3000a.h"
#include "mem.h"
#include "plugins.h"

extern u32 psxNextCounter, psxNextsCounter;

void psxRcntInit();
void psxRcntUpdate();

void psxRcntWcount(u32 index, u32 value);
void psxRcntWmode(u32 index, u32 value);
void psxRcntWtarget(u32 index, u32 value);

u32 psxRcntRcount(u32 index);
u32 psxRcntRmode(u32 index);
u32 psxRcntRtarget(u32 index);

s32 psxRcntFreeze(gzFile f, s32 Mode);

#endif
