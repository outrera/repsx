#include "plugins.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <SDL.h>
#include <SDL_joystick.h>

enum {
	DKEY_SELECT = 0,
	DKEY_L3,
	DKEY_R3,
	DKEY_START,
	DKEY_UP,
	DKEY_RIGHT,
	DKEY_DOWN,
	DKEY_LEFT,
	DKEY_L2,
	DKEY_R2,
	DKEY_L1,
	DKEY_R1,
	DKEY_TRIANGLE,
	DKEY_CIRCLE,
	DKEY_CROSS,
	DKEY_SQUARE,

	DKEY_TOTAL
};

enum {
	ANALOG_LEFT = 0,
	ANALOG_RIGHT,

	ANALOG_TOTAL
};

enum { NONE = 0, AXIS, HAT, BUTTON };

typedef struct tagKeyDef {
	uint8_t			JoyEvType;
	union {
		int16_t		d;
		int16_t		Axis;   // positive=axis+, negative=axis-, abs(Axis)-1=axis index
		uint16_t	Hat;	// 8-bit for hat number, 8-bit for direction
		uint16_t	Button; // button number
	} J;
	uint16_t		Key;
} KEYDEF;

enum { ANALOG_XP = 0, ANALOG_XM, ANALOG_YP, ANALOG_YM };

typedef struct tagPadDef {
	int8_t			DevNum;
	uint16_t		Type;
	KEYDEF			KeyDef[DKEY_TOTAL];
	KEYDEF			AnalogDef[ANALOG_TOTAL][4];
} PADDEF;

typedef struct tagConfig {
	uint8_t			Threaded;
	PADDEF			PadDef[2];
} CONFIG;

typedef struct tagPadState {
	SDL_Joystick		*JoyDev;
	uint8_t				PadMode;
	uint8_t				PadID;
	volatile uint16_t	KeyStatus;
	volatile uint16_t	JoyKeyStatus;
	volatile uint8_t	AnalogStatus[ANALOG_TOTAL][2]; // 0-255 where 127 is center position
	volatile uint8_t	AnalogKeyStatus[ANALOG_TOTAL][4];
} PADSTATE;

typedef struct tagGlobalData {
	CONFIG				cfg;
	uint8_t				Opened;
	PADSTATE			PadState[2];
	volatile long		KeyLeftOver;
} GLOBALDATA;


enum {
	CMD_READ_DATA_AND_VIBRATE = 0x42,
	CMD_CONFIG_MODE = 0x43,
	CMD_SET_MODE_AND_LOCK = 0x44,
	CMD_QUERY_MODEL_AND_MODE = 0x45,
	CMD_QUERY_ACT = 0x46, // ??
	CMD_QUERY_COMB = 0x47, // ??
	CMD_QUERY_MODE = 0x4C, // QUERY_MODE ??
	CMD_VIBRATION_TOGGLE = 0x4D,
};


static GLOBALDATA g;



void InitKeyboard() {
	g.PadState[0].KeyStatus = 0xFFFF;
	g.PadState[1].KeyStatus = 0xFFFF;
}

void DestroyKeyboard() {
}

void CheckKeyboard() {
  int i, j;
  for (i = 0; i < 2; i++) {
    g.PadState[i].KeyStatus = 0xFFFF; //turk all buttons off
    for (j = 0; j < DKEY_TOTAL; j++) {
      if (SdlKeys[g.cfg.PadDef[i].KeyDef[j].Key])
        g.PadState[i].KeyStatus &= ~(1 << j);
    }
  }
}


#if 0
void CheckKeyboard() {
	uint8_t					i, j, found;
	XEvent					evt;
	XClientMessageEvent		*xce;
	uint16_t				Key;

	while (XPending(g.Disp)) {
		XNextEvent(g.Disp, &evt);
		switch (evt.type) {
			case KeyPress:
				Key = XLookupKeysym((XKeyEvent *)&evt, 0);
				found = 0;
				for (i = 0; i < 2; i++) {
					for (j = 0; j < DKEY_TOTAL; j++) {
						if (g.cfg.PadDef[i].KeyDef[j].Key == Key) {
							found = 1;
							g.PadState[i].KeyStatus &= ~(1 << j);
						}
					}
				}
				if (!found && !AnalogKeyPressed(Key)) {
					g.KeyLeftOver = Key;
				}
				return;

			case KeyRelease:
				Key = XLookupKeysym((XKeyEvent *)&evt, 0);
				found = 0;
				for (i = 0; i < 2; i++) {
					for (j = 0; j < DKEY_TOTAL; j++) {
						if (g.cfg.PadDef[i].KeyDef[j].Key == Key) {
							found = 1;
							g.PadState[i].KeyStatus |= (1 << j);
						}
					}
				}
				if (!found && !AnalogKeyReleased(Key)) {
					g.KeyLeftOver = ((long)Key | 0x40000000);
				}
				break;

			case ClientMessage:
				xce = (XClientMessageEvent *)&evt;
				if (xce->message_type == wmprotocols && (Atom)xce->data.l[0] == wmdelwindow) {
					// Fake an ESC key if user clicked the close button on window
					g.KeyLeftOver = XK_Escape;
					return;
				}
				break;
		}
	}
}
#endif


int PAD_GetMapping(int I, char *N) {
  if (!strcmp(N,"Select")) return g.cfg.PadDef[I].KeyDef[DKEY_SELECT].Key;
  if (!strcmp(N,"Start")) return g.cfg.PadDef[I].KeyDef[DKEY_START].Key;
  if (!strcmp(N,"Up")) return g.cfg.PadDef[I].KeyDef[DKEY_UP].Key;
  if (!strcmp(N,"Right")) return g.cfg.PadDef[I].KeyDef[DKEY_RIGHT].Key;
  if (!strcmp(N,"Down")) return g.cfg.PadDef[I].KeyDef[DKEY_DOWN].Key;
  if (!strcmp(N,"Left")) return g.cfg.PadDef[I].KeyDef[DKEY_LEFT].Key;
  if (!strcmp(N,"L1")) return g.cfg.PadDef[I].KeyDef[DKEY_L1].Key;
  if (!strcmp(N,"L2")) return g.cfg.PadDef[I].KeyDef[DKEY_L2].Key;
  if (!strcmp(N,"Triangle")) return g.cfg.PadDef[I].KeyDef[DKEY_TRIANGLE].Key;
  if (!strcmp(N,"Circle")) return g.cfg.PadDef[I].KeyDef[DKEY_CIRCLE].Key;
  if (!strcmp(N,"Cross")) return g.cfg.PadDef[I].KeyDef[DKEY_CROSS].Key;
  if (!strcmp(N,"Square")) return g.cfg.PadDef[I].KeyDef[DKEY_SQUARE].Key;
  if (!strcmp(N,"R1")) return g.cfg.PadDef[I].KeyDef[DKEY_R1].Key;
  if (!strcmp(N,"R2")) return g.cfg.PadDef[I].KeyDef[DKEY_R2].Key;
  return SDLK_UNKNOWN;
}

