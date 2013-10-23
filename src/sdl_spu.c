/*
* Sound (SPU) functions.
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <fcntl.h>

#include <unistd.h>
#include <pthread.h>
#define RRand(range) (random()%range)  
#include <string.h> 
#include <sys/time.h>  
#include <math.h>  


#include "plugins.h"
#include "r3000a.h"

/////////////////////////////////////////////////////////
// generic defines
/////////////////////////////////////////////////////////

#define INLINE inline

#ifndef max
#define max(a,b)            (((a) > (b)) ? (a) : (b))
#define min(a,b)            (((a) < (b)) ? (a) : (b))
#endif

////////////////////////////////////////////////////////////////////////
// spu defines
////////////////////////////////////////////////////////////////////////

// sound buffer sizes
// 400 ms complete sound buffer
#define SOUNDSIZE   70560
// 137 ms test buffer... if less than that is buffered, a new upload will happen
#define TESTSIZE    24192

// num of channels
#define MAXCHAN     24

// ~ 1 ms of data
#define NSSIZE 45

///////////////////////////////////////////////////////////
// struct defines
///////////////////////////////////////////////////////////

// ADSR INFOS PER CHANNEL
typedef struct
{
 int            AttackModeExp;
 long           AttackTime;
 long           DecayTime;
 long           SustainLevel;
 int            SustainModeExp;
 long           SustainModeDec;
 long           SustainTime;
 int            ReleaseModeExp;
 unsigned long  ReleaseVal;
 long           ReleaseTime;
 long           ReleaseStartTime; 
 long           ReleaseVol; 
 long           lTime;
 long           lVolume;
} ADSRInfo;

typedef struct
{
 int            State;
 int            AttackModeExp;
 int            AttackRate;
 int            DecayRate;
 int            SustainLevel;
 int            SustainModeExp;
 int            SustainIncrease;
 int            SustainRate;
 int            ReleaseModeExp;
 int            ReleaseRate;
 int            EnvelopeVol;
 long           lVolume;
 long           lDummy1;
 long           lDummy2;
} ADSRInfoEx;
              
///////////////////////////////////////////////////////////

// Tmp Flags

// used for debug channel muting
#define FLAG_MUTE  1

// used for simple interpolation
#define FLAG_IPOL0 2
#define FLAG_IPOL1 4

///////////////////////////////////////////////////////////

// MAIN CHANNEL STRUCT
typedef struct
{
 // no mutexes used anymore... don't need them to sync access
 //HANDLE            hMutex;

 int               bNew;                               // start flag

 int               iSBPos;                             // mixing stuff
 int               spos;
 int               sinc;
 int               SB[32+32];                          // Pete added another 32 dwords in 1.6 ... prevents overflow issues with gaussian/cubic interpolation (thanx xodnizel!), and can be used for even better interpolations, eh? :)
 int               sval;

 unsigned char *   pStart;                             // start ptr into sound mem
 unsigned char *   pCurr;                              // current pos in sound mem
 unsigned char *   pLoop;                              // loop ptr in sound mem

 int               bOn;                                // is channel active (sample playing?)
 int               bStop;                              // is channel stopped (sample _can_ still be playing, ADSR Release phase)
 int               bReverb;                            // can we do reverb on this channel? must have ctrl register bit, to get active
 int               iActFreq;                           // current psx pitch
 int               iUsedFreq;                          // current pc pitch
 int               iLeftVolume;                        // left volume
 int               iLeftVolRaw;                        // left psx volume value
 int               bIgnoreLoop;                        // ignore loop bit, if an external loop address is used
 int               iMute;                              // mute mode
 int               iRightVolume;                       // right volume
 int               iRightVolRaw;                       // right psx volume value
 int               iRawPitch;                          // raw pitch (0...3fff)
 int               iIrqDone;                           // debug irq done flag
 int               s_1;                                // last decoding infos
 int               s_2;
 int               bRVBActive;                         // reverb active flag
 int               iRVBOffset;                         // reverb offset
 int               iRVBRepeat;                         // reverb repeat
 int               bNoise;                             // noise active flag
 int               bFMod;                              // freq mod (0=off, 1=sound channel, 2=freq channel)
 int               iRVBNum;                            // another reverb helper
 int               iOldNoise;                          // old noise val for this channel   
 ADSRInfo          ADSR;                               // active ADSR settings
 ADSRInfoEx        ADSRX;                              // next ADSR settings (will be moved to active on sample start)
} SPUCHAN;

///////////////////////////////////////////////////////////

typedef struct
{
 int StartAddr;      // reverb area start addr in samples
 int CurrAddr;       // reverb area curr addr in samples

 int VolLeft;
 int VolRight;
 int iLastRVBLeft;
 int iLastRVBRight;
 int iRVBLeft;
 int iRVBRight;

 int FB_SRC_A;       // (offset)
 int FB_SRC_B;       // (offset)
 int IIR_ALPHA;      // (coef.)
 int ACC_COEF_A;     // (coef.)
 int ACC_COEF_B;     // (coef.)
 int ACC_COEF_C;     // (coef.)
 int ACC_COEF_D;     // (coef.)
 int IIR_COEF;       // (coef.)
 int FB_ALPHA;       // (coef.)
 int FB_X;           // (coef.)
 int IIR_DEST_A0;    // (offset)
 int IIR_DEST_A1;    // (offset)
 int ACC_SRC_A0;     // (offset)
 int ACC_SRC_A1;     // (offset)
 int ACC_SRC_B0;     // (offset)
 int ACC_SRC_B1;     // (offset)
 int IIR_SRC_A0;     // (offset)
 int IIR_SRC_A1;     // (offset)
 int IIR_DEST_B0;    // (offset)
 int IIR_DEST_B1;    // (offset)
 int ACC_SRC_C0;     // (offset)
 int ACC_SRC_C1;     // (offset)
 int ACC_SRC_D0;     // (offset)
 int ACC_SRC_D1;     // (offset)
 int IIR_SRC_B1;     // (offset)
 int IIR_SRC_B0;     // (offset)
 int MIX_DEST_A0;    // (offset)
 int MIX_DEST_A1;    // (offset)
 int MIX_DEST_B0;    // (offset)
 int MIX_DEST_B1;    // (offset)
 int IN_COEF_L;      // (coef.)
 int IN_COEF_R;      // (coef.)
} REVERBInfo;


INLINE void MixXA(void);
INLINE void FeedXA(xa_decode_t *xap);
INLINE void FeedCDDA(unsigned char *pcm, int nBytes);


#define H_SPUReverbAddr  0x0da2
#define H_SPUirqAddr     0x0da4
#define H_SPUaddr        0x0da6
#define H_SPUdata        0x0da8
#define H_SPUctrl        0x0daa
#define H_SPUstat        0x0dae
#define H_SPUmvolL       0x0d80
#define H_SPUmvolR       0x0d82
#define H_SPUrvolL       0x0d84
#define H_SPUrvolR       0x0d86
#define H_SPUon1         0x0d88
#define H_SPUon2         0x0d8a
#define H_SPUoff1        0x0d8c
#define H_SPUoff2        0x0d8e
#define H_FMod1          0x0d90
#define H_FMod2          0x0d92
#define H_Noise1         0x0d94
#define H_Noise2         0x0d96
#define H_RVBon1         0x0d98
#define H_RVBon2         0x0d9a
#define H_SPUMute1       0x0d9c
#define H_SPUMute2       0x0d9e
#define H_CDLeft         0x0db0
#define H_CDRight        0x0db2
#define H_ExtLeft        0x0db4
#define H_ExtRight       0x0db6
#define H_Reverb         0x0dc0
#define H_SPUPitch0      0x0c04
#define H_SPUPitch1      0x0c14
#define H_SPUPitch2      0x0c24
#define H_SPUPitch3      0x0c34
#define H_SPUPitch4      0x0c44
#define H_SPUPitch5      0x0c54
#define H_SPUPitch6      0x0c64
#define H_SPUPitch7      0x0c74
#define H_SPUPitch8      0x0c84
#define H_SPUPitch9      0x0c94
#define H_SPUPitch10     0x0ca4
#define H_SPUPitch11     0x0cb4
#define H_SPUPitch12     0x0cc4
#define H_SPUPitch13     0x0cd4
#define H_SPUPitch14     0x0ce4
#define H_SPUPitch15     0x0cf4
#define H_SPUPitch16     0x0d04
#define H_SPUPitch17     0x0d14
#define H_SPUPitch18     0x0d24
#define H_SPUPitch19     0x0d34
#define H_SPUPitch20     0x0d44
#define H_SPUPitch21     0x0d54
#define H_SPUPitch22     0x0d64
#define H_SPUPitch23     0x0d74

#define H_SPUStartAdr0   0x0c06
#define H_SPUStartAdr1   0x0c16
#define H_SPUStartAdr2   0x0c26
#define H_SPUStartAdr3   0x0c36
#define H_SPUStartAdr4   0x0c46
#define H_SPUStartAdr5   0x0c56
#define H_SPUStartAdr6   0x0c66
#define H_SPUStartAdr7   0x0c76
#define H_SPUStartAdr8   0x0c86
#define H_SPUStartAdr9   0x0c96
#define H_SPUStartAdr10  0x0ca6
#define H_SPUStartAdr11  0x0cb6
#define H_SPUStartAdr12  0x0cc6
#define H_SPUStartAdr13  0x0cd6
#define H_SPUStartAdr14  0x0ce6
#define H_SPUStartAdr15  0x0cf6
#define H_SPUStartAdr16  0x0d06
#define H_SPUStartAdr17  0x0d16
#define H_SPUStartAdr18  0x0d26
#define H_SPUStartAdr19  0x0d36
#define H_SPUStartAdr20  0x0d46
#define H_SPUStartAdr21  0x0d56
#define H_SPUStartAdr22  0x0d66
#define H_SPUStartAdr23  0x0d76

#define H_SPULoopAdr0   0x0c0e
#define H_SPULoopAdr1   0x0c1e
#define H_SPULoopAdr2   0x0c2e
#define H_SPULoopAdr3   0x0c3e
#define H_SPULoopAdr4   0x0c4e
#define H_SPULoopAdr5   0x0c5e
#define H_SPULoopAdr6   0x0c6e
#define H_SPULoopAdr7   0x0c7e
#define H_SPULoopAdr8   0x0c8e
#define H_SPULoopAdr9   0x0c9e
#define H_SPULoopAdr10  0x0cae
#define H_SPULoopAdr11  0x0cbe
#define H_SPULoopAdr12  0x0cce
#define H_SPULoopAdr13  0x0cde
#define H_SPULoopAdr14  0x0cee
#define H_SPULoopAdr15  0x0cfe
#define H_SPULoopAdr16  0x0d0e
#define H_SPULoopAdr17  0x0d1e
#define H_SPULoopAdr18  0x0d2e
#define H_SPULoopAdr19  0x0d3e
#define H_SPULoopAdr20  0x0d4e
#define H_SPULoopAdr21  0x0d5e
#define H_SPULoopAdr22  0x0d6e
#define H_SPULoopAdr23  0x0d7e

#define H_SPU_ADSRLevel0   0x0c08
#define H_SPU_ADSRLevel1   0x0c18
#define H_SPU_ADSRLevel2   0x0c28
#define H_SPU_ADSRLevel3   0x0c38
#define H_SPU_ADSRLevel4   0x0c48
#define H_SPU_ADSRLevel5   0x0c58
#define H_SPU_ADSRLevel6   0x0c68
#define H_SPU_ADSRLevel7   0x0c78
#define H_SPU_ADSRLevel8   0x0c88
#define H_SPU_ADSRLevel9   0x0c98
#define H_SPU_ADSRLevel10  0x0ca8
#define H_SPU_ADSRLevel11  0x0cb8
#define H_SPU_ADSRLevel12  0x0cc8
#define H_SPU_ADSRLevel13  0x0cd8
#define H_SPU_ADSRLevel14  0x0ce8
#define H_SPU_ADSRLevel15  0x0cf8
#define H_SPU_ADSRLevel16  0x0d08
#define H_SPU_ADSRLevel17  0x0d18
#define H_SPU_ADSRLevel18  0x0d28
#define H_SPU_ADSRLevel19  0x0d38
#define H_SPU_ADSRLevel20  0x0d48
#define H_SPU_ADSRLevel21  0x0d58
#define H_SPU_ADSRLevel22  0x0d68
#define H_SPU_ADSRLevel23  0x0d78


void SoundOn(int start,int end,unsigned short val);
void SoundOff(int start,int end,unsigned short val);
void FModOn(int start,int end,unsigned short val);
void NoiseOn(int start,int end,unsigned short val);
void SetVolumeL(unsigned char ch,short vol);
void SetVolumeR(unsigned char ch,short vol);
void SetPitch(int ch,unsigned short val);
void ReverbOn(int start,int end,unsigned short val);

void SetREVERB(unsigned short val);
INLINE void StartREVERB(int ch);
INLINE void StoreREVERB(int ch,int ns);

void SetupTimer(void);
void RemoveTimer(void);

INLINE void StartADSR(int ch);
INLINE int  MixADSR(int ch);

void ReadConfig(void);
void StartCfgTool(char * pCmdLine);


void SetupSound(void);
void RemoveSound(void);
unsigned long SoundGetBytesBuffered(void);
void SoundFeedStreamData(unsigned char* pSound,long lBytes);
unsigned long timeGetTime_spu();

// globals

// psx buffer / addresses

static unsigned short  regArea[10000];
static unsigned short  spuMem[256*1024];
static unsigned char * spuMemC;
static unsigned char * pSpuIrq=0;
static unsigned char * pSpuBuffer;
static unsigned char * pMixIrq=0;

// user settings

static int             iVolume=3;
static int             iXAPitch=1;
static int             iUseTimer=2;
static int             iSPUIRQWait=1;
static int             iDebugMode=0;
static int             iRecordMode=0;
static int             iUseReverb=2;
static int             iUseInterpolation=2;
static int             iDisStereo=0;

// MAIN infos struct for each channel

static SPUCHAN         s_chan[MAXCHAN+1];              // channel + 1 infos (1 is security for fmod handling)
static REVERBInfo      rvb;

static unsigned long   dwNoiseVal=1;                   // global noise generator
static int             iSpuAsyncWait=0;

static unsigned short  spuCtrl=0;                      // some vars to store psx reg infos
static unsigned short  spuStat=0;
static unsigned short  spuIrq=0;
static unsigned long   spuAddr=0xffffffff;             // address into spu mem
static int             bEndThread=0;                   // thread handlers
static int             bThreadEnded=0;
static int             bSpuInit=0;
static int             bSPUIsOpen=0;

static pthread_t thread = (pthread_t)-1;               // thread id (linux)

static unsigned long dwNewChannel=0;                   // flags for faster testing, if new channel starts

static void (*irqCallback)(void)=0;                  // func of main emu, called on spu irq
static void (*cddavCallback)(unsigned short,unsigned short)=0;

// certain globals (were local before, but with the new timeproc I need em global)
static const int f[5][2] = {   {    0,  0  },
                               {   60,  0  },
                               {  115, -52 },
                               {   98, -55 },
                               {  122, -60 } };
static int SSumR[NSSIZE];
static int SSumL[NSSIZE];
static int iFMod[NSSIZE];
static int iCycle = 0;
static short * pS;

static int lastch=-1;       // last channel processed on spu irq in timer mode
static int lastns=0;       // last ns pos
static int iSecureStart=0; // secure start counter


static unsigned long RateTable[160];


static xa_decode_t   * xapGlobal=0;

static uint32_t * XAFeed  = NULL;
static uint32_t * XAPlay  = NULL;
static uint32_t * XAStart = NULL;
static uint32_t * XAEnd   = NULL;

static uint32_t   XARepeat  = 0;
static uint32_t   XALastVal = 0;

static uint32_t * CDDAFeed  = NULL;
static uint32_t * CDDAPlay  = NULL;
static uint32_t * CDDAStart = NULL;
static uint32_t * CDDAEnd   = NULL;

static int             iLeftXAVol  = 32767;
static int             iRightXAVol = 32767;

static int gauss_ptr = 0;
static int gauss_window[8] = {0, 0, 0, 0, 0, 0, 0, 0};

// REVERB info and timing vars...
static int *          sRVBPlay      = 0;
static int *          sRVBEnd       = 0;
static int *          sRVBStart     = 0;
static int            iReverbOff    = -1;   // some delay factor for reverb
static int            iReverbRepeat = 0;
static int            iReverbNum    = 1;    

////////////////////////////////////////////////////////////////////////
// SET REVERB
////////////////////////////////////////////////////////////////////////

void SetREVERB(unsigned short val)
{
 switch(val)
  {
   case 0x0000: iReverbOff=-1;  break;                                         // off
   case 0x007D: iReverbOff=32;  iReverbNum=2; iReverbRepeat=128;  break;       // ok room

   case 0x0033: iReverbOff=32;  iReverbNum=2; iReverbRepeat=64;   break;       // studio small
   case 0x00B1: iReverbOff=48;  iReverbNum=2; iReverbRepeat=96;   break;       // ok studio medium
   case 0x00E3: iReverbOff=64;  iReverbNum=2; iReverbRepeat=128;  break;       // ok studio large ok

   case 0x01A5: iReverbOff=128; iReverbNum=4; iReverbRepeat=32;   break;       // ok hall
   case 0x033D: iReverbOff=256; iReverbNum=4; iReverbRepeat=64;   break;       // space echo
   case 0x0001: iReverbOff=184; iReverbNum=3; iReverbRepeat=128;  break;       // echo/delay
   case 0x0017: iReverbOff=128; iReverbNum=2; iReverbRepeat=128;  break;       // half echo
   default:     iReverbOff=32;  iReverbNum=1; iReverbRepeat=0;    break;
  }
}

////////////////////////////////////////////////////////////////////////
// START REVERB
////////////////////////////////////////////////////////////////////////

INLINE void StartREVERB(int ch)
{
 if(s_chan[ch].bReverb && (spuCtrl&0x80))              // reverb possible?
  {
   if(iUseReverb==2) s_chan[ch].bRVBActive=1;
   else
   if(iUseReverb==1 && iReverbOff>0)                   // -> fake reverb used?
    {
     s_chan[ch].bRVBActive=1;                            // -> activate it
     s_chan[ch].iRVBOffset=iReverbOff*45;
     s_chan[ch].iRVBRepeat=iReverbRepeat*45;
     s_chan[ch].iRVBNum   =iReverbNum;
    }
  }
 else s_chan[ch].bRVBActive=0;                         // else -> no reverb
}

////////////////////////////////////////////////////////////////////////
// HELPER FOR NEILL'S REVERB: re-inits our reverb mixing buf
////////////////////////////////////////////////////////////////////////

INLINE void InitREVERB(void)
{
 if(iUseReverb==2)
  {memset(sRVBStart,0,NSSIZE*2*4);}
}

////////////////////////////////////////////////////////////////////////
// STORE REVERB
////////////////////////////////////////////////////////////////////////

INLINE void StoreREVERB(int ch,int ns)
{
 if(iUseReverb==0) return;
 else
 if(iUseReverb==2) // -------------------------------- // Neil's reverb
  {
   const int iRxl=(s_chan[ch].sval*s_chan[ch].iLeftVolume)/0x4000;
   const int iRxr=(s_chan[ch].sval*s_chan[ch].iRightVolume)/0x4000;

   ns<<=1;

   *(sRVBStart+ns)  +=iRxl;                            // -> we mix all active reverb channels into an extra buffer
   *(sRVBStart+ns+1)+=iRxr;
  }
 else // --------------------------------------------- // Pete's easy fake reverb
  {
   int * pN;int iRn,iRr=0;

   // we use the half channel volume (/0x8000) for the first reverb effects, quarter for next and so on

   int iRxl=(s_chan[ch].sval*s_chan[ch].iLeftVolume)/0x8000;
   int iRxr=(s_chan[ch].sval*s_chan[ch].iRightVolume)/0x8000;
 
   for(iRn=1;iRn<=s_chan[ch].iRVBNum;iRn++,iRr+=s_chan[ch].iRVBRepeat,iRxl/=2,iRxr/=2)
    {
     pN=sRVBPlay+((s_chan[ch].iRVBOffset+iRr+ns)<<1);
     if(pN>=sRVBEnd) pN=sRVBStart+(pN-sRVBEnd);

     (*pN)+=iRxl;
     pN++;
     (*pN)+=iRxr;
    }
  }
}

////////////////////////////////////////////////////////////////////////

INLINE int g_buffer(int iOff)                          // get_buffer content helper: takes care about wraps
{
 short * p=(short *)spuMem;
 iOff=(iOff*4)+rvb.CurrAddr;
 while(iOff>0x3FFFF)       iOff=rvb.StartAddr+(iOff-0x40000);
 while(iOff<rvb.StartAddr) iOff=0x3ffff-(rvb.StartAddr-iOff);
 return (int)*(p+iOff);
}

////////////////////////////////////////////////////////////////////////

INLINE void s_buffer(int iOff,int iVal)                // set_buffer content helper: takes care about wraps and clipping
{
 short * p=(short *)spuMem;
 iOff=(iOff*4)+rvb.CurrAddr;
 while(iOff>0x3FFFF) iOff=rvb.StartAddr+(iOff-0x40000);
 while(iOff<rvb.StartAddr) iOff=0x3ffff-(rvb.StartAddr-iOff);
 if(iVal<-32768L) iVal=-32768L;if(iVal>32767L) iVal=32767L;
 *(p+iOff)=(short)iVal;
}

////////////////////////////////////////////////////////////////////////

INLINE void s_buffer1(int iOff,int iVal)                // set_buffer (+1 sample) content helper: takes care about wraps and clipping
{
 short * p=(short *)spuMem;
 iOff=(iOff*4)+rvb.CurrAddr+1;
 while(iOff>0x3FFFF) iOff=rvb.StartAddr+(iOff-0x40000);
 while(iOff<rvb.StartAddr) iOff=0x3ffff-(rvb.StartAddr-iOff);
 if(iVal<-32768L) iVal=-32768L;if(iVal>32767L) iVal=32767L;
 *(p+iOff)=(short)iVal;
}

////////////////////////////////////////////////////////////////////////

INLINE int MixREVERBLeft(int ns)
{
 if(iUseReverb==0) return 0;
 else
 if(iUseReverb==2)
  {
   static int iCnt=0;                                  // this func will be called with 44.1 khz

   if(!rvb.StartAddr)                                  // reverb is off
    {
     rvb.iLastRVBLeft=rvb.iLastRVBRight=rvb.iRVBLeft=rvb.iRVBRight=0;
     return 0;
    }

   iCnt++;                                    

   if(iCnt&1)                                          // we work on every second left value: downsample to 22 khz
    {
     if(spuCtrl&0x80)                                  // -> reverb on? oki
      {
       int ACC0,ACC1,FB_A0,FB_A1,FB_B0,FB_B1;

       const int INPUT_SAMPLE_L=*(sRVBStart+(ns<<1));                         
       const int INPUT_SAMPLE_R=*(sRVBStart+(ns<<1)+1);                     

       const int IIR_INPUT_A0 = (g_buffer(rvb.IIR_SRC_A0) * rvb.IIR_COEF)/32768L + (INPUT_SAMPLE_L * rvb.IN_COEF_L)/32768L;
       const int IIR_INPUT_A1 = (g_buffer(rvb.IIR_SRC_A1) * rvb.IIR_COEF)/32768L + (INPUT_SAMPLE_R * rvb.IN_COEF_R)/32768L;
       const int IIR_INPUT_B0 = (g_buffer(rvb.IIR_SRC_B0) * rvb.IIR_COEF)/32768L + (INPUT_SAMPLE_L * rvb.IN_COEF_L)/32768L;
       const int IIR_INPUT_B1 = (g_buffer(rvb.IIR_SRC_B1) * rvb.IIR_COEF)/32768L + (INPUT_SAMPLE_R * rvb.IN_COEF_R)/32768L;

       const int IIR_A0 = (IIR_INPUT_A0 * rvb.IIR_ALPHA)/32768L + (g_buffer(rvb.IIR_DEST_A0) * (32768L - rvb.IIR_ALPHA))/32768L;
       const int IIR_A1 = (IIR_INPUT_A1 * rvb.IIR_ALPHA)/32768L + (g_buffer(rvb.IIR_DEST_A1) * (32768L - rvb.IIR_ALPHA))/32768L;
       const int IIR_B0 = (IIR_INPUT_B0 * rvb.IIR_ALPHA)/32768L + (g_buffer(rvb.IIR_DEST_B0) * (32768L - rvb.IIR_ALPHA))/32768L;
       const int IIR_B1 = (IIR_INPUT_B1 * rvb.IIR_ALPHA)/32768L + (g_buffer(rvb.IIR_DEST_B1) * (32768L - rvb.IIR_ALPHA))/32768L;

       s_buffer1(rvb.IIR_DEST_A0, IIR_A0);
       s_buffer1(rvb.IIR_DEST_A1, IIR_A1);
       s_buffer1(rvb.IIR_DEST_B0, IIR_B0);
       s_buffer1(rvb.IIR_DEST_B1, IIR_B1);
 
       ACC0 = (g_buffer(rvb.ACC_SRC_A0) * rvb.ACC_COEF_A)/32768L +
              (g_buffer(rvb.ACC_SRC_B0) * rvb.ACC_COEF_B)/32768L +
              (g_buffer(rvb.ACC_SRC_C0) * rvb.ACC_COEF_C)/32768L +
              (g_buffer(rvb.ACC_SRC_D0) * rvb.ACC_COEF_D)/32768L;
       ACC1 = (g_buffer(rvb.ACC_SRC_A1) * rvb.ACC_COEF_A)/32768L +
              (g_buffer(rvb.ACC_SRC_B1) * rvb.ACC_COEF_B)/32768L +
              (g_buffer(rvb.ACC_SRC_C1) * rvb.ACC_COEF_C)/32768L +
              (g_buffer(rvb.ACC_SRC_D1) * rvb.ACC_COEF_D)/32768L;

       FB_A0 = g_buffer(rvb.MIX_DEST_A0 - rvb.FB_SRC_A);
       FB_A1 = g_buffer(rvb.MIX_DEST_A1 - rvb.FB_SRC_A);
       FB_B0 = g_buffer(rvb.MIX_DEST_B0 - rvb.FB_SRC_B);
       FB_B1 = g_buffer(rvb.MIX_DEST_B1 - rvb.FB_SRC_B);

       s_buffer(rvb.MIX_DEST_A0, ACC0 - (FB_A0 * rvb.FB_ALPHA)/32768L);
       s_buffer(rvb.MIX_DEST_A1, ACC1 - (FB_A1 * rvb.FB_ALPHA)/32768L);
       
       s_buffer(rvb.MIX_DEST_B0, (rvb.FB_ALPHA * ACC0)/32768L - (FB_A0 * (int)(rvb.FB_ALPHA^0xFFFF8000))/32768L - (FB_B0 * rvb.FB_X)/32768L);
       s_buffer(rvb.MIX_DEST_B1, (rvb.FB_ALPHA * ACC1)/32768L - (FB_A1 * (int)(rvb.FB_ALPHA^0xFFFF8000))/32768L - (FB_B1 * rvb.FB_X)/32768L);
 
       rvb.iLastRVBLeft  = rvb.iRVBLeft;
       rvb.iLastRVBRight = rvb.iRVBRight;

       rvb.iRVBLeft  = (g_buffer(rvb.MIX_DEST_A0)+g_buffer(rvb.MIX_DEST_B0))/3;
       rvb.iRVBRight = (g_buffer(rvb.MIX_DEST_A1)+g_buffer(rvb.MIX_DEST_B1))/3;

       rvb.iRVBLeft  = (rvb.iRVBLeft  * rvb.VolLeft)  / 0x4000;
       rvb.iRVBRight = (rvb.iRVBRight * rvb.VolRight) / 0x4000;

       rvb.CurrAddr++;
       if(rvb.CurrAddr>0x3ffff) rvb.CurrAddr=rvb.StartAddr;

       return rvb.iLastRVBLeft+(rvb.iRVBLeft-rvb.iLastRVBLeft)/2;
      }
     else                                              // -> reverb off
      {
       rvb.iLastRVBLeft=rvb.iLastRVBRight=rvb.iRVBLeft=rvb.iRVBRight=0;
      }

     rvb.CurrAddr++;
     if(rvb.CurrAddr>0x3ffff) rvb.CurrAddr=rvb.StartAddr;
    }

   return rvb.iLastRVBLeft;
  }
 else                                                  // easy fake reverb:
  {
   const int iRV=*sRVBPlay;                            // -> simply take the reverb mix buf value
   *sRVBPlay++=0;                                      // -> init it after
   if(sRVBPlay>=sRVBEnd) sRVBPlay=sRVBStart;           // -> and take care about wrap arounds
   return iRV;                                         // -> return reverb mix buf val
  }
}

////////////////////////////////////////////////////////////////////////

INLINE int MixREVERBRight(void)
{
 if(iUseReverb==0) return 0;
 else
 if(iUseReverb==2)                                     // Neill's reverb:
  {
   int i=rvb.iLastRVBRight+(rvb.iRVBRight-rvb.iLastRVBRight)/2;
   rvb.iLastRVBRight=rvb.iRVBRight;
   return i;                                           // -> just return the last right reverb val (little bit scaled by the previous right val)
  }
 else                                                  // easy fake reverb:
  {
   const int iRV=*sRVBPlay;                            // -> simply take the reverb mix buf value
   *sRVBPlay++=0;                                      // -> init it after
   if(sRVBPlay>=sRVBEnd) sRVBPlay=sRVBStart;           // -> and take care about wrap arounds
   return iRV;                                         // -> return reverb mix buf val
  }
}

/*
-----------------------------------------------------------------------------
PSX reverb hardware notes
by Neill Corlett
-----------------------------------------------------------------------------

Yadda yadda disclaimer yadda probably not perfect yadda well it's okay anyway
yadda yadda.

-----------------------------------------------------------------------------

Basics
------

- The reverb buffer is 22khz 16-bit mono PCM.
- It starts at the reverb address given by 1DA2, extends to
  the end of sound RAM, and wraps back to the 1DA2 address.

Setting the address at 1DA2 resets the current reverb work address.

This work address ALWAYS increments every 1/22050 sec., regardless of
whether reverb is enabled (bit 7 of 1DAA set).

And the contents of the reverb buffer ALWAYS play, scaled by the
"reverberation depth left/right" volumes (1D84/1D86).
(which, by the way, appear to be scaled so 3FFF=approx. 1.0, 4000=-1.0)

-----------------------------------------------------------------------------

Register names
--------------

These are probably not their real names.
These are probably not even correct names.
We will use them anyway, because we can.

1DC0: FB_SRC_A       (offset)
1DC2: FB_SRC_B       (offset)
1DC4: IIR_ALPHA      (coef.)
1DC6: ACC_COEF_A     (coef.)
1DC8: ACC_COEF_B     (coef.)
1DCA: ACC_COEF_C     (coef.)
1DCC: ACC_COEF_D     (coef.)
1DCE: IIR_COEF       (coef.)
1DD0: FB_ALPHA       (coef.)
1DD2: FB_X           (coef.)
1DD4: IIR_DEST_A0    (offset)
1DD6: IIR_DEST_A1    (offset)
1DD8: ACC_SRC_A0     (offset)
1DDA: ACC_SRC_A1     (offset)
1DDC: ACC_SRC_B0     (offset)
1DDE: ACC_SRC_B1     (offset)
1DE0: IIR_SRC_A0     (offset)
1DE2: IIR_SRC_A1     (offset)
1DE4: IIR_DEST_B0    (offset)
1DE6: IIR_DEST_B1    (offset)
1DE8: ACC_SRC_C0     (offset)
1DEA: ACC_SRC_C1     (offset)
1DEC: ACC_SRC_D0     (offset)
1DEE: ACC_SRC_D1     (offset)
1DF0: IIR_SRC_B1     (offset)
1DF2: IIR_SRC_B0     (offset)
1DF4: MIX_DEST_A0    (offset)
1DF6: MIX_DEST_A1    (offset)
1DF8: MIX_DEST_B0    (offset)
1DFA: MIX_DEST_B1    (offset)
1DFC: IN_COEF_L      (coef.)
1DFE: IN_COEF_R      (coef.)

The coefficients are signed fractional values.
-32768 would be -1.0
 32768 would be  1.0 (if it were possible... the highest is of course 32767)

The offsets are (byte/8) offsets into the reverb buffer.
i.e. you multiply them by 8, you get byte offsets.
You can also think of them as (samples/4) offsets.
They appear to be signed.  They can be negative.
None of the documented presets make them negative, though.

Yes, 1DF0 and 1DF2 appear to be backwards.  Not a typo.

-----------------------------------------------------------------------------

What it does
------------

We take all reverb sources:
- regular channels that have the reverb bit on
- cd and external sources, if their reverb bits are on
and mix them into one stereo 44100hz signal.

Lowpass/downsample that to 22050hz.  The PSX uses a proper bandlimiting
algorithm here, but I haven't figured out the hysterically exact specifics.
I use an 8-tap filter with these coefficients, which are nice but probably
not the real ones:

0.037828187894
0.157538631280
0.321159685278
0.449322115345
0.449322115345
0.321159685278
0.157538631280
0.037828187894

So we have two input samples (INPUT_SAMPLE_L, INPUT_SAMPLE_R) every 22050hz.

* IN MY EMULATION, I divide these by 2 to make it clip less.
  (and of course the L/R output coefficients are adjusted to compensate)
  The real thing appears to not do this.

At every 22050hz tick:
- If the reverb bit is enabled (bit 7 of 1DAA), execute the reverb
  steady-state algorithm described below
- AFTERWARDS, retrieve the "wet out" L and R samples from the reverb buffer
  (This part may not be exactly right and I guessed at the coefs. TODO: check later.)
  L is: 0.333 * (buffer[MIX_DEST_A0] + buffer[MIX_DEST_B0])
  R is: 0.333 * (buffer[MIX_DEST_A1] + buffer[MIX_DEST_B1])
- Advance the current buffer position by 1 sample

The wet out L and R are then upsampled to 44100hz and played at the
"reverberation depth left/right" (1D84/1D86) volume, independent of the main
volume.

-----------------------------------------------------------------------------

Reverb steady-state
-------------------

The reverb steady-state algorithm is fairly clever, and of course by
"clever" I mean "batshit insane".

buffer[x] is relative to the current buffer position, not the beginning of
the buffer.  Note that all buffer offsets must wrap around so they're
contained within the reverb work area.

Clipping is performed at the end... maybe also sooner, but definitely at
the end.

IIR_INPUT_A0 = buffer[IIR_SRC_A0] * IIR_COEF + INPUT_SAMPLE_L * IN_COEF_L;
IIR_INPUT_A1 = buffer[IIR_SRC_A1] * IIR_COEF + INPUT_SAMPLE_R * IN_COEF_R;
IIR_INPUT_B0 = buffer[IIR_SRC_B0] * IIR_COEF + INPUT_SAMPLE_L * IN_COEF_L;
IIR_INPUT_B1 = buffer[IIR_SRC_B1] * IIR_COEF + INPUT_SAMPLE_R * IN_COEF_R;

IIR_A0 = IIR_INPUT_A0 * IIR_ALPHA + buffer[IIR_DEST_A0] * (1.0 - IIR_ALPHA);
IIR_A1 = IIR_INPUT_A1 * IIR_ALPHA + buffer[IIR_DEST_A1] * (1.0 - IIR_ALPHA);
IIR_B0 = IIR_INPUT_B0 * IIR_ALPHA + buffer[IIR_DEST_B0] * (1.0 - IIR_ALPHA);
IIR_B1 = IIR_INPUT_B1 * IIR_ALPHA + buffer[IIR_DEST_B1] * (1.0 - IIR_ALPHA);

buffer[IIR_DEST_A0 + 1sample] = IIR_A0;
buffer[IIR_DEST_A1 + 1sample] = IIR_A1;
buffer[IIR_DEST_B0 + 1sample] = IIR_B0;
buffer[IIR_DEST_B1 + 1sample] = IIR_B1;

ACC0 = buffer[ACC_SRC_A0] * ACC_COEF_A +
       buffer[ACC_SRC_B0] * ACC_COEF_B +
       buffer[ACC_SRC_C0] * ACC_COEF_C +
       buffer[ACC_SRC_D0] * ACC_COEF_D;
ACC1 = buffer[ACC_SRC_A1] * ACC_COEF_A +
       buffer[ACC_SRC_B1] * ACC_COEF_B +
       buffer[ACC_SRC_C1] * ACC_COEF_C +
       buffer[ACC_SRC_D1] * ACC_COEF_D;

FB_A0 = buffer[MIX_DEST_A0 - FB_SRC_A];
FB_A1 = buffer[MIX_DEST_A1 - FB_SRC_A];
FB_B0 = buffer[MIX_DEST_B0 - FB_SRC_B];
FB_B1 = buffer[MIX_DEST_B1 - FB_SRC_B];

buffer[MIX_DEST_A0] = ACC0 - FB_A0 * FB_ALPHA;
buffer[MIX_DEST_A1] = ACC1 - FB_A1 * FB_ALPHA;
buffer[MIX_DEST_B0] = (FB_ALPHA * ACC0) - FB_A0 * (FB_ALPHA^0x8000) - FB_B0 * FB_X;
buffer[MIX_DEST_B1] = (FB_ALPHA * ACC1) - FB_A1 * (FB_ALPHA^0x8000) - FB_B1 * FB_X;

-----------------------------------------------------------------------------
*/

