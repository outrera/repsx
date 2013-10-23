#include "plugins.h"

void clearDynarec(void) {
	psxCpu->Reset();
}

int LoadPlugins() {
	int ret;

	ReleasePlugins();
  cdrIsoInit();

  Config.UseNet = FALSE;

	ret = CDR_init();
	if (ret < 0) { SysMessage (_("Error initializing CD-ROM plugin: %d"), ret); return -1; }
	ret = GPU_init();
	if (ret < 0) { SysMessage (_("Error initializing GPU plugin: %d"), ret); return -1; }
	ret = SPU_init();
	if (ret < 0) { SysMessage (_("Error initializing SPU plugin: %d"), ret); return -1; }
	ret = PAD_init(1);
	if (ret < 0) { SysMessage (_("Error initializing Controller plugin: %d"), ret); return -1; }
	if (Config.UseNet) {
		ret = NET_init();
		if (ret < 0) { SysMessage (_("Error initializing NetPlay plugin: %d"), ret); return -1; }
	}

	ret = SIO1_init();
	if (ret < 0) { SysMessage (_("Error initializing SIO1 plugin: %d"), ret); return -1; }


	return 0;
}

unsigned long gpuDisp;
int OpenPlugins() {
	int ret;

	//signal(SIGINT, SignalExit);
	//signal(SIGPIPE, SignalExit);

	GPU_clearDynarec(clearDynarec);

  ret = CDR_open();
	if (ret < 0) { SysMessage("Error Opening CDR Plugin"); return -1; }
	ret = SPU_open();
	if (ret < 0) { SysMessage("Error Opening SPU Plugin"); return -1; }
	SPU_registerCallback(SPUirq);
	ret = GPU_open(&gpuDisp, "PCSX", NULL);
	if (ret < 0) { SysMessage("Error Opening GPU Plugin"); return -1; }
	ret = PAD_open(&gpuDisp);
	if (ret < 0) { SysMessage("Error Opening PAD Plugin"); return -1; }

	return 0;
}

void ClosePlugins() {
	int ret;

	//signal(SIGINT, SIG_DFL);
	//signal(SIGPIPE, SIG_DFL);
	ret = CDR_close();
	if (ret < 0) { SysMessage(_("Error Closing CDR Plugin")); return; }
	ret = SPU_close();
	if (ret < 0) { SysMessage(_("Error Closing SPU Plugin")); return; }
	ret = PAD_close();
	if (ret < 0) { SysMessage(_("Error Closing PAD Plugin")); return; }
	ret = GPU_close();
	if (ret < 0) { SysMessage(_("Error Closing GPU Plugin")); return; }
}


void ReleasePlugins() {
	NET_close();
	NetOpened = FALSE;
  SIO1_shutdown();
}

