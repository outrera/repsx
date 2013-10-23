#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <setjmp.h>
#include <SDL/SDL.h>

#include "common.h"
#include "plugins.h"
#include "getopt.h"
#include "sdl_text.h"

#define MIN_W 320
#define MIN_H 240

//#include <dlfcn.h>
//#import <sys/param.h>
//#include "sio.h"



SDL_Surface *screen;

char SdlKeys[1<<16];

static jmp_buf restartJmp;

static int Paused = 0;
static int Done = 0;

static int Fullscreen = 0;
static int Recompile = 0;
static int Scale = 1;
static int ShowFPS = 0;

enum {RunImage,PrintUsage,PrintVersion};
static int Action = RunImage;

static char *StateToLoad;
static char *CDs[1024];
static int NCDs = 0;

static int T0 = 0, frames = 0;
static float fps = 0;


static int StartTime;
static int LimitFPS;

typedef struct {
 char **Items;
 int Cursor;
 int NItems;
 void *H;
} menu;

static menu *Menu;
static int MenuDepth=-1;
static menu MenuStack[128];

static void menuPush(char **m, void *Handler) {
  Menu = &MenuStack[++MenuDepth];
  Menu->Items = m;
  Menu->Cursor = 0;
  Menu->H = Handler;
  for (Menu->NItems=0; m[Menu->NItems]; Menu->NItems++);
  LimitFPS = 14;
}

static void menuPop() {
  if (MenuDepth != -1) Menu = &MenuStack[--MenuDepth];
  if (MenuDepth == -1) LimitFPS = 0;
}


static void usage() {
  printf("Usage:"); 
  printf("  repsx [Options...] [any number of ISO/IMG/BIN images]\n");
  printf("\n");
  printf("Options:\n");
  //printf("  -f, --fullscreen             Full screen\n");
  printf("  -h, --help                   Print this help\n");
  printf("  -v, --version                Print version info\n");
  //printf("  -i, --patch=<patch.ppf>      Apply given patch\n");
  //printf("  -r, --recompile              Enable recompiler opitmization\n");
}


static void version() {
  printf("repsx-%s by SNV\n", PACKAGE_VERSION);
}


void lock_surface(SDL_Surface *surface) {
	if(SDL_MUSTLOCK(surface))
		assert(SDL_LockSurface(surface) >= 0);
}

void unlock_surface(SDL_Surface *surface) {
	if(SDL_MUSTLOCK(surface))
		SDL_UnlockSurface(surface);
}




void resize(int w, int h) {
  if (w<MIN_W) w = MIN_W;
  if (h<MIN_H) h = MIN_H;

  // maintain 4/3 aspect ration
	if (w*3 < h*4) w = h*4.0/3;
  else h = w*3.0/4;

	screen = SDL_SetVideoMode(w, h, 32,
		SDL_ANYFORMAT|SDL_HWSURFACE/*|SDL_DOUBLEBUF*/|SDL_VIDEORESIZE);
  if (!screen) {
    printf("Failed to set video mode\n");
  }
}




//called by gpu.c
uint8_t *screenBlitStart(int *w, int *h) {
  StartTime = SDL_GetTicks();
  lock_surface(screen);
  *w = screen->w;
  *h = screen->h;
  return screen->pixels;
}

void screenBlitEnd() {
  frames++;
  int t = SDL_GetTicks();
  if (t - T0 >= 1000) {
    float seconds = (t - T0) / 1000.0;
    fps = frames / seconds;
    T0 = t;
    frames = 0;
  }

  if (LimitFPS && t-StartTime > 0) {
    float ft = 1.000/LimitFPS; //frame time
    float pt = (t-StartTime)/1000.0; //passed time
    int wt = (int)((ft-pt)*1000000.0);
    if (wt > 0) usleep(wt);
  }

  if (ShowFPS) sdlText(screen, 0, 0, "FPS:%.0f\n", fps);
  unlock_surface(screen);
  SDL_Flip(screen);
}


void restart() {
  longjmp(restartJmp, 0);
}

void SaveStateN(int N) {
  char Tmp[PATHLEN];
  sprintf(Tmp, "%s/saves/%s", WorkDir, GameTitle);
  makePath(Tmp);

  sprintf(Tmp, "%s/saves/%s/%02d.sav", WorkDir, GameTitle, N);
  printf("Saving %s\n", Tmp);
  SaveState(Tmp);
}

