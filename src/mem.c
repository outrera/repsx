/*
* PSX memory functions.
*/

// TODO: Implement caches & cycle penalty.

#include "mem.h"
#include "r3000a.h"
#include "hw.h"
#include <sys/mman.h>


#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

s8 *psxM = NULL; // Kernel & User Memory (2 Meg)
s8 *psxP = NULL; // Parallel Port (64K)
s8 *psxR = NULL; // BIOS ROM (512K)
s8 *psxH = NULL; // Scratch Pad (1K) & Hardware Registers (8K)

u8 **psxMemWLUT = NULL;
u8 **psxMemRLUT = NULL;

static u32 WB=0x00800000; //write break point
static void *wbHandler;

static u32 RB=0x00800000; //read break point
static void *rbHandler;

/*  Playstation Memory Map (from Playstation doc by Joshua Walker)
0x0000_0000-0x0000_ffff		Kernel (64K)
0x0001_0000-0x001f_ffff		User Memory (1.9 Meg)

0x1f00_0000-0x1f00_ffff		Parallel Port (64K)

0x1f80_0000-0x1f80_03ff		Scratch Pad (1024 bytes)

0x1f80_1000-0x1f80_2fff		Hardware Registers (8K)

0x1fc0_0000-0x1fc7_ffff		BIOS (512K)

0x8000_0000-0x801f_ffff		Kernel and User Memory Mirror (2 Meg) Cached
0x9fc0_0000-0x9fc7_ffff		BIOS Mirror (512K) Cached

0xa000_0000-0xa01f_ffff		Kernel and User Memory Mirror (2 Meg) Uncached
0xbfc0_0000-0xbfc7_ffff		BIOS Mirror (512K) Uncached
*/