void PAD_SetMapping(int I, char *N, int K) {
  if (!strcmp(N,"Select")) g.cfg.PadDef[I].KeyDef[DKEY_SELECT].Key = K;
  if (!strcmp(N,"Start")) g.cfg.PadDef[I].KeyDef[DKEY_START].Key = K;
  if (!strcmp(N,"Up")) g.cfg.PadDef[I].KeyDef[DKEY_UP].Key = K;
  if (!strcmp(N,"Right")) g.cfg.PadDef[I].KeyDef[DKEY_RIGHT].Key = K;
  if (!strcmp(N,"Down")) g.cfg.PadDef[I].KeyDef[DKEY_DOWN].Key = K;
  if (!strcmp(N,"Left")) g.cfg.PadDef[I].KeyDef[DKEY_LEFT].Key = K;
  if (!strcmp(N,"L1")) g.cfg.PadDef[I].KeyDef[DKEY_L1].Key = K;
  if (!strcmp(N,"L2")) g.cfg.PadDef[I].KeyDef[DKEY_L2].Key = K;
  if (!strcmp(N,"Triangle")) g.cfg.PadDef[I].KeyDef[DKEY_TRIANGLE].Key = K;
  if (!strcmp(N,"Circle")) g.cfg.PadDef[I].KeyDef[DKEY_CIRCLE].Key = K;
  if (!strcmp(N,"Cross")) g.cfg.PadDef[I].KeyDef[DKEY_CROSS].Key = K;
  if (!strcmp(N,"Square")) g.cfg.PadDef[I].KeyDef[DKEY_SQUARE].Key = K;
  if (!strcmp(N,"R1")) g.cfg.PadDef[I].KeyDef[DKEY_R1].Key = K;
  if (!strcmp(N,"R2")) g.cfg.PadDef[I].KeyDef[DKEY_R2].Key = K;
}

static void SetDefaultConfig() {
	memset(&g.cfg, 0, sizeof(g.cfg));

	g.cfg.Threaded = 0;

	g.cfg.PadDef[0].DevNum = 0;
	g.cfg.PadDef[1].DevNum = 1;

	g.cfg.PadDef[0].Type = PSE_PAD_TYPE_STANDARD;
	g.cfg.PadDef[1].Type = PSE_PAD_TYPE_STANDARD;

	// Pad1 keyboard
	g.cfg.PadDef[0].KeyDef[DKEY_SELECT].Key = SDLK_q;
	g.cfg.PadDef[0].KeyDef[DKEY_START].Key = SDLK_w;
	g.cfg.PadDef[0].KeyDef[DKEY_UP].Key = SDLK_r;
	g.cfg.PadDef[0].KeyDef[DKEY_RIGHT].Key = SDLK_g;
	g.cfg.PadDef[0].KeyDef[DKEY_DOWN].Key = SDLK_f;
	g.cfg.PadDef[0].KeyDef[DKEY_LEFT].Key = SDLK_d;
	g.cfg.PadDef[0].KeyDef[DKEY_L1].Key = SDLK_t;
	g.cfg.PadDef[0].KeyDef[DKEY_L2].Key = SDLK_e;
	g.cfg.PadDef[0].KeyDef[DKEY_TRIANGLE].Key = SDLK_KP8;
	g.cfg.PadDef[0].KeyDef[DKEY_CIRCLE].Key = SDLK_KP6;
	g.cfg.PadDef[0].KeyDef[DKEY_CROSS].Key = SDLK_KP5;
	g.cfg.PadDef[0].KeyDef[DKEY_SQUARE].Key = SDLK_KP4;
	g.cfg.PadDef[0].KeyDef[DKEY_R1].Key = SDLK_KP7;
	g.cfg.PadDef[0].KeyDef[DKEY_R2].Key = SDLK_KP9;

	// Pad1 joystick
	g.cfg.PadDef[0].KeyDef[DKEY_SELECT].JoyEvType = BUTTON;
	g.cfg.PadDef[0].KeyDef[DKEY_SELECT].J.Button = 8;
	g.cfg.PadDef[0].KeyDef[DKEY_START].JoyEvType = BUTTON;
	g.cfg.PadDef[0].KeyDef[DKEY_START].J.Button = 9;
	g.cfg.PadDef[0].KeyDef[DKEY_UP].JoyEvType = AXIS;
	g.cfg.PadDef[0].KeyDef[DKEY_UP].J.Axis = -2;
	g.cfg.PadDef[0].KeyDef[DKEY_RIGHT].JoyEvType = AXIS;
	g.cfg.PadDef[0].KeyDef[DKEY_RIGHT].J.Axis = 1;
	g.cfg.PadDef[0].KeyDef[DKEY_DOWN].JoyEvType = AXIS;
	g.cfg.PadDef[0].KeyDef[DKEY_DOWN].J.Axis = 2;
	g.cfg.PadDef[0].KeyDef[DKEY_LEFT].JoyEvType = AXIS;
	g.cfg.PadDef[0].KeyDef[DKEY_LEFT].J.Axis = -1;
	g.cfg.PadDef[0].KeyDef[DKEY_L2].JoyEvType = BUTTON;
	g.cfg.PadDef[0].KeyDef[DKEY_L2].J.Button = 4;
	g.cfg.PadDef[0].KeyDef[DKEY_L1].JoyEvType = BUTTON;
	g.cfg.PadDef[0].KeyDef[DKEY_L1].J.Button = 6;
	g.cfg.PadDef[0].KeyDef[DKEY_R2].JoyEvType = BUTTON;
	g.cfg.PadDef[0].KeyDef[DKEY_R2].J.Button = 5;
	g.cfg.PadDef[0].KeyDef[DKEY_R1].JoyEvType = BUTTON;
	g.cfg.PadDef[0].KeyDef[DKEY_R1].J.Button = 7;
	g.cfg.PadDef[0].KeyDef[DKEY_TRIANGLE].JoyEvType = BUTTON;
	g.cfg.PadDef[0].KeyDef[DKEY_TRIANGLE].J.Button = 0;
	g.cfg.PadDef[0].KeyDef[DKEY_CIRCLE].JoyEvType = BUTTON;
	g.cfg.PadDef[0].KeyDef[DKEY_CIRCLE].J.Button = 1;
	g.cfg.PadDef[0].KeyDef[DKEY_CROSS].JoyEvType = BUTTON;
	g.cfg.PadDef[0].KeyDef[DKEY_CROSS].J.Button = 2;
	g.cfg.PadDef[0].KeyDef[DKEY_SQUARE].JoyEvType = BUTTON;
	g.cfg.PadDef[0].KeyDef[DKEY_SQUARE].J.Button = 3;

	// Pad2 joystick
	g.cfg.PadDef[1].KeyDef[DKEY_SELECT].JoyEvType = BUTTON;
	g.cfg.PadDef[1].KeyDef[DKEY_SELECT].J.Button = 8;
	g.cfg.PadDef[1].KeyDef[DKEY_START].JoyEvType = BUTTON;
	g.cfg.PadDef[1].KeyDef[DKEY_START].J.Button = 9;
	g.cfg.PadDef[1].KeyDef[DKEY_UP].JoyEvType = AXIS;
	g.cfg.PadDef[1].KeyDef[DKEY_UP].J.Axis = -2;
	g.cfg.PadDef[1].KeyDef[DKEY_RIGHT].JoyEvType = AXIS;
	g.cfg.PadDef[1].KeyDef[DKEY_RIGHT].J.Axis = 1;
	g.cfg.PadDef[1].KeyDef[DKEY_DOWN].JoyEvType = AXIS;
	g.cfg.PadDef[1].KeyDef[DKEY_DOWN].J.Axis = 2;
	g.cfg.PadDef[1].KeyDef[DKEY_LEFT].JoyEvType = AXIS;
	g.cfg.PadDef[1].KeyDef[DKEY_LEFT].J.Axis = -1;
	g.cfg.PadDef[1].KeyDef[DKEY_L2].JoyEvType = BUTTON;
	g.cfg.PadDef[1].KeyDef[DKEY_L2].J.Button = 4;
	g.cfg.PadDef[1].KeyDef[DKEY_L1].JoyEvType = BUTTON;
	g.cfg.PadDef[1].KeyDef[DKEY_L1].J.Button = 6;
	g.cfg.PadDef[1].KeyDef[DKEY_R2].JoyEvType = BUTTON;
	g.cfg.PadDef[1].KeyDef[DKEY_R2].J.Button = 5;
	g.cfg.PadDef[1].KeyDef[DKEY_R1].JoyEvType = BUTTON;
	g.cfg.PadDef[1].KeyDef[DKEY_R1].J.Button = 7;
	g.cfg.PadDef[1].KeyDef[DKEY_TRIANGLE].JoyEvType = BUTTON;
	g.cfg.PadDef[1].KeyDef[DKEY_TRIANGLE].J.Button = 0;
	g.cfg.PadDef[1].KeyDef[DKEY_CIRCLE].JoyEvType = BUTTON;
	g.cfg.PadDef[1].KeyDef[DKEY_CIRCLE].J.Button = 1;
	g.cfg.PadDef[1].KeyDef[DKEY_CROSS].JoyEvType = BUTTON;
	g.cfg.PadDef[1].KeyDef[DKEY_CROSS].J.Button = 2;
	g.cfg.PadDef[1].KeyDef[DKEY_SQUARE].JoyEvType = BUTTON;
	g.cfg.PadDef[1].KeyDef[DKEY_SQUARE].J.Button = 3;
}



