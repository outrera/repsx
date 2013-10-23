#ifndef __PSXCOMMON_H__
#define __PSXCOMMON_H__


// System includes
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <ctype.h>
#include <sys/types.h>
#include <assert.h>
#include <zlib.h>


//#define PSXREC
//#define ENABLE_SIO1API 1

// size we use for filename buffers
#define PATHLEN 1024


#define PACKAGE_VERSION "0.01"

// Define types
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;
typedef intptr_t sptr;

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef uintptr_t uptr;

typedef uint8_t boolean;

#define __inline inline

#define TRUE 1
#define FALSE 0
#define BOOL unsigned short
#define LOWORD(l)           ((unsigned short)(l))
#define HIWORD(l)           ((unsigned short)(((uint32_t)(l) >> 16) & 0xFFFF))
#define max(a,b)            (((a) > (b)) ? (a) : (b))
#define min(a,b)            (((a) < (b)) ? (a) : (b))
#define DWORD uint32_t
#define __int64 long long int 
#define MAKELONG(low,high)     ((unsigned long)(((unsigned short)(low)) | (((unsigned long)((unsigned short)(high))) << 16)))

// Local includes
#include "system.h"
#include "debug.h"

#define strnicmp strncasecmp

#define _(msgid) msgid
#define N_(msgid) msgid


extern int Log;

void __Log(char *fmt, ...);

typedef struct {
  char Mcd1[PATHLEN];
  char Mcd2[PATHLEN];
  char PatchesDir[PATHLEN];
	boolean Xa;
	boolean Sio;
	boolean Mdec;
	boolean PsxAuto;
	boolean Cdda;
	boolean HLE;
	boolean Debug;
	boolean PsxOut;
	boolean SpuIrq;
	boolean RCntFix;
	boolean UseNet;
	boolean VSyncWA;
	u8 Cpu; // CPU_DYNAREC or CPU_INTERPRETER
	u8 PsxType; // PSX_TYPE_NTSC or PSX_TYPE_PAL
} PcsxConfig;

extern PcsxConfig Config;
extern boolean NetOpened;

extern char SdlKeys[];

extern char WorkDir[];

#define gzfreeze(ptr, size) { \
	if (Mode == 1) gzwrite(f, ptr, size); \
	if (Mode == 0) gzread(f, ptr, size); \
}

// Make the timing events trigger faster as we are currently assuming everything
// takes one cycle, which is not the case on real hardware.
// FIXME: Count the proper cycle and get rid of this
#define BIAS	2
#define PSXCLK	33868800	/* 33.8688 MHz */

enum {
	PSX_TYPE_NTSC = 0,
	PSX_TYPE_PAL
}; // PSX Types

enum {
	CPU_DYNAREC = 0,
	CPU_INTERPRETER
}; // CPU Types

int EmuInit();
void EmuReset();
void EmuShutdown();
void EmuUpdate();

// prints hex dump onto console
void hd(uint8_t *P, int S);

void pathParts(char *Dir, char *Name, char *Ext, char *Path);
int folderP(char *Name);
int fileP(char *Name);
int fileSize(char *File);
int fileExist(char *File);
void removeFile(char *fn);
void makePath(char *Path);
uint8_t *loadFile(char *name, int *len);
void saveFile(char *Name, void *Data, int Length);
void saveBMP(char *filename, uint8_t *p, int w, int h, int flip);
void resample(void *dst, int dw, int dh, void *src, int sw, int sh);

u32 getWB();
void setWB(u32 mem, void*);
u32 getRB();
void setRB(u32 mem, void*);


#endif
