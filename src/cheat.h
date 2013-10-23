#ifndef CHEAT_H
#define CHEAT_H

typedef struct {
	uint32_t	Addr;
	uint16_t	Val;
} CheatCode;

typedef struct {
	char		*Descr;
	int			First;		// index of the first cheat code
	int			n;			// number of cheat codes for this cheat
	int			Enabled;
} Cheat;

void ClearAllCheats();

void LoadCheats(const char *filename);
void SaveCheats(const char *filename);

void ApplyCheats();

int AddCheat(const char *descr, char *code);
void RemoveCheat(int index);
int EditCheat(int index, const char *descr, char *code);

void FreeCheatSearchResults();
void FreeCheatSearchMem();
void CheatSearchBackupMemory();

void CheatSearchEqual8(u8 val);
void CheatSearchEqual16(u16 val);
void CheatSearchEqual32(u32 val);
void CheatSearchNotEqual8(u8 val);
void CheatSearchNotEqual16(u16 val);
void CheatSearchNotEqual32(u32 val);
void CheatSearchRange8(u8 min, u8 max);
void CheatSearchRange16(u16 min, u16 max);
void CheatSearchRange32(u32 min, u32 max);
void CheatSearchIncreasedBy8(u8 val);
void CheatSearchIncreasedBy16(u16 val);
void CheatSearchIncreasedBy32(u32 val);
void CheatSearchDecreasedBy8(u8 val);
void CheatSearchDecreasedBy16(u16 val);
void CheatSearchDecreasedBy32(u32 val);
void CheatSearchIncreased8();
void CheatSearchIncreased16();
void CheatSearchIncreased32();
void CheatSearchDecreased8();
void CheatSearchDecreased16();
void CheatSearchDecreased32();
void CheatSearchDifferent8();
void CheatSearchDifferent16();
void CheatSearchDifferent32();
void CheatSearchNoChange8();
void CheatSearchNoChange16();
void CheatSearchNoChange32();

extern Cheat *Cheats;
extern CheatCode *CheatCodes;
extern int NumCheats;
extern int NumCodes;

extern s8 *prevM;
extern u32 *SearchResults;
extern int NumSearchResults;

#define PREVM(mem)		(&prevM[mem])
#define PrevMu8(mem)	(*(u8 *)PREVM(mem))
#define PrevMu16(mem)	(SWAP16(*(u16 *)PREVM(mem)))
#define PrevMu32(mem)	(SWAP32(*(u32 *)PREVM(mem)))

// cheat types
#define CHEAT_CONST8		0x30	/* 8-bit Constant Write */
#define CHEAT_CONST16		0x80	/* 16-bit Constant Write */
#define CHEAT_INC16			0x10	/* 16-bit Increment */
#define CHEAT_DEC16			0x11	/* 16-bit Decrement */
#define CHEAT_INC8			0x20	/* 8-bit Increment */
#define CHEAT_DEC8			0x21	/* 8-bit Decrement */
#define CHEAT_SLIDE			0x50	/* Slide Codes */
#define CHEAT_MEMCPY		0xC2	/* Memory Copy */

#define CHEAT_EQU8			0xE0	/* 8-bit Equal To */
#define CHEAT_NOTEQU8		0xE1	/* 8-bit Not Equal To */
#define CHEAT_LESSTHAN8		0xE2	/* 8-bit Less Than */
#define CHEAT_GREATERTHAN8  0xE3	/* 8-bit Greater Than */
#define CHEAT_EQU16			0xD0	/* 16-bit Equal To */
#define CHEAT_NOTEQU16		0xD1	/* 16-bit Not Equal To */
#define CHEAT_LESSTHAN16	0xD2	/* 16-bit Less Than */
#define CHEAT_GREATERTHAN16 0xD3	/* 16-bit Greater Than */

#endif
