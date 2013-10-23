EXE = repsx
PREFIX =


CC = $(PREFIX)gcc
AS = $(CC)
LD = $(CC)
AR = $(PREFIX)ar rcs
STRIP = $(PREFIX)strip
CP = cp -fp
RM = rm -f

CFLAGS = $(shell sdl-config --cflags) -O2 -Wall
ASFLAGS = -Wall
LDFLAGS = -lz $(shell sdl-config --libs) 
ARFLAGS =


OBJS = gte.o common.o mem.o cdrom.o iso.o mdec.o counters.o r3000a.o \
       cheat.o misc.o dma.o sio.o debug.o plugins.o hle.o socket.o \
       xa.o ppf.o hw.o r3000a_dis.o bios.o interpreter.o gpu.o scalers.o \
       sdl_spu.o sdl_pad.o sdl_net.o sdl_sio1.o getopt.o sdl_text.o sdl_main.o


all: $(EXE)

.c.o:
	$(CC) $(CFLAGS) -c -o $@ $<

.S.o:
	$(AS) $(ASFLAGS) -c -o $@ $<

$(EXE): $(OBJS)
	$(LD) $(LDFLAGS) $(OBJS) -o $@

.PHONY: clean


clean:
	-$(RM) $(EXE)
	-$(RM) *.o




