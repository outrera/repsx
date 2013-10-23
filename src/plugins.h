#ifndef __PLUGINS_H__
#define __PLUGINS_H__

#include "common.h"
#include "mem.h"
#include "r3000a.h"

int LoadPlugins();
void ReleasePlugins();
int OpenPlugins();
void ClosePlugins();


// GPU Functions
extern u8  *psxVSecure;
long GPU_open(unsigned long *, char *, char *);
long GPU_init(void);
long GPU_shutdown(void);
long GPU_close(void);
void GPU_writeStatus(uint32_t);
void GPU_writeData(uint32_t);
void GPU_writeDataMem(uint32_t *, int);
uint32_t GPUr_eadStatus(void);
uint32_t GPU_readData(void);
void GPU_readDataMem(uint32_t *, int);
long GPU_dmaChain(uint32_t *,uint32_t);
void GPU_updateLace(void);
long GPU_configure(void);
long GPU_test(void);
void GPU_about(void);
void GPU_makeSnapshot(void);
void GPU_keypressed(int);
void GPU_displayText(char *);
typedef struct GPUFREEZETAG {
  uint32_t ulFreezeVersion;      // should be always 1 for now
  uint32_t ulStatus;             // current gpu status
  uint32_t ulControl[256];       // latest control register values
  unsigned char psxVRam[1024*1024*2]; // current VRam image (full 2 MB for ZN)
} GPUFreeze_t;
long GPU_freeze(uint32_t, GPUFreeze_t *);
long GPU_getScreenPic(unsigned char *);
void GPU_showScreenPic(unsigned char *);
void GPU_clearDynarec(void (*callback)(void));
void GPU_vBlank(int);
void GPU_WriteConfig();

char *GPU_GetScaler();
void GPU_SetScaler(char *Name);


void GPU_dumpVRAM();
void GPU_pick(int x, int y, int w, int h);


// CD-ROM Functions
long CDR_init(void);
long CDR_shutdown(void);
long CDR_open(void);
long CDR_close(void);
long CDR_getTN(unsigned char *);
long CDR_getTD(unsigned char, unsigned char *);
long CDR_readTrack(unsigned char *);
unsigned char* CDR_getBuffer(void);
unsigned char* CDR_getBufferSub(void);
long CDR_configure(void);
long CDR_test(void);
void CDR_about(void);
long CDR_play(unsigned char *);
long CDR_stop(void);
long CDR_setfilename(char *);
struct CdrStat {
	uint32_t Type;
	uint32_t Status;
	unsigned char Time[3];
};
long CDR_getStatus(struct CdrStat *);
char* CDR_getDriveLetter(void);
struct SubQ {
	char res0[12];
	unsigned char ControlAndADR;
	unsigned char TrackNumber;
	unsigned char IndexNumber;
	unsigned char TrackRelativeAddress[3];
	unsigned char Filler;
	unsigned char AbsoluteAddress[3];
	unsigned char CRC[2];
	char res1[72];
};
long CDR_readCDDA(unsigned char, unsigned char, unsigned char, unsigned char *);
long CDR_getTE(unsigned char, unsigned char *, unsigned char *, unsigned char *);


// SPU Functions
typedef struct {
	s32	y0, y1;
} ADPCM_Decode_t;

typedef struct {
	int				freq;
	int				nbits;
	int				stereo;
	int				nsamples;
	ADPCM_Decode_t	left, right;
	short			pcm[16384];
} xa_decode_t;

s32 xa_decode_sector( xa_decode_t *xdp,
					   unsigned char *sectorp,
					   int is_first_sector );

#define H_SPUirqAddr     0x0da4
#define H_SPUaddr        0x0da6
#define H_SPUdata        0x0da8
#define H_SPUctrl        0x0daa
#define H_SPUstat        0x0dae
#define H_SPUon1         0x0d88
#define H_SPUon2         0x0d8a
#define H_SPUoff1        0x0d8c
#define H_SPUoff2        0x0d8e


void SPUirq(void);