void LoadPADConfig() {
	FILE		*fp;
  char    Tmp[1024];
	char		buf[256];
	int			current, a, b, c;

	SetDefaultConfig();

  sprintf(Tmp, "%s/saves/input.cfg", WorkDir);
	fp = fopen(Tmp, "r");
	if (fp == NULL) {
		return;
	}

	current = 0;

	while (fgets(buf, 256, fp) != NULL) {
		if (strncmp(buf, "Threaded=", 9) == 0) {
			g.cfg.Threaded = atoi(&buf[9]);
		} else if (strncmp(buf, "[PAD", 4) == 0) {
			current = atoi(&buf[4]) - 1;
			if (current < 0) {
				current = 0;
			} else if (current > 1) {
				current = 1;
			}
		} else if (strncmp(buf, "DevNum=", 7) == 0) {
			g.cfg.PadDef[current].DevNum = atoi(&buf[7]);
		} else if (strncmp(buf, "Type=", 5) == 0) {
			g.cfg.PadDef[current].Type = atoi(&buf[5]);
		} else if (strncmp(buf, "Select=", 7) == 0) {
			sscanf(buf, "Select=%d,%d,%d", &a, &b, &c);
			g.cfg.PadDef[current].KeyDef[DKEY_SELECT].Key = a;
			g.cfg.PadDef[current].KeyDef[DKEY_SELECT].JoyEvType = b;
			g.cfg.PadDef[current].KeyDef[DKEY_SELECT].J.d = c;
		} else if (strncmp(buf, "L3=", 3) == 0) {
			sscanf(buf, "L3=%d,%d,%d", &a, &b, &c);
			g.cfg.PadDef[current].KeyDef[DKEY_L3].Key = a;
			g.cfg.PadDef[current].KeyDef[DKEY_L3].JoyEvType = b;
			g.cfg.PadDef[current].KeyDef[DKEY_L3].J.d = c;
		} else if (strncmp(buf, "R3=", 3) == 0) {
			sscanf(buf, "R3=%d,%d,%d", &a, &b, &c);
			g.cfg.PadDef[current].KeyDef[DKEY_R3].Key = a;
			g.cfg.PadDef[current].KeyDef[DKEY_R3].JoyEvType = b;
			g.cfg.PadDef[current].KeyDef[DKEY_R3].J.d = c;
		} else if (strncmp(buf, "Start=", 6) == 0) {
			sscanf(buf, "Start=%d,%d,%d", &a, &b, &c);
			g.cfg.PadDef[current].KeyDef[DKEY_START].Key = a;
			g.cfg.PadDef[current].KeyDef[DKEY_START].JoyEvType = b;
			g.cfg.PadDef[current].KeyDef[DKEY_START].J.d = c;
		} else if (strncmp(buf, "Up=", 3) == 0) {
			sscanf(buf, "Up=%d,%d,%d", &a, &b, &c);
			g.cfg.PadDef[current].KeyDef[DKEY_UP].Key = a;
			g.cfg.PadDef[current].KeyDef[DKEY_UP].JoyEvType = b;
			g.cfg.PadDef[current].KeyDef[DKEY_UP].J.d = c;
		} else if (strncmp(buf, "Right=", 6) == 0) {
			sscanf(buf, "Right=%d,%d,%d", &a, &b, &c);
			g.cfg.PadDef[current].KeyDef[DKEY_RIGHT].Key = a;
			g.cfg.PadDef[current].KeyDef[DKEY_RIGHT].JoyEvType = b;
			g.cfg.PadDef[current].KeyDef[DKEY_RIGHT].J.d = c;
		} else if (strncmp(buf, "Down=", 5) == 0) {
			sscanf(buf, "Down=%d,%d,%d", &a, &b, &c);
			g.cfg.PadDef[current].KeyDef[DKEY_DOWN].Key = a;
			g.cfg.PadDef[current].KeyDef[DKEY_DOWN].JoyEvType = b;
			g.cfg.PadDef[current].KeyDef[DKEY_DOWN].J.d = c;
		} else if (strncmp(buf, "Left=", 5) == 0) {
			sscanf(buf, "Left=%d,%d,%d", &a, &b, &c);
			g.cfg.PadDef[current].KeyDef[DKEY_LEFT].Key = a;
			g.cfg.PadDef[current].KeyDef[DKEY_LEFT].JoyEvType = b;
			g.cfg.PadDef[current].KeyDef[DKEY_LEFT].J.d = c;
		} else if (strncmp(buf, "L2=", 3) == 0) {
			sscanf(buf, "L2=%d,%d,%d", &a, &b, &c);
			g.cfg.PadDef[current].KeyDef[DKEY_L2].Key = a;
			g.cfg.PadDef[current].KeyDef[DKEY_L2].JoyEvType = b;
			g.cfg.PadDef[current].KeyDef[DKEY_L2].J.d = c;
		} else if (strncmp(buf, "R2=", 3) == 0) {
			sscanf(buf, "R2=%d,%d,%d", &a, &b, &c);
			g.cfg.PadDef[current].KeyDef[DKEY_R2].Key = a;
			g.cfg.PadDef[current].KeyDef[DKEY_R2].JoyEvType = b;
			g.cfg.PadDef[current].KeyDef[DKEY_R2].J.d = c;
		} else if (strncmp(buf, "L1=", 3) == 0) {
			sscanf(buf, "L1=%d,%d,%d", &a, &b, &c);
			g.cfg.PadDef[current].KeyDef[DKEY_L1].Key = a;
			g.cfg.PadDef[current].KeyDef[DKEY_L1].JoyEvType = b;
			g.cfg.PadDef[current].KeyDef[DKEY_L1].J.d = c;
		} else if (strncmp(buf, "R1=", 3) == 0) {
			sscanf(buf, "R1=%d,%d,%d", &a, &b, &c);
			g.cfg.PadDef[current].KeyDef[DKEY_R1].Key = a;
			g.cfg.PadDef[current].KeyDef[DKEY_R1].JoyEvType = b;
			g.cfg.PadDef[current].KeyDef[DKEY_R1].J.d = c;
		} else if (strncmp(buf, "Triangle=", 9) == 0) {
			sscanf(buf, "Triangle=%d,%d,%d", &a, &b, &c);
			g.cfg.PadDef[current].KeyDef[DKEY_TRIANGLE].Key = a;
			g.cfg.PadDef[current].KeyDef[DKEY_TRIANGLE].JoyEvType = b;
			g.cfg.PadDef[current].KeyDef[DKEY_TRIANGLE].J.d = c;
		} else if (strncmp(buf, "Circle=", 7) == 0) {
			sscanf(buf, "Circle=%d,%d,%d", &a, &b, &c);
			g.cfg.PadDef[current].KeyDef[DKEY_CIRCLE].Key = a;
			g.cfg.PadDef[current].KeyDef[DKEY_CIRCLE].JoyEvType = b;
			g.cfg.PadDef[current].KeyDef[DKEY_CIRCLE].J.d = c;
		} else if (strncmp(buf, "Cross=", 6) == 0) {
			sscanf(buf, "Cross=%d,%d,%d", &a, &b, &c);
			g.cfg.PadDef[current].KeyDef[DKEY_CROSS].Key = a;
			g.cfg.PadDef[current].KeyDef[DKEY_CROSS].JoyEvType = b;
			g.cfg.PadDef[current].KeyDef[DKEY_CROSS].J.d = c;
		} else if (strncmp(buf, "Square=", 7) == 0) {
			sscanf(buf, "Square=%d,%d,%d", &a, &b, &c);
			g.cfg.PadDef[current].KeyDef[DKEY_SQUARE].Key = a;
			g.cfg.PadDef[current].KeyDef[DKEY_SQUARE].JoyEvType = b;
			g.cfg.PadDef[current].KeyDef[DKEY_SQUARE].J.d = c;
		} else if (strncmp(buf, "LeftAnalogXP=", 13) == 0) {
			sscanf(buf, "LeftAnalogXP=%d,%d,%d", &a, &b, &c);
			g.cfg.PadDef[current].AnalogDef[ANALOG_LEFT][ANALOG_XP].Key = a;
			g.cfg.PadDef[current].AnalogDef[ANALOG_LEFT][ANALOG_XP].JoyEvType = b;
			g.cfg.PadDef[current].AnalogDef[ANALOG_LEFT][ANALOG_XP].J.d = c;
		} else if (strncmp(buf, "LeftAnalogXM=", 13) == 0) {
			sscanf(buf, "LeftAnalogXM=%d,%d,%d", &a, &b, &c);
			g.cfg.PadDef[current].AnalogDef[ANALOG_LEFT][ANALOG_XM].Key = a;
			g.cfg.PadDef[current].AnalogDef[ANALOG_LEFT][ANALOG_XM].JoyEvType = b;
			g.cfg.PadDef[current].AnalogDef[ANALOG_LEFT][ANALOG_XM].J.d = c;
		} else if (strncmp(buf, "LeftAnalogYP=", 13) == 0) {
			sscanf(buf, "LeftAnalogYP=%d,%d,%d", &a, &b, &c);
			g.cfg.PadDef[current].AnalogDef[ANALOG_LEFT][ANALOG_YP].Key = a;
			g.cfg.PadDef[current].AnalogDef[ANALOG_LEFT][ANALOG_YP].JoyEvType = b;
			g.cfg.PadDef[current].AnalogDef[ANALOG_LEFT][ANALOG_YP].J.d = c;
		} else if (strncmp(buf, "LeftAnalogYM=", 13) == 0) {
			sscanf(buf, "LeftAnalogYM=%d,%d,%d", &a, &b, &c);
			g.cfg.PadDef[current].AnalogDef[ANALOG_LEFT][ANALOG_YM].Key = a;
			g.cfg.PadDef[current].AnalogDef[ANALOG_LEFT][ANALOG_YM].JoyEvType = b;
			g.cfg.PadDef[current].AnalogDef[ANALOG_LEFT][ANALOG_YM].J.d = c;
		} else if (strncmp(buf, "RightAnalogXP=", 14) == 0) {
			sscanf(buf, "RightAnalogXP=%d,%d,%d", &a, &b, &c);
			g.cfg.PadDef[current].AnalogDef[ANALOG_RIGHT][ANALOG_XP].Key = a;
			g.cfg.PadDef[current].AnalogDef[ANALOG_RIGHT][ANALOG_XP].JoyEvType = b;
			g.cfg.PadDef[current].AnalogDef[ANALOG_RIGHT][ANALOG_XP].J.d = c;
		} else if (strncmp(buf, "RightAnalogXM=", 14) == 0) {
			sscanf(buf, "RightAnalogXM=%d,%d,%d", &a, &b, &c);
			g.cfg.PadDef[current].AnalogDef[ANALOG_RIGHT][ANALOG_XM].Key = a;
			g.cfg.PadDef[current].AnalogDef[ANALOG_RIGHT][ANALOG_XM].JoyEvType = b;
			g.cfg.PadDef[current].AnalogDef[ANALOG_RIGHT][ANALOG_XM].J.d = c;
		} else if (strncmp(buf, "RightAnalogYP=", 14) == 0) {
			sscanf(buf, "RightAnalogYP=%d,%d,%d", &a, &b, &c);
			g.cfg.PadDef[current].AnalogDef[ANALOG_RIGHT][ANALOG_YP].Key = a;
			g.cfg.PadDef[current].AnalogDef[ANALOG_RIGHT][ANALOG_YP].JoyEvType = b;
			g.cfg.PadDef[current].AnalogDef[ANALOG_RIGHT][ANALOG_YP].J.d = c;
		} else if (strncmp(buf, "RightAnalogYM=", 14) == 0) {
			sscanf(buf, "RightAnalogYM=%d,%d,%d", &a, &b, &c);
			g.cfg.PadDef[current].AnalogDef[ANALOG_RIGHT][ANALOG_YM].Key = a;
			g.cfg.PadDef[current].AnalogDef[ANALOG_RIGHT][ANALOG_YM].JoyEvType = b;
			g.cfg.PadDef[current].AnalogDef[ANALOG_RIGHT][ANALOG_YM].J.d = c;
		}
	}

	fclose(fp);
}