void LoadStateN(int N) {
  char Tmp[PATHLEN];
  FILE *F;
  sprintf(Tmp, "%s/saves/%s/%02d.sav", WorkDir, GameTitle, N);

  F = fopen(Tmp, "rb");
  if (!F) {
    printf("Cant open %s\n", Tmp);
    return;
  }
  fclose(F);

  SaveStateN(8); //backup in case user hits F-key by accident

  printf("Loading %s\n", Tmp);
  StateToLoad = strdup(Tmp);
  restart();
}


static char *MainMenu[] = {
  //"Save",
  //"Load",
  "Debug",
  "Input",
  "Options",
  //"Change CD"
  //"Cheat"
  "----------",
  "Quit",
  0
};

static char *InputMenu[] = {
  "Start", "Select",
  "Up", "Down", "Left", "Right",
  "Triangle", "Circle", "Cross", "Square",
  "L1", "L2", "R1", "R2",
  0
};


static char *InputSetMenu[] = {"???",0};

static void MainMenuHandler(char *N, int Key);

static void InputSetHandler(char *N, int Key) {
  int c = MenuStack[MenuDepth-1].Cursor;
  if (Key == SDLK_ESCAPE) return;
  PAD_SetMapping(0, InputMenu[c], Key);
  SdlKeys[Key] = 0;
  menuPop();
  menuPop();
  MainMenuHandler("Input", SDLK_RETURN); 
  Menu->Cursor = c;
}

static void InputMenuHandler(char *N, int Key) {
  if (Key == SDLK_ESCAPE) SavePADConfig();
  if (Key != SDLK_RETURN) return;
  menuPush(InputSetMenu, InputSetHandler);
}

static char *OptionsMenu[] = {
  "Scaler",
  //"Bios",
  0
};


static void OptionsMenuHandler(char *N, int Key) {
  int i;
  int c = Menu->Cursor;
  if (Key == SDLK_ESCAPE) {
    GPU_WriteConfig();
    return;
  }

  if (!strcmp(OptionsMenu[c], "Scaler")) {
    char *S = GPU_GetScaler();
    if (Key == SDLK_LEFT) {
      for (i=0; Scalers[i].Name; i++) if (!strcmp(Scalers[i].Name, S)) {
        if (i) GPU_SetScaler(Scalers[i-1].Name);
        break;
      }
    } else if (Key == SDLK_RIGHT) {
      for (i=0; Scalers[i].Name; i++) if (!strcmp(Scalers[i].Name, S)) {
        if (Scalers[i+1].Name) GPU_SetScaler(Scalers[i+1].Name);
        break;
      }
    } else {
      return;
    }
    menuPop();
    MainMenuHandler("Options", SDLK_RETURN);
    Menu->Cursor = c;
  }
  //menuPush(InputSetMenu, InputSetHandler);
}

static char *DebugMenu[] = {
  "Breakpoint",
  "Stack",
  "Dump",
  0
};


static void wbHandler() {
  printf("WB: PC=0x%08X\n", psxRegs.pc-4);
}

static void rbHandler() {
  printf("RB: PC=0x%08X\n", psxRegs.pc-4);
}

