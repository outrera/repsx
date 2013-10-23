#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <assert.h>

#include "common.h"
#include "r3000a.h"
#include "bios.h"

#include "cheat.h"
#include "ppf.h"

PcsxConfig Config;
char WorkDir[PATHLEN];
boolean NetOpened = FALSE;

int Log = 0;


void hd(uint8_t *P, int S) {
  int X, Y, I;
  for (Y=0; Y < (S+15)/16; Y++) {
    printf("%04X |", Y*16);
    for(X=0; X<16; X++) {
      I = Y*16 + X;
      if ((I&0xf) == 8) printf(" ");
      if (I<S) printf(" %02X", P[I]);
      else printf("   ");
    }
    printf(" |");
    for (X=0; X< 16; X++) {
      I = Y*16 + X;
      if (I<S && isprint(P[I]) && P[I]!='\n' && P[I]!='\t') printf("%c", P[I]);
      else printf(" ");
    }
    printf("\n");
  }
}

char *downcase(char *t) {
  char *s = t;
  while(*s) {if(isupper(*s)) *s = tolower(*s); s++;}
  return t;
}


char *upcase(char *t) {
  char *s = t;
  while(*s) {if(islower(*s)) *s = toupper(*s); s++;}
  return t;
}

void pathParts(char *Dir, char *Name, char *Ext, char *Path) {
  char *P, *Q;
  int L;

  if ((P = strrchr(Path, '/'))) {
    if (Dir) {
      L = P-Path;
      strncpy(Dir, Path, L);
      Dir[L] = 0;
    }
    P++;
  } else {
    if (Dir) Dir[0] = 0;
    P = Path;
  }
  if ((Q = strrchr(P, '.'))) {
    if (Ext) strcpy(Ext, Q+1);
    if (Name) {
       L = Q-P;
       strncpy(Name, P, L);
       Name[L] = 0;
    }
  } else {
    if (Name) strcpy(Name, P);
    if (Ext) Ext[0] = 0;
  }
}

uint8_t *loadFile(char *name, int *len) {
	int sz;
	uint8_t *r;
	FILE *f = fopen(name, "r");
	if(!f) {
    printf("Cant open %s\n", name);
    exit(-1);
  }
	fseek(f, 0, SEEK_END);
	sz = ftell(f);
	r = malloc(sz);
	fseek(f, 0, SEEK_SET);
	fread(r, 1, sz, f);
	fclose(f);
  if (len) *len = sz;
	return r;
}


static void createPathForFile(char *FileName) {
   char Buffer[4*1024];
   char *Q = 0, *P = FileName;
   while(*P) {
     if(*P == '/') Q = P;
     ++P;
   }
   if(Q) {
      int N = Q - FileName;
      memcpy(Buffer, FileName, N);
      Buffer[N] = 0;
      if(!fileExist(Buffer)) makePath(Buffer);
   }
}

void saveFile(char *Name, void *Data, int Length) {
  FILE *F;
  createPathForFile(Name);
  F = fopen(Name, "wb");
  if (!F) {
    printf("Cant create %s\n", Name);
    exit(-1);
  }
  fwrite(Data, 1, Length, F);
  fclose(F);
}




void removeFile(char *fn) {
	char b[1024];
	sprintf(b, "rm -f '%s'", fn);
	system(b);
}

int fileExist(char *File) {
  struct stat S;
  if(stat(File, &S) == 0) return 1;
  return 0;
}

int folderP(char *Name) {
  DIR *Dir;
  if(!(Dir = opendir(Name))) return 0;
  closedir(Dir);
  return 1;
}

int fileP(char *Name) {
  FILE *F;
  if (folderP(Name)) return 0;
  F = fopen(Name, "r");
  if (!F) return 0;
  fclose (F);
  return 1;
}

int fileSize(char *File) {
  int S;
  FILE *F = fopen(File, "r");
  if(!F) return 0;
  fseek(F, 0, SEEK_END);
  S = ftell(F);
  fclose(F);
  return S;
}


