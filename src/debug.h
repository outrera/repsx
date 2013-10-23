#ifndef __DEBUG_H__
#define __DEBUG_H__

enum breakpoint_types {
	E, R1, R2, R4, W1, W2, W4
};

void StartDebugger();
void StopDebugger();

void DebugVSync();
void ProcessDebug();

void DebugCheckBP(u32 address, enum breakpoint_types type);

void PauseDebugger();
void ResumeDebugger();

extern char *disRNameCP0[];

char* disR3000AF(u32 code, u32 pc);

/* 
 * Specficies which logs should be activated.
 */

//#define LOG_STDOUT

//#define PAD_LOG  __Log
//#define GTE_LOG  __Log
//#define CDR_LOG  __Log("%8.8lx %8.8lx: ", psxRegs.pc, psxRegs.cycle); __Log

//#define PSXHW_LOG   __Log("%8.8lx %8.8lx: ", psxRegs.pc, psxRegs.cycle); __Log
//#define PSXBIOS_LOG __Log("%8.8lx %8.8lx: ", psxRegs.pc, psxRegs.cycle); __Log
//#define PSXDMA_LOG  __Log
//#define PSXMEM_LOG  __Log("%8.8lx %8.8lx: ", psxRegs.pc, psxRegs.cycle); __Log
//#define PSXCPU_LOG  __Log

//#define CDRCMD_DEBUG

#if defined (PSXCPU_LOG) || defined(PSXDMA_LOG) || defined(CDR_LOG) || defined(PSXHW_LOG) || \
	defined(PSXBIOS_LOG) || defined(PSXMEM_LOG) || defined(GTE_LOG)    || defined(PAD_LOG)
#define EMU_LOG __Log
#endif

#endif