static void viewRAM(uint8_t *in, uint32_t l, int InitW, int InitBits, int ExitKey) {
  static uint8_t *m=0;
  static int base, sw, sb, sx, sy;
  int i, c, w, h, sh, x,y, Shift=8, Alt=0, WB, RB, bx=-1, by=-1, mb;
  uint8_t *p, *d, *s, *e, Pal[3*256];
  SDL_Event event;

  if (in!=m) {
    base=0;
    sb=InitBits;
    sw=InitW*8/sb;
    sx=0;
    sy=0;
  }
  m = in;

  memset(Pal, 0, 256*3);
  for(y=0;y<16;y++) for(x=0;x<16;x++) {
    i = (y*16+x)*3;
    Pal[i+0] = (x<<4)|y;
    Pal[i+1] = (y<<4)|x;
    Pal[i+2] = x*y;
  }

  //setWB(0x800DB47C,wbHandler);

  while(!Done) {
    WB=getWB();
    RB=getRB();

    LimitFPS = 14;
    p = screenBlitStart(&w, &h);
    memset(p, 0, w*h*4);
    sdlText(screen,0,0,"ARROWS+SHIFT+ALT = move, +/- = width, Z = mode");
    sdlText(screen,0,16,"%08X: b=%06X, M=%d, w=%d, x=%d, y=%d WB=%06X RB=%06X"
           , 0x80000000 + base + (sy*sw + sx)*sb/8
           , base, sb, sw, sx, sy, WB, RB);
    sh = l*sb/8/sw;
    e = m+l;
    for (i=0, y=32; y<screen->h; y++, i++) {
      d = screen->pixels + y*screen->w*4;
      s = m + base + ((sy+i)*sw + sx)*sb/8;
      for (x=0; x<screen->w; x++) {
        if (s >= e || x>=sw-sx) continue;
        if (y == by && x == bx) {
          if (mb==SDL_BUTTON_LEFT) WB = 0x80000000 + s-m;
          else RB = 0x80000000 + s-m;
          bx = -1;
          by = -1;
        }
        if (sb == 8) {
          c = *s++;
          d[1] = Pal[c*3+0];
          d[2] = Pal[c*3+1];
          d[3] = Pal[c*3+2];
          d += 4;
        } else if (sb == 4) {
          if (x&1) c = *s++>>4;
          else c = *s&0xf;
          c = c<<4;
          d[1] = c;
          d[2] = c;
          d[3] = c;
          d += 4;
        } else if (sb == 16) {
          c = *(u16*)s;
          s += 2;
          d[1] = (c&0x1f)<<3;
          d[2] = ((c>>5)&0x1f)<<3;
          d[3] = ((c>>10)&0x1f)<<3;
          d += 4;
        }
      }
    }
    screenBlitEnd();
    LimitFPS = 0;
    
    setWB(WB,wbHandler);
    setRB(RB,rbHandler);

    while(SDL_PollEvent(&event)) {
      switch(event.type) {
      case SDL_VIDEORESIZE: resize(event.resize.w, event.resize.h); break;
      case SDL_QUIT: Done=1; break;
      case SDL_KEYDOWN: SdlKeys[event.key.keysym.sym] = 1; 
                        Shift = (event.key.keysym.mod & KMOD_SHIFT) ? 64 : 8;
                        Alt = event.key.keysym.mod & (KMOD_LALT|KMOD_RALT);
                        break;
      case SDL_MOUSEBUTTONDOWN:
        if (SdlKeys[SDLK_LSHIFT]||SdlKeys[SDLK_RSHIFT]) { //mice+shift sets breakpoint
          mb = event.button.button;
          bx = event.button.x;
          by = event.button.y;
        }
        break;
      case SDL_KEYUP: SdlKeys[event.key.keysym.sym] = 0; break;
      }
    }


    if (Alt) {
      if (SdlKeys[SDLK_LEFT]) base -= Shift==8?1:16;
      if (SdlKeys[SDLK_RIGHT]) base += Shift==8?1:16;
      if (base < 0) base = 0;
    } else {
      if (SdlKeys[SDLK_UP]) sy-=Shift;
      if (SdlKeys[SDLK_DOWN]) sy+=Shift;
      if (SdlKeys[SDLK_LEFT]) sx-=Shift;
      if (SdlKeys[SDLK_RIGHT]) sx+=Shift;
      if (sx < 0) sx = 0;
      if (sy < 0) sy = 0;

      i = sw;
      if (SdlKeys[SDLK_EQUALS]) sw += Shift==8?1:16;
      if (SdlKeys[SDLK_MINUS]) sw -= Shift==8?1:16;
      if (sw < 8) sw = 8;
      sy = (sy*i+sw-1)/sw;
    }
    if (sy > (l-base)*8/sb/sw) sy=(l-base)*8/sb/sw;

    if (SdlKeys[SDLK_ESCAPE]) {
      SdlKeys[SDLK_ESCAPE] = 0;
      return;
    }

    if (SdlKeys[ExitKey]) {
      SdlKeys[ExitKey] = 0;
      return;
    }

    if (SdlKeys[SDLK_z]) {
      SdlKeys[SDLK_z] = 0;
      c = sb;
      if (sb == 8) sb = 16;
      else if (sb == 16) sb = 4;
      else sb = 8;
      sw = sw*c/sb;
    }
  }

}

static void DebugMenuHandler(char *N, int Key) {
  if (Key != SDLK_RETURN) return;


  if (!strcmp("Dump", N)) {
    GPU_dumpVRAM();
    menuPop();
  }
}