////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////
// ADSR func
////////////////////////////////////////////////////////////////////////

void InitADSR(void)                                    // INIT ADSR
{
 unsigned long r,rs,rd;int i;

 memset(RateTable,0,sizeof(unsigned long)*160);        // build the rate table according to Neill's rules (see at bottom of file)

 r=3;rs=1;rd=0;

 for(i=32;i<160;i++)                                   // we start at pos 32 with the real values... everything before is 0
  {
   if(r<0x3FFFFFFF)
    {
     r+=rs;
     rd++;if(rd==5) {rd=1;rs*=2;}
    }
   if(r>0x3FFFFFFF) r=0x3FFFFFFF;

   RateTable[i]=r;
  }
}

////////////////////////////////////////////////////////////////////////

INLINE void StartADSR(int ch)                          // MIX ADSR
{
 s_chan[ch].ADSRX.lVolume=1;                           // and init some adsr vars
 s_chan[ch].ADSRX.State=0;
 s_chan[ch].ADSRX.EnvelopeVol=0;
}

////////////////////////////////////////////////////////////////////////

INLINE int MixADSR(int ch)                             // MIX ADSR
{    
 if(s_chan[ch].bStop)                                  // should be stopped:
  {                                                    // do release
   if(s_chan[ch].ADSRX.ReleaseModeExp)
    {
     switch((s_chan[ch].ADSRX.EnvelopeVol>>28)&0x7)
      {
       case 0: s_chan[ch].ADSRX.EnvelopeVol-=RateTable[(4*(s_chan[ch].ADSRX.ReleaseRate^0x1F))-0x18 +0 + 32]; break;
       case 1: s_chan[ch].ADSRX.EnvelopeVol-=RateTable[(4*(s_chan[ch].ADSRX.ReleaseRate^0x1F))-0x18 +4 + 32]; break;
       case 2: s_chan[ch].ADSRX.EnvelopeVol-=RateTable[(4*(s_chan[ch].ADSRX.ReleaseRate^0x1F))-0x18 +6 + 32]; break;
       case 3: s_chan[ch].ADSRX.EnvelopeVol-=RateTable[(4*(s_chan[ch].ADSRX.ReleaseRate^0x1F))-0x18 +8 + 32]; break;
       case 4: s_chan[ch].ADSRX.EnvelopeVol-=RateTable[(4*(s_chan[ch].ADSRX.ReleaseRate^0x1F))-0x18 +9 + 32]; break;
       case 5: s_chan[ch].ADSRX.EnvelopeVol-=RateTable[(4*(s_chan[ch].ADSRX.ReleaseRate^0x1F))-0x18 +10+ 32]; break;
       case 6: s_chan[ch].ADSRX.EnvelopeVol-=RateTable[(4*(s_chan[ch].ADSRX.ReleaseRate^0x1F))-0x18 +11+ 32]; break;
       case 7: s_chan[ch].ADSRX.EnvelopeVol-=RateTable[(4*(s_chan[ch].ADSRX.ReleaseRate^0x1F))-0x18 +12+ 32]; break;
      }
    }
   else
    {
     s_chan[ch].ADSRX.EnvelopeVol-=RateTable[(4*(s_chan[ch].ADSRX.ReleaseRate^0x1F))-0x0C + 32];
    }

   if(s_chan[ch].ADSRX.EnvelopeVol<0) 
    {
     s_chan[ch].ADSRX.EnvelopeVol=0;
     s_chan[ch].bOn=0;
     //s_chan[ch].bReverb=0;
     //s_chan[ch].bNoise=0;
    }

   s_chan[ch].ADSRX.lVolume=s_chan[ch].ADSRX.EnvelopeVol>>21;
   return s_chan[ch].ADSRX.lVolume;
  }
 else                                                  // not stopped yet?
  {
   if(s_chan[ch].ADSRX.State==0)                       // -> attack
    {
     if(s_chan[ch].ADSRX.AttackModeExp)
      {
       if(s_chan[ch].ADSRX.EnvelopeVol<0x60000000) 
        s_chan[ch].ADSRX.EnvelopeVol+=RateTable[(s_chan[ch].ADSRX.AttackRate^0x7F)-0x10 + 32];
       else
        s_chan[ch].ADSRX.EnvelopeVol+=RateTable[(s_chan[ch].ADSRX.AttackRate^0x7F)-0x18 + 32];
      }
     else
      {
       s_chan[ch].ADSRX.EnvelopeVol+=RateTable[(s_chan[ch].ADSRX.AttackRate^0x7F)-0x10 + 32];
      }

     if(s_chan[ch].ADSRX.EnvelopeVol<0) 
      {
       s_chan[ch].ADSRX.EnvelopeVol=0x7FFFFFFF;
       s_chan[ch].ADSRX.State=1;
      }

     s_chan[ch].ADSRX.lVolume=s_chan[ch].ADSRX.EnvelopeVol>>21;
     return s_chan[ch].ADSRX.lVolume;
    }
   //--------------------------------------------------//
   if(s_chan[ch].ADSRX.State==1)                       // -> decay
    {
     switch((s_chan[ch].ADSRX.EnvelopeVol>>28)&0x7)
      {
       case 0: s_chan[ch].ADSRX.EnvelopeVol-=RateTable[(4*(s_chan[ch].ADSRX.DecayRate^0x1F))-0x18+0 + 32]; break;
       case 1: s_chan[ch].ADSRX.EnvelopeVol-=RateTable[(4*(s_chan[ch].ADSRX.DecayRate^0x1F))-0x18+4 + 32]; break;
       case 2: s_chan[ch].ADSRX.EnvelopeVol-=RateTable[(4*(s_chan[ch].ADSRX.DecayRate^0x1F))-0x18+6 + 32]; break;
       case 3: s_chan[ch].ADSRX.EnvelopeVol-=RateTable[(4*(s_chan[ch].ADSRX.DecayRate^0x1F))-0x18+8 + 32]; break;
       case 4: s_chan[ch].ADSRX.EnvelopeVol-=RateTable[(4*(s_chan[ch].ADSRX.DecayRate^0x1F))-0x18+9 + 32]; break;
       case 5: s_chan[ch].ADSRX.EnvelopeVol-=RateTable[(4*(s_chan[ch].ADSRX.DecayRate^0x1F))-0x18+10+ 32]; break;
       case 6: s_chan[ch].ADSRX.EnvelopeVol-=RateTable[(4*(s_chan[ch].ADSRX.DecayRate^0x1F))-0x18+11+ 32]; break;
       case 7: s_chan[ch].ADSRX.EnvelopeVol-=RateTable[(4*(s_chan[ch].ADSRX.DecayRate^0x1F))-0x18+12+ 32]; break;
      }

     if(s_chan[ch].ADSRX.EnvelopeVol<0) s_chan[ch].ADSRX.EnvelopeVol=0;
     if(((s_chan[ch].ADSRX.EnvelopeVol>>27)&0xF) <= s_chan[ch].ADSRX.SustainLevel)
      {
       s_chan[ch].ADSRX.State=2;
      }

     s_chan[ch].ADSRX.lVolume=s_chan[ch].ADSRX.EnvelopeVol>>21;
     return s_chan[ch].ADSRX.lVolume;
    }
   //--------------------------------------------------//
   if(s_chan[ch].ADSRX.State==2)                       // -> sustain
    {
     if(s_chan[ch].ADSRX.SustainIncrease)
      {
       if(s_chan[ch].ADSRX.SustainModeExp)
        {
         if(s_chan[ch].ADSRX.EnvelopeVol<0x60000000) 
          s_chan[ch].ADSRX.EnvelopeVol+=RateTable[(s_chan[ch].ADSRX.SustainRate^0x7F)-0x10 + 32];
         else
          s_chan[ch].ADSRX.EnvelopeVol+=RateTable[(s_chan[ch].ADSRX.SustainRate^0x7F)-0x18 + 32];
        }
       else
        {
         s_chan[ch].ADSRX.EnvelopeVol+=RateTable[(s_chan[ch].ADSRX.SustainRate^0x7F)-0x10 + 32];
        }

       if(s_chan[ch].ADSRX.EnvelopeVol<0) 
        {
         s_chan[ch].ADSRX.EnvelopeVol=0x7FFFFFFF;
        }
      }
     else
      {
       if(s_chan[ch].ADSRX.SustainModeExp)
        {
         switch((s_chan[ch].ADSRX.EnvelopeVol>>28)&0x7)
          {
           case 0: s_chan[ch].ADSRX.EnvelopeVol-=RateTable[((s_chan[ch].ADSRX.SustainRate^0x7F))-0x1B +0 + 32];break;
           case 1: s_chan[ch].ADSRX.EnvelopeVol-=RateTable[((s_chan[ch].ADSRX.SustainRate^0x7F))-0x1B +4 + 32];break;
           case 2: s_chan[ch].ADSRX.EnvelopeVol-=RateTable[((s_chan[ch].ADSRX.SustainRate^0x7F))-0x1B +6 + 32];break;
           case 3: s_chan[ch].ADSRX.EnvelopeVol-=RateTable[((s_chan[ch].ADSRX.SustainRate^0x7F))-0x1B +8 + 32];break;
           case 4: s_chan[ch].ADSRX.EnvelopeVol-=RateTable[((s_chan[ch].ADSRX.SustainRate^0x7F))-0x1B +9 + 32];break;
           case 5: s_chan[ch].ADSRX.EnvelopeVol-=RateTable[((s_chan[ch].ADSRX.SustainRate^0x7F))-0x1B +10+ 32];break;
           case 6: s_chan[ch].ADSRX.EnvelopeVol-=RateTable[((s_chan[ch].ADSRX.SustainRate^0x7F))-0x1B +11+ 32];break;
           case 7: s_chan[ch].ADSRX.EnvelopeVol-=RateTable[((s_chan[ch].ADSRX.SustainRate^0x7F))-0x1B +12+ 32];break;
          }
        }
       else
        {
         s_chan[ch].ADSRX.EnvelopeVol-=RateTable[((s_chan[ch].ADSRX.SustainRate^0x7F))-0x0F + 32];
        }

       if(s_chan[ch].ADSRX.EnvelopeVol<0) 
        {
         s_chan[ch].ADSRX.EnvelopeVol=0;
        }
      }
     s_chan[ch].ADSRX.lVolume=s_chan[ch].ADSRX.EnvelopeVol>>21;
     return s_chan[ch].ADSRX.lVolume;
    }
  }
 return 0;
}