int psxMemInit() {
	int i;

	psxMemRLUT = (u8 **)malloc(0x10000 * sizeof(void *));
	psxMemWLUT = (u8 **)malloc(0x10000 * sizeof(void *));
	memset(psxMemRLUT, 0, 0x10000 * sizeof(void *));
	memset(psxMemWLUT, 0, 0x10000 * sizeof(void *));

	psxM = mmap(0, 0x00220000,
		PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	psxP = &psxM[0x200000];
	psxH = &psxM[0x210000];

	psxR = (s8 *)malloc(0x00080000);

	if (psxMemRLUT == NULL || psxMemWLUT == NULL || 
		psxM == NULL || psxP == NULL || psxH == NULL) {
		SysMessage(_("Error allocating memory!"));
		return -1;
	}

// MemR
	for (i = 0; i < 0x80; i++) psxMemRLUT[i + 0x0000] = (u8 *)&psxM[(i & 0x1f) << 16];

	memcpy(psxMemRLUT + 0x8000, psxMemRLUT, 0x80 * sizeof(void *));
	memcpy(psxMemRLUT + 0xa000, psxMemRLUT, 0x80 * sizeof(void *));

	psxMemRLUT[0x1f00] = (u8 *)psxP;
	psxMemRLUT[0x1f80] = (u8 *)psxH;

	for (i = 0; i < 0x08; i++) psxMemRLUT[i + 0x1fc0] = (u8 *)&psxR[i << 16];

	memcpy(psxMemRLUT + 0x9fc0, psxMemRLUT + 0x1fc0, 0x08 * sizeof(void *));
	memcpy(psxMemRLUT + 0xbfc0, psxMemRLUT + 0x1fc0, 0x08 * sizeof(void *));

// MemW
	for (i = 0; i < 0x80; i++) psxMemWLUT[i + 0x0000] = (u8 *)&psxM[(i & 0x1f) << 16];

	memcpy(psxMemWLUT + 0x8000, psxMemWLUT, 0x80 * sizeof(void *));
	memcpy(psxMemWLUT + 0xa000, psxMemWLUT, 0x80 * sizeof(void *));

	psxMemWLUT[0x1f00] = (u8 *)psxP;
	psxMemWLUT[0x1f80] = (u8 *)psxH;

	return 0;
}

#include "scph7502.h"
void psxMemReset() {
	memset(psxM, 0, 0x00200000);
	memset(psxP, 0, 0x00010000);

	if (Config.HLE) memset(psxR, 0, 0x80000);
  else memcpy(psxR, scph7502, 0x80000);
}

void psxMemShutdown() {
	munmap(psxM, 0x00220000);

	free(psxR);
	free(psxMemRLUT);
	free(psxMemWLUT);
}

static int writeok = 1;

u8 psxMemRead8(u32 mem) {
	char *p;
	u32 t;

	t = mem >> 16;
	if (t == 0x1f80) {
		if (mem < 0x1f801000)
			return psxHu8(mem);
		else
			return psxHwRead8(mem);
	} else {
		p = (char *)(psxMemRLUT[t]);
		if (p != NULL) {
			//if (Config.Debug) DebugCheckBP((mem & 0xffffff) | 0x80000000, R1);
      //if (((mem&0xfffff8)|0x80000000)==RB) ((void (*)())rbHandler)();
			return *(u8 *)(p + (mem & 0xffff));
		} else {
#ifdef PSXMEM_LOG
			PSXMEM_LOG("err lb %8.8lx\n", mem);
#endif
			return 0;
		}
	}
}

u16 psxMemRead16(u32 mem) {
	char *p;
	u32 t;

	t = mem >> 16;
	if (t == 0x1f80) {
		if (mem < 0x1f801000)
			return psxHu16(mem);
		else
			return psxHwRead16(mem);
	} else {
		p = (char *)(psxMemRLUT[t]);
		if (p != NULL) {
			//if (Config.Debug) DebugCheckBP((mem & 0xffffff) | 0x80000000, R2);
      //if (((mem&0xfffff8)|0x80000000)==RB) ((void (*)())rbHandler)();
			return SWAPu16(*(u16 *)(p + (mem & 0xffff)));
		} else {
#ifdef PSXMEM_LOG
			PSXMEM_LOG("err lh %8.8lx\n", mem);
#endif
			return 0;
		}
	}
}

u32 psxMemRead32(u32 mem) {
	char *p;
	u32 t;

	t = mem >> 16;
	if (t == 0x1f80) {
		if (mem < 0x1f801000)
			return psxHu32(mem);
		else
			return psxHwRead32(mem);
	} else {
		p = (char *)(psxMemRLUT[t]);
		if (p != NULL) {
			//if (Config.Debug) DebugCheckBP((mem & 0xffffff) | 0x80000000, R4);
      //if (((mem&0xfffff8)|0x80000000)==RB) ((void (*)())rbHandler)();
			return SWAPu32(*(u32 *)(p + (mem & 0xffff)));
		} else {
#ifdef PSXMEM_LOG
			if (writeok) { PSXMEM_LOG("err lw %8.8lx\n", mem); }
#endif
			return 0;
		}
	}
}

void psxMemWrite8(u32 mem, u8 value) {
	char *p;
	u32 t;

	t = mem >> 16;
	if (t == 0x1f80) {
		if (mem < 0x1f801000)
			psxHu8(mem) = value;
		else
			psxHwWrite8(mem, value);
	} else {
		p = (char *)(psxMemWLUT[t]);
		if (p != NULL) {
      if (((mem&0xfffff8)|0x80000000)==WB) ((void (*)())wbHandler)();
			//if (Config.Debug) DebugCheckBP((mem & 0xffffff) | 0x80000000, W1);
			*(u8 *)(p + (mem & 0xffff)) = value;
#ifdef PSXREC
			psxCpu->Clear((mem & (~3)), 1);
#endif
		} else {
#ifdef PSXMEM_LOG
			PSXMEM_LOG("err sb %8.8lx\n", mem);
#endif
		}
	}
}

void psxMemWrite16(u32 mem, u16 value) {
	char *p;
	u32 t;

	t = mem >> 16;
	if (t == 0x1f80) {
		if (mem < 0x1f801000)
			psxHu16ref(mem) = SWAPu16(value);
		else
			psxHwWrite16(mem, value);
	} else {
		p = (char *)(psxMemWLUT[t]);
		if (p != NULL) {
      if (((mem&0xfffff8)|0x80000000)==WB) ((void (*)())wbHandler)();
			//if (Config.Debug) DebugCheckBP((mem & 0xffffff) | 0x80000000, W2);
			*(u16 *)(p + (mem & 0xffff)) = SWAPu16(value);
#ifdef PSXREC
			psxCpu->Clear((mem & (~1)), 1);
#endif
		} else {
#ifdef PSXMEM_LOG
			PSXMEM_LOG("err sh %8.8lx\n", mem);
#endif
		}
	}
}

void psxMemWrite32(u32 mem, u32 value) {
	char *p;
	u32 t;

//	if ((mem&0x1fffff) == 0x71E18 || value == 0x48088800) SysPrintf("t2fix!!\n");
	t = mem >> 16;
	if (t == 0x1f80) {
		if (mem < 0x1f801000)
			psxHu32ref(mem) = SWAPu32(value);
		else
			psxHwWrite32(mem, value);
	} else {
    //if (mem < 0x801FF000/* && mem != */) printf("%08X\n", mem);
		p = (char *)(psxMemWLUT[t]);
		if (p != NULL) {
      if (((mem&0xfffff8)|0x80000000)==WB) ((void (*)())wbHandler)();
			//if (Config.Debug) DebugCheckBP((mem & 0xffffff) | 0x80000000, W4);
			*(u32 *)(p + (mem & 0xffff)) = SWAPu32(value);
#ifdef PSXREC
			psxCpu->Clear(mem, 1);
#endif
		} else {
			if (mem != 0xfffe0130) {
#ifdef PSXREC
				if (!writeok)
					psxCpu->Clear(mem, 1);
#endif

#ifdef PSXMEM_LOG
				if (writeok) { PSXMEM_LOG("err sw %8.8lx\n", mem); }
#endif
			} else {
				int i;

				switch (value) {
					case 0x800: case 0x804:
						if (writeok == 0) break;
						writeok = 0;
						memset(psxMemWLUT + 0x0000, 0, 0x80 * sizeof(void *));
						memset(psxMemWLUT + 0x8000, 0, 0x80 * sizeof(void *));
						memset(psxMemWLUT + 0xa000, 0, 0x80 * sizeof(void *));
						break;
					case 0x00: case 0x1e988:
						if (writeok == 1) break;
						writeok = 1;
						for (i = 0; i < 0x80; i++) psxMemWLUT[i + 0x0000] = (void *)&psxM[(i & 0x1f) << 16];
						memcpy(psxMemWLUT + 0x8000, psxMemWLUT, 0x80 * sizeof(void *));
						memcpy(psxMemWLUT + 0xa000, psxMemWLUT, 0x80 * sizeof(void *));
						break;
					default:
#ifdef PSXMEM_LOG
						PSXMEM_LOG("unk %8.8lx = %x\n", mem, value);
#endif
						break;
				}
			}
		}
	}
}

void *psxMemPointer(u32 mem) {
	char *p;
	u32 t;

	t = mem >> 16;
	if (t == 0x1f80) {
		if (mem < 0x1f801000)
			return (void *)&psxH[mem];
		else
			return NULL;
	} else {
		p = (char *)(psxMemWLUT[t]);
		if (p != NULL) {
			return (void *)(p + (mem & 0xffff));
		}
		return NULL;
	}
}

u32 getWB() {
  return WB;
}

void setWB(u32 mem, void*h) {
  WB = mem&0xFFFFFFF8;
  if (WB == 0) WB=0x00800000;
  wbHandler = h;
}


u32 getRB() {
  return RB;
}

void setRB(u32 mem, void*h) {
  RB = mem&0xFFFFFFF8;
  if (RB == 0) RB=0x00800000;
  rbHandler = h;
}