static void MainMenuHandler(char *N, int Key) {
  int i;
  char **M;
  if (Key != SDLK_RETURN) return;
  if (!strcmp("Quit", N)) {
    Done = 1;
    return;
  }
  
  if (!strcmp("Input", N)) {
    i = 0;
    M = (char**)malloc(sizeof(char*)*64);
    for (i=0; InputMenu[i]; i++) {
      M[i] = malloc(128);
      sprintf(M[i], "%s: %s", InputMenu[i]
             ,SDL_GetKeyName(PAD_GetMapping(0, InputMenu[i])));
    }
    M[i] = 0;
    menuPush(M,InputMenuHandler);
  } else if (!strcmp("Options", N)) {
    i = 0;
    M = (char**)malloc(sizeof(char*)*64);
    for (i=0; OptionsMenu[i]; i++) {
      M[i] = malloc(128);
      if (!strcmp(OptionsMenu[i],"Scaler"))
        sprintf(M[i], "%s: %s", OptionsMenu[i], GPU_GetScaler());
    }
    M[i] = 0;
    menuPush(M,OptionsMenuHandler);
  } else if (!strcmp("Debug", N)) {
    menuPush(DebugMenu,DebugMenuHandler);
  }
}

void menuLoop() {
  int i, w, h;
  uint8_t *p;
  SDL_Event event;

  menuPush(MainMenu, MainMenuHandler);

  while(!Done) {
    p = screenBlitStart(&w, &h);
    memset(p, 0, w*h*4);

    if (MenuDepth<0) {
      screenBlitEnd();
      return;
    }

    sdlText(screen,0,0,"ARROWS = move, ENTER = pick, ESC = back");
    for (i=0; i<Menu->NItems; i++) {
      if (i==Menu->Cursor) sdlText(screen,4,i*16+32,">");
      sdlText(screen,16,i*16+32,Menu->Items[i]);
    }

    screenBlitEnd();

    while(SDL_PollEvent(&event)) {
      switch(event.type) {
      case SDL_VIDEORESIZE: resize(event.resize.w, event.resize.h); break;
      case SDL_QUIT: Done=1; return;
      case SDL_KEYDOWN: SdlKeys[event.key.keysym.sym] = 1; break;
      case SDL_KEYUP: SdlKeys[event.key.keysym.sym] = 0; break;
      }
    }

    for (i=0; i<0x10000; i++) if (SdlKeys[i]) {
      ((void (*)(char *,int))Menu->H)(Menu->Items[Menu->Cursor], i);
      break;
    }

    if (SdlKeys[SDLK_RETURN]) SdlKeys[SDLK_RETURN] = 0;

    if (SdlKeys[SDLK_ESCAPE]) {
      SdlKeys[SDLK_ESCAPE] = 0;
      menuPop();
    }

    if (SdlKeys[SDLK_UP] && Menu->Cursor>0) Menu->Cursor--;
    if (SdlKeys[SDLK_DOWN] && Menu->Cursor<Menu->NItems-1) Menu->Cursor++;
  }
}

void SysUpdate() {
  int Reset = 0, k, n;
  static int MakingSnaps = 0;
  SDL_Event event;

  while(SDL_PollEvent(&event)) { // read system events
    switch(event.type) {
    case SDL_VIDEORESIZE:
      resize(event.resize.w, event.resize.h);
      break;
    case SDL_QUIT: Done = 1; break;
    case SDL_MOUSEBUTTONDOWN:
      if (event.button.button == SDL_BUTTON_LEFT)
        GPU_pick(event.button.x, event.button.y, screen->w, screen->h);
      break;

    case SDL_KEYDOWN:
      k = event.key.keysym.sym;
      if(SDLK_F1 <= k && k <= SDLK_F8) {
        n = k-SDLK_F1+1;
        if (event.key.keysym.mod & KMOD_SHIFT) SaveStateN(n);
        else LoadStateN(n);
      }
      if (k == SDLK_a) MakingSnaps = !MakingSnaps;
      if (k == SDLK_v) viewRAM((uint8_t*)psxM, 0x200000, 512, 8, SDLK_v);
      if (k == SDLK_n) viewRAM(psxVSecure, 0x100000, 2048, 16, SDLK_n);

      //if(k == SDLK_z) startTrace();
      if(k == SDLK_BACKSPACE) {
        printf("Reset!\n");
        Reset = 1;
      }
      if (k == SDLK_ESCAPE) menuLoop();
      else SdlKeys[k] = 1;
      break;
    case SDL_KEYUP: SdlKeys[event.key.keysym.sym] = 0; break;
    //case SDL_BACKSPACE: Reset=1; break;
    }
  }

  if (Reset||Done) {
    Paused = 0;
    restart();
  }

  if (MakingSnaps) GPU_makeSnapshot();
}