/*
James Higgs ADSR investigations:

PSX SPU Envelope Timings
~~~~~~~~~~~~~~~~~~~~~~~~

First, here is an extract from doomed's SPU doc, which explains the basics
of the SPU "volume envelope": 

*** doomed doc extract start ***

--------------------------------------------------------------------------
Voices.
--------------------------------------------------------------------------
The SPU has 24 hardware voices. These voices can be used to reproduce sample
data, noise or can be used as frequency modulator on the next voice.
Each voice has it's own programmable ADSR envelope filter. The main volume
can be programmed independently for left and right output.

The ADSR envelope filter works as follows:
Ar = Attack rate, which specifies the speed at which the volume increases
     from zero to it's maximum value, as soon as the note on is given. The
     slope can be set to lineair or exponential.
Dr = Decay rate specifies the speed at which the volume decreases to the
     sustain level. Decay is always decreasing exponentially.
Sl = Sustain level, base level from which sustain starts.
Sr = Sustain rate is the rate at which the volume of the sustained note
     increases or decreases. This can be either lineair or exponential.
Rr = Release rate is the rate at which the volume of the note decreases
     as soon as the note off is given.

     lvl |
       ^ |     /\Dr     __
     Sl _| _  / _ \__---  \
         |   /       ---__ \ Rr
         |  /Ar       Sr  \ \
         | /                \\
         |/___________________\________
                                  ->time

The overal volume can also be set to sweep up or down lineairly or
exponentially from it's current value. This can be done seperately
for left and right.

Relevant SPU registers:
-------------------------------------------------------------
$1f801xx8         Attack/Decay/Sustain level
bit  |0f|0e 0d 0c 0b 0a 09 08|07 06 05 04|03 02 01 00|
desc.|Am|         Ar         |Dr         |Sl         |

Am       0        Attack mode Linear
         1                    Exponential

Ar       0-7f     attack rate
Dr       0-f      decay rate
Sl       0-f      sustain level
-------------------------------------------------------------
$1f801xxa         Sustain rate, Release Rate.
bit  |0f|0e|0d|0c 0b 0a 09 08 07 06|05|04 03 02 01 00|
desc.|Sm|Sd| 0|   Sr               |Rm|Rr            |

Sm       0        sustain rate mode linear
         1                          exponential
Sd       0        sustain rate mode increase
         1                          decrease
Sr       0-7f     Sustain Rate
Rm       0        Linear decrease
         1        Exponential decrease
Rr       0-1f     Release Rate

Note: decay mode is always Expontial decrease, and thus cannot
be set.
-------------------------------------------------------------
$1f801xxc         Current ADSR volume
bit  |0f 0e 0d 0c 0b 0a 09 08 07 06 05 04 03 02 01 00|
desc.|ADSRvol                                        |

ADSRvol           Returns the current envelope volume when
                  read.
-- James' Note: return range: 0 -> 32767

*** doomed doc extract end *** 

By using a small PSX proggie to visualise the envelope as it was played,
the following results for envelope timing were obtained:

1. Attack rate value (linear mode)

   Attack value range: 0 -> 127

   Value  | 48 | 52 | 56 | 60 | 64 | 68 | 72 |    | 80 |
   -----------------------------------------------------------------
   Frames | 11 | 21 | 42 | 84 | 169| 338| 676|    |2890|

   Note: frames is no. of PAL frames to reach full volume (100%
   amplitude)

   Hmm, noticing that the time taken to reach full volume doubles
   every time we add 4 to our attack value, we know the equation is
   of form:
             frames = k * 2 ^ (value / 4)

   (You may ponder about envelope generator hardware at this point,
   or maybe not... :)

   By substituting some stuff and running some checks, we get:

       k = 0.00257              (close enuf)

   therefore,
             frames = 0.00257 * 2 ^ (value / 4)
   If you just happen to be writing an emulator, then you can probably
   use an equation like:

       %volume_increase_per_tick = 1 / frames


   ------------------------------------
   Pete:
   ms=((1<<(value>>2))*514)/10000
   ------------------------------------

2. Decay rate value (only has log mode)

   Decay value range: 0 -> 15

   Value  |  8 |  9 | 10 | 11 | 12 | 13 | 14 | 15 |
   ------------------------------------------------
   frames |    |    |    |    |  6 | 12 | 24 | 47 |

   Note: frames here is no. of PAL frames to decay to 50% volume.

   formula: frames = k * 2 ^ (value)

   Substituting, we get: k = 0.00146

   Further info on logarithmic nature:
   frames to decay to sustain level 3  =  3 * frames to decay to 
   sustain level 9

   Also no. of frames to 25% volume = roughly 1.85 * no. of frames to
   50% volume.

   Frag it - just use linear approx.

   ------------------------------------
   Pete:
   ms=((1<<value)*292)/10000
   ------------------------------------


3. Sustain rate value (linear mode)

   Sustain rate range: 0 -> 127

   Value  | 48 | 52 | 56 | 60 | 64 | 68 | 72 |
   -------------------------------------------
   frames |  9 | 19 | 37 | 74 | 147| 293| 587|

   Here, frames = no. of PAL frames for volume amplitude to go from 100%
   to 0% (or vice-versa).

   Same formula as for attack value, just a different value for k:

   k = 0.00225

   ie: frames = 0.00225 * 2 ^ (value / 4)

   For emulation purposes:

   %volume_increase_or_decrease_per_tick = 1 / frames

   ------------------------------------
   Pete:
   ms=((1<<(value>>2))*450)/10000
   ------------------------------------


4. Release rate (linear mode)

   Release rate range: 0 -> 31

   Value  | 13 | 14 | 15 | 16 | 17 |
   ---------------------------------------------------------------
   frames | 18 | 36 | 73 | 146| 292|

   Here, frames = no. of PAL frames to decay from 100% vol to 0% vol
   after "note-off" is triggered.

   Formula: frames = k * 2 ^ (value)

   And so: k = 0.00223

   ------------------------------------
   Pete:
   ms=((1<<value)*446)/10000
   ------------------------------------


Other notes:   

Log stuff not figured out. You may get some clues from the "Decay rate"
stuff above. For emu purposes it may not be important - use linear
approx.

To get timings in millisecs, multiply frames by 20.



- James Higgs 17/6/2000
james7780@yahoo.com

//---------------------------------------------------------------

OLD adsr mixing according to james' rules... has to be called
every one millisecond


 long v,v2,lT,l1,l2,l3;

 if(s_chan[ch].bStop)                                  // psx wants to stop? -> release phase
  {
   if(s_chan[ch].ADSR.ReleaseVal!=0)                   // -> release not 0: do release (if 0: stop right now)
    {
     if(!s_chan[ch].ADSR.ReleaseVol)                   // --> release just started? set up the release stuff
      {
       s_chan[ch].ADSR.ReleaseStartTime=s_chan[ch].ADSR.lTime;
       s_chan[ch].ADSR.ReleaseVol=s_chan[ch].ADSR.lVolume;
       s_chan[ch].ADSR.ReleaseTime =                   // --> calc how long does it take to reach the wanted sus level
         (s_chan[ch].ADSR.ReleaseTime*
          s_chan[ch].ADSR.ReleaseVol)/1024;
      }
                                                       // -> NO release exp mode used (yet)
     v=s_chan[ch].ADSR.ReleaseVol;                     // -> get last volume
     lT=s_chan[ch].ADSR.lTime-                         // -> how much time is past?
        s_chan[ch].ADSR.ReleaseStartTime;
     l1=s_chan[ch].ADSR.ReleaseTime;
                                                       
     if(lT<l1)                                         // -> we still have to release
      {
       v=v-((v*lT)/l1);                                // --> calc new volume
      }
     else                                              // -> release is over: now really stop that sample
      {v=0;s_chan[ch].bOn=0;s_chan[ch].ADSR.ReleaseVol=0;s_chan[ch].bNoise=0;}
    }
   else                                                // -> release IS 0: release at once
    {
     v=0;s_chan[ch].bOn=0;s_chan[ch].ADSR.ReleaseVol=0;s_chan[ch].bNoise=0;
    }
  }
 else                                               
  {//--------------------------------------------------// not in release phase:
   v=1024;
   lT=s_chan[ch].ADSR.lTime;
   l1=s_chan[ch].ADSR.AttackTime;
                                                       
   if(lT<l1)                                           // attack
    {                                                  // no exp mode used (yet)
//     if(s_chan[ch].ADSR.AttackModeExp)
//      {
//       v=(v*lT)/l1;
//      }
//     else
      {
       v=(v*lT)/l1;
      }
     if(v==0) v=1;
    }
   else                                                // decay
    {                                                  // should be exp, but who cares? ;)
     l2=s_chan[ch].ADSR.DecayTime;
     v2=s_chan[ch].ADSR.SustainLevel;

     lT-=l1;
     if(lT<l2)
      {
       v-=(((v-v2)*lT)/l2);
      }
     else                                              // sustain
      {                                                // no exp mode used (yet)
       l3=s_chan[ch].ADSR.SustainTime;
       lT-=l2;
       if(s_chan[ch].ADSR.SustainModeDec>0)
        {
         if(l3!=0) v2+=((v-v2)*lT)/l3;
         else      v2=v;
        }
       else
        {
         if(l3!=0) v2-=(v2*lT)/l3;
         else      v2=v;
        }

       if(v2>v)  v2=v;
       if(v2<=0) {v2=0;s_chan[ch].bOn=0;s_chan[ch].ADSR.ReleaseVol=0;s_chan[ch].bNoise=0;}

       v=v2;
      }
    }
  }

 //----------------------------------------------------// 
 // ok, done for this channel, so increase time

 s_chan[ch].ADSR.lTime+=1;                             // 1 = 1.020408f ms;      

 if(v>1024)     v=1024;                                // adjust volume
 if(v<0)        v=0;                                  
 s_chan[ch].ADSR.lVolume=v;                            // store act volume

 return v;                                             // return the volume factor
*/


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------