void SavePADConfig() {
  char    Tmp[1024];
	FILE		*fp;
	int			i;


  sprintf(Tmp, "%s/saves/input.cfg", WorkDir);
	fp = fopen(Tmp, "w");
	if (fp == NULL) {
		return;
	}

	fprintf(fp, "[CONFIG]\n");
	fprintf(fp, "Threaded=%d\n", g.cfg.Threaded);
	fprintf(fp, "\n");

	for (i = 0; i < 2; i++) {
		fprintf(fp, "[PAD%d]\n", i + 1);
		fprintf(fp, "DevNum=%d\n", g.cfg.PadDef[i].DevNum);
		fprintf(fp, "Type=%d\n", g.cfg.PadDef[i].Type);

		fprintf(fp, "Select=%d,%d,%d\n", g.cfg.PadDef[i].KeyDef[DKEY_SELECT].Key,
			g.cfg.PadDef[i].KeyDef[DKEY_SELECT].JoyEvType, g.cfg.PadDef[i].KeyDef[DKEY_SELECT].J.d);
		fprintf(fp, "L3=%d,%d,%d\n", g.cfg.PadDef[i].KeyDef[DKEY_L3].Key,
			g.cfg.PadDef[i].KeyDef[DKEY_L3].JoyEvType, g.cfg.PadDef[i].KeyDef[DKEY_L3].J.d);
		fprintf(fp, "R3=%d,%d,%d\n", g.cfg.PadDef[i].KeyDef[DKEY_R3].Key,
			g.cfg.PadDef[i].KeyDef[DKEY_R3].JoyEvType, g.cfg.PadDef[i].KeyDef[DKEY_R3].J.d);
		fprintf(fp, "Start=%d,%d,%d\n", g.cfg.PadDef[i].KeyDef[DKEY_START].Key,
			g.cfg.PadDef[i].KeyDef[DKEY_START].JoyEvType, g.cfg.PadDef[i].KeyDef[DKEY_START].J.d);
		fprintf(fp, "Up=%d,%d,%d\n", g.cfg.PadDef[i].KeyDef[DKEY_UP].Key,
			g.cfg.PadDef[i].KeyDef[DKEY_UP].JoyEvType, g.cfg.PadDef[i].KeyDef[DKEY_UP].J.d);
		fprintf(fp, "Right=%d,%d,%d\n", g.cfg.PadDef[i].KeyDef[DKEY_RIGHT].Key,
			g.cfg.PadDef[i].KeyDef[DKEY_RIGHT].JoyEvType, g.cfg.PadDef[i].KeyDef[DKEY_RIGHT].J.d);
		fprintf(fp, "Down=%d,%d,%d\n", g.cfg.PadDef[i].KeyDef[DKEY_DOWN].Key,
			g.cfg.PadDef[i].KeyDef[DKEY_DOWN].JoyEvType, g.cfg.PadDef[i].KeyDef[DKEY_DOWN].J.d);
		fprintf(fp, "Left=%d,%d,%d\n", g.cfg.PadDef[i].KeyDef[DKEY_LEFT].Key,
			g.cfg.PadDef[i].KeyDef[DKEY_LEFT].JoyEvType, g.cfg.PadDef[i].KeyDef[DKEY_LEFT].J.d);
		fprintf(fp, "L2=%d,%d,%d\n", g.cfg.PadDef[i].KeyDef[DKEY_L2].Key,
			g.cfg.PadDef[i].KeyDef[DKEY_L2].JoyEvType, g.cfg.PadDef[i].KeyDef[DKEY_L2].J.d);
		fprintf(fp, "R2=%d,%d,%d\n", g.cfg.PadDef[i].KeyDef[DKEY_R2].Key,
			g.cfg.PadDef[i].KeyDef[DKEY_R2].JoyEvType, g.cfg.PadDef[i].KeyDef[DKEY_R2].J.d);
		fprintf(fp, "L1=%d,%d,%d\n", g.cfg.PadDef[i].KeyDef[DKEY_L1].Key,
			g.cfg.PadDef[i].KeyDef[DKEY_L1].JoyEvType, g.cfg.PadDef[i].KeyDef[DKEY_L1].J.d);
		fprintf(fp, "R1=%d,%d,%d\n", g.cfg.PadDef[i].KeyDef[DKEY_R1].Key,
			g.cfg.PadDef[i].KeyDef[DKEY_R1].JoyEvType, g.cfg.PadDef[i].KeyDef[DKEY_R1].J.d);
		fprintf(fp, "Triangle=%d,%d,%d\n", g.cfg.PadDef[i].KeyDef[DKEY_TRIANGLE].Key,
			g.cfg.PadDef[i].KeyDef[DKEY_TRIANGLE].JoyEvType, g.cfg.PadDef[i].KeyDef[DKEY_TRIANGLE].J.d);
		fprintf(fp, "Circle=%d,%d,%d\n", g.cfg.PadDef[i].KeyDef[DKEY_CIRCLE].Key,
			g.cfg.PadDef[i].KeyDef[DKEY_CIRCLE].JoyEvType, g.cfg.PadDef[i].KeyDef[DKEY_CIRCLE].J.d);
		fprintf(fp, "Cross=%d,%d,%d\n", g.cfg.PadDef[i].KeyDef[DKEY_CROSS].Key,
			g.cfg.PadDef[i].KeyDef[DKEY_CROSS].JoyEvType, g.cfg.PadDef[i].KeyDef[DKEY_CROSS].J.d);
		fprintf(fp, "Square=%d,%d,%d\n", g.cfg.PadDef[i].KeyDef[DKEY_SQUARE].Key,
			g.cfg.PadDef[i].KeyDef[DKEY_SQUARE].JoyEvType, g.cfg.PadDef[i].KeyDef[DKEY_SQUARE].J.d);
		fprintf(fp, "LeftAnalogXP=%d,%d,%d\n", g.cfg.PadDef[i].AnalogDef[ANALOG_LEFT][ANALOG_XP].Key,
			g.cfg.PadDef[i].AnalogDef[ANALOG_LEFT][ANALOG_XP].JoyEvType,
			g.cfg.PadDef[i].AnalogDef[ANALOG_LEFT][ANALOG_XP].J.d);
		fprintf(fp, "LeftAnalogXM=%d,%d,%d\n", g.cfg.PadDef[i].AnalogDef[ANALOG_LEFT][ANALOG_XM].Key,
			g.cfg.PadDef[i].AnalogDef[ANALOG_LEFT][ANALOG_XM].JoyEvType,
			g.cfg.PadDef[i].AnalogDef[ANALOG_LEFT][ANALOG_XM].J.d);
		fprintf(fp, "LeftAnalogYP=%d,%d,%d\n", g.cfg.PadDef[i].AnalogDef[ANALOG_LEFT][ANALOG_YP].Key,
			g.cfg.PadDef[i].AnalogDef[ANALOG_LEFT][ANALOG_YP].JoyEvType,
			g.cfg.PadDef[i].AnalogDef[ANALOG_LEFT][ANALOG_YP].J.d);
		fprintf(fp, "LeftAnalogYM=%d,%d,%d\n", g.cfg.PadDef[i].AnalogDef[ANALOG_LEFT][ANALOG_YM].Key,
			g.cfg.PadDef[i].AnalogDef[ANALOG_LEFT][ANALOG_YM].JoyEvType,
			g.cfg.PadDef[i].AnalogDef[ANALOG_LEFT][ANALOG_YM].J.d);
		fprintf(fp, "RightAnalogXP=%d,%d,%d\n", g.cfg.PadDef[i].AnalogDef[ANALOG_RIGHT][ANALOG_XP].Key,
			g.cfg.PadDef[i].AnalogDef[ANALOG_RIGHT][ANALOG_XP].JoyEvType,
			g.cfg.PadDef[i].AnalogDef[ANALOG_RIGHT][ANALOG_XP].J.d);
		fprintf(fp, "RightAnalogXM=%d,%d,%d\n", g.cfg.PadDef[i].AnalogDef[ANALOG_RIGHT][ANALOG_XM].Key,
			g.cfg.PadDef[i].AnalogDef[ANALOG_RIGHT][ANALOG_XM].JoyEvType,
			g.cfg.PadDef[i].AnalogDef[ANALOG_RIGHT][ANALOG_XM].J.d);
		fprintf(fp, "RightAnalogYP=%d,%d,%d\n", g.cfg.PadDef[i].AnalogDef[ANALOG_RIGHT][ANALOG_YP].Key,
			g.cfg.PadDef[i].AnalogDef[ANALOG_RIGHT][ANALOG_YP].JoyEvType,
			g.cfg.PadDef[i].AnalogDef[ANALOG_RIGHT][ANALOG_YP].J.d);
		fprintf(fp, "RightAnalogYM=%d,%d,%d\n", g.cfg.PadDef[i].AnalogDef[ANALOG_RIGHT][ANALOG_YM].Key,
			g.cfg.PadDef[i].AnalogDef[ANALOG_RIGHT][ANALOG_YM].JoyEvType,
			g.cfg.PadDef[i].AnalogDef[ANALOG_RIGHT][ANALOG_YM].J.d);

		fprintf(fp, "\n");
	}

	fclose(fp);
}