long SPU_open(void);
long SPU_init(void);				
long SPU_shutdown(void);	
long SPU_close(void);			
void SPU_playSample(unsigned char);		
void SPU_writeRegister(unsigned long, unsigned short);
unsigned short SPU_readRegister(unsigned long);
void SPU_writeDMA(unsigned short);
unsigned short SPU_readDMA(void);
void SPU_writeDMAMem(unsigned short *, int);
void SPU_readDMAMem(unsigned short *, int);
void SPU_playADPCMchannel(xa_decode_t *);
void SPU_registerCallback(void (*callback)(void));
long SPU_configure(void);
long SPU_test(void);
void SPU_about(void);
typedef struct {
	unsigned char PluginName[8];
	uint32_t PluginVersion;
	uint32_t Size;
	unsigned char SPUPorts[0x200];
	unsigned char SPURam[0x80000];
	xa_decode_t xa;
	unsigned char *SPUInfo;
} SPUFreeze_t;

long SPU_freeze(uint32_t, SPUFreeze_t *);
void SPU_async(uint32_t);
void SPU_playCDDAchannel(short *, int);




/*

  PAD Functions that must be exported from PAD Plugin

  long	PADinit(long flags);	// called only once when PSEmu Starts
  void	PADshutdown(void);		// called when PSEmu exits
  long	PADopen(PadInitS *);	// called when PSEmu is running program
  long	PADclose(void);
  long	PADconfigure(void);
  void  PADabout(void);
  long  PADtest(void);			// called from Configure Dialog and after PADopen();
  long	PADquery(void);

  unsigned char PADstartPoll(int);
  unsigned char PADpoll(unsigned char);

*/

// PADquery responses (notice - values ORed)
// PSEmu will use them also in PADinit to tell Plugin which Ports will use
// notice that PSEmu will call PADinit and PADopen only once when they are from
// same plugin

// might be used in port 1
#define PSE_PAD_USE_PORT1			1
// might be used in port 2
#define PSE_PAD_USE_PORT2			2

// MOUSE SCPH-1030
#define PSE_PAD_TYPE_MOUSE			1
// NEGCON - 16 button analog controller SLPH-00001
#define PSE_PAD_TYPE_NEGCON			2
// GUN CONTROLLER - gun controller SLPH-00014 from Konami
#define PSE_PAD_TYPE_GUN			3
// STANDARD PAD SCPH-1080, SCPH-1150
#define PSE_PAD_TYPE_STANDARD		4
// ANALOG JOYSTICK SCPH-1110
#define PSE_PAD_TYPE_ANALOGJOY		5
// GUNCON - gun controller SLPH-00034 from Namco
#define PSE_PAD_TYPE_GUNCON			6
// ANALOG CONTROLLER SCPH-1150
#define PSE_PAD_TYPE_ANALOGPAD		7


typedef struct {
	// controler type - fill it withe predefined values above
	unsigned char controllerType;

	// status of buttons - every controller fills this field
	unsigned short buttonStatus;

	// for analog pad fill those next 4 bytes
	// values are analog in range 0-255 where 127 is center position
	unsigned char rightJoyX, rightJoyY, leftJoyX, leftJoyY;

	// for mouse fill those next 2 bytes
	// values are in range -128 - 127
	unsigned char moveX, moveY;

	unsigned char reserved[91];
} PadDataS;


long PAD_open(unsigned long *);
long PAD_configure(void);
void PAD_about(void);
long PAD_init(long);
long PAD_shutdown(void);	
long PAD_test(void);		
long PAD_close(void);
long PAD_query(void);
long PAD_readPort1(PadDataS*);
long PAD_readPort2(PadDataS*);
long PAD_keypressed(void);
unsigned char PAD_startPoll(int N); //start polling pad N
unsigned char PAD1_poll(unsigned char);
unsigned char PAD2_poll(unsigned char);
void PAD_setSensitive(int);
int PAD_GetMapping(int I, char *N);
void PAD_SetMapping(int I, char *N, int K);
void SavePADConfig();