/*
-----------------------------------------------------------------------------
Neill Corlett
Playstation SPU envelope timing notes
-----------------------------------------------------------------------------

This is preliminary.  This may be wrong.  But the model described herein fits
all of my experimental data, and it's just simple enough to sound right.

ADSR envelope level ranges from 0x00000000 to 0x7FFFFFFF internally.
The value returned by channel reg 0xC is (envelope_level>>16).

Each sample, an increment or decrement value will be added to or
subtracted from this envelope level.

Create the rate log table.  The values double every 4 entries.
   entry #0 = 4

    4, 5, 6, 7,
    8,10,12,14,
   16,20,24,28, ...

   entry #40 = 4096...
   entry #44 = 8192...
   entry #48 = 16384...
   entry #52 = 32768...
   entry #56 = 65536...

increments and decrements are in terms of ratelogtable[n]
n may exceed the table bounds (plan on n being between -32 and 127).
table values are all clipped between 0x00000000 and 0x3FFFFFFF

when you "voice on", the envelope is always fully reset.
(yes, it may click. the real thing does this too.)

envelope level begins at zero.

each state happens for at least 1 cycle
(transitions are not instantaneous)
this may result in some oddness: if the decay rate is uberfast, it will cut
the envelope from full down to half in one sample, potentially skipping over
the sustain level

ATTACK
------
- if the envelope level has overflowed past the max, clip to 0x7FFFFFFF and
  proceed to DECAY.

Linear attack mode:
- line extends upward to 0x7FFFFFFF
- increment per sample is ratelogtable[(Ar^0x7F)-0x10]

Logarithmic attack mode:
if envelope_level < 0x60000000:
  - line extends upward to 0x60000000
  - increment per sample is ratelogtable[(Ar^0x7F)-0x10]
else:
  - line extends upward to 0x7FFFFFFF
  - increment per sample is ratelogtable[(Ar^0x7F)-0x18]

DECAY
-----
- if ((envelope_level>>27)&0xF) <= Sl, proceed to SUSTAIN.
  Do not clip to the sustain level.
- current line ends at (envelope_level & 0x07FFFFFF)
- decrement per sample depends on (envelope_level>>28)&0x7
  0: ratelogtable[(4*(Dr^0x1F))-0x18+0]
  1: ratelogtable[(4*(Dr^0x1F))-0x18+4]
  2: ratelogtable[(4*(Dr^0x1F))-0x18+6]
  3: ratelogtable[(4*(Dr^0x1F))-0x18+8]
  4: ratelogtable[(4*(Dr^0x1F))-0x18+9]
  5: ratelogtable[(4*(Dr^0x1F))-0x18+10]
  6: ratelogtable[(4*(Dr^0x1F))-0x18+11]
  7: ratelogtable[(4*(Dr^0x1F))-0x18+12]
  (note that this is the same as the release rate formula, except that
   decay rates 10-1F aren't possible... those would be slower in theory)

SUSTAIN
-------
- no terminating condition except for voice off
- Sd=0 (increase) behavior is identical to ATTACK for both log and linear.
- Sd=1 (decrease) behavior:
Linear sustain decrease:
- line extends to 0x00000000
- decrement per sample is ratelogtable[(Sr^0x7F)-0x0F]
Logarithmic sustain decrease:
- current line ends at (envelope_level & 0x07FFFFFF)
- decrement per sample depends on (envelope_level>>28)&0x7
  0: ratelogtable[(Sr^0x7F)-0x1B+0]
  1: ratelogtable[(Sr^0x7F)-0x1B+4]
  2: ratelogtable[(Sr^0x7F)-0x1B+6]
  3: ratelogtable[(Sr^0x7F)-0x1B+8]
  4: ratelogtable[(Sr^0x7F)-0x1B+9]
  5: ratelogtable[(Sr^0x7F)-0x1B+10]
  6: ratelogtable[(Sr^0x7F)-0x1B+11]
  7: ratelogtable[(Sr^0x7F)-0x1B+12]

RELEASE
-------
- if the envelope level has overflowed to negative, clip to 0 and QUIT.

Linear release mode:
- line extends to 0x00000000
- decrement per sample is ratelogtable[(4*(Rr^0x1F))-0x0C]

Logarithmic release mode:
- line extends to (envelope_level & 0x0FFFFFFF)
- decrement per sample depends on (envelope_level>>28)&0x7
  0: ratelogtable[(4*(Rr^0x1F))-0x18+0]
  1: ratelogtable[(4*(Rr^0x1F))-0x18+4]
  2: ratelogtable[(4*(Rr^0x1F))-0x18+6]
  3: ratelogtable[(4*(Rr^0x1F))-0x18+8]
  4: ratelogtable[(4*(Rr^0x1F))-0x18+9]
  5: ratelogtable[(4*(Rr^0x1F))-0x18+10]
  6: ratelogtable[(4*(Rr^0x1F))-0x18+11]
  7: ratelogtable[(4*(Rr^0x1F))-0x18+12]

-----------------------------------------------------------------------------
*/


////////////////////////////////////////////////////////////////////////
// helpers for simple interpolation

//
// easy interpolation on upsampling, no special filter, just "Pete's common sense" tm
//
// instead of having n equal sample values in a row like:
//       ____
//           |____
//
// we compare the current delta change with the next delta change.
//
// if curr_delta is positive,
//
//  - and next delta is smaller (or changing direction):
//         \.
//          -__
//
//  - and next delta significant (at least twice) bigger:
//         --_
//            \.
//
//  - and next delta is nearly same:
//          \.
//           \.
//
//
// if curr_delta is negative,
//
//  - and next delta is smaller (or changing direction):
//          _--
//         /
//
//  - and next delta significant (at least twice) bigger:
//            /
//         __- 
//
//  - and next delta is nearly same:
//           /
//          /
//


INLINE void InterpolateUp(int ch)
{
 if(s_chan[ch].SB[32]==1)                              // flag == 1? calc step and set flag... and don't change the value in this pass
  {
   const int id1=s_chan[ch].SB[30]-s_chan[ch].SB[29];  // curr delta to next val
   const int id2=s_chan[ch].SB[31]-s_chan[ch].SB[30];  // and next delta to next-next val :)

   s_chan[ch].SB[32]=0;

   if(id1>0)                                           // curr delta positive
    {
     if(id2<id1)
      {s_chan[ch].SB[28]=id1;s_chan[ch].SB[32]=2;}
     else
     if(id2<(id1<<1))
      s_chan[ch].SB[28]=(id1*s_chan[ch].sinc)/0x10000L;
     else
      s_chan[ch].SB[28]=(id1*s_chan[ch].sinc)/0x20000L; 
    }
   else                                                // curr delta negative
    {
     if(id2>id1)
      {s_chan[ch].SB[28]=id1;s_chan[ch].SB[32]=2;}
     else
     if(id2>(id1<<1))
      s_chan[ch].SB[28]=(id1*s_chan[ch].sinc)/0x10000L;
     else
      s_chan[ch].SB[28]=(id1*s_chan[ch].sinc)/0x20000L; 
    }
  }
 else
 if(s_chan[ch].SB[32]==2)                              // flag 1: calc step and set flag... and don't change the value in this pass
  {
   s_chan[ch].SB[32]=0;

   s_chan[ch].SB[28]=(s_chan[ch].SB[28]*s_chan[ch].sinc)/0x20000L;
   if(s_chan[ch].sinc<=0x8000)
        s_chan[ch].SB[29]=s_chan[ch].SB[30]-(s_chan[ch].SB[28]*((0x10000/s_chan[ch].sinc)-1));
   else s_chan[ch].SB[29]+=s_chan[ch].SB[28];
  }
 else                                                  // no flags? add bigger val (if possible), calc smaller step, set flag1
  s_chan[ch].SB[29]+=s_chan[ch].SB[28];
}

//
// even easier interpolation on downsampling, also no special filter, again just "Pete's common sense" tm
//

INLINE void InterpolateDown(int ch)
{
 if(s_chan[ch].sinc>=0x20000L)                                 // we would skip at least one val?
  {
   s_chan[ch].SB[29]+=(s_chan[ch].SB[30]-s_chan[ch].SB[29])/2; // add easy weight
   if(s_chan[ch].sinc>=0x30000L)                               // we would skip even more vals?
    s_chan[ch].SB[29]+=(s_chan[ch].SB[31]-s_chan[ch].SB[30])/2;// add additional next weight
  }
}

////////////////////////////////////////////////////////////////////////
// helpers for gauss interpolation

#define gval0 (((short*)(&s_chan[ch].SB[29]))[gpos])
#define gval(x) (((short*)(&s_chan[ch].SB[29]))[(gpos+x)&3])



static const int gauss[]={
	0x172, 0x519, 0x176, 0x000, 0x16E, 0x519, 0x17A, 0x000, 
	0x16A, 0x518, 0x17D, 0x000, 0x166, 0x518, 0x181, 0x000, 
	0x162, 0x518, 0x185, 0x000, 0x15F, 0x518, 0x189, 0x000, 
	0x15B, 0x518, 0x18D, 0x000, 0x157, 0x517, 0x191, 0x000, 
	0x153, 0x517, 0x195, 0x000, 0x150, 0x517, 0x19A, 0x000, 
	0x14C, 0x516, 0x19E, 0x000, 0x148, 0x516, 0x1A2, 0x000, 
	0x145, 0x515, 0x1A6, 0x000, 0x141, 0x514, 0x1AA, 0x000, 
	0x13E, 0x514, 0x1AE, 0x000, 0x13A, 0x513, 0x1B2, 0x000, 
	0x137, 0x512, 0x1B7, 0x001, 0x133, 0x511, 0x1BB, 0x001, 
	0x130, 0x511, 0x1BF, 0x001, 0x12C, 0x510, 0x1C3, 0x001, 
	0x129, 0x50F, 0x1C8, 0x001, 0x125, 0x50E, 0x1CC, 0x001, 
	0x122, 0x50D, 0x1D0, 0x001, 0x11E, 0x50C, 0x1D5, 0x001, 
	0x11B, 0x50B, 0x1D9, 0x001, 0x118, 0x50A, 0x1DD, 0x001, 
	0x114, 0x508, 0x1E2, 0x001, 0x111, 0x507, 0x1E6, 0x002, 
	0x10E, 0x506, 0x1EB, 0x002, 0x10B, 0x504, 0x1EF, 0x002, 
	0x107, 0x503, 0x1F3, 0x002, 0x104, 0x502, 0x1F8, 0x002, 
	0x101, 0x500, 0x1FC, 0x002, 0x0FE, 0x4FF, 0x201, 0x002, 
	0x0FB, 0x4FD, 0x205, 0x003, 0x0F8, 0x4FB, 0x20A, 0x003, 
	0x0F5, 0x4FA, 0x20F, 0x003, 0x0F2, 0x4F8, 0x213, 0x003, 
	0x0EF, 0x4F6, 0x218, 0x003, 0x0EC, 0x4F5, 0x21C, 0x004, 
	0x0E9, 0x4F3, 0x221, 0x004, 0x0E6, 0x4F1, 0x226, 0x004, 
	0x0E3, 0x4EF, 0x22A, 0x004, 0x0E0, 0x4ED, 0x22F, 0x004, 
	0x0DD, 0x4EB, 0x233, 0x005, 0x0DA, 0x4E9, 0x238, 0x005, 
	0x0D7, 0x4E7, 0x23D, 0x005, 0x0D4, 0x4E5, 0x241, 0x005, 
	0x0D2, 0x4E3, 0x246, 0x006, 0x0CF, 0x4E0, 0x24B, 0x006, 
	0x0CC, 0x4DE, 0x250, 0x006, 0x0C9, 0x4DC, 0x254, 0x006, 
	0x0C7, 0x4D9, 0x259, 0x007, 0x0C4, 0x4D7, 0x25E, 0x007, 
	0x0C1, 0x4D5, 0x263, 0x007, 0x0BF, 0x4D2, 0x267, 0x008, 
	0x0BC, 0x4D0, 0x26C, 0x008, 0x0BA, 0x4CD, 0x271, 0x008, 
	0x0B7, 0x4CB, 0x276, 0x009, 0x0B4, 0x4C8, 0x27B, 0x009, 
	0x0B2, 0x4C5, 0x280, 0x009, 0x0AF, 0x4C3, 0x284, 0x00A, 
	0x0AD, 0x4C0, 0x289, 0x00A, 0x0AB, 0x4BD, 0x28E, 0x00A, 
	0x0A8, 0x4BA, 0x293, 0x00B, 0x0A6, 0x4B7, 0x298, 0x00B, 
	0x0A3, 0x4B5, 0x29D, 0x00B, 0x0A1, 0x4B2, 0x2A2, 0x00C, 
	0x09F, 0x4AF, 0x2A6, 0x00C, 0x09C, 0x4AC, 0x2AB, 0x00D, 
	0x09A, 0x4A9, 0x2B0, 0x00D, 0x098, 0x4A6, 0x2B5, 0x00E, 
	0x096, 0x4A2, 0x2BA, 0x00E, 0x093, 0x49F, 0x2BF, 0x00F, 
	0x091, 0x49C, 0x2C4, 0x00F, 0x08F, 0x499, 0x2C9, 0x00F, 
	0x08D, 0x496, 0x2CE, 0x010, 0x08B, 0x492, 0x2D3, 0x010, 
	0x089, 0x48F, 0x2D8, 0x011, 0x086, 0x48C, 0x2DC, 0x011, 
	0x084, 0x488, 0x2E1, 0x012, 0x082, 0x485, 0x2E6, 0x013, 
	0x080, 0x481, 0x2EB, 0x013, 0x07E, 0x47E, 0x2F0, 0x014, 
	0x07C, 0x47A, 0x2F5, 0x014, 0x07A, 0x477, 0x2FA, 0x015, 
	0x078, 0x473, 0x2FF, 0x015, 0x076, 0x470, 0x304, 0x016, 
	0x075, 0x46C, 0x309, 0x017, 0x073, 0x468, 0x30E, 0x017, 
	0x071, 0x465, 0x313, 0x018, 0x06F, 0x461, 0x318, 0x018, 
	0x06D, 0x45D, 0x31D, 0x019, 0x06B, 0x459, 0x322, 0x01A, 
	0x06A, 0x455, 0x326, 0x01B, 0x068, 0x452, 0x32B, 0x01B, 
	0x066, 0x44E, 0x330, 0x01C, 0x064, 0x44A, 0x335, 0x01D, 
	0x063, 0x446, 0x33A, 0x01D, 0x061, 0x442, 0x33F, 0x01E, 
	0x05F, 0x43E, 0x344, 0x01F, 0x05E, 0x43A, 0x349, 0x020, 
	0x05C, 0x436, 0x34E, 0x020, 0x05A, 0x432, 0x353, 0x021, 
	0x059, 0x42E, 0x357, 0x022, 0x057, 0x42A, 0x35C, 0x023, 
	0x056, 0x425, 0x361, 0x024, 0x054, 0x421, 0x366, 0x024, 
	0x053, 0x41D, 0x36B, 0x025, 0x051, 0x419, 0x370, 0x026, 
	0x050, 0x415, 0x374, 0x027, 0x04E, 0x410, 0x379, 0x028, 
	0x04D, 0x40C, 0x37E, 0x029, 0x04C, 0x408, 0x383, 0x02A, 
	0x04A, 0x403, 0x388, 0x02B, 0x049, 0x3FF, 0x38C, 0x02C, 
	0x047, 0x3FB, 0x391, 0x02D, 0x046, 0x3F6, 0x396, 0x02E, 
	0x045, 0x3F2, 0x39B, 0x02F, 0x043, 0x3ED, 0x39F, 0x030, 
	0x042, 0x3E9, 0x3A4, 0x031, 0x041, 0x3E5, 0x3A9, 0x032, 
	0x040, 0x3E0, 0x3AD, 0x033, 0x03E, 0x3DC, 0x3B2, 0x034, 
	0x03D, 0x3D7, 0x3B7, 0x035, 0x03C, 0x3D2, 0x3BB, 0x036, 
	0x03B, 0x3CE, 0x3C0, 0x037, 0x03A, 0x3C9, 0x3C5, 0x038, 
	0x038, 0x3C5, 0x3C9, 0x03A, 0x037, 0x3C0, 0x3CE, 0x03B, 
	0x036, 0x3BB, 0x3D2, 0x03C, 0x035, 0x3B7, 0x3D7, 0x03D, 
	0x034, 0x3B2, 0x3DC, 0x03E, 0x033, 0x3AD, 0x3E0, 0x040, 
	0x032, 0x3A9, 0x3E5, 0x041, 0x031, 0x3A4, 0x3E9, 0x042, 
	0x030, 0x39F, 0x3ED, 0x043, 0x02F, 0x39B, 0x3F2, 0x045, 
	0x02E, 0x396, 0x3F6, 0x046, 0x02D, 0x391, 0x3FB, 0x047, 
	0x02C, 0x38C, 0x3FF, 0x049, 0x02B, 0x388, 0x403, 0x04A, 
	0x02A, 0x383, 0x408, 0x04C, 0x029, 0x37E, 0x40C, 0x04D, 
	0x028, 0x379, 0x410, 0x04E, 0x027, 0x374, 0x415, 0x050, 
	0x026, 0x370, 0x419, 0x051, 0x025, 0x36B, 0x41D, 0x053, 
	0x024, 0x366, 0x421, 0x054, 0x024, 0x361, 0x425, 0x056, 
	0x023, 0x35C, 0x42A, 0x057, 0x022, 0x357, 0x42E, 0x059, 
	0x021, 0x353, 0x432, 0x05A, 0x020, 0x34E, 0x436, 0x05C, 
	0x020, 0x349, 0x43A, 0x05E, 0x01F, 0x344, 0x43E, 0x05F, 
	0x01E, 0x33F, 0x442, 0x061, 0x01D, 0x33A, 0x446, 0x063, 
	0x01D, 0x335, 0x44A, 0x064, 0x01C, 0x330, 0x44E, 0x066, 
	0x01B, 0x32B, 0x452, 0x068, 0x01B, 0x326, 0x455, 0x06A, 
	0x01A, 0x322, 0x459, 0x06B, 0x019, 0x31D, 0x45D, 0x06D, 
	0x018, 0x318, 0x461, 0x06F, 0x018, 0x313, 0x465, 0x071, 
	0x017, 0x30E, 0x468, 0x073, 0x017, 0x309, 0x46C, 0x075, 
	0x016, 0x304, 0x470, 0x076, 0x015, 0x2FF, 0x473, 0x078, 
	0x015, 0x2FA, 0x477, 0x07A, 0x014, 0x2F5, 0x47A, 0x07C, 
	0x014, 0x2F0, 0x47E, 0x07E, 0x013, 0x2EB, 0x481, 0x080, 
	0x013, 0x2E6, 0x485, 0x082, 0x012, 0x2E1, 0x488, 0x084, 
	0x011, 0x2DC, 0x48C, 0x086, 0x011, 0x2D8, 0x48F, 0x089, 
	0x010, 0x2D3, 0x492, 0x08B, 0x010, 0x2CE, 0x496, 0x08D, 
	0x00F, 0x2C9, 0x499, 0x08F, 0x00F, 0x2C4, 0x49C, 0x091, 
	0x00F, 0x2BF, 0x49F, 0x093, 0x00E, 0x2BA, 0x4A2, 0x096, 
	0x00E, 0x2B5, 0x4A6, 0x098, 0x00D, 0x2B0, 0x4A9, 0x09A, 
	0x00D, 0x2AB, 0x4AC, 0x09C, 0x00C, 0x2A6, 0x4AF, 0x09F, 
	0x00C, 0x2A2, 0x4B2, 0x0A1, 0x00B, 0x29D, 0x4B5, 0x0A3, 
	0x00B, 0x298, 0x4B7, 0x0A6, 0x00B, 0x293, 0x4BA, 0x0A8, 
	0x00A, 0x28E, 0x4BD, 0x0AB, 0x00A, 0x289, 0x4C0, 0x0AD, 
	0x00A, 0x284, 0x4C3, 0x0AF, 0x009, 0x280, 0x4C5, 0x0B2, 
	0x009, 0x27B, 0x4C8, 0x0B4, 0x009, 0x276, 0x4CB, 0x0B7, 
	0x008, 0x271, 0x4CD, 0x0BA, 0x008, 0x26C, 0x4D0, 0x0BC, 
	0x008, 0x267, 0x4D2, 0x0BF, 0x007, 0x263, 0x4D5, 0x0C1, 
	0x007, 0x25E, 0x4D7, 0x0C4, 0x007, 0x259, 0x4D9, 0x0C7, 
	0x006, 0x254, 0x4DC, 0x0C9, 0x006, 0x250, 0x4DE, 0x0CC, 
	0x006, 0x24B, 0x4E0, 0x0CF, 0x006, 0x246, 0x4E3, 0x0D2, 
	0x005, 0x241, 0x4E5, 0x0D4, 0x005, 0x23D, 0x4E7, 0x0D7, 
	0x005, 0x238, 0x4E9, 0x0DA, 0x005, 0x233, 0x4EB, 0x0DD, 
	0x004, 0x22F, 0x4ED, 0x0E0, 0x004, 0x22A, 0x4EF, 0x0E3, 
	0x004, 0x226, 0x4F1, 0x0E6, 0x004, 0x221, 0x4F3, 0x0E9, 
	0x004, 0x21C, 0x4F5, 0x0EC, 0x003, 0x218, 0x4F6, 0x0EF, 
	0x003, 0x213, 0x4F8, 0x0F2, 0x003, 0x20F, 0x4FA, 0x0F5, 
	0x003, 0x20A, 0x4FB, 0x0F8, 0x003, 0x205, 0x4FD, 0x0FB, 
	0x002, 0x201, 0x4FF, 0x0FE, 0x002, 0x1FC, 0x500, 0x101, 
	0x002, 0x1F8, 0x502, 0x104, 0x002, 0x1F3, 0x503, 0x107, 
	0x002, 0x1EF, 0x504, 0x10B, 0x002, 0x1EB, 0x506, 0x10E, 
	0x002, 0x1E6, 0x507, 0x111, 0x001, 0x1E2, 0x508, 0x114, 
	0x001, 0x1DD, 0x50A, 0x118, 0x001, 0x1D9, 0x50B, 0x11B, 
	0x001, 0x1D5, 0x50C, 0x11E, 0x001, 0x1D0, 0x50D, 0x122, 
	0x001, 0x1CC, 0x50E, 0x125, 0x001, 0x1C8, 0x50F, 0x129, 
	0x001, 0x1C3, 0x510, 0x12C, 0x001, 0x1BF, 0x511, 0x130, 
	0x001, 0x1BB, 0x511, 0x133, 0x001, 0x1B7, 0x512, 0x137, 
	0x000, 0x1B2, 0x513, 0x13A, 0x000, 0x1AE, 0x514, 0x13E, 
	0x000, 0x1AA, 0x514, 0x141, 0x000, 0x1A6, 0x515, 0x145, 
	0x000, 0x1A2, 0x516, 0x148, 0x000, 0x19E, 0x516, 0x14C, 
	0x000, 0x19A, 0x517, 0x150, 0x000, 0x195, 0x517, 0x153, 
	0x000, 0x191, 0x517, 0x157, 0x000, 0x18D, 0x518, 0x15B, 
	0x000, 0x189, 0x518, 0x15F, 0x000, 0x185, 0x518, 0x162, 
	0x000, 0x181, 0x518, 0x166, 0x000, 0x17D, 0x518, 0x16A, 
	0x000, 0x17A, 0x519, 0x16E, 0x000, 0x176, 0x519, 0x172};