// cfg.c functions...
void LoadPADConfig();
void SavePADConfig();

// sdljoy.c functions...
void InitSDLJoy();
void DestroySDLJoy();
void CheckJoy();

// xkb.c functions...
void InitKeyboard();
void DestroyKeyboard();
void CheckKeyboard();

// analog.c functions...
void InitAnalog();
void CheckAnalog();
int AnalogKeyPressed(uint16_t Key);
int AnalogKeyReleased(uint16_t Key);



void InitAnalog() {
	g.PadState[0].AnalogStatus[ANALOG_LEFT][0] = 127;
	g.PadState[0].AnalogStatus[ANALOG_LEFT][1] = 127;
	g.PadState[0].AnalogStatus[ANALOG_RIGHT][0] = 127;
	g.PadState[0].AnalogStatus[ANALOG_RIGHT][1] = 127;
	g.PadState[1].AnalogStatus[ANALOG_LEFT][0] = 127;
	g.PadState[1].AnalogStatus[ANALOG_LEFT][1] = 127;
	g.PadState[1].AnalogStatus[ANALOG_RIGHT][0] = 127;
	g.PadState[1].AnalogStatus[ANALOG_RIGHT][1] = 127;

	memset(g.PadState[0].AnalogKeyStatus, 0, sizeof(g.PadState[0].AnalogKeyStatus));
	memset(g.PadState[1].AnalogKeyStatus, 0, sizeof(g.PadState[1].AnalogKeyStatus));
}