#ifdef WIN32
#define m_mkdir(X) mkdir(X)
#else
#define m_mkdir(X) mkdir(X, S_IRWXU|S_IRWXG|S_IROTH|S_IXOTH)
#endif

void makePath(char *Path) {
  char T[1024], *P;
  strcpy(T, Path);
  P = T;
  while((P = strchr(P+1, '/'))) {
    *P = 0;
    // create a directory named with read/write/search permissions for
    // owner and group, and with read/search permissions for others.
    if (!fileExist(T)) m_mkdir(T);
    *P = '/';
  }
  if (!fileExist(T)) m_mkdir(T);
  //free(shell("mkdir -p '%s'", Path));
}


int EmuInit() {
	if (psxInit() != 0) {
    printf("Failed to init plugins\n");
    return -1;
  }
	if (LoadPlugins() == -1) {
    printf("Failed to init plugins\n");
    return -1;
	}

	LoadMcds();

	if (OpenPlugins() == -1) {
    printf("Failed to open plugins\n");
    return -1;
	}
  return 0;
}

void EmuReset() {
	FreeCheatSearchResults();
	FreeCheatSearchMem();
	psxReset();
}

void EmuShutdown() {
	ClearAllCheats();
	FreeCheatSearchResults();
	FreeCheatSearchMem();
	FreePPFCache();
	psxShutdown();
}

void EmuUpdate() {
	// Do not allow hotkeys inside a softcall from HLE BIOS
	if (!Config.HLE || !hleSoftCall)
		SysUpdate();

	ApplyCheats();
}

void __Log(char *fmt, ...) {
	va_list list;
	char tmp[1024];

	va_start(list, fmt);
	vsprintf(tmp, fmt, list);
	SysPrintf(tmp);
	va_end(list);
}



void saveBMP(char *filename, uint8_t *pixels, int w, int h, int flip) {
  int x,y, yi, ey;
  FILE *bmpfile;
  uint8_t *p, header[0x36], empty[2] = {0,0}, line[1024*3];
  int size = w*h*3 + 0x38;

  memset(header, 0, 0x36);
  header[0] = 'B';
  header[1] = 'M';
  header[2] = size & 0xff;
  header[3] = (size >> 8) & 0xff;
  header[4] = (size >> 16) & 0xff;
  header[5] = (size >> 24) & 0xff;
  header[0x0a] = 0x36;
  header[0x0e] = 0x28;
  header[0x12] = w % 256;
  header[0x13] = w / 256;
  header[0x16] = h % 256;
  header[0x17] = h / 256;
  header[0x1a] = 0x01;
  header[0x1c] = 0x18;
  header[0x26] = 0x12;
  header[0x27] = 0x0B;
  header[0x2A] = 0x12;
  header[0x2B] = 0x0B;

  bmpfile = fopen(filename,"wb");
  if (!bmpfile) return;
  fwrite(header, 0x36, 1, bmpfile);

  if (flip) {
    y = 0;
    yi = 1;
    ey = h;
  } else {
    y = h-1;
    yi = -1;
    ey = -1;
  } 

  for ( ; y!=ey; y+=yi) {
    p = pixels+y*w*4;
    for (x=0; x<w; x++) {
      line[x*3+0] = p[2];
      line[x*3+1] = p[1];
      line[x*3+2] = p[0];
      p += 4;
    }
    fwrite(line, w*3, 1, bmpfile);
  }
  fwrite(empty, 0x2, 1, bmpfile);
  fclose(bmpfile);
}


void resample(void *dst, int dw, int dh, void *src, int sw, int sh) {
  uint32_t *dstp = (uint32_t *)dst, *e;
  int dy, sx, sy, ix, iy;

  if(dw == 0 || dh == 0) return;

  ix = ((uint32_t)sw << 16) / dw;
  iy = ((uint32_t)sh << 16) / dh;

  for (dy=0, sy=0; dy<dh; ++dy, sy+=iy) {
    uint32_t *srcp = (uint32_t *)src + (sy>>16) * sw;
    e = dstp+dw;
    for (sx=0; dstp<e; sx+=ix) *dstp++ = srcp[sx>>16]<<8;
  }
}