////////////////////////////////////////////////////////////////////////


#define gvall0 gauss_window[gauss_ptr]
#define gvall(x) gauss_window[(gauss_ptr+x)&3]
#define gvalr0 gauss_window[4+gauss_ptr]
#define gvalr(x) gauss_window[4+((gauss_ptr+x)&3)]

////////////////////////////////////////////////////////////////////////
// MIX XA & CDDA
////////////////////////////////////////////////////////////////////////

INLINE void MixXA(void)
{
 int ns;
 uint32_t l;

 for(ns=0;ns<NSSIZE && XAPlay!=XAFeed;ns++)
  {
   XALastVal=*XAPlay++;
   if(XAPlay==XAEnd) XAPlay=XAStart;
#ifdef XA_HACK
   SSumL[ns]+=(((short)(XALastVal&0xffff))       * iLeftXAVol)/32768;
   SSumR[ns]+=(((short)((XALastVal>>16)&0xffff)) * iRightXAVol)/32768;
#else
   SSumL[ns]+=(((short)(XALastVal&0xffff))       * iLeftXAVol)/32767;
   SSumR[ns]+=(((short)((XALastVal>>16)&0xffff)) * iRightXAVol)/32767;
#endif
  }

 if(XAPlay==XAFeed && XARepeat)
  {
   XARepeat--;
   for(;ns<NSSIZE;ns++)
    {
#ifdef XA_HACK
     SSumL[ns]+=(((short)(XALastVal&0xffff))       * iLeftXAVol)/32768;
     SSumR[ns]+=(((short)((XALastVal>>16)&0xffff)) * iRightXAVol)/32768;
#else
     SSumL[ns]+=(((short)(XALastVal&0xffff))       * iLeftXAVol)/32767;
     SSumR[ns]+=(((short)((XALastVal>>16)&0xffff)) * iRightXAVol)/32767;
#endif
    }
  }

 for(ns=0;ns<NSSIZE && CDDAPlay!=CDDAFeed && (CDDAPlay!=CDDAEnd-1||CDDAFeed!=CDDAStart);ns++)
  {
   l=*CDDAPlay++;
   if(CDDAPlay==CDDAEnd) CDDAPlay=CDDAStart;
   SSumL[ns]+=(((short)(l&0xffff))       * iLeftXAVol)/32767;
   SSumR[ns]+=(((short)((l>>16)&0xffff)) * iRightXAVol)/32767;
  }
}

////////////////////////////////////////////////////////////////////////
// small linux time helper... only used for watchdog
////////////////////////////////////////////////////////////////////////

unsigned long timeGetTime_spu()
{
 struct timeval tv;
 gettimeofday(&tv, 0);                                 // well, maybe there are better ways
 return tv.tv_sec * 1000 + tv.tv_usec/1000;            // to do that, but at least it works
}

////////////////////////////////////////////////////////////////////////
// FEED XA 
////////////////////////////////////////////////////////////////////////

INLINE void FeedXA(xa_decode_t *xap)
{
 int sinc,spos,i,iSize,iPlace,vl,vr;

 if(!bSPUIsOpen) return;

 xapGlobal = xap;                                      // store info for save states
 XARepeat  = 100;                                      // set up repeat

#ifdef XA_HACK
 iSize=((45500*xap->nsamples)/xap->freq);              // get size
#else
 iSize=((44100*xap->nsamples)/xap->freq);              // get size
#endif
 if(!iSize) return;                                    // none? bye

 if(XAFeed<XAPlay) iPlace=XAPlay-XAFeed;               // how much space in my buf?
 else              iPlace=(XAEnd-XAFeed) + (XAPlay-XAStart);

 if(iPlace==0) return;                                 // no place at all

 //----------------------------------------------------//
 if(iXAPitch)                                          // pitch change option?
  {
   static DWORD dwLT=0;
   static DWORD dwFPS=0;
   static int   iFPSCnt=0;
   static int   iLastSize=0;
   static DWORD dwL1=0;
   DWORD dw=timeGetTime_spu(),dw1,dw2;

   iPlace=iSize;

   dwFPS+=dw-dwLT;iFPSCnt++;

   dwLT=dw;
                                       
   if(iFPSCnt>=10)
    {
     if(!dwFPS) dwFPS=1;
     dw1=1000000/dwFPS; 
     if(dw1>=(dwL1-100) && dw1<=(dwL1+100)) dw1=dwL1;
     else dwL1=dw1;
     dw2=(xap->freq*100/xap->nsamples);
     if((!dw1)||((dw2+100)>=dw1)) iLastSize=0;
     else
      {
       iLastSize=iSize*dw2/dw1;
       if(iLastSize>iPlace) iLastSize=iPlace;
       iSize=iLastSize;
      }
     iFPSCnt=0;dwFPS=0;
    }
   else
    {
     if(iLastSize) iSize=iLastSize;
    }
  }
 //----------------------------------------------------//

 spos=0x10000L;
 sinc = (xap->nsamples << 16) / iSize;                 // calc freq by num / size

 if(xap->stereo)
{
   uint32_t * pS=(uint32_t *)xap->pcm;
   uint32_t l=0;

   if(iXAPitch)
    {
     int32_t l1,l2;short s;
     for(i=0;i<iSize;i++)
      {
       if(iUseInterpolation==2) 
        {
         while(spos>=0x10000L)
          {
           l = *pS++;
           gauss_window[gauss_ptr] = (short)LOWORD(l);
           gauss_window[4+gauss_ptr] = (short)HIWORD(l);
           gauss_ptr = (gauss_ptr+1) & 3;
           spos -= 0x10000L;
          }
         vl = (spos >> 6) & ~3;
         vr=(gauss[vl]*gvall0)&~2047;
         vr+=(gauss[vl+1]*gvall(1))&~2047;
         vr+=(gauss[vl+2]*gvall(2))&~2047;
         vr+=(gauss[vl+3]*gvall(3))&~2047;
         l= (vr >> 11) & 0xffff;
         vr=(gauss[vl]*gvalr0)&~2047;
         vr+=(gauss[vl+1]*gvalr(1))&~2047;
         vr+=(gauss[vl+2]*gvalr(2))&~2047;
         vr+=(gauss[vl+3]*gvalr(3))&~2047;
         l |= vr << 5;
        }
       else
        {
         while(spos>=0x10000L)
          {
           l = *pS++;
           spos -= 0x10000L;
          }
        }

       s=(short)LOWORD(l);
       l1=s;
       l1=(l1*iPlace)/iSize;
       if(l1<-32767) l1=-32767;
       if(l1> 32767) l1=32767;
       s=(short)HIWORD(l);
       l2=s;
       l2=(l2*iPlace)/iSize;
       if(l2<-32767) l2=-32767;
       if(l2> 32767) l2=32767;
       l=(l1&0xffff)|(l2<<16);

       *XAFeed++=l;

       if(XAFeed==XAEnd) XAFeed=XAStart;
       if(XAFeed==XAPlay) 
        {
         if(XAPlay!=XAStart) XAFeed=XAPlay-1;
         break;
        }

       spos += sinc;
      }
    }
   else
    {
     for(i=0;i<iSize;i++)
      {
       if(iUseInterpolation==2) 
        {
         while(spos>=0x10000L)
          {
           l = *pS++;
           gauss_window[gauss_ptr] = (short)LOWORD(l);
           gauss_window[4+gauss_ptr] = (short)HIWORD(l);
           gauss_ptr = (gauss_ptr+1) & 3;
           spos -= 0x10000L;
          }
         vl = (spos >> 6) & ~3;
         vr=(gauss[vl]*gvall0)&~2047;
         vr+=(gauss[vl+1]*gvall(1))&~2047;
         vr+=(gauss[vl+2]*gvall(2))&~2047;
         vr+=(gauss[vl+3]*gvall(3))&~2047;
         l= (vr >> 11) & 0xffff;
         vr=(gauss[vl]*gvalr0)&~2047;
         vr+=(gauss[vl+1]*gvalr(1))&~2047;
         vr+=(gauss[vl+2]*gvalr(2))&~2047;
         vr+=(gauss[vl+3]*gvalr(3))&~2047;
         l |= vr << 5;
        }
       else
        {
         while(spos>=0x10000L)
          {
           l = *pS++;
           spos -= 0x10000L;
          }
        }

       *XAFeed++=l;

       if(XAFeed==XAEnd) XAFeed=XAStart;
       if(XAFeed==XAPlay) 
        {
         if(XAPlay!=XAStart) XAFeed=XAPlay-1;
         break;
        }

       spos += sinc;
      }
    }
  }
 else
  {
   unsigned short * pS=(unsigned short *)xap->pcm;
   uint32_t l;short s=0;

   if(iXAPitch)
    {
     int32_t l1;
     for(i=0;i<iSize;i++)
      {
       if(iUseInterpolation==2) 
        {
         while(spos>=0x10000L)
          {
           gauss_window[gauss_ptr] = (short)*pS++;
           gauss_ptr = (gauss_ptr+1) & 3;
           spos -= 0x10000L;
          }
         vl = (spos >> 6) & ~3;
         vr=(gauss[vl]*gvall0)&~2047;
         vr+=(gauss[vl+1]*gvall(1))&~2047;
         vr+=(gauss[vl+2]*gvall(2))&~2047;
         vr+=(gauss[vl+3]*gvall(3))&~2047;
         l1=s= vr >> 11;
         l1 &= 0xffff;
        }
       else
        {
         while(spos>=0x10000L)
          {
           s = *pS++;
           spos -= 0x10000L;
          }
         l1=s;
        }

       l1=(l1*iPlace)/iSize;
       if(l1<-32767) l1=-32767;
       if(l1> 32767) l1=32767;
       l=(l1&0xffff)|(l1<<16);
       *XAFeed++=l;

       if(XAFeed==XAEnd) XAFeed=XAStart;
       if(XAFeed==XAPlay) 
        {
         if(XAPlay!=XAStart) XAFeed=XAPlay-1;
         break;
        }

       spos += sinc;
      }
    }
   else
    {
     for(i=0;i<iSize;i++)
      {
       if(iUseInterpolation==2) 
        {
         while(spos>=0x10000L)
          {
           gauss_window[gauss_ptr] = (short)*pS++;
           gauss_ptr = (gauss_ptr+1) & 3;
           spos -= 0x10000L;
          }
         vl = (spos >> 6) & ~3;
         vr=(gauss[vl]*gvall0)&~2047;
         vr+=(gauss[vl+1]*gvall(1))&~2047;
         vr+=(gauss[vl+2]*gvall(2))&~2047;
         vr+=(gauss[vl+3]*gvall(3))&~2047;
         l=s= vr >> 11;
         l &= 0xffff;
        }
       else
        {
         while(spos>=0x10000L)
          {
           s = *pS++;
           spos -= 0x10000L;
          }
         l=s;
        }

       *XAFeed++=(l|(l<<16));

       if(XAFeed==XAEnd) XAFeed=XAStart;
       if(XAFeed==XAPlay) 
        {
         if(XAPlay!=XAStart) XAFeed=XAPlay-1;
         break;
        }

       spos += sinc;
      }
    }
  }
}

////////////////////////////////////////////////////////////////////////
// FEED CDDA
////////////////////////////////////////////////////////////////////////

INLINE void FeedCDDA(unsigned char *pcm, int nBytes)
{
 while(nBytes>0)
  {
   if(CDDAFeed==CDDAEnd) CDDAFeed=CDDAStart;
   while(CDDAFeed==CDDAPlay-1||
         (CDDAFeed==CDDAEnd-1&&CDDAPlay==CDDAStart))
   {
    if (!iUseTimer) usleep(1000);
    else return;
   }
   *CDDAFeed++=(*pcm | (*(pcm+1)<<8) | (*(pcm+2)<<16) | (*(pcm+3)<<24));
   nBytes-=4;
   pcm+=4;
  }
}


////////////////////////////////////////////////////////////////////////
// START SOUND... called by main thread to setup a new sound on a channel
////////////////////////////////////////////////////////////////////////