void CheckAnalog() {
	int			i, j, k, val;
	uint8_t		n;

	for (i = 0; i < 2; i++) {
		if (g.cfg.PadDef[i].Type != PSE_PAD_TYPE_ANALOGPAD) {
			continue;
		}

		for (j = 0; j < ANALOG_TOTAL; j++) {
			for (k = 0; k < 4; k++) {
				if (g.PadState[i].AnalogKeyStatus[j][k]) {
					switch (k) {
						case ANALOG_XP: g.PadState[i].AnalogStatus[j][0] = 255; k++; break;
						case ANALOG_XM: g.PadState[i].AnalogStatus[j][0] = 0; break;
						case ANALOG_YP: g.PadState[i].AnalogStatus[j][1] = 255; k++; break;
						case ANALOG_YM: g.PadState[i].AnalogStatus[j][1] = 0; break;
					}
					continue;
				}

				switch (g.cfg.PadDef[i].AnalogDef[j][k].JoyEvType) {
					case AXIS:
						n = abs(g.cfg.PadDef[i].AnalogDef[j][k].J.Axis) - 1;

						if (g.cfg.PadDef[i].AnalogDef[j][k].J.Axis > 0) {
							val = SDL_JoystickGetAxis(g.PadState[i].JoyDev, n);
							if (val >= 0) {
								val += 32640;
								val /= 256;

								switch (k) {
									case ANALOG_XP: g.PadState[i].AnalogStatus[j][0] = val; break;
									case ANALOG_XM: g.PadState[i].AnalogStatus[j][0] = 255 - val; break;
									case ANALOG_YP: g.PadState[i].AnalogStatus[j][1] = val; break;
									case ANALOG_YM: g.PadState[i].AnalogStatus[j][1] = 255 - val; break;
								}
							}
						} else if (g.cfg.PadDef[i].AnalogDef[j][k].J.Axis < 0) {
							val = SDL_JoystickGetAxis(g.PadState[i].JoyDev, n);
							if (val <= 0) {
								val += 32640;
								val /= 256;

								switch (k) {
									case ANALOG_XP: g.PadState[i].AnalogStatus[j][0] = 255 - val; break;
									case ANALOG_XM: g.PadState[i].AnalogStatus[j][0] = val; break;
									case ANALOG_YP: g.PadState[i].AnalogStatus[j][1] = 255 - val; break;
									case ANALOG_YM: g.PadState[i].AnalogStatus[j][1] = val; break;
								}
							}
						}
						break;

					case HAT:
						n = (g.cfg.PadDef[i].AnalogDef[j][k].J.Hat >> 8);

						g.PadState[i].AnalogStatus[j][0] = 0;

						if (SDL_JoystickGetHat(g.PadState[i].JoyDev, n) & (g.cfg.PadDef[i].AnalogDef[j][k].J.Hat & 0xFF)) {
							switch (k) {
								case ANALOG_XP: g.PadState[i].AnalogStatus[j][0] = 255; k++; break;
								case ANALOG_XM: g.PadState[i].AnalogStatus[j][0] = 0; break;
								case ANALOG_YP: g.PadState[i].AnalogStatus[j][1] = 255; k++; break;
								case ANALOG_YM: g.PadState[i].AnalogStatus[j][1] = 0; break;
							}
						} else {
							switch (k) {
								case ANALOG_XP: g.PadState[i].AnalogStatus[j][0] = 127; break;
								case ANALOG_XM: g.PadState[i].AnalogStatus[j][0] = 127; break;
								case ANALOG_YP: g.PadState[i].AnalogStatus[j][1] = 127; break;
								case ANALOG_YM: g.PadState[i].AnalogStatus[j][1] = 127; break;
							}
						}
						break;

					case BUTTON:
						if (SDL_JoystickGetButton(g.PadState[i].JoyDev, g.cfg.PadDef[i].AnalogDef[j][k].J.Button)) {
							switch (k) {
								case ANALOG_XP: g.PadState[i].AnalogStatus[j][0] = 255; k++; break;
								case ANALOG_XM: g.PadState[i].AnalogStatus[j][0] = 0; break;
								case ANALOG_YP: g.PadState[i].AnalogStatus[j][1] = 255; k++; break;
								case ANALOG_YM: g.PadState[i].AnalogStatus[j][1] = 0; break;
							}
						} else {
							switch (k) {
								case ANALOG_XP: g.PadState[i].AnalogStatus[j][0] = 127; break;
								case ANALOG_XM: g.PadState[i].AnalogStatus[j][0] = 127; break;
								case ANALOG_YP: g.PadState[i].AnalogStatus[j][1] = 127; break;
								case ANALOG_YM: g.PadState[i].AnalogStatus[j][1] = 127; break;
							}
						}
						break;

					default:
						switch (k) {
							case ANALOG_XP: g.PadState[i].AnalogStatus[j][0] = 127; break;
							case ANALOG_XM: g.PadState[i].AnalogStatus[j][0] = 127; break;
							case ANALOG_YP: g.PadState[i].AnalogStatus[j][1] = 127; break;
							case ANALOG_YM: g.PadState[i].AnalogStatus[j][1] = 127; break;
						}
						break;
				}
			}
		}
	}
}

