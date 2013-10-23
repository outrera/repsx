#ifndef _SIO_H_
#define _SIO_H_

#include "common.h"
#include "r3000a.h"
#include "mem.h"
#include "plugins.h"

#define MCD_SIZE	(1024 * 8 * 16)

extern char Mcd1Data[MCD_SIZE], Mcd2Data[MCD_SIZE];

void sioWrite8(unsigned char value);
void sioWriteStat16(unsigned short value);
void sioWriteMode16(unsigned short value);
void sioWriteCtrl16(unsigned short value);
void sioWriteBaud16(unsigned short value);

unsigned char sioRead8();
unsigned short sioReadStat16();
unsigned short sioReadMode16();
unsigned short sioReadCtrl16();
unsigned short sioReadBaud16();

void netError();

void sioInterrupt();
int sioFreeze(gzFile f, int Mode);

void LoadMcd(int mcd, char *str);
void LoadMcds();
void SaveMcd(char *mcd, char *data, uint32_t adr, int size);
void CreateMcd(char *mcd);
void ConvertMcd(char *mcd, char *data);

typedef struct {
	char Title[48 + 1]; // Title in ASCII
	char sTitle[48 * 2 + 1]; // Title in Shift-JIS
	char ID[12 + 1];
	char Name[16 + 1];
	int IconCount;
	short Icon[16 * 16 * 3];
	unsigned char Flags;
} McdBlock;

void GetMcdBlockInfo(int mcd, int block, McdBlock *info);

#endif