INLINE void StartSound(int ch)
{
 StartADSR(ch);
 StartREVERB(ch);

 s_chan[ch].pCurr=s_chan[ch].pStart;                   // set sample start

 s_chan[ch].s_1=0;                                     // init mixing vars
 s_chan[ch].s_2=0;
 s_chan[ch].iSBPos=28;

 s_chan[ch].bNew=0;                                    // init channel flags
 s_chan[ch].bStop=0;
 s_chan[ch].bOn=1;

 s_chan[ch].SB[29]=0;                                  // init our interpolation helpers
 s_chan[ch].SB[30]=0;

 if(iUseInterpolation>=2)                              // gauss interpolation?
      {s_chan[ch].spos=0x30000L;s_chan[ch].SB[28]=0;}  // -> start with more decoding
 else {s_chan[ch].spos=0x10000L;s_chan[ch].SB[31]=0;}  // -> no/simple interpolation starts with one 44100 decoding

 dwNewChannel&=~(1<<ch);                               // clear new channel bit
}

////////////////////////////////////////////////////////////////////////
// ALL KIND OF HELPERS
////////////////////////////////////////////////////////////////////////

INLINE void VoiceChangeFrequency(int ch)
{
 s_chan[ch].iUsedFreq=s_chan[ch].iActFreq;             // -> take it and calc steps
 s_chan[ch].sinc=s_chan[ch].iRawPitch<<4;
 if(!s_chan[ch].sinc) s_chan[ch].sinc=1;
 if(iUseInterpolation==1) s_chan[ch].SB[32]=1;         // -> freq change in simle imterpolation mode: set flag
}

////////////////////////////////////////////////////////////////////////

INLINE void FModChangeFrequency(int ch,int ns)
{
 int NP=s_chan[ch].iRawPitch;

 NP=((32768L+iFMod[ns])*NP)/32768L;

 if(NP>0x3fff) NP=0x3fff;
 if(NP<0x1)    NP=0x1;

 NP=(44100L*NP)/(4096L);                               // calc frequency

 s_chan[ch].iActFreq=NP;
 s_chan[ch].iUsedFreq=NP;
 s_chan[ch].sinc=(((NP/10)<<16)/4410);
 if(!s_chan[ch].sinc) s_chan[ch].sinc=1;
 if(iUseInterpolation==1)                              // freq change in simple interpolation mode
 s_chan[ch].SB[32]=1;
 iFMod[ns]=0;
}                    

////////////////////////////////////////////////////////////////////////

// noise handler... just produces some noise data
// surely wrong... and no noise frequency (spuCtrl&0x3f00) will be used...
// and sometimes the noise will be used as fmod modulation... pfff

INLINE int iGetNoiseVal(int ch)
{
 int fa;

 if((dwNoiseVal<<=1)&0x80000000L)
  {
   dwNoiseVal^=0x0040001L;
   fa=((dwNoiseVal>>2)&0x7fff);
   fa=-fa;
  }
 else fa=(dwNoiseVal>>2)&0x7fff;

 // mmm... depending on the noise freq we allow bigger/smaller changes to the previous val
 fa=s_chan[ch].iOldNoise+((fa-s_chan[ch].iOldNoise)/((0x001f-((spuCtrl&0x3f00)>>9))+1));
 if(fa>32767L)  fa=32767L;
 if(fa<-32767L) fa=-32767L;              
 s_chan[ch].iOldNoise=fa;

 if(iUseInterpolation<2)                               // no gauss/cubic interpolation?
 s_chan[ch].SB[29] = fa;                               // -> store noise val in "current sample" slot
 return fa;
}                                 

////////////////////////////////////////////////////////////////////////

INLINE void StoreInterpolationVal(int ch,int fa)
{
 if(s_chan[ch].bFMod==2)                               // fmod freq channel
  s_chan[ch].SB[29]=fa;
 else
  {
   if((spuCtrl&0x4000)==0) fa=0;                       // muted?
   else                                                // else adjust
    {
     if(fa>32767L)  fa=32767L;
     if(fa<-32767L) fa=-32767L;              
    }

   if(iUseInterpolation>=2)                            // gauss/cubic interpolation
    {     
     int gpos = s_chan[ch].SB[28];
     gval0 = fa;          
     gpos = (gpos+1) & 3;
     s_chan[ch].SB[28] = gpos;
    }
   else
   if(iUseInterpolation==1)                            // simple interpolation
    {
     s_chan[ch].SB[28] = 0;                    
     s_chan[ch].SB[29] = s_chan[ch].SB[30];            // -> helpers for simple linear interpolation: delay real val for two slots, and calc the two deltas, for a 'look at the future behaviour'
     s_chan[ch].SB[30] = s_chan[ch].SB[31];
     s_chan[ch].SB[31] = fa;
     s_chan[ch].SB[32] = 1;                            // -> flag: calc new interolation
    }
   else s_chan[ch].SB[29]=fa;                          // no interpolation
  }
}

////////////////////////////////////////////////////////////////////////

INLINE int iGetInterpolationVal(int ch)
{
 int fa;

 if(s_chan[ch].bFMod==2) return s_chan[ch].SB[29];

 switch(iUseInterpolation)
  {   
   //--------------------------------------------------//
   case 3:                                             // cubic interpolation
    {
     long xd;int gpos;
     xd = ((s_chan[ch].spos) >> 1)+1;
     gpos = s_chan[ch].SB[28];

     fa  = gval(3) - 3*gval(2) + 3*gval(1) - gval0;
     fa *= (xd - (2<<15)) / 6;
     fa >>= 15;
     fa += gval(2) - gval(1) - gval(1) + gval0;
     fa *= (xd - (1<<15)) >> 1;
     fa >>= 15;
     fa += gval(1) - gval0;
     fa *= xd;
     fa >>= 15;
     fa = fa + gval0;

    } break;
   //--------------------------------------------------//
   case 2:                                             // gauss interpolation
    {
     int vl, vr;int gpos;
     vl = (s_chan[ch].spos >> 6) & ~3;
     gpos = s_chan[ch].SB[28];
     vr=(gauss[vl]*gval0)&~2047;
     vr+=(gauss[vl+1]*gval(1))&~2047;
     vr+=(gauss[vl+2]*gval(2))&~2047;
     vr+=(gauss[vl+3]*gval(3))&~2047;
     fa = vr>>11;
    } break;
   //--------------------------------------------------//
   case 1:                                             // simple interpolation
    {
     if(s_chan[ch].sinc<0x10000L)                      // -> upsampling?
          InterpolateUp(ch);                           // --> interpolate up
     else InterpolateDown(ch);                         // --> else down
     fa=s_chan[ch].SB[29];
    } break;
   //--------------------------------------------------//
   default:                                            // no interpolation
    {
     fa=s_chan[ch].SB[29];                  
    } break;
   //--------------------------------------------------//
  }

 return fa;
}

////////////////////////////////////////////////////////////////////////
// MAIN SPU FUNCTION
// here is the main job handler... thread, timer or direct func call
// basically the whole sound processing is done in this fat func!
////////////////////////////////////////////////////////////////////////

// 5 ms waiting phase, if buffer is full and no new sound has to get started
// .. can be made smaller (smallest val: 1 ms), but bigger waits give
// better performance

#define PAUSE_W 5
#define PAUSE_L 5000

////////////////////////////////////////////////////////////////////////

static void *MAINThread(void *arg)
{
 int s_1,s_2,fa,ns;
#ifndef _MACOSX
 int voldiv = iVolume;
#else
 const int voldiv = 2;
#endif
 unsigned char * start;unsigned int nSample;
 int ch,predict_nr,shift_factor,flags,d,s;
 int bIRQReturn=0;

 while(!bEndThread)                                    // until we are shutting down
  {
   // ok, at the beginning we are looking if there is
   // enuff free place in the dsound/oss buffer to
   // fill in new data, or if there is a new channel to start.
   // if not, we wait (thread) or return (timer/spuasync)
   // until enuff free place is available/a new channel gets
   // started

   if(dwNewChannel)                                    // new channel should start immedately?
    {                                                  // (at least one bit 0 ... MAXCHANNEL is set?)
     iSecureStart++;                                   // -> set iSecure
     if(iSecureStart>5) iSecureStart=0;                //    (if it is set 5 times - that means on 5 tries a new samples has been started - in a row, we will reset it, to give the sound update a chance)
    }
   else iSecureStart=0;                                // 0: no new channel should start

   while(!iSecureStart && !bEndThread &&               // no new start? no thread end?
         (SoundGetBytesBuffered()>TESTSIZE))           // and still enuff data in sound buffer?
    {
     iSecureStart=0;                                   // reset secure

     if(iUseTimer) return 0;                           // linux no-thread mode? bye
     usleep(PAUSE_L);                                  // else sleep for x ms (linux)

     if(dwNewChannel) iSecureStart=1;                  // if a new channel kicks in (or, of course, sound buffer runs low), we will leave the loop
    }

   //--------------------------------------------------// continue from irq handling in timer mode? 

   if(lastch>=0)                                       // will be -1 if no continue is pending
    {
     ch=lastch; ns=lastns; lastch=-1;                  // -> setup all kind of vars to continue
     goto GOON;                                        // -> directly jump to the continue point
    }

   //--------------------------------------------------//
   //- main channel loop                              -// 
   //--------------------------------------------------//
    {
     for(ch=0;ch<MAXCHAN;ch++)                         // loop em all... we will collect 1 ms of sound of each playing channel
      {
       if(s_chan[ch].bNew) StartSound(ch);             // start new sound
       if(!s_chan[ch].bOn) continue;                   // channel not playing? next

       if(s_chan[ch].iActFreq!=s_chan[ch].iUsedFreq)   // new psx frequency?
        VoiceChangeFrequency(ch);

       ns=0;
       while(ns<NSSIZE)                                // loop until 1 ms of data is reached
        {
         if(s_chan[ch].bFMod==1 && iFMod[ns])          // fmod freq channel
          FModChangeFrequency(ch,ns);

         while(s_chan[ch].spos>=0x10000L)
          {
           if(s_chan[ch].iSBPos==28)                   // 28 reached?
            {
             start=s_chan[ch].pCurr;                   // set up the current pos

             if (start == (unsigned char*)-1)          // special "stop" sign
              {
               s_chan[ch].bOn=0;                       // -> turn everything off
               s_chan[ch].ADSRX.lVolume=0;
               s_chan[ch].ADSRX.EnvelopeVol=0;
               goto ENDX;                              // -> and done for this channel
              }

             s_chan[ch].iSBPos=0;

             //////////////////////////////////////////// spu irq handler here? mmm... do it later

             s_1=s_chan[ch].s_1;
             s_2=s_chan[ch].s_2;

             predict_nr=(int)*start;start++;
             shift_factor=predict_nr&0xf;
             predict_nr >>= 4;
             flags=(int)*start;start++;

             // -------------------------------------- // 

             for (nSample=0;nSample<28;start++)      
              {
               d=(int)*start;
               s=((d&0xf)<<12);
               if(s&0x8000) s|=0xffff0000;

               fa=(s >> shift_factor);
               fa=fa + ((s_1 * f[predict_nr][0])>>6) + ((s_2 * f[predict_nr][1])>>6);
               s_2=s_1;s_1=fa;
               s=((d & 0xf0) << 8);

               s_chan[ch].SB[nSample++]=fa;

               if(s&0x8000) s|=0xffff0000;
               fa=(s>>shift_factor);
               fa=fa + ((s_1 * f[predict_nr][0])>>6) + ((s_2 * f[predict_nr][1])>>6);
               s_2=s_1;s_1=fa;

               s_chan[ch].SB[nSample++]=fa;
              }

             //////////////////////////////////////////// irq check

             if(irqCallback && (spuCtrl&0x40))         // some callback and irq active?
              {
               if((pSpuIrq >  start-16 &&              // irq address reached?
                   pSpuIrq <= start) ||
                  ((flags&1) &&                        // special: irq on looping addr, when stop/loop flag is set 
                   (pSpuIrq >  s_chan[ch].pLoop-16 &&
                    pSpuIrq <= s_chan[ch].pLoop)))
               {
                 s_chan[ch].iIrqDone=1;                // -> debug flag
                 irqCallback();                        // -> call main emu

                 if(iSPUIRQWait)                       // -> option: wait after irq for main emu
                  {
                   iSpuAsyncWait=1;
                   bIRQReturn=1;
                  }
                }
              }

             //////////////////////////////////////////// flag handler

             if((flags&4) && (!s_chan[ch].bIgnoreLoop))
              s_chan[ch].pLoop=start-16;               // loop adress

             if(flags&1)                               // 1: stop/loop
              {
               // We play this block out first...
               //if(!(flags&2))                          // 1+2: do loop... otherwise: stop
               if(flags!=3 || s_chan[ch].pLoop==NULL)  // PETE: if we don't check exactly for 3, loop hang ups will happen (DQ4, for example)
                {                                      // and checking if pLoop is set avoids crashes, yeah
                 start = (unsigned char*)-1;
                }
               else
                {
                 start = s_chan[ch].pLoop;
                }
              }

             s_chan[ch].pCurr=start;                   // store values for next cycle
             s_chan[ch].s_1=s_1;
             s_chan[ch].s_2=s_2;

             if(bIRQReturn)                            // special return for "spu irq - wait for cpu action"
              {
               bIRQReturn=0;
               if(iUseTimer!=2)
                { 
                 DWORD dwWatchTime=timeGetTime_spu()+2500;

                 while(iSpuAsyncWait && !bEndThread && 
                       timeGetTime_spu()<dwWatchTime)
                     usleep(1000L);
                }
               else
                {
                 lastch=ch; 
                 lastns=ns;

                 return 0;
                }
              }

GOON: ;
            }

           fa=s_chan[ch].SB[s_chan[ch].iSBPos++];      // get sample data

           StoreInterpolationVal(ch,fa);               // store val for later interpolation

           s_chan[ch].spos -= 0x10000L;
          }

         if(s_chan[ch].bNoise)
              fa=iGetNoiseVal(ch);                     // get noise val
         else fa=iGetInterpolationVal(ch);             // get sample val

         s_chan[ch].sval = (MixADSR(ch) * fa) / 1023;  // mix adsr

         if(s_chan[ch].bFMod==2)                       // fmod freq channel
          iFMod[ns]=s_chan[ch].sval;                   // -> store 1T sample data, use that to do fmod on next channel
         else                                          // no fmod freq channel
          {
           //////////////////////////////////////////////
           // ok, left/right sound volume (psx volume goes from 0 ... 0x3fff)

           if(s_chan[ch].iMute) 
            s_chan[ch].sval=0;                         // debug mute
           else
            {
             SSumL[ns]+=(s_chan[ch].sval*s_chan[ch].iLeftVolume)/0x4000L;
             SSumR[ns]+=(s_chan[ch].sval*s_chan[ch].iRightVolume)/0x4000L;
            }

           //////////////////////////////////////////////
           // now let us store sound data for reverb    

           if(s_chan[ch].bRVBActive) StoreREVERB(ch,ns);
          }

         ////////////////////////////////////////////////
         // ok, go on until 1 ms data of this channel is collected

         ns++;
         s_chan[ch].spos += s_chan[ch].sinc;

        }
ENDX:   ;
      }
    }

  //---------------------------------------------------//
  //- here we have another 1 ms of sound data
  //---------------------------------------------------//
  // mix XA infos (if any)

  MixXA();
  
  ///////////////////////////////////////////////////////
  // mix all channels (including reverb) into one buffer

  if(iDisStereo)                                       // no stereo?
   {
    int dl, dr;
    for (ns = 0; ns < NSSIZE; ns++)
     {
      SSumL[ns] += MixREVERBLeft(ns);

      dl = SSumL[ns] / voldiv; SSumL[ns] = 0;
      if (dl < -32767) dl = -32767; if (dl > 32767) dl = 32767;

      SSumR[ns] += MixREVERBRight();

      dr = SSumR[ns] / voldiv; SSumR[ns] = 0;
      if (dr < -32767) dr = -32767; if (dr > 32767) dr = 32767;
      *pS++ = (dl + dr) / 2;
     }
   }
  else                                                 // stereo:
  for (ns = 0; ns < NSSIZE; ns++)
   {
    SSumL[ns] += MixREVERBLeft(ns);

    d = SSumL[ns] / voldiv; SSumL[ns] = 0;
    if (d < -32767) d = -32767; if (d > 32767) d = 32767;
    *pS++ = d;

    SSumR[ns] += MixREVERBRight();

    d = SSumR[ns] / voldiv; SSumR[ns] = 0;
    if(d < -32767) d = -32767; if(d > 32767) d = 32767;
    *pS++ = d;
   }

  //////////////////////////////////////////////////////                   
  // special irq handling in the decode buffers (0x0000-0x1000)
  // we know: 
  // the decode buffers are located in spu memory in the following way:
  // 0x0000-0x03ff  CD audio left
  // 0x0400-0x07ff  CD audio right
  // 0x0800-0x0bff  Voice 1
  // 0x0c00-0x0fff  Voice 3
  // and decoded data is 16 bit for one sample
  // we assume: 
  // even if voices 1/3 are off or no cd audio is playing, the internal
  // play positions will move on and wrap after 0x400 bytes.
  // Therefore: we just need a pointer from spumem+0 to spumem+3ff, and 
  // increase this pointer on each sample by 2 bytes. If this pointer
  // (or 0x400 offsets of this pointer) hits the spuirq address, we generate
  // an IRQ. Only problem: the "wait for cpu" option is kinda hard to do here
  // in some of Peops timer modes. So: we ignore this option here (for now).

  if(pMixIrq && irqCallback)
   {
    for(ns=0;ns<NSSIZE;ns++)
     {
      if((spuCtrl&0x40) && pSpuIrq && pSpuIrq<spuMemC+0x1000)                 
       {
        for(ch=0;ch<4;ch++)
         {
          if(pSpuIrq>=pMixIrq+(ch*0x400) && pSpuIrq<pMixIrq+(ch*0x400)+2)
           {irqCallback();s_chan[ch].iIrqDone=1;}
         }
       }
      pMixIrq+=2;if(pMixIrq>spuMemC+0x3ff) pMixIrq=spuMemC;
     }
   }

  InitREVERB();

  // feed the sound
  // wanna have around 1/60 sec (16.666 ms) updates
  if (iCycle++ > 16)
   {
    SoundFeedStreamData((unsigned char *)pSpuBuffer,
                        ((unsigned char *)pS) - ((unsigned char *)pSpuBuffer));
    pS = (short *)pSpuBuffer;
    iCycle = 0;
   }
 }

 // end of big main loop...

 bThreadEnded = 1;

 return 0;
}

// SPU ASYNC... even newer epsxe func
//  1 time every 'cycle' cycles... harhar

void SPU_async(uint32_t cycle)
{
 if(iSpuAsyncWait)
  {
   iSpuAsyncWait++;
   if(iSpuAsyncWait<=64) return;
   iSpuAsyncWait=0;
  }

 if(iUseTimer==2)                                      // special mode, only used in Linux by this spu (or if you enable the experimental Windows mode)
  {
   if(!bSpuInit) return;                               // -> no init, no call

   MAINThread(0);                                      // -> linux high-compat mode
  }
}

