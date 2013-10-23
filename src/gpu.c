#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/time.h>
#include "plugins.h"



////////////////////////////////////////////////////////////////////////


#define INFO_TW        0
#define INFO_DRAWSTART 1
#define INFO_DRAWEND   2
#define INFO_DRAWOFF   3

#define SHADETEXBIT(x) ((x>>24) & 0x1)
#define SEMITRANSBIT(x) ((x>>25) & 0x1)
#define PSXRGB(r,g,b) ((g<<10)|(b<<5)|r)

#define DATAREGISTERMODES unsigned short

#define DR_NORMAL        0
#define DR_VRAMTRANSFER  1


#define GPUSTATUS_ODDLINES            0x80000000
#define GPUSTATUS_DMABITS             0x60000000 // Two bits
#define GPUSTATUS_READYFORCOMMANDS    0x10000000
#define GPUSTATUS_READYFORVRAM        0x08000000
#define GPUSTATUS_IDLE                0x04000000
#define GPUSTATUS_DISPLAYDISABLED     0x00800000
#define GPUSTATUS_INTERLACED          0x00400000
#define GPUSTATUS_RGB24               0x00200000
#define GPUSTATUS_PAL                 0x00100000
#define GPUSTATUS_DOUBLEHEIGHT        0x00080000
#define GPUSTATUS_WIDTHBITS           0x00070000 // Three bits
#define GPUSTATUS_MASKENABLED         0x00001000
#define GPUSTATUS_MASKDRAWN           0x00000800
#define GPUSTATUS_DRAWINGALLOWED      0x00000400
#define GPUSTATUS_DITHER              0x00000200

#define GPUIsBusy (lGPUstatusRet &= ~GPUSTATUS_IDLE)
#define GPUIsIdle (lGPUstatusRet |= GPUSTATUS_IDLE)

#define GPUIsNotReadyForCommands (lGPUstatusRet &= ~GPUSTATUS_READYFORCOMMANDS)
#define GPUIsReadyForCommands (lGPUstatusRet |= GPUSTATUS_READYFORCOMMANDS)

typedef struct VRAMLOADTTAG
{
 short x;
 short y;
 short Width;
 short Height;
 short RowsRemaining;
 short ColsRemaining;
 unsigned short *ImagePtr;
} VRAMLoad_t;

typedef struct PSXPOINTTAG
{
 int32_t x;
 int32_t y;
} PSXPoint_t;

typedef struct PSXSPOINTTAG
{
 short x;
 short y;
} PSXSPoint_t;

typedef struct PSXRECTTAG
{
 short x0;
 short x1;
 short y0;
 short y1;
} PSXRect_t;

typedef struct RECTTAG
{
 int left;
 int top;
 int right;
 int bottom;
}RECT;

typedef struct TWINTAG
{
 PSXRect_t  Position;
} TWin_t;


typedef struct PSXDISPLAYTAG
{
 PSXPoint_t  DisplayModeNew;
 PSXPoint_t  DisplayMode;
 PSXPoint_t  DisplayPosition;
 PSXPoint_t  DisplayEnd;
 
 int32_t        Double;
 int32_t        Height;
 int32_t        PAL;
 int32_t        InterlacedNew;
 int32_t        Interlaced;
 int32_t        RGB24New;
 int32_t        RGB24;
 PSXSPoint_t DrawOffset;
 int32_t        Disabled;
 PSXRect_t   Range;

} PSXDisplay_t;


////////////////////////////////////////////////////////////////////////
// PPDK developer must change libraryName field and can change revision and build
////////////////////////////////////////////////////////////////////////

const  unsigned char version  = 1;    // do not touch - library for PSEmu 1.x
const  unsigned char revision = 1;
const  unsigned char build    = 17;   // increase that with each version

////////////////////////////////////////////////////////////////////////
// memory image of the PSX vram 
////////////////////////////////////////////////////////////////////////

u8  *psxVSecure=0;
static unsigned char  *psxVub;
static signed   char  *psxVsb;
static unsigned short *psxVuw;
static unsigned short *psxVuw_eom;
static signed   short *psxVsw;
static uint32_t  *psxVul;
static int32_t  *psxVsl;

////////////////////////////////////////////////////////////////////////
// GPU globals
////////////////////////////////////////////////////////////////////////

#define OPAQUEON   10
#define OPAQUEOFF  11

#define KEY_RESETTEXSTORE 1
#define KEY_SHOWFPS       2
#define KEY_RESETOPAQUE   4
#define KEY_RESETDITHER   8
#define KEY_RESETFILTER   16
#define KEY_RESETADVBLEND 32
//#define KEY_BLACKWHITE    64
#define KEY_BADTEXTURES   128
#define KEY_CHECKTHISOUT  256

#if !defined(__BIG_ENDIAN__) || defined(__x86_64__) || defined(__i386__)
#ifndef __LITTLE_ENDIAN__
#define __LITTLE_ENDIAN__
#endif
#endif

#ifdef __LITTLE_ENDIAN__
#define RED(x) (x & 0xff)
#define BLUE(x) ((x>>16) & 0xff)
#define GREEN(x) ((x>>8) & 0xff)
#define COLOR(x) (x & 0xffffff)
#elif defined __BIG_ENDIAN__
#define RED(x) ((x>>24) & 0xff)
#define BLUE(x) ((x>>8) & 0xff)
#define GREEN(x) ((x>>16) & 0xff)
#define COLOR(x) SWP32(x & 0xffffff)
#endif

// byteswappings
#define SWP16(x) ({ uint16_t y=(x); (((y)>>8 & 0xff) | ((y)<<8 & 0xff00)); })
#define SWP32(x) ({ uint32_t y=(x); (((y)>>24 & 0xfful) | ((y)>>8 & 0xff00ul) | ((y)<<8 & 0xff0000ul) | ((y)<<24 & 0xff000000ul)); })

#ifdef __BIG_ENDIAN__

// big endian config
#define HOST2LE32(x) SWP32(x)
#define HOST2BE32(x) (x)
#define LE2HOST32(x) SWP32(x)
#define BE2HOST32(x) (x)

#define HOST2LE16(x) SWP16(x)
#define HOST2BE16(x) (x)
#define LE2HOST16(x) SWP16(x)
#define BE2HOST16(x) (x)

#else

// little endian config
#define HOST2LE32(x) (x)
#define HOST2BE32(x) SWP32(x)
#define LE2HOST32(x) (x)
#define BE2HOST32(x) SWP32(x)

#define HOST2LE16(x) (x)
#define HOST2BE16(x) SWP16(x)
#define LE2HOST16(x) (x)
#define BE2HOST16(x) SWP16(x)

#endif

#define GETLEs16(X) ((int16_t)GETLE16((uint16_t *)X))
#define GETLEs32(X) ((int16_t)GETLE32((uint16_t *)X))

#define GETLE16(X) LE2HOST16(*(uint16_t *)X)
#define GETLE32(X) LE2HOST32(*(uint32_t *)X)
#define GETLE16D(X) ({uint32_t val = GETLE32(X); (val<<16 | val >> 16);})
#define PUTLE16(X, Y) do{*((uint16_t *)X)=HOST2LE16((uint16_t)Y);}while(0)
#define PUTLE32(X, Y) do{*((uint32_t *)X)=HOST2LE16((uint32_t)Y);}while(0)

static long              lGPUdataRet;
static long              lGPUstatusRet;
static char              szDispBuf[64];
static char              szMenuBuf[36];
static char              szDebugText[512];
static uint32_t          ulStatusControl[256];

static uint32_t          gpuDataM[256];
static unsigned char     gpuCommand = 0;
static long              gpuDataC = 0;
static long              gpuDataP = 0;

static VRAMLoad_t        VRAMWrite;
static VRAMLoad_t        VRAMRead;
static DATAREGISTERMODES DataWriteMode;
static DATAREGISTERMODES DataReadMode;

static BOOL              bSkipNextFrame = FALSE;
static DWORD             dwLaceCnt=0;
static int               iColDepth;
static int               iWindowMode;
static short             sDispWidths[8] = {256,320,512,640,368,384,512,640};
static PSXDisplay_t      PSXDisplay;
static PSXDisplay_t      PreviousPSXDisplay;
static long              lSelectedSlot=0;
static BOOL              bDoLazyUpdate=FALSE;
static uint32_t          lGPUInfoVals[16];
static int               iFakePrimBusy=0;

static unsigned long dwCoreFlags = 0;

static uint8_t *FB = 0; // frame buffer, where final image is stored as RGB
static uint8_t *SB = 0; // scale buffer, where scaled image is stored


static BOOL           bUsingTWin=FALSE;
static TWin_t         TWin;
//static unsigned long  clutid;                                 // global clut
static unsigned short usMirror=0;                             // sprite mirror
static int            iDither=0;
static int32_t        drawX;
static int32_t        drawY;
static int32_t        drawW;
static int32_t        drawH;
static uint32_t       dwCfgFixes;
static uint32_t       dwActFixes=0;
static uint32_t       dwEmuFixes=0;
static int            iUseFixes;
static int            iUseDither=0;
static BOOL           bDoVSyncUpdate=FALSE;

unsigned long          ulKeybits=0;

static short g_m1=255,g_m2=255,g_m3=255;
static short DrawSemiTrans=FALSE;
static short Ymin;
static short Ymax;

static short          ly0,lx0,ly1,lx1,ly2,lx2,ly3,lx3;        // global psx vertex coords
static int32_t           GlobalTextAddrX,GlobalTextAddrY,GlobalTextTP;
static int32_t           GlobalTextREST,GlobalTextABR;

static time_t tStart;

// FPS stuff
static float          fFrameRateHz=0;
static DWORD          dwFrameRateTicks=16;
static float          fFrameRate;
static int            iFrameLimit;
static int            UseFrameLimit=0;
static int            UseFrameSkip=0;


// FPS skipping / limit
static BOOL   bInitCap = TRUE;
static float  fps_skip = 0;
static float  fps_cur  = 0;

#define MAXLACE 16


static int SnapRequested;
static int SnapCount;
void GPU_makeSnapshot() {SnapRequested = 1;}


void updateDisplay(void);
void SetAutoFrameCap(void);
void SetFixes(void);

void ReadConfig(void);
void WriteConfig(void);
void ReadWinSizeConfig(void);

void SoftDlgProc(void);
void AboutDlgProc(void);

void UploadScreen (long Position);
void PrepareFullScreenUpload (long Position);

void offsetPSXLine(void);
void offsetPSX2(void);
void offsetPSX3(void);
void offsetPSX4(void);

void FillSoftwareAreaTrans(short x0,short y0,short x1,short y1,unsigned short col);
void FillSoftwareArea(short x0,short y0,short x1,short y1,unsigned short col);
void drawPoly3G(int32_t rgb1, int32_t rgb2, int32_t rgb3);
void drawPoly4G(int32_t rgb1, int32_t rgb2, int32_t rgb3, int32_t rgb4);
void drawPoly3F(int32_t rgb);
void drawPoly4F(int32_t rgb);
void drawPoly4FT(unsigned char * baseAddr);
void drawPoly4GT(unsigned char * baseAddr);
void drawPoly3FT(unsigned char * baseAddr);
void drawPoly3GT(unsigned char * baseAddr);
void DrawSoftwareSprite(unsigned char * baseAddr,short w,short h,int32_t tx,int32_t ty);
void DrawSoftwareSpriteTWin(unsigned char * baseAddr,int32_t w,int32_t h);
void DrawSoftwareSpriteMirror(unsigned char * baseAddr,int32_t w,int32_t h);
void DrawSoftwareLineShade(int32_t rgb0, int32_t rgb1);
void DrawSoftwareLineFlat(int32_t rgb);



void FrameCap(void); 
void FrameSkip(void); 
void calcfps(void);
void PCFrameCap (void);
void PCcalcfps(void);
void SetAutoFrameCap(void);
void SetFPSHandler(void);
void InitFPS(void);
void CheckFrameRate(void);


void    SetKeyHandler(void);
void    ReleaseKeyHandler(void);


void DisplayText(void);
void CloseMenu(void);
void InitMenu(void);
void BuildDispMenu(int iInc);
void SwitchDispMenu(int iStep);


static __inline unsigned short BGR24to16 (uint32_t BGR)
{
 return (unsigned short)(((BGR>>3)&0x1f)|((BGR&0xf80000)>>9)|((BGR&0xf800)>>6));
}

////////////////////////////////////////////////////////////////////////
// former zn.c
////////////////////////////////////////////////////////////////////////

static uint32_t      dwGPUVersion=0;
static int           iGPUHeight=512;
static int           iGPUHeightMask=511;
static int           GlobalTextIL=0;
static int           iTileCheat=0;


////////////////////////////////////////////////////////////////////////
// former fps.c
////////////////////////////////////////////////////////////////////////


void CheckFrameRate(void)
{
 if(UseFrameSkip)                                      // skipping mode?
  {
   if(!(dwActFixes&0x80))                              // not old skipping mode?
    {
     dwLaceCnt++;                                      // -> store cnt of vsync between frames
     if(dwLaceCnt>=MAXLACE && UseFrameLimit)           // -> if there are many laces without screen toggling,
      {                                                //    do std frame limitation
       if(dwLaceCnt==MAXLACE) bInitCap=TRUE;
       FrameCap();
      }
    }
   else if(UseFrameLimit) FrameCap();
   calcfps();                                          // -> calc fps display in skipping mode
  }
 else                                                  // non-skipping mode:
  {
   if(UseFrameLimit) FrameCap();                       // -> do it
   if(ulKeybits&KEY_SHOWFPS) calcfps();                // -> and calc fps display
  }
}

#define TIMEBASE 100000

unsigned long timeGetTime()
{
 struct timeval tv;
 gettimeofday(&tv, 0);                                 // well, maybe there are better ways
 return tv.tv_sec * 100000 + tv.tv_usec/10;            // to do that, but at least it works
}

void FrameCap (void)
{
 static unsigned long curticks, lastticks, _ticks_since_last_update;
 static unsigned int TicksToWait = 0;
 int overslept=0, tickstogo=0;
 BOOL Waiting = TRUE;

  {
   curticks = timeGetTime();
   _ticks_since_last_update = curticks - lastticks;

    if((_ticks_since_last_update > TicksToWait) ||
       (curticks <lastticks))
    {
     lastticks = curticks;
     overslept = _ticks_since_last_update - TicksToWait;
     if((_ticks_since_last_update-TicksToWait) > dwFrameRateTicks)
          TicksToWait=0;
     else
          TicksToWait=dwFrameRateTicks - overslept;
    }
   else
    {
     while (Waiting)
      {
       curticks = timeGetTime();
       _ticks_since_last_update = curticks - lastticks;
       tickstogo = TicksToWait - _ticks_since_last_update;
       if ((_ticks_since_last_update > TicksToWait) ||
           (curticks < lastticks) || tickstogo < overslept)
        {
         Waiting = FALSE;
         lastticks = curticks;
         overslept = _ticks_since_last_update - TicksToWait;
         TicksToWait = dwFrameRateTicks - overslept;
         return;
        }
	if (tickstogo >= 200 && !(dwActFixes&16))
		usleep(tickstogo*10 - 200);
      }
    }
  }
}

#define MAXSKIP 120

void FrameSkip(void)
{
 static int   iNumSkips=0,iAdditionalSkip=0;           // number of additional frames to skip
 static DWORD dwLastLace=0;                            // helper var for frame limitation
 static DWORD curticks, lastticks, _ticks_since_last_update;
 int tickstogo=0;
 static int overslept=0;

 if(!dwLaceCnt) return;                                // important: if no updatelace happened, we ignore it completely

 if(iNumSkips)                                         // we are in skipping mode?
  {
   dwLastLace+=dwLaceCnt;                              // -> calc frame limit helper (number of laces)
   bSkipNextFrame = TRUE;                              // -> we skip next frame
   iNumSkips--;                                        // -> ok, one done
  }
 else                                                  // ok, no additional skipping has to be done...
  {                                                    // we check now, if some limitation is needed, or a new skipping has to get started
   DWORD dwWaitTime;

   if(bInitCap || bSkipNextFrame)                      // first time or we skipped before?
    {
     if(UseFrameLimit && !bInitCap)                    // frame limit wanted and not first time called?
      {
       DWORD dwT=_ticks_since_last_update;             // -> that's the time of the last drawn frame
       dwLastLace+=dwLaceCnt;                          // -> and that's the number of updatelace since the start of the last drawn frame

       curticks = timeGetTime();                       // -> now we calc the time of the last drawn frame + the time we spent skipping
       _ticks_since_last_update= dwT+curticks - lastticks;

       dwWaitTime=dwLastLace*dwFrameRateTicks;         // -> and now we calc the time the real psx would have needed

       if(_ticks_since_last_update<dwWaitTime)         // -> we were too fast?
        {
         if((dwWaitTime-_ticks_since_last_update)>     // -> some more security, to prevent
            (60*dwFrameRateTicks))                     //    wrong waiting times
          _ticks_since_last_update=dwWaitTime;

         while(_ticks_since_last_update<dwWaitTime)    // -> loop until we have reached the real psx time
          {                                            //    (that's the additional limitation, yup)
           curticks = timeGetTime();
           _ticks_since_last_update = dwT+curticks - lastticks;
          }
        }
       else                                            // we were still too slow ?!!?
        {
         if(iAdditionalSkip<MAXSKIP)                   // -> well, somewhen we really have to stop skipping on very slow systems
          {
           iAdditionalSkip++;                          // -> inc our watchdog var
           dwLaceCnt=0;                                // -> reset lace count
           lastticks = timeGetTime();
           return;                                     // -> done, we will skip next frame to get more speed
          } 
        }
      }

     bInitCap=FALSE;                                   // -> ok, we have inited the frameskip func
     iAdditionalSkip=0;                                // -> init additional skip
     bSkipNextFrame=FALSE;                             // -> we don't skip the next frame
     lastticks = timeGetTime();                        // -> we store the start time of the next frame
     dwLaceCnt=0;                                      // -> and we start to count the laces 
     dwLastLace=0;
     _ticks_since_last_update=0;
     return;                                           // -> done, the next frame will get drawn
    }

   bSkipNextFrame=FALSE;                               // init the frame skip signal to 'no skipping' first

   curticks = timeGetTime();                           // get the current time (we are now at the end of one drawn frame)
   _ticks_since_last_update = curticks - lastticks;

   dwLastLace=dwLaceCnt;                               // store curr count (frame limitation helper)
   dwWaitTime=dwLaceCnt*dwFrameRateTicks;              // calc the 'real psx lace time'
   if (dwWaitTime >= overslept)
   	dwWaitTime-=overslept;

   if(_ticks_since_last_update>dwWaitTime)             // hey, we needed way too long for that frame...
    {
     if(UseFrameLimit)                                 // if limitation, we skip just next frame,
      {                                                // and decide after, if we need to do more
       iNumSkips=0;
      }
     else
      {
       iNumSkips=_ticks_since_last_update/dwWaitTime;  // -> calc number of frames to skip to catch up
       iNumSkips--;                                    // -> since we already skip next frame, one down
       if(iNumSkips>MAXSKIP) iNumSkips=MAXSKIP;        // -> well, somewhere we have to draw a line
      }
     bSkipNextFrame = TRUE;                            // -> signal for skipping the next frame
    }
   else                                                // we were faster than real psx? fine :)
   if(UseFrameLimit)                                   // frame limit used? so we wait til the 'real psx time' has been reached
    {
     if(dwLaceCnt>MAXLACE)                             // -> security check
      _ticks_since_last_update=dwWaitTime;

     while(_ticks_since_last_update<dwWaitTime)        // -> just do a waiting loop...
      {
       curticks = timeGetTime();
       _ticks_since_last_update = curticks - lastticks;

	tickstogo = dwWaitTime - _ticks_since_last_update;
	if (tickstogo-overslept >= 200 && !(dwActFixes&16))
		usleep(tickstogo*10 - 200);
      }
    }
   overslept = _ticks_since_last_update - dwWaitTime;
   if (overslept < 0)
	overslept = 0;
   lastticks = timeGetTime();                          // ok, start time of the next frame
  }

 dwLaceCnt=0;                                          // init lace counter
}

void calcfps(void)
{
 static unsigned long curticks,_ticks_since_last_update,lastticks;
 static long   fps_cnt = 0;
 static unsigned long  fps_tck = 1;
 static long          fpsskip_cnt = 0;
 static unsigned long fpsskip_tck = 1;

  {
   curticks = timeGetTime();
   _ticks_since_last_update=curticks-lastticks;

   if(UseFrameSkip && !UseFrameLimit && _ticks_since_last_update)
    fps_skip=min(fps_skip,((float)TIMEBASE/(float)_ticks_since_last_update+1.0f));

   lastticks = curticks;
  }

 if(UseFrameSkip && UseFrameLimit)
  {
   fpsskip_tck += _ticks_since_last_update;

   if(++fpsskip_cnt==2)
    {
     fps_skip = (float)2000/(float)fpsskip_tck;
     fps_skip +=6.0f;
     fpsskip_cnt = 0;
     fpsskip_tck = 1;
    }
  }

 fps_tck += _ticks_since_last_update;

 if(++fps_cnt==20)
  {
   fps_cur = (float)(TIMEBASE*20)/(float)fps_tck;

   fps_cnt = 0;
   fps_tck = 1;

   //if(UseFrameLimit && fps_cur>fFrameRateHz)           // optical adjust ;) avoids flickering fps display
    //fps_cur=fFrameRateHz;
  }

}

void PCFrameCap (void)
{
 static unsigned long curticks, lastticks, _ticks_since_last_update;
 static unsigned long TicksToWait = 0;
 BOOL Waiting = TRUE;

 while (Waiting)
  {
   curticks = timeGetTime();
   _ticks_since_last_update = curticks - lastticks;
   if ((_ticks_since_last_update > TicksToWait) ||
       (curticks < lastticks))
    {
     Waiting = FALSE;
     lastticks = curticks;
     TicksToWait = (TIMEBASE/ (unsigned long)fFrameRateHz);
    }
  }
}

void PCcalcfps(void)
{
 static unsigned long curticks,_ticks_since_last_update,lastticks;
 static long  fps_cnt = 0;
 static float fps_acc = 0;
 float CurrentFPS=0;

 curticks = timeGetTime();
 _ticks_since_last_update=curticks-lastticks;
 if(_ticks_since_last_update)
      CurrentFPS=(float)TIMEBASE/(float)_ticks_since_last_update;
 else CurrentFPS = 0;
 lastticks = curticks;

 fps_acc += CurrentFPS;

 if(++fps_cnt==10)
  {
   fps_cur = fps_acc / 10;
   fps_acc = 0;
   fps_cnt = 0;
  }

 fps_skip=CurrentFPS+1.0f;
}

void SetAutoFrameCap(void)
{
 if(iFrameLimit==1)
  {
   fFrameRateHz = fFrameRate;
   dwFrameRateTicks=(TIMEBASE*100 / (unsigned long)(fFrameRateHz*100));
   return;
  }

 if(dwActFixes&32)
  {
   if (PSXDisplay.Interlaced)
        fFrameRateHz = PSXDisplay.PAL?50.0f:60.0f;
   else fFrameRateHz = PSXDisplay.PAL?25.0f:30.0f;
  }
 else
  {
   fFrameRateHz = PSXDisplay.PAL?50.0f:59.94f;
   dwFrameRateTicks=(TIMEBASE*100 / (unsigned long)(fFrameRateHz*100));
  }
}

void SetFPSHandler(void)
{
}

void InitFPS(void)
{
 if(!fFrameRate) fFrameRate=200.0f;
 if(fFrameRateHz==0) fFrameRateHz=fFrameRate;          // set user framerate
 dwFrameRateTicks=(TIMEBASE / (unsigned long)fFrameRateHz);
}


////////////////////////////////////////////////////////////////////////
// former draw.c
////////////////////////////////////////////////////////////////////////


void          DoBufferSwap(void);
void          CreatePic(unsigned char * pMem);
void          DestroyPic(void);
void          DisplayPic(void);
void          ShowGpuPic(void);
void          ShowTextGpuPic(void);

static long           lLowerpart;
static BOOL           bCheckMask = FALSE;
static unsigned short sSetMask = 0;
unsigned long  lSetMask = 0;
static int            iShowFPS = 0;
static int            iMaintainAspect = 0;
static int            iUseNoStretchBlt = 0;
static char* Scaler = "None";
static int            iFastFwd = 0;
static PSXPoint_t     ptCursorPoint[8];
static unsigned short usCursorActive = 0;


////////////////////////////////////////////////////////////////////////


void BlitScreen32(uint8_t *surf, int x, int y)
{
 unsigned char *pD;
 unsigned int startxy;
 uint32_t lu;
 unsigned short s;
 unsigned short row, column;
 unsigned short dx = PreviousPSXDisplay.Range.x1;
 unsigned short dy = PreviousPSXDisplay.DisplayMode.y;

 int32_t lPitch = PSXDisplay.DisplayMode.x << 2;

 uint32_t *destpix;

 if (PreviousPSXDisplay.Range.y0) // centering needed?
  {
   memset(surf, 0, (PreviousPSXDisplay.Range.y0 >> 1) * lPitch);

   dy -= PreviousPSXDisplay.Range.y0;
   surf += (PreviousPSXDisplay.Range.y0 >> 1) * lPitch;

   memset(surf + dy * lPitch,
          0, ((PreviousPSXDisplay.Range.y0 + 1) >> 1) * lPitch);
  }

 if (PreviousPSXDisplay.Range.x0)
  {
   for (column = 0; column < dy; column++)
    {
     destpix = (uint32_t *)(surf + (column * lPitch));
     memset(destpix, 0, PreviousPSXDisplay.Range.x0 << 2);
    }
   surf += PreviousPSXDisplay.Range.x0 << 2;
  }

 if (PSXDisplay.RGB24)
  {
   for (column = 0; column < dy; column++)
    {
     startxy = ((1024) * (column + y)) + x;
     pD = (unsigned char *)&psxVuw[startxy];
     destpix = (uint32_t *)(surf + (column * lPitch));
     for (row = 0; row < dx; row++)
      {
       lu = *((uint32_t *)pD);
       destpix[row] = (RED(lu) << 16) | (GREEN(lu) << 8) | (BLUE(lu));
       pD += 3;
      }
    }
  }
 else
  {
   for (column = 0;column<dy;column++)
    {
     startxy = (1024 * (column + y)) + x;
     destpix = (uint32_t *)(surf + (column * lPitch));
     for (row = 0; row < dx; row++)
      {
       s = GETLE16(&psxVuw[startxy++]);
       destpix[row] = (((s << 19) & 0xf80000)>>16) | ((s << 6) & 0xf800) | (((s >> 7) & 0xf8)<<16);
      }
    }
  }
}

//Note: dest x,y,w,h are both input and output variables
inline void MaintainAspect(unsigned int *dx,unsigned int *dy,unsigned int *dw,unsigned int *dh)
{
	//Currently just 4/3 aspect ratio
	int t;

	if (*dw * 3 > *dh * 4) {
		t = *dh * 4.0f / 3;	//new width aspect
		*dx = (*dw - t) / 2;	//centering
		*dw = t;
	} else {
		t = *dw * 3.0f / 4;
		*dy = (*dh - t) / 2;
		*dh = t;
	}
}


void GPU_pick(int x, int y, int w, int h) {
  x = x*PSXDisplay.DisplayMode.x/w;
  y = y*PSXDisplay.DisplayMode.y/h;
  printf("pick %d,%d\n", x, y);
}

void GPU_dumpVRAM() {
  char Tmp[PATHLEN];
  sprintf(Tmp, "%s/snaps/vram.bin", WorkDir);
  printf("Dumping %s\n", Tmp);
  saveFile(Tmp, psxVSecure, 0x100000);
  sprintf(Tmp, "%s/snaps/ram.bin", WorkDir);
  printf("Dumping %s\n", Tmp);
  saveFile(Tmp, psxM, 0x200000);
}

char *GPU_GetScaler() {return Scaler;}
void GPU_SetScaler(char *Name) {Scaler = Name;}


void DoBufferSwap() {
  char Tmp[PATHLEN];
  int i, w, h;
  uint8_t *S;
  void (*sf) (unsigned char *, DWORD, unsigned char *, int, int) = 0;

	finalw = PSXDisplay.DisplayMode.x;
	finalh = PSXDisplay.DisplayMode.y;

	if (finalw == 0 || finalh == 0) return;

  BlitScreen32(FB, PSXDisplay.DisplayPosition.x, PSXDisplay.DisplayPosition.y);

  if (SnapRequested) {
    sprintf(Tmp, "%s/snaps/%05d.bmp", WorkDir, SnapCount++);
    printf("Saving %s\n", Tmp);
    saveBMP(Tmp, FB, finalw, finalh, 0);
    SnapRequested = 0;
  }

  for (i=0; Scalers[i].Name; i++) if (!strcmp(Scalers[i].Name, Scaler)) {
    sf = (void (*) (unsigned char *, DWORD, unsigned char *, int, int))Scalers[i].F;
    break;
  }

  // note that scalers modify finalw and finalh for us
  if (sf && finalw <= 320 && finalh <= 256) {
    sf(FB, finalw*4, SB, finalw, finalh);
    S = SB;
  } else {
    S = FB;
  }

  uint8_t *p = screenBlitStart(&w,&h);
  resample((uint32_t*)p, w, h, (uint32_t*)S, finalw, finalh);
  screenBlitEnd();
}


void DoClearScreenBuffer(void) {
  int w, h;
  uint8_t *p = screenBlitStart(&w,&h);
  memset(p,0,w*h*4);
}


void CreatePic(unsigned char * pMem)
{
  printf("FIXME: CreatePic isnt impelemnted\n");
}

void DestroyPic(void)
{
  printf("FIXME: DestroyPic isnt impelemnted\n");
}


void DisplayPic(void)
{
  printf("FIXME: DisplayPic isnt impelemnted\n");
}

void ShowGpuPic(void)
{
}

void ShowTextGpuPic(void)
{
}


////////////////////////////////////////////////////////////////////////
// prim.c
////////////////////////////////////////////////////////////////////////

// Update global TP infos
__inline void UpdateGlobalTP(unsigned short gdata)
{
 GlobalTextAddrX = (gdata << 6) & 0x3c0;               // texture addr

 if(iGPUHeight==1024)
  {
   if(dwGPUVersion==2)
    {
     GlobalTextAddrY =((gdata & 0x60 ) << 3);
     GlobalTextIL    =(gdata & 0x2000) >> 13;
     GlobalTextABR = (unsigned short)((gdata >> 7) & 0x3);
     GlobalTextTP = (gdata >> 9) & 0x3;
     if(GlobalTextTP==3) GlobalTextTP=2;
     usMirror =0;
     lGPUstatusRet = (lGPUstatusRet & 0xffffe000 ) | (gdata & 0x1fff );

     // tekken dithering? right now only if dithering is forced by user
     if(iUseDither==2) iDither=2; else iDither=0;

     return;
    }
   else
    {
     GlobalTextAddrY = (unsigned short)(((gdata << 4) & 0x100) | ((gdata >> 2) & 0x200));
    }
  }
 else GlobalTextAddrY = (gdata << 4) & 0x100;

 GlobalTextTP = (gdata >> 7) & 0x3;                    // tex mode (4,8,15)

 if(GlobalTextTP==3) GlobalTextTP=2;                   // seen in Wild9 :(

 GlobalTextABR = (gdata >> 5) & 0x3;                   // blend mode

 lGPUstatusRet&=~0x000001ff;                           // Clear the necessary bits
 lGPUstatusRet|=(gdata & 0x01ff);                      // set the necessary bits

 switch(iUseDither)
 {
  case 0:
   iDither=0;
  break;
  case 1:
   if(lGPUstatusRet&0x0200) iDither=2;
   else iDither=0;
  break;
  case 2:
   iDither=2;
  break;
 }
}

////////////////////////////////////////////////////////////////////////

__inline void SetRenderMode(uint32_t DrawAttributes)
{
 DrawSemiTrans = (SEMITRANSBIT(DrawAttributes)) ? TRUE : FALSE;

 if(SHADETEXBIT(DrawAttributes)) 
  {g_m1=g_m2=g_m3=128;}
 else
  {
   if((dwActFixes&4) && ((DrawAttributes&0x00ffffff)==0))
    DrawAttributes|=0x007f7f7f;

   g_m1=(short)(DrawAttributes&0xff);
   g_m2=(short)((DrawAttributes>>8)&0xff);
   g_m3=(short)((DrawAttributes>>16)&0xff);
  }
}

////////////////////////////////////////////////////////////////////////

// oki, here are the psx gpu coord rules: poly coords are
// 11 bit signed values (-1024...1023). If the x or y distance 
// exceeds 1024, the polygon will not be drawn. 
// Since quads are treated as two triangles by the real gpu,
// this 'discard rule' applies to each of the quad's triangle 
// (so one triangle can be drawn, the other one discarded). 
// Also, y drawing is wrapped at 512 one time,
// then it will get negative (and therefore not drawn). The
// 'CheckCoord' funcs are a simple (not comlete!) approach to
// do things right, I will add a better detection soon... the 
// current approach will be easier to do in hw/accel plugins, imho

// 11 bit signed
#define SIGNSHIFT 21
#define CHKMAX_X 1024
#define CHKMAX_Y 512

void AdjustCoord4()
{
 lx0=(short)(((int)lx0<<SIGNSHIFT)>>SIGNSHIFT);
 lx1=(short)(((int)lx1<<SIGNSHIFT)>>SIGNSHIFT);
 lx2=(short)(((int)lx2<<SIGNSHIFT)>>SIGNSHIFT);
 lx3=(short)(((int)lx3<<SIGNSHIFT)>>SIGNSHIFT);
 ly0=(short)(((int)ly0<<SIGNSHIFT)>>SIGNSHIFT);
 ly1=(short)(((int)ly1<<SIGNSHIFT)>>SIGNSHIFT);
 ly2=(short)(((int)ly2<<SIGNSHIFT)>>SIGNSHIFT);
 ly3=(short)(((int)ly3<<SIGNSHIFT)>>SIGNSHIFT);
}

void AdjustCoord3()
{
 lx0=(short)(((int)lx0<<SIGNSHIFT)>>SIGNSHIFT);
 lx1=(short)(((int)lx1<<SIGNSHIFT)>>SIGNSHIFT);
 lx2=(short)(((int)lx2<<SIGNSHIFT)>>SIGNSHIFT);
 ly0=(short)(((int)ly0<<SIGNSHIFT)>>SIGNSHIFT);
 ly1=(short)(((int)ly1<<SIGNSHIFT)>>SIGNSHIFT);
 ly2=(short)(((int)ly2<<SIGNSHIFT)>>SIGNSHIFT);
}

void AdjustCoord2()
{
 lx0=(short)(((int)lx0<<SIGNSHIFT)>>SIGNSHIFT);
 lx1=(short)(((int)lx1<<SIGNSHIFT)>>SIGNSHIFT);
 ly0=(short)(((int)ly0<<SIGNSHIFT)>>SIGNSHIFT);
 ly1=(short)(((int)ly1<<SIGNSHIFT)>>SIGNSHIFT);
}

void AdjustCoord1()
{
 lx0=(short)(((int)lx0<<SIGNSHIFT)>>SIGNSHIFT);
 ly0=(short)(((int)ly0<<SIGNSHIFT)>>SIGNSHIFT);

 if(lx0<-512 && PSXDisplay.DrawOffset.x<=-512)
  lx0+=2048;

 if(ly0<-512 && PSXDisplay.DrawOffset.y<=-512)
  ly0+=2048;
}

/*
////////////////////////////////////////////////////////////////////////
// special checks... nascar, syphon filter 2, mgs
////////////////////////////////////////////////////////////////////////

// xenogears FT4: not removed correctly right now... the tri 0,1,2
// should get removed, the tri 1,2,3 should stay... pfff

// x -466 1023 180 1023
// y   20 -228 222 -100

// 0 __1
//  \ / \ 
//   2___3

*/

__inline BOOL CheckCoord4()
{
 if(lx0<0)
  {
   if(((lx1-lx0)>CHKMAX_X) ||
      ((lx2-lx0)>CHKMAX_X)) 
    {
     if(lx3<0)
      {
       if((lx1-lx3)>CHKMAX_X) return TRUE;
       if((lx2-lx3)>CHKMAX_X) return TRUE;
      }
    }
  }
 if(lx1<0)
  {
   if((lx0-lx1)>CHKMAX_X) return TRUE;
   if((lx2-lx1)>CHKMAX_X) return TRUE;
   if((lx3-lx1)>CHKMAX_X) return TRUE;
  }
 if(lx2<0)
  {
   if((lx0-lx2)>CHKMAX_X) return TRUE;
   if((lx1-lx2)>CHKMAX_X) return TRUE;
   if((lx3-lx2)>CHKMAX_X) return TRUE;
  }
 if(lx3<0)
  {
   if(((lx1-lx3)>CHKMAX_X) ||
      ((lx2-lx3)>CHKMAX_X))
    {
     if(lx0<0)
      {
       if((lx1-lx0)>CHKMAX_X) return TRUE;
       if((lx2-lx0)>CHKMAX_X) return TRUE;
      }
    }
  }
 

 if(ly0<0)
  {
   if((ly1-ly0)>CHKMAX_Y) return TRUE;
   if((ly2-ly0)>CHKMAX_Y) return TRUE;
  }
 if(ly1<0)
  {
   if((ly0-ly1)>CHKMAX_Y) return TRUE;
   if((ly2-ly1)>CHKMAX_Y) return TRUE;
   if((ly3-ly1)>CHKMAX_Y) return TRUE;
  }
 if(ly2<0)
  {
   if((ly0-ly2)>CHKMAX_Y) return TRUE;
   if((ly1-ly2)>CHKMAX_Y) return TRUE;
   if((ly3-ly2)>CHKMAX_Y) return TRUE;
  }
 if(ly3<0)
  {
   if((ly1-ly3)>CHKMAX_Y) return TRUE;
   if((ly2-ly3)>CHKMAX_Y) return TRUE;
  }

 return FALSE;
}

__inline BOOL CheckCoord3()
{
 if(lx0<0)
  {
   if((lx1-lx0)>CHKMAX_X) return TRUE;
   if((lx2-lx0)>CHKMAX_X) return TRUE;
  }
 if(lx1<0)
  {
   if((lx0-lx1)>CHKMAX_X) return TRUE;
   if((lx2-lx1)>CHKMAX_X) return TRUE;
  }
 if(lx2<0)
  {
   if((lx0-lx2)>CHKMAX_X) return TRUE;
   if((lx1-lx2)>CHKMAX_X) return TRUE;
  }
 if(ly0<0)
  {
   if((ly1-ly0)>CHKMAX_Y) return TRUE;
   if((ly2-ly0)>CHKMAX_Y) return TRUE;
  }
 if(ly1<0)
  {
   if((ly0-ly1)>CHKMAX_Y) return TRUE;
   if((ly2-ly1)>CHKMAX_Y) return TRUE;
  }
 if(ly2<0)
  {
   if((ly0-ly2)>CHKMAX_Y) return TRUE;
   if((ly1-ly2)>CHKMAX_Y) return TRUE;
  }

 return FALSE;
}


__inline BOOL CheckCoord2()
{
 if(lx0<0)
  {
   if((lx1-lx0)>CHKMAX_X) return TRUE;
  }
 if(lx1<0)
  {
   if((lx0-lx1)>CHKMAX_X) return TRUE;
  }
 if(ly0<0)
  {
   if((ly1-ly0)>CHKMAX_Y) return TRUE;
  }
 if(ly1<0)
  {
   if((ly0-ly1)>CHKMAX_Y) return TRUE;
  }

 return FALSE;
}

__inline BOOL CheckCoordL(short slx0,short sly0,short slx1,short sly1)
{
 if(slx0<0)
  {
   if((slx1-slx0)>CHKMAX_X) return TRUE;
  }
 if(slx1<0)
  {
   if((slx0-slx1)>CHKMAX_X) return TRUE;
  }
 if(sly0<0)
  {
   if((sly1-sly0)>CHKMAX_Y) return TRUE;
  }
 if(sly1<0)
  {
   if((sly0-sly1)>CHKMAX_Y) return TRUE;
  }

 return FALSE;
}


////////////////////////////////////////////////////////////////////////
// mask stuff... used in silent hill
////////////////////////////////////////////////////////////////////////

void cmdSTP(unsigned char * baseAddr)
{
 uint32_t gdata = GETLE32(&((uint32_t*)baseAddr)[0]);

 lGPUstatusRet&=~0x1800;                                   // Clear the necessary bits
 lGPUstatusRet|=((gdata & 0x03) << 11);                    // Set the necessary bits

 if(gdata&1) {sSetMask=0x8000;lSetMask=0x80008000;}
 else        {sSetMask=0;     lSetMask=0;         }

 if(gdata&2) bCheckMask=TRUE;
 else        bCheckMask=FALSE;
}
 
////////////////////////////////////////////////////////////////////////
// cmd: Set texture page infos
////////////////////////////////////////////////////////////////////////

void cmdTexturePage(unsigned char * baseAddr)
{
 uint32_t gdata = GETLE32(&((uint32_t*)baseAddr)[0]);

 lGPUstatusRet&=~0x000007ff;
 lGPUstatusRet|=(gdata & 0x07ff);
 
 usMirror=gdata&0x3000;
 
 UpdateGlobalTP((unsigned short)gdata);
 GlobalTextREST = (gdata&0x00ffffff)>>9;
}

////////////////////////////////////////////////////////////////////////
// cmd: turn on/off texture window
////////////////////////////////////////////////////////////////////////

void cmdTextureWindow(unsigned char *baseAddr)
{
 uint32_t gdata = GETLE32(&((uint32_t*)baseAddr)[0]);

 uint32_t YAlign,XAlign;

 lGPUInfoVals[INFO_TW]=gdata&0xFFFFF;

 if(gdata & 0x020)
  TWin.Position.y1 = 8;    // xxxx1
 else if (gdata & 0x040)
  TWin.Position.y1 = 16;   // xxx10
 else if (gdata & 0x080)
  TWin.Position.y1 = 32;   // xx100
 else if (gdata & 0x100)
  TWin.Position.y1 = 64;   // x1000
 else if (gdata & 0x200)
  TWin.Position.y1 = 128;  // 10000
 else
  TWin.Position.y1 = 256;  // 00000

  // Texture window size is determined by the least bit set of the relevant 5 bits

 if (gdata & 0x001)
  TWin.Position.x1 = 8;    // xxxx1
 else if (gdata & 0x002)
  TWin.Position.x1 = 16;   // xxx10
 else if (gdata & 0x004)
  TWin.Position.x1 = 32;   // xx100
 else if (gdata & 0x008)
  TWin.Position.x1 = 64;   // x1000
 else if (gdata & 0x010)
  TWin.Position.x1 = 128;  // 10000
 else
  TWin.Position.x1 = 256;  // 00000

 // Re-calculate the bit field, because we can't trust what is passed in the data


 YAlign = (uint32_t)(32 - (TWin.Position.y1 >> 3));
 XAlign = (uint32_t)(32 - (TWin.Position.x1 >> 3));

 // Absolute position of the start of the texture window

 TWin.Position.y0 = (short)(((gdata >> 15) & YAlign) << 3);
 TWin.Position.x0 = (short)(((gdata >> 10) & XAlign) << 3);

 if((TWin.Position.x0 == 0 &&                          // tw turned off
     TWin.Position.y0 == 0 &&
     TWin.Position.x1 == 0 &&
     TWin.Position.y1 == 0) ||  
     (TWin.Position.x1 == 256 &&
      TWin.Position.y1 == 256))
  {
   bUsingTWin = FALSE;                                 // -> just do it
  }                                                    
 else                                                  // otherwise
  {
   bUsingTWin = TRUE;                                  // -> tw turned on
  }
}

////////////////////////////////////////////////////////////////////////
// cmd: start of drawing area... primitives will be clipped inside
////////////////////////////////////////////////////////////////////////



void cmdDrawAreaStart(unsigned char * baseAddr)
{
 uint32_t gdata = GETLE32(&((uint32_t*)baseAddr)[0]);

 drawX  = gdata & 0x3ff;                               // for soft drawing

 if(dwGPUVersion==2)
  {
   lGPUInfoVals[INFO_DRAWSTART]=gdata&0x3FFFFF;
   drawY  = (gdata>>12)&0x3ff;
   if(drawY>=1024) drawY=1023;                         // some security
  }
 else
  {
   lGPUInfoVals[INFO_DRAWSTART]=gdata&0xFFFFF;
   drawY  = (gdata>>10)&0x3ff;
   if(drawY>=512) drawY=511;                           // some security
  }
}

////////////////////////////////////////////////////////////////////////
// cmd: end of drawing area... primitives will be clipped inside
////////////////////////////////////////////////////////////////////////

void cmdDrawAreaEnd(unsigned char * baseAddr)
{
 uint32_t gdata = GETLE32(&((uint32_t*)baseAddr)[0]);

 drawW  = gdata & 0x3ff;                               // for soft drawing

 if(dwGPUVersion==2)
  {
   lGPUInfoVals[INFO_DRAWEND]=gdata&0x3FFFFF;
   drawH  = (gdata>>12)&0x3ff;
   if(drawH>=1024) drawH=1023;                         // some security
  }
 else
  {
   lGPUInfoVals[INFO_DRAWEND]=gdata&0xFFFFF;
   drawH  = (gdata>>10)&0x3ff;
   if(drawH>=512) drawH=511;                           // some security
  }
}

////////////////////////////////////////////////////////////////////////
// cmd: draw offset... will be added to prim coords
////////////////////////////////////////////////////////////////////////

void cmdDrawOffset(unsigned char * baseAddr)
{
 uint32_t gdata = GETLE32(&((uint32_t*)baseAddr)[0]);

 PSXDisplay.DrawOffset.x = (short)(gdata & 0x7ff);

 if(dwGPUVersion==2)
  {
   lGPUInfoVals[INFO_DRAWOFF]=gdata&0x7FFFFF;
   PSXDisplay.DrawOffset.y = (short)((gdata>>12) & 0x7ff);
  }
 else
  {
   lGPUInfoVals[INFO_DRAWOFF]=gdata&0x3FFFFF;
   PSXDisplay.DrawOffset.y = (short)((gdata>>11) & 0x7ff);
  }
 
 PSXDisplay.DrawOffset.y=(short)(((int)PSXDisplay.DrawOffset.y<<21)>>21);
 PSXDisplay.DrawOffset.x=(short)(((int)PSXDisplay.DrawOffset.x<<21)>>21);
}
 
////////////////////////////////////////////////////////////////////////
// cmd: load image to vram
////////////////////////////////////////////////////////////////////////

void primLoadImage(unsigned char * baseAddr)
{
 unsigned short *sgpuData = ((unsigned short *) baseAddr);

 VRAMWrite.x      = GETLEs16(&sgpuData[2])&0x3ff;
 VRAMWrite.y      = GETLEs16(&sgpuData[3])&iGPUHeightMask;
 VRAMWrite.Width  = GETLEs16(&sgpuData[4]);
 VRAMWrite.Height = GETLEs16(&sgpuData[5]);

 DataWriteMode = DR_VRAMTRANSFER;

 VRAMWrite.ImagePtr = psxVuw + (VRAMWrite.y<<10) + VRAMWrite.x;
 VRAMWrite.RowsRemaining = VRAMWrite.Width;
 VRAMWrite.ColsRemaining = VRAMWrite.Height;
}

////////////////////////////////////////////////////////////////////////
// cmd: vram -> psx mem
////////////////////////////////////////////////////////////////////////

void primStoreImage(unsigned char * baseAddr)
{
 unsigned short *sgpuData = ((unsigned short *) baseAddr);

 VRAMRead.x      = GETLEs16(&sgpuData[2])&0x03ff;
 VRAMRead.y      = GETLEs16(&sgpuData[3])&iGPUHeightMask;
 VRAMRead.Width  = GETLEs16(&sgpuData[4]);
 VRAMRead.Height = GETLEs16(&sgpuData[5]);

 VRAMRead.ImagePtr = psxVuw + (VRAMRead.y<<10) + VRAMRead.x;
 VRAMRead.RowsRemaining = VRAMRead.Width;
 VRAMRead.ColsRemaining = VRAMRead.Height;

 DataReadMode = DR_VRAMTRANSFER;

 lGPUstatusRet |= GPUSTATUS_READYFORVRAM;
}

////////////////////////////////////////////////////////////////////////
// cmd: blkfill - NO primitive! Doesn't care about draw areas...
////////////////////////////////////////////////////////////////////////

void primBlkFill(unsigned char * baseAddr)
{
 uint32_t *gpuData = ((uint32_t *) baseAddr);
 short *sgpuData = ((short *) baseAddr);

 short sX = GETLEs16(&sgpuData[2]);
 short sY = GETLEs16(&sgpuData[3]);
 short sW = GETLEs16(&sgpuData[4]) & 0x3ff;
 short sH = GETLEs16(&sgpuData[5]) & 0x3ff;

 sW = (sW+15) & ~15;

 // Increase H & W if they are one short of full values, because they never can be full values
 if (sH >= 1023) sH=1024;
 if (sW >= 1023) sW=1024; 

 // x and y of end pos
 sW+=sX;
 sH+=sY;

 FillSoftwareArea(sX, sY, sW, sH, BGR24to16(GETLE32(&gpuData[0])));

 bDoVSyncUpdate=TRUE;
}
 
////////////////////////////////////////////////////////////////////////
// cmd: move image vram -> vram
////////////////////////////////////////////////////////////////////////

void primMoveImage(unsigned char * baseAddr)
{
 short *sgpuData = ((short *) baseAddr);

 short imageY0,imageX0,imageY1,imageX1,imageSX,imageSY,i,j;

 imageX0 = GETLEs16(&sgpuData[2])&0x03ff;
 imageY0 = GETLEs16(&sgpuData[3])&iGPUHeightMask;
 imageX1 = GETLEs16(&sgpuData[4])&0x03ff;
 imageY1 = GETLEs16(&sgpuData[5])&iGPUHeightMask;
 imageSX = GETLEs16(&sgpuData[6]);
 imageSY = GETLEs16(&sgpuData[7]);

 if((imageX0 == imageX1) && (imageY0 == imageY1)) return; 
 if(imageSX<=0)  return;
 if(imageSY<=0)  return;

 // ZN SF2: screwed moves
 // 
 // move sgpuData[2],sgpuData[3],sgpuData[4],sgpuData[5],sgpuData[6],sgpuData[7]
 // 
 // move 365 182 32723 -21846 17219  15427
 // move 127 160 147   -1     20817  13409
 // move 141 165 16275 -21862 -32126 13442
 // move 161 136 24620 -1     16962  13388
 // move 168 138 32556 -13090 -29556 15500
 //
 // and here's the hack for it:

 if(iGPUHeight==1024 && GETLEs16(&sgpuData[7])>1024) return;

 if((imageY0+imageSY)>iGPUHeight ||
    (imageX0+imageSX)>1024       ||
    (imageY1+imageSY)>iGPUHeight ||
    (imageX1+imageSX)>1024)
  {
   int i,j;
   for(j=0;j<imageSY;j++)
    for(i=0;i<imageSX;i++)
     psxVuw [(1024*((imageY1+j)&iGPUHeightMask))+((imageX1+i)&0x3ff)]=
      psxVuw[(1024*((imageY0+j)&iGPUHeightMask))+((imageX0+i)&0x3ff)];

   bDoVSyncUpdate=TRUE;
 
   return;
  }
 
 if(imageSX&1)                                         // not dword aligned? slower func
  {
   unsigned short *SRCPtr, *DSTPtr;
   unsigned short LineOffset;

   SRCPtr = psxVuw + (1024*imageY0) + imageX0;
   DSTPtr = psxVuw + (1024*imageY1) + imageX1;

   LineOffset = 1024 - imageSX;

   for(j=0;j<imageSY;j++)
    {
     for(i=0;i<imageSX;i++) *DSTPtr++ = *SRCPtr++;
     SRCPtr += LineOffset;
     DSTPtr += LineOffset;
    }
  }
 else                                                  // dword aligned
  {
   uint32_t *SRCPtr, *DSTPtr;
   unsigned short LineOffset;
   int dx=imageSX>>1;

   SRCPtr = (uint32_t *)(psxVuw + (1024*imageY0) + imageX0);
   DSTPtr = (uint32_t *)(psxVuw + (1024*imageY1) + imageX1);

   LineOffset = 512 - dx;

   for(j=0;j<imageSY;j++)
    {
     for(i=0;i<dx;i++) *DSTPtr++ = *SRCPtr++;
     SRCPtr += LineOffset;
     DSTPtr += LineOffset;
    }
  }

 imageSX+=imageX1;
 imageSY+=imageY1;

/*
 if(!PSXDisplay.Interlaced)                            // stupid frame skip stuff
  {
   if(UseFrameSkip &&
      imageX1<PSXDisplay.DisplayEnd.x &&
      imageSX>=PSXDisplay.DisplayPosition.x &&
      imageY1<PSXDisplay.DisplayEnd.y &&
      imageSY>=PSXDisplay.DisplayPosition.y)
    updateDisplay();
  }
*/

 bDoVSyncUpdate=TRUE;
}

////////////////////////////////////////////////////////////////////////
// cmd: draw free-size Tile 
////////////////////////////////////////////////////////////////////////

//#define SMALLDEBUG
//#include <dbgout.h>

void primTileS(unsigned char * baseAddr)
{
 uint32_t *gpuData = ((uint32_t*)baseAddr);
 short *sgpuData = ((short *) baseAddr);
 short sW = GETLEs16(&sgpuData[4]) & 0x3ff;
 short sH = GETLEs16(&sgpuData[5]) & iGPUHeightMask;              // mmm... limit tiles to 0x1ff or height?

 lx0 = GETLEs16(&sgpuData[2]);
 ly0 = GETLEs16(&sgpuData[3]);

 if(!(dwActFixes&8)) AdjustCoord1();
                      
 // x and y of start
 ly2 = ly3 = ly0+sH +PSXDisplay.DrawOffset.y;
 ly0 = ly1 = ly0    +PSXDisplay.DrawOffset.y;
 lx1 = lx2 = lx0+sW +PSXDisplay.DrawOffset.x;
 lx0 = lx3 = lx0    +PSXDisplay.DrawOffset.x;

 DrawSemiTrans = (SEMITRANSBIT(GETLE32(&gpuData[0]))) ? TRUE : FALSE;

 if(!(iTileCheat && sH==32 && GETLE32(&gpuData[0])==0x60ffffff)) // special cheat for certain ZiNc games
  FillSoftwareAreaTrans(lx0,ly0,lx2,ly2,
                        BGR24to16(GETLE32(&gpuData[0])));          

 bDoVSyncUpdate=TRUE;
}

////////////////////////////////////////////////////////////////////////
// cmd: draw 1 dot Tile (point)
////////////////////////////////////////////////////////////////////////

void primTile1(unsigned char * baseAddr)
{
 uint32_t *gpuData = ((uint32_t*)baseAddr);
 short *sgpuData = ((short *) baseAddr);
 short sH = 1;
 short sW = 1;

 lx0 = GETLEs16(&sgpuData[2]);
 ly0 = GETLEs16(&sgpuData[3]);

 if(!(dwActFixes&8)) AdjustCoord1();

 // x and y of start
 ly2 = ly3 = ly0+sH +PSXDisplay.DrawOffset.y;
 ly0 = ly1 = ly0    +PSXDisplay.DrawOffset.y;
 lx1 = lx2 = lx0+sW +PSXDisplay.DrawOffset.x;
 lx0 = lx3 = lx0    +PSXDisplay.DrawOffset.x;

 DrawSemiTrans = (SEMITRANSBIT(GETLE32(&gpuData[0]))) ? TRUE : FALSE;

 FillSoftwareAreaTrans(lx0,ly0,lx2,ly2,
                       BGR24to16(GETLE32(&gpuData[0])));          // Takes Start and Offset

 bDoVSyncUpdate=TRUE;
}

////////////////////////////////////////////////////////////////////////
// cmd: draw 8 dot Tile (small rect)
////////////////////////////////////////////////////////////////////////

void primTile8(unsigned char * baseAddr)
{
 uint32_t *gpuData = ((uint32_t*)baseAddr);
 short *sgpuData = ((short *) baseAddr);
 short sH = 8;
 short sW = 8;

 lx0 = GETLEs16(&sgpuData[2]);
 ly0 = GETLEs16(&sgpuData[3]);

 if(!(dwActFixes&8)) AdjustCoord1();

 // x and y of start
 ly2 = ly3 = ly0+sH +PSXDisplay.DrawOffset.y;
 ly0 = ly1 = ly0    +PSXDisplay.DrawOffset.y;
 lx1 = lx2 = lx0+sW +PSXDisplay.DrawOffset.x;
 lx0 = lx3 = lx0    +PSXDisplay.DrawOffset.x;

 DrawSemiTrans = (SEMITRANSBIT(GETLE32(&gpuData[0]))) ? TRUE : FALSE;

 FillSoftwareAreaTrans(lx0,ly0,lx2,ly2,
                       BGR24to16(GETLE32(&gpuData[0])));          // Takes Start and Offset

 bDoVSyncUpdate=TRUE;
}

////////////////////////////////////////////////////////////////////////
// cmd: draw 16 dot Tile (medium rect)
////////////////////////////////////////////////////////////////////////

void primTile16(unsigned char * baseAddr)
{
 uint32_t *gpuData = ((uint32_t*)baseAddr);
 short *sgpuData = ((short *) baseAddr);
 short sH = 16;
 short sW = 16;

 lx0 = GETLEs16(&sgpuData[2]);
 ly0 = GETLEs16(&sgpuData[3]);

 if(!(dwActFixes&8)) AdjustCoord1();

 // x and y of start
 ly2 = ly3 = ly0+sH +PSXDisplay.DrawOffset.y;
 ly0 = ly1 = ly0    +PSXDisplay.DrawOffset.y;
 lx1 = lx2 = lx0+sW +PSXDisplay.DrawOffset.x;
 lx0 = lx3 = lx0    +PSXDisplay.DrawOffset.x;

 DrawSemiTrans = (SEMITRANSBIT(GETLE32(&gpuData[0]))) ? TRUE : FALSE;

 FillSoftwareAreaTrans(lx0,ly0,lx2,ly2,
                       BGR24to16(GETLE32(&gpuData[0])));          // Takes Start and Offset

 bDoVSyncUpdate=TRUE;
}

////////////////////////////////////////////////////////////////////////
// cmd: small sprite (textured rect)
////////////////////////////////////////////////////////////////////////

void primSprt8(unsigned char * baseAddr)
{
 uint32_t *gpuData = ((uint32_t *) baseAddr);
 short *sgpuData = ((short *) baseAddr);

 lx0 = GETLEs16(&sgpuData[2]);
 ly0 = GETLEs16(&sgpuData[3]);

 if(!(dwActFixes&8)) AdjustCoord1();

 SetRenderMode(GETLE32(&gpuData[0]));

 if(bUsingTWin) DrawSoftwareSpriteTWin(baseAddr,8,8);
 else
 if(usMirror)   DrawSoftwareSpriteMirror(baseAddr,8,8);
 else           DrawSoftwareSprite(baseAddr,8,8,
                                   baseAddr[8],
                                   baseAddr[9]);

 bDoVSyncUpdate=TRUE;
}

////////////////////////////////////////////////////////////////////////
// cmd: medium sprite (textured rect)
////////////////////////////////////////////////////////////////////////

void primSprt16(unsigned char * baseAddr)
{
 uint32_t *gpuData = ((uint32_t *) baseAddr);
 short *sgpuData = ((short *) baseAddr);

 lx0 = GETLEs16(&sgpuData[2]);
 ly0 = GETLEs16(&sgpuData[3]);

 if(!(dwActFixes&8)) AdjustCoord1();

 SetRenderMode(GETLE32(&gpuData[0]));

 if(bUsingTWin) DrawSoftwareSpriteTWin(baseAddr,16,16);
 else
 if(usMirror)   DrawSoftwareSpriteMirror(baseAddr,16,16);
 else           DrawSoftwareSprite(baseAddr,16,16,
                                   baseAddr[8],
                                   baseAddr[9]);

 bDoVSyncUpdate=TRUE;
}

////////////////////////////////////////////////////////////////////////
// cmd: free-size sprite (textured rect)
////////////////////////////////////////////////////////////////////////

// func used on texture coord wrap
void primSprtSRest(unsigned char * baseAddr,unsigned short type)
{
 uint32_t *gpuData = ((uint32_t *) baseAddr);
 short *sgpuData = ((short *) baseAddr);
 unsigned short sTypeRest=0;

 short s;
 short sX = GETLEs16(&sgpuData[2]);
 short sY = GETLEs16(&sgpuData[3]);
 short sW = GETLEs16(&sgpuData[6]) & 0x3ff;
 short sH = GETLEs16(&sgpuData[7]) & 0x1ff;
 short tX = baseAddr[8];
 short tY = baseAddr[9];

 switch(type)
  {
   case 1:
    s=256-baseAddr[8];
    sW-=s;
    sX+=s;
    tX=0;
    break;
   case 2:
    s=256-baseAddr[9];
    sH-=s;
    sY+=s;
    tY=0;
    break;
   case 3:
    s=256-baseAddr[8];
    sW-=s;
    sX+=s;
    tX=0;
    s=256-baseAddr[9];
    sH-=s;
    sY+=s;
    tY=0;
    break;
   case 4:
    s=512-baseAddr[8];
    sW-=s;
    sX+=s;
    tX=0;
    break;
   case 5:
    s=512-baseAddr[9];
    sH-=s;
    sY+=s;
    tY=0;
    break;
   case 6:
    s=512-baseAddr[8];
    sW-=s;
    sX+=s;
    tX=0;
    s=512-baseAddr[9];
    sH-=s;
    sY+=s;
    tY=0;
    break;
  }

 SetRenderMode(GETLE32(&gpuData[0]));

 if(tX+sW>256) {sW=256-tX;sTypeRest+=1;}
 if(tY+sH>256) {sH=256-tY;sTypeRest+=2;}

 lx0 = sX;
 ly0 = sY;

 if(!(dwActFixes&8)) AdjustCoord1();

 DrawSoftwareSprite(baseAddr,sW,sH,tX,tY);

 if(sTypeRest && type<4)  
  {
   if(sTypeRest&1  && type==1)  primSprtSRest(baseAddr,4);
   if(sTypeRest&2  && type==2)  primSprtSRest(baseAddr,5);
   if(sTypeRest==3 && type==3)  primSprtSRest(baseAddr,6);
  }

}
                                     
////////////////////////////////////////////////////////////////////////

void primSprtS(unsigned char * baseAddr)
{
 uint32_t *gpuData = ((uint32_t *) baseAddr);
 short *sgpuData = ((short *) baseAddr);
 short sW,sH;

 lx0 = GETLEs16(&sgpuData[2]);
 ly0 = GETLEs16(&sgpuData[3]);

 if(!(dwActFixes&8)) AdjustCoord1();

 sW = GETLEs16(&sgpuData[6]) & 0x3ff;
 sH = GETLEs16(&sgpuData[7]) & 0x1ff;

 SetRenderMode(GETLE32(&gpuData[0]));

 if(bUsingTWin) DrawSoftwareSpriteTWin(baseAddr,sW,sH);
 else
 if(usMirror)   DrawSoftwareSpriteMirror(baseAddr,sW,sH);
 else          
  {
   unsigned short sTypeRest=0;
   short tX=baseAddr[8];
   short tY=baseAddr[9];

   if(tX+sW>256) {sW=256-tX;sTypeRest+=1;}
   if(tY+sH>256) {sH=256-tY;sTypeRest+=2;}

   DrawSoftwareSprite(baseAddr,sW,sH,tX,tY);

   if(sTypeRest) 
    {
     if(sTypeRest&1)  primSprtSRest(baseAddr,1);
     if(sTypeRest&2)  primSprtSRest(baseAddr,2);
     if(sTypeRest==3) primSprtSRest(baseAddr,3);
    }

  }

 bDoVSyncUpdate=TRUE;
}

////////////////////////////////////////////////////////////////////////
// cmd: flat shaded Poly4
////////////////////////////////////////////////////////////////////////

void primPolyF4(unsigned char *baseAddr)
{
 uint32_t *gpuData = ((uint32_t *) baseAddr);
 short *sgpuData = ((short *) baseAddr);

 lx0 = GETLEs16(&sgpuData[2]);
 ly0 = GETLEs16(&sgpuData[3]);
 lx1 = GETLEs16(&sgpuData[4]);
 ly1 = GETLEs16(&sgpuData[5]);
 lx2 = GETLEs16(&sgpuData[6]);
 ly2 = GETLEs16(&sgpuData[7]);
 lx3 = GETLEs16(&sgpuData[8]);
 ly3 = GETLEs16(&sgpuData[9]);

 if(!(dwActFixes&8)) 
  {
   AdjustCoord4();
   if(CheckCoord4()) return;
  }

 offsetPSX4();
 DrawSemiTrans = (SEMITRANSBIT(GETLE32(&gpuData[0]))) ? TRUE : FALSE;

 drawPoly4F(GETLE32(&gpuData[0]));

 bDoVSyncUpdate=TRUE;
}

////////////////////////////////////////////////////////////////////////
// cmd: smooth shaded Poly4
////////////////////////////////////////////////////////////////////////

void primPolyG4(unsigned char * baseAddr)
{
 uint32_t *gpuData = (uint32_t *)baseAddr;
 short *sgpuData = ((short *) baseAddr);

 lx0 = GETLEs16(&sgpuData[2]);
 ly0 = GETLEs16(&sgpuData[3]);
 lx1 = GETLEs16(&sgpuData[6]);
 ly1 = GETLEs16(&sgpuData[7]);
 lx2 = GETLEs16(&sgpuData[10]);
 ly2 = GETLEs16(&sgpuData[11]);
 lx3 = GETLEs16(&sgpuData[14]);
 ly3 = GETLEs16(&sgpuData[15]);

 if(!(dwActFixes&8))
  {
   AdjustCoord4();
   if(CheckCoord4()) return;
  }

 offsetPSX4();
 DrawSemiTrans = (SEMITRANSBIT(GETLE32(&gpuData[0]))) ? TRUE : FALSE;

 drawPoly4G(GETLE32(&gpuData[0]), GETLE32(&gpuData[2]), 
            GETLE32(&gpuData[4]), GETLE32(&gpuData[6]));

 bDoVSyncUpdate=TRUE;
}

////////////////////////////////////////////////////////////////////////
// cmd: flat shaded Texture3
////////////////////////////////////////////////////////////////////////

void primPolyFT3(unsigned char * baseAddr)
{
 uint32_t *gpuData = ((uint32_t *) baseAddr);
 short *sgpuData = ((short *) baseAddr);

 lx0 = GETLEs16(&sgpuData[2]);
 ly0 = GETLEs16(&sgpuData[3]);
 lx1 = GETLEs16(&sgpuData[6]);
 ly1 = GETLEs16(&sgpuData[7]);
 lx2 = GETLEs16(&sgpuData[10]);
 ly2 = GETLEs16(&sgpuData[11]);

 lLowerpart=GETLE32(&gpuData[4])>>16;
 UpdateGlobalTP((unsigned short)lLowerpart);

 if(!(dwActFixes&8))
  {
   AdjustCoord3();
   if(CheckCoord3()) return;
  }

 offsetPSX3();
 SetRenderMode(GETLE32(&gpuData[0]));

 drawPoly3FT(baseAddr);

 bDoVSyncUpdate=TRUE;
}

////////////////////////////////////////////////////////////////////////
// cmd: flat shaded Texture4
////////////////////////////////////////////////////////////////////////

void primPolyFT4(unsigned char * baseAddr)
{
 uint32_t *gpuData = ((uint32_t *) baseAddr);
 short *sgpuData = ((short *) baseAddr);

 lx0 = GETLEs16(&sgpuData[2]);
 ly0 = GETLEs16(&sgpuData[3]);
 lx1 = GETLEs16(&sgpuData[6]);
 ly1 = GETLEs16(&sgpuData[7]);
 lx2 = GETLEs16(&sgpuData[10]);
 ly2 = GETLEs16(&sgpuData[11]);
 lx3 = GETLEs16(&sgpuData[14]);
 ly3 = GETLEs16(&sgpuData[15]);

 lLowerpart=GETLE32(&gpuData[4])>>16;
 UpdateGlobalTP((unsigned short)lLowerpart);

 if(!(dwActFixes&8))
  {
   AdjustCoord4();
   if(CheckCoord4()) return;
  }

 offsetPSX4();

 SetRenderMode(GETLE32(&gpuData[0]));

 drawPoly4FT(baseAddr);

 bDoVSyncUpdate=TRUE;
}

////////////////////////////////////////////////////////////////////////
// cmd: smooth shaded Texture3
////////////////////////////////////////////////////////////////////////

void primPolyGT3(unsigned char *baseAddr)
{    
 uint32_t *gpuData = ((uint32_t *) baseAddr);
 short *sgpuData = ((short *) baseAddr);

 lx0 = GETLEs16(&sgpuData[2]);
 ly0 = GETLEs16(&sgpuData[3]);
 lx1 = GETLEs16(&sgpuData[8]);
 ly1 = GETLEs16(&sgpuData[9]);
 lx2 = GETLEs16(&sgpuData[14]);
 ly2 = GETLEs16(&sgpuData[15]);

 lLowerpart=GETLE32(&gpuData[5])>>16;
 UpdateGlobalTP((unsigned short)lLowerpart);

 if(!(dwActFixes&8))
  {
   AdjustCoord3();
   if(CheckCoord3()) return;
  }
           
 offsetPSX3();
 DrawSemiTrans = (SEMITRANSBIT(GETLE32(&gpuData[0]))) ? TRUE : FALSE;

 if(SHADETEXBIT(GETLE32(&gpuData[0])))
  {
   gpuData[0] = (gpuData[0]&HOST2LE32(0xff000000))|HOST2LE32(0x00808080);
   gpuData[3] = (gpuData[3]&HOST2LE32(0xff000000))|HOST2LE32(0x00808080);
   gpuData[6] = (gpuData[6]&HOST2LE32(0xff000000))|HOST2LE32(0x00808080);
  }

 drawPoly3GT(baseAddr);

 bDoVSyncUpdate=TRUE;
}

////////////////////////////////////////////////////////////////////////
// cmd: smooth shaded Poly3
////////////////////////////////////////////////////////////////////////

void primPolyG3(unsigned char *baseAddr)
{    
 uint32_t *gpuData = ((uint32_t *) baseAddr);
 short *sgpuData = ((short *) baseAddr);

 lx0 = GETLEs16(&sgpuData[2]);
 ly0 = GETLEs16(&sgpuData[3]);
 lx1 = GETLEs16(&sgpuData[6]);
 ly1 = GETLEs16(&sgpuData[7]);
 lx2 = GETLEs16(&sgpuData[10]);
 ly2 = GETLEs16(&sgpuData[11]);

 if(!(dwActFixes&8))
  {
   AdjustCoord3();
   if(CheckCoord3()) return;
  }

 offsetPSX3();
 DrawSemiTrans = (SEMITRANSBIT(GETLE32(&gpuData[0]))) ? TRUE : FALSE;

 drawPoly3G(GETLE32(&gpuData[0]), GETLE32(&gpuData[2]), GETLE32(&gpuData[4]));

 bDoVSyncUpdate=TRUE;
}

////////////////////////////////////////////////////////////////////////
// cmd: smooth shaded Texture4
////////////////////////////////////////////////////////////////////////

void primPolyGT4(unsigned char *baseAddr)
{ 
 uint32_t *gpuData = ((uint32_t *) baseAddr);
 short *sgpuData = ((short *) baseAddr);

 lx0 = GETLEs16(&sgpuData[2]);
 ly0 = GETLEs16(&sgpuData[3]);
 lx1 = GETLEs16(&sgpuData[8]);
 ly1 = GETLEs16(&sgpuData[9]);
 lx2 = GETLEs16(&sgpuData[14]);
 ly2 = GETLEs16(&sgpuData[15]);
 lx3 = GETLEs16(&sgpuData[20]);
 ly3 = GETLEs16(&sgpuData[21]);

 lLowerpart=GETLE32(&gpuData[5])>>16;
 UpdateGlobalTP((unsigned short)lLowerpart);

 if(!(dwActFixes&8))
  {
   AdjustCoord4();
   if(CheckCoord4()) return;
  }

 offsetPSX4();
 DrawSemiTrans = (SEMITRANSBIT(GETLE32(&gpuData[0]))) ? TRUE : FALSE;

 if(SHADETEXBIT(GETLE32(&gpuData[0])))
  {
   gpuData[0] = (gpuData[0]&HOST2LE32(0xff000000))|HOST2LE32(0x00808080);
   gpuData[3] = (gpuData[3]&HOST2LE32(0xff000000))|HOST2LE32(0x00808080);
   gpuData[6] = (gpuData[6]&HOST2LE32(0xff000000))|HOST2LE32(0x00808080);
   gpuData[9] = (gpuData[9]&HOST2LE32(0xff000000))|HOST2LE32(0x00808080);
  }

 drawPoly4GT(baseAddr);

 bDoVSyncUpdate=TRUE;
}

////////////////////////////////////////////////////////////////////////
// cmd: smooth shaded Poly3
////////////////////////////////////////////////////////////////////////

void primPolyF3(unsigned char *baseAddr)
{    
 uint32_t *gpuData = ((uint32_t *) baseAddr);
 short *sgpuData = ((short *) baseAddr);

 lx0 = GETLEs16(&sgpuData[2]);
 ly0 = GETLEs16(&sgpuData[3]);
 lx1 = GETLEs16(&sgpuData[4]);
 ly1 = GETLEs16(&sgpuData[5]);
 lx2 = GETLEs16(&sgpuData[6]);
 ly2 = GETLEs16(&sgpuData[7]);

 if(!(dwActFixes&8))
  {
   AdjustCoord3();
   if(CheckCoord3()) return;
  }

 offsetPSX3();
 SetRenderMode(GETLE32(&gpuData[0]));

 drawPoly3F(GETLE32(&gpuData[0]));

 bDoVSyncUpdate=TRUE;
}

////////////////////////////////////////////////////////////////////////
// cmd: skipping shaded polylines
////////////////////////////////////////////////////////////////////////

void primLineGSkip(unsigned char *baseAddr)
{    
 uint32_t *gpuData = ((uint32_t *) baseAddr);
 int iMax=255;
 int i=2;

 ly1 = (short)((GETLE32(&gpuData[1])>>16) & 0xffff);
 lx1 = (short)(GETLE32(&gpuData[1]) & 0xffff);

 while(!(((GETLE32(&gpuData[i]) & 0xF000F000) == 0x50005000) && i>=4))
  {
   i++;
   ly1 = (short)((GETLE32(&gpuData[i])>>16) & 0xffff);
   lx1 = (short)(GETLE32(&gpuData[i]) & 0xffff);
   i++;if(i>iMax) break;
  }
}

////////////////////////////////////////////////////////////////////////
// cmd: shaded polylines
////////////////////////////////////////////////////////////////////////

void primLineGEx(unsigned char *baseAddr)
{    
 uint32_t *gpuData = ((uint32_t *) baseAddr);
 int iMax=255;
 uint32_t lc0,lc1;
 short slx0,slx1,sly0,sly1;int i=2;BOOL bDraw=TRUE;

 sly1 = (short)((GETLE32(&gpuData[1])>>16) & 0xffff);
 slx1 = (short)(GETLE32(&gpuData[1]) & 0xffff);

 if(!(dwActFixes&8)) 
  {
   slx1=(short)(((int)slx1<<SIGNSHIFT)>>SIGNSHIFT);
   sly1=(short)(((int)sly1<<SIGNSHIFT)>>SIGNSHIFT);
  }

 lc1 = gpuData[0] & 0xffffff;

 DrawSemiTrans = (SEMITRANSBIT(GETLE32(&gpuData[0]))) ? TRUE : FALSE;

 while(!(((GETLE32(&gpuData[i]) & 0xF000F000) == 0x50005000) && i>=4))
  {
   sly0=sly1; slx0=slx1; lc0=lc1;
   lc1=GETLE32(&gpuData[i]) & 0xffffff;

   i++;

   // no check needed on gshaded polyline positions
   // if((gpuData[i] & 0xF000F000) == 0x50005000) break;

   sly1 = (short)((GETLE32(&gpuData[i])>>16) & 0xffff);
   slx1 = (short)(GETLE32(&gpuData[i]) & 0xffff);

   if(!(dwActFixes&8))
    {
     slx1=(short)(((int)slx1<<SIGNSHIFT)>>SIGNSHIFT);
     sly1=(short)(((int)sly1<<SIGNSHIFT)>>SIGNSHIFT);
     if(CheckCoordL(slx0,sly0,slx1,sly1)) bDraw=FALSE; else bDraw=TRUE;
    }

   if ((lx0 != lx1) || (ly0 != ly1))
    {
     ly0=sly0;
     lx0=slx0;
     ly1=sly1;
     lx1=slx1;
              
     offsetPSX2();
     if(bDraw) DrawSoftwareLineShade(lc0, lc1);
    }
   i++;  
   if(i>iMax) break;
  }

 bDoVSyncUpdate=TRUE;
}

////////////////////////////////////////////////////////////////////////
// cmd: shaded polyline2
////////////////////////////////////////////////////////////////////////

void primLineG2(unsigned char *baseAddr)
{    
 uint32_t *gpuData = ((uint32_t *) baseAddr);
 short *sgpuData = ((short *) baseAddr);

 lx0 = GETLEs16(&sgpuData[2]);
 ly0 = GETLEs16(&sgpuData[3]);
 lx1 = GETLEs16(&sgpuData[6]);
 ly1 = GETLEs16(&sgpuData[7]);

 if(!(dwActFixes&8))
  {
   AdjustCoord2();
   if(CheckCoord2()) return;
  }

 if((lx0 == lx1) && (ly0 == ly1)) {lx1++;ly1++;}

 DrawSemiTrans = (SEMITRANSBIT(GETLE32(&gpuData[0]))) ? TRUE : FALSE;
 offsetPSX2();
 DrawSoftwareLineShade(GETLE32(&gpuData[0]),GETLE32(&gpuData[2]));

 bDoVSyncUpdate=TRUE;
}

////////////////////////////////////////////////////////////////////////
// cmd: skipping flat polylines
////////////////////////////////////////////////////////////////////////

void primLineFSkip(unsigned char *baseAddr)
{
 uint32_t *gpuData = ((uint32_t *) baseAddr);
 int i=2,iMax=255;

 ly1 = (short)((GETLE32(&gpuData[1])>>16) & 0xffff);
 lx1 = (short)(GETLE32(&gpuData[1]) & 0xffff);

 while(!(((GETLE32(&gpuData[i]) & 0xF000F000) == 0x50005000) && i>=3))
  {
   ly1 = (short)((GETLE32(&gpuData[i])>>16) & 0xffff);
   lx1 = (short)(GETLE32(&gpuData[i]) & 0xffff);
   i++;if(i>iMax) break;
  }             
}

////////////////////////////////////////////////////////////////////////
// cmd: drawing flat polylines
////////////////////////////////////////////////////////////////////////

void primLineFEx(unsigned char *baseAddr)
{
 uint32_t *gpuData = ((uint32_t *) baseAddr);
 int iMax;
 short slx0,slx1,sly0,sly1;int i=2;BOOL bDraw=TRUE;

 iMax=255;

 sly1 = (short)((GETLE32(&gpuData[1])>>16) & 0xffff);
 slx1 = (short)(GETLE32(&gpuData[1]) & 0xffff);
 if(!(dwActFixes&8))
  {
   slx1=(short)(((int)slx1<<SIGNSHIFT)>>SIGNSHIFT);
   sly1=(short)(((int)sly1<<SIGNSHIFT)>>SIGNSHIFT);
  }

 SetRenderMode(GETLE32(&gpuData[0]));

 while(!(((GETLE32(&gpuData[i]) & 0xF000F000) == 0x50005000) && i>=3))
  {
   sly0 = sly1;slx0=slx1;
   sly1 = (short)((GETLE32(&gpuData[i])>>16) & 0xffff);
   slx1 = (short)(GETLE32(&gpuData[i]) & 0xffff);
   if(!(dwActFixes&8))
    {
     slx1=(short)(((int)slx1<<SIGNSHIFT)>>SIGNSHIFT);
     sly1=(short)(((int)sly1<<SIGNSHIFT)>>SIGNSHIFT);

     if(CheckCoordL(slx0,sly0,slx1,sly1)) bDraw=FALSE; else bDraw=TRUE;
    }

   ly0=sly0;
   lx0=slx0;
   ly1=sly1;
   lx1=slx1;

   offsetPSX2();
   if(bDraw) DrawSoftwareLineFlat(GETLE32(&gpuData[0]));

   i++;if(i>iMax) break;
  }

 bDoVSyncUpdate=TRUE;
}

////////////////////////////////////////////////////////////////////////
// cmd: drawing flat polyline2
////////////////////////////////////////////////////////////////////////

void primLineF2(unsigned char *baseAddr)
{
 uint32_t *gpuData = ((uint32_t *) baseAddr);
 short *sgpuData = ((short *) baseAddr);

 lx0 = GETLEs16(&sgpuData[2]);
 ly0 = GETLEs16(&sgpuData[3]);
 lx1 = GETLEs16(&sgpuData[4]);
 ly1 = GETLEs16(&sgpuData[5]);

 if(!(dwActFixes&8))
  {
   AdjustCoord2();
   if(CheckCoord2()) return;
  }

 if((lx0 == lx1) && (ly0 == ly1)) {lx1++;ly1++;}
                    
 offsetPSX2();
 SetRenderMode(GETLE32(&gpuData[0]));

 DrawSoftwareLineFlat(GETLE32(&gpuData[0]));

 bDoVSyncUpdate=TRUE;
}

////////////////////////////////////////////////////////////////////////
// cmd: well, easiest command... not implemented
////////////////////////////////////////////////////////////////////////

void primNI(unsigned char *bA)
{
}

////////////////////////////////////////////////////////////////////////
// cmd func ptr table
////////////////////////////////////////////////////////////////////////


void (*primTableJ[256])(unsigned char *) = 
{
    // 00
    primNI,primNI,primBlkFill,primNI,primNI,primNI,primNI,primNI,
    // 08
    primNI,primNI,primNI,primNI,primNI,primNI,primNI,primNI,
    // 10
    primNI,primNI,primNI,primNI,primNI,primNI,primNI,primNI,
    // 18
    primNI,primNI,primNI,primNI,primNI,primNI,primNI,primNI,
    // 20
    primPolyF3,primPolyF3,primPolyF3,primPolyF3,primPolyFT3,primPolyFT3,primPolyFT3,primPolyFT3,
    // 28
    primPolyF4,primPolyF4,primPolyF4,primPolyF4,primPolyFT4,primPolyFT4,primPolyFT4,primPolyFT4,
    // 30
    primPolyG3,primPolyG3,primPolyG3,primPolyG3,primPolyGT3,primPolyGT3,primPolyGT3,primPolyGT3,
    // 38
    primPolyG4,primPolyG4,primPolyG4,primPolyG4,primPolyGT4,primPolyGT4,primPolyGT4,primPolyGT4,
    // 40
    primLineF2,primLineF2,primLineF2,primLineF2,primNI,primNI,primNI,primNI,
    // 48
    primLineFEx,primLineFEx,primLineFEx,primLineFEx,primLineFEx,primLineFEx,primLineFEx,primLineFEx,
    // 50
    primLineG2,primLineG2,primLineG2,primLineG2,primNI,primNI,primNI,primNI,
    // 58
    primLineGEx,primLineGEx,primLineGEx,primLineGEx,primLineGEx,primLineGEx,primLineGEx,primLineGEx,
    // 60
    primTileS,primTileS,primTileS,primTileS,primSprtS,primSprtS,primSprtS,primSprtS,
    // 68
    primTile1,primTile1,primTile1,primTile1,primNI,primNI,primNI,primNI,
    // 70
    primTile8,primTile8,primTile8,primTile8,primSprt8,primSprt8,primSprt8,primSprt8,
    // 78
    primTile16,primTile16,primTile16,primTile16,primSprt16,primSprt16,primSprt16,primSprt16,
    // 80
    primMoveImage,primNI,primNI,primNI,primNI,primNI,primNI,primNI,
    // 88
    primNI,primNI,primNI,primNI,primNI,primNI,primNI,primNI,
    // 90
    primNI,primNI,primNI,primNI,primNI,primNI,primNI,primNI,
    // 98
    primNI,primNI,primNI,primNI,primNI,primNI,primNI,primNI,
    // a0
    primLoadImage,primNI,primNI,primNI,primNI,primNI,primNI,primNI,
    // a8
    primNI,primNI,primNI,primNI,primNI,primNI,primNI,primNI,
    // b0
    primNI,primNI,primNI,primNI,primNI,primNI,primNI,primNI,
    // b8
    primNI,primNI,primNI,primNI,primNI,primNI,primNI,primNI,
    // c0
    primStoreImage,primNI,primNI,primNI,primNI,primNI,primNI,primNI,
    // c8
    primNI,primNI,primNI,primNI,primNI,primNI,primNI,primNI,
    // d0
    primNI,primNI,primNI,primNI,primNI,primNI,primNI,primNI,
    // d8
    primNI,primNI,primNI,primNI,primNI,primNI,primNI,primNI,
    // e0
    primNI,cmdTexturePage,cmdTextureWindow,cmdDrawAreaStart,cmdDrawAreaEnd,cmdDrawOffset,cmdSTP,primNI,
    // e8
    primNI,primNI,primNI,primNI,primNI,primNI,primNI,primNI,
    // f0
    primNI,primNI,primNI,primNI,primNI,primNI,primNI,primNI,
    // f8
    primNI,primNI,primNI,primNI,primNI,primNI,primNI,primNI
};

////////////////////////////////////////////////////////////////////////
// cmd func ptr table for skipping
////////////////////////////////////////////////////////////////////////

void (*primTableSkip[256])(unsigned char *) = 
{
    // 00
    primNI,primNI,primBlkFill,primNI,primNI,primNI,primNI,primNI,
    // 08
    primNI,primNI,primNI,primNI,primNI,primNI,primNI,primNI,
    // 10
    primNI,primNI,primNI,primNI,primNI,primNI,primNI,primNI,
    // 18
    primNI,primNI,primNI,primNI,primNI,primNI,primNI,primNI,
    // 20
    primNI,primNI,primNI,primNI,primNI,primNI,primNI,primNI,
    // 28
    primNI,primNI,primNI,primNI,primNI,primNI,primNI,primNI,
    // 30
    primNI,primNI,primNI,primNI,primNI,primNI,primNI,primNI,
    // 38
    primNI,primNI,primNI,primNI,primNI,primNI,primNI,primNI,
    // 40
    primNI,primNI,primNI,primNI,primNI,primNI,primNI,primNI,
    // 48
    primLineFSkip,primLineFSkip,primLineFSkip,primLineFSkip,primLineFSkip,primLineFSkip,primLineFSkip,primLineFSkip,
    // 50
    primNI,primNI,primNI,primNI,primNI,primNI,primNI,primNI,
    // 58
    primLineGSkip,primLineGSkip,primLineGSkip,primLineGSkip,primLineGSkip,primLineGSkip,primLineGSkip,primLineGSkip,
    // 60
    primNI,primNI,primNI,primNI,primNI,primNI,primNI,primNI,
    // 68
    primNI,primNI,primNI,primNI,primNI,primNI,primNI,primNI,
    // 70
    primNI,primNI,primNI,primNI,primNI,primNI,primNI,primNI,
    // 78
    primNI,primNI,primNI,primNI,primNI,primNI,primNI,primNI,
    // 80
    primMoveImage,primNI,primNI,primNI,primNI,primNI,primNI,primNI,
    // 88
    primNI,primNI,primNI,primNI,primNI,primNI,primNI,primNI,
    // 90
    primNI,primNI,primNI,primNI,primNI,primNI,primNI,primNI,
    // 98
    primNI,primNI,primNI,primNI,primNI,primNI,primNI,primNI,
    // a0
    primLoadImage,primNI,primNI,primNI,primNI,primNI,primNI,primNI,
    // a8
    primNI,primNI,primNI,primNI,primNI,primNI,primNI,primNI,
    // b0
    primNI,primNI,primNI,primNI,primNI,primNI,primNI,primNI,
    // b8
    primNI,primNI,primNI,primNI,primNI,primNI,primNI,primNI,
    // c0
    primStoreImage,primNI,primNI,primNI,primNI,primNI,primNI,primNI,
    // c8
    primNI,primNI,primNI,primNI,primNI,primNI,primNI,primNI,
    // d0
    primNI,primNI,primNI,primNI,primNI,primNI,primNI,primNI,
    // d8
    primNI,primNI,primNI,primNI,primNI,primNI,primNI,primNI,
    // e0
    primNI,cmdTexturePage,cmdTextureWindow,cmdDrawAreaStart,cmdDrawAreaEnd,cmdDrawOffset,cmdSTP,primNI,
    // e8
    primNI,primNI,primNI,primNI,primNI,primNI,primNI,primNI,
    // f0
    primNI,primNI,primNI,primNI,primNI,primNI,primNI,primNI,
    // f8
    primNI,primNI,primNI,primNI,primNI,primNI,primNI,primNI
};


////////////////////////////////////////////////////////////////////////
// soft.c
////////////////////////////////////////////////////////////////////////


// switches for painting textured quads as 2 triangles (small glitches, but better shading!)
// can be toggled by game fix 0x200 in version 1.17 anyway, so let the defines enabled!

#define POLYQUAD3                 
#define POLYQUAD3GT                 

// fast solid loops... a bit more additional code, of course

#define FASTSOLID

// psx blending mode 3 with 25% incoming color (instead 50% without the define)

#define HALFBRIGHTMODE3

// color decode defines

#define XCOL1(x)     (x & 0x1f)
#define XCOL2(x)     (x & 0x3e0)
#define XCOL3(x)     (x & 0x7c00)

#define XCOL1D(x)     (x & 0x1f)
#define XCOL2D(x)     ((x>>5) & 0x1f)
#define XCOL3D(x)     ((x>>10) & 0x1f)

#define X32TCOL1(x)  ((x & 0x001f001f)<<7)
#define X32TCOL2(x)  ((x & 0x03e003e0)<<2)
#define X32TCOL3(x)  ((x & 0x7c007c00)>>3)

#define X32COL1(x)   (x & 0x001f001f)
#define X32COL2(x)   ((x>>5) & 0x001f001f)
#define X32COL3(x)   ((x>>10) & 0x001f001f)

#define X32ACOL1(x)  (x & 0x001e001e)
#define X32ACOL2(x)  ((x>>5) & 0x001e001e)
#define X32ACOL3(x)  ((x>>10) & 0x001e001e)

#define X32BCOL1(x)  (x & 0x001c001c)
#define X32BCOL2(x)  ((x>>5) & 0x001c001c)
#define X32BCOL3(x)  ((x>>10) & 0x001c001c)

#define X32PSXCOL(r,g,b) ((g<<10)|(b<<5)|r)

#define XPSXCOL(r,g,b) ((g&0x7c00)|(b&0x3e0)|(r&0x1f))


////////////////////////////////////////////////////////////////////////
// POLYGON OFFSET FUNCS
////////////////////////////////////////////////////////////////////////

void offsetPSXLine(void)
{
 short x0,x1,y0,y1,dx,dy;float px,py;

 x0 = lx0+1+PSXDisplay.DrawOffset.x;
 x1 = lx1+1+PSXDisplay.DrawOffset.x;
 y0 = ly0+1+PSXDisplay.DrawOffset.y;
 y1 = ly1+1+PSXDisplay.DrawOffset.y;

 dx=x1-x0;
 dy=y1-y0;

 // tricky line width without sqrt

 if(dx>=0)
  {
   if(dy>=0)
    {
     px=0.5f;
          if(dx>dy) py=-0.5f;
     else if(dx<dy) py= 0.5f;
     else           py= 0.0f;
    }
   else
    {
     py=-0.5f;
     dy=-dy;
          if(dx>dy) px= 0.5f;
     else if(dx<dy) px=-0.5f;
     else           px= 0.0f;
    }
  }
 else
  {
   if(dy>=0)
    {
     py=0.5f;
     dx=-dx;
          if(dx>dy) px=-0.5f;
     else if(dx<dy) px= 0.5f;
     else           px= 0.0f;
    }
   else
    {
     px=-0.5f;
          if(dx>dy) py=-0.5f;
     else if(dx<dy) py= 0.5f;
     else           py= 0.0f;
    }
  } 
 
 lx0=(short)((float)x0-px);
 lx3=(short)((float)x0+py);
 
 ly0=(short)((float)y0-py);
 ly3=(short)((float)y0-px);
 
 lx1=(short)((float)x1-py);
 lx2=(short)((float)x1+px);
 
 ly1=(short)((float)y1+px);
 ly2=(short)((float)y1+py);
}

void offsetPSX2(void)
{
 lx0 += PSXDisplay.DrawOffset.x;
 ly0 += PSXDisplay.DrawOffset.y;
 lx1 += PSXDisplay.DrawOffset.x;
 ly1 += PSXDisplay.DrawOffset.y;
}

void offsetPSX3(void)
{
 lx0 += PSXDisplay.DrawOffset.x;
 ly0 += PSXDisplay.DrawOffset.y;
 lx1 += PSXDisplay.DrawOffset.x;
 ly1 += PSXDisplay.DrawOffset.y;
 lx2 += PSXDisplay.DrawOffset.x;
 ly2 += PSXDisplay.DrawOffset.y;
}

void offsetPSX4(void)
{
 lx0 += PSXDisplay.DrawOffset.x;
 ly0 += PSXDisplay.DrawOffset.y;
 lx1 += PSXDisplay.DrawOffset.x;
 ly1 += PSXDisplay.DrawOffset.y;
 lx2 += PSXDisplay.DrawOffset.x;
 ly2 += PSXDisplay.DrawOffset.y;
 lx3 += PSXDisplay.DrawOffset.x;
 ly3 += PSXDisplay.DrawOffset.y;
}

/////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////
// PER PIXEL FUNCS
////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////


unsigned char dithertable[16] =
{
    7, 0, 6, 1,
    2, 5, 3, 4,
    1, 6, 0, 7,
    4, 3, 5, 2
};

void Dither16(unsigned short * pdest,uint32_t r,uint32_t g,uint32_t b,unsigned short sM)
{
 unsigned char coeff;
 unsigned char rlow, glow, blow;
 int x,y;
                 
 x=pdest-psxVuw;
 y=x>>10;
 x-=(y<<10);

 coeff = dithertable[(y&3)*4+(x&3)];

 rlow = r&7; glow = g&7; blow = b&7;

 r>>=3; g>>=3; b>>=3;

 if ((r < 0x1F) && rlow > coeff) r++;
 if ((g < 0x1F) && glow > coeff) g++;
 if ((b < 0x1F) && blow > coeff) b++;

 PUTLE16(pdest, ((unsigned short)b<<10) |
        ((unsigned short)g<<5) |
        (unsigned short)r | sM);
}

/////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////

__inline void GetShadeTransCol_Dither(unsigned short * pdest, int32_t m1, int32_t m2, int32_t m3)
{
 int32_t r,g,b;

 if(bCheckMask && (*pdest & HOST2LE16(0x8000))) return;

 if(DrawSemiTrans)
  {
   r=((XCOL1D(GETLE16(pdest)))<<3);
   b=((XCOL2D(GETLE16(pdest)))<<3);
   g=((XCOL3D(GETLE16(pdest)))<<3);

   if(GlobalTextABR==0)
    {
     r=(r>>1)+(m1>>1);
     b=(b>>1)+(m2>>1);
     g=(g>>1)+(m3>>1);
    }
   else
   if(GlobalTextABR==1)
    {
     r+=m1;
     b+=m2;
     g+=m3;
    }
   else
   if(GlobalTextABR==2)
    {
     r-=m1;
     b-=m2;
     g-=m3;
     if(r&0x80000000) r=0;
     if(b&0x80000000) b=0;
     if(g&0x80000000) g=0;
    }
   else
    {
#ifdef HALFBRIGHTMODE3
     r+=(m1>>2);
     b+=(m2>>2);
     g+=(m3>>2);
#else
     r+=(m1>>1);
     b+=(m2>>1);
     g+=(m3>>1);
#endif
    }
  }
 else 
  {
   r=m1;
   b=m2;
   g=m3;
  }

 if(r&0x7FFFFF00) r=0xff;
 if(b&0x7FFFFF00) b=0xff;
 if(g&0x7FFFFF00) g=0xff;

 Dither16(pdest,r,b,g,sSetMask);
}

////////////////////////////////////////////////////////////////////////

__inline void GetShadeTransCol(unsigned short * pdest,unsigned short color)
{
 if(bCheckMask && (*pdest & HOST2LE16(0x8000))) return;

 if(DrawSemiTrans)
  {
   int32_t r,g,b;
 
   if(GlobalTextABR==0)
    {
     PUTLE16(pdest, (((GETLE16(pdest)&0x7bde)>>1)+(((color)&0x7bde)>>1))|sSetMask);//0x8000;
     return;
/*
     r=(XCOL1(*pdest)>>1)+((XCOL1(color))>>1);
     b=(XCOL2(*pdest)>>1)+((XCOL2(color))>>1);
     g=(XCOL3(*pdest)>>1)+((XCOL3(color))>>1);
*/
    }
   else
   if(GlobalTextABR==1)
    {
     r=(XCOL1(GETLE16(pdest)))+((XCOL1(color)));
     b=(XCOL2(GETLE16(pdest)))+((XCOL2(color)));
     g=(XCOL3(GETLE16(pdest)))+((XCOL3(color)));
    }
   else
   if(GlobalTextABR==2)
    {
     r=(XCOL1(GETLE16(pdest)))-((XCOL1(color)));
     b=(XCOL2(GETLE16(pdest)))-((XCOL2(color)));
     g=(XCOL3(GETLE16(pdest)))-((XCOL3(color)));
     if(r&0x80000000) r=0;
     if(b&0x80000000) b=0;
     if(g&0x80000000) g=0;
   }
   else
    {
#ifdef HALFBRIGHTMODE3
     r=(XCOL1(GETLE16(pdest)))+((XCOL1(color))>>2);
     b=(XCOL2(GETLE16(pdest)))+((XCOL2(color))>>2);
     g=(XCOL3(GETLE16(pdest)))+((XCOL3(color))>>2);
#else
     r=(XCOL1(GETLE16(pdest)))+((XCOL1(color))>>1);
     b=(XCOL2(GETLE16(pdest)))+((XCOL2(color))>>1);
     g=(XCOL3(GETLE16(pdest)))+((XCOL3(color))>>1);
#endif
    }

   if(r&0x7FFFFFE0) r=0x1f;
   if(b&0x7FFFFC00) b=0x3e0;
   if(g&0x7FFF8000) g=0x7c00;

   PUTLE16(pdest, (XPSXCOL(r,g,b))|sSetMask);//0x8000;
  }
 else PUTLE16(pdest, color|sSetMask);
}  

////////////////////////////////////////////////////////////////////////

__inline void GetShadeTransCol32(uint32_t * pdest,uint32_t color)
{
 if(DrawSemiTrans)
  {
   int32_t r,g,b;
 
   if(GlobalTextABR==0)
    {
     if(!bCheckMask)
      {
       PUTLE32(pdest, (((GETLE32(pdest)&0x7bde7bde)>>1)+(((color)&0x7bde7bde)>>1))|lSetMask);//0x80008000;
       return;
      }
     r=(X32ACOL1(GETLE32(pdest))>>1)+((X32ACOL1(color))>>1);
     b=(X32ACOL2(GETLE32(pdest))>>1)+((X32ACOL2(color))>>1);
     g=(X32ACOL3(GETLE32(pdest))>>1)+((X32ACOL3(color))>>1);
    }
   else
   if(GlobalTextABR==1)
    {
     r=(X32COL1(GETLE32(pdest)))+((X32COL1(color)));
     b=(X32COL2(GETLE32(pdest)))+((X32COL2(color)));
     g=(X32COL3(GETLE32(pdest)))+((X32COL3(color)));
    }
   else
   if(GlobalTextABR==2)
    {
     int32_t sr,sb,sg,src,sbc,sgc,c;
     src=XCOL1(color);sbc=XCOL2(color);sgc=XCOL3(color);
     c=GETLE32(pdest)>>16;
     sr=(XCOL1(c))-src;   if(sr&0x8000) sr=0;
     sb=(XCOL2(c))-sbc;  if(sb&0x8000) sb=0;
     sg=(XCOL3(c))-sgc; if(sg&0x8000) sg=0;
     r=((int32_t)sr)<<16;b=((int32_t)sb)<<11;g=((int32_t)sg)<<6;
     c=LOWORD(GETLE32(pdest));
     sr=(XCOL1(c))-src;   if(sr&0x8000) sr=0;
     sb=(XCOL2(c))-sbc;  if(sb&0x8000) sb=0;
     sg=(XCOL3(c))-sgc; if(sg&0x8000) sg=0;
     r|=sr;b|=sb>>5;g|=sg>>10;
    }
   else
    {
#ifdef HALFBRIGHTMODE3
     r=(X32COL1(GETLE32(pdest)))+((X32BCOL1(color))>>2);
     b=(X32COL2(GETLE32(pdest)))+((X32BCOL2(color))>>2);
     g=(X32COL3(GETLE32(pdest)))+((X32BCOL3(color))>>2);
#else
     r=(X32COL1(GETLE32(pdest)))+((X32ACOL1(color))>>1);
     b=(X32COL2(GETLE32(pdest)))+((X32ACOL2(color))>>1);
     g=(X32COL3(GETLE32(pdest)))+((X32ACOL3(color))>>1);
#endif
    }

   if(r&0x7FE00000) r=0x1f0000|(r&0xFFFF);
   if(r&0x7FE0)     r=0x1f    |(r&0xFFFF0000);
   if(b&0x7FE00000) b=0x1f0000|(b&0xFFFF);
   if(b&0x7FE0)     b=0x1f    |(b&0xFFFF0000);
   if(g&0x7FE00000) g=0x1f0000|(g&0xFFFF);
   if(g&0x7FE0)     g=0x1f    |(g&0xFFFF0000);

   if(bCheckMask) 
    {
     uint32_t ma=GETLE32(pdest);
     PUTLE32(pdest, (X32PSXCOL(r,g,b))|lSetMask);//0x80008000;
     if(ma&0x80000000) PUTLE32(pdest, (ma&0xFFFF0000)|(*pdest&0xFFFF));
     if(ma&0x00008000) PUTLE32(pdest, (ma&0xFFFF)    |(*pdest&0xFFFF0000));
     return;
    }
   PUTLE32(pdest, (X32PSXCOL(r,g,b))|lSetMask);//0x80008000;
  }
 else 
  {
   if(bCheckMask) 
    {
     uint32_t ma=GETLE32(pdest);
     PUTLE32(pdest, color|lSetMask);//0x80008000;
     if(ma&0x80000000) PUTLE32(pdest, (ma&0xFFFF0000)|(GETLE32(pdest)&0xFFFF));
     if(ma&0x00008000) PUTLE32(pdest, (ma&0xFFFF)    |(GETLE32(pdest)&0xFFFF0000));
     return;
    }

   PUTLE32(pdest, color|lSetMask);//0x80008000;
  }
}  

////////////////////////////////////////////////////////////////////////

__inline void GetTextureTransColG(unsigned short * pdest,unsigned short color)
{
 int32_t r,g,b;unsigned short l;

 if(color==0) return;

 if(bCheckMask && (*pdest & HOST2LE16(0x8000))) return;

 l=sSetMask|(color&0x8000);

 if(DrawSemiTrans && (color&0x8000))
  {
   if(GlobalTextABR==0)
    {
     unsigned short d;
     d     =(GETLE16(pdest)&0x7bde)>>1;
     color =((color) &0x7bde)>>1;
     r=(XCOL1(d))+((((XCOL1(color)))* g_m1)>>7);
     b=(XCOL2(d))+((((XCOL2(color)))* g_m2)>>7);
     g=(XCOL3(d))+((((XCOL3(color)))* g_m3)>>7);

/*
     r=(XCOL1(*pdest)>>1)+((((XCOL1(color))>>1)* g_m1)>>7);
     b=(XCOL2(*pdest)>>1)+((((XCOL2(color))>>1)* g_m2)>>7);
     g=(XCOL3(*pdest)>>1)+((((XCOL3(color))>>1)* g_m3)>>7);
*/
    }
   else
   if(GlobalTextABR==1)
    {
     r=(XCOL1(GETLE16(pdest)))+((((XCOL1(color)))* g_m1)>>7);
     b=(XCOL2(GETLE16(pdest)))+((((XCOL2(color)))* g_m2)>>7);
     g=(XCOL3(GETLE16(pdest)))+((((XCOL3(color)))* g_m3)>>7);
    }
   else
   if(GlobalTextABR==2)
    {
     r=(XCOL1(GETLE16(pdest)))-((((XCOL1(color)))* g_m1)>>7);
     b=(XCOL2(GETLE16(pdest)))-((((XCOL2(color)))* g_m2)>>7);
     g=(XCOL3(GETLE16(pdest)))-((((XCOL3(color)))* g_m3)>>7);
     if(r&0x80000000) r=0;
     if(b&0x80000000) b=0;
     if(g&0x80000000) g=0;
    }
   else
    {
#ifdef HALFBRIGHTMODE3
     r=(XCOL1(GETLE16(pdest)))+((((XCOL1(color))>>2)* g_m1)>>7);
     b=(XCOL2(GETLE16(pdest)))+((((XCOL2(color))>>2)* g_m2)>>7);
     g=(XCOL3(GETLE16(pdest)))+((((XCOL3(color))>>2)* g_m3)>>7);
#else
     r=(XCOL1(GETLE16(pdest)))+((((XCOL1(color))>>1)* g_m1)>>7);
     b=(XCOL2(GETLE16(pdest)))+((((XCOL2(color))>>1)* g_m2)>>7);
     g=(XCOL3(GETLE16(pdest)))+((((XCOL3(color))>>1)* g_m3)>>7);
#endif
    }
  }
 else 
  {
   r=((XCOL1(color))* g_m1)>>7;
   b=((XCOL2(color))* g_m2)>>7;
   g=((XCOL3(color))* g_m3)>>7;
  }

 if(r&0x7FFFFFE0) r=0x1f;
 if(b&0x7FFFFC00) b=0x3e0;
 if(g&0x7FFF8000) g=0x7c00;

 PUTLE16(pdest, (XPSXCOL(r,g,b))|l);
}

////////////////////////////////////////////////////////////////////////

__inline void GetTextureTransColG_S(unsigned short * pdest,unsigned short color)
{
 int32_t r,g,b;unsigned short l;

 if(color==0) return;

 l=sSetMask|(color&0x8000);

 r=((XCOL1(color))* g_m1)>>7;
 b=((XCOL2(color))* g_m2)>>7;
 g=((XCOL3(color))* g_m3)>>7;

 if(r&0x7FFFFFE0) r=0x1f;
 if(b&0x7FFFFC00) b=0x3e0;
 if(g&0x7FFF8000) g=0x7c00;

 PUTLE16(pdest, (XPSXCOL(r,g,b))|l);
}

////////////////////////////////////////////////////////////////////////

__inline void GetTextureTransColG_SPR(unsigned short * pdest,unsigned short color)
{
 int32_t r,g,b;unsigned short l;

 if(color==0) return;

 if(bCheckMask && (GETLE16(pdest) & 0x8000)) return;

 l=sSetMask|(color&0x8000);

 if(DrawSemiTrans && (color&0x8000))
  {
   if(GlobalTextABR==0)
    {
     unsigned short d;
     d     =(GETLE16(pdest)&0x7bde)>>1;
     color =((color) &0x7bde)>>1;
     r=(XCOL1(d))+((((XCOL1(color)))* g_m1)>>7);
     b=(XCOL2(d))+((((XCOL2(color)))* g_m2)>>7);
     g=(XCOL3(d))+((((XCOL3(color)))* g_m3)>>7);

/*
     r=(XCOL1(*pdest)>>1)+((((XCOL1(color))>>1)* g_m1)>>7);
     b=(XCOL2(*pdest)>>1)+((((XCOL2(color))>>1)* g_m2)>>7);
     g=(XCOL3(*pdest)>>1)+((((XCOL3(color))>>1)* g_m3)>>7);
*/
    }
   else
   if(GlobalTextABR==1)
    {
     r=(XCOL1(GETLE16(pdest)))+((((XCOL1(color)))* g_m1)>>7);
     b=(XCOL2(GETLE16(pdest)))+((((XCOL2(color)))* g_m2)>>7);
     g=(XCOL3(GETLE16(pdest)))+((((XCOL3(color)))* g_m3)>>7);
    }
   else
   if(GlobalTextABR==2)
    {
     r=(XCOL1(GETLE16(pdest)))-((((XCOL1(color)))* g_m1)>>7);
     b=(XCOL2(GETLE16(pdest)))-((((XCOL2(color)))* g_m2)>>7);
     g=(XCOL3(GETLE16(pdest)))-((((XCOL3(color)))* g_m3)>>7);
     if(r&0x80000000) r=0;
     if(b&0x80000000) b=0;
     if(g&0x80000000) g=0;
    }
   else
    {
#ifdef HALFBRIGHTMODE3
     r=(XCOL1(GETLE16(pdest)))+((((XCOL1(color))>>2)* g_m1)>>7);
     b=(XCOL2(GETLE16(pdest)))+((((XCOL2(color))>>2)* g_m2)>>7);
     g=(XCOL3(GETLE16(pdest)))+((((XCOL3(color))>>2)* g_m3)>>7);
#else
     r=(XCOL1(GETLE16(pdest)))+((((XCOL1(color))>>1)* g_m1)>>7);
     b=(XCOL2(GETLE16(pdest)))+((((XCOL2(color))>>1)* g_m2)>>7);
     g=(XCOL3(GETLE16(pdest)))+((((XCOL3(color))>>1)* g_m3)>>7);
#endif
    }
  }
 else 
  {
   r=((XCOL1(color))* g_m1)>>7;
   b=((XCOL2(color))* g_m2)>>7;
   g=((XCOL3(color))* g_m3)>>7;
  }

 if(r&0x7FFFFFE0) r=0x1f;
 if(b&0x7FFFFC00) b=0x3e0;
 if(g&0x7FFF8000) g=0x7c00;

 PUTLE16(pdest, (XPSXCOL(r,g,b))|l);
}

////////////////////////////////////////////////////////////////////////

__inline void GetTextureTransColG32(uint32_t * pdest,uint32_t color)
{
 int32_t r,g,b,l;

 if(color==0) return;

 l=lSetMask|(color&0x80008000);

 if(DrawSemiTrans && (color&0x80008000))
  {
   if(GlobalTextABR==0)
    {                 
     r=((((X32TCOL1(GETLE32(pdest)))+((X32COL1(color)) * g_m1))&0xFF00FF00)>>8);
     b=((((X32TCOL2(GETLE32(pdest)))+((X32COL2(color)) * g_m2))&0xFF00FF00)>>8);
     g=((((X32TCOL3(GETLE32(pdest)))+((X32COL3(color)) * g_m3))&0xFF00FF00)>>8);
    }
   else
   if(GlobalTextABR==1)
    {
     r=(X32COL1(GETLE32(pdest)))+(((((X32COL1(color)))* g_m1)&0xFF80FF80)>>7);
     b=(X32COL2(GETLE32(pdest)))+(((((X32COL2(color)))* g_m2)&0xFF80FF80)>>7);
     g=(X32COL3(GETLE32(pdest)))+(((((X32COL3(color)))* g_m3)&0xFF80FF80)>>7);
    }
   else
   if(GlobalTextABR==2)
    {
     int32_t t;
     r=(((((X32COL1(color)))* g_m1)&0xFF80FF80)>>7);
     t=(GETLE32(pdest)&0x001f0000)-(r&0x003f0000); if(t&0x80000000) t=0;
     r=(GETLE32(pdest)&0x0000001f)-(r&0x0000003f); if(r&0x80000000) r=0;
     r|=t;

     b=(((((X32COL2(color)))* g_m2)&0xFF80FF80)>>7);
     t=((GETLE32(pdest)>>5)&0x001f0000)-(b&0x003f0000); if(t&0x80000000) t=0;
     b=((GETLE32(pdest)>>5)&0x0000001f)-(b&0x0000003f); if(b&0x80000000) b=0;
     b|=t;

     g=(((((X32COL3(color)))* g_m3)&0xFF80FF80)>>7);
     t=((GETLE32(pdest)>>10)&0x001f0000)-(g&0x003f0000); if(t&0x80000000) t=0;
     g=((GETLE32(pdest)>>10)&0x0000001f)-(g&0x0000003f); if(g&0x80000000) g=0;
     g|=t;
    }
   else
    {
#ifdef HALFBRIGHTMODE3
     r=(X32COL1(GETLE32(pdest)))+(((((X32BCOL1(color))>>2)* g_m1)&0xFF80FF80)>>7);
     b=(X32COL2(GETLE32(pdest)))+(((((X32BCOL2(color))>>2)* g_m2)&0xFF80FF80)>>7);
     g=(X32COL3(GETLE32(pdest)))+(((((X32BCOL3(color))>>2)* g_m3)&0xFF80FF80)>>7);
#else
     r=(X32COL1(GETLE32(pdest)))+(((((X32ACOL1(color))>>1)* g_m1)&0xFF80FF80)>>7);
     b=(X32COL2(GETLE32(pdest)))+(((((X32ACOL2(color))>>1)* g_m2)&0xFF80FF80)>>7);
     g=(X32COL3(GETLE32(pdest)))+(((((X32ACOL3(color))>>1)* g_m3)&0xFF80FF80)>>7);
#endif
    }

   if(!(color&0x8000))
    {
     r=(r&0xffff0000)|((((X32COL1(color))* g_m1)&0x0000FF80)>>7);
     b=(b&0xffff0000)|((((X32COL2(color))* g_m2)&0x0000FF80)>>7);
     g=(g&0xffff0000)|((((X32COL3(color))* g_m3)&0x0000FF80)>>7);
    }
   if(!(color&0x80000000))
    {
     r=(r&0xffff)|((((X32COL1(color))* g_m1)&0xFF800000)>>7);
     b=(b&0xffff)|((((X32COL2(color))* g_m2)&0xFF800000)>>7);
     g=(g&0xffff)|((((X32COL3(color))* g_m3)&0xFF800000)>>7);
    }

  }
 else 
  {
   r=(((X32COL1(color))* g_m1)&0xFF80FF80)>>7;
   b=(((X32COL2(color))* g_m2)&0xFF80FF80)>>7;
   g=(((X32COL3(color))* g_m3)&0xFF80FF80)>>7;
  }

 if(r&0x7FE00000) r=0x1f0000|(r&0xFFFF);
 if(r&0x7FE0)     r=0x1f    |(r&0xFFFF0000);
 if(b&0x7FE00000) b=0x1f0000|(b&0xFFFF);
 if(b&0x7FE0)     b=0x1f    |(b&0xFFFF0000);
 if(g&0x7FE00000) g=0x1f0000|(g&0xFFFF);
 if(g&0x7FE0)     g=0x1f    |(g&0xFFFF0000);
         
 if(bCheckMask) 
  {
   uint32_t ma=GETLE32(pdest);

   PUTLE32(pdest, (X32PSXCOL(r,g,b))|l);
   
   if((color&0xffff)==0    ) PUTLE32(pdest, (ma&0xffff)|(GETLE32(pdest)&0xffff0000));
   if((color&0xffff0000)==0) PUTLE32(pdest, (ma&0xffff0000)|(GETLE32(pdest)&0xffff));
   if(ma&0x80000000) PUTLE32(pdest, (ma&0xFFFF0000)|(GETLE32(pdest)&0xFFFF));
   if(ma&0x00008000) PUTLE32(pdest, (ma&0xFFFF)    |(GETLE32(pdest)&0xFFFF0000));

   return;                            
  }
 if((color&0xffff)==0    ) {PUTLE32(pdest, (GETLE32(pdest)&0xffff)|(((X32PSXCOL(r,g,b))|l)&0xffff0000));return;}
 if((color&0xffff0000)==0) {PUTLE32(pdest, (GETLE32(pdest)&0xffff0000)|(((X32PSXCOL(r,g,b))|l)&0xffff));return;}

 PUTLE32(pdest, (X32PSXCOL(r,g,b))|l);
}

////////////////////////////////////////////////////////////////////////

__inline void GetTextureTransColG32_S(uint32_t * pdest,uint32_t color)
{
 int32_t r,g,b;

 if(color==0) return;

 r=(((X32COL1(color))* g_m1)&0xFF80FF80)>>7;
 b=(((X32COL2(color))* g_m2)&0xFF80FF80)>>7;
 g=(((X32COL3(color))* g_m3)&0xFF80FF80)>>7;

 if(r&0x7FE00000) r=0x1f0000|(r&0xFFFF);
 if(r&0x7FE0)     r=0x1f    |(r&0xFFFF0000);
 if(b&0x7FE00000) b=0x1f0000|(b&0xFFFF);
 if(b&0x7FE0)     b=0x1f    |(b&0xFFFF0000);
 if(g&0x7FE00000) g=0x1f0000|(g&0xFFFF);
 if(g&0x7FE0)     g=0x1f    |(g&0xFFFF0000);
         
 if((color&0xffff)==0)     {PUTLE32(pdest, (GETLE32(pdest)&0xffff)|(((X32PSXCOL(r,g,b))|lSetMask|(color&0x80008000))&0xffff0000));return;}
 if((color&0xffff0000)==0) {PUTLE32(pdest, (GETLE32(pdest)&0xffff0000)|(((X32PSXCOL(r,g,b))|lSetMask|(color&0x80008000))&0xffff));return;}

 PUTLE32(pdest, (X32PSXCOL(r,g,b))|lSetMask|(color&0x80008000));
}

////////////////////////////////////////////////////////////////////////

__inline void GetTextureTransColG32_SPR(uint32_t * pdest,uint32_t color)
{
 int32_t r,g,b;

 if(color==0) return;

 if(DrawSemiTrans && (color&0x80008000))
  {
   if(GlobalTextABR==0)
    {                 
     r=((((X32TCOL1(GETLE32(pdest)))+((X32COL1(color)) * g_m1))&0xFF00FF00)>>8);
     b=((((X32TCOL2(GETLE32(pdest)))+((X32COL2(color)) * g_m2))&0xFF00FF00)>>8);
     g=((((X32TCOL3(GETLE32(pdest)))+((X32COL3(color)) * g_m3))&0xFF00FF00)>>8);
    }
   else
   if(GlobalTextABR==1)
    {
     r=(X32COL1(GETLE32(pdest)))+(((((X32COL1(color)))* g_m1)&0xFF80FF80)>>7);
     b=(X32COL2(GETLE32(pdest)))+(((((X32COL2(color)))* g_m2)&0xFF80FF80)>>7);
     g=(X32COL3(GETLE32(pdest)))+(((((X32COL3(color)))* g_m3)&0xFF80FF80)>>7);
    }
   else
   if(GlobalTextABR==2)
    {
     int32_t t;
     r=(((((X32COL1(color)))* g_m1)&0xFF80FF80)>>7);
     t=(GETLE32(pdest)&0x001f0000)-(r&0x003f0000); if(t&0x80000000) t=0;
     r=(GETLE32(pdest)&0x0000001f)-(r&0x0000003f); if(r&0x80000000) r=0;
     r|=t;

     b=(((((X32COL2(color)))* g_m2)&0xFF80FF80)>>7);
     t=((GETLE32(pdest)>>5)&0x001f0000)-(b&0x003f0000); if(t&0x80000000) t=0;
     b=((GETLE32(pdest)>>5)&0x0000001f)-(b&0x0000003f); if(b&0x80000000) b=0;
     b|=t;

     g=(((((X32COL3(color)))* g_m3)&0xFF80FF80)>>7);
     t=((GETLE32(pdest)>>10)&0x001f0000)-(g&0x003f0000); if(t&0x80000000) t=0;
     g=((GETLE32(pdest)>>10)&0x0000001f)-(g&0x0000003f); if(g&0x80000000) g=0;
     g|=t;
    }
   else
    {
#ifdef HALFBRIGHTMODE3
     r=(X32COL1(GETLE32(pdest)))+(((((X32BCOL1(color))>>2)* g_m1)&0xFF80FF80)>>7);
     b=(X32COL2(GETLE32(pdest)))+(((((X32BCOL2(color))>>2)* g_m2)&0xFF80FF80)>>7);
     g=(X32COL3(GETLE32(pdest)))+(((((X32BCOL3(color))>>2)* g_m3)&0xFF80FF80)>>7);
#else
     r=(X32COL1(GETLE32(pdest)))+(((((X32ACOL1(color))>>1)* g_m1)&0xFF80FF80)>>7);
     b=(X32COL2(GETLE32(pdest)))+(((((X32ACOL2(color))>>1)* g_m2)&0xFF80FF80)>>7);
     g=(X32COL3(GETLE32(pdest)))+(((((X32ACOL3(color))>>1)* g_m3)&0xFF80FF80)>>7);
#endif
    }

   if(!(color&0x8000))
    {
     r=(r&0xffff0000)|((((X32COL1(color))* g_m1)&0x0000FF80)>>7);
     b=(b&0xffff0000)|((((X32COL2(color))* g_m2)&0x0000FF80)>>7);
     g=(g&0xffff0000)|((((X32COL3(color))* g_m3)&0x0000FF80)>>7);
    }
   if(!(color&0x80000000))
    {
     r=(r&0xffff)|((((X32COL1(color))* g_m1)&0xFF800000)>>7);
     b=(b&0xffff)|((((X32COL2(color))* g_m2)&0xFF800000)>>7);
     g=(g&0xffff)|((((X32COL3(color))* g_m3)&0xFF800000)>>7);
    }

  }
 else 
  {
   r=(((X32COL1(color))* g_m1)&0xFF80FF80)>>7;
   b=(((X32COL2(color))* g_m2)&0xFF80FF80)>>7;
   g=(((X32COL3(color))* g_m3)&0xFF80FF80)>>7;
  }

 if(r&0x7FE00000) r=0x1f0000|(r&0xFFFF);
 if(r&0x7FE0)     r=0x1f    |(r&0xFFFF0000);
 if(b&0x7FE00000) b=0x1f0000|(b&0xFFFF);
 if(b&0x7FE0)     b=0x1f    |(b&0xFFFF0000);
 if(g&0x7FE00000) g=0x1f0000|(g&0xFFFF);
 if(g&0x7FE0)     g=0x1f    |(g&0xFFFF0000);
         
 if(bCheckMask) 
  {
   uint32_t ma=GETLE32(pdest);

   PUTLE32(pdest, (X32PSXCOL(r,g,b))|lSetMask|(color&0x80008000));
   
   if((color&0xffff)==0    ) PUTLE32(pdest, (ma&0xffff)|(GETLE32(pdest)&0xffff0000));
   if((color&0xffff0000)==0) PUTLE32(pdest, (ma&0xffff0000)|(GETLE32(pdest)&0xffff));
   if(ma&0x80000000) PUTLE32(pdest, (ma&0xFFFF0000)|(GETLE32(pdest)&0xFFFF));
   if(ma&0x00008000) PUTLE32(pdest, (ma&0xFFFF)    |(GETLE32(pdest)&0xFFFF0000));

   return;                            
  }
 if((color&0xffff)==0    ) {PUTLE32(pdest, (GETLE32(pdest)&0xffff)|(((X32PSXCOL(r,g,b))|lSetMask|(color&0x80008000))&0xffff0000));return;}
 if((color&0xffff0000)==0) {PUTLE32(pdest, (GETLE32(pdest)&0xffff0000)|(((X32PSXCOL(r,g,b))|lSetMask|(color&0x80008000))&0xffff));return;}

 PUTLE32(pdest, (X32PSXCOL(r,g,b))|lSetMask|(color&0x80008000));
}

////////////////////////////////////////////////////////////////////////

__inline void GetTextureTransColGX_Dither(unsigned short * pdest,unsigned short color,int32_t m1,int32_t m2,int32_t m3)
{
 int32_t r,g,b;

 if(color==0) return;
 
 if(bCheckMask && (*pdest & HOST2LE16(0x8000))) return;

 m1=(((XCOL1D(color)))*m1)>>4;
 m2=(((XCOL2D(color)))*m2)>>4;
 m3=(((XCOL3D(color)))*m3)>>4;

 if(DrawSemiTrans && (color&0x8000))
  {
   r=((XCOL1D(GETLE16(pdest)))<<3);
   b=((XCOL2D(GETLE16(pdest)))<<3);
   g=((XCOL3D(GETLE16(pdest)))<<3);

   if(GlobalTextABR==0)
    {
     r=(r>>1)+(m1>>1);
     b=(b>>1)+(m2>>1);
     g=(g>>1)+(m3>>1);
    }
   else
   if(GlobalTextABR==1)
    {
     r+=m1;
     b+=m2;
     g+=m3;
    }
   else
   if(GlobalTextABR==2)
    {
     r-=m1;
     b-=m2;
     g-=m3;
     if(r&0x80000000) r=0;
     if(b&0x80000000) b=0;
     if(g&0x80000000) g=0;
    }
   else
    {
#ifdef HALFBRIGHTMODE3
     r+=(m1>>2);
     b+=(m2>>2);
     g+=(m3>>2);
#else
     r+=(m1>>1);
     b+=(m2>>1);
     g+=(m3>>1);
#endif
    }
  }
 else 
  {
   r=m1;
   b=m2;
   g=m3;
  }

 if(r&0x7FFFFF00) r=0xff;
 if(b&0x7FFFFF00) b=0xff;
 if(g&0x7FFFFF00) g=0xff;

 Dither16(pdest,r,b,g,sSetMask|(color&0x8000));

}

////////////////////////////////////////////////////////////////////////

__inline void GetTextureTransColGX(unsigned short * pdest,unsigned short color,short m1,short m2,short m3)
{
 int32_t r,g,b;unsigned short l;

 if(color==0) return;
 
 if(bCheckMask && (*pdest & HOST2LE16(0x8000))) return;

 l=sSetMask|(color&0x8000);

 if(DrawSemiTrans && (color&0x8000))
  {
   if(GlobalTextABR==0)
    {
     unsigned short d;
     d     =(GETLE16(pdest)&0x7bde)>>1;
     color =((color) &0x7bde)>>1;
     r=(XCOL1(d))+((((XCOL1(color)))* m1)>>7);
     b=(XCOL2(d))+((((XCOL2(color)))* m2)>>7);
     g=(XCOL3(d))+((((XCOL3(color)))* m3)>>7);
/*
     r=(XCOL1(*pdest)>>1)+((((XCOL1(color))>>1)* m1)>>7);
     b=(XCOL2(*pdest)>>1)+((((XCOL2(color))>>1)* m2)>>7);
     g=(XCOL3(*pdest)>>1)+((((XCOL3(color))>>1)* m3)>>7);
*/
    }
   else
   if(GlobalTextABR==1)
    {
     r=(XCOL1(GETLE16(pdest)))+((((XCOL1(color)))* m1)>>7);
     b=(XCOL2(GETLE16(pdest)))+((((XCOL2(color)))* m2)>>7);
     g=(XCOL3(GETLE16(pdest)))+((((XCOL3(color)))* m3)>>7);
    }
   else
   if(GlobalTextABR==2)
    {
     r=(XCOL1(GETLE16(pdest)))-((((XCOL1(color)))* m1)>>7);
     b=(XCOL2(GETLE16(pdest)))-((((XCOL2(color)))* m2)>>7);
     g=(XCOL3(GETLE16(pdest)))-((((XCOL3(color)))* m3)>>7);
     if(r&0x80000000) r=0;
     if(b&0x80000000) b=0;
     if(g&0x80000000) g=0;
    }
   else
    {
#ifdef HALFBRIGHTMODE3
     r=(XCOL1(GETLE16(pdest)))+((((XCOL1(color))>>2)* m1)>>7);
     b=(XCOL2(GETLE16(pdest)))+((((XCOL2(color))>>2)* m2)>>7);
     g=(XCOL3(GETLE16(pdest)))+((((XCOL3(color))>>2)* m3)>>7);
#else
     r=(XCOL1(GETLE16(pdest)))+((((XCOL1(color))>>1)* m1)>>7);
     b=(XCOL2(GETLE16(pdest)))+((((XCOL2(color))>>1)* m2)>>7);
     g=(XCOL3(GETLE16(pdest)))+((((XCOL3(color))>>1)* m3)>>7);
#endif
    }
  }
 else 
  {
   r=((XCOL1(color))* m1)>>7;
   b=((XCOL2(color))* m2)>>7;
   g=((XCOL3(color))* m3)>>7;
  }

 if(r&0x7FFFFFE0) r=0x1f;
 if(b&0x7FFFFC00) b=0x3e0;
 if(g&0x7FFF8000) g=0x7c00;

 PUTLE16(pdest, (XPSXCOL(r,g,b))|l);
}

////////////////////////////////////////////////////////////////////////

__inline void GetTextureTransColGX_S(unsigned short * pdest,unsigned short color,short m1,short m2,short m3)
{
 int32_t r,g,b;

 if(color==0) return;
 
 r=((XCOL1(color))* m1)>>7;
 b=((XCOL2(color))* m2)>>7;
 g=((XCOL3(color))* m3)>>7;

 if(r&0x7FFFFFE0) r=0x1f;
 if(b&0x7FFFFC00) b=0x3e0;
 if(g&0x7FFF8000) g=0x7c00;

 PUTLE16(pdest, (XPSXCOL(r,g,b))|sSetMask|(color&0x8000));
}

////////////////////////////////////////////////////////////////////////

__inline void GetTextureTransColGX32_S(uint32_t * pdest,uint32_t color,short m1,short m2,short m3)
{
 int32_t r,g,b;
 
 if(color==0) return;

 r=(((X32COL1(color))* m1)&0xFF80FF80)>>7;
 b=(((X32COL2(color))* m2)&0xFF80FF80)>>7;
 g=(((X32COL3(color))* m3)&0xFF80FF80)>>7;
                
 if(r&0x7FE00000) r=0x1f0000|(r&0xFFFF);
 if(r&0x7FE0)     r=0x1f    |(r&0xFFFF0000);
 if(b&0x7FE00000) b=0x1f0000|(b&0xFFFF);
 if(b&0x7FE0)     b=0x1f    |(b&0xFFFF0000);
 if(g&0x7FE00000) g=0x1f0000|(g&0xFFFF);
 if(g&0x7FE0)     g=0x1f    |(g&0xFFFF0000);

 if((color&0xffff)==0)     {PUTLE32(pdest, (GETLE32(pdest)&0xffff)|(((X32PSXCOL(r,g,b))|lSetMask|(color&0x80008000))&0xffff0000));return;}
 if((color&0xffff0000)==0) {PUTLE32(pdest, (GETLE32(pdest)&0xffff0000)|(((X32PSXCOL(r,g,b))|lSetMask|(color&0x80008000))&0xffff));return;}

 PUTLE32(pdest, (X32PSXCOL(r,g,b))|lSetMask|(color&0x80008000));
}

////////////////////////////////////////////////////////////////////////
// FILL FUNCS
////////////////////////////////////////////////////////////////////////

void FillSoftwareAreaTrans(short x0,short y0,short x1, // FILL AREA TRANS
                      short y1,unsigned short col)
{
 short j,i,dx,dy;

 if(y0>y1) return;
 if(x0>x1) return;

 if(x1<drawX) return;
 if(y1<drawY) return;
 if(x0>drawW) return;
 if(y0>drawH) return;

 x1=min(x1,drawW+1);
 y1=min(y1,drawH+1);
 x0=max(x0,drawX);
 y0=max(y0,drawY);
    
 if(y0>=iGPUHeight)   return;
 if(x0>1023)          return;

 if(y1>iGPUHeight) y1=iGPUHeight;
 if(x1>1024)       x1=1024;

 dx=x1-x0;dy=y1-y0;

 if(dx==1 && dy==1 && x0==1020 && y0==511)             // special fix for pinball game... emu protection???
  {
/*
m->v 1020 511 1 1
writedatamem 0x00000000 1
tile1 newcol 7fff (orgcol 0xffffff), oldvram 0
v->m 1020 511 1 1
readdatamem 0x00007fff 1
m->v 1020 511 1 1
writedatamem 0x00000000 1
tile1 newcol 8000 (orgcol 0xffffff), oldvram 0
v->m 1020 511 1 1
readdatamem 0x00008000 1
*/

   static int iCheat=0;
   col+=iCheat;
   if(iCheat==1) iCheat=0; else iCheat=1;
  }


 if(dx&1)                                              // slow fill
  {
   unsigned short *DSTPtr;
   unsigned short LineOffset;
   DSTPtr = psxVuw + (1024*y0) + x0;
   LineOffset = 1024 - dx;
   for(i=0;i<dy;i++)
    {
     for(j=0;j<dx;j++)
      GetShadeTransCol(DSTPtr++,col);
     DSTPtr += LineOffset;
    } 
  }
 else                                                  // fast fill
  {
   uint32_t *DSTPtr;
   unsigned short LineOffset;
   uint32_t lcol=lSetMask|(((uint32_t)(col))<<16)|col;
   dx>>=1;
   DSTPtr = (uint32_t *)(psxVuw + (1024*y0) + x0);
   LineOffset = 512 - dx;

   if(!bCheckMask && !DrawSemiTrans)
    {
     for(i=0;i<dy;i++)
      {
       for(j=0;j<dx;j++) { PUTLE32(DSTPtr, lcol); DSTPtr++; }
       DSTPtr += LineOffset;
      }
    }
   else
    {
     for(i=0;i<dy;i++)
      {
       for(j=0;j<dx;j++) 
        GetShadeTransCol32(DSTPtr++,lcol);
       DSTPtr += LineOffset;
      } 
    }
  }
}

////////////////////////////////////////////////////////////////////////

void FillSoftwareArea(short x0,short y0,short x1,      // FILL AREA (BLK FILL)
                      short y1,unsigned short col)     // no draw area check here!
{
 short j,i,dx,dy;

 if(y0>y1) return;
 if(x0>x1) return;
    
 if(y0>=iGPUHeight)   return;
 if(x0>1023)          return;

 if(y1>iGPUHeight) y1=iGPUHeight;
 if(x1>1024)       x1=1024;

 dx=x1-x0;dy=y1-y0;
 if(dx&1)
  {
   unsigned short *DSTPtr;
   unsigned short LineOffset;

   DSTPtr = psxVuw + (1024*y0) + x0;
   LineOffset = 1024 - dx;

   for(i=0;i<dy;i++)
    {
     for(j=0;j<dx;j++) { PUTLE16(DSTPtr, col); DSTPtr++; }
     DSTPtr += LineOffset;
    } 
  }
 else
  {
   uint32_t *DSTPtr;
   unsigned short LineOffset;
   uint32_t lcol=(((int32_t)col)<<16)|col;
   dx>>=1;
   DSTPtr = (uint32_t *)(psxVuw + (1024*y0) + x0);
   LineOffset = 512 - dx;

   for(i=0;i<dy;i++)
    {
     for(j=0;j<dx;j++) { PUTLE32(DSTPtr, lcol); DSTPtr++; }
     DSTPtr += LineOffset;
    } 
  }
}

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
// EDGE INTERPOLATION
////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////

typedef struct SOFTVTAG
{
 int x,y;   
 int u,v;
 int32_t R,G,B;
} soft_vertex;

static soft_vertex vtx[4];
static soft_vertex * left_array[4], * right_array[4];
static int left_section, right_section;
static int left_section_height, right_section_height;
static int left_x, delta_left_x, right_x, delta_right_x;
static int left_u, delta_left_u, left_v, delta_left_v;
static int right_u, delta_right_u, right_v, delta_right_v;
static int left_R, delta_left_R, right_R, delta_right_R;
static int left_G, delta_left_G, right_G, delta_right_G;
static int left_B, delta_left_B, right_B, delta_right_B;

#ifdef USE_NASM

// NASM version (external):
#define shl10idiv i386_shl10idiv

__inline int shl10idiv(int x, int y);

#else

__inline int shl10idiv(int x, int y)
{
 __int64 bi=x;
 bi<<=10;
 return bi/y;
}

#endif

#if 0

// GNUC long long int version:

__inline int shl10idiv(int x, int y) 
{ 
 long long int bi=x; 
 bi<<=10; 
 return bi/y; 
}

#endif

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
                        
__inline int RightSection_F(void)
{
 soft_vertex * v1 = right_array[ right_section ];
 soft_vertex * v2 = right_array[ right_section-1 ];

 int height = v2->y - v1->y;
 if(height == 0) return 0;
 delta_right_x = (v2->x - v1->x) / height;
 right_x = v1->x;

 right_section_height = height;
 return height;
}

////////////////////////////////////////////////////////////////////////

__inline int LeftSection_F(void)
{
 soft_vertex * v1 = left_array[ left_section ];
 soft_vertex * v2 = left_array[ left_section-1 ];

 int height = v2->y - v1->y;
 if(height == 0) return 0;
 delta_left_x = (v2->x - v1->x) / height;
 left_x = v1->x;

 left_section_height = height;
 return height;  
}

////////////////////////////////////////////////////////////////////////

__inline BOOL NextRow_F(void)
{
 if(--left_section_height<=0) 
  {
   if(--left_section <= 0) {return TRUE;}
   if(LeftSection_F()  <= 0) {return TRUE;}
  }
 else
  {
   left_x += delta_left_x;
  }

 if(--right_section_height<=0) 
  {
   if(--right_section<=0) {return TRUE;}
   if(RightSection_F() <=0) {return TRUE;}
  }
 else
  {
   right_x += delta_right_x;
  }
 return FALSE;
}

////////////////////////////////////////////////////////////////////////

__inline BOOL SetupSections_F(short x1, short y1, short x2, short y2, short x3, short y3)
{
 soft_vertex * v1, * v2, * v3;
 int height,longest;

 v1 = vtx;   v1->x=x1<<16;v1->y=y1;
 v2 = vtx+1; v2->x=x2<<16;v2->y=y2;
 v3 = vtx+2; v3->x=x3<<16;v3->y=y3;

 if(v1->y > v2->y) { soft_vertex * v = v1; v1 = v2; v2 = v; }
 if(v1->y > v3->y) { soft_vertex * v = v1; v1 = v3; v3 = v; }
 if(v2->y > v3->y) { soft_vertex * v = v2; v2 = v3; v3 = v; }

 height = v3->y - v1->y;
 if(height == 0) {return FALSE;}
 longest = (((v2->y - v1->y) << 16) / height) * ((v3->x - v1->x)>>16) + (v1->x - v2->x);
 if(longest == 0) {return FALSE;}

 if(longest < 0)
  {
   right_array[0] = v3;
   right_array[1] = v2;
   right_array[2] = v1;
   right_section  = 2;
   left_array[0]  = v3;
   left_array[1]  = v1;
   left_section   = 1;

   if(LeftSection_F() <= 0) return FALSE;
   if(RightSection_F() <= 0)
    {
     right_section--;
     if(RightSection_F() <= 0) return FALSE;
    }
  }
 else
  {
   left_array[0]  = v3;
   left_array[1]  = v2;
   left_array[2]  = v1;
   left_section   = 2;
   right_array[0] = v3;
   right_array[1] = v1;
   right_section  = 1;

   if(RightSection_F() <= 0) return FALSE;
   if(LeftSection_F() <= 0)
    {    
     left_section--;
     if(LeftSection_F() <= 0) return FALSE;
    }
  }

 Ymin=v1->y;
 Ymax=min(v3->y-1,drawH);

 return TRUE;
}

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////

__inline int RightSection_G(void)
{
 soft_vertex * v1 = right_array[ right_section ];
 soft_vertex * v2 = right_array[ right_section-1 ];

 int height = v2->y - v1->y;
 if(height == 0) return 0;
 delta_right_x = (v2->x - v1->x) / height;
 right_x = v1->x;

 right_section_height = height;
 return height;
}

////////////////////////////////////////////////////////////////////////

__inline int LeftSection_G(void)
{
 soft_vertex * v1 = left_array[ left_section ];
 soft_vertex * v2 = left_array[ left_section-1 ];

 int height = v2->y - v1->y;
 if(height == 0) return 0;
 delta_left_x = (v2->x - v1->x) / height;
 left_x = v1->x;

 delta_left_R = ((v2->R - v1->R)) / height;
 left_R = v1->R;
 delta_left_G = ((v2->G - v1->G)) / height;
 left_G = v1->G;
 delta_left_B = ((v2->B - v1->B)) / height;
 left_B = v1->B;

 left_section_height = height;
 return height;  
}

////////////////////////////////////////////////////////////////////////

__inline BOOL NextRow_G(void)
{
 if(--left_section_height<=0) 
  {
   if(--left_section <= 0) {return TRUE;}
   if(LeftSection_G()  <= 0) {return TRUE;}
  }
 else
  {
   left_x += delta_left_x;
   left_R += delta_left_R;
   left_G += delta_left_G;
   left_B += delta_left_B;
  }

 if(--right_section_height<=0) 
  {
   if(--right_section<=0) {return TRUE;}
   if(RightSection_G() <=0) {return TRUE;}
  }
 else
  {
   right_x += delta_right_x;
  }
 return FALSE;
}

////////////////////////////////////////////////////////////////////////

__inline BOOL SetupSections_G(short x1,short y1,short x2,short y2,short x3,short y3,int32_t rgb1, int32_t rgb2, int32_t rgb3)
{
 soft_vertex * v1, * v2, * v3;
 int height,longest,temp;

 v1 = vtx;   v1->x=x1<<16;v1->y=y1;
 v1->R=(rgb1) & 0x00ff0000;
 v1->G=(rgb1<<8) & 0x00ff0000;
 v1->B=(rgb1<<16) & 0x00ff0000;
 v2 = vtx+1; v2->x=x2<<16;v2->y=y2;
 v2->R=(rgb2) & 0x00ff0000;
 v2->G=(rgb2<<8) & 0x00ff0000;
 v2->B=(rgb2<<16) & 0x00ff0000;
 v3 = vtx+2; v3->x=x3<<16;v3->y=y3;
 v3->R=(rgb3) & 0x00ff0000;
 v3->G=(rgb3<<8) & 0x00ff0000;
 v3->B=(rgb3<<16) & 0x00ff0000;

 if(v1->y > v2->y) { soft_vertex * v = v1; v1 = v2; v2 = v; }
 if(v1->y > v3->y) { soft_vertex * v = v1; v1 = v3; v3 = v; }
 if(v2->y > v3->y) { soft_vertex * v = v2; v2 = v3; v3 = v; }

 height = v3->y - v1->y;
 if(height == 0) {return FALSE;}
 temp=(((v2->y - v1->y) << 16) / height);
 longest = temp * ((v3->x - v1->x)>>16) + (v1->x - v2->x);
 if(longest == 0) {return FALSE;}

 if(longest < 0)
  {
   right_array[0] = v3;
   right_array[1] = v2;
   right_array[2] = v1;
   right_section  = 2;
   left_array[0]  = v3;
   left_array[1]  = v1;
   left_section   = 1;

   if(LeftSection_G() <= 0) return FALSE;
   if(RightSection_G() <= 0)
    {
     right_section--;
     if(RightSection_G() <= 0) return FALSE;
    }
   if(longest > -0x1000) longest = -0x1000;     
  }
 else
  {
   left_array[0]  = v3;
   left_array[1]  = v2;
   left_array[2]  = v1;
   left_section   = 2;
   right_array[0] = v3;
   right_array[1] = v1;
   right_section  = 1;

   if(RightSection_G() <= 0) return FALSE;
   if(LeftSection_G() <= 0)
    {    
     left_section--;
     if(LeftSection_G() <= 0) return FALSE;
    }
   if(longest < 0x1000) longest = 0x1000;     
  }

 Ymin=v1->y;
 Ymax=min(v3->y-1,drawH);    

 delta_right_R=shl10idiv(temp*((v3->R - v1->R)>>10)+((v1->R - v2->R)<<6),longest);
 delta_right_G=shl10idiv(temp*((v3->G - v1->G)>>10)+((v1->G - v2->G)<<6),longest);
 delta_right_B=shl10idiv(temp*((v3->B - v1->B)>>10)+((v1->B - v2->B)<<6),longest);

 return TRUE;
}

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////

__inline int RightSection_FT(void)
{
 soft_vertex * v1 = right_array[ right_section ];
 soft_vertex * v2 = right_array[ right_section-1 ];

 int height = v2->y - v1->y;
 if(height == 0) return 0;
 delta_right_x = (v2->x - v1->x) / height;
 right_x = v1->x;

 right_section_height = height;
 return height;
}

////////////////////////////////////////////////////////////////////////

__inline int LeftSection_FT(void)
{
 soft_vertex * v1 = left_array[ left_section ];
 soft_vertex * v2 = left_array[ left_section-1 ];

 int height = v2->y - v1->y;
 if(height == 0) return 0;
 delta_left_x = (v2->x - v1->x) / height;
 left_x = v1->x;
 
 delta_left_u = ((v2->u - v1->u)) / height;
 left_u = v1->u;
 delta_left_v = ((v2->v - v1->v)) / height;
 left_v = v1->v;

 left_section_height = height;
 return height;  
}

////////////////////////////////////////////////////////////////////////

__inline BOOL NextRow_FT(void)
{
 if(--left_section_height<=0) 
  {
   if(--left_section <= 0) {return TRUE;}
   if(LeftSection_FT()  <= 0) {return TRUE;}
  }
 else
  {
   left_x += delta_left_x;
   left_u += delta_left_u;
   left_v += delta_left_v;
  }

 if(--right_section_height<=0) 
  {
   if(--right_section<=0) {return TRUE;}
   if(RightSection_FT() <=0) {return TRUE;}
  }
 else
  {
   right_x += delta_right_x;
  }
 return FALSE;
}

////////////////////////////////////////////////////////////////////////

__inline BOOL SetupSections_FT(short x1, short y1, short x2, short y2, short x3, short y3, short tx1, short ty1, short tx2, short ty2, short tx3, short ty3)
{
 soft_vertex * v1, * v2, * v3;
 int height,longest,temp;

 v1 = vtx;   v1->x=x1<<16;v1->y=y1;
 v1->u=tx1<<16;v1->v=ty1<<16;
 v2 = vtx+1; v2->x=x2<<16;v2->y=y2;
 v2->u=tx2<<16;v2->v=ty2<<16;
 v3 = vtx+2; v3->x=x3<<16;v3->y=y3;
 v3->u=tx3<<16;v3->v=ty3<<16;

 if(v1->y > v2->y) { soft_vertex * v = v1; v1 = v2; v2 = v; }
 if(v1->y > v3->y) { soft_vertex * v = v1; v1 = v3; v3 = v; }
 if(v2->y > v3->y) { soft_vertex * v = v2; v2 = v3; v3 = v; }

 height = v3->y - v1->y;
 if(height == 0) {return FALSE;}

 temp=(((v2->y - v1->y) << 16) / height);
 longest = temp * ((v3->x - v1->x)>>16) + (v1->x - v2->x);

 if(longest == 0) {return FALSE;}

 if(longest < 0)
  {
   right_array[0] = v3;
   right_array[1] = v2;
   right_array[2] = v1;
   right_section  = 2;
   left_array[0]  = v3;
   left_array[1]  = v1;
   left_section   = 1;

   if(LeftSection_FT() <= 0) return FALSE;
   if(RightSection_FT() <= 0)
    {
     right_section--;
     if(RightSection_FT() <= 0) return FALSE;
    }
   if(longest > -0x1000) longest = -0x1000;     
  }
 else
  {
   left_array[0]  = v3;
   left_array[1]  = v2;
   left_array[2]  = v1;
   left_section   = 2;
   right_array[0] = v3;
   right_array[1] = v1;
   right_section  = 1;

   if(RightSection_FT() <= 0) return FALSE;
   if(LeftSection_FT() <= 0)
    {    
     left_section--;                
     if(LeftSection_FT() <= 0) return FALSE;
    }
   if(longest < 0x1000) longest = 0x1000;     
  }

 Ymin=v1->y;
 Ymax=min(v3->y-1,drawH);

 delta_right_u=shl10idiv(temp*((v3->u - v1->u)>>10)+((v1->u - v2->u)<<6),longest);
 delta_right_v=shl10idiv(temp*((v3->v - v1->v)>>10)+((v1->v - v2->v)<<6),longest);

/*
Mmm... adjust neg tex deltas... will sometimes cause slight
texture distortions 

 longest>>=16;
 if(longest)
  {
   if(longest<0) longest=-longest;
   if(delta_right_u<0)
    delta_right_u-=delta_right_u/longest;
   if(delta_right_v<0)
    delta_right_v-=delta_right_v/longest;
  }
*/

 return TRUE;
}

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////

__inline int RightSection_GT(void)
{
 soft_vertex * v1 = right_array[ right_section ];
 soft_vertex * v2 = right_array[ right_section-1 ];

 int height = v2->y - v1->y;
 if(height == 0) return 0;
 delta_right_x = (v2->x - v1->x) / height;
 right_x = v1->x;

 right_section_height = height;
 return height;
}

////////////////////////////////////////////////////////////////////////

__inline int LeftSection_GT(void)
{
 soft_vertex * v1 = left_array[ left_section ];
 soft_vertex * v2 = left_array[ left_section-1 ];

 int height = v2->y - v1->y;
 if(height == 0) return 0;
 delta_left_x = (v2->x - v1->x) / height;
 left_x = v1->x;

 delta_left_u = ((v2->u - v1->u)) / height;
 left_u = v1->u;
 delta_left_v = ((v2->v - v1->v)) / height;
 left_v = v1->v;

 delta_left_R = ((v2->R - v1->R)) / height;
 left_R = v1->R;
 delta_left_G = ((v2->G - v1->G)) / height;
 left_G = v1->G;
 delta_left_B = ((v2->B - v1->B)) / height;
 left_B = v1->B;

 left_section_height = height;
 return height;  
}

////////////////////////////////////////////////////////////////////////

__inline BOOL NextRow_GT(void)
{
 if(--left_section_height<=0) 
  {
   if(--left_section <= 0) {return TRUE;}
   if(LeftSection_GT()  <= 0) {return TRUE;}
  }
 else
  {
   left_x += delta_left_x;
   left_u += delta_left_u;
   left_v += delta_left_v;
   left_R += delta_left_R;
   left_G += delta_left_G;
   left_B += delta_left_B;
  }

 if(--right_section_height<=0) 
  {
   if(--right_section<=0) {return TRUE;}
   if(RightSection_GT() <=0) {return TRUE;}
  }
 else
  {
   right_x += delta_right_x;
  }
 return FALSE;
}

////////////////////////////////////////////////////////////////////////

__inline BOOL SetupSections_GT(short x1, short y1, short x2, short y2, short x3, short y3, short tx1, short ty1, short tx2, short ty2, short tx3, short ty3, int32_t rgb1, int32_t rgb2, int32_t rgb3)
{
 soft_vertex * v1, * v2, * v3;
 int height,longest,temp;

 v1 = vtx;   v1->x=x1<<16;v1->y=y1;
 v1->u=tx1<<16;v1->v=ty1<<16;
 v1->R=(rgb1) & 0x00ff0000;
 v1->G=(rgb1<<8) & 0x00ff0000;
 v1->B=(rgb1<<16) & 0x00ff0000;

 v2 = vtx+1; v2->x=x2<<16;v2->y=y2;
 v2->u=tx2<<16;v2->v=ty2<<16;
 v2->R=(rgb2) & 0x00ff0000;
 v2->G=(rgb2<<8) & 0x00ff0000;
 v2->B=(rgb2<<16) & 0x00ff0000;
             
 v3 = vtx+2; v3->x=x3<<16;v3->y=y3;
 v3->u=tx3<<16;v3->v=ty3<<16;
 v3->R=(rgb3) & 0x00ff0000;
 v3->G=(rgb3<<8) & 0x00ff0000;
 v3->B=(rgb3<<16) & 0x00ff0000;

 if(v1->y > v2->y) { soft_vertex * v = v1; v1 = v2; v2 = v; }
 if(v1->y > v3->y) { soft_vertex * v = v1; v1 = v3; v3 = v; }
 if(v2->y > v3->y) { soft_vertex * v = v2; v2 = v3; v3 = v; }

 height = v3->y - v1->y;
 if(height == 0) {return FALSE;}

 temp=(((v2->y - v1->y) << 16) / height);
 longest = temp * ((v3->x - v1->x)>>16) + (v1->x - v2->x);

 if(longest == 0) {return FALSE;}

 if(longest < 0)
  {
   right_array[0] = v3;
   right_array[1] = v2;
   right_array[2] = v1;
   right_section  = 2;
   left_array[0]  = v3;
   left_array[1]  = v1;
   left_section   = 1;

   if(LeftSection_GT() <= 0) return FALSE;
   if(RightSection_GT() <= 0)
    {
     right_section--;
     if(RightSection_GT() <= 0) return FALSE;
    }

   if(longest > -0x1000) longest = -0x1000;     
  }
 else
  {
   left_array[0]  = v3;
   left_array[1]  = v2;
   left_array[2]  = v1;
   left_section   = 2;
   right_array[0] = v3;
   right_array[1] = v1;
   right_section  = 1;

   if(RightSection_GT() <= 0) return FALSE;
   if(LeftSection_GT() <= 0)
    {    
     left_section--;
     if(LeftSection_GT() <= 0) return FALSE;
    }
   if(longest < 0x1000) longest = 0x1000;     
  }

 Ymin=v1->y;
 Ymax=min(v3->y-1,drawH);

 delta_right_R=shl10idiv(temp*((v3->R - v1->R)>>10)+((v1->R - v2->R)<<6),longest);
 delta_right_G=shl10idiv(temp*((v3->G - v1->G)>>10)+((v1->G - v2->G)<<6),longest);
 delta_right_B=shl10idiv(temp*((v3->B - v1->B)>>10)+((v1->B - v2->B)<<6),longest);

 delta_right_u=shl10idiv(temp*((v3->u - v1->u)>>10)+((v1->u - v2->u)<<6),longest);
 delta_right_v=shl10idiv(temp*((v3->v - v1->v)>>10)+((v1->v - v2->v)<<6),longest);


/*
Mmm... adjust neg tex deltas... will sometimes cause slight
texture distortions 
 longest>>=16;
 if(longest)
  {
   if(longest<0) longest=-longest;
   if(delta_right_u<0)
    delta_right_u-=delta_right_u/longest;
   if(delta_right_v<0)
    delta_right_v-=delta_right_v/longest;
  }
*/


 return TRUE;
}

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////

__inline int RightSection_F4(void)
{
 soft_vertex * v1 = right_array[ right_section ];
 soft_vertex * v2 = right_array[ right_section-1 ];

 int height = v2->y - v1->y;
 right_section_height = height;
 right_x = v1->x;
 if(height == 0) 
  {
   return 0;
  }
 delta_right_x = (v2->x - v1->x) / height;

 return height;
}

////////////////////////////////////////////////////////////////////////

__inline int LeftSection_F4(void)
{
 soft_vertex * v1 = left_array[ left_section ];
 soft_vertex * v2 = left_array[ left_section-1 ];

 int height = v2->y - v1->y;
 left_section_height = height;
 left_x = v1->x;
 if(height == 0) 
  {
   return 0;
  }
 delta_left_x = (v2->x - v1->x) / height;

 return height;  
}

////////////////////////////////////////////////////////////////////////

__inline BOOL NextRow_F4(void)
{
 if(--left_section_height<=0) 
  {
   if(--left_section > 0) 
    while(LeftSection_F4()<=0) 
     {
      if(--left_section  <= 0) break;
     }
  }
 else
  {
   left_x += delta_left_x;
  }

 if(--right_section_height<=0) 
  {
   if(--right_section > 0) 
    while(RightSection_F4()<=0) 
     {
      if(--right_section<=0) break;
     }
  }
 else
  {
   right_x += delta_right_x;
  }
 return FALSE;
}

////////////////////////////////////////////////////////////////////////

__inline BOOL SetupSections_F4(short x1, short y1, short x2, short y2, short x3, short y3, short x4, short y4)
{
 soft_vertex * v1, * v2, * v3, * v4;
 int height,width,longest1,longest2;

 v1 = vtx;   v1->x=x1<<16;v1->y=y1;
 v2 = vtx+1; v2->x=x2<<16;v2->y=y2;
 v3 = vtx+2; v3->x=x3<<16;v3->y=y3;
 v4 = vtx+3; v4->x=x4<<16;v4->y=y4;

 if(v1->y > v2->y) { soft_vertex * v = v1; v1 = v2; v2 = v; }
 if(v1->y > v3->y) { soft_vertex * v = v1; v1 = v3; v3 = v; }
 if(v1->y > v4->y) { soft_vertex * v = v1; v1 = v4; v4 = v; }
 if(v2->y > v3->y) { soft_vertex * v = v2; v2 = v3; v3 = v; }
 if(v2->y > v4->y) { soft_vertex * v = v2; v2 = v4; v4 = v; }
 if(v3->y > v4->y) { soft_vertex * v = v3; v3 = v4; v4 = v; }

 height = v4->y - v1->y; if(height == 0) height =1;
 width  = (v4->x - v1->x)>>16;
 longest1 = (((v2->y - v1->y) << 16) / height) * width + (v1->x - v2->x);
 longest2 = (((v3->y - v1->y) << 16) / height) * width + (v1->x - v3->x);

 if(longest1 < 0)                                      // 2 is right
  {
   if(longest2 < 0)                                    // 3 is right
    {
     left_array[0]  = v4;
     left_array[1]  = v1;
     left_section   = 1;

     height = v3->y - v1->y; if(height == 0) height=1;
     longest1 = (((v2->y - v1->y) << 16) / height) * ((v3->x - v1->x)>>16) + (v1->x - v2->x);
     if(longest1 >= 0)
      {
       right_array[0] = v4;                     //  1
       right_array[1] = v3;                     //     3
       right_array[2] = v1;                     //  4
       right_section  = 2;    
      }
     else
      {
       height = v4->y - v2->y; if(height == 0) height=1;
       longest1 = (((v3->y - v2->y) << 16) / height) * ((v4->x - v2->x)>>16) + (v2->x - v3->x);
       if(longest1 >= 0)
        {
         right_array[0] = v4;                    //  1
         right_array[1] = v2;                    //     2
         right_array[2] = v1;                    //  4
         right_section  = 2;    
        }
       else
        {
         right_array[0] = v4;                    //  1
         right_array[1] = v3;                    //     2
         right_array[2] = v2;                    //     3
         right_array[3] = v1;                    //  4
         right_section  = 3;    
        }
      }
    }
   else                                            
    {
     left_array[0]  = v4;
     left_array[1]  = v3;                         //    1
     left_array[2]  = v1;                         //      2
     left_section   = 2;                          //  3
     right_array[0] = v4;                         //    4
     right_array[1] = v2;
     right_array[2] = v1;
     right_section  = 2;
    }
  }
 else
  {
   if(longest2 < 0)             
    {
     left_array[0]  = v4;                          //    1
     left_array[1]  = v2;                          //  2
     left_array[2]  = v1;                          //      3
     left_section   = 2;                           //    4
     right_array[0] = v4;
     right_array[1] = v3;
     right_array[2] = v1;
     right_section  = 2;
    }
   else                         
    {
     right_array[0] = v4;
     right_array[1] = v1;
     right_section  = 1;

     height = v3->y - v1->y; if(height == 0) height=1;
     longest1 = (((v2->y - v1->y) << 16) / height) * ((v3->x - v1->x)>>16) + (v1->x - v2->x);
     if(longest1<0)
      {
       left_array[0]  = v4;                        //    1
       left_array[1]  = v3;                        //  3
       left_array[2]  = v1;                        //    4
       left_section   = 2;    
      }
     else
      {
       height = v4->y - v2->y; if(height == 0) height=1;
       longest1 = (((v3->y - v2->y) << 16) / height) * ((v4->x - v2->x)>>16) + (v2->x - v3->x);
       if(longest1<0)
        {
         left_array[0]  = v4;                      //    1
         left_array[1]  = v2;                      //  2
         left_array[2]  = v1;                      //    4
         left_section   = 2;    
        }
       else
        {
         left_array[0]  = v4;                      //    1
         left_array[1]  = v3;                      //  2
         left_array[2]  = v2;                      //  3
         left_array[3]  = v1;                      //     4
         left_section   = 3;    
        }
      }
    }
  }

 while(LeftSection_F4()<=0) 
  {
   if(--left_section  <= 0) break;
  }

 while(RightSection_F4()<=0) 
  {
   if(--right_section <= 0) break;
  }

 Ymin=v1->y;
 Ymax=min(v4->y-1,drawH);

 return TRUE;
}

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////

__inline int RightSection_FT4(void)
{
 soft_vertex * v1 = right_array[ right_section ];
 soft_vertex * v2 = right_array[ right_section-1 ];

 int height = v2->y - v1->y;
 right_section_height = height;
 right_x = v1->x;
 right_u = v1->u;
 right_v = v1->v;
 if(height == 0) 
  {
   return 0;
  }
 delta_right_x = (v2->x - v1->x) / height;
 delta_right_u = (v2->u - v1->u) / height;
 delta_right_v = (v2->v - v1->v) / height;

 return height;
}

////////////////////////////////////////////////////////////////////////

__inline int LeftSection_FT4(void)
{
 soft_vertex * v1 = left_array[ left_section ];
 soft_vertex * v2 = left_array[ left_section-1 ];

 int height = v2->y - v1->y;
 left_section_height = height;
 left_x = v1->x;
 left_u = v1->u;
 left_v = v1->v;
 if(height == 0) 
  {
   return 0;
  }
 delta_left_x = (v2->x - v1->x) / height;
 delta_left_u = (v2->u - v1->u) / height;
 delta_left_v = (v2->v - v1->v) / height;

 return height;  
}

////////////////////////////////////////////////////////////////////////

__inline BOOL NextRow_FT4(void)
{
 if(--left_section_height<=0) 
  {
   if(--left_section > 0) 
    while(LeftSection_FT4()<=0) 
     {
      if(--left_section  <= 0) break;
     }
  }
 else
  {
   left_x += delta_left_x;
   left_u += delta_left_u;
   left_v += delta_left_v;
  }

 if(--right_section_height<=0) 
  {
   if(--right_section > 0) 
    while(RightSection_FT4()<=0) 
     {
      if(--right_section<=0) break;
     }
  }
 else
  {
   right_x += delta_right_x;
   right_u += delta_right_u;
   right_v += delta_right_v;
  }
 return FALSE;
}

////////////////////////////////////////////////////////////////////////

__inline BOOL SetupSections_FT4(short x1, short y1, short x2, short y2, short x3, short y3, short x4, short y4, short tx1, short ty1, short tx2, short ty2, short tx3, short ty3, short tx4, short ty4)
{
 soft_vertex * v1, * v2, * v3, * v4;
 int height,width,longest1,longest2;

 v1 = vtx;   v1->x=x1<<16;v1->y=y1;
 v1->u=tx1<<16;v1->v=ty1<<16;

 v2 = vtx+1; v2->x=x2<<16;v2->y=y2;
 v2->u=tx2<<16;v2->v=ty2<<16;
             
 v3 = vtx+2; v3->x=x3<<16;v3->y=y3;
 v3->u=tx3<<16;v3->v=ty3<<16;

 v4 = vtx+3; v4->x=x4<<16;v4->y=y4;
 v4->u=tx4<<16;v4->v=ty4<<16;

 if(v1->y > v2->y) { soft_vertex * v = v1; v1 = v2; v2 = v; }
 if(v1->y > v3->y) { soft_vertex * v = v1; v1 = v3; v3 = v; }
 if(v1->y > v4->y) { soft_vertex * v = v1; v1 = v4; v4 = v; }
 if(v2->y > v3->y) { soft_vertex * v = v2; v2 = v3; v3 = v; }
 if(v2->y > v4->y) { soft_vertex * v = v2; v2 = v4; v4 = v; }
 if(v3->y > v4->y) { soft_vertex * v = v3; v3 = v4; v4 = v; }

 height = v4->y - v1->y; if(height == 0) height =1;
 width  = (v4->x - v1->x)>>16;
 longest1 = (((v2->y - v1->y) << 16) / height) * width + (v1->x - v2->x);
 longest2 = (((v3->y - v1->y) << 16) / height) * width + (v1->x - v3->x);

 if(longest1 < 0)                                      // 2 is right
  {
   if(longest2 < 0)                                    // 3 is right
    {
     left_array[0]  = v4;
     left_array[1]  = v1;
     left_section   = 1;

     height = v3->y - v1->y; if(height == 0) height=1;
     longest1 = (((v2->y - v1->y) << 16) / height) * ((v3->x - v1->x)>>16) + (v1->x - v2->x);
     if(longest1 >= 0)
      {
       right_array[0] = v4;                     //  1
       right_array[1] = v3;                     //     3
       right_array[2] = v1;                     //  4
       right_section  = 2;    
      }
     else
      {
       height = v4->y - v2->y; if(height == 0) height=1;
       longest1 = (((v3->y - v2->y) << 16) / height) * ((v4->x - v2->x)>>16) + (v2->x - v3->x);
       if(longest1 >= 0)
        {
         right_array[0] = v4;                    //  1
         right_array[1] = v2;                    //     2
         right_array[2] = v1;                    //  4
         right_section  = 2;    
        }
       else
        {
         right_array[0] = v4;                    //  1
         right_array[1] = v3;                    //     2
         right_array[2] = v2;                    //     3
         right_array[3] = v1;                    //  4
         right_section  = 3;    
        }
      }
    }
   else                                            
    {
     left_array[0]  = v4;
     left_array[1]  = v3;                         //    1
     left_array[2]  = v1;                         //      2
     left_section   = 2;                          //  3
     right_array[0] = v4;                         //    4
     right_array[1] = v2;
     right_array[2] = v1;
     right_section  = 2;
    }
  }
 else
  {
   if(longest2 < 0)             
    {
     left_array[0]  = v4;                          //    1
     left_array[1]  = v2;                          //  2
     left_array[2]  = v1;                          //      3
     left_section   = 2;                           //    4
     right_array[0] = v4;
     right_array[1] = v3;
     right_array[2] = v1;
     right_section  = 2;
    }
   else                         
    {
     right_array[0] = v4;
     right_array[1] = v1;
     right_section  = 1;

     height = v3->y - v1->y; if(height == 0) height=1;
     longest1 = (((v2->y - v1->y) << 16) / height) * ((v3->x - v1->x)>>16) + (v1->x - v2->x);
     if(longest1<0)
      {
       left_array[0]  = v4;                        //    1
       left_array[1]  = v3;                        //  3
       left_array[2]  = v1;                        //    4
       left_section   = 2;    
      }
     else
      {
       height = v4->y - v2->y; if(height == 0) height=1;
       longest1 = (((v3->y - v2->y) << 16) / height) * ((v4->x - v2->x)>>16) + (v2->x - v3->x);
       if(longest1<0)
        {
         left_array[0]  = v4;                      //    1
         left_array[1]  = v2;                      //  2
         left_array[2]  = v1;                      //    4
         left_section   = 2;    
        }
       else
        {
         left_array[0]  = v4;                      //    1
         left_array[1]  = v3;                      //  2
         left_array[2]  = v2;                      //  3
         left_array[3]  = v1;                      //     4
         left_section   = 3;    
        }
      }
    }
  }

 while(LeftSection_FT4()<=0) 
  {
   if(--left_section  <= 0) break;
  }

 while(RightSection_FT4()<=0) 
  {
   if(--right_section <= 0) break;
  }

 Ymin=v1->y;
 Ymax=min(v4->y-1,drawH);

 return TRUE;
}

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////

__inline int RightSection_GT4(void)
{
 soft_vertex * v1 = right_array[ right_section ];
 soft_vertex * v2 = right_array[ right_section-1 ];

 int height = v2->y - v1->y;
 right_section_height = height;
 right_x = v1->x;
 right_u = v1->u;
 right_v = v1->v;
 right_R = v1->R;
 right_G = v1->G;
 right_B = v1->B;

 if(height == 0) 
  {
   return 0;
  }
 delta_right_x = (v2->x - v1->x) / height;
 delta_right_u = (v2->u - v1->u) / height;
 delta_right_v = (v2->v - v1->v) / height;
 delta_right_R = (v2->R - v1->R) / height;
 delta_right_G = (v2->G - v1->G) / height;
 delta_right_B = (v2->B - v1->B) / height;

 return height;
}

////////////////////////////////////////////////////////////////////////

__inline int LeftSection_GT4(void)
{
 soft_vertex * v1 = left_array[ left_section ];
 soft_vertex * v2 = left_array[ left_section-1 ];

 int height = v2->y - v1->y;
 left_section_height = height;
 left_x = v1->x;
 left_u = v1->u;
 left_v = v1->v;
 left_R = v1->R;
 left_G = v1->G;
 left_B = v1->B;

 if(height == 0) 
  {
   return 0;
  }
 delta_left_x = (v2->x - v1->x) / height;
 delta_left_u = (v2->u - v1->u) / height;
 delta_left_v = (v2->v - v1->v) / height;
 delta_left_R = (v2->R - v1->R) / height;
 delta_left_G = (v2->G - v1->G) / height;
 delta_left_B = (v2->B - v1->B) / height;

 return height;  
}

////////////////////////////////////////////////////////////////////////

__inline BOOL NextRow_GT4(void)
{
 if(--left_section_height<=0) 
  {
   if(--left_section > 0) 
    while(LeftSection_GT4()<=0) 
     {
      if(--left_section  <= 0) break;
     }
  }
 else
  {
   left_x += delta_left_x;
   left_u += delta_left_u;
   left_v += delta_left_v;
   left_R += delta_left_R;
   left_G += delta_left_G;
   left_B += delta_left_B;
  }

 if(--right_section_height<=0) 
  {
   if(--right_section > 0) 
    while(RightSection_GT4()<=0) 
     {
      if(--right_section<=0) break;
     }
  }
 else
  {
   right_x += delta_right_x;
   right_u += delta_right_u;
   right_v += delta_right_v;
   right_R += delta_right_R;
   right_G += delta_right_G;
   right_B += delta_right_B;
  }
 return FALSE;
}

////////////////////////////////////////////////////////////////////////

__inline BOOL SetupSections_GT4(short x1, short y1, short x2, short y2, short x3, short y3, short x4, short y4, short tx1, short ty1, short tx2, short ty2, short tx3, short ty3, short tx4, short ty4,int32_t rgb1,int32_t rgb2,int32_t rgb3,int32_t rgb4)
{
 soft_vertex * v1, * v2, * v3, * v4;
 int height,width,longest1,longest2;

 v1 = vtx;   v1->x=x1<<16;v1->y=y1;
 v1->u=tx1<<16;v1->v=ty1<<16;
 v1->R=(rgb1) & 0x00ff0000;
 v1->G=(rgb1<<8) & 0x00ff0000;
 v1->B=(rgb1<<16) & 0x00ff0000;

 v2 = vtx+1; v2->x=x2<<16;v2->y=y2;
 v2->u=tx2<<16;v2->v=ty2<<16;
 v2->R=(rgb2) & 0x00ff0000;
 v2->G=(rgb2<<8) & 0x00ff0000;
 v2->B=(rgb2<<16) & 0x00ff0000;
             
 v3 = vtx+2; v3->x=x3<<16;v3->y=y3;
 v3->u=tx3<<16;v3->v=ty3<<16;
 v3->R=(rgb3) & 0x00ff0000;
 v3->G=(rgb3<<8) & 0x00ff0000;
 v3->B=(rgb3<<16) & 0x00ff0000;

 v4 = vtx+3; v4->x=x4<<16;v4->y=y4;
 v4->u=tx4<<16;v4->v=ty4<<16;
 v4->R=(rgb4) & 0x00ff0000;
 v4->G=(rgb4<<8) & 0x00ff0000;
 v4->B=(rgb4<<16) & 0x00ff0000;

 if(v1->y > v2->y) { soft_vertex * v = v1; v1 = v2; v2 = v; }
 if(v1->y > v3->y) { soft_vertex * v = v1; v1 = v3; v3 = v; }
 if(v1->y > v4->y) { soft_vertex * v = v1; v1 = v4; v4 = v; }
 if(v2->y > v3->y) { soft_vertex * v = v2; v2 = v3; v3 = v; }
 if(v2->y > v4->y) { soft_vertex * v = v2; v2 = v4; v4 = v; }
 if(v3->y > v4->y) { soft_vertex * v = v3; v3 = v4; v4 = v; }

 height = v4->y - v1->y; if(height == 0) height =1;
 width  = (v4->x - v1->x)>>16;
 longest1 = (((v2->y - v1->y) << 16) / height) * width + (v1->x - v2->x);
 longest2 = (((v3->y - v1->y) << 16) / height) * width + (v1->x - v3->x);

 if(longest1 < 0)                                      // 2 is right
  {
   if(longest2 < 0)                                    // 3 is right
    {
     left_array[0]  = v4;
     left_array[1]  = v1;
     left_section   = 1;

     height = v3->y - v1->y; if(height == 0) height=1;
     longest1 = (((v2->y - v1->y) << 16) / height) * ((v3->x - v1->x)>>16) + (v1->x - v2->x);
     if(longest1 >= 0)
      {
       right_array[0] = v4;                     //  1
       right_array[1] = v3;                     //     3
       right_array[2] = v1;                     //  4
       right_section  = 2;    
      }
     else
      {
       height = v4->y - v2->y; if(height == 0) height=1;
       longest1 = (((v3->y - v2->y) << 16) / height) * ((v4->x - v2->x)>>16) + (v2->x - v3->x);
       if(longest1 >= 0)
        {
         right_array[0] = v4;                    //  1
         right_array[1] = v2;                    //     2
         right_array[2] = v1;                    //  4
         right_section  = 2;    
        }
       else
        {
         right_array[0] = v4;                    //  1
         right_array[1] = v3;                    //     2
         right_array[2] = v2;                    //     3
         right_array[3] = v1;                    //  4
         right_section  = 3;    
        }
      }
    }
   else                                            
    {
     left_array[0]  = v4;
     left_array[1]  = v3;                         //    1
     left_array[2]  = v1;                         //      2
     left_section   = 2;                          //  3
     right_array[0] = v4;                         //    4
     right_array[1] = v2;
     right_array[2] = v1;
     right_section  = 2;
    }
  }
 else
  {
   if(longest2 < 0)             
    {
     left_array[0]  = v4;                          //    1
     left_array[1]  = v2;                          //  2
     left_array[2]  = v1;                          //      3
     left_section   = 2;                           //    4
     right_array[0] = v4;
     right_array[1] = v3;
     right_array[2] = v1;
     right_section  = 2;
    }
   else                         
    {
     right_array[0] = v4;
     right_array[1] = v1;
     right_section  = 1;

     height = v3->y - v1->y; if(height == 0) height=1;
     longest1 = (((v2->y - v1->y) << 16) / height) * ((v3->x - v1->x)>>16) + (v1->x - v2->x);
     if(longest1<0)
      {
       left_array[0]  = v4;                        //    1
       left_array[1]  = v3;                        //  3
       left_array[2]  = v1;                        //    4
       left_section   = 2;    
      }
     else
      {
       height = v4->y - v2->y; if(height == 0) height=1;
       longest1 = (((v3->y - v2->y) << 16) / height) * ((v4->x - v2->x)>>16) + (v2->x - v3->x);
       if(longest1<0)
        {
         left_array[0]  = v4;                      //    1
         left_array[1]  = v2;                      //  2
         left_array[2]  = v1;                      //    4
         left_section   = 2;    
        }
       else
        {
         left_array[0]  = v4;                      //    1
         left_array[1]  = v3;                      //  2
         left_array[2]  = v2;                      //  3
         left_array[3]  = v1;                      //     4
         left_section   = 3;    
        }
      }
    }
  }

 while(LeftSection_GT4()<=0) 
  {
   if(--left_section  <= 0) break;
  }

 while(RightSection_GT4()<=0) 
  {
   if(--right_section <= 0) break;
  }

 Ymin=v1->y;
 Ymax=min(v4->y-1,drawH);

 return TRUE;
}

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
// POLY FUNCS
////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////
// POLY 3/4 FLAT SHADED
////////////////////////////////////////////////////////////////////////

__inline void drawPoly3Fi(short x1,short y1,short x2,short y2,short x3,short y3,int32_t rgb)
{
 int i,j,xmin,xmax,ymin,ymax;
 unsigned short color;uint32_t lcolor;

 if(x1>drawW && x2>drawW && x3>drawW) return;
 if(y1>drawH && y2>drawH && y3>drawH) return;
 if(x1<drawX && x2<drawX && x3<drawX) return;
 if(y1<drawY && y2<drawY && y3<drawY) return;
 if(drawY>=drawH) return;
 if(drawX>=drawW) return; 

 if(!SetupSections_F(x1,y1,x2,y2,x3,y3)) return;

 ymax=Ymax;

 color = ((rgb & 0x00f80000)>>9) | ((rgb & 0x0000f800)>>6) | ((rgb & 0x000000f8)>>3);
 lcolor=lSetMask|(((uint32_t)(color))<<16)|color;

 for(ymin=Ymin;ymin<drawY;ymin++)
  if(NextRow_F()) return;

#ifdef FASTSOLID

 if(!bCheckMask && !DrawSemiTrans)
  {
   color |=sSetMask;
   for (i=ymin;i<=ymax;i++)
    {
     xmin=left_x >> 16;      if(drawX>xmin) xmin=drawX;
     xmax=(right_x >> 16)-1; if(drawW<xmax) xmax=drawW;

     for(j=xmin;j<xmax;j+=2) 
      {
       PUTLE32(((uint32_t *)&psxVuw[(i<<10)+j]), lcolor);
      }
     if(j==xmax) PUTLE16(&psxVuw[(i<<10)+j], color);

     if(NextRow_F()) return;
    }
   return;
  }

#endif

 for (i=ymin;i<=ymax;i++)
  {
   xmin=left_x >> 16;      if(drawX>xmin) xmin=drawX;
   xmax=(right_x >> 16)-1; if(drawW<xmax) xmax=drawW;

   for(j=xmin;j<xmax;j+=2) 
    {
     GetShadeTransCol32((uint32_t *)&psxVuw[(i<<10)+j],lcolor);
    }
   if(j==xmax)
    GetShadeTransCol(&psxVuw[(i<<10)+j],color);

   if(NextRow_F()) return;
  }
}

////////////////////////////////////////////////////////////////////////

void drawPoly3F(int32_t rgb)
{
 drawPoly3Fi(lx0,ly0,lx1,ly1,lx2,ly2,rgb);
}

#ifdef POLYQUAD3FS

void drawPoly4F_TRI(int32_t rgb)
{
 drawPoly3Fi(lx1,ly1,lx3,ly3,lx2,ly2,rgb);
 drawPoly3Fi(lx0,ly0,lx1,ly1,lx2,ly2,rgb);
}

#endif

// more exact:

void drawPoly4F(int32_t rgb)
{
 int i,j,xmin,xmax,ymin,ymax;
 unsigned short color;uint32_t lcolor;
 
 if(lx0>drawW && lx1>drawW && lx2>drawW && lx3>drawW) return;
 if(ly0>drawH && ly1>drawH && ly2>drawH && ly3>drawH) return;
 if(lx0<drawX && lx1<drawX && lx2<drawX && lx3<drawX) return;
 if(ly0<drawY && ly1<drawY && ly2<drawY && ly3<drawY) return;
 if(drawY>=drawH) return;
 if(drawX>=drawW) return; 

 if(!SetupSections_F4(lx0,ly0,lx1,ly1,lx2,ly2,lx3,ly3)) return;

 ymax=Ymax;

 for(ymin=Ymin;ymin<drawY;ymin++)
  if(NextRow_F4()) return;

 color = ((rgb & 0x00f80000)>>9) | ((rgb & 0x0000f800)>>6) | ((rgb & 0x000000f8)>>3);
 lcolor= lSetMask|(((uint32_t)(color))<<16)|color;

#ifdef FASTSOLID

 if(!bCheckMask && !DrawSemiTrans)
  {
   color |=sSetMask;
   for (i=ymin;i<=ymax;i++)
    {
     xmin=left_x >> 16;      if(drawX>xmin) xmin=drawX;
     xmax=(right_x >> 16)-1; if(drawW<xmax) xmax=drawW;

     for(j=xmin;j<xmax;j+=2) 
      {
       PUTLE32(((uint32_t *)&psxVuw[(i<<10)+j]), lcolor);
      }
     if(j==xmax) PUTLE16(&psxVuw[(i<<10)+j], color);

     if(NextRow_F4()) return;
    }
   return;
  }                                                        

#endif

 for (i=ymin;i<=ymax;i++)
  {
   xmin=left_x >> 16;      if(drawX>xmin) xmin=drawX;
   xmax=(right_x >> 16)-1; if(drawW<xmax) xmax=drawW;

   for(j=xmin;j<xmax;j+=2) 
    {
     GetShadeTransCol32((uint32_t *)&psxVuw[(i<<10)+j],lcolor);
    }
   if(j==xmax) GetShadeTransCol(&psxVuw[(i<<10)+j],color);

   if(NextRow_F4()) return;
  }
}

////////////////////////////////////////////////////////////////////////
// POLY 3/4 F-SHADED TEX PAL 4
////////////////////////////////////////////////////////////////////////

void drawPoly3TEx4(short x1, short y1, short x2, short y2, short x3, short y3, short tx1, short ty1, short tx2, short ty2, short tx3, short ty3,short clX, short clY)
{
 int i,j,xmin,xmax,ymin,ymax;
 int32_t difX, difY,difX2, difY2;
 int32_t posX,posY,YAdjust,XAdjust;
 int32_t clutP;
 short tC1,tC2;
 
 if(x1>drawW && x2>drawW && x3>drawW) return;
 if(y1>drawH && y2>drawH && y3>drawH) return;
 if(x1<drawX && x2<drawX && x3<drawX) return;
 if(y1<drawY && y2<drawY && y3<drawY) return;
 if(drawY>=drawH) return;
 if(drawX>=drawW) return; 

 if(!SetupSections_FT(x1,y1,x2,y2,x3,y3,tx1,ty1,tx2,ty2,tx3,ty3)) return;

 ymax=Ymax;

 for(ymin=Ymin;ymin<drawY;ymin++)
  if(NextRow_FT()) return;

 clutP=(clY<<10)+clX;

 YAdjust=((GlobalTextAddrY)<<11)+(GlobalTextAddrX<<1);

 difX=delta_right_u;difX2=difX<<1;
 difY=delta_right_v;difY2=difY<<1;

#ifdef FASTSOLID

 if(!bCheckMask && !DrawSemiTrans)
  {
   for (i=ymin;i<=ymax;i++)
    {
     xmin=(left_x >> 16);
     xmax=(right_x >> 16)-1; //!!!!!!!!!!!!!!!!
     if(drawW<xmax) xmax=drawW;

     if(xmax>=xmin)
      {
       posX=left_u;
       posY=left_v;

       if(xmin<drawX)
        {j=drawX-xmin;xmin=drawX;posX+=j*difX;posY+=j*difY;}

       for(j=xmin;j<xmax;j+=2)
        {
         XAdjust=(posX>>16);
         tC1 = psxVub[((posY>>5)&(int32_t)0xFFFFF800)+YAdjust+(XAdjust>>1)];
         tC1=(tC1>>((XAdjust&1)<<2))&0xf;
         XAdjust=((posX+difX)>>16);
         tC2 = psxVub[(((posY+difY)>>5)&(int32_t)0xFFFFF800)+YAdjust+
                    (XAdjust>>1)];
         tC2=(tC2>>((XAdjust&1)<<2))&0xf;

         GetTextureTransColG32_S((uint32_t *)&psxVuw[(i<<10)+j],
             GETLE16(&psxVuw[clutP+tC1])|
             ((int32_t)GETLE16(&psxVuw[clutP+tC2]))<<16);

         posX+=difX2;
         posY+=difY2;
        }
       if(j==xmax)
        {
         XAdjust=(posX>>16);
         tC1 = psxVub[((posY>>5)&(int32_t)0xFFFFF800)+YAdjust+
                      (XAdjust>>1)];
         tC1=(tC1>>((XAdjust&1)<<2))&0xf;
         GetTextureTransColG_S(&psxVuw[(i<<10)+j],GETLE16(&psxVuw[clutP+tC1]));
        }
      }
     if(NextRow_FT()) 
      {
       return;
      }
    }
   return;
  }

#endif

 for (i=ymin;i<=ymax;i++)
  {
   xmin=(left_x >> 16);
   xmax=(right_x >> 16)-1; //!!!!!!!!!!!!!!!!!!
   if(drawW<xmax) xmax=drawW;

   if(xmax>=xmin)
    {
     posX=left_u;
     posY=left_v;

     if(xmin<drawX)
      {j=drawX-xmin;xmin=drawX;posX+=j*difX;posY+=j*difY;}

     for(j=xmin;j<xmax;j+=2)
      {
       XAdjust=(posX>>16);
       tC1 = psxVub[((posY>>5)&(int32_t)0xFFFFF800)+YAdjust+(XAdjust>>1)];
       tC1=(tC1>>((XAdjust&1)<<2))&0xf;
       XAdjust=((posX+difX)>>16);
       tC2 = psxVub[(((posY+difY)>>5)&(int32_t)0xFFFFF800)+YAdjust+
                    (XAdjust>>1)];
       tC2=(tC2>>((XAdjust&1)<<2))&0xf;

       GetTextureTransColG32((uint32_t *)&psxVuw[(i<<10)+j],
           GETLE16(&psxVuw[clutP+tC1])|
           ((int32_t)GETLE16(&psxVuw[clutP+tC2]))<<16);

       posX+=difX2;
       posY+=difY2;
      }
     if(j==xmax)
      {
       XAdjust=(posX>>16);
       tC1 = psxVub[((posY>>5)&(int32_t)0xFFFFF800)+YAdjust+
                    (XAdjust>>1)];
       tC1=(tC1>>((XAdjust&1)<<2))&0xf;
       GetTextureTransColG(&psxVuw[(i<<10)+j],GETLE16(&psxVuw[clutP+tC1]));
      }
    }
   if(NextRow_FT()) 
    {
     return;
    }
  }
}

////////////////////////////////////////////////////////////////////////

void drawPoly3TEx4_IL(short x1, short y1, short x2, short y2, short x3, short y3, short tx1, short ty1, short tx2, short ty2, short tx3, short ty3,short clX, short clY)
{
 int i,j,xmin,xmax,ymin,ymax,n_xi,n_yi,TXV;
 int32_t difX, difY,difX2, difY2;
 int32_t posX,posY,YAdjust,XAdjust;
 int32_t clutP;
 short tC1,tC2;
 
 if(x1>drawW && x2>drawW && x3>drawW) return;
 if(y1>drawH && y2>drawH && y3>drawH) return;
 if(x1<drawX && x2<drawX && x3<drawX) return;
 if(y1<drawY && y2<drawY && y3<drawY) return;
 if(drawY>=drawH) return;
 if(drawX>=drawW) return; 

 if(!SetupSections_FT(x1,y1,x2,y2,x3,y3,tx1,ty1,tx2,ty2,tx3,ty3)) return;

 ymax=Ymax;

 for(ymin=Ymin;ymin<drawY;ymin++)
  if(NextRow_FT()) return;

 clutP=(clY<<10)+clX;

 YAdjust=(GlobalTextAddrY<<10)+GlobalTextAddrX;

 difX=delta_right_u;difX2=difX<<1;
 difY=delta_right_v;difY2=difY<<1;

#ifdef FASTSOLID

 if(!bCheckMask && !DrawSemiTrans)
  {
   for (i=ymin;i<=ymax;i++)
    {
     xmin=(left_x >> 16);
     xmax=(right_x >> 16)-1;
     if(drawW<xmax) xmax=drawW;

     if(xmax>=xmin)
      {
       posX=left_u;
       posY=left_v;

       if(xmin<drawX)
        {j=drawX-xmin;xmin=drawX;posX+=j*difX;posY+=j*difY;}

       for(j=xmin;j<xmax;j+=2)
        {
         XAdjust=(posX>>16);

         TXV=posY>>16;
         n_xi = ( ( XAdjust >> 2 ) & ~0x3c ) + ( ( TXV << 2 ) & 0x3c );
         n_yi = ( TXV & ~0xf ) + ( ( XAdjust >> 4 ) & 0xf );

         tC1= (GETLE16(&psxVuw[(n_yi<<10)+YAdjust+n_xi]) >> ((XAdjust & 0x03)<<2)) & 0x0f ;

         XAdjust=((posX+difX)>>16);

         TXV=(posY+difY)>>16;
         n_xi = ( ( XAdjust >> 2 ) & ~0x3c ) + ( ( TXV << 2 ) & 0x3c );
         n_yi = ( TXV & ~0xf ) + ( ( XAdjust >> 4 ) & 0xf );

         tC2= (GETLE16(&psxVuw[(n_yi<<10)+YAdjust+n_xi]) >> ((XAdjust & 0x03)<<2)) & 0x0f ;

         GetTextureTransColG32_S((uint32_t *)&psxVuw[(i<<10)+j],
             GETLE16(&psxVuw[clutP+tC1])|
             ((int32_t)GETLE16(&psxVuw[clutP+tC2]))<<16);

         posX+=difX2;
         posY+=difY2;
        }
       if(j==xmax)
        {
         XAdjust=(posX>>16);

         TXV=posY>>16;
         n_xi = ( ( XAdjust >> 2 ) & ~0x3c ) + ( ( TXV << 2 ) & 0x3c );
         n_yi = ( TXV & ~0xf ) + ( ( XAdjust >> 4 ) & 0xf );

         tC1= (GETLE16(&psxVuw[(n_yi<<10)+YAdjust+n_xi]) >> ((XAdjust & 0x03)<<2)) & 0x0f ;

         GetTextureTransColG_S(&psxVuw[(i<<10)+j],GETLE16(&psxVuw[clutP+tC1]));
        }
      }
     if(NextRow_FT()) 
      {
       return;
      }
    }
   return;
  }

#endif

 for (i=ymin;i<=ymax;i++)
  {
   xmin=(left_x >> 16);
   xmax=(right_x >> 16)-1; //!!!!!!!!!!!!!!!!!!
   if(drawW<xmax) xmax=drawW;

   if(xmax>=xmin)
    {
     posX=left_u;
     posY=left_v;

     if(xmin<drawX)
      {j=drawX-xmin;xmin=drawX;posX+=j*difX;posY+=j*difY;}

     for(j=xmin;j<xmax;j+=2)
      {
       XAdjust=(posX>>16);

       TXV=posY>>16;
       n_xi = ( ( XAdjust >> 2 ) & ~0x3c ) + ( ( TXV << 2 ) & 0x3c );
       n_yi = ( TXV & ~0xf ) + ( ( XAdjust >> 4 ) & 0xf );

       tC1= (GETLE16(&psxVuw[(n_yi<<10)+YAdjust+n_xi]) >> ((XAdjust & 0x03)<<2)) & 0x0f ;

       XAdjust=((posX+difX)>>16);

       TXV=(posY+difY)>>16;
       n_xi = ( ( XAdjust >> 2 ) & ~0x3c ) + ( ( TXV << 2 ) & 0x3c );
       n_yi = ( TXV & ~0xf ) + ( ( XAdjust >> 4 ) & 0xf );

       tC2= (GETLE16(&psxVuw[(n_yi<<10)+YAdjust+n_xi]) >> ((XAdjust & 0x03)<<2)) & 0x0f ;

       GetTextureTransColG32((uint32_t *)&psxVuw[(i<<10)+j],
           GETLE16(&psxVuw[clutP+tC1])|
           ((int32_t)GETLE16(&psxVuw[clutP+tC2]))<<16);

       posX+=difX2;
       posY+=difY2;
      }
     if(j==xmax)
      {
       XAdjust=(posX>>16);

       TXV=posY>>16;
       n_xi = ( ( XAdjust >> 2 ) & ~0x3c ) + ( ( TXV << 2 ) & 0x3c );
       n_yi = ( TXV & ~0xf ) + ( ( XAdjust >> 4 ) & 0xf );

       tC1= (GETLE16(&psxVuw[(n_yi<<10)+YAdjust+n_xi]) >> ((XAdjust & 0x03)<<2)) & 0x0f ;

       GetTextureTransColG(&psxVuw[(i<<10)+j],GETLE16(&psxVuw[clutP+tC1]));
      }
    }
   if(NextRow_FT()) 
    {
     return;
    }
  }
}

////////////////////////////////////////////////////////////////////////

void drawPoly3TEx4_TW(short x1, short y1, short x2, short y2, short x3, short y3, short tx1, short ty1, short tx2, short ty2, short tx3, short ty3,short clX, short clY)
{
 int i,j,xmin,xmax,ymin,ymax;
 int32_t difX, difY,difX2, difY2;
 int32_t posX,posY,YAdjust,XAdjust;
 int32_t clutP;
 short tC1,tC2;
 
 if(x1>drawW && x2>drawW && x3>drawW) return;
 if(y1>drawH && y2>drawH && y3>drawH) return;
 if(x1<drawX && x2<drawX && x3<drawX) return;
 if(y1<drawY && y2<drawY && y3<drawY) return;
 if(drawY>=drawH) return;
 if(drawX>=drawW) return; 

 if(!SetupSections_FT(x1,y1,x2,y2,x3,y3,tx1,ty1,tx2,ty2,tx3,ty3)) return;

 ymax=Ymax;

 for(ymin=Ymin;ymin<drawY;ymin++)
  if(NextRow_FT()) return;

 clutP=(clY<<10)+clX;

 YAdjust=((GlobalTextAddrY)<<11)+(GlobalTextAddrX<<1);
 YAdjust+=(TWin.Position.y0<<11)+(TWin.Position.x0>>1);

 difX=delta_right_u;difX2=difX<<1;
 difY=delta_right_v;difY2=difY<<1;

#ifdef FASTSOLID

 if(!bCheckMask && !DrawSemiTrans)
  {
   for (i=ymin;i<=ymax;i++)
    {
     xmin=(left_x >> 16);
     xmax=(right_x >> 16);//-1; //!!!!!!!!!!!!!!!!
     if(xmax>xmin) xmax--;

     if(drawW<xmax) xmax=drawW;

     if(xmax>=xmin)
      {
       posX=left_u;
       posY=left_v;

       if(xmin<drawX)
        {j=drawX-xmin;xmin=drawX;posX+=j*difX;posY+=j*difY;}

       for(j=xmin;j<xmax;j+=2)
        {
         XAdjust=(posX>>16)%TWin.Position.x1;
         tC1 = psxVub[(((posY>>16)%TWin.Position.y1)<<11)+
                      YAdjust+(XAdjust>>1)];
         tC1=(tC1>>((XAdjust&1)<<2))&0xf;
         XAdjust=((posX+difX)>>16)%TWin.Position.x1;
         tC2 = psxVub[((((posY+difY)>>16)%TWin.Position.y1)<<11)+
                      YAdjust+(XAdjust>>1)];
         tC2=(tC2>>((XAdjust&1)<<2))&0xf;

         GetTextureTransColG32_S((uint32_t *)&psxVuw[(i<<10)+j],
             GETLE16(&psxVuw[clutP+tC1])|
             ((int32_t)GETLE16(&psxVuw[clutP+tC2]))<<16);

         posX+=difX2;
         posY+=difY2;
        }
       if(j==xmax)
        {
         XAdjust=(posX>>16)%TWin.Position.x1;
         tC1 = psxVub[(((posY>>16)%TWin.Position.y1)<<11)+
                      YAdjust+(XAdjust>>1)];
         tC1=(tC1>>((XAdjust&1)<<2))&0xf;
         GetTextureTransColG_S(&psxVuw[(i<<10)+j],GETLE16(&psxVuw[clutP+tC1]));
        }
      }
     if(NextRow_FT()) 
      {
       return;
      }
    }
   return;
  }

#endif

 for (i=ymin;i<=ymax;i++)
  {
   xmin=(left_x >> 16);
   xmax=(right_x >> 16)-1; //!!!!!!!!!!!!!!!!!!
   if(drawW<xmax) xmax=drawW;

   if(xmax>=xmin)
    {
     posX=left_u;
     posY=left_v;

     if(xmin<drawX)
      {j=drawX-xmin;xmin=drawX;posX+=j*difX;posY+=j*difY;}

     for(j=xmin;j<xmax;j+=2)
      {
       XAdjust=(posX>>16)%TWin.Position.x1;
       tC1 = psxVub[(((posY>>16)%TWin.Position.y1)<<11)+
                    YAdjust+(XAdjust>>1)];
       tC1=(tC1>>((XAdjust&1)<<2))&0xf;
       XAdjust=((posX+difX)>>16)%TWin.Position.x1;
       tC2 = psxVub[((((posY+difY)>>16)%TWin.Position.y1)<<11)+
                    YAdjust+(XAdjust>>1)];
       tC2=(tC2>>((XAdjust&1)<<2))&0xf;

       GetTextureTransColG32((uint32_t *)&psxVuw[(i<<10)+j],
           GETLE16(&psxVuw[clutP+tC1])|
           ((int32_t)GETLE16(&psxVuw[clutP+tC2]))<<16);

       posX+=difX2;
       posY+=difY2;
      }
     if(j==xmax)
      {
       XAdjust=(posX>>16)%TWin.Position.x1;
       tC1 = psxVub[(((posY>>16)%TWin.Position.y1)<<11)+
                    YAdjust+(XAdjust>>1)];
       tC1=(tC1>>((XAdjust&1)<<2))&0xf;
       GetTextureTransColG(&psxVuw[(i<<10)+j],GETLE16(&psxVuw[clutP+tC1]));
      }
    }
   if(NextRow_FT()) 
    {
     return;
    }
  }
}

////////////////////////////////////////////////////////////////////////

#ifdef POLYQUAD3

void drawPoly4TEx4_TRI(short x1, short y1, short x2, short y2, short x3, short y3, short x4, short y4, short tx1, short ty1, short tx2, short ty2, short tx3, short ty3, short tx4, short ty4,short clX, short clY)
{
 drawPoly3TEx4(x2,y2,x3,y3,x4,y4,
               tx2,ty2,tx3,ty3,tx4,ty4,
               clX,clY);
 drawPoly3TEx4(x1,y1,x2,y2,x4,y4,
               tx1,ty1,tx2,ty2,tx4,ty4,
               clX,clY);
}

#endif

// more exact:

void drawPoly4TEx4(short x1, short y1, short x2, short y2, short x3, short y3, short x4, short y4, short tx1, short ty1, short tx2, short ty2, short tx3, short ty3, short tx4, short ty4,short clX, short clY)
{
 int32_t num; 
 int32_t i,j,xmin,xmax,ymin,ymax;
 int32_t difX, difY, difX2, difY2;
 int32_t posX,posY,YAdjust,clutP,XAdjust;
 short tC1,tC2;

 if(x1>drawW && x2>drawW && x3>drawW && x4>drawW) return;
 if(y1>drawH && y2>drawH && y3>drawH && y4>drawH) return;
 if(x1<drawX && x2<drawX && x3<drawX && x4<drawX) return;
 if(y1<drawY && y2<drawY && y3<drawY && y4<drawY) return;
 if(drawY>=drawH) return;
 if(drawX>=drawW) return; 

 if(!SetupSections_FT4(x1,y1,x2,y2,x3,y3,x4,y4,tx1,ty1,tx2,ty2,tx3,ty3,tx4,ty4)) return;

 ymax=Ymax;

 for(ymin=Ymin;ymin<drawY;ymin++)
  if(NextRow_FT4()) return;

 clutP=(clY<<10)+clX;

 YAdjust=((GlobalTextAddrY)<<11)+(GlobalTextAddrX<<1);

#ifdef FASTSOLID

 if(!bCheckMask && !DrawSemiTrans)
  {
   for (i=ymin;i<=ymax;i++)
    {
     xmin=(left_x >> 16);
     xmax=(right_x >> 16);

     if(xmax>=xmin)
      {
       posX=left_u;
       posY=left_v;

       num=(xmax-xmin);
       if(num==0) num=1;
       difX=(right_u-posX)/num;
       difY=(right_v-posY)/num;
       difX2=difX<<1;
       difY2=difY<<1;

       if(xmin<drawX)
        {j=drawX-xmin;xmin=drawX;posX+=j*difX;posY+=j*difY;}
       xmax--;if(drawW<xmax) xmax=drawW;

       for(j=xmin;j<xmax;j+=2)
        {
         XAdjust=(posX>>16);
         tC1 = psxVub[((posY>>5)&(int32_t)0xFFFFF800)+YAdjust+(XAdjust>>1)];
         tC1=(tC1>>((XAdjust&1)<<2))&0xf;
         XAdjust=((posX+difX)>>16);
         tC2 = psxVub[(((posY+difY)>>5)&(int32_t)0xFFFFF800)+YAdjust+
                       (XAdjust>>1)];
         tC2=(tC2>>((XAdjust&1)<<2))&0xf;

         GetTextureTransColG32_S((uint32_t *)&psxVuw[(i<<10)+j],
              GETLE16(&psxVuw[clutP+tC1])|
              ((int32_t)GETLE16(&psxVuw[clutP+tC2]))<<16);
         posX+=difX2;
         posY+=difY2;
        }
       if(j==xmax)
        {
         XAdjust=(posX>>16);
         tC1 = psxVub[((posY>>5)&(int32_t)0xFFFFF800)+YAdjust+
                      (XAdjust>>1)];
         tC1=(tC1>>((XAdjust&1)<<2))&0xf;
         GetTextureTransColG_S(&psxVuw[(i<<10)+j],GETLE16(&psxVuw[clutP+tC1]));
        }

      }
     if(NextRow_FT4()) return;
    }
   return;
  }

#endif

 for (i=ymin;i<=ymax;i++)
  {
   xmin=(left_x >> 16);
   xmax=(right_x >> 16);

   if(xmax>=xmin)
    {
     posX=left_u;
     posY=left_v;

     num=(xmax-xmin);
     if(num==0) num=1;
     difX=(right_u-posX)/num;
     difY=(right_v-posY)/num;
     difX2=difX<<1;
     difY2=difY<<1;

     if(xmin<drawX)
      {j=drawX-xmin;xmin=drawX;posX+=j*difX;posY+=j*difY;}
     xmax--;if(drawW<xmax) xmax=drawW;

     for(j=xmin;j<xmax;j+=2)
      {
       XAdjust=(posX>>16);
       tC1 = psxVub[((posY>>5)&(int32_t)0xFFFFF800)+YAdjust+(XAdjust>>1)];
       tC1=(tC1>>((XAdjust&1)<<2))&0xf;
       XAdjust=((posX+difX)>>16);
       tC2 = psxVub[(((posY+difY)>>5)&(int32_t)0xFFFFF800)+YAdjust+
                     (XAdjust>>1)];
       tC2=(tC2>>((XAdjust&1)<<2))&0xf;

       GetTextureTransColG32((uint32_t *)&psxVuw[(i<<10)+j],
            GETLE16(&psxVuw[clutP+tC1])|
            ((int32_t)GETLE16(&psxVuw[clutP+tC2]))<<16);
       posX+=difX2;
       posY+=difY2;
      }
     if(j==xmax)
      {
       XAdjust=(posX>>16);
       tC1 = psxVub[((posY>>5)&(int32_t)0xFFFFF800)+YAdjust+
                    (XAdjust>>1)];
       tC1=(tC1>>((XAdjust&1)<<2))&0xf;
       GetTextureTransColG(&psxVuw[(i<<10)+j],GETLE16(&psxVuw[clutP+tC1]));
      }
    }
   if(NextRow_FT4()) return;
  }
}

////////////////////////////////////////////////////////////////////////

void drawPoly4TEx4_IL(short x1, short y1, short x2, short y2, short x3, short y3, short x4, short y4, short tx1, short ty1, short tx2, short ty2, short tx3, short ty3, short tx4, short ty4,short clX, short clY)
{
 int32_t num; 
 int32_t i,j=0,xmin,xmax,ymin,ymax,n_xi,n_yi,TXV;
 int32_t difX, difY, difX2, difY2;
 int32_t posX=0,posY=0,YAdjust,clutP,XAdjust;
 short tC1,tC2;

 if(x1>drawW && x2>drawW && x3>drawW && x4>drawW) return;
 if(y1>drawH && y2>drawH && y3>drawH && y4>drawH) return;
 if(x1<drawX && x2<drawX && x3<drawX && x4<drawX) return;
 if(y1<drawY && y2<drawY && y3<drawY && y4<drawY) return;
 if(drawY>=drawH) return;
 if(drawX>=drawW) return; 

 if(!SetupSections_FT4(x1,y1,x2,y2,x3,y3,x4,y4,tx1,ty1,tx2,ty2,tx3,ty3,tx4,ty4)) return;

 ymax=Ymax;

 for(ymin=Ymin;ymin<drawY;ymin++)
  if(NextRow_FT4()) return;

 clutP=(clY<<10)+clX;

 YAdjust=((GlobalTextAddrY)<<10)+GlobalTextAddrX;

#ifdef FASTSOLID

 if(!bCheckMask && !DrawSemiTrans)
  {
   for (i=ymin;i<=ymax;i++)
    {
     xmin=(left_x >> 16);
     xmax=(right_x >> 16);

     if(xmax>=xmin)
      {
       posX=left_u;
       posY=left_v;

       num=(xmax-xmin);
       if(num==0) num=1;
       difX=(right_u-posX)/num;
       difY=(right_v-posY)/num;
       difX2=difX<<1;
       difY2=difY<<1;

       if(xmin<drawX)
        {j=drawX-xmin;xmin=drawX;posX+=j*difX;posY+=j*difY;}
       xmax--;if(drawW<xmax) xmax=drawW;

       for(j=xmin;j<xmax;j+=2)
        {
         XAdjust=(posX>>16);

         TXV=posY>>16;
         n_xi = ( ( XAdjust >> 2 ) & ~0x3c ) + ( ( TXV << 2 ) & 0x3c );
         n_yi = ( TXV & ~0xf ) + ( ( XAdjust >> 4 ) & 0xf );

         tC1= (GETLE16(&psxVuw[(n_yi<<10)+YAdjust+n_xi]) >> ((XAdjust & 0x03)<<2)) & 0x0f ;

         XAdjust=((posX+difX)>>16);

         TXV=(posY+difY)>>16;
         n_xi = ( ( XAdjust >> 2 ) & ~0x3c ) + ( ( TXV << 2 ) & 0x3c );
         n_yi = ( TXV & ~0xf ) + ( ( XAdjust >> 4 ) & 0xf );

         tC2= (GETLE16(&psxVuw[(n_yi<<10)+YAdjust+n_xi]) >> ((XAdjust & 0x03)<<2)) & 0x0f ;

         GetTextureTransColG32_S((uint32_t *)&psxVuw[(i<<10)+j],
              GETLE16(&psxVuw[clutP+tC1])|
              ((int32_t)GETLE16(&psxVuw[clutP+tC2]))<<16);
         posX+=difX2;
         posY+=difY2;
        }
         posX+=difX2;
         posY+=difY2;
        }

       if(j==xmax)
        {
         XAdjust=(posX>>16);
         TXV=posY>>16;
         n_xi = ( ( XAdjust >> 2 ) & ~0x3c ) + ( ( TXV << 2 ) & 0x3c );
         n_yi = ( TXV & ~0xf ) + ( ( XAdjust >> 4 ) & 0xf );

         tC1= (GETLE16(&psxVuw[(n_yi<<10)+YAdjust+n_xi]) >> ((XAdjust & 0x03)<<2)) & 0x0f ;

         GetTextureTransColG_S(&psxVuw[(i<<10)+j],GETLE16(&psxVuw[clutP+tC1]));
        }

      }
     if(NextRow_FT4()) return;
    }
#endif

 for (i=ymin;i<=ymax;i++)
  {
   xmin=(left_x >> 16);
   xmax=(right_x >> 16);

   if(xmax>=xmin)
    {
     posX=left_u;
     posY=left_v;

     num=(xmax-xmin);
     if(num==0) num=1;
     difX=(right_u-posX)/num;
     difY=(right_v-posY)/num;
     difX2=difX<<1;
     difY2=difY<<1;

     if(xmin<drawX)
      {j=drawX-xmin;xmin=drawX;posX+=j*difX;posY+=j*difY;}
     xmax--;if(drawW<xmax) xmax=drawW;

     for(j=xmin;j<xmax;j+=2)
      {
       XAdjust=(posX>>16);

       TXV=posY>>16;
       n_xi = ( ( XAdjust >> 2 ) & ~0x3c ) + ( ( TXV << 2 ) & 0x3c );
       n_yi = ( TXV & ~0xf ) + ( ( XAdjust >> 4 ) & 0xf );

       tC1= (GETLE16(&psxVuw[(n_yi<<10)+YAdjust+n_xi]) >> ((XAdjust & 0x03)<<2)) & 0x0f ;

       XAdjust=((posX+difX)>>16);

       TXV=(posY+difY)>>16;
       n_xi = ( ( XAdjust >> 2 ) & ~0x3c ) + ( ( TXV << 2 ) & 0x3c );
       n_yi = ( TXV & ~0xf ) + ( ( XAdjust >> 4 ) & 0xf );

       tC2= (GETLE16(&psxVuw[(n_yi<<10)+YAdjust+n_xi]) >> ((XAdjust & 0x03)<<2)) & 0x0f ;

       GetTextureTransColG32((uint32_t *)&psxVuw[(i<<10)+j],
            GETLE16(&psxVuw[clutP+tC1])|
            ((int32_t)GETLE16(&psxVuw[clutP+tC2]))<<16);
       posX+=difX2;
       posY+=difY2;
      }
     if(j==xmax)
      {
       XAdjust=(posX>>16);
       TXV=posY>>16;
       n_xi = ( ( XAdjust >> 2 ) & ~0x3c ) + ( ( TXV << 2 ) & 0x3c );
       n_yi = ( TXV & ~0xf ) + ( ( XAdjust >> 4 ) & 0xf );

       tC1= (GETLE16(&psxVuw[(n_yi<<10)+YAdjust+n_xi]) >> ((XAdjust & 0x03)<<2)) & 0x0f ;

       GetTextureTransColG(&psxVuw[(i<<10)+j],GETLE16(&psxVuw[clutP+tC1]));
      }
    }
   if(NextRow_FT4()) return;
  }
}

////////////////////////////////////////////////////////////////////////

void drawPoly4TEx4_TW(short x1, short y1, short x2, short y2, short x3, short y3, short x4, short y4, short tx1, short ty1, short tx2, short ty2, short tx3, short ty3, short tx4, short ty4,short clX, short clY)
{
 int32_t num; 
 int32_t i,j,xmin,xmax,ymin,ymax;
 int32_t difX, difY, difX2, difY2;
 int32_t posX,posY,YAdjust,clutP,XAdjust;
 short tC1,tC2;

 if(x1>drawW && x2>drawW && x3>drawW && x4>drawW) return;
 if(y1>drawH && y2>drawH && y3>drawH && y4>drawH) return;
 if(x1<drawX && x2<drawX && x3<drawX && x4<drawX) return;
 if(y1<drawY && y2<drawY && y3<drawY && y4<drawY) return;
 if(drawY>=drawH) return;
 if(drawX>=drawW) return; 

 if(!SetupSections_FT4(x1,y1,x2,y2,x3,y3,x4,y4,tx1,ty1,tx2,ty2,tx3,ty3,tx4,ty4)) return;

 ymax=Ymax;

 for(ymin=Ymin;ymin<drawY;ymin++)
  if(NextRow_FT4()) return;

 clutP=(clY<<10)+clX;

 YAdjust=((GlobalTextAddrY)<<11)+(GlobalTextAddrX<<1);
 YAdjust+=(TWin.Position.y0<<11)+(TWin.Position.x0>>1);

#ifdef FASTSOLID

 if(!bCheckMask && !DrawSemiTrans)
  {
   for (i=ymin;i<=ymax;i++)
    {
     xmin=(left_x >> 16);
     xmax=(right_x >> 16);

     if(xmax>=xmin)
      {
       posX=left_u;
       posY=left_v;

       num=(xmax-xmin);
       if(num==0) num=1;
       difX=(right_u-posX)/num;
       difY=(right_v-posY)/num;
       difX2=difX<<1;
       difY2=difY<<1;

       if(xmin<drawX)
        {j=drawX-xmin;xmin=drawX;posX+=j*difX;posY+=j*difY;}
       xmax--;if(drawW<xmax) xmax=drawW;

       for(j=xmin;j<xmax;j+=2)
        {
         XAdjust=(posX>>16)%TWin.Position.x1;
         tC1 = psxVub[(((posY>>16)%TWin.Position.y1)<<11)+
                      YAdjust+(XAdjust>>1)];
         tC1=(tC1>>((XAdjust&1)<<2))&0xf;
         XAdjust=((posX+difX)>>16)%TWin.Position.x1;
         tC2 = psxVub[((((posY+difY)>>16)%TWin.Position.y1)<<11)+
                      YAdjust+(XAdjust>>1)];
         tC2=(tC2>>((XAdjust&1)<<2))&0xf;

         GetTextureTransColG32_S((uint32_t *)&psxVuw[(i<<10)+j],
              GETLE16(&psxVuw[clutP+tC1])|
              ((int32_t)GETLE16(&psxVuw[clutP+tC2]))<<16);
         posX+=difX2;
         posY+=difY2;
        }
       if(j==xmax)
        {
         XAdjust=(posX>>16)%TWin.Position.x1;
         tC1 = psxVub[(((posY>>16)%TWin.Position.y1)<<11)+
                      YAdjust+(XAdjust>>1)];
         tC1=(tC1>>((XAdjust&1)<<2))&0xf;
         GetTextureTransColG_S(&psxVuw[(i<<10)+j],GETLE16(&psxVuw[clutP+tC1]));
        }
      }
     if(NextRow_FT4()) return;
    }
   return;
  }

#endif

 for (i=ymin;i<=ymax;i++)
  {
   xmin=(left_x >> 16);
   xmax=(right_x >> 16);

   if(xmax>=xmin)
    {
     posX=left_u;
     posY=left_v;

     num=(xmax-xmin);
     if(num==0) num=1;
     difX=(right_u-posX)/num;
     difY=(right_v-posY)/num;
     difX2=difX<<1;
     difY2=difY<<1;

     if(xmin<drawX)
      {j=drawX-xmin;xmin=drawX;posX+=j*difX;posY+=j*difY;}
     xmax--;if(drawW<xmax) xmax=drawW;

     for(j=xmin;j<xmax;j+=2)
      {
       XAdjust=(posX>>16)%TWin.Position.x1;
       tC1 = psxVub[(((posY>>16)%TWin.Position.y1)<<11)+
                    YAdjust+(XAdjust>>1)];
       tC1=(tC1>>((XAdjust&1)<<2))&0xf;
       XAdjust=((posX+difX)>>16)%TWin.Position.x1;
       tC2 = psxVub[((((posY+difY)>>16)%TWin.Position.y1)<<11)+
                    YAdjust+(XAdjust>>1)];
       tC2=(tC2>>((XAdjust&1)<<2))&0xf;

       GetTextureTransColG32((uint32_t *)&psxVuw[(i<<10)+j],
            GETLE16(&psxVuw[clutP+tC1])|
            ((int32_t)GETLE16(&psxVuw[clutP+tC2]))<<16);
       posX+=difX2;
       posY+=difY2;
      }
     if(j==xmax)
      {
       XAdjust=(posX>>16)%TWin.Position.x1;
       tC1 = psxVub[(((posY>>16)%TWin.Position.y1)<<11)+
                    YAdjust+(XAdjust>>1)];
       tC1=(tC1>>((XAdjust&1)<<2))&0xf;
       GetTextureTransColG(&psxVuw[(i<<10)+j],GETLE16(&psxVuw[clutP+tC1]));
      }
    }
   if(NextRow_FT4()) return;
  }
}

////////////////////////////////////////////////////////////////////////

void drawPoly4TEx4_TW_S(short x1, short y1, short x2, short y2, short x3, short y3, short x4, short y4, short tx1, short ty1, short tx2, short ty2, short tx3, short ty3, short tx4, short ty4,short clX, short clY)
{
 int32_t num; 
 int32_t i,j,xmin,xmax,ymin,ymax;
 int32_t difX, difY, difX2, difY2;
 int32_t posX,posY,YAdjust,clutP,XAdjust;
 short tC1,tC2;

 if(x1>drawW && x2>drawW && x3>drawW && x4>drawW) return;
 if(y1>drawH && y2>drawH && y3>drawH && y4>drawH) return;
 if(x1<drawX && x2<drawX && x3<drawX && x4<drawX) return;
 if(y1<drawY && y2<drawY && y3<drawY && y4<drawY) return;
 if(drawY>=drawH) return;
 if(drawX>=drawW) return; 

 if(!SetupSections_FT4(x1,y1,x2,y2,x3,y3,x4,y4,tx1,ty1,tx2,ty2,tx3,ty3,tx4,ty4)) return;

 ymax=Ymax;

 for(ymin=Ymin;ymin<drawY;ymin++)
  if(NextRow_FT4()) return;

 clutP=(clY<<10)+clX;

 YAdjust=((GlobalTextAddrY)<<11)+(GlobalTextAddrX<<1);
 YAdjust+=(TWin.Position.y0<<11)+(TWin.Position.x0>>1);

#ifdef FASTSOLID

 if(!bCheckMask && !DrawSemiTrans)
  {
   for (i=ymin;i<=ymax;i++)
    {
     xmin=(left_x >> 16);
     xmax=(right_x >> 16);

     if(xmax>=xmin)
      {
       posX=left_u;
       posY=left_v;

       num=(xmax-xmin);
       if(num==0) num=1;
       difX=(right_u-posX)/num;
       difY=(right_v-posY)/num;
       difX2=difX<<1;
       difY2=difY<<1;

       if(xmin<drawX)
        {j=drawX-xmin;xmin=drawX;posX+=j*difX;posY+=j*difY;}
       xmax--;if(drawW<xmax) xmax=drawW;

       for(j=xmin;j<xmax;j+=2)
        {
         XAdjust=(posX>>16)%TWin.Position.x1;
         tC1 = psxVub[(((posY>>16)%TWin.Position.y1)<<11)+
                      YAdjust+(XAdjust>>1)];
         tC1=(tC1>>((XAdjust&1)<<2))&0xf;
         XAdjust=((posX+difX)>>16)%TWin.Position.x1;
         tC2 = psxVub[((((posY+difY)>>16)%TWin.Position.y1)<<11)+
                      YAdjust+(XAdjust>>1)];
         tC2=(tC2>>((XAdjust&1)<<2))&0xf;

         GetTextureTransColG32_S((uint32_t *)&psxVuw[(i<<10)+j],
              GETLE16(&psxVuw[clutP+tC1])|
              ((int32_t)GETLE16(&psxVuw[clutP+tC2]))<<16);
         posX+=difX2;
         posY+=difY2;
        }
       if(j==xmax)
        {
         XAdjust=(posX>>16)%TWin.Position.x1;
         tC1 = psxVub[(((posY>>16)%TWin.Position.y1)<<11)+
                      YAdjust+(XAdjust>>1)];
         tC1=(tC1>>((XAdjust&1)<<2))&0xf;
         GetTextureTransColG_S(&psxVuw[(i<<10)+j],GETLE16(&psxVuw[clutP+tC1]));
        }
      }
     if(NextRow_FT4()) return;
    }
   return;
  }

#endif

 for (i=ymin;i<=ymax;i++)
  {
   xmin=(left_x >> 16);
   xmax=(right_x >> 16);

   if(xmax>=xmin)
    {
     posX=left_u;
     posY=left_v;

     num=(xmax-xmin);
     if(num==0) num=1;
     difX=(right_u-posX)/num;
     difY=(right_v-posY)/num;
     difX2=difX<<1;
     difY2=difY<<1;

     if(xmin<drawX)
      {j=drawX-xmin;xmin=drawX;posX+=j*difX;posY+=j*difY;}
     xmax--;if(drawW<xmax) xmax=drawW;

     for(j=xmin;j<xmax;j+=2)
      {
       XAdjust=(posX>>16)%TWin.Position.x1;
       tC1 = psxVub[(((posY>>16)%TWin.Position.y1)<<11)+
                    YAdjust+(XAdjust>>1)];
       tC1=(tC1>>((XAdjust&1)<<2))&0xf;
       XAdjust=((posX+difX)>>16)%TWin.Position.x1;
       tC2 = psxVub[((((posY+difY)>>16)%TWin.Position.y1)<<11)+
                    YAdjust+(XAdjust>>1)];
       tC2=(tC2>>((XAdjust&1)<<2))&0xf;

       GetTextureTransColG32_SPR((uint32_t *)&psxVuw[(i<<10)+j],
            GETLE16(&psxVuw[clutP+tC1])|
            ((int32_t)GETLE16(&psxVuw[clutP+tC2]))<<16);
       posX+=difX2;
       posY+=difY2;
      }
     if(j==xmax)
      {
       XAdjust=(posX>>16)%TWin.Position.x1;
       tC1 = psxVub[(((posY>>16)%TWin.Position.y1)<<11)+
                    YAdjust+(XAdjust>>1)];
       tC1=(tC1>>((XAdjust&1)<<2))&0xf;
       GetTextureTransColG_SPR(&psxVuw[(i<<10)+j],GETLE16(&psxVuw[clutP+tC1]));
      }
    }
   if(NextRow_FT4()) return;
  }
}
////////////////////////////////////////////////////////////////////////
// POLY 3 F-SHADED TEX PAL 8
////////////////////////////////////////////////////////////////////////

void drawPoly3TEx8(short x1, short y1, short x2, short y2, short x3, short y3, short tx1, short ty1, short tx2, short ty2, short tx3, short ty3,short clX, short clY)
{
 int i,j,xmin,xmax,ymin,ymax;
 int32_t difX, difY,difX2, difY2;
 int32_t posX,posY,YAdjust,clutP;
 short tC1,tC2;

 if(x1>drawW && x2>drawW && x3>drawW) return;
 if(y1>drawH && y2>drawH && y3>drawH) return;
 if(x1<drawX && x2<drawX && x3<drawX) return;
 if(y1<drawY && y2<drawY && y3<drawY) return;
 if(drawY>=drawH) return;
 if(drawX>=drawW) return; 

 if(!SetupSections_FT(x1,y1,x2,y2,x3,y3,tx1,ty1,tx2,ty2,tx3,ty3)) return;

 ymax=Ymax;

 for(ymin=Ymin;ymin<drawY;ymin++)
  if(NextRow_FT()) return;

 clutP=(clY<<10)+clX;

 YAdjust=((GlobalTextAddrY)<<11)+(GlobalTextAddrX<<1);

 difX=delta_right_u;difX2=difX<<1;
 difY=delta_right_v;difY2=difY<<1;

#ifdef FASTSOLID

 if(!bCheckMask && !DrawSemiTrans)
  {
   for (i=ymin;i<=ymax;i++)
    {
     xmin=(left_x >> 16);
     xmax=(right_x >> 16)-1; //!!!!!!!!!!!!!!!
     if(drawW<xmax) xmax=drawW;

     if(xmax>=xmin)
      {
       posX=left_u;
       posY=left_v;

       if(xmin<drawX)
        {j=drawX-xmin;xmin=drawX;posX+=j*difX;posY+=j*difY;}

       for(j=xmin;j<xmax;j+=2)
        {
         tC1 = psxVub[((posY>>5)&(int32_t)0xFFFFF800)+YAdjust+(posX>>16)];
         tC2 = psxVub[(((posY+difY)>>5)&(int32_t)0xFFFFF800)+YAdjust+
                      ((posX+difX)>>16)];
         GetTextureTransColG32_S((uint32_t *)&psxVuw[(i<<10)+j],
             GETLE16(&psxVuw[clutP+tC1])|
             ((int32_t)GETLE16(&psxVuw[clutP+tC2]))<<16);
         posX+=difX2;
         posY+=difY2;
        }

       if(j==xmax)
        {
         tC1 = psxVub[((posY>>5)&(int32_t)0xFFFFF800)+YAdjust+(posX>>16)];
         GetTextureTransColG_S(&psxVuw[(i<<10)+j],GETLE16(&psxVuw[clutP+tC1]));
        }
      }
     if(NextRow_FT()) 
      {
       return;
      }
    }
   return;
  }

#endif

 for (i=ymin;i<=ymax;i++)
  {
   xmin=(left_x >> 16);
   xmax=(right_x >> 16)-1; //!!!!!!!!!!!!!!!!!
   if(drawW<xmax) xmax=drawW;

   if(xmax>=xmin)
    {
     posX=left_u;
     posY=left_v;

     if(xmin<drawX)
      {j=drawX-xmin;xmin=drawX;posX+=j*difX;posY+=j*difY;}

     for(j=xmin;j<xmax;j+=2)
      {
       tC1 = psxVub[((posY>>5)&(int32_t)0xFFFFF800)+YAdjust+(posX>>16)];
       tC2 = psxVub[(((posY+difY)>>5)&(int32_t)0xFFFFF800)+YAdjust+
                    ((posX+difX)>>16)];
       GetTextureTransColG32((uint32_t *)&psxVuw[(i<<10)+j],
           GETLE16(&psxVuw[clutP+tC1])|
           ((int32_t)GETLE16(&psxVuw[clutP+tC2]))<<16);
       posX+=difX2;
       posY+=difY2;
      }

     if(j==xmax)
      {
       tC1 = psxVub[((posY>>5)&(int32_t)0xFFFFF800)+YAdjust+(posX>>16)];
       GetTextureTransColG(&psxVuw[(i<<10)+j],GETLE16(&psxVuw[clutP+tC1]));
      }

    }
   if(NextRow_FT()) 
    {
     return;
    }
  }
}

////////////////////////////////////////////////////////////////////////

void drawPoly3TEx8_IL(short x1, short y1, short x2, short y2, short x3, short y3, short tx1, short ty1, short tx2, short ty2, short tx3, short ty3,short clX, short clY)
{
 int i,j,xmin,xmax,ymin,ymax,n_xi,n_yi,TXV,TXU;
 int32_t difX, difY,difX2, difY2;
 int32_t posX,posY,YAdjust,clutP;
 short tC1,tC2;

 if(x1>drawW && x2>drawW && x3>drawW) return;
 if(y1>drawH && y2>drawH && y3>drawH) return;
 if(x1<drawX && x2<drawX && x3<drawX) return;
 if(y1<drawY && y2<drawY && y3<drawY) return;
 if(drawY>=drawH) return;
 if(drawX>=drawW) return; 

 if(!SetupSections_FT(x1,y1,x2,y2,x3,y3,tx1,ty1,tx2,ty2,tx3,ty3)) return;

 ymax=Ymax;

 for(ymin=Ymin;ymin<drawY;ymin++)
  if(NextRow_FT()) return;

 clutP=(clY<<10)+clX;

 YAdjust=(GlobalTextAddrY<<10)+GlobalTextAddrX;

 difX=delta_right_u;difX2=difX<<1;
 difY=delta_right_v;difY2=difY<<1;

#ifdef FASTSOLID

 if(!bCheckMask && !DrawSemiTrans)
  {
   for (i=ymin;i<=ymax;i++)
    {
     xmin=(left_x >> 16);
     xmax=(right_x >> 16)-1; //!!!!!!!!!!!!!!!
     if(drawW<xmax) xmax=drawW;

     if(xmax>=xmin)
      {
       posX=left_u;
       posY=left_v;

       if(xmin<drawX)
        {j=drawX-xmin;xmin=drawX;posX+=j*difX;posY+=j*difY;}

       for(j=xmin;j<xmax;j+=2)
        {
         TXU=posX>>16;
         TXV=posY>>16;
         n_xi = ( ( TXU >> 1 ) & ~0x78 ) + ( ( TXU << 2 ) & 0x40 ) + ( ( TXV << 3 ) & 0x38 );
         n_yi = ( TXV & ~0x7 ) + ( ( TXU >> 5 ) & 0x7 );

         tC1= (GETLE16(&psxVuw[(n_yi<<10)+YAdjust+n_xi]) >> ((TXU & 0x01)<<3)) & 0xff;

         TXU=(posX+difX)>>16;
         TXV=(posY+difY)>>16;
         n_xi = ( ( TXU >> 1 ) & ~0x78 ) + ( ( TXU << 2 ) & 0x40 ) + ( ( TXV << 3 ) & 0x38 );
         n_yi = ( TXV & ~0x7 ) + ( ( TXU >> 5 ) & 0x7 );

         tC2= (GETLE16(&psxVuw[(n_yi<<10)+YAdjust+n_xi]) >> ((TXU & 0x01)<<3)) & 0xff;

         GetTextureTransColG32_S((uint32_t *)&psxVuw[(i<<10)+j],
             GETLE16(&psxVuw[clutP+tC1])|
             ((int32_t)GETLE16(&psxVuw[clutP+tC2]))<<16);
         posX+=difX2;
         posY+=difY2;
        }

       if(j==xmax)
        {
         TXU=posX>>16;
         TXV=posY>>16;
         n_xi = ( ( TXU >> 1 ) & ~0x78 ) + ( ( TXU << 2 ) & 0x40 ) + ( ( TXV << 3 ) & 0x38 );
         n_yi = ( TXV & ~0x7 ) + ( ( TXU >> 5 ) & 0x7 );

         tC1= (GETLE16(&psxVuw[(n_yi<<10)+YAdjust+n_xi]) >> ((TXU & 0x01)<<3)) & 0xff;

         GetTextureTransColG_S(&psxVuw[(i<<10)+j],GETLE16(&psxVuw[clutP+tC1]));
        }
      }
     if(NextRow_FT()) 
      {
       return;
      }
    }
   return;
  }

#endif

 for (i=ymin;i<=ymax;i++)
  {
   xmin=(left_x >> 16);
   xmax=(right_x >> 16)-1; //!!!!!!!!!!!!!!!!!
   if(drawW<xmax) xmax=drawW;

   if(xmax>=xmin)
    {
     posX=left_u;
     posY=left_v;

     if(xmin<drawX)
      {j=drawX-xmin;xmin=drawX;posX+=j*difX;posY+=j*difY;}

     for(j=xmin;j<xmax;j+=2)
      {
       TXU=posX>>16;
       TXV=posY>>16;
       n_xi = ( ( TXU >> 1 ) & ~0x78 ) + ( ( TXU << 2 ) & 0x40 ) + ( ( TXV << 3 ) & 0x38 );
       n_yi = ( TXV & ~0x7 ) + ( ( TXU >> 5 ) & 0x7 );

       tC1= (GETLE16(&psxVuw[(n_yi<<10)+YAdjust+n_xi]) >> ((TXU & 0x01)<<3)) & 0xff;

       TXU=(posX+difX)>>16;
       TXV=(posY+difY)>>16;
       n_xi = ( ( TXU >> 1 ) & ~0x78 ) + ( ( TXU << 2 ) & 0x40 ) + ( ( TXV << 3 ) & 0x38 );
       n_yi = ( TXV & ~0x7 ) + ( ( TXU >> 5 ) & 0x7 );

       tC2= (GETLE16(&psxVuw[(n_yi<<10)+YAdjust+n_xi]) >> ((TXU & 0x01)<<3)) & 0xff;

       GetTextureTransColG32((uint32_t *)&psxVuw[(i<<10)+j],
           GETLE16(&psxVuw[clutP+tC1])|
           ((int32_t)GETLE16(&psxVuw[clutP+tC2]))<<16);
       posX+=difX2;
       posY+=difY2;
      }

     if(j==xmax)
      {
       TXU=posX>>16;
       TXV=posY>>16;
       n_xi = ( ( TXU >> 1 ) & ~0x78 ) + ( ( TXU << 2 ) & 0x40 ) + ( ( TXV << 3 ) & 0x38 );
       n_yi = ( TXV & ~0x7 ) + ( ( TXU >> 5 ) & 0x7 );

       tC1= (GETLE16(&psxVuw[(n_yi<<10)+YAdjust+n_xi]) >> ((TXU & 0x01)<<3)) & 0xff;

       GetTextureTransColG(&psxVuw[(i<<10)+j],GETLE16(&psxVuw[clutP+tC1]));
      }

    }
   if(NextRow_FT()) 
    {
     return;
    }
  }
}

////////////////////////////////////////////////////////////////////////

void drawPoly3TEx8_TW(short x1, short y1, short x2, short y2, short x3, short y3, short tx1, short ty1, short tx2, short ty2, short tx3, short ty3,short clX, short clY)
{
 int i,j,xmin,xmax,ymin,ymax;
 int32_t difX, difY,difX2, difY2;
 int32_t posX,posY,YAdjust,clutP;
 short tC1,tC2;

 if(x1>drawW && x2>drawW && x3>drawW) return;
 if(y1>drawH && y2>drawH && y3>drawH) return;
 if(x1<drawX && x2<drawX && x3<drawX) return;
 if(y1<drawY && y2<drawY && y3<drawY) return;
 if(drawY>=drawH) return;
 if(drawX>=drawW) return; 

 if(!SetupSections_FT(x1,y1,x2,y2,x3,y3,tx1,ty1,tx2,ty2,tx3,ty3)) return;

 ymax=Ymax;

 for(ymin=Ymin;ymin<drawY;ymin++)
  if(NextRow_FT()) return;

 clutP=(clY<<10)+clX;

 YAdjust=((GlobalTextAddrY)<<11)+(GlobalTextAddrX<<1);
 YAdjust+=(TWin.Position.y0<<11)+(TWin.Position.x0);

 difX=delta_right_u;difX2=difX<<1;
 difY=delta_right_v;difY2=difY<<1;

#ifdef FASTSOLID

 if(!bCheckMask && !DrawSemiTrans)
  {
   for (i=ymin;i<=ymax;i++)
    {
     xmin=(left_x >> 16);
     xmax=(right_x >> 16);//-1; //!!!!!!!!!!!!!!!!
     if(xmax>xmin) xmax--;

     if(drawW<xmax) xmax=drawW;

     if(xmax>=xmin)
      {
       posX=left_u;
       posY=left_v;

       if(xmin<drawX)
        {j=drawX-xmin;xmin=drawX;posX+=j*difX;posY+=j*difY;}

       for(j=xmin;j<xmax;j+=2)
        {
         tC1 = psxVub[(((posY>>16)%TWin.Position.y1)<<11)+
                      YAdjust+((posX>>16)%TWin.Position.x1)];
         tC2 = psxVub[((((posY+difY)>>16)%TWin.Position.y1)<<11)+
                      YAdjust+(((posX+difX)>>16)%TWin.Position.x1)];
         GetTextureTransColG32_S((uint32_t *)&psxVuw[(i<<10)+j],
             GETLE16(&psxVuw[clutP+tC1])|
             ((int32_t)GETLE16(&psxVuw[clutP+tC2]))<<16);
         posX+=difX2;
         posY+=difY2;
        }

       if(j==xmax)
        {
         tC1 = psxVub[(((posY>>16)%TWin.Position.y1)<<11)+
                      YAdjust+((posX>>16)%TWin.Position.x1)];
         GetTextureTransColG_S(&psxVuw[(i<<10)+j],GETLE16(&psxVuw[clutP+tC1]));
        }
      }
     if(NextRow_FT()) 
      {
       return;
      }
    }
   return;
  }

#endif

 for (i=ymin;i<=ymax;i++)
  {
   xmin=(left_x >> 16);
   xmax=(right_x >> 16)-1; //!!!!!!!!!!!!!!!!!
   if(drawW<xmax) xmax=drawW;

   if(xmax>=xmin)
    {
     posX=left_u;
     posY=left_v;

     if(xmin<drawX)
      {j=drawX-xmin;xmin=drawX;posX+=j*difX;posY+=j*difY;}

     for(j=xmin;j<xmax;j+=2)
      {
       tC1 = psxVub[(((posY>>16)%TWin.Position.y1)<<11)+
                    YAdjust+((posX>>16)%TWin.Position.x1)];
       tC2 = psxVub[((((posY+difY)>>16)%TWin.Position.y1)<<11)+
                    YAdjust+(((posX+difX)>>16)%TWin.Position.x1)];
       GetTextureTransColG32((uint32_t *)&psxVuw[(i<<10)+j],
           GETLE16(&psxVuw[clutP+tC1])|
           ((int32_t)GETLE16(&psxVuw[clutP+tC2]))<<16);
       posX+=difX2;
       posY+=difY2;
      }

     if(j==xmax)
      {
       tC1 = psxVub[(((posY>>16)%TWin.Position.y1)<<11)+
                    YAdjust+((posX>>16)%TWin.Position.x1)];
       GetTextureTransColG(&psxVuw[(i<<10)+j],GETLE16(&psxVuw[clutP+tC1]));
      }

    }
   if(NextRow_FT()) 
    {
     return;
    }
  }
}

////////////////////////////////////////////////////////////////////////

#ifdef POLYQUAD3

void drawPoly4TEx8_TRI(short x1, short y1, short x2, short y2, short x3, short y3, short x4, short y4, short tx1, short ty1, short tx2, short ty2, short tx3, short ty3, short tx4, short ty4,short clX, short clY)
{
 drawPoly3TEx8(x2,y2,x3,y3,x4,y4,
               tx2,ty2,tx3,ty3,tx4,ty4,
               clX,clY);

 drawPoly3TEx8(x1,y1,x2,y2,x4,y4,
               tx1,ty1,tx2,ty2,tx4,ty4,
               clX,clY);
}

#endif

// more exact:

void drawPoly4TEx8(short x1, short y1, short x2, short y2, short x3, short y3, short x4, short y4, short tx1, short ty1, short tx2, short ty2, short tx3, short ty3, short tx4, short ty4,short clX, short clY)
{
 int32_t num; 
 int32_t i,j,xmin,xmax,ymin,ymax;
 int32_t difX, difY, difX2, difY2;
 int32_t posX,posY,YAdjust,clutP;
 short tC1,tC2;

 if(x1>drawW && x2>drawW && x3>drawW && x4>drawW) return;
 if(y1>drawH && y2>drawH && y3>drawH && y4>drawH) return;
 if(x1<drawX && x2<drawX && x3<drawX && x4<drawX) return;
 if(y1<drawY && y2<drawY && y3<drawY && y4<drawY) return;
 if(drawY>=drawH) return;
 if(drawX>=drawW) return; 

 if(!SetupSections_FT4(x1,y1,x2,y2,x3,y3,x4,y4,tx1,ty1,tx2,ty2,tx3,ty3,tx4,ty4)) return;

 ymax=Ymax;

 for(ymin=Ymin;ymin<drawY;ymin++)
  if(NextRow_FT4()) return;

 clutP=(clY<<10)+clX;

 YAdjust=((GlobalTextAddrY)<<11)+(GlobalTextAddrX<<1);

#ifdef FASTSOLID

 if(!bCheckMask && !DrawSemiTrans)
  {
   for (i=ymin;i<=ymax;i++)
    {
     xmin=(left_x >> 16);
     xmax=(right_x >> 16);

     if(xmax>=xmin)
      {
       posX=left_u;
       posY=left_v;

       num=(xmax-xmin);
       if(num==0) num=1;
       difX=(right_u-posX)/num;
       difY=(right_v-posY)/num;
       difX2=difX<<1;
       difY2=difY<<1;

       if(xmin<drawX)
        {j=drawX-xmin;xmin=drawX;posX+=j*difX;posY+=j*difY;}
       xmax--;if(drawW<xmax) xmax=drawW;

       for(j=xmin;j<xmax;j+=2)
        {
         tC1 = psxVub[((posY>>5)&(int32_t)0xFFFFF800)+YAdjust+(posX>>16)];
         tC2 = psxVub[(((posY+difY)>>5)&(int32_t)0xFFFFF800)+YAdjust+
                     ((posX+difX)>>16)];
         GetTextureTransColG32_S((uint32_t *)&psxVuw[(i<<10)+j],
              GETLE16(&psxVuw[clutP+tC1])|
              ((int32_t)GETLE16(&psxVuw[clutP+tC2]))<<16);
         posX+=difX2;
         posY+=difY2;
        }
       if(j==xmax)
        {
         tC1 = psxVub[((posY>>5)&(int32_t)0xFFFFF800)+YAdjust+(posX>>16)];
         GetTextureTransColG_S(&psxVuw[(i<<10)+j],GETLE16(&psxVuw[clutP+tC1]));
        }
      }
     if(NextRow_FT4()) return;
    }
   return;
  }

#endif

 for (i=ymin;i<=ymax;i++)
  {
   xmin=(left_x >> 16);
   xmax=(right_x >> 16);

   if(xmax>=xmin)
    {
     posX=left_u;
     posY=left_v;

     num=(xmax-xmin);
     if(num==0) num=1;
     difX=(right_u-posX)/num;
     difY=(right_v-posY)/num;
     difX2=difX<<1;
     difY2=difY<<1;

     if(xmin<drawX)
      {j=drawX-xmin;xmin=drawX;posX+=j*difX;posY+=j*difY;}
     xmax--;if(drawW<xmax) xmax=drawW;

     for(j=xmin;j<xmax;j+=2)
      {
       tC1 = psxVub[((posY>>5)&(int32_t)0xFFFFF800)+YAdjust+(posX>>16)];
       tC2 = psxVub[(((posY+difY)>>5)&(int32_t)0xFFFFF800)+YAdjust+
                     ((posX+difX)>>16)];
       GetTextureTransColG32((uint32_t *)&psxVuw[(i<<10)+j],
            GETLE16(&psxVuw[clutP+tC1])|
            ((int32_t)GETLE16(&psxVuw[clutP+tC2]))<<16);
       posX+=difX2;
       posY+=difY2;
      }
     if(j==xmax)
      {
       tC1 = psxVub[((posY>>5)&(int32_t)0xFFFFF800)+YAdjust+(posX>>16)];
       GetTextureTransColG(&psxVuw[(i<<10)+j],GETLE16(&psxVuw[clutP+tC1]));
      }
    }
   if(NextRow_FT4()) return;
  }
}

////////////////////////////////////////////////////////////////////////

void drawPoly4TEx8_IL(short x1, short y1, short x2, short y2, short x3, short y3, short x4, short y4, short tx1, short ty1, short tx2, short ty2, short tx3, short ty3, short tx4, short ty4,short clX, short clY)
{
 int32_t num; 
 int32_t i,j,xmin,xmax,ymin,ymax,n_xi,n_yi,TXV,TXU;
 int32_t difX, difY, difX2, difY2;
 int32_t posX,posY,YAdjust,clutP;
 short tC1,tC2;

 if(x1>drawW && x2>drawW && x3>drawW && x4>drawW) return;
 if(y1>drawH && y2>drawH && y3>drawH && y4>drawH) return;
 if(x1<drawX && x2<drawX && x3<drawX && x4<drawX) return;
 if(y1<drawY && y2<drawY && y3<drawY && y4<drawY) return;
 if(drawY>=drawH) return;
 if(drawX>=drawW) return; 

 if(!SetupSections_FT4(x1,y1,x2,y2,x3,y3,x4,y4,tx1,ty1,tx2,ty2,tx3,ty3,tx4,ty4)) return;

 ymax=Ymax;

 for(ymin=Ymin;ymin<drawY;ymin++)
  if(NextRow_FT4()) return;

 clutP=(clY<<10)+clX;

 YAdjust=(GlobalTextAddrY<<10)+GlobalTextAddrX;

#ifdef FASTSOLID

 if(!bCheckMask && !DrawSemiTrans)
  {
   for (i=ymin;i<=ymax;i++)
    {
     xmin=(left_x >> 16);
     xmax=(right_x >> 16);

     if(xmax>=xmin)
      {
       posX=left_u;
       posY=left_v;

       num=(xmax-xmin);
       if(num==0) num=1;
       difX=(right_u-posX)/num;
       difY=(right_v-posY)/num;
       difX2=difX<<1;
       difY2=difY<<1;

       if(xmin<drawX)
        {j=drawX-xmin;xmin=drawX;posX+=j*difX;posY+=j*difY;}
       xmax--;if(drawW<xmax) xmax=drawW;

       for(j=xmin;j<xmax;j+=2)
        {
         TXU=posX>>16;
         TXV=posY>>16;
         n_xi = ( ( TXU >> 1 ) & ~0x78 ) + ( ( TXU << 2 ) & 0x40 ) + ( ( TXV << 3 ) & 0x38 );
         n_yi = ( TXV & ~0x7 ) + ( ( TXU >> 5 ) & 0x7 );

         tC1= (GETLE16(&psxVuw[(n_yi<<10)+YAdjust+n_xi]) >> ((TXU & 0x01)<<3)) & 0xff;

         TXU=(posX+difX)>>16;
         TXV=(posY+difY)>>16;
         n_xi = ( ( TXU >> 1 ) & ~0x78 ) + ( ( TXU << 2 ) & 0x40 ) + ( ( TXV << 3 ) & 0x38 );
         n_yi = ( TXV & ~0x7 ) + ( ( TXU >> 5 ) & 0x7 );

         tC2= (GETLE16(&psxVuw[(n_yi<<10)+YAdjust+n_xi]) >> ((TXU & 0x01)<<3)) & 0xff;

         GetTextureTransColG32_S((uint32_t *)&psxVuw[(i<<10)+j],
              GETLE16(&psxVuw[clutP+tC1])|
              ((int32_t)GETLE16(&psxVuw[clutP+tC2]))<<16);
         posX+=difX2;
         posY+=difY2;
        }
       if(j==xmax)
        {
         TXU=posX>>16;
         TXV=posY>>16;
         n_xi = ( ( TXU >> 1 ) & ~0x78 ) + ( ( TXU << 2 ) & 0x40 ) + ( ( TXV << 3 ) & 0x38 );
         n_yi = ( TXV & ~0x7 ) + ( ( TXU >> 5 ) & 0x7 );

         tC1= (GETLE16(&psxVuw[(n_yi<<10)+YAdjust+n_xi]) >> ((TXU & 0x01)<<3)) & 0xff;

         GetTextureTransColG_S(&psxVuw[(i<<10)+j],GETLE16(&psxVuw[clutP+tC1]));
        }
      }
     if(NextRow_FT4()) return;
    }
   return;
  }

#endif

 for (i=ymin;i<=ymax;i++)
  {
   xmin=(left_x >> 16);
   xmax=(right_x >> 16);

   if(xmax>=xmin)
    {
     posX=left_u;
     posY=left_v;

     num=(xmax-xmin);
     if(num==0) num=1;
     difX=(right_u-posX)/num;
     difY=(right_v-posY)/num;
     difX2=difX<<1;
     difY2=difY<<1;

     if(xmin<drawX)
      {j=drawX-xmin;xmin=drawX;posX+=j*difX;posY+=j*difY;}
     xmax--;if(drawW<xmax) xmax=drawW;

     for(j=xmin;j<xmax;j+=2)
      {
       TXU=posX>>16;
       TXV=posY>>16;
       n_xi = ( ( TXU >> 1 ) & ~0x78 ) + ( ( TXU << 2 ) & 0x40 ) + ( ( TXV << 3 ) & 0x38 );
       n_yi = ( TXV & ~0x7 ) + ( ( TXU >> 5 ) & 0x7 );

       tC1= (GETLE16(&psxVuw[(n_yi<<10)+YAdjust+n_xi]) >> ((TXU & 0x01)<<3)) & 0xff;

       TXU=(posX+difX)>>16;
       TXV=(posY+difY)>>16;
       n_xi = ( ( TXU >> 1 ) & ~0x78 ) + ( ( TXU << 2 ) & 0x40 ) + ( ( TXV << 3 ) & 0x38 );
       n_yi = ( TXV & ~0x7 ) + ( ( TXU >> 5 ) & 0x7 );
       
       tC2= (GETLE16(&psxVuw[(n_yi<<10)+YAdjust+n_xi]) >> ((TXU & 0x01)<<3)) & 0xff;

       GetTextureTransColG32((uint32_t *)&psxVuw[(i<<10)+j],
            GETLE16(&psxVuw[clutP+tC1])|
            ((int32_t)GETLE16(&psxVuw[clutP+tC2]))<<16);
       posX+=difX2;
       posY+=difY2;
      }
     if(j==xmax)
      {
       TXU=posX>>16;
       TXV=posY>>16;
       n_xi = ( ( TXU >> 1 ) & ~0x78 ) + ( ( TXU << 2 ) & 0x40 ) + ( ( TXV << 3 ) & 0x38 );
       n_yi = ( TXV & ~0x7 ) + ( ( TXU >> 5 ) & 0x7 );
       tC1= (GETLE16(&psxVuw[(n_yi<<10)+YAdjust+n_xi]) >> ((TXU & 0x01)<<3)) & 0xff;
       GetTextureTransColG(&psxVuw[(i<<10)+j],GETLE16(&psxVuw[clutP+tC1]));
      }
    }
   if(NextRow_FT4()) return;
  }
}

////////////////////////////////////////////////////////////////////////

void drawPoly4TEx8_TW(short x1, short y1, short x2, short y2, short x3, short y3, short x4, short y4, short tx1, short ty1, short tx2, short ty2, short tx3, short ty3, short tx4, short ty4,short clX, short clY)
{
 int32_t num; 
 int32_t i,j,xmin,xmax,ymin,ymax;
 int32_t difX, difY, difX2, difY2;
 int32_t posX,posY,YAdjust,clutP;
 short tC1,tC2;

 if(x1>drawW && x2>drawW && x3>drawW && x4>drawW) return;
 if(y1>drawH && y2>drawH && y3>drawH && y4>drawH) return;
 if(x1<drawX && x2<drawX && x3<drawX && x4<drawX) return;
 if(y1<drawY && y2<drawY && y3<drawY && y4<drawY) return;
 if(drawY>=drawH) return;
 if(drawX>=drawW) return; 

 if(!SetupSections_FT4(x1,y1,x2,y2,x3,y3,x4,y4,tx1,ty1,tx2,ty2,tx3,ty3,tx4,ty4)) return;

 ymax=Ymax;

 for(ymin=Ymin;ymin<drawY;ymin++)
  if(NextRow_FT4()) return;

 clutP=(clY<<10)+clX;

 YAdjust=((GlobalTextAddrY)<<11)+(GlobalTextAddrX<<1);
 YAdjust+=(TWin.Position.y0<<11)+(TWin.Position.x0);

#ifdef FASTSOLID

 if(!bCheckMask && !DrawSemiTrans)
  {
   for (i=ymin;i<=ymax;i++)
    {
     xmin=(left_x >> 16);
     xmax=(right_x >> 16);

     if(xmax>=xmin)
      {
       posX=left_u;
       posY=left_v;

       num=(xmax-xmin);
       if(num==0) num=1;
       difX=(right_u-posX)/num;
       difY=(right_v-posY)/num;
       difX2=difX<<1;
       difY2=difY<<1;

       if(xmin<drawX)
        {j=drawX-xmin;xmin=drawX;posX+=j*difX;posY+=j*difY;}
       xmax--;if(drawW<xmax) xmax=drawW;

       for(j=xmin;j<xmax;j+=2)
        {
         tC1 = psxVub[(((posY>>16)%TWin.Position.y1)<<11)+
                      YAdjust+((posX>>16)%TWin.Position.x1)];
         tC2 = psxVub[((((posY+difY)>>16)%TWin.Position.y1)<<11)+
                      YAdjust+(((posX+difX)>>16)%TWin.Position.x1)];
         GetTextureTransColG32_S((uint32_t *)&psxVuw[(i<<10)+j],
              GETLE16(&psxVuw[clutP+tC1])|
              ((int32_t)GETLE16(&psxVuw[clutP+tC2]))<<16);
         posX+=difX2;
         posY+=difY2;
        }
       if(j==xmax)
        {
         tC1 = psxVub[((((posY+difY)>>16)%TWin.Position.y1)<<11)+
                      YAdjust+((posX>>16)%TWin.Position.x1)];
         GetTextureTransColG_S(&psxVuw[(i<<10)+j],GETLE16(&psxVuw[clutP+tC1]));
        }
      }
     if(NextRow_FT4()) return;
    }
   return;
  }

#endif


 for (i=ymin;i<=ymax;i++)
  {
   xmin=(left_x >> 16);
   xmax=(right_x >> 16);

   if(xmax>=xmin)
    {
     posX=left_u;
     posY=left_v;

     num=(xmax-xmin);
     if(num==0) num=1;
     difX=(right_u-posX)/num;
     difY=(right_v-posY)/num;
     difX2=difX<<1;
     difY2=difY<<1;

     if(xmin<drawX)
      {j=drawX-xmin;xmin=drawX;posX+=j*difX;posY+=j*difY;}
     xmax--;if(drawW<xmax) xmax=drawW;

     for(j=xmin;j<xmax;j+=2)
      {
       tC1 = psxVub[(((posY>>16)%TWin.Position.y1)<<11)+
                    YAdjust+((posX>>16)%TWin.Position.x1)];
       tC2 = psxVub[((((posY+difY)>>16)%TWin.Position.y1)<<11)+
                     YAdjust+(((posX+difX)>>16)%TWin.Position.x1)];
       GetTextureTransColG32((uint32_t *)&psxVuw[(i<<10)+j],
            GETLE16(&psxVuw[clutP+tC1])|
            ((int32_t)GETLE16(&psxVuw[clutP+tC2]))<<16);
       posX+=difX2;
       posY+=difY2;
      }
     if(j==xmax)
      {
       tC1 = psxVub[((((posY+difY)>>16)%TWin.Position.y1)<<11)+
                    YAdjust+((posX>>16)%TWin.Position.x1)];
       GetTextureTransColG(&psxVuw[(i<<10)+j],GETLE16(&psxVuw[clutP+tC1]));
      }
    }
   if(NextRow_FT4()) return;
  }
}

////////////////////////////////////////////////////////////////////////

void drawPoly4TEx8_TW_S(short x1, short y1, short x2, short y2, short x3, short y3, short x4, short y4, short tx1, short ty1, short tx2, short ty2, short tx3, short ty3, short tx4, short ty4,short clX, short clY)
{
 int32_t num; 
 int32_t i,j,xmin,xmax,ymin,ymax;
 int32_t difX, difY, difX2, difY2;
 int32_t posX,posY,YAdjust,clutP;
 short tC1,tC2;

 if(x1>drawW && x2>drawW && x3>drawW && x4>drawW) return;
 if(y1>drawH && y2>drawH && y3>drawH && y4>drawH) return;
 if(x1<drawX && x2<drawX && x3<drawX && x4<drawX) return;
 if(y1<drawY && y2<drawY && y3<drawY && y4<drawY) return;
 if(drawY>=drawH) return;
 if(drawX>=drawW) return; 

 if(!SetupSections_FT4(x1,y1,x2,y2,x3,y3,x4,y4,tx1,ty1,tx2,ty2,tx3,ty3,tx4,ty4)) return;

 ymax=Ymax;

 for(ymin=Ymin;ymin<drawY;ymin++)
  if(NextRow_FT4()) return;

 clutP=(clY<<10)+clX;

 YAdjust=((GlobalTextAddrY)<<11)+(GlobalTextAddrX<<1);
 YAdjust+=(TWin.Position.y0<<11)+(TWin.Position.x0);

#ifdef FASTSOLID

 if(!bCheckMask && !DrawSemiTrans)
  {
   for (i=ymin;i<=ymax;i++)
    {
     xmin=(left_x >> 16);
     xmax=(right_x >> 16);

     if(xmax>=xmin)
      {
       posX=left_u;
       posY=left_v;

       num=(xmax-xmin);
       if(num==0) num=1;
       difX=(right_u-posX)/num;
       difY=(right_v-posY)/num;
       difX2=difX<<1;
       difY2=difY<<1;

       if(xmin<drawX)
        {j=drawX-xmin;xmin=drawX;posX+=j*difX;posY+=j*difY;}
       xmax--;if(drawW<xmax) xmax=drawW;

       for(j=xmin;j<xmax;j+=2)
        {
         tC1 = psxVub[(((posY>>16)%TWin.Position.y1)<<11)+
                      YAdjust+((posX>>16)%TWin.Position.x1)];
         tC2 = psxVub[((((posY+difY)>>16)%TWin.Position.y1)<<11)+
                      YAdjust+(((posX+difX)>>16)%TWin.Position.x1)];
         GetTextureTransColG32_S((uint32_t *)&psxVuw[(i<<10)+j],
              GETLE16(&psxVuw[clutP+tC1])|
              ((int32_t)GETLE16(&psxVuw[clutP+tC2]))<<16);
         posX+=difX2;
         posY+=difY2;
        }
       if(j==xmax)
        {
         tC1 = psxVub[((((posY+difY)>>16)%TWin.Position.y1)<<11)+
                      YAdjust+((posX>>16)%TWin.Position.x1)];
         GetTextureTransColG_S(&psxVuw[(i<<10)+j],GETLE16(&psxVuw[clutP+tC1]));
        }
      }
     if(NextRow_FT4()) return;
    }
   return;
  }

#endif


 for (i=ymin;i<=ymax;i++)
  {
   xmin=(left_x >> 16);
   xmax=(right_x >> 16);

   if(xmax>=xmin)
    {
     posX=left_u;
     posY=left_v;

     num=(xmax-xmin);
     if(num==0) num=1;
     difX=(right_u-posX)/num;
     difY=(right_v-posY)/num;
     difX2=difX<<1;
     difY2=difY<<1;

     if(xmin<drawX)
      {j=drawX-xmin;xmin=drawX;posX+=j*difX;posY+=j*difY;}
     xmax--;if(drawW<xmax) xmax=drawW;

     for(j=xmin;j<xmax;j+=2)
      {
       tC1 = psxVub[(((posY>>16)%TWin.Position.y1)<<11)+
                    YAdjust+((posX>>16)%TWin.Position.x1)];
       tC2 = psxVub[((((posY+difY)>>16)%TWin.Position.y1)<<11)+
                     YAdjust+(((posX+difX)>>16)%TWin.Position.x1)];
       GetTextureTransColG32_SPR((uint32_t *)&psxVuw[(i<<10)+j],
            GETLE16(&psxVuw[clutP+tC1])|
            ((int32_t)GETLE16(&psxVuw[clutP+tC2]))<<16);
       posX+=difX2;
       posY+=difY2;
      }
     if(j==xmax)
      {
       tC1 = psxVub[((((posY+difY)>>16)%TWin.Position.y1)<<11)+
                    YAdjust+((posX>>16)%TWin.Position.x1)];
       GetTextureTransColG_SPR(&psxVuw[(i<<10)+j],GETLE16(&psxVuw[clutP+tC1]));
      }
    }
   if(NextRow_FT4()) return;
  }
}

////////////////////////////////////////////////////////////////////////
// POLY 3 F-SHADED TEX 15 BIT
////////////////////////////////////////////////////////////////////////

void drawPoly3TD(short x1, short y1, short x2, short y2, short x3, short y3, short tx1, short ty1, short tx2, short ty2, short tx3, short ty3)
{
 int i,j,xmin,xmax,ymin,ymax;
 int32_t difX, difY,difX2, difY2;
 int32_t posX,posY;

 if(x1>drawW && x2>drawW && x3>drawW) return;
 if(y1>drawH && y2>drawH && y3>drawH) return;
 if(x1<drawX && x2<drawX && x3<drawX) return;
 if(y1<drawY && y2<drawY && y3<drawY) return;
 if(drawY>=drawH) return;
 if(drawX>=drawW) return; 
                     
 if(!SetupSections_FT(x1,y1,x2,y2,x3,y3,tx1,ty1,tx2,ty2,tx3,ty3)) return;

 ymax=Ymax;

 for(ymin=Ymin;ymin<drawY;ymin++)
  if(NextRow_FT()) return;

 difX=delta_right_u;difX2=difX<<1;
 difY=delta_right_v;difY2=difY<<1;

#ifdef FASTSOLID

 if(!bCheckMask && !DrawSemiTrans)
  {
   for (i=ymin;i<=ymax;i++)
    {
     xmin=(left_x >> 16);
     xmax=(right_x >> 16)-1; //!!!!!!!!!!!!!
     if(drawW<xmax) xmax=drawW;

     if(xmax>=xmin)
      {
       posX=left_u;
       posY=left_v;

       if(xmin<drawX)
        {j=drawX-xmin;xmin=drawX;posX+=j*difX;posY+=j*difY;}

       for(j=xmin;j<xmax;j+=2)
        {
         GetTextureTransColG32_S((uint32_t *)&psxVuw[(i<<10)+j],
              (((int32_t)GETLE16(&psxVuw[((((posY+difY)>>16)+GlobalTextAddrY)<<10)+((posX+difX)>>16)+GlobalTextAddrX]))<<16)|
              GETLE16(&psxVuw[(((posY>>16)+GlobalTextAddrY)<<10)+((posX)>>16)+GlobalTextAddrX]));

         posX+=difX2;
         posY+=difY2;
        }
       if(j==xmax)
         GetTextureTransColG_S(&psxVuw[(i<<10)+j],
             GETLE16(&psxVuw[(((posY>>16)+GlobalTextAddrY)<<10)+(posX>>16)+GlobalTextAddrX]));
      }
     if(NextRow_FT()) 
      {
       return;
      }
    }
   return;
  }

#endif

 for (i=ymin;i<=ymax;i++)
  {
   xmin=(left_x >> 16);
   xmax=(right_x >> 16)-1; //!!!!!!!!!!!!!!
   if(drawW<xmax) xmax=drawW;

   if(xmax>=xmin)
    {
     posX=left_u;
     posY=left_v;

     if(xmin<drawX)
      {j=drawX-xmin;xmin=drawX;posX+=j*difX;posY+=j*difY;}

     for(j=xmin;j<xmax;j+=2)
      {
       GetTextureTransColG32((uint32_t *)&psxVuw[(i<<10)+j],
            (((int32_t)GETLE16(&psxVuw[((((posY+difY)>>16)+GlobalTextAddrY)<<10)+((posX+difX)>>16)+GlobalTextAddrX]))<<16)|
            GETLE16(&psxVuw[(((posY>>16)+GlobalTextAddrY)<<10)+((posX)>>16)+GlobalTextAddrX]));

       posX+=difX2;
       posY+=difY2;
      }
     if(j==xmax)
       GetTextureTransColG(&psxVuw[(i<<10)+j],
           GETLE16(&psxVuw[(((posY>>16)+GlobalTextAddrY)<<10)+(posX>>16)+GlobalTextAddrX]));
    }
   if(NextRow_FT()) 
    {
     return;
    }
  }
}

////////////////////////////////////////////////////////////////////////

void drawPoly3TD_TW(short x1, short y1, short x2, short y2, short x3, short y3, short tx1, short ty1, short tx2, short ty2, short tx3, short ty3)
{
 int i,j,xmin,xmax,ymin,ymax;
 int32_t difX, difY,difX2, difY2;
 int32_t posX,posY;

 if(x1>drawW && x2>drawW && x3>drawW) return;
 if(y1>drawH && y2>drawH && y3>drawH) return;
 if(x1<drawX && x2<drawX && x3<drawX) return;
 if(y1<drawY && y2<drawY && y3<drawY) return;
 if(drawY>=drawH) return;
 if(drawX>=drawW) return; 
                     
 if(!SetupSections_FT(x1,y1,x2,y2,x3,y3,tx1,ty1,tx2,ty2,tx3,ty3)) return;

 ymax=Ymax;

 for(ymin=Ymin;ymin<drawY;ymin++)
  if(NextRow_FT()) return;

 difX=delta_right_u;difX2=difX<<1;
 difY=delta_right_v;difY2=difY<<1;

#ifdef FASTSOLID

 if(!bCheckMask && !DrawSemiTrans)
  {
   for (i=ymin;i<=ymax;i++)
    {
     xmin=(left_x >> 16);
     xmax=(right_x >> 16)-1; //!!!!!!!!!!!!!
     if(drawW<xmax) xmax=drawW;

     if(xmax>=xmin)
      {
       posX=left_u;
       posY=left_v;

       if(xmin<drawX)
        {j=drawX-xmin;xmin=drawX;posX+=j*difX;posY+=j*difY;}

       for(j=xmin;j<xmax;j+=2)
        {
         GetTextureTransColG32_S((uint32_t *)&psxVuw[(i<<10)+j],
              (((int32_t)GETLE16(&psxVuw[(((((posY+difY)>>16)%TWin.Position.y1)+GlobalTextAddrY+TWin.Position.y0)<<10)+
              (((posX+difX)>>16)%TWin.Position.x1)+GlobalTextAddrX+TWin.Position.x0]))<<16)|
              GETLE16(&psxVuw[((((posY>>16)%TWin.Position.y1)+GlobalTextAddrY+TWin.Position.y0)<<10)+
                     (((posX)>>16)%TWin.Position.x1)+GlobalTextAddrX+TWin.Position.x0]));

         posX+=difX2;
         posY+=difY2;
        }
       if(j==xmax)
         GetTextureTransColG_S(&psxVuw[(i<<10)+j],
             GETLE16(&psxVuw[((((posY>>16)%TWin.Position.y1)+GlobalTextAddrY+TWin.Position.y0)<<10)+
                    ((posX>>16)%TWin.Position.x1)+GlobalTextAddrX+TWin.Position.x0]));
      }
     if(NextRow_FT()) 
      {
       return;
      }
    }
   return;
  }

#endif

 for (i=ymin;i<=ymax;i++)
  {
   xmin=(left_x >> 16);
   xmax=(right_x >> 16)-1; //!!!!!!!!!!!!!!
   if(drawW<xmax) xmax=drawW;

   if(xmax>=xmin)
    {
     posX=left_u;
     posY=left_v;

     if(xmin<drawX)
      {j=drawX-xmin;xmin=drawX;posX+=j*difX;posY+=j*difY;}

     for(j=xmin;j<xmax;j+=2)
      {
       GetTextureTransColG32((uint32_t *)&psxVuw[(i<<10)+j],
            (((int32_t)GETLE16(&psxVuw[(((((posY+difY)>>16)%TWin.Position.y1)+GlobalTextAddrY+TWin.Position.y0)<<10)+
            (((posX+difX)>>16)%TWin.Position.x1)+GlobalTextAddrX+TWin.Position.x0]))<<16)|
            GETLE16(&psxVuw[((((posY>>16)%TWin.Position.y1)+GlobalTextAddrY+TWin.Position.y0)<<10)+
                   (((posX)>>16)%TWin.Position.x1)+GlobalTextAddrX+TWin.Position.x0]));

       posX+=difX2;
       posY+=difY2;
      }
     if(j==xmax)
       GetTextureTransColG(&psxVuw[(i<<10)+j],
           GETLE16(&psxVuw[((((posY>>16)%TWin.Position.y1)+GlobalTextAddrY+TWin.Position.y0)<<10)+
                  ((posX>>16)%TWin.Position.x1)+GlobalTextAddrX+TWin.Position.x0]));
    }
   if(NextRow_FT()) 
    {
     return;
    }
  }
}


////////////////////////////////////////////////////////////////////////

#ifdef POLYQUAD3

void drawPoly4TD_TRI(short x1, short y1, short x2, short y2, short x3, short y3, short x4, short y4, short tx1, short ty1, short tx2, short ty2, short tx3, short ty3, short tx4, short ty4)
{
 drawPoly3TD(x2,y2,x3,y3,x4,y4,
            tx2,ty2,tx3,ty3,tx4,ty4);
 drawPoly3TD(x1,y1,x2,y2,x4,y4,
            tx1,ty1,tx2,ty2,tx4,ty4);
}

#endif

// more exact:

void drawPoly4TD(short x1, short y1, short x2, short y2, short x3, short y3, short x4, short y4, short tx1, short ty1, short tx2, short ty2, short tx3, short ty3, short tx4, short ty4)
{
 int32_t num; 
 int32_t i,j,xmin,xmax,ymin,ymax;
 int32_t difX, difY, difX2, difY2;
 int32_t posX,posY;

 if(x1>drawW && x2>drawW && x3>drawW && x4>drawW) return;
 if(y1>drawH && y2>drawH && y3>drawH && y4>drawH) return;
 if(x1<drawX && x2<drawX && x3<drawX && x4<drawX) return;
 if(y1<drawY && y2<drawY && y3<drawY && y4<drawY) return;
 if(drawY>=drawH) return;
 if(drawX>=drawW) return; 

 if(!SetupSections_FT4(x1,y1,x2,y2,x3,y3,x4,y4,tx1,ty1,tx2,ty2,tx3,ty3,tx4,ty4)) return;

 ymax=Ymax;

 for(ymin=Ymin;ymin<drawY;ymin++)
  if(NextRow_FT4()) return;

#ifdef FASTSOLID

 if(!bCheckMask && !DrawSemiTrans)
  {
   for (i=ymin;i<=ymax;i++)
    {
     xmin=(left_x >> 16);
     xmax=(right_x >> 16);

     if(xmax>=xmin)
      {
       posX=left_u;
       posY=left_v;

       num=(xmax-xmin);
       if(num==0) num=1;
       difX=(right_u-posX)/num;
       difY=(right_v-posY)/num;
       difX2=difX<<1;
       difY2=difY<<1;

       if(xmin<drawX)
        {j=drawX-xmin;xmin=drawX;posX+=j*difX;posY+=j*difY;}
       xmax--;if(drawW<xmax) xmax=drawW;

       for(j=xmin;j<xmax;j+=2)
        {
         GetTextureTransColG32_S((uint32_t *)&psxVuw[(i<<10)+j],
              (((int32_t)GETLE16(&psxVuw[((((posY+difY)>>16)+GlobalTextAddrY)<<10)+((posX+difX)>>16)+GlobalTextAddrX]))<<16)|
              GETLE16(&psxVuw[(((posY>>16)+GlobalTextAddrY)<<10)+((posX)>>16)+GlobalTextAddrX]));

         posX+=difX2;
         posY+=difY2;
        }
       if(j==xmax)
        GetTextureTransColG_S(&psxVuw[(i<<10)+j],
           GETLE16(&psxVuw[(((posY>>16)+GlobalTextAddrY)<<10)+(posX>>16)+GlobalTextAddrX]));
      }
     if(NextRow_FT4()) return;
    }
   return;
  }

#endif

 for (i=ymin;i<=ymax;i++)
  {
   xmin=(left_x >> 16);
   xmax=(right_x >> 16);

   if(xmax>=xmin)
    {
     posX=left_u;
     posY=left_v;

     num=(xmax-xmin);
     if(num==0) num=1;
     difX=(right_u-posX)/num;
     difY=(right_v-posY)/num;
     difX2=difX<<1;
     difY2=difY<<1;

     if(xmin<drawX)
      {j=drawX-xmin;xmin=drawX;posX+=j*difX;posY+=j*difY;}
     xmax--;if(drawW<xmax) xmax=drawW;

     for(j=xmin;j<xmax;j+=2)
      {
       GetTextureTransColG32((uint32_t *)&psxVuw[(i<<10)+j],
            (((int32_t)GETLE16(&psxVuw[((((posY+difY)>>16)+GlobalTextAddrY)<<10)+((posX+difX)>>16)+GlobalTextAddrX]))<<16)|
            GETLE16(&psxVuw[(((posY>>16)+GlobalTextAddrY)<<10)+((posX)>>16)+GlobalTextAddrX]));

       posX+=difX2;
       posY+=difY2;
      }
     if(j==xmax)
      GetTextureTransColG(&psxVuw[(i<<10)+j],
         GETLE16(&psxVuw[(((posY>>16)+GlobalTextAddrY)<<10)+(posX>>16)+GlobalTextAddrX]));
    }
   if(NextRow_FT4()) return;
  }
}

////////////////////////////////////////////////////////////////////////

void drawPoly4TD_TW(short x1, short y1, short x2, short y2, short x3, short y3, short x4, short y4, short tx1, short ty1, short tx2, short ty2, short tx3, short ty3, short tx4, short ty4)
{
 int32_t num; 
 int32_t i,j,xmin,xmax,ymin,ymax;
 int32_t difX, difY, difX2, difY2;
 int32_t posX,posY;

 if(x1>drawW && x2>drawW && x3>drawW && x4>drawW) return;
 if(y1>drawH && y2>drawH && y3>drawH && y4>drawH) return;
 if(x1<drawX && x2<drawX && x3<drawX && x4<drawX) return;
 if(y1<drawY && y2<drawY && y3<drawY && y4<drawY) return;
 if(drawY>=drawH) return;
 if(drawX>=drawW) return; 

 if(!SetupSections_FT4(x1,y1,x2,y2,x3,y3,x4,y4,tx1,ty1,tx2,ty2,tx3,ty3,tx4,ty4)) return;

 ymax=Ymax;

 for(ymin=Ymin;ymin<drawY;ymin++)
  if(NextRow_FT4()) return;

#ifdef FASTSOLID

 if(!bCheckMask && !DrawSemiTrans)
  {
   for (i=ymin;i<=ymax;i++)
    {
     xmin=(left_x >> 16);
     xmax=(right_x >> 16);

     if(xmax>=xmin)
      {
       posX=left_u;
       posY=left_v;

       num=(xmax-xmin);
       if(num==0) num=1;
       difX=(right_u-posX)/num;
       difY=(right_v-posY)/num;
       difX2=difX<<1;
       difY2=difY<<1;

       if(xmin<drawX)
        {j=drawX-xmin;xmin=drawX;posX+=j*difX;posY+=j*difY;}
       xmax--;if(drawW<xmax) xmax=drawW;

       for(j=xmin;j<xmax;j+=2)
        {
         GetTextureTransColG32_S((uint32_t *)&psxVuw[(i<<10)+j],
              (((int32_t)GETLE16(&psxVuw[(((((posY+difY)>>16)%TWin.Position.y1)+GlobalTextAddrY+TWin.Position.y0)<<10)+
                             (((posX+difX)>>16)%TWin.Position.x1)+GlobalTextAddrX+TWin.Position.x0]))<<16)|
              GETLE16(&psxVuw[((((posY>>16)%TWin.Position.y1)+GlobalTextAddrY)<<10)+TWin.Position.y0+
                     ((posX>>16)%TWin.Position.x1)+GlobalTextAddrX+TWin.Position.x0]));

         posX+=difX2;
         posY+=difY2;
        }
       if(j==xmax)
        GetTextureTransColG_S(&psxVuw[(i<<10)+j],
           GETLE16(&psxVuw[((((posY>>16)%TWin.Position.y1)+GlobalTextAddrY+TWin.Position.y0)<<10)+
                  ((posX>>16)%TWin.Position.x1)+GlobalTextAddrX+TWin.Position.x0]));
      }
     if(NextRow_FT4()) return;
    }
   return;
  }

#endif

 for (i=ymin;i<=ymax;i++)
  {
   xmin=(left_x >> 16);
   xmax=(right_x >> 16);

   if(xmax>=xmin)
    {
     posX=left_u;
     posY=left_v;

     num=(xmax-xmin);
     if(num==0) num=1;
     difX=(right_u-posX)/num;
     difY=(right_v-posY)/num;
     difX2=difX<<1;
     difY2=difY<<1;

     if(xmin<drawX)
      {j=drawX-xmin;xmin=drawX;posX+=j*difX;posY+=j*difY;}
     xmax--;if(drawW<xmax) xmax=drawW;

     for(j=xmin;j<xmax;j+=2)
      {
       GetTextureTransColG32((uint32_t *)&psxVuw[(i<<10)+j],
            (((int32_t)GETLE16(&psxVuw[(((((posY+difY)>>16)%TWin.Position.y1)+GlobalTextAddrY+TWin.Position.y0)<<10)+
                           (((posX+difX)>>16)%TWin.Position.x1)+GlobalTextAddrX+TWin.Position.x0]))<<16)|
            GETLE16(&psxVuw[((((posY>>16)%TWin.Position.y1)+GlobalTextAddrY+TWin.Position.y0)<<10)+
                   ((posX>>16)%TWin.Position.x1)+GlobalTextAddrX+TWin.Position.x0]));

       posX+=difX2;
       posY+=difY2;
      }
     if(j==xmax)
      GetTextureTransColG(&psxVuw[(i<<10)+j],
         GETLE16(&psxVuw[((((posY>>16)%TWin.Position.y1)+GlobalTextAddrY+TWin.Position.y0)<<10)+
                ((posX>>16)%TWin.Position.x1)+GlobalTextAddrX+TWin.Position.x0]));
    }
   if(NextRow_FT4()) return;
  }
}

////////////////////////////////////////////////////////////////////////

void drawPoly4TD_TW_S(short x1, short y1, short x2, short y2, short x3, short y3, short x4, short y4, short tx1, short ty1, short tx2, short ty2, short tx3, short ty3, short tx4, short ty4)
{
 int32_t num; 
 int32_t i,j,xmin,xmax,ymin,ymax;
 int32_t difX, difY, difX2, difY2;
 int32_t posX,posY;

 if(x1>drawW && x2>drawW && x3>drawW && x4>drawW) return;
 if(y1>drawH && y2>drawH && y3>drawH && y4>drawH) return;
 if(x1<drawX && x2<drawX && x3<drawX && x4<drawX) return;
 if(y1<drawY && y2<drawY && y3<drawY && y4<drawY) return;
 if(drawY>=drawH) return;
 if(drawX>=drawW) return; 

 if(!SetupSections_FT4(x1,y1,x2,y2,x3,y3,x4,y4,tx1,ty1,tx2,ty2,tx3,ty3,tx4,ty4)) return;

 ymax=Ymax;

 for(ymin=Ymin;ymin<drawY;ymin++)
  if(NextRow_FT4()) return;

#ifdef FASTSOLID

 if(!bCheckMask && !DrawSemiTrans)
  {
   for (i=ymin;i<=ymax;i++)
    {
     xmin=(left_x >> 16);
     xmax=(right_x >> 16);

     if(xmax>=xmin)
      {
       posX=left_u;
       posY=left_v;

       num=(xmax-xmin);
       if(num==0) num=1;
       difX=(right_u-posX)/num;
       difY=(right_v-posY)/num;
       difX2=difX<<1;
       difY2=difY<<1;

       if(xmin<drawX)
        {j=drawX-xmin;xmin=drawX;posX+=j*difX;posY+=j*difY;}
       xmax--;if(drawW<xmax) xmax=drawW;

       for(j=xmin;j<xmax;j+=2)
        {
         GetTextureTransColG32_S((uint32_t *)&psxVuw[(i<<10)+j],
              (((int32_t)GETLE16(&psxVuw[(((((posY+difY)>>16)%TWin.Position.y1)+GlobalTextAddrY+TWin.Position.y0)<<10)+
                             (((posX+difX)>>16)%TWin.Position.x1)+GlobalTextAddrX+TWin.Position.x0]))<<16)|
              GETLE16(&psxVuw[((((posY>>16)%TWin.Position.y1)+GlobalTextAddrY)<<10)+TWin.Position.y0+
                     ((posX>>16)%TWin.Position.x1)+GlobalTextAddrX+TWin.Position.x0]));

         posX+=difX2;
         posY+=difY2;
        }
       if(j==xmax)
        GetTextureTransColG_S(&psxVuw[(i<<10)+j],
           GETLE16(&psxVuw[((((posY>>16)%TWin.Position.y1)+GlobalTextAddrY+TWin.Position.y0)<<10)+
                  ((posX>>16)%TWin.Position.x1)+GlobalTextAddrX+TWin.Position.x0]));
      }
     if(NextRow_FT4()) return;
    }
   return;
  }

#endif

 for (i=ymin;i<=ymax;i++)
  {
   xmin=(left_x >> 16);
   xmax=(right_x >> 16);

   if(xmax>=xmin)
    {
     posX=left_u;
     posY=left_v;

     num=(xmax-xmin);
     if(num==0) num=1;
     difX=(right_u-posX)/num;
     difY=(right_v-posY)/num;
     difX2=difX<<1;
     difY2=difY<<1;

     if(xmin<drawX)
      {j=drawX-xmin;xmin=drawX;posX+=j*difX;posY+=j*difY;}
     xmax--;if(drawW<xmax) xmax=drawW;

     for(j=xmin;j<xmax;j+=2)
      {
       GetTextureTransColG32_SPR((uint32_t *)&psxVuw[(i<<10)+j],
            (((int32_t)GETLE16(&psxVuw[(((((posY+difY)>>16)%TWin.Position.y1)+GlobalTextAddrY+TWin.Position.y0)<<10)+
                           (((posX+difX)>>16)%TWin.Position.x1)+GlobalTextAddrX+TWin.Position.x0]))<<16)|
            GETLE16(&psxVuw[((((posY>>16)%TWin.Position.y1)+GlobalTextAddrY+TWin.Position.y0)<<10)+
                   ((posX>>16)%TWin.Position.x1)+GlobalTextAddrX+TWin.Position.x0]));

       posX+=difX2;
       posY+=difY2;
      }
     if(j==xmax)
      GetTextureTransColG_SPR(&psxVuw[(i<<10)+j],
         GETLE16(&psxVuw[((((posY>>16)%TWin.Position.y1)+GlobalTextAddrY+TWin.Position.y0)<<10)+
                ((posX>>16)%TWin.Position.x1)+GlobalTextAddrX+TWin.Position.x0]));
    }
   if(NextRow_FT4()) return;
  }
}

////////////////////////////////////////////////////////////////////////
// POLY 3/4 G-SHADED
////////////////////////////////////////////////////////////////////////
 
__inline void drawPoly3Gi(short x1,short y1,short x2,short y2,short x3,short y3,int32_t rgb1, int32_t rgb2, int32_t rgb3)
{
 int i,j,xmin,xmax,ymin,ymax;
 int32_t cR1,cG1,cB1;
 int32_t difR,difB,difG,difR2,difB2,difG2;

 if(x1>drawW && x2>drawW && x3>drawW) return;
 if(y1>drawH && y2>drawH && y3>drawH) return;
 if(x1<drawX && x2<drawX && x3<drawX) return;
 if(y1<drawY && y2<drawY && y3<drawY) return;
 if(drawY>=drawH) return;
 if(drawX>=drawW) return; 

 if(!SetupSections_G(x1,y1,x2,y2,x3,y3,rgb1,rgb2,rgb3)) return;

 ymax=Ymax;

 for(ymin=Ymin;ymin<drawY;ymin++)
  if(NextRow_G()) return;

 difR=delta_right_R;
 difG=delta_right_G;
 difB=delta_right_B;
 difR2=difR<<1;
 difG2=difG<<1;
 difB2=difB<<1;

#ifdef FASTSOLID

 if(!bCheckMask && !DrawSemiTrans && iDither!=2)
  {
   for (i=ymin;i<=ymax;i++)
    {
     xmin=(left_x >> 16);
     xmax=(right_x >> 16)-1;if(drawW<xmax) xmax=drawW;

     if(xmax>=xmin)
      {
       cR1=left_R;
       cG1=left_G;
       cB1=left_B;

       if(xmin<drawX)
        {j=drawX-xmin;xmin=drawX;cR1+=j*difR;cG1+=j*difG;cB1+=j*difB;}

       for(j=xmin;j<xmax;j+=2) 
        {
         PUTLE32(((uint32_t *)&psxVuw[(i<<10)+j]), 
            ((((cR1+difR) <<7)&0x7c000000)|(((cG1+difG) << 2)&0x03e00000)|(((cB1+difB)>>3)&0x001f0000)|
             (((cR1) >> 9)&0x7c00)|(((cG1) >> 14)&0x03e0)|(((cB1) >> 19)&0x001f))|lSetMask);
   
         cR1+=difR2;
         cG1+=difG2;
         cB1+=difB2;
        }
       if(j==xmax)
        PUTLE16(&psxVuw[(i<<10)+j], (((cR1 >> 9)&0x7c00)|((cG1 >> 14)&0x03e0)|((cB1 >> 19)&0x001f))|sSetMask);
      }
     if(NextRow_G()) return;
    }
   return;
  }

#endif

 if(iDither==2)
 for (i=ymin;i<=ymax;i++)
  {
   xmin=(left_x >> 16);
   xmax=(right_x >> 16)-1;if(drawW<xmax) xmax=drawW;

   if(xmax>=xmin)
    {
     cR1=left_R;
     cG1=left_G;
     cB1=left_B;

     if(xmin<drawX)
      {j=drawX-xmin;xmin=drawX;cR1+=j*difR;cG1+=j*difG;cB1+=j*difB;}

     for(j=xmin;j<=xmax;j++) 
      {
       GetShadeTransCol_Dither(&psxVuw[(i<<10)+j],(cB1>>16),(cG1>>16),(cR1>>16));

       cR1+=difR;
       cG1+=difG;
       cB1+=difB;
      }
    }
   if(NextRow_G()) return;
  }
 else
 for (i=ymin;i<=ymax;i++)
  {
   xmin=(left_x >> 16);
   xmax=(right_x >> 16)-1;if(drawW<xmax) xmax=drawW;

   if(xmax>=xmin)
    {
     cR1=left_R;
     cG1=left_G;
     cB1=left_B;

     if(xmin<drawX)
      {j=drawX-xmin;xmin=drawX;cR1+=j*difR;cG1+=j*difG;cB1+=j*difB;}

     for(j=xmin;j<=xmax;j++) 
      {
       GetShadeTransCol(&psxVuw[(i<<10)+j],((cR1 >> 9)&0x7c00)|((cG1 >> 14)&0x03e0)|((cB1 >> 19)&0x001f));

       cR1+=difR;
       cG1+=difG;
       cB1+=difB;
      }
    }
   if(NextRow_G()) return;
  }

}

////////////////////////////////////////////////////////////////////////

void drawPoly3G(int32_t rgb1, int32_t rgb2, int32_t rgb3)
{
 drawPoly3Gi(lx0,ly0,lx1,ly1,lx2,ly2,rgb1,rgb2,rgb3);
}

// draw two g-shaded tris for right psx shading emulation

void drawPoly4G(int32_t rgb1, int32_t rgb2, int32_t rgb3, int32_t rgb4)
{
 drawPoly3Gi(lx1,ly1,lx3,ly3,lx2,ly2,
             rgb2,rgb4,rgb3);
 drawPoly3Gi(lx0,ly0,lx1,ly1,lx2,ly2,
             rgb1,rgb2,rgb3);
}

////////////////////////////////////////////////////////////////////////
// POLY 3/4 G-SHADED TEX PAL4
////////////////////////////////////////////////////////////////////////

void drawPoly3TGEx4(short x1, short y1, short x2, short y2, short x3, short y3, short tx1, short ty1, short tx2, short ty2, short tx3, short ty3, short clX, short clY,int32_t col1, int32_t col2, int32_t col3)
{
 int i,j,xmin,xmax,ymin,ymax;
 int32_t cR1,cG1,cB1;
 int32_t difR,difB,difG,difR2,difB2,difG2;
 int32_t difX, difY,difX2, difY2;
 int32_t posX,posY,YAdjust,clutP,XAdjust;
 short tC1,tC2;

 if(x1>drawW && x2>drawW && x3>drawW) return;
 if(y1>drawH && y2>drawH && y3>drawH) return;
 if(x1<drawX && x2<drawX && x3<drawX) return;
 if(y1<drawY && y2<drawY && y3<drawY) return;
 if(drawY>=drawH) return;
 if(drawX>=drawW) return; 

 if(!SetupSections_GT(x1,y1,x2,y2,x3,y3,tx1,ty1,tx2,ty2,tx3,ty3,col1,col2,col3)) return;

 ymax=Ymax;

 for(ymin=Ymin;ymin<drawY;ymin++)
  if(NextRow_GT()) return;

 clutP=(clY<<10)+clX;

 YAdjust=((GlobalTextAddrY)<<11)+(GlobalTextAddrX<<1);

 difR=delta_right_R;
 difG=delta_right_G;
 difB=delta_right_B;
 difR2=difR<<1;
 difG2=difG<<1;
 difB2=difB<<1;

 difX=delta_right_u;difX2=difX<<1;
 difY=delta_right_v;difY2=difY<<1;

#ifdef FASTSOLID

 if(!bCheckMask && !DrawSemiTrans && !iDither)
  {
   for (i=ymin;i<=ymax;i++)
    {
     xmin=((left_x) >> 16);
     xmax=((right_x) >> 16)-1; //!!!!!!!!!!!!!
     if(drawW<xmax) xmax=drawW;

     if(xmax>=xmin)
      {
       posX=left_u;
       posY=left_v;
       cR1=left_R;
       cG1=left_G;
       cB1=left_B;

       if(xmin<drawX)
        {j=drawX-xmin;xmin=drawX;posX+=j*difX;posY+=j*difY;cR1+=j*difR;cG1+=j*difG;cB1+=j*difB;}

       for(j=xmin;j<xmax;j+=2) 
        {
         XAdjust=(posX>>16);
         tC1 = psxVub[((posY>>5)&0xFFFFF800)+YAdjust+(XAdjust>>1)];
         tC1=(tC1>>((XAdjust&1)<<2))&0xf;
         XAdjust=((posX+difX)>>16);
         tC2 = psxVub[(((posY+difY)>>5)&(int32_t)0xFFFFF800)+YAdjust+
                      (XAdjust>>1)];
         tC2=(tC2>>((XAdjust&1)<<2))&0xf;

         GetTextureTransColGX32_S((uint32_t *)&psxVuw[(i<<10)+j],
               GETLE16(&psxVuw[clutP+tC1])|
               ((int32_t)GETLE16(&psxVuw[clutP+tC2]))<<16,
               (cB1>>16)|((cB1+difB)&0xff0000),
               (cG1>>16)|((cG1+difG)&0xff0000),
               (cR1>>16)|((cR1+difR)&0xff0000));
         posX+=difX2;
         posY+=difY2;
         cR1+=difR2;
         cG1+=difG2;
         cB1+=difB2;
        }
       if(j==xmax)
        {
         XAdjust=(posX>>16);
         tC1 = psxVub[((posY>>5)&(int32_t)0xFFFFF800)+YAdjust+(XAdjust>>1)];
         tC1=(tC1>>((XAdjust&1)<<2))&0xf;
         GetTextureTransColGX_S(&psxVuw[(i<<10)+j], 
              GETLE16(&psxVuw[clutP+tC1]),
              (cB1>>16),(cG1>>16),(cR1>>16));
        }
      }
     if(NextRow_GT()) 
      {
       return;
      }
    }
   return;
  }

#endif

 for (i=ymin;i<=ymax;i++)
  {
   xmin=(left_x >> 16);
   xmax=(right_x >> 16)-1; //!!!!!!!!!!!!!!!!
   if(drawW<xmax) xmax=drawW;

   if(xmax>=xmin)
    {
     posX=left_u;
     posY=left_v;
     cR1=left_R;
     cG1=left_G;
     cB1=left_B;

     if(xmin<drawX)
      {j=drawX-xmin;xmin=drawX;posX+=j*difX;posY+=j*difY;cR1+=j*difR;cG1+=j*difG;cB1+=j*difB;}

     for(j=xmin;j<=xmax;j++) 
      {
       XAdjust=(posX>>16);
       tC1 = psxVub[((posY>>5)&(int32_t)0xFFFFF800)+YAdjust+(XAdjust>>1)];
       tC1=(tC1>>((XAdjust&1)<<2))&0xf;
       if(iDither)
        GetTextureTransColGX_Dither(&psxVuw[(i<<10)+j], 
            GETLE16(&psxVuw[clutP+tC1]),
            (cB1>>16),(cG1>>16),(cR1>>16));
       else
        GetTextureTransColGX(&psxVuw[(i<<10)+j], 
            GETLE16(&psxVuw[clutP+tC1]),
            (cB1>>16),(cG1>>16),(cR1>>16));
       posX+=difX;
       posY+=difY;
       cR1+=difR;
       cG1+=difG;
       cB1+=difB;
      }
    }
   if(NextRow_GT()) 
    {
     return;
    }
  }
}

////////////////////////////////////////////////////////////////////////

void drawPoly3TGEx4_IL(short x1, short y1, short x2, short y2, short x3, short y3, short tx1, short ty1, short tx2, short ty2, short tx3, short ty3, short clX, short clY,int32_t col1, int32_t col2, int32_t col3)
{
 int i,j,xmin,xmax,ymin,ymax,n_xi,n_yi,TXV;
 int32_t cR1,cG1,cB1;
 int32_t difR,difB,difG,difR2,difB2,difG2;
 int32_t difX, difY,difX2, difY2;
 int32_t posX,posY,YAdjust,clutP,XAdjust;
 short tC1,tC2;

 if(x1>drawW && x2>drawW && x3>drawW) return;
 if(y1>drawH && y2>drawH && y3>drawH) return;
 if(x1<drawX && x2<drawX && x3<drawX) return;
 if(y1<drawY && y2<drawY && y3<drawY) return;
 if(drawY>=drawH) return;
 if(drawX>=drawW) return; 

 if(!SetupSections_GT(x1,y1,x2,y2,x3,y3,tx1,ty1,tx2,ty2,tx3,ty3,col1,col2,col3)) return;

 ymax=Ymax;

 for(ymin=Ymin;ymin<drawY;ymin++)
  if(NextRow_GT()) return;

 clutP=(clY<<10)+clX;

 YAdjust=(GlobalTextAddrY<<10)+GlobalTextAddrX;

 difR=delta_right_R;
 difG=delta_right_G;
 difB=delta_right_B;
 difR2=difR<<1;
 difG2=difG<<1;
 difB2=difB<<1;

 difX=delta_right_u;difX2=difX<<1;
 difY=delta_right_v;difY2=difY<<1;

#ifdef FASTSOLID

 if(!bCheckMask && !DrawSemiTrans && !iDither)
  {
   for (i=ymin;i<=ymax;i++)
    {
     xmin=((left_x) >> 16);
     xmax=((right_x) >> 16)-1; //!!!!!!!!!!!!!
     if(drawW<xmax) xmax=drawW;

     if(xmax>=xmin)
      {
       posX=left_u;
       posY=left_v;
       cR1=left_R;
       cG1=left_G;
       cB1=left_B;

       if(xmin<drawX)
        {j=drawX-xmin;xmin=drawX;posX+=j*difX;posY+=j*difY;cR1+=j*difR;cG1+=j*difG;cB1+=j*difB;}

       for(j=xmin;j<xmax;j+=2) 
        {
         XAdjust=(posX>>16);

         TXV=posY>>16;
         n_xi = ( ( XAdjust >> 2 ) & ~0x3c ) + ( ( TXV << 2 ) & 0x3c );
         n_yi = ( TXV & ~0xf ) + ( ( XAdjust >> 4 ) & 0xf );
 
         tC1= (GETLE16(&psxVuw[(n_yi<<10)+YAdjust+n_xi]) >> ((XAdjust & 0x03)<<2)) & 0x0f ;

         XAdjust=((posX+difX)>>16);

         TXV=(posY+difY)>>16;
         n_xi = ( ( XAdjust >> 2 ) & ~0x3c ) + ( ( TXV << 2 ) & 0x3c );
         n_yi = ( TXV & ~0xf ) + ( ( XAdjust >> 4 ) & 0xf );

         tC2= (GETLE16(&psxVuw[(n_yi<<10)+YAdjust+n_xi]) >> ((XAdjust & 0x03)<<2)) & 0x0f ;

         GetTextureTransColGX32_S((uint32_t *)&psxVuw[(i<<10)+j],
               GETLE16(&psxVuw[clutP+tC1])|
               ((int32_t)GETLE16(&psxVuw[clutP+tC2]))<<16,
               (cB1>>16)|((cB1+difB)&0xff0000),
               (cG1>>16)|((cG1+difG)&0xff0000),
               (cR1>>16)|((cR1+difR)&0xff0000));
         posX+=difX2;
         posY+=difY2;
         cR1+=difR2;
         cG1+=difG2;
         cB1+=difB2;
        }
       if(j==xmax)
        {
         XAdjust=(posX>>16);

         TXV=posY>>16;
         n_xi = ( ( XAdjust >> 2 ) & ~0x3c ) + ( ( TXV << 2 ) & 0x3c );
         n_yi = ( TXV & ~0xf ) + ( ( XAdjust >> 4 ) & 0xf );

         tC1= (GETLE16(&psxVuw[(n_yi<<10)+YAdjust+n_xi]) >> ((XAdjust & 0x03)<<2)) & 0x0f ;

         GetTextureTransColGX_S(&psxVuw[(i<<10)+j], 
              GETLE16(&psxVuw[clutP+tC1]),
              (cB1>>16),(cG1>>16),(cR1>>16));
        }
      }
     if(NextRow_GT()) 
      {
       return;
      }
    }
   return;
  }

#endif

 for (i=ymin;i<=ymax;i++)
  {
   xmin=(left_x >> 16);
   xmax=(right_x >> 16)-1; //!!!!!!!!!!!!!!!!
   if(drawW<xmax) xmax=drawW;

   if(xmax>=xmin)
    {
     posX=left_u;
     posY=left_v;
     cR1=left_R;
     cG1=left_G;
     cB1=left_B;

     if(xmin<drawX)
      {j=drawX-xmin;xmin=drawX;posX+=j*difX;posY+=j*difY;cR1+=j*difR;cG1+=j*difG;cB1+=j*difB;}

     for(j=xmin;j<=xmax;j++) 
      {
       XAdjust=(posX>>16);

       TXV=posY>>16;
       n_xi = ( ( XAdjust >> 2 ) & ~0x3c ) + ( ( TXV << 2 ) & 0x3c );
       n_yi = ( TXV & ~0xf ) + ( ( XAdjust >> 4 ) & 0xf );

       tC1= (GETLE16(&psxVuw[(n_yi<<10)+YAdjust+n_xi]) >> ((XAdjust & 0x03)<<2)) & 0x0f ;

       if(iDither)
        GetTextureTransColGX_Dither(&psxVuw[(i<<10)+j], 
            GETLE16(&psxVuw[clutP+tC1]),
            (cB1>>16),(cG1>>16),(cR1>>16));
       else
        GetTextureTransColGX(&psxVuw[(i<<10)+j], 
            GETLE16(&psxVuw[clutP+tC1]),
            (cB1>>16),(cG1>>16),(cR1>>16));
       posX+=difX;
       posY+=difY;
       cR1+=difR;
       cG1+=difG;
       cB1+=difB;
      }
    }
   if(NextRow_GT()) 
    {
     return;
    }
  }
}

////////////////////////////////////////////////////////////////////////

void drawPoly3TGEx4_TW(short x1, short y1, short x2, short y2, short x3, short y3, short tx1, short ty1, short tx2, short ty2, short tx3, short ty3, short clX, short clY,int32_t col1, int32_t col2, int32_t col3)
{
 int i,j,xmin,xmax,ymin,ymax;
 int32_t cR1,cG1,cB1;
 int32_t difR,difB,difG,difR2,difB2,difG2;
 int32_t difX, difY,difX2, difY2;
 int32_t posX,posY,YAdjust,clutP,XAdjust;
 short tC1,tC2;

 if(x1>drawW && x2>drawW && x3>drawW) return;
 if(y1>drawH && y2>drawH && y3>drawH) return;
 if(x1<drawX && x2<drawX && x3<drawX) return;
 if(y1<drawY && y2<drawY && y3<drawY) return;
 if(drawY>=drawH) return;
 if(drawX>=drawW) return; 

 if(!SetupSections_GT(x1,y1,x2,y2,x3,y3,tx1,ty1,tx2,ty2,tx3,ty3,col1,col2,col3)) return;

 ymax=Ymax;

 for(ymin=Ymin;ymin<drawY;ymin++)
  if(NextRow_GT()) return;

 clutP=(clY<<10)+clX;

 YAdjust=((GlobalTextAddrY)<<11)+(GlobalTextAddrX<<1);
 YAdjust+=(TWin.Position.y0<<11)+(TWin.Position.x0>>1);

 difR=delta_right_R;
 difG=delta_right_G;
 difB=delta_right_B;
 difR2=difR<<1;
 difG2=difG<<1;
 difB2=difB<<1;

 difX=delta_right_u;difX2=difX<<1;
 difY=delta_right_v;difY2=difY<<1;

#ifdef FASTSOLID

 if(!bCheckMask && !DrawSemiTrans && !iDither)
  {
   for (i=ymin;i<=ymax;i++)
    {
     xmin=((left_x) >> 16);
     xmax=((right_x) >> 16)-1; //!!!!!!!!!!!!!
     if(drawW<xmax) xmax=drawW;

     if(xmax>=xmin)
      {
       posX=left_u;
       posY=left_v;
       cR1=left_R;
       cG1=left_G;
       cB1=left_B;

       if(xmin<drawX)
        {j=drawX-xmin;xmin=drawX;posX+=j*difX;posY+=j*difY;cR1+=j*difR;cG1+=j*difG;cB1+=j*difB;}

       for(j=xmin;j<xmax;j+=2) 
        {
         XAdjust=(posX>>16)%TWin.Position.x1;
         tC1 = psxVub[(((posY>>16)%TWin.Position.y1)<<11)+
                      YAdjust+(XAdjust>>1)];
         tC1=(tC1>>((XAdjust&1)<<2))&0xf;
         XAdjust=((posX+difX)>>16)%TWin.Position.x1;
         tC2 = psxVub[((((posY+difY)>>16)%TWin.Position.y1)<<11)+
                      YAdjust+(XAdjust>>1)];
         tC2=(tC2>>((XAdjust&1)<<2))&0xf;
         GetTextureTransColGX32_S((uint32_t *)&psxVuw[(i<<10)+j],
              GETLE16(&psxVuw[clutP+tC1])|
              ((int32_t)GETLE16(&psxVuw[clutP+tC2]))<<16,
              (cB1>>16)|((cB1+difB)&0xff0000),
              (cG1>>16)|((cG1+difG)&0xff0000),
              (cR1>>16)|((cR1+difR)&0xff0000));
         posX+=difX2;
         posY+=difY2;
         cR1+=difR2;
         cG1+=difG2;
         cB1+=difB2;
        }
       if(j==xmax)
        {
         XAdjust=(posX>>16)%TWin.Position.x1;
         tC1 = psxVub[(((posY>>16)%TWin.Position.y1)<<11)+
                       YAdjust+(XAdjust>>1)];
         tC1=(tC1>>((XAdjust&1)<<2))&0xf;
         GetTextureTransColGX_S(&psxVuw[(i<<10)+j], 
             GETLE16(&psxVuw[clutP+tC1]),
             (cB1>>16),(cG1>>16),(cR1>>16));
        }
      }
     if(NextRow_GT()) 
      {
       return;
      }
    }
   return;
  }

#endif

 for (i=ymin;i<=ymax;i++)
  {
   xmin=(left_x >> 16);
   xmax=(right_x >> 16)-1; //!!!!!!!!!!!!!!!!
   if(drawW<xmax) xmax=drawW;

   if(xmax>=xmin)
    {
     posX=left_u;
     posY=left_v;
     cR1=left_R;
     cG1=left_G;
     cB1=left_B;

     if(xmin<drawX)
      {j=drawX-xmin;xmin=drawX;posX+=j*difX;posY+=j*difY;cR1+=j*difR;cG1+=j*difG;cB1+=j*difB;}

     for(j=xmin;j<=xmax;j++) 
      {
       XAdjust=(posX>>16)%TWin.Position.x1;
       tC1 = psxVub[(((posY>>16)%TWin.Position.y1)<<11)+
                    YAdjust+(XAdjust>>1)];
       tC1=(tC1>>((XAdjust&1)<<2))&0xf;
       if(iDither)
        GetTextureTransColGX_Dither(&psxVuw[(i<<10)+j], 
            GETLE16(&psxVuw[clutP+tC1]),
            (cB1>>16),(cG1>>16),(cR1>>16));
       else
        GetTextureTransColGX(&psxVuw[(i<<10)+j], 
            GETLE16(&psxVuw[clutP+tC1]),
            (cB1>>16),(cG1>>16),(cR1>>16));
       posX+=difX;
       posY+=difY;
       cR1+=difR;
       cG1+=difG;
       cB1+=difB;
      }
    }
   if(NextRow_GT()) 
    {
     return;
    }
  }
}

////////////////////////////////////////////////////////////////////////

// note: the psx is doing g-shaded quads as two g-shaded tris,
// like the following func... sadly texturing is not 100%
// correct that way, so small texture distortions can 
// happen... 

void drawPoly4TGEx4_TRI_IL(short x1, short y1, short x2, short y2, short x3, short y3, short x4, short y4,
                    short tx1, short ty1, short tx2, short ty2, short tx3, short ty3, short tx4, short ty4,
                    short clX, short clY,
                    int32_t col1, int32_t col2, int32_t col3, int32_t col4)
{
 drawPoly3TGEx4_IL(x2,y2,x3,y3,x4,y4,
                   tx2,ty2,tx3,ty3,tx4,ty4,
                   clX,clY,
                   col2,col4,col3);
 drawPoly3TGEx4_IL(x1,y1,x2,y2,x4,y4,
                   tx1,ty1,tx2,ty2,tx4,ty4,
                   clX,clY,
                   col1,col2,col3);
}

#ifdef POLYQUAD3GT

void drawPoly4TGEx4_TRI(short x1, short y1, short x2, short y2, short x3, short y3, short x4, short y4, 
                    short tx1, short ty1, short tx2, short ty2, short tx3, short ty3, short tx4, short ty4, 
                    short clX, short clY,
                    int32_t col1, int32_t col2, int32_t col3, int32_t col4)
{
 drawPoly3TGEx4(x2,y2,x3,y3,x4,y4,
                tx2,ty2,tx3,ty3,tx4,ty4,
                clX,clY,
                col2,col4,col3);
 drawPoly3TGEx4(x1,y1,x2,y2,x4,y4,
                tx1,ty1,tx2,ty2,tx4,ty4,
                clX,clY,
                col1,col2,col3);
}

#endif
               
////////////////////////////////////////////////////////////////////////

void drawPoly4TGEx4(short x1, short y1, short x2, short y2, short x3, short y3, short x4, short y4, 
                    short tx1, short ty1, short tx2, short ty2, short tx3, short ty3, short tx4, short ty4, 
                    short clX, short clY,
                    int32_t col1, int32_t col2, int32_t col4, int32_t col3)
{
 int32_t num; 
 int32_t i,j,xmin,xmax,ymin,ymax;
 int32_t cR1,cG1,cB1;
 int32_t difR,difB,difG,difR2,difB2,difG2;
 int32_t difX, difY, difX2, difY2;
 int32_t posX,posY,YAdjust,clutP,XAdjust;
 short tC1,tC2;

 if(x1>drawW && x2>drawW && x3>drawW && x4>drawW) return;
 if(y1>drawH && y2>drawH && y3>drawH && y4>drawH) return;
 if(x1<drawX && x2<drawX && x3<drawX && x4<drawX) return;
 if(y1<drawY && y2<drawY && y3<drawY && y4<drawY) return;
 if(drawY>=drawH) return;
 if(drawX>=drawW) return; 

 if(!SetupSections_GT4(x1,y1,x2,y2,x3,y3,x4,y4,tx1,ty1,tx2,ty2,tx3,ty3,tx4,ty4,col1,col2,col3,col4)) return;

 ymax=Ymax;

 for(ymin=Ymin;ymin<drawY;ymin++)
  if(NextRow_GT4()) return;

 clutP=(clY<<10)+clX;

 YAdjust=((GlobalTextAddrY)<<11)+(GlobalTextAddrX<<1);


#ifdef FASTSOLID

 if(!bCheckMask && !DrawSemiTrans && !iDither)
  {
   for (i=ymin;i<=ymax;i++)
    {
     xmin=(left_x >> 16);
     xmax=(right_x >> 16);

     if(xmax>=xmin)
      {
       posX=left_u;
       posY=left_v;

       num=(xmax-xmin);
       if(num==0) num=1;
       difX=(right_u-posX)/num;
       difY=(right_v-posY)/num;
       difX2=difX<<1;
       difY2=difY<<1;

       cR1=left_R;
       cG1=left_G;
       cB1=left_B;
       difR=(right_R-cR1)/num;
       difG=(right_G-cG1)/num;
       difB=(right_B-cB1)/num;
       difR2=difR<<1;
       difG2=difG<<1;
       difB2=difB<<1;

       if(xmin<drawX)
        {j=drawX-xmin;xmin=drawX;posX+=j*difX;posY+=j*difY;cR1+=j*difR;cG1+=j*difG;cB1+=j*difB;}
       xmax--;if(drawW<xmax) xmax=drawW;

       for(j=xmin;j<xmax;j+=2)
        {
         XAdjust=(posX>>16);
         tC1 = psxVub[((posY>>5)&(int32_t)0xFFFFF800)+YAdjust+(XAdjust>>1)];
         tC1=(tC1>>((XAdjust&1)<<2))&0xf;
         XAdjust=((posX+difX)>>16);
         tC2 = psxVub[(((posY+difY)>>5)&(int32_t)0xFFFFF800)+YAdjust+
                       (XAdjust>>1)];
         tC2=(tC2>>((XAdjust&1)<<2))&0xf;

         GetTextureTransColGX32_S((uint32_t *)&psxVuw[(i<<10)+j],
              GETLE16(&psxVuw[clutP+tC1])|
              ((int32_t)GETLE16(&psxVuw[clutP+tC2]))<<16,
              (cB1>>16)|((cB1+difB)&0xff0000),
              (cG1>>16)|((cG1+difG)&0xff0000),
              (cR1>>16)|((cR1+difR)&0xff0000));
         posX+=difX2;
         posY+=difY2;
         cR1+=difR2;
         cG1+=difG2;
         cB1+=difB2;
        }
       if(j==xmax)
        {
         XAdjust=(posX>>16);
         tC1 = psxVub[((posY>>5)&(int32_t)0xFFFFF800)+YAdjust+
                      (XAdjust>>1)];
         tC1=(tC1>>((XAdjust&1)<<2))&0xf;

         GetTextureTransColGX_S(&psxVuw[(i<<10)+j], 
             GETLE16(&psxVuw[clutP+tC1]),
             (cB1>>16),(cG1>>16),(cR1>>16));
        }
      }
     if(NextRow_GT4()) return;
    }
   return;
  }

#endif

 for (i=ymin;i<=ymax;i++)
  {
   xmin=(left_x >> 16);
   xmax=(right_x >> 16);

   if(xmax>=xmin)
    {
     posX=left_u;
     posY=left_v;

     num=(xmax-xmin);
     if(num==0) num=1;
     difX=(right_u-posX)/num;
     difY=(right_v-posY)/num;
     difX2=difX<<1;
     difY2=difY<<1;

     cR1=left_R;
     cG1=left_G;
     cB1=left_B;
     difR=(right_R-cR1)/num;
     difG=(right_G-cG1)/num;
     difB=(right_B-cB1)/num;
     difR2=difR<<1;
     difG2=difG<<1;
     difB2=difB<<1;

     if(xmin<drawX)
      {j=drawX-xmin;xmin=drawX;posX+=j*difX;posY+=j*difY;cR1+=j*difR;cG1+=j*difG;cB1+=j*difB;}
     xmax--;if(drawW<xmax) xmax=drawW;

     for(j=xmin;j<=xmax;j++)
      {
       XAdjust=(posX>>16);
       tC1 = psxVub[((posY>>5)&(int32_t)0xFFFFF800)+YAdjust+
                    (XAdjust>>1)];
       tC1=(tC1>>((XAdjust&1)<<2))&0xf;
       if(iDither)
        GetTextureTransColGX_Dither(&psxVuw[(i<<10)+j], 
           GETLE16(&psxVuw[clutP+tC1]),
           (cB1>>16),(cG1>>16),(cR1>>16));
       else
        GetTextureTransColGX(&psxVuw[(i<<10)+j], 
           GETLE16(&psxVuw[clutP+tC1]),
           (cB1>>16),(cG1>>16),(cR1>>16));
       posX+=difX;
       posY+=difY;
       cR1+=difR;
       cG1+=difG;
       cB1+=difB;
      }
    }
   if(NextRow_GT4()) return;
  }
}

////////////////////////////////////////////////////////////////////////

void drawPoly4TGEx4_TW(short x1, short y1, short x2, short y2, short x3, short y3, short x4, short y4, 
                    short tx1, short ty1, short tx2, short ty2, short tx3, short ty3, short tx4, short ty4, 
                    short clX, short clY,
                    int32_t col1, int32_t col2, int32_t col3, int32_t col4)
{
 drawPoly3TGEx4_TW(x2,y2,x3,y3,x4,y4,
                   tx2,ty2,tx3,ty3,tx4,ty4,
                   clX,clY,
                   col2,col4,col3);

 drawPoly3TGEx4_TW(x1,y1,x2,y2,x4,y4,
                   tx1,ty1,tx2,ty2,tx4,ty4,
                   clX,clY,
                   col1,col2,col3);
}

////////////////////////////////////////////////////////////////////////
// POLY 3/4 G-SHADED TEX PAL8
////////////////////////////////////////////////////////////////////////

void drawPoly3TGEx8(short x1, short y1, short x2, short y2, short x3, short y3, short tx1, short ty1, short tx2, short ty2, short tx3, short ty3, short clX, short clY,int32_t col1, int32_t col2, int32_t col3)
{
 int i,j,xmin,xmax,ymin,ymax;
 int32_t cR1,cG1,cB1;
 int32_t difR,difB,difG,difR2,difB2,difG2;
 int32_t difX, difY,difX2, difY2;
 int32_t posX,posY,YAdjust,clutP;
 short tC1,tC2;

 if(x1>drawW && x2>drawW && x3>drawW) return;
 if(y1>drawH && y2>drawH && y3>drawH) return;
 if(x1<drawX && x2<drawX && x3<drawX) return;
 if(y1<drawY && y2<drawY && y3<drawY) return;
 if(drawY>=drawH) return;
 if(drawX>=drawW) return; 

 if(!SetupSections_GT(x1,y1,x2,y2,x3,y3,tx1,ty1,tx2,ty2,tx3,ty3,col1,col2,col3)) return;

 ymax=Ymax;

 for(ymin=Ymin;ymin<drawY;ymin++)
  if(NextRow_GT()) return;

 clutP=(clY<<10)+clX;

 YAdjust=((GlobalTextAddrY)<<11)+(GlobalTextAddrX<<1);

 difR=delta_right_R;
 difG=delta_right_G;
 difB=delta_right_B;
 difR2=difR<<1;
 difG2=difG<<1;
 difB2=difB<<1;
 difX=delta_right_u;difX2=difX<<1;
 difY=delta_right_v;difY2=difY<<1;

#ifdef FASTSOLID

 if(!bCheckMask && !DrawSemiTrans && !iDither)
  {
   for (i=ymin;i<=ymax;i++)
    {
     xmin=(left_x >> 16);
     xmax=(right_x >> 16)-1; // !!!!!!!!!!!!!
     if(drawW<xmax) xmax=drawW;

     if(xmax>=xmin)
      {
       posX=left_u;
       posY=left_v;
       cR1=left_R;
       cG1=left_G;
       cB1=left_B;

       if(xmin<drawX)
        {j=drawX-xmin;xmin=drawX;posX+=j*difX;posY+=j*difY;cR1+=j*difR;cG1+=j*difG;cB1+=j*difB;}

       for(j=xmin;j<xmax;j+=2)
        {
         tC1 = psxVub[((posY>>5)&(int32_t)0xFFFFF800)+YAdjust+((posX>>16))];
         tC2 = psxVub[(((posY+difY)>>5)&(int32_t)0xFFFFF800)+YAdjust+
                      (((posX+difX)>>16))];
         GetTextureTransColGX32_S((uint32_t *)&psxVuw[(i<<10)+j],
              GETLE16(&psxVuw[clutP+tC1])|
              ((int32_t)GETLE16(&psxVuw[clutP+tC2]))<<16,
              (cB1>>16)|((cB1+difB)&0xff0000),
              (cG1>>16)|((cG1+difG)&0xff0000),
              (cR1>>16)|((cR1+difR)&0xff0000));
         posX+=difX2;
         posY+=difY2;
         cR1+=difR2;
         cG1+=difG2;
         cB1+=difB2;
        }
       if(j==xmax)
        {
         tC1 = psxVub[((posY>>5)&(int32_t)0xFFFFF800)+YAdjust+((posX>>16))];
         GetTextureTransColGX_S(&psxVuw[(i<<10)+j], 
              GETLE16(&psxVuw[clutP+tC1]),
              (cB1>>16),(cG1>>16),(cR1>>16));
        }
      }
     if(NextRow_GT()) 
      {
       return;
      }
    }
   return;
  }

#endif

 for (i=ymin;i<=ymax;i++)
  {
   xmin=(left_x >> 16);
   xmax=(right_x >> 16)-1; //!!!!!!!!!!!!!!!!!!!!!!!
   if(drawW<xmax) xmax=drawW;

   if(xmax>=xmin)
    {
     posX=left_u;
     posY=left_v;
     cR1=left_R;
     cG1=left_G;
     cB1=left_B;

     if(xmin<drawX)
      {j=drawX-xmin;xmin=drawX;posX+=j*difX;posY+=j*difY;cR1+=j*difR;cG1+=j*difG;cB1+=j*difB;}

     for(j=xmin;j<=xmax;j++)
      {
       tC1 = psxVub[((posY>>5)&(int32_t)0xFFFFF800)+YAdjust+((posX>>16))];
       if(iDither)
        GetTextureTransColGX_Dither(&psxVuw[(i<<10)+j], 
            GETLE16(&psxVuw[clutP+tC1]),
            (cB1>>16),(cG1>>16),(cR1>>16));
       else
        GetTextureTransColGX(&psxVuw[(i<<10)+j], 
            GETLE16(&psxVuw[clutP+tC1]),
            (cB1>>16),(cG1>>16),(cR1>>16));
       posX+=difX;
       posY+=difY;
       cR1+=difR;
       cG1+=difG;
       cB1+=difB;
      }
    }
   if(NextRow_GT()) 
    {
     return;
    }
  }
}

////////////////////////////////////////////////////////////////////////

void drawPoly3TGEx8_IL(short x1, short y1, short x2, short y2, short x3, short y3, short tx1, short ty1, short tx2, short ty2, short tx3, short ty3, short clX, short clY,int32_t col1, int32_t col2, int32_t col3)
{
 int i,j,xmin,xmax,ymin,ymax,n_xi,n_yi,TXV,TXU;
 int32_t cR1,cG1,cB1;
 int32_t difR,difB,difG,difR2,difB2,difG2;
 int32_t difX, difY,difX2, difY2;
 int32_t posX,posY,YAdjust,clutP;
 short tC1,tC2;

 if(x1>drawW && x2>drawW && x3>drawW) return;
 if(y1>drawH && y2>drawH && y3>drawH) return;
 if(x1<drawX && x2<drawX && x3<drawX) return;
 if(y1<drawY && y2<drawY && y3<drawY) return;
 if(drawY>=drawH) return;
 if(drawX>=drawW) return; 

 if(!SetupSections_GT(x1,y1,x2,y2,x3,y3,tx1,ty1,tx2,ty2,tx3,ty3,col1,col2,col3)) return;

 ymax=Ymax;

 for(ymin=Ymin;ymin<drawY;ymin++)
  if(NextRow_GT()) return;

 clutP=(clY<<10)+clX;

 YAdjust=(GlobalTextAddrY<<10)+GlobalTextAddrX;

 difR=delta_right_R;
 difG=delta_right_G;
 difB=delta_right_B;
 difR2=difR<<1;
 difG2=difG<<1;
 difB2=difB<<1;
 difX=delta_right_u;difX2=difX<<1;
 difY=delta_right_v;difY2=difY<<1;

#ifdef FASTSOLID

 if(!bCheckMask && !DrawSemiTrans && !iDither)
  {
   for (i=ymin;i<=ymax;i++)
    {
     xmin=(left_x >> 16);
     xmax=(right_x >> 16)-1; // !!!!!!!!!!!!!
     if(drawW<xmax) xmax=drawW;

     if(xmax>=xmin)
      {
       posX=left_u;
       posY=left_v;
       cR1=left_R;
       cG1=left_G;
       cB1=left_B;

       if(xmin<drawX)
        {j=drawX-xmin;xmin=drawX;posX+=j*difX;posY+=j*difY;cR1+=j*difR;cG1+=j*difG;cB1+=j*difB;}

       for(j=xmin;j<xmax;j+=2)
        {
         TXU=posX>>16;
         TXV=posY>>16;
         n_xi = ( ( TXU >> 1 ) & ~0x78 ) + ( ( TXU << 2 ) & 0x40 ) + ( ( TXV << 3 ) & 0x38 );
         n_yi = ( TXV & ~0x7 ) + ( ( TXU >> 5 ) & 0x7 );

         tC1= (GETLE16(&psxVuw[(n_yi<<10)+YAdjust+n_xi]) >> ((TXU & 0x01)<<3)) & 0xff;

         TXU=(posX+difX)>>16;
         TXV=(posY+difY)>>16;
         n_xi = ( ( TXU >> 1 ) & ~0x78 ) + ( ( TXU << 2 ) & 0x40 ) + ( ( TXV << 3 ) & 0x38 );
         n_yi = ( TXV & ~0x7 ) + ( ( TXU >> 5 ) & 0x7 );

         tC2= (GETLE16(&psxVuw[(n_yi<<10)+YAdjust+n_xi]) >> ((TXU & 0x01)<<3)) & 0xff;

         GetTextureTransColGX32_S((uint32_t *)&psxVuw[(i<<10)+j],
              GETLE16(&psxVuw[clutP+tC1])|
              ((int32_t)GETLE16(&psxVuw[clutP+tC2]))<<16,
              (cB1>>16)|((cB1+difB)&0xff0000),
              (cG1>>16)|((cG1+difG)&0xff0000),
              (cR1>>16)|((cR1+difR)&0xff0000));
         posX+=difX2;
         posY+=difY2;
         cR1+=difR2;
         cG1+=difG2;
         cB1+=difB2;
        }
       if(j==xmax)
        {
         TXU=posX>>16;
         TXV=posY>>16;
         n_xi = ( ( TXU >> 1 ) & ~0x78 ) + ( ( TXU << 2 ) & 0x40 ) + ( ( TXV << 3 ) & 0x38 );
         n_yi = ( TXV & ~0x7 ) + ( ( TXU >> 5 ) & 0x7 );

         tC1= (GETLE16(&psxVuw[(n_yi<<10)+YAdjust+n_xi]) >> ((TXU & 0x01)<<3)) & 0xff;

         GetTextureTransColGX_S(&psxVuw[(i<<10)+j], 
              GETLE16(&psxVuw[clutP+tC1]),
              (cB1>>16),(cG1>>16),(cR1>>16));
        }
      }
     if(NextRow_GT()) 
      {
       return;
      }
    }
   return;
  }

#endif

 for (i=ymin;i<=ymax;i++)
  {
   xmin=(left_x >> 16);
   xmax=(right_x >> 16)-1; //!!!!!!!!!!!!!!!!!!!!!!!
   if(drawW<xmax) xmax=drawW;

   if(xmax>=xmin)
    {
     posX=left_u;
     posY=left_v;
     cR1=left_R;
     cG1=left_G;
     cB1=left_B;

     if(xmin<drawX)
      {j=drawX-xmin;xmin=drawX;posX+=j*difX;posY+=j*difY;cR1+=j*difR;cG1+=j*difG;cB1+=j*difB;}

     for(j=xmin;j<=xmax;j++)
      {
       TXU=posX>>16;
       TXV=posY>>16;
       n_xi = ( ( TXU >> 1 ) & ~0x78 ) + ( ( TXU << 2 ) & 0x40 ) + ( ( TXV << 3 ) & 0x38 );
       n_yi = ( TXV & ~0x7 ) + ( ( TXU >> 5 ) & 0x7 );

       tC1= (GETLE16(&psxVuw[(n_yi<<10)+YAdjust+n_xi]) >> ((TXU & 0x01)<<3)) & 0xff;

       if(iDither)
        GetTextureTransColGX_Dither(&psxVuw[(i<<10)+j], 
            GETLE16(&psxVuw[clutP+tC1]),
            (cB1>>16),(cG1>>16),(cR1>>16));
       else
        GetTextureTransColGX(&psxVuw[(i<<10)+j], 
            GETLE16(&psxVuw[clutP+tC1]),
            (cB1>>16),(cG1>>16),(cR1>>16));
       posX+=difX;
       posY+=difY;
       cR1+=difR;
       cG1+=difG;
       cB1+=difB;
      }
    }
   if(NextRow_GT()) 
    {
     return;
    }
  }
}

////////////////////////////////////////////////////////////////////////

void drawPoly3TGEx8_TW(short x1, short y1, short x2, short y2, short x3, short y3, short tx1, short ty1, short tx2, short ty2, short tx3, short ty3, short clX, short clY,int32_t col1, int32_t col2, int32_t col3)
{
 int i,j,xmin,xmax,ymin,ymax;
 int32_t cR1,cG1,cB1;
 int32_t difR,difB,difG,difR2,difB2,difG2;
 int32_t difX, difY,difX2, difY2;
 int32_t posX,posY,YAdjust,clutP;
 short tC1,tC2;

 if(x1>drawW && x2>drawW && x3>drawW) return;
 if(y1>drawH && y2>drawH && y3>drawH) return;
 if(x1<drawX && x2<drawX && x3<drawX) return;
 if(y1<drawY && y2<drawY && y3<drawY) return;
 if(drawY>=drawH) return;
 if(drawX>=drawW) return; 

 if(!SetupSections_GT(x1,y1,x2,y2,x3,y3,tx1,ty1,tx2,ty2,tx3,ty3,col1,col2,col3)) return;

 ymax=Ymax;

 for(ymin=Ymin;ymin<drawY;ymin++)
  if(NextRow_GT()) return;

 clutP=(clY<<10)+clX;

 YAdjust=((GlobalTextAddrY)<<11)+(GlobalTextAddrX<<1);
 YAdjust+=(TWin.Position.y0<<11)+(TWin.Position.x0);

 difR=delta_right_R;
 difG=delta_right_G;
 difB=delta_right_B;
 difR2=difR<<1;
 difG2=difG<<1;
 difB2=difB<<1;
 difX=delta_right_u;difX2=difX<<1;
 difY=delta_right_v;difY2=difY<<1;

#ifdef FASTSOLID

 if(!bCheckMask && !DrawSemiTrans && !iDither)
  {
   for (i=ymin;i<=ymax;i++)
    {
     xmin=(left_x >> 16);
     xmax=(right_x >> 16)-1; // !!!!!!!!!!!!!
     if(drawW<xmax) xmax=drawW;

     if(xmax>=xmin)
      {
       posX=left_u;
       posY=left_v;
       cR1=left_R;
       cG1=left_G;
       cB1=left_B;

       if(xmin<drawX)
        {j=drawX-xmin;xmin=drawX;posX+=j*difX;posY+=j*difY;cR1+=j*difR;cG1+=j*difG;cB1+=j*difB;}

       for(j=xmin;j<xmax;j+=2)
        {
         tC1 = psxVub[(((posY>>16)%TWin.Position.y1)<<11)+
                      YAdjust+((posX>>16)%TWin.Position.x1)];
         tC2 = psxVub[((((posY+difY)>>16)%TWin.Position.y1)<<11)+
                      YAdjust+(((posX+difX)>>16)%TWin.Position.x1)];
                      
         GetTextureTransColGX32_S((uint32_t *)&psxVuw[(i<<10)+j],
              GETLE16(&psxVuw[clutP+tC1])|
              ((int32_t)GETLE16(&psxVuw[clutP+tC2]))<<16,
              (cB1>>16)|((cB1+difB)&0xff0000),
              (cG1>>16)|((cG1+difG)&0xff0000),
              (cR1>>16)|((cR1+difR)&0xff0000));
         posX+=difX2;
         posY+=difY2;
         cR1+=difR2;
         cG1+=difG2;
         cB1+=difB2;
        }
       if(j==xmax)
        {
         tC1 = psxVub[(((posY>>16)%TWin.Position.y1)<<11)+
                      YAdjust+((posX>>16)%TWin.Position.x1)];
         GetTextureTransColGX_S(&psxVuw[(i<<10)+j], 
              GETLE16(&psxVuw[clutP+tC1]),
              (cB1>>16),(cG1>>16),(cR1>>16));
        }
      }
     if(NextRow_GT()) 
      {
       return;
      }
    }
   return;
  }

#endif

 for (i=ymin;i<=ymax;i++)
  {
   xmin=(left_x >> 16);
   xmax=(right_x >> 16)-1; //!!!!!!!!!!!!!!!!!!!!!!!
   if(drawW<xmax) xmax=drawW;

   if(xmax>=xmin)
    {
     posX=left_u;
     posY=left_v;
     cR1=left_R;
     cG1=left_G;
     cB1=left_B;

     if(xmin<drawX)
      {j=drawX-xmin;xmin=drawX;posX+=j*difX;posY+=j*difY;cR1+=j*difR;cG1+=j*difG;cB1+=j*difB;}

     for(j=xmin;j<=xmax;j++)
      {
       tC1 = psxVub[(((posY>>16)%TWin.Position.y1)<<11)+
                    YAdjust+((posX>>16)%TWin.Position.x1)];
       if(iDither)
        GetTextureTransColGX_Dither(&psxVuw[(i<<10)+j], 
            GETLE16(&psxVuw[clutP+tC1]),
            (cB1>>16),(cG1>>16),(cR1>>16));
       else
        GetTextureTransColGX(&psxVuw[(i<<10)+j], 
            GETLE16(&psxVuw[clutP+tC1]),
            (cB1>>16),(cG1>>16),(cR1>>16));
       posX+=difX;
       posY+=difY;
       cR1+=difR;
       cG1+=difG;
       cB1+=difB;
      }
    }
   if(NextRow_GT()) 
    {
     return;
    }
  }
}

////////////////////////////////////////////////////////////////////////

// note: two g-shaded tris: small texture distortions can happen

void drawPoly4TGEx8_TRI_IL(short x1, short y1, short x2, short y2, short x3, short y3, short x4, short y4, 
                           short tx1, short ty1, short tx2, short ty2, short tx3, short ty3, short tx4, short ty4, 
                           short clX, short clY,
                           int32_t col1, int32_t col2, int32_t col3, int32_t col4)
{
 drawPoly3TGEx8_IL(x2,y2,x3,y3,x4,y4,
                   tx2,ty2,tx3,ty3,tx4,ty4,
                   clX,clY,
                   col2,col4,col3);
 drawPoly3TGEx8_IL(x1,y1,x2,y2,x4,y4,
                   tx1,ty1,tx2,ty2,tx4,ty4,
                   clX,clY,
                   col1,col2,col3);
}

#ifdef POLYQUAD3GT
                      
void drawPoly4TGEx8_TRI(short x1, short y1, short x2, short y2, short x3, short y3, short x4, short y4, 
                   short tx1, short ty1, short tx2, short ty2, short tx3, short ty3, short tx4, short ty4, 
                   short clX, short clY,
                   int32_t col1, int32_t col2, int32_t col3, int32_t col4)
{
 drawPoly3TGEx8(x2,y2,x3,y3,x4,y4,
                tx2,ty2,tx3,ty3,tx4,ty4,
                clX,clY,
                col2,col4,col3);
 drawPoly3TGEx8(x1,y1,x2,y2,x4,y4,
                tx1,ty1,tx2,ty2,tx4,ty4,
                clX,clY,
                col1,col2,col3);
}

#endif

void drawPoly4TGEx8(short x1, short y1, short x2, short y2, short x3, short y3, short x4, short y4, 
                   short tx1, short ty1, short tx2, short ty2, short tx3, short ty3, short tx4, short ty4, 
                   short clX, short clY,
                   int32_t col1, int32_t col2, int32_t col4, int32_t col3)
{
 int32_t num; 
 int32_t i,j,xmin,xmax,ymin,ymax;
 int32_t cR1,cG1,cB1;
 int32_t difR,difB,difG,difR2,difB2,difG2;
 int32_t difX, difY, difX2, difY2;
 int32_t posX,posY,YAdjust,clutP;
 short tC1,tC2;

 if(x1>drawW && x2>drawW && x3>drawW && x4>drawW) return;
 if(y1>drawH && y2>drawH && y3>drawH && y4>drawH) return;
 if(x1<drawX && x2<drawX && x3<drawX && x4<drawX) return;
 if(y1<drawY && y2<drawY && y3<drawY && y4<drawY) return;
 if(drawY>=drawH) return;
 if(drawX>=drawW) return; 

 if(!SetupSections_GT4(x1,y1,x2,y2,x3,y3,x4,y4,tx1,ty1,tx2,ty2,tx3,ty3,tx4,ty4,col1,col2,col3,col4)) return;

 ymax=Ymax;

 for(ymin=Ymin;ymin<drawY;ymin++)
  if(NextRow_GT4()) return;

 clutP=(clY<<10)+clX;

 YAdjust=((GlobalTextAddrY)<<11)+(GlobalTextAddrX<<1);

#ifdef FASTSOLID

 if(!bCheckMask && !DrawSemiTrans && !iDither)
  {
   for (i=ymin;i<=ymax;i++)
    {
     xmin=(left_x >> 16);
     xmax=(right_x >> 16);

     if(xmax>=xmin)
      {
       posX=left_u;
       posY=left_v;

       num=(xmax-xmin);
       if(num==0) num=1;
       difX=(right_u-posX)/num;
       difY=(right_v-posY)/num;
       difX2=difX<<1;
       difY2=difY<<1;

       cR1=left_R;
       cG1=left_G;
       cB1=left_B;
       difR=(right_R-cR1)/num;
       difG=(right_G-cG1)/num;
       difB=(right_B-cB1)/num;
       difR2=difR<<1;
       difG2=difG<<1;
       difB2=difB<<1;

       if(xmin<drawX)
        {j=drawX-xmin;xmin=drawX;posX+=j*difX;posY+=j*difY;cR1+=j*difR;cG1+=j*difG;cB1+=j*difB;}
       xmax--;if(drawW<xmax) xmax=drawW;

       for(j=xmin;j<xmax;j+=2)
        {
         tC1 = psxVub[((posY>>5)&(int32_t)0xFFFFF800)+YAdjust+(posX>>16)];
         tC2 = psxVub[(((posY+difY)>>5)&(int32_t)0xFFFFF800)+YAdjust+
                     ((posX+difX)>>16)];

         GetTextureTransColGX32_S((uint32_t *)&psxVuw[(i<<10)+j],
              GETLE16(&psxVuw[clutP+tC1])|
              ((int32_t)GETLE16(&psxVuw[clutP+tC2]))<<16,
              (cB1>>16)|((cB1+difB)&0xff0000),
              (cG1>>16)|((cG1+difG)&0xff0000),
              (cR1>>16)|((cR1+difR)&0xff0000));
         posX+=difX2;
         posY+=difY2;
         cR1+=difR2;
         cG1+=difG2;
         cB1+=difB2;
        }
       if(j==xmax)
        {
         tC1 = psxVub[((posY>>5)&(int32_t)0xFFFFF800)+YAdjust+(posX>>16)];
         GetTextureTransColGX_S(&psxVuw[(i<<10)+j], 
              GETLE16(&psxVuw[clutP+tC1]),
             (cB1>>16),(cG1>>16),(cR1>>16));
        }
      }
     if(NextRow_GT4()) return;
    }
   return;
  }

#endif

 for (i=ymin;i<=ymax;i++)
  {
   xmin=(left_x >> 16);
   xmax=(right_x >> 16);

   if(xmax>=xmin)
    {
     posX=left_u;
     posY=left_v;

     num=(xmax-xmin);
     if(num==0) num=1;
     difX=(right_u-posX)/num;
     difY=(right_v-posY)/num;
     difX2=difX<<1;
     difY2=difY<<1;

     cR1=left_R;
     cG1=left_G;
     cB1=left_B;
     difR=(right_R-cR1)/num;
     difG=(right_G-cG1)/num;
     difB=(right_B-cB1)/num;
     difR2=difR<<1;
     difG2=difG<<1;
     difB2=difB<<1;

     if(xmin<drawX)
      {j=drawX-xmin;xmin=drawX;posX+=j*difX;posY+=j*difY;cR1+=j*difR;cG1+=j*difG;cB1+=j*difB;}
     xmax--;if(drawW<xmax) xmax=drawW;

     for(j=xmin;j<=xmax;j++)
      {
       tC1 = psxVub[((posY>>5)&(int32_t)0xFFFFF800)+YAdjust+(posX>>16)];
       if(iDither)
        GetTextureTransColGX_Dither(&psxVuw[(i<<10)+j], 
            GETLE16(&psxVuw[clutP+tC1]),
           (cB1>>16),(cG1>>16),(cR1>>16));
       else
        GetTextureTransColGX(&psxVuw[(i<<10)+j], 
            GETLE16(&psxVuw[clutP+tC1]),
           (cB1>>16),(cG1>>16),(cR1>>16));
       posX+=difX;
       posY+=difY;
       cR1+=difR;
       cG1+=difG;
       cB1+=difB;
      }
    }
   if(NextRow_GT4()) return;
  }
}

////////////////////////////////////////////////////////////////////////

void drawPoly4TGEx8_TW(short x1, short y1, short x2, short y2, short x3, short y3, short x4, short y4, 
                   short tx1, short ty1, short tx2, short ty2, short tx3, short ty3, short tx4, short ty4, 
                   short clX, short clY,
                   int32_t col1, int32_t col2, int32_t col3, int32_t col4)
{
 drawPoly3TGEx8_TW(x2,y2,x3,y3,x4,y4,
                tx2,ty2,tx3,ty3,tx4,ty4,
                clX,clY,
                col2,col4,col3);
 drawPoly3TGEx8_TW(x1,y1,x2,y2,x4,y4,
                tx1,ty1,tx2,ty2,tx4,ty4,
                clX,clY,
                col1,col2,col3);
}

////////////////////////////////////////////////////////////////////////
// POLY 3 G-SHADED TEX 15 BIT
////////////////////////////////////////////////////////////////////////

void drawPoly3TGD(short x1, short y1, short x2, short y2, short x3, short y3, short tx1, short ty1, short tx2, short ty2, short tx3, short ty3,int32_t col1, int32_t col2, int32_t col3)
{
 int i,j,xmin,xmax,ymin,ymax;
 int32_t cR1,cG1,cB1;
 int32_t difR,difB,difG,difR2,difB2,difG2;
 int32_t difX, difY,difX2, difY2;
 int32_t posX,posY;

 if(x1>drawW && x2>drawW && x3>drawW) return;
 if(y1>drawH && y2>drawH && y3>drawH) return;
 if(x1<drawX && x2<drawX && x3<drawX) return;
 if(y1<drawY && y2<drawY && y3<drawY) return;
 if(drawY>=drawH) return;
 if(drawX>=drawW) return; 

 if(!SetupSections_GT(x1,y1,x2,y2,x3,y3,tx1,ty1,tx2,ty2,tx3,ty3,col1,col2,col3)) return;

 ymax=Ymax;

 for(ymin=Ymin;ymin<drawY;ymin++)
  if(NextRow_GT()) return;

 difR=delta_right_R;
 difG=delta_right_G;
 difB=delta_right_B;
 difR2=difR<<1;
 difG2=difG<<1;
 difB2=difB<<1;
 difX=delta_right_u;difX2=difX<<1;
 difY=delta_right_v;difY2=difY<<1;

#ifdef FASTSOLID

 if(!bCheckMask && !DrawSemiTrans && !iDither)
  {       
   for (i=ymin;i<=ymax;i++)
    {
     xmin=(left_x >> 16);
     xmax=(right_x >> 16)-1; //!!!!!!!!!!!!!!!!!!!!
     if(drawW<xmax) xmax=drawW;

     if(xmax>=xmin)
      {
       posX=left_u;
       posY=left_v;
       cR1=left_R;
       cG1=left_G;
       cB1=left_B;

       if(xmin<drawX)
        {j=drawX-xmin;xmin=drawX;posX+=j*difX;posY+=j*difY;cR1+=j*difR;cG1+=j*difG;cB1+=j*difB;}

       for(j=xmin;j<xmax;j+=2)
        {
         GetTextureTransColGX32_S((uint32_t *)&psxVuw[(i<<10)+j],
              (((int32_t)GETLE16(&psxVuw[((((posY+difY)>>16)+GlobalTextAddrY)<<10)+((posX+difX)>>16)+GlobalTextAddrX]))<<16)|
              GETLE16(&psxVuw[(((posY>>16)+GlobalTextAddrY)<<10)+((posX)>>16)+GlobalTextAddrX]),
              (cB1>>16)|((cB1+difB)&0xff0000),
              (cG1>>16)|((cG1+difG)&0xff0000),
              (cR1>>16)|((cR1+difR)&0xff0000));
         posX+=difX2;
         posY+=difY2;
         cR1+=difR2;
         cG1+=difG2;
         cB1+=difB2;
        }
       if(j==xmax)
        GetTextureTransColGX_S(&psxVuw[(i<<10)+j],
            GETLE16(&psxVuw[(((posY>>16)+GlobalTextAddrY)<<10)+(posX>>16)+GlobalTextAddrX]),
            (cB1>>16),(cG1>>16),(cR1>>16));
      }
     if(NextRow_GT()) 
      {
       return;
      }
    }
   return;
  }

#endif

 for (i=ymin;i<=ymax;i++)
  {
   xmin=(left_x >> 16);
   xmax=(right_x >> 16)-1; //!!!!!!!!!!!!!!!!!!
   if(drawW<xmax) xmax=drawW;

   if(xmax>=xmin)
    {
     posX=left_u;
     posY=left_v;
     cR1=left_R;
     cG1=left_G;
     cB1=left_B;

     if(xmin<drawX)
      {j=drawX-xmin;xmin=drawX;posX+=j*difX;posY+=j*difY;cR1+=j*difR;cG1+=j*difG;cB1+=j*difB;}

     for(j=xmin;j<=xmax;j++)
      {
       if(iDither)
        GetTextureTransColGX_Dither(&psxVuw[(i<<10)+j],
          GETLE16(&psxVuw[(((posY>>16)+GlobalTextAddrY)<<10)+(posX>>16)+GlobalTextAddrX]),
          (cB1>>16),(cG1>>16),(cR1>>16));
       else
        GetTextureTransColGX(&psxVuw[(i<<10)+j],
          GETLE16(&psxVuw[(((posY>>16)+GlobalTextAddrY)<<10)+(posX>>16)+GlobalTextAddrX]),
          (cB1>>16),(cG1>>16),(cR1>>16));
       posX+=difX;
       posY+=difY;
       cR1+=difR;
       cG1+=difG;
       cB1+=difB;
      }
    }
   if(NextRow_GT()) 
    {
     return;
    }
  }
}

////////////////////////////////////////////////////////////////////////

void drawPoly3TGD_TW(short x1, short y1, short x2, short y2, short x3, short y3, short tx1, short ty1, short tx2, short ty2, short tx3, short ty3,int32_t col1, int32_t col2, int32_t col3)
{
 int i,j,xmin,xmax,ymin,ymax;
 int32_t cR1,cG1,cB1;
 int32_t difR,difB,difG,difR2,difB2,difG2;
 int32_t difX, difY,difX2, difY2;
 int32_t posX,posY;

 if(x1>drawW && x2>drawW && x3>drawW) return;
 if(y1>drawH && y2>drawH && y3>drawH) return;
 if(x1<drawX && x2<drawX && x3<drawX) return;
 if(y1<drawY && y2<drawY && y3<drawY) return;
 if(drawY>=drawH) return;
 if(drawX>=drawW) return; 

 if(!SetupSections_GT(x1,y1,x2,y2,x3,y3,tx1,ty1,tx2,ty2,tx3,ty3,col1,col2,col3)) return;

 ymax=Ymax;

 for(ymin=Ymin;ymin<drawY;ymin++)
  if(NextRow_GT()) return;

 difR=delta_right_R;
 difG=delta_right_G;
 difB=delta_right_B;
 difR2=difR<<1;
 difG2=difG<<1;
 difB2=difB<<1;
 difX=delta_right_u;difX2=difX<<1;
 difY=delta_right_v;difY2=difY<<1;

#ifdef FASTSOLID

 if(!bCheckMask && !DrawSemiTrans && !iDither)
  {       
   for (i=ymin;i<=ymax;i++)
    {
     xmin=(left_x >> 16);
     xmax=(right_x >> 16)-1; //!!!!!!!!!!!!!!!!!!!!
     if(drawW<xmax) xmax=drawW;

     if(xmax>=xmin)
      {
       posX=left_u;
       posY=left_v;
       cR1=left_R;
       cG1=left_G;
       cB1=left_B;

       if(xmin<drawX)
        {j=drawX-xmin;xmin=drawX;posX+=j*difX;posY+=j*difY;cR1+=j*difR;cG1+=j*difG;cB1+=j*difB;}

       for(j=xmin;j<xmax;j+=2)
        {
         GetTextureTransColGX32_S((uint32_t *)&psxVuw[(i<<10)+j],
              (((int32_t)GETLE16(&psxVuw[(((((posY+difY)>>16)%TWin.Position.y1)+GlobalTextAddrY+TWin.Position.y0)<<10)+
                             (((posX+difX)>>16)%TWin.Position.x1)+GlobalTextAddrX+TWin.Position.x0]))<<16)|
              GETLE16(&psxVuw[((((posY>>16)%TWin.Position.y1)+GlobalTextAddrY+TWin.Position.y0)<<10)+
                     (((posX)>>16)%TWin.Position.x1)+GlobalTextAddrX+TWin.Position.x0]),
              (cB1>>16)|((cB1+difB)&0xff0000),
              (cG1>>16)|((cG1+difG)&0xff0000),
              (cR1>>16)|((cR1+difR)&0xff0000));
         posX+=difX2;
         posY+=difY2;
         cR1+=difR2;
         cG1+=difG2;
         cB1+=difB2;
        }
       if(j==xmax)
        GetTextureTransColGX_S(&psxVuw[(i<<10)+j],
            GETLE16(&psxVuw[((((posY>>16)%TWin.Position.y1)+GlobalTextAddrY+TWin.Position.y0)<<10)+
                   ((posX>>16)%TWin.Position.x1)+GlobalTextAddrX+TWin.Position.x0]),
            (cB1>>16),(cG1>>16),(cR1>>16));
      }
     if(NextRow_GT()) 
      {
       return;
      }
    }
   return;
  }

#endif

 for (i=ymin;i<=ymax;i++)
  {
   xmin=(left_x >> 16);
   xmax=(right_x >> 16)-1; //!!!!!!!!!!!!!!!!!!
   if(drawW<xmax) xmax=drawW;

   if(xmax>=xmin)
    {
     posX=left_u;
     posY=left_v;
     cR1=left_R;
     cG1=left_G;
     cB1=left_B;

     if(xmin<drawX)
      {j=drawX-xmin;xmin=drawX;posX+=j*difX;posY+=j*difY;cR1+=j*difR;cG1+=j*difG;cB1+=j*difB;}

     for(j=xmin;j<=xmax;j++)
      {
       if(iDither)
        GetTextureTransColGX_Dither(&psxVuw[(i<<10)+j],
          GETLE16(&psxVuw[((((posY>>16)%TWin.Position.y1)+GlobalTextAddrY+TWin.Position.y0)<<10)+
                 ((posX>>16)%TWin.Position.x1)+GlobalTextAddrX+TWin.Position.x0]),
          (cB1>>16),(cG1>>16),(cR1>>16));
       else
        GetTextureTransColGX(&psxVuw[(i<<10)+j],
          GETLE16(&psxVuw[((((posY>>16)%TWin.Position.y1)+GlobalTextAddrY+TWin.Position.y0)<<10)+
                 ((posX>>16)%TWin.Position.x1)+GlobalTextAddrX+TWin.Position.x0]),
          (cB1>>16),(cG1>>16),(cR1>>16));
       posX+=difX;
       posY+=difY;
       cR1+=difR;
       cG1+=difG;
       cB1+=difB;
      }
    }
   if(NextRow_GT()) 
    {
     return;
    }
  }
}

////////////////////////////////////////////////////////////////////////

// note: two g-shaded tris: small texture distortions can happen

#ifdef POLYQUAD3GT

void drawPoly4TGD_TRI(short x1, short y1, short x2, short y2, short x3, short y3, short x4, short y4, short tx1, short ty1, short tx2, short ty2, short tx3, short ty3, short tx4, short ty4, int32_t col1, int32_t col2, int32_t col3, int32_t col4)
{
 drawPoly3TGD(x2,y2,x3,y3,x4,y4,
              tx2,ty2,tx3,ty3,tx4,ty4,
              col2,col4,col3);
 drawPoly3TGD(x1,y1,x2,y2,x4,y4,
              tx1,ty1,tx2,ty2,tx4,ty4,
              col1,col2,col3);
}

#endif

void drawPoly4TGD(short x1, short y1, short x2, short y2, short x3, short y3, short x4, short y4, short tx1, short ty1, short tx2, short ty2, short tx3, short ty3, short tx4, short ty4, int32_t col1, int32_t col2, int32_t col4, int32_t col3)
{
 int32_t num; 
 int32_t i,j,xmin,xmax,ymin,ymax;
 int32_t cR1,cG1,cB1;
 int32_t difR,difB,difG,difR2,difB2,difG2;
 int32_t difX, difY, difX2, difY2;
 int32_t posX,posY;

 if(x1>drawW && x2>drawW && x3>drawW && x4>drawW) return;
 if(y1>drawH && y2>drawH && y3>drawH && y4>drawH) return;
 if(x1<drawX && x2<drawX && x3<drawX && x4<drawX) return;
 if(y1<drawY && y2<drawY && y3<drawY && y4<drawY) return;
 if(drawY>=drawH) return;
 if(drawX>=drawW) return; 

 if(!SetupSections_GT4(x1,y1,x2,y2,x3,y3,x4,y4,tx1,ty1,tx2,ty2,tx3,ty3,tx4,ty4,col1,col2,col3,col4)) return;

 ymax=Ymax;

 for(ymin=Ymin;ymin<drawY;ymin++)
  if(NextRow_GT4()) return;

#ifdef FASTSOLID

 if(!bCheckMask && !DrawSemiTrans && !iDither)
  {
   for (i=ymin;i<=ymax;i++)
    {
     xmin=(left_x >> 16);
     xmax=(right_x >> 16);

     if(xmax>=xmin)
      {
       posX=left_u;
       posY=left_v;

       num=(xmax-xmin);
       if(num==0) num=1;
       difX=(right_u-posX)/num;
       difY=(right_v-posY)/num;
       difX2=difX<<1;
       difY2=difY<<1;

       cR1=left_R;
       cG1=left_G;
       cB1=left_B;
       difR=(right_R-cR1)/num;
       difG=(right_G-cG1)/num;
       difB=(right_B-cB1)/num;
       difR2=difR<<1;
       difG2=difG<<1;
       difB2=difB<<1;

       if(xmin<drawX)
        {j=drawX-xmin;xmin=drawX;posX+=j*difX;posY+=j*difY;cR1+=j*difR;cG1+=j*difG;cB1+=j*difB;}
       xmax--;if(drawW<xmax) xmax=drawW;

       for(j=xmin;j<xmax;j+=2)
        {
         GetTextureTransColGX32_S((uint32_t *)&psxVuw[(i<<10)+j],
              (((int32_t)GETLE16(&psxVuw[((((posY+difY)>>16)+GlobalTextAddrY)<<10)+((posX+difX)>>16)+GlobalTextAddrX]))<<16)|
              GETLE16(&psxVuw[(((posY>>16)+GlobalTextAddrY)<<10)+((posX)>>16)+GlobalTextAddrX]),
              (cB1>>16)|((cB1+difB)&0xff0000),
              (cG1>>16)|((cG1+difG)&0xff0000),
              (cR1>>16)|((cR1+difR)&0xff0000));
         posX+=difX2;
         posY+=difY2;
         cR1+=difR2;
         cG1+=difG2;
         cB1+=difB2;
        }
       if(j==xmax)
        GetTextureTransColGX_S(&psxVuw[(i<<10)+j],
            GETLE16(&psxVuw[(((posY>>16)+GlobalTextAddrY)<<10)+(posX>>16)+GlobalTextAddrX]),
            (cB1>>16),(cG1>>16),(cR1>>16));
      }
     if(NextRow_GT4()) return;
    }
   return;
  }

#endif

 for (i=ymin;i<=ymax;i++)
  {
   xmin=(left_x >> 16);
   xmax=(right_x >> 16);

   if(xmax>=xmin)
    {
     posX=left_u;
     posY=left_v;

     num=(xmax-xmin);
     if(num==0) num=1;
     difX=(right_u-posX)/num;
     difY=(right_v-posY)/num;
     difX2=difX<<1;
     difY2=difY<<1;

     cR1=left_R;
     cG1=left_G;
     cB1=left_B;
     difR=(right_R-cR1)/num;
     difG=(right_G-cG1)/num;
     difB=(right_B-cB1)/num;
     difR2=difR<<1;
     difG2=difG<<1;
     difB2=difB<<1;

     if(xmin<drawX)
      {j=drawX-xmin;xmin=drawX;posX+=j*difX;posY+=j*difY;cR1+=j*difR;cG1+=j*difG;cB1+=j*difB;}
     xmax--;if(drawW<xmax) xmax=drawW;

     for(j=xmin;j<=xmax;j++)
      {
       if(iDither)
        GetTextureTransColGX(&psxVuw[(i<<10)+j],
          GETLE16(&psxVuw[(((posY>>16)+GlobalTextAddrY)<<10)+(posX>>16)+GlobalTextAddrX]),
          (cB1>>16),(cG1>>16),(cR1>>16));
       else
        GetTextureTransColGX(&psxVuw[(i<<10)+j],
          GETLE16(&psxVuw[(((posY>>16)+GlobalTextAddrY)<<10)+(posX>>16)+GlobalTextAddrX]),
          (cB1>>16),(cG1>>16),(cR1>>16));
       posX+=difX;
       posY+=difY;
       cR1+=difR;
       cG1+=difG;
       cB1+=difB;
      }
    }
   if(NextRow_GT4()) return;
  }
}

////////////////////////////////////////////////////////////////////////

void drawPoly4TGD_TW(short x1, short y1, short x2, short y2, short x3, short y3, short x4, short y4, short tx1, short ty1, short tx2, short ty2, short tx3, short ty3, short tx4, short ty4, int32_t col1, int32_t col2, int32_t col3, int32_t col4)
{
 drawPoly3TGD_TW(x2,y2,x3,y3,x4,y4,
              tx2,ty2,tx3,ty3,tx4,ty4,
              col2,col4,col3);
 drawPoly3TGD_TW(x1,y1,x2,y2,x4,y4,
              tx1,ty1,tx2,ty2,tx4,ty4,
              col1,col2,col3);
}

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////


/*
// no real rect test, but it does its job the way I need it
__inline BOOL IsNoRect(void)
{
 if(lx0==lx1 && lx2==lx3) return FALSE;
 if(lx0==lx2 && lx1==lx3) return FALSE;
 if(lx0==lx3 && lx1==lx2) return FALSE;
 return TRUE;                      
}
*/

// real rect test
__inline BOOL IsNoRect(void)
{
 if(!(dwActFixes&0x200)) return FALSE;

 if(ly0==ly1)
  {
   if(lx1==lx3 && ly3==ly2 && lx2==lx0) return FALSE;
   if(lx1==lx2 && ly2==ly3 && lx3==lx0) return FALSE;
   return TRUE;
  }
 
 if(ly0==ly2)
  {
   if(lx2==lx3 && ly3==ly1 && lx1==lx0) return FALSE;
   if(lx2==lx1 && ly1==ly3 && lx3==lx0) return FALSE;
   return TRUE;
  }
 
 if(ly0==ly3)
  {
   if(lx3==lx2 && ly2==ly1 && lx1==lx0) return FALSE;
   if(lx3==lx1 && ly1==ly2 && lx2==lx0) return FALSE;
   return TRUE;
  }
 return TRUE;
}

////////////////////////////////////////////////////////////////////////

void drawPoly3FT(unsigned char * baseAddr)
{
 uint32_t *gpuData = ((uint32_t *) baseAddr);

 if(GlobalTextIL && GlobalTextTP<2)
  {
   if(GlobalTextTP==0)
    drawPoly3TEx4_IL(lx0,ly0,lx1,ly1,lx2,ly2,
                     (GETLE32(&gpuData[2]) & 0x000000ff), ((GETLE32(&gpuData[2])>>8) & 0x000000ff), (GETLE32(&gpuData[4]) & 0x000000ff), ((GETLE32(&gpuData[4])>>8) & 0x000000ff),(GETLE32(&gpuData[6]) & 0x000000ff), ((GETLE32(&gpuData[6])>>8) & 0x000000ff), 
                     ((GETLE32(&gpuData[2])>>12) & 0x3f0), ((GETLE32(&gpuData[2])>>22) & iGPUHeightMask));
   else
    drawPoly3TEx8_IL(lx0,ly0,lx1,ly1,lx2,ly2,
                     (GETLE32(&gpuData[2]) & 0x000000ff), ((GETLE32(&gpuData[2])>>8) & 0x000000ff), (GETLE32(&gpuData[4]) & 0x000000ff), ((GETLE32(&gpuData[4])>>8) & 0x000000ff),(GETLE32(&gpuData[6]) & 0x000000ff), ((GETLE32(&gpuData[6])>>8) & 0x000000ff), 
                     ((GETLE32(&gpuData[2])>>12) & 0x3f0), ((GETLE32(&gpuData[2])>>22) & iGPUHeightMask));
   return;
  }

 if(!bUsingTWin && !(dwActFixes&0x100))
  {
   switch(GlobalTextTP)   // depending on texture mode
    {
     case 0:
      drawPoly3TEx4(lx0,ly0,lx1,ly1,lx2,ly2,
                    (GETLE32(&gpuData[2]) & 0x000000ff), ((GETLE32(&gpuData[2])>>8) & 0x000000ff), (GETLE32(&gpuData[4]) & 0x000000ff), ((GETLE32(&gpuData[4])>>8) & 0x000000ff),(GETLE32(&gpuData[6]) & 0x000000ff), ((GETLE32(&gpuData[6])>>8) & 0x000000ff), 
                    ((GETLE32(&gpuData[2])>>12) & 0x3f0), ((GETLE32(&gpuData[2])>>22) & iGPUHeightMask));
      return;
     case 1:
      drawPoly3TEx8(lx0,ly0,lx1,ly1,lx2,ly2,
                    (GETLE32(&gpuData[2]) & 0x000000ff), ((GETLE32(&gpuData[2])>>8) & 0x000000ff), (GETLE32(&gpuData[4]) & 0x000000ff), ((GETLE32(&gpuData[4])>>8) & 0x000000ff),(GETLE32(&gpuData[6]) & 0x000000ff), ((GETLE32(&gpuData[6])>>8) & 0x000000ff), 
                    ((GETLE32(&gpuData[2])>>12) & 0x3f0), ((GETLE32(&gpuData[2])>>22) & iGPUHeightMask));
      return;
     case 2:
      drawPoly3TD(lx0,ly0,lx1,ly1,lx2,ly2,(GETLE32(&gpuData[2]) & 0x000000ff), ((GETLE32(&gpuData[2])>>8) & 0x000000ff), (GETLE32(&gpuData[4]) & 0x000000ff), ((GETLE32(&gpuData[4])>>8) & 0x000000ff),(GETLE32(&gpuData[6]) & 0x000000ff), ((GETLE32(&gpuData[6])>>8) & 0x000000ff));
      return;
    }
   return;
  }

 switch(GlobalTextTP)   // depending on texture mode
  {
   case 0:
    drawPoly3TEx4_TW(lx0,ly0,lx1,ly1,lx2,ly2,
                     (GETLE32(&gpuData[2]) & 0x000000ff), ((GETLE32(&gpuData[2])>>8) & 0x000000ff), (GETLE32(&gpuData[4]) & 0x000000ff), ((GETLE32(&gpuData[4])>>8) & 0x000000ff),(GETLE32(&gpuData[6]) & 0x000000ff), ((GETLE32(&gpuData[6])>>8) & 0x000000ff), 
                     ((GETLE32(&gpuData[2])>>12) & 0x3f0), ((GETLE32(&gpuData[2])>>22) & iGPUHeightMask));
    return;
   case 1:
    drawPoly3TEx8_TW(lx0,ly0,lx1,ly1,lx2,ly2,
                     (GETLE32(&gpuData[2]) & 0x000000ff), ((GETLE32(&gpuData[2])>>8) & 0x000000ff), (GETLE32(&gpuData[4]) & 0x000000ff), ((GETLE32(&gpuData[4])>>8) & 0x000000ff),(GETLE32(&gpuData[6]) & 0x000000ff), ((GETLE32(&gpuData[6])>>8) & 0x000000ff), 
                     ((GETLE32(&gpuData[2])>>12) & 0x3f0), ((GETLE32(&gpuData[2])>>22) & iGPUHeightMask));
    return;
   case 2:
    drawPoly3TD_TW(lx0,ly0,lx1,ly1,lx2,ly2,(GETLE32(&gpuData[2]) & 0x000000ff), ((GETLE32(&gpuData[2])>>8) & 0x000000ff), (GETLE32(&gpuData[4]) & 0x000000ff), ((GETLE32(&gpuData[4])>>8) & 0x000000ff),(GETLE32(&gpuData[6]) & 0x000000ff), ((GETLE32(&gpuData[6])>>8) & 0x000000ff));
    return;
  }
}

////////////////////////////////////////////////////////////////////////

void drawPoly4FT(unsigned char * baseAddr)
{
 uint32_t *gpuData = ((uint32_t *) baseAddr);

 if(GlobalTextIL && GlobalTextTP<2)
  {
   if(GlobalTextTP==0)
    drawPoly4TEx4_IL(lx0,ly0,lx1,ly1,lx3,ly3,lx2,ly2,
                     (GETLE32(&gpuData[2]) & 0x000000ff), ((GETLE32(&gpuData[2])>>8) & 0x000000ff), (GETLE32(&gpuData[4]) & 0x000000ff), ((GETLE32(&gpuData[4])>>8) & 0x000000ff),(GETLE32(&gpuData[8]) & 0x000000ff), ((GETLE32(&gpuData[8])>>8) & 0x000000ff),(GETLE32(&gpuData[6]) & 0x000000ff), ((GETLE32(&gpuData[6])>>8) & 0x000000ff), ((GETLE32(&gpuData[2])>>12) & 0x3f0), ((GETLE32(&gpuData[2])>>22) & iGPUHeightMask));
   else
    drawPoly4TEx8_IL(lx0,ly0,lx1,ly1,lx3,ly3,lx2,ly2,
                  (GETLE32(&gpuData[2]) & 0x000000ff), ((GETLE32(&gpuData[2])>>8) & 0x000000ff), (GETLE32(&gpuData[4]) & 0x000000ff), ((GETLE32(&gpuData[4])>>8) & 0x000000ff),(GETLE32(&gpuData[8]) & 0x000000ff), ((GETLE32(&gpuData[8])>>8) & 0x000000ff),(GETLE32(&gpuData[6]) & 0x000000ff), ((GETLE32(&gpuData[6])>>8) & 0x000000ff), ((GETLE32(&gpuData[2])>>12) & 0x3f0), ((GETLE32(&gpuData[2])>>22) & iGPUHeightMask));
   return;
  }

 if(!bUsingTWin)
  {
#ifdef POLYQUAD3GT
   if(IsNoRect())
    {
     switch (GlobalTextTP)
      {
       case 0:
        drawPoly4TEx4_TRI(lx0,ly0,lx1,ly1,lx3,ly3,lx2,ly2,
                      (GETLE32(&gpuData[2]) & 0x000000ff), ((GETLE32(&gpuData[2])>>8) & 0x000000ff), (GETLE32(&gpuData[4]) & 0x000000ff), ((GETLE32(&gpuData[4])>>8) & 0x000000ff),(GETLE32(&gpuData[8]) & 0x000000ff), ((GETLE32(&gpuData[8])>>8) & 0x000000ff),(GETLE32(&gpuData[6]) & 0x000000ff), ((GETLE32(&gpuData[6])>>8) & 0x000000ff), ((GETLE32(&gpuData[2])>>12) & 0x3f0), ((GETLE32(&gpuData[2])>>22) & iGPUHeightMask));
        return;
       case 1:
        drawPoly4TEx8_TRI(lx0,ly0,lx1,ly1,lx3,ly3,lx2,ly2,
                      (GETLE32(&gpuData[2]) & 0x000000ff), ((GETLE32(&gpuData[2])>>8) & 0x000000ff), (GETLE32(&gpuData[4]) & 0x000000ff), ((GETLE32(&gpuData[4])>>8) & 0x000000ff),(GETLE32(&gpuData[8]) & 0x000000ff), ((GETLE32(&gpuData[8])>>8) & 0x000000ff),(GETLE32(&gpuData[6]) & 0x000000ff), ((GETLE32(&gpuData[6])>>8) & 0x000000ff), ((GETLE32(&gpuData[2])>>12) & 0x3f0), ((GETLE32(&gpuData[2])>>22) & iGPUHeightMask));
        return;
       case 2:
        drawPoly4TD_TRI(lx0,ly0,lx1,ly1,lx3,ly3,lx2,ly2,(GETLE32(&gpuData[2]) & 0x000000ff), ((GETLE32(&gpuData[2])>>8) & 0x000000ff), (GETLE32(&gpuData[4]) & 0x000000ff), ((GETLE32(&gpuData[4])>>8) & 0x000000ff),(GETLE32(&gpuData[8]) & 0x000000ff), ((GETLE32(&gpuData[8])>>8) & 0x000000ff),(GETLE32(&gpuData[6]) & 0x000000ff), ((GETLE32(&gpuData[6])>>8) & 0x000000ff));
        return;
      }
     return;
    }
#endif
          
   switch (GlobalTextTP)
    {
     case 0: // grandia investigations needed
      drawPoly4TEx4(lx0,ly0,lx1,ly1,lx3,ly3,lx2,ly2,
                    (GETLE32(&gpuData[2]) & 0x000000ff), ((GETLE32(&gpuData[2])>>8) & 0x000000ff), (GETLE32(&gpuData[4]) & 0x000000ff), ((GETLE32(&gpuData[4])>>8) & 0x000000ff),(GETLE32(&gpuData[8]) & 0x000000ff), ((GETLE32(&gpuData[8])>>8) & 0x000000ff),(GETLE32(&gpuData[6]) & 0x000000ff), ((GETLE32(&gpuData[6])>>8) & 0x000000ff), ((GETLE32(&gpuData[2])>>12) & 0x3f0), ((GETLE32(&gpuData[2])>>22) & iGPUHeightMask));
      return;
     case 1:
      drawPoly4TEx8(lx0,ly0,lx1,ly1,lx3,ly3,lx2,ly2,
                  (GETLE32(&gpuData[2]) & 0x000000ff), ((GETLE32(&gpuData[2])>>8) & 0x000000ff), (GETLE32(&gpuData[4]) & 0x000000ff), ((GETLE32(&gpuData[4])>>8) & 0x000000ff),(GETLE32(&gpuData[8]) & 0x000000ff), ((GETLE32(&gpuData[8])>>8) & 0x000000ff),(GETLE32(&gpuData[6]) & 0x000000ff), ((GETLE32(&gpuData[6])>>8) & 0x000000ff), ((GETLE32(&gpuData[2])>>12) & 0x3f0), ((GETLE32(&gpuData[2])>>22) & iGPUHeightMask));
      return;
     case 2:
      drawPoly4TD(lx0,ly0,lx1,ly1,lx3,ly3,lx2,ly2,(GETLE32(&gpuData[2]) & 0x000000ff), ((GETLE32(&gpuData[2])>>8) & 0x000000ff), (GETLE32(&gpuData[4]) & 0x000000ff), ((GETLE32(&gpuData[4])>>8) & 0x000000ff),(GETLE32(&gpuData[8]) & 0x000000ff), ((GETLE32(&gpuData[8])>>8) & 0x000000ff),(GETLE32(&gpuData[6]) & 0x000000ff), ((GETLE32(&gpuData[6])>>8) & 0x000000ff));
      return;
    }
   return;
  }

 switch (GlobalTextTP)
  {
   case 0:
    drawPoly4TEx4_TW(lx0,ly0,lx1,ly1,lx3,ly3,lx2,ly2,
                     (GETLE32(&gpuData[2]) & 0x000000ff), ((GETLE32(&gpuData[2])>>8) & 0x000000ff), (GETLE32(&gpuData[4]) & 0x000000ff), ((GETLE32(&gpuData[4])>>8) & 0x000000ff),(GETLE32(&gpuData[8]) & 0x000000ff), ((GETLE32(&gpuData[8])>>8) & 0x000000ff),(GETLE32(&gpuData[6]) & 0x000000ff), ((GETLE32(&gpuData[6])>>8) & 0x000000ff), ((GETLE32(&gpuData[2])>>12) & 0x3f0), ((GETLE32(&gpuData[2])>>22) & iGPUHeightMask));
    return;
   case 1:
    drawPoly4TEx8_TW(lx0,ly0,lx1,ly1,lx3,ly3,lx2,ly2,
                     (GETLE32(&gpuData[2]) & 0x000000ff), ((GETLE32(&gpuData[2])>>8) & 0x000000ff), (GETLE32(&gpuData[4]) & 0x000000ff), ((GETLE32(&gpuData[4])>>8) & 0x000000ff),(GETLE32(&gpuData[8]) & 0x000000ff), ((GETLE32(&gpuData[8])>>8) & 0x000000ff),(GETLE32(&gpuData[6]) & 0x000000ff), ((GETLE32(&gpuData[6])>>8) & 0x000000ff), ((GETLE32(&gpuData[2])>>12) & 0x3f0), ((GETLE32(&gpuData[2])>>22) & iGPUHeightMask));
    return;
   case 2:
    drawPoly4TD_TW(lx0,ly0,lx1,ly1,lx3,ly3,lx2,ly2,(GETLE32(&gpuData[2]) & 0x000000ff), ((GETLE32(&gpuData[2])>>8) & 0x000000ff), (GETLE32(&gpuData[4]) & 0x000000ff), ((GETLE32(&gpuData[4])>>8) & 0x000000ff),(GETLE32(&gpuData[8]) & 0x000000ff), ((GETLE32(&gpuData[8])>>8) & 0x000000ff),(GETLE32(&gpuData[6]) & 0x000000ff), ((GETLE32(&gpuData[6])>>8) & 0x000000ff));
    return;
  }
}

////////////////////////////////////////////////////////////////////////

void drawPoly3GT(unsigned char * baseAddr)
{
 uint32_t *gpuData = ((uint32_t *) baseAddr);

 if(GlobalTextIL && GlobalTextTP<2)
  {
   if(GlobalTextTP==0)
    drawPoly3TGEx4_IL(lx0,ly0,lx1,ly1,lx2,ly2,
                      (GETLE32(&gpuData[2]) & 0x000000ff), ((GETLE32(&gpuData[2])>>8) & 0x000000ff), (GETLE32(&gpuData[5]) & 0x000000ff), ((GETLE32(&gpuData[5])>>8) & 0x000000ff),(GETLE32(&gpuData[8]) & 0x000000ff), ((GETLE32(&gpuData[8])>>8) & 0x000000ff), 
                      ((GETLE32(&gpuData[2])>>12) & 0x3f0), ((GETLE32(&gpuData[2])>>22) & iGPUHeightMask),
                      GETLE32(&gpuData[0]),GETLE32(&gpuData[3]),GETLE32(&gpuData[6]));
   else
    drawPoly3TGEx8_IL(lx0,ly0,lx1,ly1,lx2,ly2,
                      (GETLE32(&gpuData[2]) & 0x000000ff), ((GETLE32(&gpuData[2])>>8) & 0x000000ff), (GETLE32(&gpuData[5]) & 0x000000ff), ((GETLE32(&gpuData[5])>>8) & 0x000000ff),(GETLE32(&gpuData[8]) & 0x000000ff), ((GETLE32(&gpuData[8])>>8) & 0x000000ff), 
                      ((GETLE32(&gpuData[2])>>12) & 0x3f0), ((GETLE32(&gpuData[2])>>22) & iGPUHeightMask),
                      GETLE32(&gpuData[0]),GETLE32(&gpuData[3]),GETLE32(&gpuData[6]));
   return;
  }

 if(!bUsingTWin)
  {
   switch (GlobalTextTP)
    {
     case 0:
      drawPoly3TGEx4(lx0,ly0,lx1,ly1,lx2,ly2,
                     (GETLE32(&gpuData[2]) & 0x000000ff), ((GETLE32(&gpuData[2])>>8) & 0x000000ff), (GETLE32(&gpuData[5]) & 0x000000ff), ((GETLE32(&gpuData[5])>>8) & 0x000000ff),(GETLE32(&gpuData[8]) & 0x000000ff), ((GETLE32(&gpuData[8])>>8) & 0x000000ff), 
                     ((GETLE32(&gpuData[2])>>12) & 0x3f0), ((GETLE32(&gpuData[2])>>22) & iGPUHeightMask),
                     GETLE32(&gpuData[0]),GETLE32(&gpuData[3]),GETLE32(&gpuData[6]));
      return;
     case 1:
      drawPoly3TGEx8(lx0,ly0,lx1,ly1,lx2,ly2,
                     (GETLE32(&gpuData[2]) & 0x000000ff), ((GETLE32(&gpuData[2])>>8) & 0x000000ff), (GETLE32(&gpuData[5]) & 0x000000ff), ((GETLE32(&gpuData[5])>>8) & 0x000000ff),(GETLE32(&gpuData[8]) & 0x000000ff), ((GETLE32(&gpuData[8])>>8) & 0x000000ff), 
                     ((GETLE32(&gpuData[2])>>12) & 0x3f0), ((GETLE32(&gpuData[2])>>22) & iGPUHeightMask),
                     GETLE32(&gpuData[0]),GETLE32(&gpuData[3]),GETLE32(&gpuData[6]));
      return;
     case 2:
      drawPoly3TGD(lx0,ly0,lx1,ly1,lx2,ly2,(GETLE32(&gpuData[2]) & 0x000000ff), ((GETLE32(&gpuData[2])>>8) & 0x000000ff), (GETLE32(&gpuData[5]) & 0x000000ff), ((GETLE32(&gpuData[5])>>8) & 0x000000ff),(GETLE32(&gpuData[8]) & 0x000000ff), ((GETLE32(&gpuData[8])>>8) & 0x000000ff),GETLE32(&gpuData[0]),GETLE32(&gpuData[3]),GETLE32(&gpuData[6]));
      return;
    }
   return;
  }

 switch(GlobalTextTP)
  {
   case 0:
    drawPoly3TGEx4_TW(lx0,ly0,lx1,ly1,lx2,ly2,
                      (GETLE32(&gpuData[2]) & 0x000000ff), ((GETLE32(&gpuData[2])>>8) & 0x000000ff), (GETLE32(&gpuData[5]) & 0x000000ff), ((GETLE32(&gpuData[5])>>8) & 0x000000ff),(GETLE32(&gpuData[8]) & 0x000000ff), ((GETLE32(&gpuData[8])>>8) & 0x000000ff), 
                      ((GETLE32(&gpuData[2])>>12) & 0x3f0), ((GETLE32(&gpuData[2])>>22) & iGPUHeightMask),
                      GETLE32(&gpuData[0]),GETLE32(&gpuData[3]),GETLE32(&gpuData[6]));
    return;
   case 1:
    drawPoly3TGEx8_TW(lx0,ly0,lx1,ly1,lx2,ly2,
                      (GETLE32(&gpuData[2]) & 0x000000ff), ((GETLE32(&gpuData[2])>>8) & 0x000000ff), (GETLE32(&gpuData[5]) & 0x000000ff), ((GETLE32(&gpuData[5])>>8) & 0x000000ff),(GETLE32(&gpuData[8]) & 0x000000ff), ((GETLE32(&gpuData[8])>>8) & 0x000000ff), 
                      ((GETLE32(&gpuData[2])>>12) & 0x3f0), ((GETLE32(&gpuData[2])>>22) & iGPUHeightMask),
                      GETLE32(&gpuData[0]),GETLE32(&gpuData[3]),GETLE32(&gpuData[6]));
    return;
   case 2:
    drawPoly3TGD_TW(lx0,ly0,lx1,ly1,lx2,ly2,(GETLE32(&gpuData[2]) & 0x000000ff), ((GETLE32(&gpuData[2])>>8) & 0x000000ff), (GETLE32(&gpuData[5]) & 0x000000ff), ((GETLE32(&gpuData[5])>>8) & 0x000000ff),(GETLE32(&gpuData[8]) & 0x000000ff), ((GETLE32(&gpuData[8])>>8) & 0x000000ff),GETLE32(&gpuData[0]),GETLE32(&gpuData[3]),GETLE32(&gpuData[6]));
    return;
  }
}              

////////////////////////////////////////////////////////////////////////

void drawPoly4GT(unsigned char *baseAddr)
{
 uint32_t *gpuData = ((uint32_t *) baseAddr);

 if(GlobalTextIL && GlobalTextTP<2)
  {
   if(GlobalTextTP==0)
    drawPoly4TGEx4_TRI_IL(lx0,ly0,lx1,ly1,lx3,ly3,lx2,ly2,
                          (GETLE32(&gpuData[2]) & 0x000000ff), ((GETLE32(&gpuData[2])>>8) & 0x000000ff), (GETLE32(&gpuData[5]) & 0x000000ff), ((GETLE32(&gpuData[5])>>8) & 0x000000ff),(GETLE32(&gpuData[11]) & 0x000000ff), ((GETLE32(&gpuData[11])>>8) & 0x000000ff),(GETLE32(&gpuData[8]) & 0x000000ff), ((GETLE32(&gpuData[8])>>8) & 0x000000ff),
                          ((GETLE32(&gpuData[2])>>12) & 0x3f0),((GETLE32(&gpuData[2])>>22) & iGPUHeightMask),
                          GETLE32(&gpuData[0]),GETLE32(&gpuData[3]),GETLE32(&gpuData[6]),GETLE32(&gpuData[9]));
   else
    drawPoly4TGEx8_TRI_IL(lx0,ly0,lx1,ly1,lx3,ly3,lx2,ly2,
                          (GETLE32(&gpuData[2]) & 0x000000ff), ((GETLE32(&gpuData[2])>>8) & 0x000000ff), (GETLE32(&gpuData[5]) & 0x000000ff), ((GETLE32(&gpuData[5])>>8) & 0x000000ff),(GETLE32(&gpuData[11]) & 0x000000ff), ((GETLE32(&gpuData[11])>>8) & 0x000000ff),(GETLE32(&gpuData[8]) & 0x000000ff), ((GETLE32(&gpuData[8])>>8) & 0x000000ff),
                          ((GETLE32(&gpuData[2])>>12) & 0x3f0),((GETLE32(&gpuData[2])>>22) & iGPUHeightMask),
                          GETLE32(&gpuData[0]),GETLE32(&gpuData[3]),GETLE32(&gpuData[6]),GETLE32(&gpuData[9]));
   return;
  }

 if(!bUsingTWin)
  {
#ifdef POLYQUAD3GT
   if(IsNoRect())
    {
     switch (GlobalTextTP)
      {
       case 0:
        drawPoly4TGEx4_TRI(lx0,ly0,lx1,ly1,lx3,ly3,lx2,ly2,
                      (GETLE32(&gpuData[2]) & 0x000000ff), ((GETLE32(&gpuData[2])>>8) & 0x000000ff), (GETLE32(&gpuData[5]) & 0x000000ff), ((GETLE32(&gpuData[5])>>8) & 0x000000ff),(GETLE32(&gpuData[11]) & 0x000000ff), ((GETLE32(&gpuData[11])>>8) & 0x000000ff),(GETLE32(&gpuData[8]) & 0x000000ff), ((GETLE32(&gpuData[8])>>8) & 0x000000ff),
                      ((GETLE32(&gpuData[2])>>12) & 0x3f0),((GETLE32(&gpuData[2])>>22) & iGPUHeightMask),
                       GETLE32(&gpuData[0]),GETLE32(&gpuData[3]),GETLE32(&gpuData[6]),GETLE32(&gpuData[9]));

        return;
       case 1:
        drawPoly4TGEx8_TRI(lx0,ly0,lx1,ly1,lx3,ly3,lx2,ly2,
                      (GETLE32(&gpuData[2]) & 0x000000ff), ((GETLE32(&gpuData[2])>>8) & 0x000000ff), (GETLE32(&gpuData[5]) & 0x000000ff), ((GETLE32(&gpuData[5])>>8) & 0x000000ff),(GETLE32(&gpuData[11]) & 0x000000ff), ((GETLE32(&gpuData[11])>>8) & 0x000000ff),(GETLE32(&gpuData[8]) & 0x000000ff), ((GETLE32(&gpuData[8])>>8) & 0x000000ff),
                      ((GETLE32(&gpuData[2])>>12) & 0x3f0),((GETLE32(&gpuData[2])>>22) & iGPUHeightMask),
                      GETLE32(&gpuData[0]),GETLE32(&gpuData[3]),GETLE32(&gpuData[6]),GETLE32(&gpuData[9]));
        return;
       case 2:
        drawPoly4TGD_TRI(lx0,ly0,lx1,ly1,lx3,ly3,lx2,ly2,(GETLE32(&gpuData[2]) & 0x000000ff),((GETLE32(&gpuData[2])>>8) & 0x000000ff), (GETLE32(&gpuData[5]) & 0x000000ff), ((GETLE32(&gpuData[5])>>8) & 0x000000ff),(GETLE32(&gpuData[11]) & 0x000000ff), ((GETLE32(&gpuData[11])>>8) & 0x000000ff),(GETLE32(&gpuData[8]) & 0x000000ff), ((GETLE32(&gpuData[8])>>8) & 0x000000ff),GETLE32(&gpuData[0]),GETLE32(&gpuData[3]),GETLE32(&gpuData[6]),GETLE32(&gpuData[9]));
        return;
      }
     return;
    }
#endif

   switch (GlobalTextTP)
    {
     case 0:
      drawPoly4TGEx4(lx0,ly0,lx1,ly1,lx3,ly3,lx2,ly2,
                    (GETLE32(&gpuData[2]) & 0x000000ff), ((GETLE32(&gpuData[2])>>8) & 0x000000ff), (GETLE32(&gpuData[5]) & 0x000000ff), ((GETLE32(&gpuData[5])>>8) & 0x000000ff),(GETLE32(&gpuData[11]) & 0x000000ff), ((GETLE32(&gpuData[11])>>8) & 0x000000ff),(GETLE32(&gpuData[8]) & 0x000000ff), ((GETLE32(&gpuData[8])>>8) & 0x000000ff),
                    ((GETLE32(&gpuData[2])>>12) & 0x3f0),((GETLE32(&gpuData[2])>>22) & iGPUHeightMask),
                     GETLE32(&gpuData[0]),GETLE32(&gpuData[3]),GETLE32(&gpuData[6]),GETLE32(&gpuData[9]));

      return;
     case 1:
      drawPoly4TGEx8(lx0,ly0,lx1,ly1,lx3,ly3,lx2,ly2,
                    (GETLE32(&gpuData[2]) & 0x000000ff), ((GETLE32(&gpuData[2])>>8) & 0x000000ff), (GETLE32(&gpuData[5]) & 0x000000ff), ((GETLE32(&gpuData[5])>>8) & 0x000000ff),(GETLE32(&gpuData[11]) & 0x000000ff), ((GETLE32(&gpuData[11])>>8) & 0x000000ff),(GETLE32(&gpuData[8]) & 0x000000ff), ((GETLE32(&gpuData[8])>>8) & 0x000000ff),
                    ((GETLE32(&gpuData[2])>>12) & 0x3f0),((GETLE32(&gpuData[2])>>22) & iGPUHeightMask),
                    GETLE32(&gpuData[0]),GETLE32(&gpuData[3]),GETLE32(&gpuData[6]),GETLE32(&gpuData[9]));
      return;
     case 2:
      drawPoly4TGD(lx0,ly0,lx1,ly1,lx3,ly3,lx2,ly2,(GETLE32(&gpuData[2]) & 0x000000ff),((GETLE32(&gpuData[2])>>8) & 0x000000ff), (GETLE32(&gpuData[5]) & 0x000000ff), ((GETLE32(&gpuData[5])>>8) & 0x000000ff),(GETLE32(&gpuData[11]) & 0x000000ff), ((GETLE32(&gpuData[11])>>8) & 0x000000ff),(GETLE32(&gpuData[8]) & 0x000000ff), ((GETLE32(&gpuData[8])>>8) & 0x000000ff),GETLE32(&gpuData[0]),GETLE32(&gpuData[3]),GETLE32(&gpuData[6]),GETLE32(&gpuData[9]));
      return;
    }
   return;
  }

 switch (GlobalTextTP)
  {
   case 0:
    drawPoly4TGEx4_TW(lx0,ly0,lx1,ly1,lx3,ly3,lx2,ly2,
                      (GETLE32(&gpuData[2]) & 0x000000ff), ((GETLE32(&gpuData[2])>>8) & 0x000000ff), (GETLE32(&gpuData[5]) & 0x000000ff), ((GETLE32(&gpuData[5])>>8) & 0x000000ff),(GETLE32(&gpuData[11]) & 0x000000ff), ((GETLE32(&gpuData[11])>>8) & 0x000000ff),(GETLE32(&gpuData[8]) & 0x000000ff), ((GETLE32(&gpuData[8])>>8) & 0x000000ff),
                      ((GETLE32(&gpuData[2])>>12) & 0x3f0),((GETLE32(&gpuData[2])>>22) & iGPUHeightMask),
                      GETLE32(&gpuData[0]),GETLE32(&gpuData[3]),GETLE32(&gpuData[6]),GETLE32(&gpuData[9]));
    return;
   case 1:
    drawPoly4TGEx8_TW(lx0,ly0,lx1,ly1,lx3,ly3,lx2,ly2,
                      (GETLE32(&gpuData[2]) & 0x000000ff), ((GETLE32(&gpuData[2])>>8) & 0x000000ff), (GETLE32(&gpuData[5]) & 0x000000ff), ((GETLE32(&gpuData[5])>>8) & 0x000000ff),(GETLE32(&gpuData[11]) & 0x000000ff), ((GETLE32(&gpuData[11])>>8) & 0x000000ff),(GETLE32(&gpuData[8]) & 0x000000ff), ((GETLE32(&gpuData[8])>>8) & 0x000000ff),
                      ((GETLE32(&gpuData[2])>>12) & 0x3f0),((GETLE32(&gpuData[2])>>22) & iGPUHeightMask),
                      GETLE32(&gpuData[0]),GETLE32(&gpuData[3]),GETLE32(&gpuData[6]),GETLE32(&gpuData[9]));
    return;
   case 2:
    drawPoly4TGD_TW(lx0,ly0,lx1,ly1,lx3,ly3,lx2,ly2,(GETLE32(&gpuData[2]) & 0x000000ff),((GETLE32(&gpuData[2])>>8) & 0x000000ff), (GETLE32(&gpuData[5]) & 0x000000ff), ((GETLE32(&gpuData[5])>>8) & 0x000000ff),(GETLE32(&gpuData[11]) & 0x000000ff), ((GETLE32(&gpuData[11])>>8) & 0x000000ff),(GETLE32(&gpuData[8]) & 0x000000ff), ((GETLE32(&gpuData[8])>>8) & 0x000000ff),GETLE32(&gpuData[0]),GETLE32(&gpuData[3]),GETLE32(&gpuData[6]),GETLE32(&gpuData[9]));
    return;
  }
}
                
////////////////////////////////////////////////////////////////////////
// SPRITE FUNCS
////////////////////////////////////////////////////////////////////////

void DrawSoftwareSpriteTWin(unsigned char * baseAddr,int32_t w,int32_t h)
{ 
 uint32_t *gpuData = (uint32_t *)baseAddr;
 short sx0,sy0,sx1,sy1,sx2,sy2,sx3,sy3;
 short tx0,ty0,tx1,ty1,tx2,ty2,tx3,ty3;

 sx0=lx0;
 sy0=ly0;

 sx0=sx3=sx0+PSXDisplay.DrawOffset.x;
 sx1=sx2=sx0+w;
 sy0=sy1=sy0+PSXDisplay.DrawOffset.y;
 sy2=sy3=sy0+h;
 
 tx0=tx3=GETLE32(&gpuData[2])&0xff;
 tx1=tx2=tx0+w;
 ty0=ty1=(GETLE32(&gpuData[2])>>8)&0xff;
 ty2=ty3=ty0+h;

 switch (GlobalTextTP)
  {
   case 0:
    drawPoly4TEx4_TW_S(sx0,sy0,sx1,sy1,sx2,sy2,sx3,sy3,
                     tx0,ty0,tx1,ty1,tx2,ty2,tx3,ty3, 
                     ((GETLE32(&gpuData[2])>>12) & 0x3f0), ((GETLE32(&gpuData[2])>>22) & iGPUHeightMask));
    return;
   case 1:
    drawPoly4TEx8_TW_S(sx0,sy0,sx1,sy1,sx2,sy2,sx3,sy3,
                       tx0,ty0,tx1,ty1,tx2,ty2,tx3,ty3, 
                       ((GETLE32(&gpuData[2])>>12) & 0x3f0), ((GETLE32(&gpuData[2])>>22) & iGPUHeightMask));
    return;
   case 2:
    drawPoly4TD_TW_S(sx0,sy0,sx1,sy1,sx2,sy2,sx3,sy3,
                     tx0,ty0,tx1,ty1,tx2,ty2,tx3,ty3);
    return;
  }
}                                                   

////////////////////////////////////////////////////////////////////////

void DrawSoftwareSpriteMirror(unsigned char * baseAddr,int32_t w,int32_t h)
{
 int32_t sprtY,sprtX,sprtW,sprtH,lXDir,lYDir;
 int32_t clutY0,clutX0,clutP,textX0,textY0,sprtYa,sprCY,sprCX,sprA;
 short tC;
 uint32_t *gpuData = (uint32_t *)baseAddr;
 sprtY = ly0;
 sprtX = lx0;
 sprtH = h;
 sprtW = w;
 clutY0 = (GETLE32(&gpuData[2])>>22) & iGPUHeightMask;
 clutX0 = (GETLE32(&gpuData[2])>>12) & 0x3f0;
 clutP  = (clutY0<<11) + (clutX0<<1);
 textY0 = ((GETLE32(&gpuData[2])>>8) & 0x000000ff) + GlobalTextAddrY;
 textX0 = (GETLE32(&gpuData[2]) & 0x000000ff);

 sprtX+=PSXDisplay.DrawOffset.x;
 sprtY+=PSXDisplay.DrawOffset.y;

// while (sprtX>1023)             sprtX-=1024;
// while (sprtY>MAXYLINESMIN1)    sprtY-=MAXYLINES;

 if(sprtX>drawW)
  {
//   if((sprtX+sprtW)>1023) sprtX-=1024;
//   else return;
   return;
  }

 if(sprtY>drawH)
  {
//   if ((sprtY+sprtH)>MAXYLINESMIN1) sprtY-=MAXYLINES;
//   else return;
   return;
  }

 if(sprtY<drawY)
  {
   if((sprtY+sprtH)<drawY) return;
   sprtH-=(drawY-sprtY);
   textY0+=(drawY-sprtY);
   sprtY=drawY;
  }

 if(sprtX<drawX)
  {
   if((sprtX+sprtW)<drawX) return;
   sprtW-=(drawX-sprtX);
   textX0+=(drawX-sprtX);
   sprtX=drawX;
  }

 if((sprtY+sprtH)>drawH) sprtH=drawH-sprtY+1;
 if((sprtX+sprtW)>drawW) sprtW=drawW-sprtX+1;

 if(usMirror&0x1000) lXDir=-1; else lXDir=1;
 if(usMirror&0x2000) lYDir=-1; else lYDir=1;

 switch (GlobalTextTP)
  {
   case 0: // texture is 4-bit

    sprtW=sprtW/2;
    textX0=(GlobalTextAddrX<<1)+(textX0>>1);
    sprtYa=(sprtY<<10);
    clutP=(clutY0<<10)+clutX0;
    for (sprCY=0;sprCY<sprtH;sprCY++)
     for (sprCX=0;sprCX<sprtW;sprCX++)
      {
       tC= psxVub[((textY0+(sprCY*lYDir))<<11) + textX0 +(sprCX*lXDir)];
       sprA=sprtYa+(sprCY<<10)+sprtX + (sprCX<<1);
       GetTextureTransColG_SPR(&psxVuw[sprA],GETLE16(&psxVuw[clutP+((tC>>4)&0xf)]));
       GetTextureTransColG_SPR(&psxVuw[sprA+1],GETLE16(&psxVuw[clutP+(tC&0xf)]));
      }
    return;

   case 1: 

    clutP>>=1;
    for(sprCY=0;sprCY<sprtH;sprCY++)
     for(sprCX=0;sprCX<sprtW;sprCX++)
      { 
       tC = psxVub[((textY0+(sprCY*lYDir))<<11)+(GlobalTextAddrX<<1) + textX0 + (sprCX*lXDir)] & 0xff;
       GetTextureTransColG_SPR(&psxVuw[((sprtY+sprCY)<<10)+sprtX + sprCX],psxVuw[clutP+tC]);
      }
     return;

   case 2:

    for (sprCY=0;sprCY<sprtH;sprCY++)
     for (sprCX=0;sprCX<sprtW;sprCX++)
      { 
       GetTextureTransColG_SPR(&psxVuw[((sprtY+sprCY)<<10)+sprtX+sprCX],
           GETLE16(&psxVuw[((textY0+(sprCY*lYDir))<<10)+GlobalTextAddrX + textX0 +(sprCX*lXDir)]));
      }
     return;
  }
}

////////////////////////////////////////////////////////////////////////

void DrawSoftwareSprite_IL(unsigned char * baseAddr,short w,short h,int32_t tx,int32_t ty)
{
 int32_t sprtY,sprtX,sprtW,sprtH,tdx,tdy;
 uint32_t *gpuData = (uint32_t *)baseAddr;

 sprtY = ly0;
 sprtX = lx0;
 sprtH = h;
 sprtW = w;

 sprtX+=PSXDisplay.DrawOffset.x;
 sprtY+=PSXDisplay.DrawOffset.y;

 if(sprtX>drawW) return;
 if(sprtY>drawH) return;

 tdx=tx+sprtW;
 tdy=ty+sprtH;

 sprtW+=sprtX;
 sprtH+=sprtY;

 // Pete is too lazy to make a faster version ;)

 if(GlobalTextTP==0)
  drawPoly4TEx4_IL(sprtX,sprtY,sprtX,sprtH,sprtW,sprtH,sprtW,sprtY,
                   tx,ty,      tx,tdy,     tdx,tdy,    tdx,ty,     
                   (GETLE32(&gpuData[2])>>12) & 0x3f0, ((GETLE32(&gpuData[2])>>22) & iGPUHeightMask));


 else
  drawPoly4TEx8_IL(sprtX,sprtY,sprtX,sprtH,sprtW,sprtH,sprtW,sprtY,
                   tx,ty,      tx,tdy,     tdx,tdy,    tdx,ty,     
                   (GETLE32(&gpuData[2])>>12) & 0x3f0, ((GETLE32(&gpuData[2])>>22) & iGPUHeightMask));
}

////////////////////////////////////////////////////////////////////////

void DrawSoftwareSprite(unsigned char * baseAddr,short w,short h,int32_t tx,int32_t ty)
{
 int32_t sprtY,sprtX,sprtW,sprtH;
 int32_t clutY0,clutX0,clutP,textX0,textY0,sprtYa,sprCY,sprCX,sprA;
 short tC,tC2;
 uint32_t *gpuData = (uint32_t *)baseAddr;
 unsigned char * pV;
 BOOL bWT,bWS;

 if(GlobalTextIL && GlobalTextTP<2)
  {DrawSoftwareSprite_IL(baseAddr,w,h,tx,ty);return;}

 sprtY = ly0;
 sprtX = lx0;
 sprtH = h;
 sprtW = w;
 clutY0 = (GETLE32(&gpuData[2])>>22) & iGPUHeightMask;
 clutX0 = (GETLE32(&gpuData[2])>>12) & 0x3f0;

 clutP  = (clutY0<<11) + (clutX0<<1);

 textY0 =ty+ GlobalTextAddrY;
 textX0 =tx;

 sprtX+=PSXDisplay.DrawOffset.x;
 sprtY+=PSXDisplay.DrawOffset.y;

 //while (sprtX>1023)             sprtX-=1024;
 //while (sprtY>MAXYLINESMIN1)    sprtY-=MAXYLINES;

 if(sprtX>drawW)
  {
//   if((sprtX+sprtW)>1023) sprtX-=1024;
//   else return;
   return;
  }

 if(sprtY>drawH)
  {
//   if ((sprtY+sprtH)>MAXYLINESMIN1) sprtY-=MAXYLINES;
//   else return;
   return;
  }

 if(sprtY<drawY)
  {
   if((sprtY+sprtH)<drawY) return;
   sprtH-=(drawY-sprtY);
   textY0+=(drawY-sprtY);
   sprtY=drawY;
  }

 if(sprtX<drawX)
  {
   if((sprtX+sprtW)<drawX) return;

   sprtW-=(drawX-sprtX);
   textX0+=(drawX-sprtX);
   sprtX=drawX;
  }

 if((sprtY+sprtH)>drawH) sprtH=drawH-sprtY+1;
 if((sprtX+sprtW)>drawW) sprtW=drawW-sprtX+1;


 bWT=FALSE;
 bWS=FALSE;

 switch (GlobalTextTP)
  {
   case 0:

    if(textX0&1) {bWS=TRUE;sprtW--;}
    if(sprtW&1)  bWT=TRUE;
    
    sprtW=sprtW>>1;
    textX0=(GlobalTextAddrX<<1)+(textX0>>1)+(textY0<<11);
    sprtYa=(sprtY<<10)+sprtX;
    clutP=(clutY0<<10)+clutX0;

#ifdef FASTSOLID
 
    if(!bCheckMask && !DrawSemiTrans)
     {
      for (sprCY=0;sprCY<sprtH;sprCY++)
       {
        sprA=sprtYa+(sprCY<<10);
        pV=&psxVub[(sprCY<<11)+textX0];

        if(bWS)
         {
          tC=*pV++;
          GetTextureTransColG_S(&psxVuw[sprA++],GETLE16(&psxVuw[clutP+((tC>>4)&0xf)]));
         }

        for (sprCX=0;sprCX<sprtW;sprCX++,sprA+=2)
         { 
          tC=*pV++;

          GetTextureTransColG32_S((uint32_t *)&psxVuw[sprA],
              (((int32_t)GETLE16(&psxVuw[clutP+((tC>>4)&0xf)]))<<16)|
              GETLE16(&psxVuw[clutP+(tC&0x0f)]));
         }

        if(bWT)
         {
          tC=*pV;
          GetTextureTransColG_S(&psxVuw[sprA],GETLE16(&psxVuw[clutP+(tC&0x0f)]));
         }
       }
      return;
     }

#endif

    for (sprCY=0;sprCY<sprtH;sprCY++)
     {
      sprA=sprtYa+(sprCY<<10);
      pV=&psxVub[(sprCY<<11)+textX0];

      if(bWS)
       {
        tC=*pV++;
        GetTextureTransColG_SPR(&psxVuw[sprA++],GETLE16(&psxVuw[clutP+((tC>>4)&0xf)]));
       }

      for (sprCX=0;sprCX<sprtW;sprCX++,sprA+=2)
       { 
        tC=*pV++;

        GetTextureTransColG32_SPR((uint32_t *)&psxVuw[sprA],
            (((int32_t)GETLE16(&psxVuw[clutP+((tC>>4)&0xf)])<<16))|
            GETLE16(&psxVuw[clutP+(tC&0x0f)]));
       }

      if(bWT)
       {
        tC=*pV;
        GetTextureTransColG_SPR(&psxVuw[sprA],GETLE16(&psxVuw[clutP+(tC&0x0f)]));
       }
     }
    return;

   case 1:
    clutP>>=1;sprtW--;
    textX0+=(GlobalTextAddrX<<1) + (textY0<<11);

#ifdef FASTSOLID

    if(!bCheckMask && !DrawSemiTrans)
     {
      for(sprCY=0;sprCY<sprtH;sprCY++)
       {
        sprA=((sprtY+sprCY)<<10)+sprtX;
        pV=&psxVub[(sprCY<<11)+textX0];
        for(sprCX=0;sprCX<sprtW;sprCX+=2,sprA+=2)
         { 
          tC = *pV++;tC2 = *pV++;
          GetTextureTransColG32_S((uint32_t *)&psxVuw[sprA],
              (((int32_t)GETLE16(&psxVuw[clutP+tC2]))<<16)|
              GETLE16(&psxVuw[clutP+tC]));
         }
        if(sprCX==sprtW)
         GetTextureTransColG_S(&psxVuw[sprA],GETLE16(&psxVuw[clutP+(*pV)]));
       }
      return;
     }

#endif

    for(sprCY=0;sprCY<sprtH;sprCY++)
     {
      sprA=((sprtY+sprCY)<<10)+sprtX;
      pV=&psxVub[(sprCY<<11)+textX0];
      for(sprCX=0;sprCX<sprtW;sprCX+=2,sprA+=2)
       { 
        tC = *pV++;tC2 = *pV++;
        GetTextureTransColG32_SPR((uint32_t *)&psxVuw[sprA],
            (((int32_t)GETLE16(&psxVuw[clutP+tC2]))<<16)|
            GETLE16(&psxVuw[clutP+tC]));
       }
      if(sprCX==sprtW)
       GetTextureTransColG_SPR(&psxVuw[sprA],GETLE16(&psxVuw[clutP+(*pV)]));
     }
    return;

   case 2:

    textX0+=(GlobalTextAddrX) + (textY0<<10);
    sprtW--;

#ifdef FASTSOLID

    if(!bCheckMask && !DrawSemiTrans)
     {
      for (sprCY=0;sprCY<sprtH;sprCY++)
       {
        sprA=((sprtY+sprCY)<<10)+sprtX;

        for (sprCX=0;sprCX<sprtW;sprCX+=2,sprA+=2)
         { 
          GetTextureTransColG32_S((uint32_t *)&psxVuw[sprA],
              (((int32_t)GETLE16(&psxVuw[(sprCY<<10) + textX0 + sprCX +1]))<<16)|
              GETLE16(&psxVuw[(sprCY<<10) + textX0 + sprCX]));
         }
        if(sprCX==sprtW)
         GetTextureTransColG_S(&psxVuw[sprA],
              GETLE16(&psxVuw[(sprCY<<10) + textX0 + sprCX]));

       }
      return;
     }

#endif

    for (sprCY=0;sprCY<sprtH;sprCY++)
     {
      sprA=((sprtY+sprCY)<<10)+sprtX;

      for (sprCX=0;sprCX<sprtW;sprCX+=2,sprA+=2)
       { 
        GetTextureTransColG32_SPR((uint32_t *)&psxVuw[sprA],
            (((int32_t)GETLE16(&psxVuw[(sprCY<<10) + textX0 + sprCX +1]))<<16)|
            GETLE16(&psxVuw[(sprCY<<10) + textX0 + sprCX]));
       }
      if(sprCX==sprtW)
       GetTextureTransColG_SPR(&psxVuw[sprA],
            GETLE16(&psxVuw[(sprCY<<10) + textX0 + sprCX]));

     }
    return;
   }                
}
 
///////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////
// LINE FUNCS
////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////


///////////////////////////////////////////////////////////////////////

void Line_E_SE_Shade(int x0, int y0, int x1, int y1, uint32_t rgb0, uint32_t rgb1)
{
    int dx, dy, incrE, incrSE, d;
		uint32_t r0, g0, b0, r1, g1, b1;
		int32_t dr, dg, db;

		r0 = (rgb0 & 0x00ff0000);
		g0 = (rgb0 & 0x0000ff00) << 8;
		b0 = (rgb0 & 0x000000ff) << 16;
		r1 = (rgb1 & 0x00ff0000);
		g1 = (rgb1 & 0x0000ff00) << 8;
		b1 = (rgb1 & 0x000000ff) << 16;

    dx = x1 - x0;
    dy = y1 - y0;

		if (dx > 0)
		{
			dr = ((int32_t)r1 - (int32_t)r0) / dx;
			dg = ((int32_t)g1 - (int32_t)g0) / dx;
			db = ((int32_t)b1 - (int32_t)b0) / dx;
		}
		else
		{
			dr = ((int32_t)r1 - (int32_t)r0);
			dg = ((int32_t)g1 - (int32_t)g0);
			db = ((int32_t)b1 - (int32_t)b0);
		}

    d = 2*dy - dx;              /* Initial value of d */
    incrE = 2*dy;               /* incr. used for move to E */
    incrSE = 2*(dy - dx);       /* incr. used for move to SE */

		if ((x0>=drawX)&&(x0<drawW)&&(y0>=drawY)&&(y0<drawH))
			GetShadeTransCol(&psxVuw[(y0<<10)+x0],(unsigned short)(((r0 >> 9)&0x7c00)|((g0 >> 14)&0x03e0)|((b0 >> 19)&0x001f)));
    while(x0 < x1)
    {
        if (d <= 0)
        {
            d = d + incrE;              /* Choose E */
        }
        else
        {
            d = d + incrSE;             /* Choose SE */
            y0++;
        }
        x0++;

				r0+=dr;
				g0+=dg;
				b0+=db;

				if ((x0>=drawX)&&(x0<drawW)&&(y0>=drawY)&&(y0<drawH))
					GetShadeTransCol(&psxVuw[(y0<<10)+x0],(unsigned short)(((r0 >> 9)&0x7c00)|((g0 >> 14)&0x03e0)|((b0 >> 19)&0x001f)));
    }
}

///////////////////////////////////////////////////////////////////////

void Line_S_SE_Shade(int x0, int y0, int x1, int y1, uint32_t rgb0, uint32_t rgb1)
{
    int dx, dy, incrS, incrSE, d;
		uint32_t r0, g0, b0, r1, g1, b1;
		int32_t dr, dg, db;

		r0 = (rgb0 & 0x00ff0000);
		g0 = (rgb0 & 0x0000ff00) << 8;
		b0 = (rgb0 & 0x000000ff) << 16;
		r1 = (rgb1 & 0x00ff0000);
		g1 = (rgb1 & 0x0000ff00) << 8;
		b1 = (rgb1 & 0x000000ff) << 16;

    dx = x1 - x0;
    dy = y1 - y0;

		if (dy > 0)
		{
			dr = ((int32_t)r1 - (int32_t)r0) / dy;
			dg = ((int32_t)g1 - (int32_t)g0) / dy;
			db = ((int32_t)b1 - (int32_t)b0) / dy;
		}
		else
		{
			dr = ((int32_t)r1 - (int32_t)r0);
			dg = ((int32_t)g1 - (int32_t)g0);
			db = ((int32_t)b1 - (int32_t)b0);
		}

    d = 2*dx - dy;              /* Initial value of d */
    incrS = 2*dx;               /* incr. used for move to S */
    incrSE = 2*(dx - dy);       /* incr. used for move to SE */

		if ((x0>=drawX)&&(x0<drawW)&&(y0>=drawY)&&(y0<drawH))
			GetShadeTransCol(&psxVuw[(y0<<10)+x0],(unsigned short)(((r0 >> 9)&0x7c00)|((g0 >> 14)&0x03e0)|((b0 >> 19)&0x001f)));
    while(y0 < y1)
    {
        if (d <= 0)
        {
            d = d + incrS;              /* Choose S */
        }
        else
        {
            d = d + incrSE;             /* Choose SE */
            x0++;
        }
        y0++;

				r0+=dr;
				g0+=dg;
				b0+=db;

				if ((x0>=drawX)&&(x0<drawW)&&(y0>=drawY)&&(y0<drawH))
					GetShadeTransCol(&psxVuw[(y0<<10)+x0],(unsigned short)(((r0 >> 9)&0x7c00)|((g0 >> 14)&0x03e0)|((b0 >> 19)&0x001f)));
    }
}

///////////////////////////////////////////////////////////////////////

void Line_N_NE_Shade(int x0, int y0, int x1, int y1, uint32_t rgb0, uint32_t rgb1)
{
    int dx, dy, incrN, incrNE, d;
		uint32_t r0, g0, b0, r1, g1, b1;
		int32_t dr, dg, db;

		r0 = (rgb0 & 0x00ff0000);
		g0 = (rgb0 & 0x0000ff00) << 8;
		b0 = (rgb0 & 0x000000ff) << 16;
		r1 = (rgb1 & 0x00ff0000);
		g1 = (rgb1 & 0x0000ff00) << 8;
		b1 = (rgb1 & 0x000000ff) << 16;

    dx = x1 - x0;
    dy = -(y1 - y0);

		if (dy > 0)
		{
			dr = ((int32_t)r1 - (int32_t)r0) / dy;
			dg = ((int32_t)g1 - (int32_t)g0) / dy;
			db = ((int32_t)b1 - (int32_t)b0) / dy;
		}
		else
		{
			dr = ((int32_t)r1 - (int32_t)r0);
			dg = ((int32_t)g1 - (int32_t)g0);
			db = ((int32_t)b1 - (int32_t)b0);
		}

    d = 2*dx - dy;              /* Initial value of d */
    incrN = 2*dx;               /* incr. used for move to N */
    incrNE = 2*(dx - dy);       /* incr. used for move to NE */

		if ((x0>=drawX)&&(x0<drawW)&&(y0>=drawY)&&(y0<drawH))
			GetShadeTransCol(&psxVuw[(y0<<10)+x0],(unsigned short)(((r0 >> 9)&0x7c00)|((g0 >> 14)&0x03e0)|((b0 >> 19)&0x001f)));
    while(y0 > y1)
    {
        if (d <= 0)
        {
            d = d + incrN;              /* Choose N */
        }
        else
        {
            d = d + incrNE;             /* Choose NE */
            x0++;
        }
        y0--;

				r0+=dr;
				g0+=dg;
				b0+=db;

				if ((x0>=drawX)&&(x0<drawW)&&(y0>=drawY)&&(y0<drawH))
					GetShadeTransCol(&psxVuw[(y0<<10)+x0],(unsigned short)(((r0 >> 9)&0x7c00)|((g0 >> 14)&0x03e0)|((b0 >> 19)&0x001f)));
    }
}

///////////////////////////////////////////////////////////////////////

void Line_E_NE_Shade(int x0, int y0, int x1, int y1, uint32_t rgb0, uint32_t rgb1)
{
    int dx, dy, incrE, incrNE, d;
		uint32_t r0, g0, b0, r1, g1, b1;
		int32_t dr, dg, db;

		r0 = (rgb0 & 0x00ff0000);
		g0 = (rgb0 & 0x0000ff00) << 8;
		b0 = (rgb0 & 0x000000ff) << 16;
		r1 = (rgb1 & 0x00ff0000);
		g1 = (rgb1 & 0x0000ff00) << 8;
		b1 = (rgb1 & 0x000000ff) << 16;

    dx = x1 - x0;
    dy = -(y1 - y0);

		if (dx > 0)
		{
			dr = ((int32_t)r1 - (int32_t)r0) / dx;
			dg = ((int32_t)g1 - (int32_t)g0) / dx;
			db = ((int32_t)b1 - (int32_t)b0) / dx;
		}
		else
		{
			dr = ((int32_t)r1 - (int32_t)r0);
			dg = ((int32_t)g1 - (int32_t)g0);
			db = ((int32_t)b1 - (int32_t)b0);
		}

    d = 2*dy - dx;              /* Initial value of d */
    incrE = 2*dy;               /* incr. used for move to E */
    incrNE = 2*(dy - dx);       /* incr. used for move to NE */

		if ((x0>=drawX)&&(x0<drawW)&&(y0>=drawY)&&(y0<drawH))
			GetShadeTransCol(&psxVuw[(y0<<10)+x0],(unsigned short)(((r0 >> 9)&0x7c00)|((g0 >> 14)&0x03e0)|((b0 >> 19)&0x001f)));
    while(x0 < x1)
    {
        if (d <= 0)
        {
            d = d + incrE;              /* Choose E */
        }
        else
        {
            d = d + incrNE;             /* Choose NE */
            y0--;
        }
        x0++;

				r0+=dr;
				g0+=dg;
				b0+=db;

				if ((x0>=drawX)&&(x0<drawW)&&(y0>=drawY)&&(y0<drawH))
					GetShadeTransCol(&psxVuw[(y0<<10)+x0],(unsigned short)(((r0 >> 9)&0x7c00)|((g0 >> 14)&0x03e0)|((b0 >> 19)&0x001f)));
    }
}

///////////////////////////////////////////////////////////////////////

void VertLineShade(int x, int y0, int y1, uint32_t rgb0, uint32_t rgb1)
{
  int y, dy;
	uint32_t r0, g0, b0, r1, g1, b1;
	int32_t dr, dg, db;

	r0 = (rgb0 & 0x00ff0000);
	g0 = (rgb0 & 0x0000ff00) << 8;
	b0 = (rgb0 & 0x000000ff) << 16;
	r1 = (rgb1 & 0x00ff0000);
	g1 = (rgb1 & 0x0000ff00) << 8;
	b1 = (rgb1 & 0x000000ff) << 16;

	dy = (y1 - y0);

	if (dy > 0)
	{
		dr = ((int32_t)r1 - (int32_t)r0) / dy;
		dg = ((int32_t)g1 - (int32_t)g0) / dy;
		db = ((int32_t)b1 - (int32_t)b0) / dy;
	}
	else
	{
		dr = ((int32_t)r1 - (int32_t)r0);
		dg = ((int32_t)g1 - (int32_t)g0);
		db = ((int32_t)b1 - (int32_t)b0);
	}

	if (y0 < drawY)
	{
		r0+=dr*(drawY - y0);
		g0+=dg*(drawY - y0);
		b0+=db*(drawY - y0);
		y0 = drawY;
	}

	if (y1 > drawH)
		y1 = drawH;

  for (y = y0; y <= y1; y++)
	{
		GetShadeTransCol(&psxVuw[(y<<10)+x],(unsigned short)(((r0 >> 9)&0x7c00)|((g0 >> 14)&0x03e0)|((b0 >> 19)&0x001f)));
		r0+=dr;
		g0+=dg;
		b0+=db;
	}
}

///////////////////////////////////////////////////////////////////////

void HorzLineShade(int y, int x0, int x1, uint32_t rgb0, uint32_t rgb1)
{
  int x, dx;
	uint32_t r0, g0, b0, r1, g1, b1;
	int32_t dr, dg, db;

	r0 = (rgb0 & 0x00ff0000);
	g0 = (rgb0 & 0x0000ff00) << 8;
	b0 = (rgb0 & 0x000000ff) << 16;
	r1 = (rgb1 & 0x00ff0000);
	g1 = (rgb1 & 0x0000ff00) << 8;
	b1 = (rgb1 & 0x000000ff) << 16;

	dx = (x1 - x0);

	if (dx > 0)
	{
		dr = ((int32_t)r1 - (int32_t)r0) / dx;
		dg = ((int32_t)g1 - (int32_t)g0) / dx;
		db = ((int32_t)b1 - (int32_t)b0) / dx;
	}
	else
	{
		dr = ((int32_t)r1 - (int32_t)r0);
		dg = ((int32_t)g1 - (int32_t)g0);
		db = ((int32_t)b1 - (int32_t)b0);
	}

	if (x0 < drawX)
	{
		r0+=dr*(drawX - x0);
		g0+=dg*(drawX - x0);
		b0+=db*(drawX - x0);
		x0 = drawX;
	}

	if (x1 > drawW)
		x1 = drawW;

  for (x = x0; x <= x1; x++)
	{
		GetShadeTransCol(&psxVuw[(y<<10)+x],(unsigned short)(((r0 >> 9)&0x7c00)|((g0 >> 14)&0x03e0)|((b0 >> 19)&0x001f)));
		r0+=dr;
		g0+=dg;
		b0+=db;
	}
}

///////////////////////////////////////////////////////////////////////

void Line_E_SE_Flat(int x0, int y0, int x1, int y1, unsigned short colour)
{
    int dx, dy, incrE, incrSE, d, x, y;

    dx = x1 - x0;
    dy = y1 - y0;
    d = 2*dy - dx;              /* Initial value of d */
    incrE = 2*dy;               /* incr. used for move to E */
    incrSE = 2*(dy - dx);       /* incr. used for move to SE */
    x = x0;
    y = y0;
		if ((x>=drawX)&&(x<drawW)&&(y>=drawY)&&(y<drawH))
			GetShadeTransCol(&psxVuw[(y<<10)+x], colour);
    while(x < x1)
    {
        if (d <= 0)
        {
            d = d + incrE;              /* Choose E */
            x++;
        }
        else
        {
            d = d + incrSE;             /* Choose SE */
            x++;
            y++;
        }
				if ((x>=drawX)&&(x<drawW)&&(y>=drawY)&&(y<drawH))
					GetShadeTransCol(&psxVuw[(y<<10)+x], colour);
    }
}

///////////////////////////////////////////////////////////////////////

void Line_S_SE_Flat(int x0, int y0, int x1, int y1, unsigned short colour)
{
    int dx, dy, incrS, incrSE, d, x, y;

    dx = x1 - x0;
    dy = y1 - y0;
    d = 2*dx - dy;              /* Initial value of d */
    incrS = 2*dx;               /* incr. used for move to S */
    incrSE = 2*(dx - dy);       /* incr. used for move to SE */
    x = x0;
    y = y0;
		if ((x>=drawX)&&(x<drawW)&&(y>=drawY)&&(y<drawH))
			GetShadeTransCol(&psxVuw[(y<<10)+x], colour);
    while(y < y1)
    {
        if (d <= 0)
        {
            d = d + incrS;              /* Choose S */
            y++;
        }
        else
        {
            d = d + incrSE;             /* Choose SE */
            x++;
            y++;
        }
				if ((x>=drawX)&&(x<drawW)&&(y>=drawY)&&(y<drawH))
					GetShadeTransCol(&psxVuw[(y<<10)+x], colour);
    }
}

///////////////////////////////////////////////////////////////////////

void Line_N_NE_Flat(int x0, int y0, int x1, int y1, unsigned short colour)
{
    int dx, dy, incrN, incrNE, d, x, y;

    dx = x1 - x0;
    dy = -(y1 - y0);
    d = 2*dx - dy;              /* Initial value of d */
    incrN = 2*dx;               /* incr. used for move to N */
    incrNE = 2*(dx - dy);       /* incr. used for move to NE */
    x = x0;
    y = y0;
		if ((x>=drawX)&&(x<drawW)&&(y>=drawY)&&(y<drawH))
			GetShadeTransCol(&psxVuw[(y<<10)+x], colour);
    while(y > y1)
    {
        if (d <= 0)
        {
            d = d + incrN;              /* Choose N */
            y--;
        }
        else
        {
            d = d + incrNE;             /* Choose NE */
            x++;
            y--;
        }
				if ((x>=drawX)&&(x<drawW)&&(y>=drawY)&&(y<drawH))
					GetShadeTransCol(&psxVuw[(y<<10)+x], colour);
    }
}

///////////////////////////////////////////////////////////////////////

void Line_E_NE_Flat(int x0, int y0, int x1, int y1, unsigned short colour)
{
    int dx, dy, incrE, incrNE, d, x, y;

    dx = x1 - x0;
    dy = -(y1 - y0);
    d = 2*dy - dx;              /* Initial value of d */
    incrE = 2*dy;               /* incr. used for move to E */
    incrNE = 2*(dy - dx);       /* incr. used for move to NE */
    x = x0;
    y = y0;
		if ((x>=drawX)&&(x<drawW)&&(y>=drawY)&&(y<drawH))
			GetShadeTransCol(&psxVuw[(y<<10)+x], colour);
    while(x < x1)
    {
        if (d <= 0)
        {
            d = d + incrE;              /* Choose E */
            x++;
        }
        else
        {
            d = d + incrNE;             /* Choose NE */
            x++;
            y--;
        }
				if ((x>=drawX)&&(x<drawW)&&(y>=drawY)&&(y<drawH))
					GetShadeTransCol(&psxVuw[(y<<10)+x], colour);
    }
}

///////////////////////////////////////////////////////////////////////

void VertLineFlat(int x, int y0, int y1, unsigned short colour)
{
	int y;

	if (y0 < drawY)
		y0 = drawY;

	if (y1 > drawH)
		y1 = drawH;

  for (y = y0; y <= y1; y++)
		GetShadeTransCol(&psxVuw[(y<<10)+x], colour);
}

///////////////////////////////////////////////////////////////////////

void HorzLineFlat(int y, int x0, int x1, unsigned short colour)
{
	int x;

	if (x0 < drawX)
		x0 = drawX;

	if (x1 > drawW)
		x1 = drawW;

	for (x = x0; x <= x1; x++)
		GetShadeTransCol(&psxVuw[(y << 10) + x], colour);
}

///////////////////////////////////////////////////////////////////////

/* Bresenham Line drawing function */
void DrawSoftwareLineShade(int32_t rgb0, int32_t rgb1)
{
	short x0, y0, x1, y1, xt, yt;
	int32_t rgbt;
	double m, dy, dx;

	if (lx0 > drawW && lx1 > drawW) return;
	if (ly0 > drawH && ly1 > drawH) return;
	if (lx0 < drawX && lx1 < drawX) return;
	if (ly0 < drawY && ly1 < drawY) return;
	if (drawY >= drawH) return;
	if (drawX >= drawW) return; 

	x0 = lx0;
	y0 = ly0;
	x1 = lx1;
	y1 = ly1;

	dx = x1 - x0;
	dy = y1 - y0;

	if (dx == 0)
	{
		if (dy > 0)
			VertLineShade(x0, y0, y1, rgb0, rgb1);
		else
			VertLineShade(x0, y1, y0, rgb1, rgb0);
	}
	else
		if (dy == 0)
		{
			if (dx > 0)
				HorzLineShade(y0, x0, x1, rgb0, rgb1);
			else
				HorzLineShade(y0, x1, x0, rgb1, rgb0);
		}
		else
		{
			if (dx < 0)
			{
				xt = x0;
				yt = y0;
				rgbt = rgb0;
				x0 = x1;
				y0 = y1;
				rgb0 = rgb1;
				x1 = xt;
				y1 = yt;
				rgb1 = rgbt;

				dx = x1 - x0;
				dy = y1 - y0;
			}

			m = dy / dx;

			if (m >= 0)
			{
				if (m > 1)
					Line_S_SE_Shade(x0, y0, x1, y1, rgb0, rgb1);
				else
					Line_E_SE_Shade(x0, y0, x1, y1, rgb0, rgb1);
			}
			else
				if (m < -1)
					Line_N_NE_Shade(x0, y0, x1, y1, rgb0, rgb1);
				else
					Line_E_NE_Shade(x0, y0, x1, y1, rgb0, rgb1);
		}
}

///////////////////////////////////////////////////////////////////////

void DrawSoftwareLineFlat(int32_t rgb)
{
	short x0, y0, x1, y1, xt, yt;
	double m, dy, dx;
	unsigned short colour = 0;
 
	if (lx0 > drawW && lx1 > drawW) return;
	if (ly0 > drawH && ly1 > drawH) return;
	if (lx0 < drawX && lx1 < drawX) return;
	if (ly0 < drawY && ly1 < drawY) return;
	if (drawY >= drawH) return;
	if (drawX >= drawW) return; 

	colour = ((rgb & 0x00f80000) >> 9) | ((rgb & 0x0000f800) >> 6) | ((rgb & 0x000000f8) >> 3);

	x0 = lx0;
	y0 = ly0;
	x1 = lx1;
	y1 = ly1;

	dx = x1 - x0;
	dy = y1 - y0;

	if (dx == 0)
	{
		if (dy == 0)
			return; // Nothing to draw
		else if (dy > 0)
			VertLineFlat(x0, y0, y1, colour);
		else
			VertLineFlat(x0, y1, y0, colour);
	}
	else
		if (dy == 0)
		{
			if (dx > 0)
				HorzLineFlat(y0, x0, x1, colour);
			else
				HorzLineFlat(y0, x1, x0, colour);
		}
		else
		{
			if (dx < 0)
			{
				xt = x0;
				yt = y0;
				x0 = x1;
				y0 = y1;
				x1 = xt;
				y1 = yt;

				dx = x1 - x0;
				dy = y1 - y0;
			}

			m = dy/dx;

			if (m >= 0)
			{
				if (m > 1)
					Line_S_SE_Flat(x0, y0, x1, y1, colour);
				else
					Line_E_SE_Flat(x0, y0, x1, y1, colour);
			}
			else
				if (m < -1)
					Line_N_NE_Flat(x0, y0, x1, y1, colour);
				else
					Line_E_NE_Flat(x0, y0, x1, y1, colour);
		}
}


////////////////////////////////////////////////////////////////////////
// menu.c
////////////////////////////////////////////////////////////////////////


// create lists/stuff for fonts (actually there are no more lists, but I am too lazy to change the func names ;)
void InitMenu(void)
{
}

// kill existing lists/fonts
void CloseMenu(void)
{
 DestroyPic();
}

// DISPLAY FPS/MENU TEXT

#include <time.h>
extern time_t tStart;

int iMPos=0;                                           // menu arrow pos

void DisplayText(void)                                 // DISPLAY TEXT
{
}

// Build Menu buffer (== Dispbuffer without FPS)...
void BuildDispMenu(int iInc)
{
 if(!(ulKeybits&KEY_SHOWFPS)) return;                  // mmm, cheater ;)

 iMPos+=iInc;                                          // up or down
 if(iMPos<0) iMPos=3;                                  // wrap around
 if(iMPos>3) iMPos=0;

 strcpy(szMenuBuf,"   FL   FS   DI   GF        ");     // main menu items

 if(UseFrameLimit)                                     // set marks
  {
   if(iFrameLimit==1) szMenuBuf[2]  = '+';
   else               szMenuBuf[2]  = '*';
  }
 if(iFastFwd)       szMenuBuf[7]  = '~';
 else
 if(UseFrameSkip)   szMenuBuf[7]  = '*';

 if(iUseDither)                                        // set marks
  {
   if(iUseDither==1) szMenuBuf[12]  = '+';
   else              szMenuBuf[12]  = '*';
  }

 if(dwActFixes)     szMenuBuf[17] = '*';

 if(dwCoreFlags&1)  szMenuBuf[23]  = 'A';
 if(dwCoreFlags&2)  szMenuBuf[23]  = 'M';

 if(dwCoreFlags&0xff00)                                //A/M/G/D   
  {
   if((dwCoreFlags&0x0f00)==0x0000)                    // D
    szMenuBuf[23]  = 'D';
   else
   if((dwCoreFlags&0x0f00)==0x0100)                    // A
    szMenuBuf[23]  = 'A';
   else
   if((dwCoreFlags&0x0f00)==0x0200)                    // M
    szMenuBuf[23]  = 'M';
   else
   if((dwCoreFlags&0x0f00)==0x0300)                    // G
    szMenuBuf[23]  = 'G';

   szMenuBuf[24]='0'+(char)((dwCoreFlags&0xf000)>>12);                         // number
  }


 if(lSelectedSlot)  szMenuBuf[26]  = '0'+(char)lSelectedSlot;   

 szMenuBuf[(iMPos+1)*5]='<';                           // set arrow

}

// Some menu action...
void SwitchDispMenu(int iStep)                         // SWITCH DISP MENU
{
 if(!(ulKeybits&KEY_SHOWFPS)) return;                  // tststs

 switch(iMPos)
  {
   case 0:                                             // frame limit
    {
     int iType=0;
     bInitCap = TRUE;

     if(UseFrameLimit) iType=iFrameLimit;
     iType+=iStep;
     if(iType<0) iType=2;
     if(iType>2) iType=0;
     if(iType==0) UseFrameLimit=0;
     else
      {
       UseFrameLimit=1;
       iFrameLimit=iType;
       SetAutoFrameCap();
      }
    } break;

   case 1:                                             // frame skip
    bInitCap = TRUE;
    if(iStep>0)
     {
      if(!UseFrameSkip) {UseFrameSkip=1;iFastFwd = 0;}
      else
       {
        if(!iFastFwd) iFastFwd=1;
        else {UseFrameSkip=0;iFastFwd = 0;}
       }
     }
    else
     {
      if(!UseFrameSkip) {UseFrameSkip=1;iFastFwd = 1;}
      else
       {
        if(iFastFwd) iFastFwd=0;
        else {UseFrameSkip=0;iFastFwd = 0;}
       }
     }
    bSkipNextFrame=FALSE;
    break;

   case 2:                                             // dithering
    iUseDither+=iStep;
    if(iUseDither<0) iUseDither=2;
    if(iUseDither>2) iUseDither=0;
    break;

   case 3:                                             // special fixes
    if(iUseFixes) {iUseFixes=0;dwActFixes=0;}
    else          {iUseFixes=1;dwActFixes=dwCfgFixes;}
    SetFixes();
    if(iFrameLimit==2) SetAutoFrameCap();
    break;
  }

 BuildDispMenu(0);                                     // update info
}


////////////////////////////////////////////////////////////////////////
// config file
////////////////////////////////////////////////////////////////////////

// CONFIG FILE helpers....
// some helper macros:
#define GetValue(name, var) \
 p = strstr(pB, name); \
 if (p != NULL) { \
  p+=strlen(name); \
  while ((*p == ' ') || (*p == '=')) p++; \
  if (*p != '\n') var = atoi(p); \
 }

#define GetStrValue(name, var) \
 p = strstr(pB, name); \
 if (p != NULL) { \
  char *q; \
  p+=strlen(name); \
  while ((*p == ' ') || (*p == '=')) p++; \
  q = p;\
  while (*p != '\n' && *p) p++; \
  var = (char*)malloc(p-q+1);\
  memcpy(var,q,(p-q));\
  var[p-q] = 0;\
 } else { \
   var = strdup("None"); \
 }


#define GetFloatValue(name, var) \
 p = strstr(pB, name); \
 if (p != NULL) { \
  p+=strlen(name); \
  while ((*p == ' ') || (*p == '=')) p++; \
  if (*p != '\n') var = (float)atof(p); \
 }

#define SetValue(name, var) \
 p = strstr(pB, name); \
 if (p != NULL) { \
  p+=strlen(name); \
  while ((*p == ' ') || (*p == '=')) p++; \
  if (*p != '\n') { \
   len = sprintf(t1, "%d", var); \
   strncpy(p, t1, len); \
   if (p[len] != ' ' && p[len] != '\n' && p[len] != 0) p[len] = ' '; \
  } \
 } \
 else { \
  size+=sprintf(pB+size, "%s = %d\n", name, var); \
 }

#define SetStrValue(name, var) \
 p = strstr(pB, name); \
 if (p != NULL) { \
  p+=strlen(name); \
  while ((*p == ' ') || (*p == '=')) p++; \
  if (*p != '\n') { \
   len = sprintf(t1, "%s", var); \
   strncpy(p, t1, len); \
   if (p[len] != ' ' && p[len] != '\n' && p[len] != 0) p[len] = ' '; \
  } \
 } \
 else { \
  size+=sprintf(pB+size, "%s = %s\n", name, var); \
 }


#define SetFloatValue(name, var) \
 p = strstr(pB, name); \
 if (p != NULL) { \
  p+=strlen(name); \
  while ((*p == ' ') || (*p == '=')) p++; \
  if (*p != '\n') { \
   len = sprintf(t1, "%.1f", (double)var); \
   strncpy(p, t1, len); \
   if (p[len] != ' ' && p[len] != '\n' && p[len] != 0) p[len] = ' '; \
  } \
 } \
 else { \
  size+=sprintf(pB+size, "%s = %.1f\n", name, (double)var); \
 }

void ReadConfigFile() {
 struct stat buf;
 FILE *in;char t[PATHLEN];int len, size;
 char * pB, * p;

 sprintf(t,"%s/saves/video.cfg",WorkDir);
 if (stat(t, &buf) == -1) return;
 size = buf.st_size;

 in = fopen(t,"rb");
 if (!in) return;

 pB=(char *)malloc(size + 1);
 memset(pB,0,size + 1);

 len = fread(pB, 1, size, in);
 fclose(in);


 GetStrValue("Scaler", Scaler);

 GetValue("Dithering", iUseDither);

 GetValue("FullScreen", iWindowMode);
 if(iWindowMode!=0) iWindowMode=0;
 else               iWindowMode=1;

 GetValue("ShowFPS", iShowFPS);
 if(iShowFPS<0) iShowFPS=0;
 if(iShowFPS>1) iShowFPS=1;

 GetValue("Maintain43", iMaintainAspect);
 if(iMaintainAspect<0) iMaintainAspect=0;
 if(iMaintainAspect>1) iMaintainAspect=1;

 GetValue("UseFrameLimit", UseFrameLimit);
 if(UseFrameLimit<0) UseFrameLimit=0;
 if(UseFrameLimit>1) UseFrameLimit=1;

 GetValue("UseFrameSkip", UseFrameSkip);
 if(UseFrameSkip<0) UseFrameSkip=0;
 if(UseFrameSkip>1) UseFrameSkip=1;

 GetValue("FPSDetection", iFrameLimit);
 if(iFrameLimit<1) iFrameLimit=1;
 if(iFrameLimit>2) iFrameLimit=2;

 GetFloatValue("FrameRate", fFrameRate);
 fFrameRate/=10;
 if(fFrameRate<10.0f)   fFrameRate=10.0f;
 if(fFrameRate>1000.0f) fFrameRate=1000.0f;

 GetValue("CfgFixes", dwCfgFixes);

 GetValue("UseFixes", iUseFixes);
 if(iUseFixes<0) iUseFixes=0;
 if(iUseFixes>1) iUseFixes=1;

 free(pB);
}

void ExecCfg(char *arg) {

}

void SoftDlgProc(void)
{
	ExecCfg("CFG");
}

void AboutDlgProc(void)
{
	char args[256];

	sprintf(args, "ABOUT");
	ExecCfg(args);
}

void ReadConfig(void)
{
 // defaults
 iColDepth=32;
 iWindowMode=1;
 iMaintainAspect=0;
 UseFrameLimit=1;
 UseFrameSkip=0;
 iFrameLimit=2;
 fFrameRate=200.0f;
 dwCfgFixes=0;
 iUseFixes=0;
 iUseNoStretchBlt=0;
 iUseDither=0;
 iShowFPS=0;

 // read sets
 ReadConfigFile();

 // additional checks
 if(!iColDepth)       iColDepth=32;
 if(iUseFixes)        dwActFixes=dwCfgFixes;
 SetFixes();
}

void GPU_WriteConfig() {
 FILE *out=0;char t[PATHLEN];int len, size=0;
 char * pB, * p; char t1[8];

  size = 0;
  pB=(char *)malloc(4096);
  memset(pB,0,4096);

 SetStrValue("Scaler", Scaler);
 //SetValue("Dithering", iUseDither);
 //SetValue("FullScreen", !iWindowMode);
 //SetFloatValue("FrameRate", fFrameRate);
 //SetValue("UseFixes", iUseFixes);

 sprintf(t,"%s/saves/video.cfg",WorkDir);
 out = fopen(t,"wb");
 if (!out) return;

 len = fwrite(pB, 1, size, out);
 fclose(out);

 free(pB);
}


////////////////////////////////////////////////////////////////////////
// some misc external display funcs
////////////////////////////////////////////////////////////////////////


void GPU_displayText(char * pText)             // some debug func
{
 if(!pText) {szDebugText[0]=0;return;}
 if(strlen(pText)>511) return;
 time(&tStart);
 strcpy(szDebugText,pText);
}

////////////////////////////////////////////////////////////////////////

void GPU_displayFlags(unsigned long dwFlags)   // some info func
{
 dwCoreFlags=dwFlags;
 BuildDispMenu(0);
}




#if 0
void GPU_makeSnapshot(void)
{
 FILE *bmpfile;
 char filename[256];
 unsigned char header[0x36];
 long size, height;
 unsigned char line[1024 * 3];
 short i, j;
 unsigned char empty[2] = {0,0};
 unsigned short color;
 int snapshotnr = 0;
 unsigned char *pD;

 height = PreviousPSXDisplay.DisplayMode.y;

 size = height * PreviousPSXDisplay.Range.x1 * 3 + 0x38;

 // fill in proper values for BMP

 // hardcoded BMP header
 memset(header, 0, 0x36);
 header[0] = 'B';
 header[1] = 'M';
 header[2] = size & 0xff;
 header[3] = (size >> 8) & 0xff;
 header[4] = (size >> 16) & 0xff;
 header[5] = (size >> 24) & 0xff;
 header[0x0a] = 0x36;
 header[0x0e] = 0x28;
 header[0x12] = PreviousPSXDisplay.Range.x1 % 256;
 header[0x13] = PreviousPSXDisplay.Range.x1 / 256;
 header[0x16] = height % 256;
 header[0x17] = height / 256;
 header[0x1a] = 0x01;
 header[0x1c] = 0x18;
 header[0x26] = 0x12;
 header[0x27] = 0x0B;
 header[0x2A] = 0x12;
 header[0x2B] = 0x0B;

 // increment snapshot value & try to get filename
 do
  {
   snapshotnr++;
   sprintf(filename, "%s/snaps/%05d.bmp", WorkDir, snapshotnr);

   bmpfile = fopen(filename,"rb");
   if (bmpfile == NULL)
    break;

   fclose(bmpfile);
  }
 while(TRUE);

 // try opening new snapshot file
 if ((bmpfile = fopen(filename,"wb")) == NULL)
  return;

 printf("Making snap %d\n", snapshotnr);

 fwrite(header, 0x36, 1, bmpfile);
 for (i = height + PSXDisplay.DisplayPosition.y - 1; i >= PSXDisplay.DisplayPosition.y; i--)
  {
   pD = (unsigned char *)&psxVuw[i * 1024 + PSXDisplay.DisplayPosition.x];
   for (j = 0; j < PreviousPSXDisplay.Range.x1; j++)
    {
     if (PSXDisplay.RGB24)
      {
       uint32_t lu = *(uint32_t *)pD;
       line[j * 3 + 2] = RED(lu);
       line[j * 3 + 1] = GREEN(lu);
       line[j * 3 + 0] = BLUE(lu);
       pD += 3;
      }
     else
      {
       color = GETLE16(pD);
       line[j * 3 + 2] = (color << 3) & 0xf1;
       line[j * 3 + 1] = (color >> 2) & 0xf1;
       line[j * 3 + 0] = (color >> 7) & 0xf1;
       pD += 2;
      }
    }
   fwrite(line, PreviousPSXDisplay.Range.x1 * 3, 1, bmpfile);
  }
 fwrite(empty, 0x2, 1, bmpfile);
 fclose(bmpfile);

 //DoTextSnapShot(snapshotnr);
}
#endif


int Xinitialize() {
  FB = (uint8_t*)malloc(640*512*sizeof(uint32_t));
  memset(FB,0,640*512*sizeof(uint32_t));

  SB = (uint8_t*)malloc(4*640*512*sizeof(uint32_t));
  memset(SB,0,4*640*512*sizeof(uint32_t));


  bUsingTWin=FALSE;
  InitMenu();
  if (iShowFPS) {
    iShowFPS=0;
    ulKeybits|=KEY_SHOWFPS;
    szDispBuf[0]=0;
    BuildDispMenu(0);
  }

  return 0;
}

void Xcleanup() {
  CloseMenu();
  if (iUseNoStretchBlt>0) {
    if(FB) free(FB);
    if(SB) free(SB);
    SB = FB = 0;
  }
}


long GPU_init() {
 memset(ulStatusControl,0,256*sizeof(uint32_t));  // init save state scontrol field

 szDebugText[0] = 0;                                     // init debug text buffer

 psxVSecure = (unsigned char *)malloc((iGPUHeight*2)*1024 + (1024*1024)); // always alloc one extra MB for soft drawing funcs security
 if (!psxVSecure)
  return -1;

 //!!! ATTENTION !!!
 psxVub=psxVSecure + 512 * 1024;                           // security offset into double sized psx vram!

 psxVsb=(signed char *)psxVub;                         // different ways of accessing PSX VRAM
 psxVsw=(signed short *)psxVub;
 psxVsl=(int32_t *)psxVub;
 psxVuw=(unsigned short *)psxVub;
 psxVul=(uint32_t *)psxVub;

 psxVuw_eom=psxVuw+1024*iGPUHeight;                    // pre-calc of end of vram

 memset(psxVSecure,0x00,(iGPUHeight*2)*1024 + (1024*1024));
 memset(lGPUInfoVals,0x00,16*sizeof(uint32_t));

 SetFPSHandler();

 PSXDisplay.RGB24        = FALSE;                      // init some stuff
 PSXDisplay.Interlaced   = FALSE;
 PSXDisplay.DrawOffset.x = 0;
 PSXDisplay.DrawOffset.y = 0;
 PSXDisplay.DisplayMode.x= 320;
 PSXDisplay.DisplayMode.y= 240;
 PreviousPSXDisplay.DisplayMode.x= 320;
 PreviousPSXDisplay.DisplayMode.y= 240;
 PSXDisplay.Disabled     = FALSE;
 PreviousPSXDisplay.Range.x0 =0;
 PreviousPSXDisplay.Range.y0 =0;
 PSXDisplay.Range.x0=0;
 PSXDisplay.Range.x1=0;
 PreviousPSXDisplay.DisplayModeNew.y=0;
 PSXDisplay.Double = 1;
 lGPUdataRet = 0x400;

 DataWriteMode = DR_NORMAL;

 // Reset transfer values, to prevent mis-transfer of data
 memset(&VRAMWrite, 0, sizeof(VRAMLoad_t));
 memset(&VRAMRead, 0, sizeof(VRAMLoad_t));
 
 // device initialised already !
 lGPUstatusRet = 0x14802000;
 GPUIsIdle;
 GPUIsReadyForCommands;
 bDoVSyncUpdate = TRUE;

 Xinitialize();
 return 0;
}


long GPU_open(unsigned long * disp,char * CapText,char * CfgFile) {
  ReadConfig();
  InitFPS();
  bDoVSyncUpdate = TRUE;
  *disp=0; // we dont use state pointer

 return 0;
}

long GPU_close() {
  return 0;
}

long GPU_shutdown() {
  Xcleanup();
  if (psxVSecure) {
    free(psxVSecure);
    psxVSecure = 0;
  }
 Xcleanup();
 return 0;
}

////////////////////////////////////////////////////////////////////////
// Update display (swap buffers)
////////////////////////////////////////////////////////////////////////

void updateDisplay(void)                               // UPDATE DISPLAY
{
 if(PSXDisplay.Disabled)                               // disable?
  {
   //DoClearFrontBuffer();                               // -> clear frontbuffer
   return;                                             // -> and bye
  }

 if(dwActFixes&32)                                     // pc fps calculation fix
  {
   if(UseFrameLimit) PCFrameCap();                     // -> brake
   if(UseFrameSkip || ulKeybits&KEY_SHOWFPS)  
    PCcalcfps();         
  }

 if(ulKeybits&KEY_SHOWFPS)                             // make fps display buf
  {
   sprintf(szDispBuf,"FPS %06.1f",fps_cur);
  }

 if(iFastFwd)                                          // fastfwd ?
  {
   static int fpscount; UseFrameSkip=1;

   if(!bSkipNextFrame) DoBufferSwap();                 // -> to skip or not to skip
   if(fpscount%6)                                      // -> skip 6/7 frames
        bSkipNextFrame = TRUE;
   else bSkipNextFrame = FALSE;
   fpscount++;
   if(fpscount >= (int)fFrameRateHz) fpscount = 0;
   return;
  }

 if(UseFrameSkip)                                      // skip ?
  {
   if(!bSkipNextFrame) DoBufferSwap();                 // -> to skip or not to skip
   if(dwActFixes&0xa0)                                 // -> pc fps calculation fix/old skipping fix
    {
     if((fps_skip < fFrameRateHz) && !(bSkipNextFrame))  // -> skip max one in a row
         {bSkipNextFrame = TRUE; fps_skip=fFrameRateHz;}
     else bSkipNextFrame = FALSE;
    }
   else FrameSkip();
  }
 else                                                  // no skip ?
  {
   DoBufferSwap();                                     // -> swap
  }
}

////////////////////////////////////////////////////////////////////////
// roughly emulated screen centering bits... not complete !!!
////////////////////////////////////////////////////////////////////////

void ChangeDispOffsetsX(void)                          // X CENTER
{
 long lx,l;

 if(!PSXDisplay.Range.x1) return;

 l=PreviousPSXDisplay.DisplayMode.x;

 l*=(long)PSXDisplay.Range.x1;
 l/=2560;lx=l;l&=0xfffffff8;

 if(l==PreviousPSXDisplay.Range.y1) return;            // abusing range.y1 for
 PreviousPSXDisplay.Range.y1=(short)l;                 // storing last x range and test

 if(lx>=PreviousPSXDisplay.DisplayMode.x)
  {
   PreviousPSXDisplay.Range.x1=
    (short)PreviousPSXDisplay.DisplayMode.x;
   PreviousPSXDisplay.Range.x0=0;
  }
 else
  {
   PreviousPSXDisplay.Range.x1=(short)l;

   PreviousPSXDisplay.Range.x0=
    (PSXDisplay.Range.x0-500)/8;

   if(PreviousPSXDisplay.Range.x0<0)
    PreviousPSXDisplay.Range.x0=0;

   if((PreviousPSXDisplay.Range.x0+lx)>
      PreviousPSXDisplay.DisplayMode.x)
    {
     PreviousPSXDisplay.Range.x0=
      (short)(PreviousPSXDisplay.DisplayMode.x-lx);
     PreviousPSXDisplay.Range.x0+=2; //???

     PreviousPSXDisplay.Range.x1+=(short)(lx-l);

     PreviousPSXDisplay.Range.x1-=2; // makes linux stretching easier

    }


   // some linux alignment security
   PreviousPSXDisplay.Range.x0=PreviousPSXDisplay.Range.x0>>1;
   PreviousPSXDisplay.Range.x0=PreviousPSXDisplay.Range.x0<<1;
   PreviousPSXDisplay.Range.x1=PreviousPSXDisplay.Range.x1>>1;
   PreviousPSXDisplay.Range.x1=PreviousPSXDisplay.Range.x1<<1;


   DoClearScreenBuffer();
  }

 bDoVSyncUpdate=TRUE;
}

////////////////////////////////////////////////////////////////////////

void ChangeDispOffsetsY(void)                          // Y CENTER
{
 int iT,iO=PreviousPSXDisplay.Range.y0;
 int iOldYOffset=PreviousPSXDisplay.DisplayModeNew.y;

// new

 if((PreviousPSXDisplay.DisplayModeNew.x+PSXDisplay.DisplayModeNew.y)>iGPUHeight)
  {
   int dy1=iGPUHeight-PreviousPSXDisplay.DisplayModeNew.x;
   int dy2=(PreviousPSXDisplay.DisplayModeNew.x+PSXDisplay.DisplayModeNew.y)-iGPUHeight;

   if(dy1>=dy2)
    {
     PreviousPSXDisplay.DisplayModeNew.y=-dy2;
    }
   else
    {
     PSXDisplay.DisplayPosition.y=0;
     PreviousPSXDisplay.DisplayModeNew.y=-dy1;
    }
  }
 else PreviousPSXDisplay.DisplayModeNew.y=0;

// eon

 if(PreviousPSXDisplay.DisplayModeNew.y!=iOldYOffset) // if old offset!=new offset: recalc height
  {
   PSXDisplay.Height = PSXDisplay.Range.y1 - 
                       PSXDisplay.Range.y0 +
                       PreviousPSXDisplay.DisplayModeNew.y;
   PSXDisplay.DisplayModeNew.y=PSXDisplay.Height*PSXDisplay.Double;
  }

//

 if(PSXDisplay.PAL) iT=48; else iT=28;

 if(PSXDisplay.Range.y0>=iT)
  {
   PreviousPSXDisplay.Range.y0=
    (short)((PSXDisplay.Range.y0-iT-4)*PSXDisplay.Double);
   if(PreviousPSXDisplay.Range.y0<0)
    PreviousPSXDisplay.Range.y0=0;
   PSXDisplay.DisplayModeNew.y+=
    PreviousPSXDisplay.Range.y0;
  }
 else 
  PreviousPSXDisplay.Range.y0=0;

 if(iO!=PreviousPSXDisplay.Range.y0)
  {
   DoClearScreenBuffer();
 }
}

////////////////////////////////////////////////////////////////////////
// check if update needed
////////////////////////////////////////////////////////////////////////

void updateDisplayIfChanged(void)                      // UPDATE DISPLAY IF CHANGED
{
 if ((PSXDisplay.DisplayMode.y == PSXDisplay.DisplayModeNew.y) && 
     (PSXDisplay.DisplayMode.x == PSXDisplay.DisplayModeNew.x))
  {
   if((PSXDisplay.RGB24      == PSXDisplay.RGB24New) && 
      (PSXDisplay.Interlaced == PSXDisplay.InterlacedNew)) return;
  }

 PSXDisplay.RGB24         = PSXDisplay.RGB24New;       // get new infos

 PSXDisplay.DisplayMode.y = PSXDisplay.DisplayModeNew.y;
 PSXDisplay.DisplayMode.x = PSXDisplay.DisplayModeNew.x;
 PreviousPSXDisplay.DisplayMode.x=                     // previous will hold
  min(640,PSXDisplay.DisplayMode.x);                   // max 640x512... that's
 PreviousPSXDisplay.DisplayMode.y=                     // the size of my 
  min(512,PSXDisplay.DisplayMode.y);                   // back buffer surface
 PSXDisplay.Interlaced    = PSXDisplay.InterlacedNew;
    
 PSXDisplay.DisplayEnd.x=                              // calc end of display
  PSXDisplay.DisplayPosition.x+ PSXDisplay.DisplayMode.x;
 PSXDisplay.DisplayEnd.y=
  PSXDisplay.DisplayPosition.y+ PSXDisplay.DisplayMode.y+PreviousPSXDisplay.DisplayModeNew.y;
 PreviousPSXDisplay.DisplayEnd.x=
  PreviousPSXDisplay.DisplayPosition.x+ PSXDisplay.DisplayMode.x;
 PreviousPSXDisplay.DisplayEnd.y=
  PreviousPSXDisplay.DisplayPosition.y+ PSXDisplay.DisplayMode.y+PreviousPSXDisplay.DisplayModeNew.y;

 ChangeDispOffsetsX();

 if(iFrameLimit==2) SetAutoFrameCap();                 // -> set it

 if(UseFrameSkip) updateDisplay();                     // stupid stuff when frame skipping enabled
}


////////////////////////////////////////////////////////////////////////
// gun cursor func: player=0-7, x=0-511, y=0-255
////////////////////////////////////////////////////////////////////////

void GPU_cursor(int iPlayer,int x,int y)
{
 if(iPlayer<0) return;
 if(iPlayer>7) return;

 usCursorActive|=(1<<iPlayer);

 if(x<0)       x=0;
 if(x>511)     x=511;
 if(y<0)       y=0;
 if(y>255)     y=255;

 ptCursorPoint[iPlayer].x=x;
 ptCursorPoint[iPlayer].y=y;
}

////////////////////////////////////////////////////////////////////////
// update lace is called evry VSync
////////////////////////////////////////////////////////////////////////

void GPU_updateLace(void)                      // VSYNC
{
 if(!(dwActFixes&1))
  lGPUstatusRet^=0x80000000;                           // odd/even bit

 if(!(dwActFixes&32))                                  // std fps limitation?
  CheckFrameRate();

 if(PSXDisplay.Interlaced)                             // interlaced mode?
  {
   if(bDoVSyncUpdate && PSXDisplay.DisplayMode.x>0 && PSXDisplay.DisplayMode.y>0)
    {
     updateDisplay();
    }
  }
 else                                                  // non-interlaced?
  {
   if(dwActFixes&64)                                   // lazy screen update fix
    {
     if(bDoLazyUpdate && !UseFrameSkip) 
      updateDisplay(); 
     bDoLazyUpdate=FALSE;
    }
   else
    {
     if(bDoVSyncUpdate && !UseFrameSkip)               // some primitives drawn?
      updateDisplay();                                 // -> update display
    }
  }

 bDoVSyncUpdate=FALSE;                                 // vsync done
}

////////////////////////////////////////////////////////////////////////
// process read request from GPU status register
////////////////////////////////////////////////////////////////////////


uint32_t GPU_readStatus(void)             // READ STATUS
{
 if(dwActFixes&1)
  {
   static int iNumRead=0;                         // odd/even hack
   if((iNumRead++)==2)
    {
     iNumRead=0;
     lGPUstatusRet^=0x80000000;                   // interlaced bit toggle... we do it on every 3 read status... needed by some games (like ChronoCross) with old epsxe versions (1.5.2 and older)
    }
  }

 if(iFakePrimBusy)                                // 27.10.2007 - PETE : emulating some 'busy' while drawing... pfff
  {
   iFakePrimBusy--;

   if(iFakePrimBusy&1)                            // we do a busy-idle-busy-idle sequence after/while drawing prims
    {
     GPUIsBusy;
     GPUIsNotReadyForCommands;
    }
   else
    {
     GPUIsIdle;
     GPUIsReadyForCommands;
    }
  }
 return lGPUstatusRet;
}

////////////////////////////////////////////////////////////////////////
// processes data send to GPU status register
// these are always single packet commands.
////////////////////////////////////////////////////////////////////////

void GPU_writeStatus(uint32_t gdata)      // WRITE STATUS
{
 uint32_t lCommand=(gdata>>24)&0xff;

 ulStatusControl[lCommand]=gdata;                      // store command for freezing

 switch(lCommand)
  {
   //--------------------------------------------------//
   // reset gpu
   case 0x00:
    memset(lGPUInfoVals,0x00,16*sizeof(uint32_t));
    lGPUstatusRet=0x14802000;
    PSXDisplay.Disabled=1;
    DataWriteMode=DataReadMode=DR_NORMAL;
    PSXDisplay.DrawOffset.x=PSXDisplay.DrawOffset.y=0;
    drawX=drawY=0;drawW=drawH=0;
    sSetMask=0;lSetMask=0;bCheckMask=FALSE;
    usMirror=0;
    GlobalTextAddrX=0;GlobalTextAddrY=0;
    GlobalTextTP=0;GlobalTextABR=0;
    PSXDisplay.RGB24=FALSE;
    PSXDisplay.Interlaced=FALSE;
    bUsingTWin = FALSE;
    return;
   //--------------------------------------------------//
   // dis/enable display 
   case 0x03:  

    PreviousPSXDisplay.Disabled = PSXDisplay.Disabled;
    PSXDisplay.Disabled = (gdata & 1);

    if(PSXDisplay.Disabled) 
         lGPUstatusRet|=GPUSTATUS_DISPLAYDISABLED;
    else lGPUstatusRet&=~GPUSTATUS_DISPLAYDISABLED;
    return;

   //--------------------------------------------------//
   // setting transfer mode
   case 0x04:
    gdata &= 0x03;                                     // Only want the lower two bits

    DataWriteMode=DataReadMode=DR_NORMAL;
    if(gdata==0x02) DataWriteMode=DR_VRAMTRANSFER;
    if(gdata==0x03) DataReadMode =DR_VRAMTRANSFER;
    lGPUstatusRet&=~GPUSTATUS_DMABITS;                 // Clear the current settings of the DMA bits
    lGPUstatusRet|=(gdata << 29);                      // Set the DMA bits according to the received data

    return;
   //--------------------------------------------------//
   // setting display position
   case 0x05: 
    {
     PreviousPSXDisplay.DisplayPosition.x = PSXDisplay.DisplayPosition.x;
     PreviousPSXDisplay.DisplayPosition.y = PSXDisplay.DisplayPosition.y;

////////
/*
     PSXDisplay.DisplayPosition.y = (short)((gdata>>10)&0x3ff);
     if (PSXDisplay.DisplayPosition.y & 0x200) 
      PSXDisplay.DisplayPosition.y |= 0xfffffc00;
     if(PSXDisplay.DisplayPosition.y<0) 
      {
       PreviousPSXDisplay.DisplayModeNew.y=PSXDisplay.DisplayPosition.y/PSXDisplay.Double;
       PSXDisplay.DisplayPosition.y=0;
      }
     else PreviousPSXDisplay.DisplayModeNew.y=0;
*/

// new
     if(iGPUHeight==1024)
      {
       if(dwGPUVersion==2) 
            PSXDisplay.DisplayPosition.y = (short)((gdata>>12)&0x3ff);
       else PSXDisplay.DisplayPosition.y = (short)((gdata>>10)&0x3ff);
      }
     else PSXDisplay.DisplayPosition.y = (short)((gdata>>10)&0x1ff);

     // store the same val in some helper var, we need it on later compares
     PreviousPSXDisplay.DisplayModeNew.x=PSXDisplay.DisplayPosition.y;

     if((PSXDisplay.DisplayPosition.y+PSXDisplay.DisplayMode.y)>iGPUHeight)
      {
       int dy1=iGPUHeight-PSXDisplay.DisplayPosition.y;
       int dy2=(PSXDisplay.DisplayPosition.y+PSXDisplay.DisplayMode.y)-iGPUHeight;

       if(dy1>=dy2)
        {
         PreviousPSXDisplay.DisplayModeNew.y=-dy2;
        }
       else
        {
         PSXDisplay.DisplayPosition.y=0;
         PreviousPSXDisplay.DisplayModeNew.y=-dy1;
        }
      }
     else PreviousPSXDisplay.DisplayModeNew.y=0;
// eon

     PSXDisplay.DisplayPosition.x = (short)(gdata & 0x3ff);
     PSXDisplay.DisplayEnd.x=
      PSXDisplay.DisplayPosition.x+ PSXDisplay.DisplayMode.x;
     PSXDisplay.DisplayEnd.y=
      PSXDisplay.DisplayPosition.y+ PSXDisplay.DisplayMode.y + PreviousPSXDisplay.DisplayModeNew.y;
     PreviousPSXDisplay.DisplayEnd.x=
      PreviousPSXDisplay.DisplayPosition.x+ PSXDisplay.DisplayMode.x;
     PreviousPSXDisplay.DisplayEnd.y=
      PreviousPSXDisplay.DisplayPosition.y+ PSXDisplay.DisplayMode.y + PreviousPSXDisplay.DisplayModeNew.y;
 
     bDoVSyncUpdate=TRUE;

     if (!(PSXDisplay.Interlaced))                      // stupid frame skipping option
      {
       if(UseFrameSkip)  updateDisplay();
       if(dwActFixes&64) bDoLazyUpdate=TRUE;
      }
    }return;
   //--------------------------------------------------//
   // setting width
   case 0x06:

    PSXDisplay.Range.x0=(short)(gdata & 0x7ff);
    PSXDisplay.Range.x1=(short)((gdata>>12) & 0xfff);

    PSXDisplay.Range.x1-=PSXDisplay.Range.x0;

    ChangeDispOffsetsX();

    return;
   //--------------------------------------------------//
   // setting height
   case 0x07:
    {

     PSXDisplay.Range.y0=(short)(gdata & 0x3ff);
     PSXDisplay.Range.y1=(short)((gdata>>10) & 0x3ff);
                                      
     PreviousPSXDisplay.Height = PSXDisplay.Height;

     PSXDisplay.Height = PSXDisplay.Range.y1 - 
                         PSXDisplay.Range.y0 +
                         PreviousPSXDisplay.DisplayModeNew.y;

     if(PreviousPSXDisplay.Height!=PSXDisplay.Height)
      {
       PSXDisplay.DisplayModeNew.y=PSXDisplay.Height*PSXDisplay.Double;

       ChangeDispOffsetsY();

       updateDisplayIfChanged();
      }
     return;
    }
   //--------------------------------------------------//
   // setting display infos
   case 0x08:

    PSXDisplay.DisplayModeNew.x =
     sDispWidths[(gdata & 0x03) | ((gdata & 0x40) >> 4)];

    if (gdata&0x04) PSXDisplay.Double=2;
    else            PSXDisplay.Double=1;

    PSXDisplay.DisplayModeNew.y = PSXDisplay.Height*PSXDisplay.Double;

    ChangeDispOffsetsY();

    PSXDisplay.PAL           = (gdata & 0x08)?TRUE:FALSE; // if 1 - PAL mode, else NTSC
    PSXDisplay.RGB24New      = (gdata & 0x10)?TRUE:FALSE; // if 1 - TrueColor
    PSXDisplay.InterlacedNew = (gdata & 0x20)?TRUE:FALSE; // if 1 - Interlace

    lGPUstatusRet&=~GPUSTATUS_WIDTHBITS;                   // Clear the width bits
    lGPUstatusRet|=
               (((gdata & 0x03) << 17) | 
               ((gdata & 0x40) << 10));                // Set the width bits

    if(PSXDisplay.InterlacedNew)
     {
      if(!PSXDisplay.Interlaced)
       {
        PreviousPSXDisplay.DisplayPosition.x = PSXDisplay.DisplayPosition.x;
        PreviousPSXDisplay.DisplayPosition.y = PSXDisplay.DisplayPosition.y;
       }
      lGPUstatusRet|=GPUSTATUS_INTERLACED;
     }
    else lGPUstatusRet&=~GPUSTATUS_INTERLACED;

    if (PSXDisplay.PAL)
         lGPUstatusRet|=GPUSTATUS_PAL;
    else lGPUstatusRet&=~GPUSTATUS_PAL;

    if (PSXDisplay.Double==2)
         lGPUstatusRet|=GPUSTATUS_DOUBLEHEIGHT;
    else lGPUstatusRet&=~GPUSTATUS_DOUBLEHEIGHT;

    if (PSXDisplay.RGB24New)
         lGPUstatusRet|=GPUSTATUS_RGB24;
    else lGPUstatusRet&=~GPUSTATUS_RGB24;

    updateDisplayIfChanged();

    return;
   //--------------------------------------------------//
   // ask about GPU version and other stuff
   case 0x10: 

    gdata&=0xff;

    switch(gdata) 
     {
      case 0x02:
       lGPUdataRet=lGPUInfoVals[INFO_TW];              // tw infos
       return;
      case 0x03:
       lGPUdataRet=lGPUInfoVals[INFO_DRAWSTART];       // draw start
       return;
      case 0x04:
       lGPUdataRet=lGPUInfoVals[INFO_DRAWEND];         // draw end
       return;
      case 0x05:
      case 0x06:
       lGPUdataRet=lGPUInfoVals[INFO_DRAWOFF];         // draw offset
       return;
      case 0x07:
       if(dwGPUVersion==2)
            lGPUdataRet=0x01;
       else lGPUdataRet=0x02;                          // gpu type
       return;
      case 0x08:
      case 0x0F:                                       // some bios addr?
       lGPUdataRet=0xBFC03720;
       return;
     }
    return;
   //--------------------------------------------------//
  }   
}

////////////////////////////////////////////////////////////////////////
// vram read/write helpers, needed by LEWPY's optimized vram read/write :)
////////////////////////////////////////////////////////////////////////

__inline void FinishedVRAMWrite(void)
{
/*
// NEWX
 if(!PSXDisplay.Interlaced && UseFrameSkip)            // stupid frame skipping
  {
   VRAMWrite.Width +=VRAMWrite.x;
   VRAMWrite.Height+=VRAMWrite.y;
   if(VRAMWrite.x<PSXDisplay.DisplayEnd.x &&
      VRAMWrite.Width >=PSXDisplay.DisplayPosition.x &&
      VRAMWrite.y<PSXDisplay.DisplayEnd.y &&
      VRAMWrite.Height>=PSXDisplay.DisplayPosition.y)
    updateDisplay();
  }
*/

 // Set register to NORMAL operation
 DataWriteMode = DR_NORMAL;
 // Reset transfer values, to prevent mis-transfer of data
 VRAMWrite.x = 0;
 VRAMWrite.y = 0;
 VRAMWrite.Width = 0;
 VRAMWrite.Height = 0;
 VRAMWrite.ColsRemaining = 0;
 VRAMWrite.RowsRemaining = 0;
}

__inline void FinishedVRAMRead(void)
{
 // Set register to NORMAL operation
 DataReadMode = DR_NORMAL;
 // Reset transfer values, to prevent mis-transfer of data
 VRAMRead.x = 0;
 VRAMRead.y = 0;
 VRAMRead.Width = 0;
 VRAMRead.Height = 0;
 VRAMRead.ColsRemaining = 0;
 VRAMRead.RowsRemaining = 0;

 // Indicate GPU is no longer ready for VRAM data in the STATUS REGISTER
 lGPUstatusRet&=~GPUSTATUS_READYFORVRAM;
}

////////////////////////////////////////////////////////////////////////
// core read from vram
////////////////////////////////////////////////////////////////////////

void GPU_readDataMem(uint32_t * pMem, int iSize)
{
 int i;

 if(DataReadMode!=DR_VRAMTRANSFER) return;

 GPUIsBusy;

 // adjust read ptr, if necessary
 while(VRAMRead.ImagePtr>=psxVuw_eom)
  VRAMRead.ImagePtr-=iGPUHeight*1024;
 while(VRAMRead.ImagePtr<psxVuw)
  VRAMRead.ImagePtr+=iGPUHeight*1024;

 for(i=0;i<iSize;i++)
  {
   // do 2 seperate 16bit reads for compatibility (wrap issues)
   if ((VRAMRead.ColsRemaining > 0) && (VRAMRead.RowsRemaining > 0))
    {
     // lower 16 bit
     lGPUdataRet=(uint32_t)GETLE16(VRAMRead.ImagePtr);

     VRAMRead.ImagePtr++;
     if(VRAMRead.ImagePtr>=psxVuw_eom) VRAMRead.ImagePtr-=iGPUHeight*1024;
     VRAMRead.RowsRemaining --;

     if(VRAMRead.RowsRemaining<=0)
      {
       VRAMRead.RowsRemaining = VRAMRead.Width;
       VRAMRead.ColsRemaining--;
       VRAMRead.ImagePtr += 1024 - VRAMRead.Width;
       if(VRAMRead.ImagePtr>=psxVuw_eom) VRAMRead.ImagePtr-=iGPUHeight*1024;
      }

     // higher 16 bit (always, even if it's an odd width)
     lGPUdataRet|=(uint32_t)GETLE16(VRAMRead.ImagePtr)<<16;
     PUTLE32(pMem, lGPUdataRet); pMem++;

     if(VRAMRead.ColsRemaining <= 0)
      {FinishedVRAMRead();goto ENDREAD;}

     VRAMRead.ImagePtr++;
     if(VRAMRead.ImagePtr>=psxVuw_eom) VRAMRead.ImagePtr-=iGPUHeight*1024;
     VRAMRead.RowsRemaining--;
     if(VRAMRead.RowsRemaining<=0)
      {
       VRAMRead.RowsRemaining = VRAMRead.Width;
       VRAMRead.ColsRemaining--;
       VRAMRead.ImagePtr += 1024 - VRAMRead.Width;
       if(VRAMRead.ImagePtr>=psxVuw_eom) VRAMRead.ImagePtr-=iGPUHeight*1024;
      }
     if(VRAMRead.ColsRemaining <= 0)
      {FinishedVRAMRead();goto ENDREAD;}
    }
   else {FinishedVRAMRead();goto ENDREAD;}
  }

ENDREAD:
 GPUIsIdle;
}


////////////////////////////////////////////////////////////////////////

uint32_t GPU_readData(void)
{
 uint32_t l;
 GPU_readDataMem(&l,1);
 return lGPUdataRet;
}

////////////////////////////////////////////////////////////////////////
// processes data send to GPU data register
// extra table entries for fixing polyline troubles
////////////////////////////////////////////////////////////////////////

const unsigned char primTableCX[256] =
{
    // 00
    0,0,3,0,0,0,0,0,
    // 08
    0,0,0,0,0,0,0,0,
    // 10
    0,0,0,0,0,0,0,0,
    // 18
    0,0,0,0,0,0,0,0,
    // 20
    4,4,4,4,7,7,7,7,
    // 28
    5,5,5,5,9,9,9,9,
    // 30
    6,6,6,6,9,9,9,9,
    // 38
    8,8,8,8,12,12,12,12,
    // 40
    3,3,3,3,0,0,0,0,
    // 48
//  5,5,5,5,6,6,6,6,    // FLINE
    254,254,254,254,254,254,254,254,
    // 50
    4,4,4,4,0,0,0,0,
    // 58
//  7,7,7,7,9,9,9,9,    // GLINE
    255,255,255,255,255,255,255,255,
    // 60
    3,3,3,3,4,4,4,4,    
    // 68
    2,2,2,2,3,3,3,3,    // 3=SPRITE1???
    // 70
    2,2,2,2,3,3,3,3,
    // 78
    2,2,2,2,3,3,3,3,
    // 80
    4,0,0,0,0,0,0,0,
    // 88
    0,0,0,0,0,0,0,0,
    // 90
    0,0,0,0,0,0,0,0,
    // 98
    0,0,0,0,0,0,0,0,
    // a0
    3,0,0,0,0,0,0,0,
    // a8
    0,0,0,0,0,0,0,0,
    // b0
    0,0,0,0,0,0,0,0,
    // b8
    0,0,0,0,0,0,0,0,
    // c0
    3,0,0,0,0,0,0,0,
    // c8
    0,0,0,0,0,0,0,0,
    // d0
    0,0,0,0,0,0,0,0,
    // d8
    0,0,0,0,0,0,0,0,
    // e0
    0,1,1,1,1,1,1,0,
    // e8
    0,0,0,0,0,0,0,0,
    // f0
    0,0,0,0,0,0,0,0,
    // f8
    0,0,0,0,0,0,0,0
};

void GPU_writeDataMem(uint32_t * pMem, int iSize)
{
 unsigned char command;
 uint32_t gdata=0;
 int i=0;
 GPUIsBusy;
 GPUIsNotReadyForCommands;

STARTVRAM:

 if(DataWriteMode==DR_VRAMTRANSFER)
  {
   BOOL bFinished=FALSE;

   // make sure we are in vram
   while(VRAMWrite.ImagePtr>=psxVuw_eom)
    VRAMWrite.ImagePtr-=iGPUHeight*1024;
   while(VRAMWrite.ImagePtr<psxVuw)
    VRAMWrite.ImagePtr+=iGPUHeight*1024;

   // now do the loop
   while(VRAMWrite.ColsRemaining>0)
    {
     while(VRAMWrite.RowsRemaining>0)
      {
       if(i>=iSize) {goto ENDVRAM;}
       i++;

       gdata=GETLE32(pMem); pMem++;

       PUTLE16(VRAMWrite.ImagePtr, (unsigned short)gdata); VRAMWrite.ImagePtr++;
       if(VRAMWrite.ImagePtr>=psxVuw_eom) VRAMWrite.ImagePtr-=iGPUHeight*1024;
       VRAMWrite.RowsRemaining --;

       if(VRAMWrite.RowsRemaining <= 0)
        {
         VRAMWrite.ColsRemaining--;
         if (VRAMWrite.ColsRemaining <= 0)             // last pixel is odd width
          {
           gdata=(gdata&0xFFFF)|(((uint32_t)GETLE16(VRAMWrite.ImagePtr))<<16);
           FinishedVRAMWrite();
           bDoVSyncUpdate=TRUE;
           goto ENDVRAM;
          }
         VRAMWrite.RowsRemaining = VRAMWrite.Width;
         VRAMWrite.ImagePtr += 1024 - VRAMWrite.Width;
        }

       PUTLE16(VRAMWrite.ImagePtr, (unsigned short)(gdata>>16)); VRAMWrite.ImagePtr++;
       if(VRAMWrite.ImagePtr>=psxVuw_eom) VRAMWrite.ImagePtr-=iGPUHeight*1024;
       VRAMWrite.RowsRemaining --;
      }

     VRAMWrite.RowsRemaining = VRAMWrite.Width;
     VRAMWrite.ColsRemaining--;
     VRAMWrite.ImagePtr += 1024 - VRAMWrite.Width;
     bFinished=TRUE;
    }

   FinishedVRAMWrite();
   if(bFinished) bDoVSyncUpdate=TRUE;
  }

ENDVRAM:

 if(DataWriteMode==DR_NORMAL)
  {
   void (* *primFunc)(unsigned char *);
   if(bSkipNextFrame) primFunc=primTableSkip;
   else               primFunc=primTableJ;

   for(;i<iSize;)
    {
     if(DataWriteMode==DR_VRAMTRANSFER) goto STARTVRAM;

     gdata=GETLE32(pMem); pMem++; i++;
 
     if(gpuDataC == 0)
      {
       command = (unsigned char)((gdata>>24) & 0xff);
 
//if(command>=0xb0 && command<0xc0) auxprintf("b0 %x!!!!!!!!!\n",command);

       if(primTableCX[command])
        {
         gpuDataC = primTableCX[command];
         gpuCommand = command;
         PUTLE32(&gpuDataM[0], gdata);
         gpuDataP = 1;
        }
       else continue;
      }
     else
      {
       PUTLE32(&gpuDataM[gpuDataP], gdata);
       if(gpuDataC>128)
        {
         if((gpuDataC==254 && gpuDataP>=3) ||
            (gpuDataC==255 && gpuDataP>=4 && !(gpuDataP&1)))
          {
           if((gpuDataM[gpuDataP] & 0xF000F000) == 0x50005000)
            gpuDataP=gpuDataC-1;
          }
        }
       gpuDataP++;
      }
 
     if(gpuDataP == gpuDataC)
      {
       gpuDataC=gpuDataP=0;
       primFunc[gpuCommand]((unsigned char *)gpuDataM);
       if(dwEmuFixes&0x0001 || dwActFixes&0x0400)      // hack for emulating "gpu busy" in some games
        iFakePrimBusy=4;
      }
    } 
  }

 lGPUdataRet=gdata;

 GPUIsReadyForCommands;
 GPUIsIdle;                
}

////////////////////////////////////////////////////////////////////////

void GPU_writeData(uint32_t gdata)
{
 PUTLE32(&gdata, gdata);
 GPU_writeDataMem(&gdata,1);
}

////////////////////////////////////////////////////////////////////////
// this functions will be removed soon (or 'soonish')... not really needed, but some emus want them
////////////////////////////////////////////////////////////////////////

void GPU_setMode(unsigned long gdata)
{
// Peops does nothing here...
// DataWriteMode=(gdata&1)?DR_VRAMTRANSFER:DR_NORMAL;
// DataReadMode =(gdata&2)?DR_VRAMTRANSFER:DR_NORMAL;
}

long GPU_getMode(void)
{
 long iT=0;

 if(DataWriteMode==DR_VRAMTRANSFER) iT|=0x1;
 if(DataReadMode ==DR_VRAMTRANSFER) iT|=0x2;
 return iT;
}

////////////////////////////////////////////////////////////////////////
// call config dlg
////////////////////////////////////////////////////////////////////////

long GPU_configure(void)
{
 SoftDlgProc();

 return 0;
}

////////////////////////////////////////////////////////////////////////
// sets all kind of act fixes
////////////////////////////////////////////////////////////////////////

void SetFixes(void)
 {
  if(dwActFixes&0x02) sDispWidths[4]=384;
  else                sDispWidths[4]=368;
 }

////////////////////////////////////////////////////////////////////////
// process gpu commands
////////////////////////////////////////////////////////////////////////

unsigned long lUsedAddr[3];

__inline BOOL CheckForEndlessLoop(unsigned long laddr)
{
 if(laddr==lUsedAddr[1]) return TRUE;
 if(laddr==lUsedAddr[2]) return TRUE;

 if(laddr<lUsedAddr[0]) lUsedAddr[1]=laddr;
 else                   lUsedAddr[2]=laddr;
 lUsedAddr[0]=laddr;
 return FALSE;
}

long GPU_dmaChain(uint32_t * baseAddrL, uint32_t addr)
{
 uint32_t dmaMem;
 unsigned char * baseAddrB;
 short count;unsigned int DMACommandCounter = 0;

 GPUIsBusy;

 lUsedAddr[0]=lUsedAddr[1]=lUsedAddr[2]=0xffffff;

 baseAddrB = (unsigned char*) baseAddrL;

 do
  {
   if(iGPUHeight==512) addr&=0x1FFFFC;
   if(DMACommandCounter++ > 2000000) break;
   if(CheckForEndlessLoop(addr)) break;

   count = baseAddrB[addr+3];

   dmaMem=addr+4;

   if(count>0) GPU_writeDataMem(&baseAddrL[dmaMem>>2],count);

   addr = GETLE32(&baseAddrL[addr>>2])&0xffffff;
  }
 while (addr != 0xffffff);

 GPUIsIdle;

 return 0;
}

////////////////////////////////////////////////////////////////////////
// show about dlg
////////////////////////////////////////////////////////////////////////


void GPU_about(void)                           // ABOUT
{
 AboutDlgProc();
 return;
}

////////////////////////////////////////////////////////////////////////
// We are ever fine ;)
////////////////////////////////////////////////////////////////////////

long GPU_test(void)
{
 // if test fails this function should return negative value for error (unable to continue)
 // and positive value for warning (can continue but output might be crappy)
 return 0;
}

////////////////////////////////////////////////////////////////////////
// Freeze
////////////////////////////////////////////////////////////////////////
long GPU_freeze(uint32_t ulGetFreezeData,GPUFreeze_t * pF)
{
 //----------------------------------------------------//
 if(ulGetFreezeData==2)                                // 2: info, which save slot is selected? (just for display)
  {
   long lSlotNum=*((long *)pF);
   if(lSlotNum<0) return 0;
   if(lSlotNum>8) return 0;
   lSelectedSlot=lSlotNum+1;
   BuildDispMenu(0);
   return 1;
  }
 //----------------------------------------------------//
 if(!pF)                    return 0;                  // some checks
 if(pF->ulFreezeVersion!=1) return 0;

 if(ulGetFreezeData==1)                                // 1: get data
  {
   pF->ulStatus=lGPUstatusRet;
   memcpy(pF->ulControl,ulStatusControl,256*sizeof(uint32_t));
   memcpy(pF->psxVRam,  psxVub,         1024*iGPUHeight*2);

   return 1;
  }

 if(ulGetFreezeData!=0) return 0;                      // 0: set data

 lGPUstatusRet=pF->ulStatus;
 memcpy(ulStatusControl,pF->ulControl,256*sizeof(uint32_t));
 memcpy(psxVub,         pF->psxVRam,  1024*iGPUHeight*2);

// RESET TEXTURE STORE HERE, IF YOU USE SOMETHING LIKE THAT

 GPU_writeStatus(ulStatusControl[0]);
 GPU_writeStatus(ulStatusControl[1]);
 GPU_writeStatus(ulStatusControl[2]);
 GPU_writeStatus(ulStatusControl[3]);
 GPU_writeStatus(ulStatusControl[8]);                   // try to repair things
 GPU_writeStatus(ulStatusControl[6]);
 GPU_writeStatus(ulStatusControl[7]);
 GPU_writeStatus(ulStatusControl[5]);
 GPU_writeStatus(ulStatusControl[4]);

 return 1;
}

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////
// SAVE STATE DISPLAY STUFF
////////////////////////////////////////////////////////////////////////

// font 0-9, 24x20 pixels, 1 byte = 4 dots
// 00 = black
// 01 = white
// 10 = red
// 11 = transparent

unsigned char cFont[10][120]=
{
// 0
{0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x05,0x54,0x00,0x00,
 0x80,0x00,0x14,0x05,0x00,0x00,
 0x80,0x00,0x14,0x05,0x00,0x00,
 0x80,0x00,0x14,0x05,0x00,0x00,
 0x80,0x00,0x14,0x05,0x00,0x00,
 0x80,0x00,0x14,0x05,0x00,0x00,
 0x80,0x00,0x14,0x05,0x00,0x00,
 0x80,0x00,0x14,0x05,0x00,0x00,
 0x80,0x00,0x14,0x05,0x00,0x00,
 0x80,0x00,0x05,0x54,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0xaa,0xaa,0xaa,0xaa,0xaa,0xaa
},
// 1
{0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x00,0x50,0x00,0x00,
 0x80,0x00,0x05,0x50,0x00,0x00,
 0x80,0x00,0x00,0x50,0x00,0x00,
 0x80,0x00,0x00,0x50,0x00,0x00,
 0x80,0x00,0x00,0x50,0x00,0x00,
 0x80,0x00,0x00,0x50,0x00,0x00,
 0x80,0x00,0x00,0x50,0x00,0x00,
 0x80,0x00,0x00,0x50,0x00,0x00,
 0x80,0x00,0x00,0x50,0x00,0x00,
 0x80,0x00,0x05,0x55,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0xaa,0xaa,0xaa,0xaa,0xaa,0xaa
},
// 2
{0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x05,0x54,0x00,0x00,
 0x80,0x00,0x14,0x05,0x00,0x00,
 0x80,0x00,0x00,0x05,0x00,0x00,
 0x80,0x00,0x00,0x05,0x00,0x00,
 0x80,0x00,0x00,0x14,0x00,0x00,
 0x80,0x00,0x00,0x50,0x00,0x00,
 0x80,0x00,0x01,0x40,0x00,0x00,
 0x80,0x00,0x05,0x00,0x00,0x00,
 0x80,0x00,0x14,0x00,0x00,0x00,
 0x80,0x00,0x15,0x55,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0xaa,0xaa,0xaa,0xaa,0xaa,0xaa
},
// 3
{0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x05,0x54,0x00,0x00,
 0x80,0x00,0x14,0x05,0x00,0x00,
 0x80,0x00,0x00,0x05,0x00,0x00,
 0x80,0x00,0x00,0x05,0x00,0x00,
 0x80,0x00,0x01,0x54,0x00,0x00,
 0x80,0x00,0x00,0x05,0x00,0x00,
 0x80,0x00,0x00,0x05,0x00,0x00,
 0x80,0x00,0x00,0x05,0x00,0x00,
 0x80,0x00,0x14,0x05,0x00,0x00,
 0x80,0x00,0x05,0x54,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0xaa,0xaa,0xaa,0xaa,0xaa,0xaa
},
// 4
{0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x00,0x14,0x00,0x00,
 0x80,0x00,0x00,0x54,0x00,0x00,
 0x80,0x00,0x01,0x54,0x00,0x00,
 0x80,0x00,0x01,0x54,0x00,0x00,
 0x80,0x00,0x05,0x14,0x00,0x00,
 0x80,0x00,0x14,0x14,0x00,0x00,
 0x80,0x00,0x15,0x55,0x00,0x00,
 0x80,0x00,0x00,0x14,0x00,0x00,
 0x80,0x00,0x00,0x14,0x00,0x00,
 0x80,0x00,0x00,0x55,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0xaa,0xaa,0xaa,0xaa,0xaa,0xaa
},
// 5
{0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x15,0x55,0x00,0x00,
 0x80,0x00,0x14,0x00,0x00,0x00,
 0x80,0x00,0x14,0x00,0x00,0x00,
 0x80,0x00,0x14,0x00,0x00,0x00,
 0x80,0x00,0x15,0x54,0x00,0x00,
 0x80,0x00,0x00,0x05,0x00,0x00,
 0x80,0x00,0x00,0x05,0x00,0x00,
 0x80,0x00,0x00,0x05,0x00,0x00,
 0x80,0x00,0x14,0x05,0x00,0x00,
 0x80,0x00,0x05,0x54,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0xaa,0xaa,0xaa,0xaa,0xaa,0xaa
},
// 6
{0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x01,0x54,0x00,0x00,
 0x80,0x00,0x05,0x00,0x00,0x00,
 0x80,0x00,0x14,0x00,0x00,0x00,
 0x80,0x00,0x14,0x00,0x00,0x00,
 0x80,0x00,0x15,0x54,0x00,0x00,
 0x80,0x00,0x15,0x05,0x00,0x00,
 0x80,0x00,0x14,0x05,0x00,0x00,
 0x80,0x00,0x14,0x05,0x00,0x00,
 0x80,0x00,0x14,0x05,0x00,0x00,
 0x80,0x00,0x05,0x54,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0xaa,0xaa,0xaa,0xaa,0xaa,0xaa
},
// 7
{0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x15,0x55,0x00,0x00,
 0x80,0x00,0x14,0x05,0x00,0x00,
 0x80,0x00,0x00,0x14,0x00,0x00,
 0x80,0x00,0x00,0x14,0x00,0x00,
 0x80,0x00,0x00,0x50,0x00,0x00,
 0x80,0x00,0x00,0x50,0x00,0x00,
 0x80,0x00,0x01,0x40,0x00,0x00,
 0x80,0x00,0x01,0x40,0x00,0x00,
 0x80,0x00,0x05,0x00,0x00,0x00,
 0x80,0x00,0x05,0x00,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0xaa,0xaa,0xaa,0xaa,0xaa,0xaa
},
// 8
{0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x05,0x54,0x00,0x00,
 0x80,0x00,0x14,0x05,0x00,0x00,
 0x80,0x00,0x14,0x05,0x00,0x00,
 0x80,0x00,0x14,0x05,0x00,0x00,
 0x80,0x00,0x05,0x54,0x00,0x00,
 0x80,0x00,0x14,0x05,0x00,0x00,
 0x80,0x00,0x14,0x05,0x00,0x00,
 0x80,0x00,0x14,0x05,0x00,0x00,
 0x80,0x00,0x14,0x05,0x00,0x00,
 0x80,0x00,0x05,0x54,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0xaa,0xaa,0xaa,0xaa,0xaa,0xaa
},
// 9
{0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x05,0x54,0x00,0x00,
 0x80,0x00,0x14,0x05,0x00,0x00,
 0x80,0x00,0x14,0x05,0x00,0x00,
 0x80,0x00,0x14,0x05,0x00,0x00,
 0x80,0x00,0x14,0x15,0x00,0x00,
 0x80,0x00,0x05,0x55,0x00,0x00,
 0x80,0x00,0x00,0x05,0x00,0x00,
 0x80,0x00,0x00,0x05,0x00,0x00,
 0x80,0x00,0x00,0x14,0x00,0x00,
 0x80,0x00,0x05,0x50,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0x80,0x00,0x00,0x00,0x00,0x00,
 0xaa,0xaa,0xaa,0xaa,0xaa,0xaa
}
};

////////////////////////////////////////////////////////////////////////

void PaintPicDot(unsigned char * p,unsigned char c)
{

 if(c==0) {*p++=0x00;*p++=0x00;*p=0x00;return;}        // black
 if(c==1) {*p++=0xff;*p++=0xff;*p=0xff;return;}        // white
 if(c==2) {*p++=0x00;*p++=0x00;*p=0xff;return;}        // red
                                                       // transparent
}

////////////////////////////////////////////////////////////////////////
// the main emu allocs 128x96x3 bytes, and passes a ptr
// to it in pMem... the plugin has to fill it with
// 8-8-8 bit BGR screen data (Win 24 bit BMP format 
// without header). 
// Beware: the func can be called at any time,
// so you have to use the frontbuffer to get a fully
// rendered picture

// LINUX version:

extern char * Xpixels;

void GPUgetScreenPic(unsigned char * pMem)
{
}


////////////////////////////////////////////////////////////////////////
// func will be called with 128x96x3 BGR data.
// the plugin has to store the data and display
// it in the upper right corner.
// If the func is called with a NULL ptr, you can
// release your picture data and stop displaying
// the screen pic

void GPU_showScreenPic(unsigned char * pMem)
{
 DestroyPic();                                         // destroy old pic data
 if(pMem==0) return;                                   // done
 CreatePic(pMem);                                      // create new pic... don't free pMem or something like that... just read from it
}

void GPU_setfix(uint32_t dwFixBits)
{
 dwEmuFixes=dwFixBits;
}

void GPU_clearDynarec(void (*callback)(void)) {}

void GPU_vBlank(int val) {}

long GPU_getScreenPic(unsigned char *pMem) { return -1; }

#define VK_INSERT      65379
#define VK_HOME        65360
#define VK_PRIOR       65365
#define VK_NEXT        65366
#define VK_END         65367
#define VK_DEL         65535

void GPU_keypressed(int keycode) {
 switch(keycode) {
   case VK_INSERT:
       if(iUseFixes) {iUseFixes=0;dwActFixes=0;}
       else          {iUseFixes=1;dwActFixes=dwCfgFixes;}
       SetFixes();
       if(iFrameLimit==2) SetAutoFrameCap();
       break;

   case VK_DEL:
       if(ulKeybits&KEY_SHOWFPS)
        {
         ulKeybits&=~KEY_SHOWFPS;
         DoClearScreenBuffer();
        }
       else 
        {
         ulKeybits|=KEY_SHOWFPS;
         szDispBuf[0]=0;
         BuildDispMenu(0);
        }
       break;

   case VK_PRIOR: BuildDispMenu(-1);            break;
   case VK_NEXT:  BuildDispMenu( 1);            break;
   case VK_END:   SwitchDispMenu(1);            break;
   case VK_HOME:  SwitchDispMenu(-1);           break;
   case 0x60:
    {
     iFastFwd = 1 - iFastFwd;
     bSkipNextFrame = FALSE;
     UseFrameSkip = iFastFwd;
     BuildDispMenu(0);
     break;
    }
#ifdef _MACGL
   default: { void HandleKey(int keycode); HandleKey(keycode); }
#endif
  }
}

void SetKeyHandler(void)
{
}

void ReleaseKeyHandler(void)
{
}


#if 0

void GPU__displayText(char *pText) {
	SysPrintf("%s\n", pText);
}

long GPU_configure(void) { return 0; }
long GPU_test(void) { return 0; }
void GPU_about(void) {}
void GPU_makeSnapshot(void) {}
void GPU_keypressed(int key) {}
l
long GPU_showScreenPic(unsigned char *pMem) { return -1; }


#endif


