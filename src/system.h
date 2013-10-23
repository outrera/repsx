#ifndef __SYSTEM_H__
#define __SYSTEM_H__

int SysInit();							// Init mem and plugins
void SysReset();						// Resets mem
void SysPrintf(const char *fmt, ...);	// Printf used by bios syscalls
void SysMessage(const char *fmt, ...);	// Message used to print msg to users
void *SysLoadLibrary(const char *lib);	// Loads Library
void *SysLoadSym(void *lib, const char *sym);	// Loads Symbol from Library
const char *SysLibError();				// Gets previous error loading sysbols
void SysCloseLibrary(void *lib);		// Closes Library
void SysUpdate();						// Called on VBlank (to update i.e. pads)
void SysRunGui();						// Returns to the Gui
void SysClose();						// Close mem and plugins

#endif