// SPU UPDATE... new epsxe func
//  1 time every 32 hsync lines
//  (312/32)x50 in pal
//  (262/32)x60 in ntsc

// since epsxe 1.5.2 (linux) uses SPUupdate, not SPUasync, I will
// leave that func in the linux port, until epsxe linux is using
// the async function as well

void SPU_update(void)
{
 SPU_async(0);
}

// XA AUDIO

void SPU_playADPCMchannel(xa_decode_t *xap)
{
 if(!xap)       return;
 if(!xap->freq) return;                                // no xa freq ? bye

 FeedXA(xap);                                          // call main XA feeder
}

// CDDA AUDIO
void SPU_playCDDAchannel(short *pcm, int nbytes)
{
 if (!pcm)      return;
 if (nbytes<=0) return;

 FeedCDDA((unsigned char *)pcm, nbytes);
}

// SETUPTIMER: init of certain buffers and threads/timers
void SetupTimer(void)
{
 memset(SSumR,0,NSSIZE*sizeof(int));                   // init some mixing buffers
 memset(SSumL,0,NSSIZE*sizeof(int));
 memset(iFMod,0,NSSIZE*sizeof(int));
 pS=(short *)pSpuBuffer;                               // setup soundbuffer pointer

 bEndThread=0;                                         // init thread vars
 bThreadEnded=0; 
 bSpuInit=1;                                           // flag: we are inited

 if(!iUseTimer)                                        // linux: use thread
  {
   pthread_create(&thread, NULL, MAINThread, NULL);
  }
}

// REMOVETIMER: kill threads/timers
void RemoveTimer(void)
{
 bEndThread=1;                                         // raise flag to end thread

 if(!iUseTimer)                                        // linux tread?
  {
   int i=0;
   while(!bThreadEnded && i<2000) {usleep(1000L);i++;} // -> wait until thread has ended
   if(thread!=(pthread_t)-1) {pthread_cancel(thread);thread=(pthread_t)-1;}  // -> cancel thread anyway
  }

 bThreadEnded=0;                                       // no more spu is running
 bSpuInit=0;
}

// SETUPSTREAMS: init most of the spu buffers
void SetupStreams(void)
{ 
 int i;

 pSpuBuffer=(unsigned char *)malloc(32768);            // alloc mixing buffer

 if(iUseReverb==1) i=88200*2;
 else              i=NSSIZE*2;

 sRVBStart = (int *)malloc(i*4);                       // alloc reverb buffer
 memset(sRVBStart,0,i*4);
 sRVBEnd  = sRVBStart + i;
 sRVBPlay = sRVBStart;

 XAStart =                                             // alloc xa buffer
  (uint32_t *)malloc(44100 * sizeof(uint32_t));
 XAEnd   = XAStart + 44100;
 XAPlay  = XAStart;
 XAFeed  = XAStart;

 CDDAStart =                                           // alloc cdda buffer
  (uint32_t *)malloc(16384 * sizeof(uint32_t));
 CDDAEnd   = CDDAStart + 16384;
 CDDAPlay  = CDDAStart;
 CDDAFeed  = CDDAStart + 1;

 for(i=0;i<MAXCHAN;i++)                                // loop sound channels
  {
// we don't use mutex sync... not needed, would only 
// slow us down:
//   s_chan[i].hMutex=CreateMutex(NULL,FALSE,NULL);
   s_chan[i].ADSRX.SustainLevel = 1024;                // -> init sustain
   s_chan[i].iMute=0;
   s_chan[i].iIrqDone=0;
   s_chan[i].pLoop=spuMemC;
   s_chan[i].pStart=spuMemC;
   s_chan[i].pCurr=spuMemC;
  }

  pMixIrq=spuMemC;                                     // enable decoded buffer irqs by setting the address
}

// REMOVESTREAMS: free most buffer
void RemoveStreams(void)
{ 
 free(pSpuBuffer);                                     // free mixing buffer
 pSpuBuffer = NULL;
 free(sRVBStart);                                      // free reverb buffer
 sRVBStart = NULL;
 free(XAStart);                                        // free XA buffer
 XAStart = NULL;
 free(CDDAStart);                                      // free CDDA buffer
 CDDAStart = NULL;
}

// INIT/EXIT STUFF

// SPUINIT: this func will be called first by the main emu
long SPU_init(void)
{
 spuMemC = (unsigned char *)spuMem;                    // just small setup
 memset((void *)&rvb, 0, sizeof(REVERBInfo));
 InitADSR();

 iVolume = 3;
 iReverbOff = -1;
 spuIrq = 0;
 spuAddr = 0xffffffff;
 bEndThread = 0;
 bThreadEnded = 0;
 spuMemC = (unsigned char *)spuMem;
 pMixIrq = 0;
 memset((void *)s_chan, 0, (MAXCHAN + 1) * sizeof(SPUCHAN));
 pSpuIrq = 0;
 iSPUIRQWait = 1;
 lastch = -1;

 ReadConfig();                                         // read user stuff
 SetupStreams();                                       // prepare streaming

 return 0;
}

// SPUOPEN: called by main emu after init
long SPU_open(void)
{
 if (bSPUIsOpen) return 0;                             // security for some stupid main emus

 SetupSound();                                         // setup sound (before init!)
 SetupTimer();                                         // timer for feeding data

 bSPUIsOpen = 1;

 return 0;
}

// SPUCLOSE: called before shutdown
long SPU_close(void)
{
 if (!bSPUIsOpen) return 0;                            // some security

 bSPUIsOpen = 0;                                       // no more open

 RemoveTimer();                                        // no more feeding
 RemoveSound();                                        // no more sound handling

 return 0;
}

// SPUSHUTDOWN: called by main emu on final exit
long SPU_shutdown(void)
{
 SPU_close();
 RemoveStreams();                                      // no more streaming

 return 0;
}

// SPUTEST: we don't test, we are always fine ;)
long SPU_test(void)
{
  printf("SDL should handle this for us\n");
  abort();
  return 0;
}

// SPUCONFIGURE: call config dialog
long SPU_configure(void)
{
  printf("SPU_configure: remove this crud\n");
  abort();
  return 0;
}

// SPUABOUT: show about window
void SPU_about(void)
{
  printf("SPU_about: remove this crud\n");
  abort();
}

// SETUP CALLBACKS
// this functions will be called once, 
// passes a callback that should be called on SPU-IRQ/cdda volume change
void SPU_registerCallback(void (*callback)(void))
{
 irqCallback = callback;
}

void SPU_registerCDDAVolume(void (*CDDAVcallback)(unsigned short,unsigned short))
{
 cddavCallback = CDDAVcallback;
}


////////////////////////////////////////////////////////////////////////
// READ DMA (one value)
////////////////////////////////////////////////////////////////////////

unsigned short SPU_readDMA(void)
{
 unsigned short s=spuMem[spuAddr>>1];
 spuAddr+=2;
 if(spuAddr>0x7ffff) spuAddr=0;

 iSpuAsyncWait=0;

 return s;
}

////////////////////////////////////////////////////////////////////////
// READ DMA (many values)
////////////////////////////////////////////////////////////////////////

void SPU_readDMAMem(unsigned short * pusPSXMem,int iSize)
{
 int i;

 for(i=0;i<iSize;i++)
  {
   *pusPSXMem++=spuMem[spuAddr>>1];                    // spu addr got by writeregister
   spuAddr+=2;                                         // inc spu addr
   if(spuAddr>0x7ffff) spuAddr=0;                      // wrap
  }

 iSpuAsyncWait=0;
}

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////

// to investigate: do sound data updates by writedma affect spu
// irqs? Will an irq be triggered, if new data is written to
// the memory irq address?

////////////////////////////////////////////////////////////////////////
// WRITE DMA (one value)
////////////////////////////////////////////////////////////////////////
  
void SPU_writeDMA(unsigned short val)
{
 spuMem[spuAddr>>1] = val;                             // spu addr got by writeregister

 spuAddr+=2;                                           // inc spu addr
 if(spuAddr>0x7ffff) spuAddr=0;                        // wrap

 iSpuAsyncWait=0;
}

////////////////////////////////////////////////////////////////////////
// WRITE DMA (many values)
////////////////////////////////////////////////////////////////////////

void SPU_writeDMAMem(unsigned short * pusPSXMem,int iSize)
{
 int i;

 for(i=0;i<iSize;i++)
  {
   spuMem[spuAddr>>1] = *pusPSXMem++;                  // spu addr got by writeregister
   spuAddr+=2;                                         // inc spu addr
   if(spuAddr>0x7ffff) spuAddr=0;                      // wrap
  }
 
 iSpuAsyncWait=0;
}

////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////
// freeze structs
////////////////////////////////////////////////////////////////////////

typedef struct
{
 unsigned short  spuIrq;
 uint32_t   pSpuIrq;
 uint32_t   spuAddr;
 uint32_t   dummy1;
 uint32_t   dummy2;
 uint32_t   dummy3;

 SPUCHAN  s_chan[MAXCHAN];   

} SPUOSSFreeze_t;

////////////////////////////////////////////////////////////////////////

void LoadStateV5(SPUFreeze_t * pF);                    // newest version
void LoadStateUnknown(SPUFreeze_t * pF);               // unknown format

////////////////////////////////////////////////////////////////////////
// SPUFREEZE: called by main emu on savestate load/save
////////////////////////////////////////////////////////////////////////

long SPU_freeze(uint32_t ulFreezeMode,SPUFreeze_t * pF)
{
 int i;
 SPUOSSFreeze_t * pFO;

 if(!pF) return 0;                                     // first check

 if(ulFreezeMode)                                      // info or save?
  {//--------------------------------------------------//
   if(ulFreezeMode==1)                                 
    memset(pF,0,sizeof(SPUFreeze_t)+sizeof(SPUOSSFreeze_t));

   strcpy(pF->PluginName,"PBOSS");
   pF->PluginVersion=5;
   pF->Size=sizeof(SPUFreeze_t)+sizeof(SPUOSSFreeze_t);

   if(ulFreezeMode==2) return 1;                       // info mode? ok, bye
                                                       // save mode:
   RemoveTimer();                                      // stop timer

   memcpy(pF->SPURam,spuMem,0x80000);                 // copy common infos
   memcpy(pF->SPUPorts,regArea,0x200);

   if(xapGlobal && XAPlay!=XAFeed)                     // some xa
    {
     pF->xa=*xapGlobal;     
    }
   else 
   memset(&pF->xa,0,sizeof(xa_decode_t));             // or clean xa

   pFO=(SPUOSSFreeze_t *)(pF+1);                       // store special stuff

   pFO->spuIrq=spuIrq;
   if(pSpuIrq)  pFO->pSpuIrq  = (unsigned long)pSpuIrq-(unsigned long)spuMemC;

   pFO->spuAddr=spuAddr;
   if(pFO->spuAddr==0) pFO->spuAddr=0xbaadf00d;

   for(i=0;i<MAXCHAN;i++)
    {
     memcpy((void *)&pFO->s_chan[i],(void *)&s_chan[i],sizeof(SPUCHAN));
     if(pFO->s_chan[i].pStart)
      pFO->s_chan[i].pStart-=(unsigned long)spuMemC;
     if(pFO->s_chan[i].pCurr)
      pFO->s_chan[i].pCurr-=(unsigned long)spuMemC;
     if(pFO->s_chan[i].pLoop)
      pFO->s_chan[i].pLoop-=(unsigned long)spuMemC;
    }

   SetupTimer();                                       // sound processing on again

   return 1;
   //--------------------------------------------------//
  }
                                                       
 if(ulFreezeMode!=0) return 0;                         // bad mode? bye

 RemoveTimer();                                        // we stop processing while doing the save!

 memcpy(spuMem,pF->SPURam,0x80000);                   // get ram
 memcpy(regArea,pF->SPUPorts,0x200);

 if(pF->xa.nsamples<=4032)                            // start xa again
  SPU_playADPCMchannel(&pF->xa);

 xapGlobal=0;

 if(!strcmp(pF->PluginName,"PBOSS") && pF->PluginVersion==5)
   LoadStateV5(pF);
 else LoadStateUnknown(pF);

 lastch = -1;

 // repair some globals
 for(i=0;i<=62;i+=2) SPU_writeRegister(H_Reverb+i,regArea[(H_Reverb+i-0xc00)>>1]);
 SPU_writeRegister(H_SPUReverbAddr,regArea[(H_SPUReverbAddr-0xc00)>>1]);
 SPU_writeRegister(H_SPUrvolL,regArea[(H_SPUrvolL-0xc00)>>1]);
 SPU_writeRegister(H_SPUrvolR,regArea[(H_SPUrvolR-0xc00)>>1]);

 SPU_writeRegister(H_SPUctrl,(unsigned short)(regArea[(H_SPUctrl-0xc00)>>1]|0x4000));
 SPU_writeRegister(H_SPUstat,regArea[(H_SPUstat-0xc00)>>1]);
 SPU_writeRegister(H_CDLeft,regArea[(H_CDLeft-0xc00)>>1]);
 SPU_writeRegister(H_CDRight,regArea[(H_CDRight-0xc00)>>1]);

 // fix to prevent new interpolations from crashing
 for(i=0;i<MAXCHAN;i++) s_chan[i].SB[28]=0;

 SetupTimer();                                         // start sound processing again

 return 1;
}

////////////////////////////////////////////////////////////////////////
void LoadStateV5(SPUFreeze_t * pF)
{
 int i;SPUOSSFreeze_t * pFO;

 pFO=(SPUOSSFreeze_t *)(pF+1);

 spuIrq = pFO->spuIrq;
 if(pFO->pSpuIrq) pSpuIrq = pFO->pSpuIrq+spuMemC; else pSpuIrq=NULL;

 if(pFO->spuAddr)
  {
   spuAddr = pFO->spuAddr;
   if (spuAddr == 0xbaadf00d) spuAddr = 0;
  }

 for(i=0;i<MAXCHAN;i++)
  {
   memcpy((void *)&s_chan[i],(void *)&pFO->s_chan[i],sizeof(SPUCHAN));

   s_chan[i].pStart+=(unsigned long)spuMemC;
   s_chan[i].pCurr+=(unsigned long)spuMemC;
   s_chan[i].pLoop+=(unsigned long)spuMemC;
   s_chan[i].iMute=0;
   s_chan[i].iIrqDone=0;
  }
}

////////////////////////////////////////////////////////////////////////
void LoadStateUnknown(SPUFreeze_t * pF)
{
 int i;

 for(i=0;i<MAXCHAN;i++)
  {
   s_chan[i].bOn=0;
   s_chan[i].bNew=0;
   s_chan[i].bStop=0;
   s_chan[i].ADSR.lVolume=0;
   s_chan[i].pLoop=spuMemC;
   s_chan[i].pStart=spuMemC;
   s_chan[i].pLoop=spuMemC;
   s_chan[i].iMute=0;
   s_chan[i].iIrqDone=0;
  }

 dwNewChannel=0;
 pSpuIrq=0;

 for(i=0;i<0xc0;i++)
  {
   SPU_writeRegister(0x1f801c00+i*2,regArea[i]);
  }
}


/*
// adsr time values (in ms) by James Higgs ... see the end of
// the adsr.c source for details

#define ATTACK_MS     514L
#define DECAYHALF_MS  292L
#define DECAY_MS      584L
#define SUSTAIN_MS    450L
#define RELEASE_MS    446L
*/

// we have a timebase of 1.020408f ms, not 1 ms... so adjust adsr defines
#define ATTACK_MS      494L
#define DECAYHALF_MS   286L
#define DECAY_MS       572L
#define SUSTAIN_MS     441L
#define RELEASE_MS     437L

////////////////////////////////////////////////////////////////////////
// WRITE REGISTERS: called by main emu
////////////////////////////////////////////////////////////////////////