int AnalogKeyPressed(uint16_t Key) {
	int i, j, k;

	for (i = 0; i < 2; i++) {
		if (g.cfg.PadDef[i].Type != PSE_PAD_TYPE_ANALOGPAD) {
			continue;
		}

		for (j = 0; j < ANALOG_TOTAL; j++) {
			for (k = 0; k < 4; k++) {
				if (g.cfg.PadDef[i].AnalogDef[j][k].Key == Key) {
					g.PadState[i].AnalogKeyStatus[j][k] = 1;
					return 1;
				}
			}
		}
	}

	return 0;
}

int AnalogKeyReleased(uint16_t Key) {
	int i, j, k;

	for (i = 0; i < 2; i++) {
		if (g.cfg.PadDef[i].Type != PSE_PAD_TYPE_ANALOGPAD) {
			continue;
		}

		for (j = 0; j < ANALOG_TOTAL; j++) {
			for (k = 0; k < 4; k++) {
				if (g.cfg.PadDef[i].AnalogDef[j][k].Key == Key) {
					g.PadState[i].AnalogKeyStatus[j][k] = 0;
					return 1;
				}
			}
		}
	}

	return 0;
}

void InitSDLJoy() {
	uint8_t				i;

	g.PadState[0].JoyKeyStatus = 0xFFFF;
	g.PadState[1].JoyKeyStatus = 0xFFFF;

	for (i = 0; i < 2; i++) {
		if (g.cfg.PadDef[i].DevNum >= 0) {
			g.PadState[i].JoyDev = SDL_JoystickOpen(g.cfg.PadDef[i].DevNum);
		} else {
			g.PadState[i].JoyDev = NULL;
		}
	}

	SDL_JoystickEventState(SDL_IGNORE);

	InitAnalog();
}

void DestroySDLJoy() {
	uint8_t				i;

	if (SDL_WasInit(SDL_INIT_JOYSTICK)) {
		for (i = 0; i < 2; i++) {
			if (g.PadState[i].JoyDev != NULL) {
				SDL_JoystickClose(g.PadState[i].JoyDev);
			}
		}
	}

	for (i = 0; i < 2; i++) {
		g.PadState[i].JoyDev = NULL;
	}
}

void CheckJoy() {
	uint8_t				i, j, n;

	SDL_JoystickUpdate();

	for (i = 0; i < 2; i++) {
		if (g.PadState[i].JoyDev == NULL) {
			continue;
		}

		for (j = 0; j < DKEY_TOTAL; j++) {
			switch (g.cfg.PadDef[i].KeyDef[j].JoyEvType) {
				case AXIS:
					n = abs(g.cfg.PadDef[i].KeyDef[j].J.Axis) - 1;

					if (g.cfg.PadDef[i].KeyDef[j].J.Axis > 0) {
						if (SDL_JoystickGetAxis(g.PadState[i].JoyDev, n) > 16383) {
							g.PadState[i].JoyKeyStatus &= ~(1 << j);
						} else {
							g.PadState[i].JoyKeyStatus |= (1 << j);
						}
					} else if (g.cfg.PadDef[i].KeyDef[j].J.Axis < 0) {
						if (SDL_JoystickGetAxis(g.PadState[i].JoyDev, n) < -16383) {
							g.PadState[i].JoyKeyStatus &= ~(1 << j);
						} else {
							g.PadState[i].JoyKeyStatus |= (1 << j);
						}
					}
					break;

				case HAT:
					n = (g.cfg.PadDef[i].KeyDef[j].J.Hat >> 8);

					if (SDL_JoystickGetHat(g.PadState[i].JoyDev, n) & (g.cfg.PadDef[i].KeyDef[j].J.Hat & 0xFF)) {
						g.PadState[i].JoyKeyStatus &= ~(1 << j);
					} else {
						g.PadState[i].JoyKeyStatus |= (1 << j);
					}
					break;

				case BUTTON:
					if (SDL_JoystickGetButton(g.PadState[i].JoyDev, g.cfg.PadDef[i].KeyDef[j].J.Button)) {
						g.PadState[i].JoyKeyStatus &= ~(1 << j);
					} else {
						g.PadState[i].JoyKeyStatus |= (1 << j);
					}
					break;

				default:
					break;
			}
		}
	}

	CheckAnalog();
}




long PAD_init(long flags) {
	LoadPADConfig();

	g.PadState[0].PadMode = 0;
	g.PadState[0].PadID = 0x41;
	g.PadState[1].PadMode = 0;
	g.PadState[1].PadID = 0x41;

	return 0;
}

long PAD_shutdown(void) {
	PAD_close();
	return 0;
}

static pthread_t			ThreadID;
static volatile uint8_t		TerminateThread = 0;

static void *JoyThread(void *param) {
	while (!TerminateThread) {
		CheckJoy();
		usleep(1000);
	}
	pthread_exit(0);
	return NULL;
}

long PAD_open(unsigned long *Disp) {
	if (!g.Opened) {
		if (SDL_WasInit(SDL_INIT_EVERYTHING)) {
			if (SDL_InitSubSystem(SDL_INIT_JOYSTICK) == -1) {
				return -1;
			}
		} else if (SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_NOPARACHUTE) == -1) {
			return -1;
		}

		InitSDLJoy();
		InitKeyboard();

		g.KeyLeftOver = 0;

		if (g.cfg.Threaded) {
			TerminateThread = 0;

			if (pthread_create(&ThreadID, NULL, JoyThread, NULL) != 0) {
				// thread creation failed, fallback to polling
				g.cfg.Threaded = 0;
			}
		}
	}

	g.Opened = 1;

	return 0;
}

long PAD_close(void) {
	if (g.Opened) {
		if (g.cfg.Threaded) {
			TerminateThread = 1;
			pthread_join(ThreadID, NULL);
		}

		DestroySDLJoy();
		DestroyKeyboard();

		if (SDL_WasInit(SDL_INIT_EVERYTHING & ~SDL_INIT_JOYSTICK)) {
			SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
		} else {
			SDL_Quit();
		}
	}

	g.Opened = 0;

	return 0;
}

long PAD_query(void) {
	return PSE_PAD_USE_PORT1 | PSE_PAD_USE_PORT2;
}

static void UpdateInput(void) {
	if (!g.cfg.Threaded) CheckJoy();
	CheckKeyboard();
}

static uint8_t stdpar[2][8] = {
	{0xFF, 0x5A, 0xFF, 0xFF, 0x80, 0x80, 0x80, 0x80},
	{0xFF, 0x5A, 0xFF, 0xFF, 0x80, 0x80, 0x80, 0x80}
};

static uint8_t unk46[2][8] = {
	{0xFF, 0x5A, 0x00, 0x00, 0x01, 0x02, 0x00, 0x0A},
	{0xFF, 0x5A, 0x00, 0x00, 0x01, 0x02, 0x00, 0x0A}
};