/*
  NET Functions:

   long NETopen(HWND hWnd)
    opens the connection.
    shall return 0 on success, else -1.
    -1 is also returned if the user selects offline mode.

   long NETclose()
    closes the connection.
    shall return 0 on success, else -1.

   void NETpause()
    this is called when the user paused the emulator.

   void NETresume()
    this is called when the user resumed the emulator.

   long NETqueryPlayer()
    returns player number

   long NETsendPadData(void *pData, int Size)
    this should be called for the first pad only on each side.

   long NETrecvPadData(void *pData, int Pad)
    call this for Pad 1/2 to get the data sent by the above func.

  extended funcs:

   long NETsendData(void *pData, int Size, int Mode)
    sends Size bytes from pData to the other side.

   long NETrecvData(void *pData, int Size, int Mode)
    receives Size bytes from pData to the other side.

   void NETsetInfo(netInfo *info);
    sets the netInfo struct.

   void NETkeypressed(int key) (linux only)
    key is a XK_?? (X11) keycode.
*/

/* Modes bits for NETsendData/NETrecvData */
#define PSE_NET_BLOCKING    0
#define PSE_NET_NONBLOCKING 1
long NET_open(unsigned long *);
long NET_init(void);
long NET_shutdown(void);
long NET_close(void);
long NET_configure(void);
long NET_test(void);
void NET_about(void);
void NET_pause(void);
void NET_resume(void);
long NET_queryPlayer(void);
long NET_sendData(void *, int, int);
long NET_recvData(void *, int, int);
long NET_sendPadData(void *, int);
long NET_recvPadData(void *, int);

typedef struct {
  // unsupported fields should be zeroed.
	char EmuName[32];
	char CdromID[9];	// ie. 'SCPH12345', no \0 trailing character
	char CdromLabel[11];
	void *psxMem;
	long (*GPU_showScreenPic)(unsigned char *);
	void (*GPU_displayText)(char *);
	void (*PAD_setSensitive)(int);
	char GPUpath[256];	// paths must be absolute
	char SPUpath[256];
	char CDRpath[256];
	char MCD1path[256];
	char MCD2path[256];
	char BIOSpath[256];	// 'HLE' for internal bios
	char Unused[1024];
} netInfo;

void NET_setInfo(netInfo *);
void NET_keypressed(int);


// SIO1 Functions (link cable)
long SIO1_open(unsigned long *);
long SIO1_init(void);
long SIO1_shutdown(void);
long SIO1_close(void);
long SIO1_configure(void);
long SIO1_test(void);
void SIO1_about(void);
void SIO1_pause(void);
void SIO1_resume(void);
long SIO1_keypressed(int);
void SIO1_writeData8(unsigned char);
void SIO1_writeData16(unsigned short);
void SIO1_writeData32(unsigned long);
void SIO1_writeStat16(unsigned short);
void SIO1_writeStat32(unsigned long);
void SIO1_writeMode16(unsigned short);
void SIO1_writeMode32(unsigned long);
void SIO1_writeCtrl16(unsigned short);
void SIO1_writeCtrl32(unsigned long);
void SIO1_writeBaud16(unsigned short);
void SIO1_writeBaud32(unsigned long);
unsigned char SIO1_readData8(void);
unsigned short SIO1_readData16(void);
unsigned long SIO1_readData32(void);
unsigned short SIO1_readStat16(void);
unsigned long SIO1_readStat32(void);
unsigned short SIO1_readMode16(void);
unsigned long SIO1_readMode32(void);
unsigned short SIO1_readCtrl16(void);
unsigned long SIO1_readCtrl32(void);
unsigned short SIO1_readBaud16(void);
unsigned long SIO1_readBaud32(void);
void SIO1_registerCallback(void (*callback)(void));

void clearDynarec(void);

void SetIsoFile(const char *filename);
const char *GetIsoFile(void);
boolean UsingIso(void);
void SetCdOpenCaseTime(s64 time);

void cdrIsoInit(void);
int cdrIsoActive(void);

// use for dumb and savestate names
extern char GameTitle[];

//these are called by gpu.c
uint8_t *screenBlitStart(int *w, int *h);
void screenBlitEnd();
void setCaption(char *Title);

// converts PSX vram to 32bit RGB
void BlitScreen32(uint8_t *surf, int x, int y);

// scalers use these to set result width
extern int finalw,finalh;

typedef struct {
  char *Name;
  void *F;
} scaler;

extern scaler Scalers[];

#endif
