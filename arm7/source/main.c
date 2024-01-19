#include <nds.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "wifiapi.h"
#include "timer.h"
#include "valarm.h"

volatile bool exitflag = false;

void myprintf(const char *format, ...) {
    static char buffer[128];
    va_list ap;
    va_start(ap, format);
    int length = vsnprintf(buffer, 127, format, ap);
    va_end(ap);
    buffer[length] = 0;
    fifoSendDatamsg(FIFO_USER_03, length+1, buffer);
}

void myprintmac(u16 macAdrs[3]) {
    myprintf("%02X:%02X:%02X:%02X:%02X:%02X", ((u8*)macAdrs)[0], ((u8*)macAdrs)[1], ((u8*)macAdrs)[2], ((u8*)macAdrs)[3], ((u8*)macAdrs)[4], ((u8*)macAdrs)[5]);
}


//---------------------------------------------------------------------------------
int main() {
//---------------------------------------------------------------------------------
	irqInit();
	fifoInit();
	initClockIRQ();
	installSystemFIFO();

	// Start the RTC tracking IRQ
    
    myprintf("Hello from ARM7!\n");
    irqEnable(IRQ_VBLANK);
    initTimer();
    
    myprintf("Initializing Wi-Fi\n");
    InitWiFi();
    
    int ret = WiFi_Initialize(0);
    myprintf("ret %i\n", ret);
    
    static u8 scanBuf[sizeof(WMBssDesc) * 8];
    static u32 recvBuf[0x200], sendBuf[0x200];
    static WMMPParam param;
    static WMMPTmpParam tmpParam;
    
    myprintf("scanbuf %u\n", sizeof(scanBuf));
    u16 bssid[3] = {0xFFFF, 0xFFFF, 0xFFFF};
    ret = WiFi_StartScanEx((1<<1)|(1<<7)|(1<<13), scanBuf, sizeof(scanBuf), 500, bssid, 1, 0, NULL, 0);
    myprintf("ret %i\n", ret);
    
    int ret2 = WiFi_EndScan();
    myprintf("endscan %i\n", ret2);
    
    WMBssDesc *bssDesc = NULL;
    
    if (ret < 0) {
        u8 *buf = scanBuf;
        for (int i = 0; i < -ret; i++) {
            WMBssDesc *desc = (WMBssDesc *)buf;
            buf += (desc->length * 2);
            
            myprintf("desc->gameInfoLength %i\n", desc->gameInfoLength);
            if (desc->gameInfoLength) {
                bssDesc = desc;
                break;
            }
        }
    }
    
    if (bssDesc) {
        u8 ssid[24];
        memset(ssid, 0, sizeof(ssid));
        ret = WiFi_StartConnectEx(bssDesc, ssid, 0, 0);
        myprintf("ret %i\n", ret);
        
    }
    
	// Keep the ARM7 mostly idle
	while (!exitflag) {
		if ( 0 == (REG_KEYINPUT & (KEY_SELECT | KEY_START | KEY_L | KEY_R))) {
			exitflag = true;
		}
        
        UpdateWiFi();
		swiWaitForVBlank();
	}
	return 0;
}