void setCaption(char *Title) {
  SDL_WM_SetCaption(Title, NULL);
}

int main(int argc, char **argv) {
  char Tmp[PATHLEN];
  int op = -1;
  struct option Options[] = {
    {"fullscreen", no_argument, &Fullscreen, 1},
    {"scale", required_argument, 0, 's'},
    {"recompile", no_argument, &Recompile, 1},
    {"help", no_argument, &Action, PrintUsage},
    {"version", no_argument, &Action, PrintVersion},
    {NULL, no_argument, NULL, 0 }};

  while((op = getopt_long(argc, argv, "fs:rhv", Options, NULL)) != -1) {
    switch(op) {
    case 0: break; // long option already processed by getopt_long
    case 's': if (!optarg) {
                printf("-s requires an argument\n");
                return -1;
              }
              Scale = atoi(optarg);
              if (Scale<1) {
                printf("scale should be >= 1\n");
                return -1;
              }
              break;
    case 'h': Action=PrintUsage; break;
    case 'v': Action=PrintVersion; break;
    }
  }

  if (Action == PrintUsage) {
    usage();
    return 0;
  } else if (Action == PrintVersion) {
    version();
    return 0;
  }

  while (optind < argc) CDs[NCDs++] = argv[optind++];
  if (NCDs) SetIsoFile(CDs[0]);
  else {
    printf("Try \"./repsx -h\" to see all options\n");
    SetIsoFile("NoIsoRunBIOS");
  }

	if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_NOPARACHUTE) < 0) {
    printf("Failed to init SDL\n");
    return -1;
  }

	resize(640, 480); //ought to be enough for anybody

  setCaption("RePSX");

  strcpy(WorkDir, ".");

  sprintf(Tmp, "%s/saves", WorkDir);
  makePath(Tmp);

  sprintf(Tmp, "%s/patches", WorkDir);
  makePath(Tmp);

  sprintf(Tmp, "%s/snaps", WorkDir);
  makePath(Tmp);

  sprintf(Config.Mcd1, "%s/saves/mc1.bin", WorkDir);
  sprintf(Config.Mcd2, "%s/saves/mc2.bin", WorkDir);
  sprintf(Config.PatchesDir, "%s/patches", WorkDir);
  Config.Cpu = Recompile ? CPU_DYNAREC : CPU_INTERPRETER;
  Config.HLE = FALSE; // use bios image
  //Config.HLE = TRUE; // useful if game code calls printf
  Config.PsxAuto =  1; // autodetect the NTSC/PAL images

  if (EmuInit() != 0) {
    printf("Failed to init emulator\n");
    return -1;
  }


	setjmp(restartJmp);
  if (Done) goto end;

	EmuReset();
  
  if (strcmp(GetIsoFile(), "NoIsoRunBIOS")) {
    if (CheckCdrom() == -1) {
      printf("Could not check CD-ROM!\n");
      return -1;
    }
    LoadCdrom();
  }

	if (StateToLoad) {
		LoadState(StateToLoad);
		free(StateToLoad);
    StateToLoad = 0;
	}

	psxCpu->Execute();

end:
  //EmuShutdown()
  //ClosePlugins();
  //SysClose();

  //SDL_Quit is known to segfault us
	//SDL_Quit();
  return 0;
}


void SysPrintf(const char *fmt, ...) {
  va_list list;
  char msg[512];

  va_start(list, fmt);
  vsprintf(msg, fmt, list);
  va_end(list);

  printf ("%s", msg);
}

void SysMessage(const char *fmt, ...) {
  va_list list;
  char msg[512];

  va_start(list, fmt);
  vsprintf(msg, fmt, list);
  va_end(list);

  printf ("Error: %s", msg);
}

void SysReset() {
  Paused = 0;
  longjmp(restartJmp, 0);
}

// Returns to the Gui
void SysRunGui() {
}

// Close mem and plugins
void SysClose() {
    EmuShutdown();
    ReleasePlugins();
}

