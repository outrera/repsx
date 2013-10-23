/*
* Internal PSX HLE functions.
*/

#include "hle.h"

static void hleDummy() {
	psxRegs.pc = psxRegs.GPR.n.ra;

	psxBranchTest();
}

static void hleA0() {
	u32 call = psxRegs.GPR.n.t1 & 0xff;

	if (biosA0[call]) biosA0[call]();

	psxBranchTest();
}

static void hleB0() {
	u32 call = psxRegs.GPR.n.t1 & 0xff;

	if (biosB0[call]) biosB0[call]();

	psxBranchTest();
}

static void hleC0() {
	u32 call = psxRegs.GPR.n.t1 & 0xff;

	if (biosC0[call]) biosC0[call]();

	psxBranchTest();
}

static void hleBootstrap() { // 0xbfc00000
	SysPrintf("hleBootstrap\n");
	CheckCdrom();
	LoadCdrom();
	SysPrintf("CdromLabel: \"%s\": PC = %8.8lx (SP = %8.8lx)\n", CdromLabel, psxRegs.pc, psxRegs.GPR.n.sp);
}

typedef struct {                   
	u32 _pc0;      
	u32 gp0;      
	u32 t_addr;   
	u32 t_size;   
	u32 d_addr;   
	u32 d_size;   
	u32 b_addr;   
	u32 b_size;   
	u32 S_addr;
	u32 s_size;
	u32 _sp,_fp,_gp,ret,base;
} EXEC;

static void hleExecRet() {
	EXEC *header = (EXEC*)PSXM(psxRegs.GPR.n.s0);

	SysPrintf("ExecRet %x: %x\n", psxRegs.GPR.n.s0, header->ret);

	psxRegs.GPR.n.ra = header->ret;
	psxRegs.GPR.n.sp = header->_sp;
	psxRegs.GPR.n.s8 = header->_fp;
	psxRegs.GPR.n.gp = header->_gp;
	psxRegs.GPR.n.s0 = header->base;

	psxRegs.GPR.n.v0 = 1;
	psxRegs.pc = psxRegs.GPR.n.ra;
}

void (*psxHLEt[256])() = {
	hleDummy, hleA0, hleB0, hleC0,
	hleBootstrap, hleExecRet,
	hleDummy, hleDummy
};