void SPU_writeRegister(unsigned long reg, unsigned short val)
{
 const unsigned long r=reg&0xfff;
 regArea[(r-0xc00)>>1] = val;

 if(r>=0x0c00 && r<0x0d80)                             // some channel info?
  {
   int ch=(r>>4)-0xc0;                                 // calc channel
   switch(r&0x0f)
    {
     //------------------------------------------------// r volume
     case 0:                                           
       SetVolumeL((unsigned char)ch,val);
       break;
     //------------------------------------------------// l volume
     case 2:                                           
       SetVolumeR((unsigned char)ch,val);
       break;
     //------------------------------------------------// pitch
     case 4:                                           
       SetPitch(ch,val);
       break;
     //------------------------------------------------// start
     case 6:      
       s_chan[ch].pStart=spuMemC+((unsigned long) val<<3);
       break;
     //------------------------------------------------// level with pre-calcs
     case 8:
       {
        const unsigned long lval=val;unsigned long lx;
        //---------------------------------------------//
        s_chan[ch].ADSRX.AttackModeExp=(lval&0x8000)?1:0; 
        s_chan[ch].ADSRX.AttackRate=(lval>>8) & 0x007f;
        s_chan[ch].ADSRX.DecayRate=(lval>>4) & 0x000f;
        s_chan[ch].ADSRX.SustainLevel=lval & 0x000f;
        //---------------------------------------------//
        if(!iDebugMode) break;
        //---------------------------------------------// stuff below is only for debug mode

        s_chan[ch].ADSR.AttackModeExp=(lval&0x8000)?1:0;        //0x007f

        lx=(((lval>>8) & 0x007f)>>2);                  // attack time to run from 0 to 100% volume
        lx=min(31,lx);                                 // no overflow on shift!
        if(lx) 
         { 
          lx = (1<<lx);
          if(lx<2147483) lx=(lx*ATTACK_MS)/10000L;     // another overflow check
          else           lx=(lx/10000L)*ATTACK_MS;
          if(!lx) lx=1;
         }
        s_chan[ch].ADSR.AttackTime=lx;                

        s_chan[ch].ADSR.SustainLevel=                 // our adsr vol runs from 0 to 1024, so scale the sustain level
         (1024*((lval) & 0x000f))/15;

        lx=(lval>>4) & 0x000f;                         // decay:
        if(lx)                                         // our const decay value is time it takes from 100% to 0% of volume
         {
          lx = ((1<<(lx))*DECAY_MS)/10000L;
          if(!lx) lx=1;
         }
        s_chan[ch].ADSR.DecayTime =                   // so calc how long does it take to run from 100% to the wanted sus level
         (lx*(1024-s_chan[ch].ADSR.SustainLevel))/1024;
       }
      break;
     //------------------------------------------------// adsr times with pre-calcs
     case 10:
      {
       const unsigned long lval=val;unsigned long lx;

       //----------------------------------------------//
       s_chan[ch].ADSRX.SustainModeExp = (lval&0x8000)?1:0;
       s_chan[ch].ADSRX.SustainIncrease= (lval&0x4000)?0:1;
       s_chan[ch].ADSRX.SustainRate = (lval>>6) & 0x007f;
       s_chan[ch].ADSRX.ReleaseModeExp = (lval&0x0020)?1:0;
       s_chan[ch].ADSRX.ReleaseRate = lval & 0x001f;
       //----------------------------------------------//
       if(!iDebugMode) break;
       //----------------------------------------------// stuff below is only for debug mode

       s_chan[ch].ADSR.SustainModeExp = (lval&0x8000)?1:0;
       s_chan[ch].ADSR.ReleaseModeExp = (lval&0x0020)?1:0;
                   
       lx=((((lval>>6) & 0x007f)>>2));                 // sustain time... often very high
       lx=min(31,lx);                                  // values are used to hold the volume
       if(lx)                                          // until a sound stop occurs
        {                                              // the highest value we reach (due to 
         lx = (1<<lx);                                 // overflow checking) is: 
         if(lx<2147483) lx=(lx*SUSTAIN_MS)/10000L;     // 94704 seconds = 1578 minutes = 26 hours... 
         else           lx=(lx/10000L)*SUSTAIN_MS;     // should be enuff... if the stop doesn't 
         if(!lx) lx=1;                                 // come in this time span, I don't care :)
        }
       s_chan[ch].ADSR.SustainTime = lx;

       lx=(lval & 0x001f);
       s_chan[ch].ADSR.ReleaseVal     =lx;
       if(lx)                                          // release time from 100% to 0%
        {                                              // note: the release time will be
         lx = (1<<lx);                                 // adjusted when a stop is coming,
         if(lx<2147483) lx=(lx*RELEASE_MS)/10000L;     // so at this time the adsr vol will 
         else           lx=(lx/10000L)*RELEASE_MS;     // run from (current volume) to 0%
         if(!lx) lx=1;
        }
       s_chan[ch].ADSR.ReleaseTime=lx;

       if(lval & 0x4000)                               // add/dec flag
            s_chan[ch].ADSR.SustainModeDec=-1;
       else s_chan[ch].ADSR.SustainModeDec=1;
      }
     break;
     //------------------------------------------------// adsr volume... mmm have to investigate this
     case 12:
       break;
     //------------------------------------------------//
     case 14:                                          // loop?
       //WaitForSingleObject(s_chan[ch].hMutex,2000);        // -> no multithread fuckups
       s_chan[ch].pLoop=spuMemC+((unsigned long) val<<3);
       s_chan[ch].bIgnoreLoop=1;
       //ReleaseMutex(s_chan[ch].hMutex);                    // -> oki, on with the thread
       break;
     //------------------------------------------------//
    }
   iSpuAsyncWait=0;
   return;
  }

 switch(r)
   {
    //-------------------------------------------------//
    case H_SPUaddr:
      spuAddr = (unsigned long) val<<3;
      break;
    //-------------------------------------------------//
    case H_SPUdata:
      spuMem[spuAddr>>1] = val;
      spuAddr+=2;
      if(spuAddr>0x7ffff) spuAddr=0;
      break;
    //-------------------------------------------------//
    case H_SPUctrl:
      spuCtrl=val;
      break;
    //-------------------------------------------------//
    case H_SPUstat:
      spuStat=val & 0xf800;
      break;
    //-------------------------------------------------//
    case H_SPUReverbAddr:
      if(val==0xFFFF || val<=0x200)
       {rvb.StartAddr=rvb.CurrAddr=0;}
      else
       {
        const long iv=(unsigned long)val<<2;
        if(rvb.StartAddr!=iv)
         {
          rvb.StartAddr=(unsigned long)val<<2;
          rvb.CurrAddr=rvb.StartAddr;
         }
       }
      break;
    //-------------------------------------------------//
    case H_SPUirqAddr:
      spuIrq = val;
      pSpuIrq=spuMemC+((unsigned long) val<<3);
      break;
    //-------------------------------------------------//
    case H_SPUrvolL:
      rvb.VolLeft=val;
      break;
    //-------------------------------------------------//
    case H_SPUrvolR:
      rvb.VolRight=val;
      break;
    //-------------------------------------------------//

/*
    case H_ExtLeft:
     //auxprintf("EL %d\n",val);
      break;
    //-------------------------------------------------//
    case H_ExtRight:
     //auxprintf("ER %d\n",val);
      break;
    //-------------------------------------------------//
    case H_SPUmvolL:
     //auxprintf("ML %d\n",val);
      break;
    //-------------------------------------------------//
    case H_SPUmvolR:
     //auxprintf("MR %d\n",val);
      break;
    //-------------------------------------------------//
    case H_SPUMute1:
     //auxprintf("M0 %04x\n",val);
      break;
    //-------------------------------------------------//
    case H_SPUMute2:
     //auxprintf("M1 %04x\n",val);
      break;
*/
    //-------------------------------------------------//
    case H_SPUon1:
      SoundOn(0,16,val);
      break;
    //-------------------------------------------------//
     case H_SPUon2:
      SoundOn(16,24,val);
      break;
    //-------------------------------------------------//
    case H_SPUoff1:
      SoundOff(0,16,val);
      break;
    //-------------------------------------------------//
    case H_SPUoff2:
      SoundOff(16,24,val);
      break;
    //-------------------------------------------------//
    case H_CDLeft:
      iLeftXAVol=val  & 0x7fff;
      if(cddavCallback) cddavCallback(0,val);
      break;
    case H_CDRight:
      iRightXAVol=val & 0x7fff;
      if(cddavCallback) cddavCallback(1,val);
      break;
    //-------------------------------------------------//
    case H_FMod1:
      FModOn(0,16,val);
      break;
    //-------------------------------------------------//
    case H_FMod2:
      FModOn(16,24,val);
      break;
    //-------------------------------------------------//
    case H_Noise1:
      NoiseOn(0,16,val);
      break;
    //-------------------------------------------------//
    case H_Noise2:
      NoiseOn(16,24,val);
      break;
    //-------------------------------------------------//
    case H_RVBon1:
      ReverbOn(0,16,val);
      break;
    //-------------------------------------------------//
    case H_RVBon2:
      ReverbOn(16,24,val);
      break;
    //-------------------------------------------------//
    case H_Reverb+0:

      rvb.FB_SRC_A=val;

      // OK, here's the fake REVERB stuff...
      // depending on effect we do more or less delay and repeats... bah
      // still... better than nothing :)

      SetREVERB(val);
      break;


    case H_Reverb+2   : rvb.FB_SRC_B=(short)val;       break;
    case H_Reverb+4   : rvb.IIR_ALPHA=(short)val;      break;
    case H_Reverb+6   : rvb.ACC_COEF_A=(short)val;     break;
    case H_Reverb+8   : rvb.ACC_COEF_B=(short)val;     break;
    case H_Reverb+10  : rvb.ACC_COEF_C=(short)val;     break;
    case H_Reverb+12  : rvb.ACC_COEF_D=(short)val;     break;
    case H_Reverb+14  : rvb.IIR_COEF=(short)val;       break;
    case H_Reverb+16  : rvb.FB_ALPHA=(short)val;       break;
    case H_Reverb+18  : rvb.FB_X=(short)val;           break;
    case H_Reverb+20  : rvb.IIR_DEST_A0=(short)val;    break;
    case H_Reverb+22  : rvb.IIR_DEST_A1=(short)val;    break;
    case H_Reverb+24  : rvb.ACC_SRC_A0=(short)val;     break;
    case H_Reverb+26  : rvb.ACC_SRC_A1=(short)val;     break;
    case H_Reverb+28  : rvb.ACC_SRC_B0=(short)val;     break;
    case H_Reverb+30  : rvb.ACC_SRC_B1=(short)val;     break;
    case H_Reverb+32  : rvb.IIR_SRC_A0=(short)val;     break;
    case H_Reverb+34  : rvb.IIR_SRC_A1=(short)val;     break;
    case H_Reverb+36  : rvb.IIR_DEST_B0=(short)val;    break;
    case H_Reverb+38  : rvb.IIR_DEST_B1=(short)val;    break;
    case H_Reverb+40  : rvb.ACC_SRC_C0=(short)val;     break;
    case H_Reverb+42  : rvb.ACC_SRC_C1=(short)val;     break;
    case H_Reverb+44  : rvb.ACC_SRC_D0=(short)val;     break;
    case H_Reverb+46  : rvb.ACC_SRC_D1=(short)val;     break;
    case H_Reverb+48  : rvb.IIR_SRC_B1=(short)val;     break;
    case H_Reverb+50  : rvb.IIR_SRC_B0=(short)val;     break;
    case H_Reverb+52  : rvb.MIX_DEST_A0=(short)val;    break;
    case H_Reverb+54  : rvb.MIX_DEST_A1=(short)val;    break;
    case H_Reverb+56  : rvb.MIX_DEST_B0=(short)val;    break;
    case H_Reverb+58  : rvb.MIX_DEST_B1=(short)val;    break;
    case H_Reverb+60  : rvb.IN_COEF_L=(short)val;      break;
    case H_Reverb+62  : rvb.IN_COEF_R=(short)val;      break;
   }

 iSpuAsyncWait=0;
}

////////////////////////////////////////////////////////////////////////
// READ REGISTER: called by main emu
////////////////////////////////////////////////////////////////////////

unsigned short SPU_readRegister(unsigned long reg)
{
 const unsigned long r=reg&0xfff;
        
 iSpuAsyncWait=0;

 if(r>=0x0c00 && r<0x0d80)
  {
   switch(r&0x0f)
    {
     case 12:                                          // get adsr vol
      {
       const int ch=(r>>4)-0xc0;
       if(s_chan[ch].bNew) return 1;                   // we are started, but not processed? return 1
       if(s_chan[ch].ADSRX.lVolume &&                  // same here... we haven't decoded one sample yet, so no envelope yet. return 1 as well
          !s_chan[ch].ADSRX.EnvelopeVol)                   
        return 1;
       return (unsigned short)(s_chan[ch].ADSRX.EnvelopeVol>>16);
      }

     case 14:                                          // get loop address
      {
       const int ch=(r>>4)-0xc0;
       if(s_chan[ch].pLoop==NULL) return 0;
       return (unsigned short)((s_chan[ch].pLoop-spuMemC)>>3);
      }
    }
  }

 switch(r)
  {
    case H_SPUctrl:
     return spuCtrl;

    case H_SPUstat:
     return spuStat;
        
    case H_SPUaddr:
     return (unsigned short)(spuAddr>>3);

    case H_SPUdata:
     {
      unsigned short s=spuMem[spuAddr>>1];
      spuAddr+=2;
      if(spuAddr>0x7ffff) spuAddr=0;
      return s;
     }

    case H_SPUirqAddr:
     return spuIrq;

    //case H_SPUIsOn1:
    // return IsSoundOn(0,16);

    //case H_SPUIsOn2:
    // return IsSoundOn(16,24);
 
  }

 return regArea[(r-0xc00)>>1];
}
 
////////////////////////////////////////////////////////////////////////
// SOUND ON register write
////////////////////////////////////////////////////////////////////////

void SoundOn(int start,int end,unsigned short val)     // SOUND ON PSX COMAND
{
 int ch;

 for(ch=start;ch<end;ch++,val>>=1)                     // loop channels
  {
   if((val&1) && s_chan[ch].pStart)                    // mmm... start has to be set before key on !?!
    {
     s_chan[ch].bIgnoreLoop=0;
     s_chan[ch].bNew=1;
     dwNewChannel|=(1<<ch);                            // bitfield for faster testing
    }
  }
}

////////////////////////////////////////////////////////////////////////
// SOUND OFF register write
////////////////////////////////////////////////////////////////////////

void SoundOff(int start,int end,unsigned short val)    // SOUND OFF PSX COMMAND
{
 int ch;
 for(ch=start;ch<end;ch++,val>>=1)                     // loop channels
  {
   if(val&1)                                           // && s_chan[i].bOn)  mmm...
    {
     s_chan[ch].bStop=1;
    }                                                  
  }
}

////////////////////////////////////////////////////////////////////////
// FMOD register write
////////////////////////////////////////////////////////////////////////

void FModOn(int start,int end,unsigned short val)      // FMOD ON PSX COMMAND
{
 int ch;

 for(ch=start;ch<end;ch++,val>>=1)                     // loop channels
  {
   if(val&1)                                           // -> fmod on/off
    {
     if(ch>0) 
      {
       s_chan[ch].bFMod=1;                             // --> sound channel
       s_chan[ch-1].bFMod=2;                           // --> freq channel
      }
    }
   else
    {
     s_chan[ch].bFMod=0;                               // --> turn off fmod
    }
  }
}

////////////////////////////////////////////////////////////////////////
// NOISE register write
////////////////////////////////////////////////////////////////////////

void NoiseOn(int start,int end,unsigned short val)     // NOISE ON PSX COMMAND
{
 int ch;

 for(ch=start;ch<end;ch++,val>>=1)                     // loop channels
  {
   if(val&1)                                           // -> noise on/off
    {
     s_chan[ch].bNoise=1;
    }
   else 
    {
     s_chan[ch].bNoise=0;
    }
  }
}

////////////////////////////////////////////////////////////////////////
// LEFT VOLUME register write
////////////////////////////////////////////////////////////////////////

// please note: sweep and phase invert are wrong... but I've never seen
// them used

void SetVolumeL(unsigned char ch,short vol)            // LEFT VOLUME
{
 s_chan[ch].iLeftVolRaw=vol;

 if(vol&0x8000)                                        // sweep?
  {
   short sInc=1;                                       // -> sweep up?
   if(vol&0x2000) sInc=-1;                             // -> or down?
   if(vol&0x1000) vol^=0xffff;                         // -> mmm... phase inverted? have to investigate this
   vol=((vol&0x7f)+1)/2;                               // -> sweep: 0..127 -> 0..64
   vol+=vol/(2*sInc);                                  // -> HACK: we don't sweep right now, so we just raise/lower the volume by the half!
   vol*=128;
  }
 else                                                  // no sweep:
  {
   if(vol&0x4000)                                      // -> mmm... phase inverted? have to investigate this
    //vol^=0xffff;
    vol=0x3fff-(vol&0x3fff);
  }

 vol&=0x3fff;
 s_chan[ch].iLeftVolume=vol;                           // store volume
}

////////////////////////////////////////////////////////////////////////
// RIGHT VOLUME register write
////////////////////////////////////////////////////////////////////////

void SetVolumeR(unsigned char ch,short vol)            // RIGHT VOLUME
{
 s_chan[ch].iRightVolRaw=vol;

 if(vol&0x8000)                                        // comments... see above :)
  {
   short sInc=1;
   if(vol&0x2000) sInc=-1;
   if(vol&0x1000) vol^=0xffff;
   vol=((vol&0x7f)+1)/2;        
   vol+=vol/(2*sInc);
   vol*=128;
  }
 else            
  {
   if(vol&0x4000) //vol=vol^=0xffff;
    vol=0x3fff-(vol&0x3fff);
  }

 vol&=0x3fff;

 s_chan[ch].iRightVolume=vol;
}

////////////////////////////////////////////////////////////////////////
// PITCH register write
////////////////////////////////////////////////////////////////////////

void SetPitch(int ch,unsigned short val)               // SET PITCH
{
 int NP;
 if(val>0x3fff) NP=0x3fff;                             // get pitch val
 else           NP=val;

 s_chan[ch].iRawPitch=NP;

 NP=(44100L*NP)/4096L;                                 // calc frequency
 if(NP<1) NP=1;                                        // some security
 s_chan[ch].iActFreq=NP;                               // store frequency
}

////////////////////////////////////////////////////////////////////////
// REVERB register write
////////////////////////////////////////////////////////////////////////

void ReverbOn(int start,int end,unsigned short val)    // REVERB ON PSX COMMAND
{
 int ch;

 for(ch=start;ch<end;ch++,val>>=1)                     // loop channels
  {
   if(val&1)                                           // -> reverb on/off
    {
     s_chan[ch].bReverb=1;
    }
   else 
    {
     s_chan[ch].bReverb=0;
    }
  }
}


#include <SDL.h>

#define BUFFER_SIZE		22050

static short			*pSndBuffer = NULL;
static int				iBufSize = 0;
static volatile int	iReadPos = 0, iWritePos = 0;

static void SOUND_FillAudio(void *unused, Uint8 *stream, int len) {
	short *p = (short *)stream;

	len /= sizeof(short);

	while (iReadPos != iWritePos && len > 0) {
		*p++ = pSndBuffer[iReadPos++];
		if (iReadPos >= iBufSize) iReadPos = 0;
		--len;
	}

	// Fill remaining space with zero
	while (len > 0) {
		*p++ = 0;
		--len;
	}
}

void SetupSound(void) {
	SDL_AudioSpec				spec;

	if (pSndBuffer != NULL) return;

	spec.freq = 44100;
	spec.format = AUDIO_S16SYS;
	spec.channels = iDisStereo ? 1 : 2;
	spec.samples = 512;
	spec.callback = SOUND_FillAudio;

	if (SDL_OpenAudio(&spec, NULL) < 0) {
    printf("Failed to open SDL audio\n");
		return;
	}

	iBufSize = BUFFER_SIZE;
	if (iDisStereo) iBufSize /= 2;

	pSndBuffer = (short *)malloc(iBufSize * sizeof(short));
	if (pSndBuffer == NULL) {
		SDL_CloseAudio();
		return;
	}

	iReadPos = 0;
	iWritePos = 0;

	SDL_PauseAudio(0);
}

void RemoveSound(void) {
	if (pSndBuffer == NULL) return;

	SDL_CloseAudio();

	free(pSndBuffer);
	pSndBuffer = NULL;
}

unsigned long SoundGetBytesBuffered(void) {
	int size;

	if (pSndBuffer == NULL) return SOUNDSIZE;

	size = iReadPos - iWritePos;
	if (size <= 0) size += iBufSize;

	if (size < iBufSize / 2) return SOUNDSIZE;

	return 0;
}

void SoundFeedStreamData(unsigned char *pSound, long lBytes) {
	short *p = (short *)pSound;

	if (pSndBuffer == NULL) return;

	while (lBytes > 0) {
		if (((iWritePos + 1) % iBufSize) == iReadPos) break;

		pSndBuffer[iWritePos] = *p++;

		++iWritePos;
		if (iWritePos >= iBufSize) iWritePos = 0;

		lBytes -= sizeof(short);
	}
}



#if 0
void SetupSound(void) {}
void RemoveSound(void) {}
unsigned long SoundGetBytesBuffered(void) {return 0;}
void SoundFeedStreamData(unsigned char* pSound,long lBytes) {}
#endif


void SPUirq(void) {
	psxHu32ref(0x1070) |= SWAPu32(0x200);
}