static uint8_t unk47[2][8] = {
	{0xFF, 0x5A, 0x00, 0x00, 0x02, 0x00, 0x01, 0x00},
	{0xFF, 0x5A, 0x00, 0x00, 0x02, 0x00, 0x01, 0x00}
};

static uint8_t unk4c[2][8] = {
	{0xFF, 0x5A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
	{0xFF, 0x5A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}
};

static uint8_t unk4d[2][8] = { 
	{0xFF, 0x5A, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
	{0xFF, 0x5A, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}
};

static uint8_t stdcfg[2][8]   = { 
	{0xFF, 0x5A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
	{0xFF, 0x5A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}
};

static uint8_t stdmode[2][8]  = { 
	{0xFF, 0x5A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
	{0xFF, 0x5A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}
};

static uint8_t stdmodel[2][8] = { 
	{0xFF,
	 0x5A,
	 0x01, // 03 - dualshock2, 01 - dualshock
	 0x02, // number of modes
	 0x01, // current mode: 01 - analog, 00 - digital
	 0x02,
	 0x01,
	 0x00},
	{0xFF, 
	 0x5A,
	 0x01, // 03 - dualshock2, 01 - dualshock
	 0x02, // number of modes
	 0x01, // current mode: 01 - analog, 00 - digital
	 0x02,
	 0x01,
	 0x00}
};

static uint8_t CurPad = 0, CurByte = 0, CurCmd = 0, CmdLen = 0;

unsigned char PAD_startPoll(int pad) {
	CurPad = pad - 1;
	CurByte = 0;

	return 0xFF;
}

unsigned char PAD_poll(unsigned char value) {
	static uint8_t		*buf = NULL;
	uint16_t			n;

	if (CurByte == 0) {
		CurByte++;

		// Don't enable Analog/Vibration for a standard pad
		if (g.cfg.PadDef[CurPad].Type != PSE_PAD_TYPE_ANALOGPAD) {
			CurCmd = CMD_READ_DATA_AND_VIBRATE;
		} else {
			CurCmd = value;
		}

		switch (CurCmd) {
			case CMD_CONFIG_MODE:
				CmdLen = 8;
				buf = stdcfg[CurPad];
				if (stdcfg[CurPad][3] == 0xFF) return 0xF3;
				else return g.PadState[CurPad].PadID;

			case CMD_SET_MODE_AND_LOCK:
				CmdLen = 8;
				buf = stdmode[CurPad];
				return 0xF3;

			case CMD_QUERY_MODEL_AND_MODE:
				CmdLen = 8;
				buf = stdmodel[CurPad];
				buf[4] = g.PadState[CurPad].PadMode;
				return 0xF3;

			case CMD_QUERY_ACT:
				CmdLen = 8;
				buf = unk46[CurPad];
				return 0xF3;

			case CMD_QUERY_COMB:
				CmdLen = 8;
				buf = unk47[CurPad];
				return 0xF3;

			case CMD_QUERY_MODE:
				CmdLen = 8;
				buf = unk4c[CurPad];
				return 0xF3;

			case CMD_VIBRATION_TOGGLE:
				CmdLen = 8;
				buf = unk4d[CurPad];
				return 0xF3;

			case CMD_READ_DATA_AND_VIBRATE:
			default:
				UpdateInput();

				n = g.PadState[CurPad].KeyStatus;
				n &= g.PadState[CurPad].JoyKeyStatus;

				stdpar[CurPad][2] = n & 0xFF;
				stdpar[CurPad][3] = n >> 8;

				if (g.PadState[CurPad].PadMode == 1) {
					CmdLen = 8;

					stdpar[CurPad][4] = g.PadState[CurPad].AnalogStatus[ANALOG_RIGHT][0];
					stdpar[CurPad][5] = g.PadState[CurPad].AnalogStatus[ANALOG_RIGHT][1];
					stdpar[CurPad][6] = g.PadState[CurPad].AnalogStatus[ANALOG_LEFT][0];
					stdpar[CurPad][7] = g.PadState[CurPad].AnalogStatus[ANALOG_LEFT][1];
				} else {
					CmdLen = 4;
				}

				buf = stdpar[CurPad];
				return g.PadState[CurPad].PadID;
		}
	}

	switch (CurCmd) {
		case CMD_CONFIG_MODE:
			if (CurByte == 2) {
				switch (value) {
					case 0:
						buf[2] = 0;
						buf[3] = 0;
						break;

					case 1:
						buf[2] = 0xFF;
						buf[3] = 0xFF;
						break;
				}
			}
			break;

		case CMD_SET_MODE_AND_LOCK:
			if (CurByte == 2) {
				g.PadState[CurPad].PadMode = value;
				g.PadState[CurPad].PadID = value ? 0x73 : 0x41;
			}
			break;

		case CMD_QUERY_ACT:
			if (CurByte == 2) {
				switch (value) {
					case 0: // default
						buf[5] = 0x02;
						buf[6] = 0x00;
						buf[7] = 0x0A;
						break;

					case 1: // Param std conf change
						buf[5] = 0x01;
						buf[6] = 0x01;
						buf[7] = 0x14;
						break;
				}
			}
			break;

		case CMD_QUERY_MODE:
			if (CurByte == 2) {
				switch (value) {
					case 0: // mode 0 - digital mode
						buf[5] = PSE_PAD_TYPE_STANDARD;
						break;

					case 1: // mode 1 - analog mode
						buf[5] = PSE_PAD_TYPE_ANALOGPAD;
						break;
				}
			}
			break;
	}

	if (CurByte >= CmdLen) return 0;
	return buf[CurByte++];
}

static long PAD_readPort(int num, PadDataS *pad) {
	UpdateInput();

	pad->buttonStatus = (g.PadState[num].KeyStatus & g.PadState[num].JoyKeyStatus);

	// ePSXe different from pcsx, swap bytes
	pad->buttonStatus = (pad->buttonStatus >> 8) | (pad->buttonStatus << 8);

	switch (g.cfg.PadDef[num].Type) {
		case PSE_PAD_TYPE_ANALOGPAD: // Analog Controller SCPH-1150
			pad->controllerType = PSE_PAD_TYPE_ANALOGPAD;
			pad->rightJoyX = g.PadState[num].AnalogStatus[ANALOG_RIGHT][0];
			pad->rightJoyY = g.PadState[num].AnalogStatus[ANALOG_RIGHT][1];
			pad->leftJoyX = g.PadState[num].AnalogStatus[ANALOG_LEFT][0];
			pad->leftJoyY = g.PadState[num].AnalogStatus[ANALOG_LEFT][1];
			break;

		case PSE_PAD_TYPE_STANDARD: // Standard Pad SCPH-1080, SCPH-1150
		default:
			pad->controllerType = PSE_PAD_TYPE_STANDARD;
			break;
	}

	return 0;
}

long PAD_readPort1(PadDataS *pad) {
	return PAD_readPort(0, pad);
}

long PAD_readPort2(PadDataS *pad) {
	return PAD_readPort(1, pad);
}

long PAD_keypressed(void) {
	long s;

	CheckKeyboard();

	s = g.KeyLeftOver;
	g.KeyLeftOver = 0;

	return s;
}

long PAD_configure(void) {
	return 0;
}

void PAD_about(void) {
}


long PAD_test(void) {
	return 0;
}


