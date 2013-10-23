#include <SDL.h>
#include <SDL_image.h>

#include "sdl_text.h"
#include "sdl_font.h"

static int S[256]; //starts
static int E[256]; //ends
static uint8_t *F;

static void setupFont(uint8_t *p) {
  int i, j, x, y, c;
  int sx, ex;


  memset(S, 0, 256);
  memset(E, 0, 256);

  F = p;
  for (y=0; y<16; y++) {
    for (x=0; x<16; x++) {
      sx = ex = -1;
      for (i=0; i<16; i++) {
        for (j=0; j<16; j++) {
          c = F[(y*16+j)*256 + x*16 + i];
          if (sx==-1 && c) sx = i;
          if (c) ex = i+1;
        }
      }
      if (sx == -1) sx = 0;
      if (ex == -1) ex = 8; // space
      S[y*16+x] = sx;
      E[y*16+x] = ex;
    }
  }
}

void sdlText(SDL_Surface *s, int x, int y, char *format, ...) {
  int i, j, dx=x, dy=y, sx, sy, c, v;
  static int Prepared = 0;
	char string[1024], *p;


	va_list ap;
	va_start(ap, format);
	vsprintf(string, format, ap);
	va_end(ap);

  if (!Prepared) {
    setupFont(Font);
    Prepared = 1;
  }
  
  for (p = string; *p; p++) {
    if(*p == '\n') {
			dy += 16;
			dx = x;
			continue;
		}
    c = (uint8_t)*p;

    sy = (c/16)*16;
    sx = (c%16)*16;

    for (j=0; j<16; j++) {
      if (dy+j >= s->h) break;
      for (i=S[c]; i<E[c]; i++, dx++) {
        v = F[(sy+j)*256 + sx + i];
        if (dx < s->w) {
          uint8_t *d = s->pixels + ((dy+j)*s->w + dx)*4;
          d[1] = v;
          d[2] = v;
          d[3] = v;
        }
      }
      if (j != 15) dx -= E[c]-S[c];
    }
    dx++;
  }
}
