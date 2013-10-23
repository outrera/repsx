#include "plugins.h"

long SIO1_init(void) { return 0; }
long SIO1_shutdown(void) { return 0; }
long SIO1_open(unsigned long *x) { return 0; }
long SIO1_close(void) { return 0; }
long SIO1_configure(void) { return 0; }
long SIO1_test(void) { return 0; }
void SIO1_about(void) {}
void SIO1_pause(void) {}
void SIO1_resume(void) {}
long SIO1_keypressed(int key) { return 0; }
void SIO1_writeData8(unsigned char val) {}
void SIO1_writeData16(unsigned short val) {}
void SIO1_writeData32(unsigned long val) {}
void SIO1_writeStat16(unsigned short val) {}
void SIO1_writeStat32(unsigned long val) {}
void SIO1_writeMode16(unsigned short val) {}
void SIO1_writeMode32(unsigned long val) {}
void SIO1_writeCtrl16(unsigned short val) {}
void SIO1_writeCtrl32(unsigned long val) {}
void SIO1_writeBaud16(unsigned short val) {}
void SIO1_writeBaud32(unsigned long val) {}
unsigned char SIO1_readData8(void) { return 0; }
unsigned short SIO1_readData16(void) { return 0; }
unsigned long SIO1_readData32(void) { return 0; }
unsigned short SIO1_readStat16(void) { return 0; }
unsigned long SIO1_readStat32(void) { return 0; }
unsigned short SIO1_readMode16(void) { return 0; }
unsigned long SIO1_readMode32(void) { return 0; }
unsigned short SIO1_readCtrl16(void) { return 0; }
unsigned long SIO1_readCtrl32(void) { return 0; }
unsigned short SIO1_readBaud16(void) { return 0; }
unsigned long SIO1_readBaud32(void) { return 0; }
void SIO1_registerCallback(void (*callback)(void)) {};
void SIO1_irq(void) {psxHu32ref(0x1070) |= SWAPu32(0x100);}
