#include <nds.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "Marionea.h"
#include "WifiApi.h"


//---------------------------------------------------------------------------------
void VblankHandler(void) {
//---------------------------------------------------------------------------------

}


//---------------------------------------------------------------------------------
void VcountHandler() {
//---------------------------------------------------------------------------------
	inputGetAndSend();
}

volatile bool exitflag = false;

//---------------------------------------------------------------------------------
void powerButtonCB() {
//---------------------------------------------------------------------------------
	exitflag = true;
}

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
	irqSet(IRQ_VCOUNT, VcountHandler);
	irqSet(IRQ_VBLANK, VblankHandler);

	irqEnable( IRQ_VBLANK | IRQ_VCOUNT | IRQ_NETWORK);
	// Start the RTC tracking IRQ
    
    myprintf("Hello from ARM7!\n");
    for (int i = 0; i < 10; i++) {
        swiWaitForVBlank();
    }
    
    myprintf("Initializing Wi-Fi\n");
    extern int getTaskId();
    InitSdk();
    InitWiFi();


	setPowerButtonCB(powerButtonCB);
    
    WiFi_DevIdle();
    WiFi_DevClass1();
    
    myprintf("MlmeScan... ");
    WlMlmeScanCfm *pCfm = (void*)WiFi_MlmeScan(0, (u16[]){0xFFFF, 0xFFFF, 0xFFFF}, 0, 0, 1, (u8[16]){1,7,13,0}, 1000);
    myprintf("%u -> %u\n", pCfm->resultCode, pCfm->bssDescCount);
    
    WlBssDesc *bssDesc = pCfm->bssDescList;
    for (int i = 0; i < pCfm->bssDescCount; i++) {
        myprintf("SSIDLength %u\n", bssDesc->ssidLength);
        
        WlBssDesc bssDescCopy;
        struct WMGameInfo gameInfo;
        if (bssDesc->ssidLength == 0) {
            u16 bssid[3];
            memcpy(&bssDescCopy, bssDesc, sizeof(bssDescCopy));
            #define MIN(a, b) (((a) < (b)) ? (a) : (b))
            
            memcpy(&gameInfo, bssDesc->gameInfo, MIN(bssDesc->gameInfoLength, sizeof(gameInfo)));
            
            WlParamSetAllReq req;
            
            if (bssDesc->rateSet.basic & 2)
                req.rate = 2;
            else
                req.rate = 1;
            
            req.preambleType = (bssDesc->capaInfo & 0x20) != 0;
            myprintf("RateSet %04X %04X\n", bssDesc->rateSet.basic, bssDesc->rateSet.support);
            myprintf("GameInfo %04X\n", bssDesc->gameInfoLength);
            
            if (bssDesc->gameInfoLength)
                req.mode = 2;
            else
                req.mode = 3;
            
            req.retryLimit = 7;
            
            myprintf("CapaInfo %04X\n", bssDesc->capaInfo);
            if (bssDesc->capaInfo & 0x10) {
                req.wepMode = 0; // ???
                req.wepKeyId = 0; // ???
                memset(req.wepKey, 0x50, sizeof(req.wepKey));
                req.authAlgo = 1;
            } else {
                req.wepMode = 0;
                req.wepKeyId = 0;
                memset(req.wepKey, 0, sizeof(req.wepKey));
                req.authAlgo = 0;
            }
            
            req.beaconLostTh = /*16*/ 254;
            req.activeZoneTime = 10;
            req.probeRes = 1;
            req.beaconType = 1;
            req.enableChannel = 0xFFFF; // idk, bitflag probably
            
            // this will be our new mac address lol
            memcpy(req.staMacAdrs, (u16[]){0xA278,0x83A0,0xC825}, sizeof(req.staMacAdrs));
            
            // SSID mask for some reason
            memset(req.ssidMask, 0, 8);
            memset(req.ssidMask+8, 0xFF, 24);
            
            WiFi_DevIdle();
            
            myprintf("ParamSetAll... ");
            WlParamSetCfm *pCfm2 = WiFi_ParamSetAll(&req);
            myprintf("%u\n", pCfm2->resultCode);
            
            WiFi_DevClass1();
            WiFi_MlmePowerMgt(0, 0, 1);
            
            myprintf("%02X:%02X:%02X:%02X:%02X:%02X\n", ((u8*)bssDescCopy.bssid)[0], ((u8*)bssDescCopy.bssid)[1], ((u8*)bssDescCopy.bssid)[2], ((u8*)bssDescCopy.bssid)[3], ((u8*)bssDescCopy.bssid)[4], ((u8*)bssDescCopy.bssid)[5]);
            
            WiFi_DevClass1();
            WiFi_MlmePowerMgt(0, 0, 1);
            
            bssDescCopy.ssidLength = 32;
            memcpy(bssDescCopy.ssid, &gameInfo.ggid, 4);
            memcpy(bssDescCopy.ssid+4, &gameInfo.tgid, 2);
            memset(bssDescCopy.ssid+6, 0, 2);
            memset(bssDescCopy.ssid+8, bssDescCopy.ssid[8], 32-8);
            
            myprintf("MlmeJoin... ");
            WlMlmeJoinCfm *pCfm3 = WiFi_MlmeJoin(2000, &bssDescCopy);
            myprintf("%u\n", pCfm3->resultCode);
            myprintf("MlmeAuth... ");
            WlMlmeAuthCfm *pCfm4 = WiFi_MlmeAuth(bssDescCopy.bssid, 0, 2000);
            myprintf("%u\n", pCfm4->resultCode);
            myprintf("MlmeAss... ");
            WlMlmeAssCfm *pCfm5 = WiFi_MlmeAss(bssDescCopy.bssid, 1, 2000);
            myprintf("%u\n", pCfm5->resultCode);
            WiFi_DevIdle();
            
            break;
        }
        
        bssDesc = (WlBssDesc*)((u16*)bssDesc + bssDesc->length);
    }
    
	// Keep the ARM7 mostly idle
	while (!exitflag) {
		if ( 0 == (REG_KEYINPUT & (KEY_SELECT | KEY_START | KEY_L | KEY_R))) {
			exitflag = true;
		}
        
        void *msg;
        if (OS_ReceiveMessage(&wvrIndicateQueue, &msg, 0)) {
            WlCmdReq *pReq = (WlCmdReq *)msg;
            
            switch (pReq->header.code) {
                case 0x84: {
                    WlMlmeAuthInd *pInd = (WlMlmeAuthInd *)msg;
                    myprintf("== WlMlmeAuthInd ==\nMAC: ");
                    myprintmac(pInd->peerMacAdrs);
                    myprintf("\nALGO: %u\n\n", pInd->algorithm);
                    break;
                }
                    
                case 0x85: {
                    WlMlmeDeAuthInd *pInd = (WlMlmeDeAuthInd *)msg;
                    myprintf("== WlMlmeDeAuthInd ==\nMAC: ");
                    myprintmac(pInd->peerMacAdrs);
                    myprintf("\nREASON: %u\n\n", pInd->reasonCode);
                    break;
                }
                    
                case 0x86: {
                    WlMlmeAssInd *pInd = (WlMlmeAssInd *)msg;
                    myprintf("== WlMlmeAssInd ==\nMAC: ");
                    myprintmac(pInd->peerMacAdrs);
                    myprintf("\nAID: %u\n\n", pInd->aid);
                    myprintf("\nSSIDLEN: %u\n\n", pInd->ssidLength);
                    break;
                }
                    
                case 0x87: {
                    WlMlmeReAssInd *pInd = (WlMlmeReAssInd *)msg;
                    myprintf("== WlMlmeReAssInd ==\nMAC: ");
                    myprintf("\nAID: %u\n\n", pInd->aid);
                    myprintf("\nSSIDLEN: %u\n\n", pInd->ssidLength);
                    break;
                }
                
                case 0x88: {
                    WlMlmeDisAssInd *pInd = (WlMlmeDisAssInd *)msg;
                    myprintf("== WlMlmeDisAssInd ==\nMAC: ");
                    myprintmac(pInd->peerMacAdrs);
                    myprintf("\nREASON: %u\n\n", pInd->reasonCode);
                    break;
                }
                
                case 0x8B: {
                    WlMlmeBeaconLostInd *pInd = (WlMlmeBeaconLostInd *)msg;
                    myprintf("== WlMlmeBeaconLostInd ==\nAP: ");
                    myprintmac(pInd->apMacAdrs);
                    myprintf("\n\n");
                    break;
                }
                
                case 0x8C: {
                    WlMlmeBeaconSendInd *pInd = (WlMlmeBeaconSendInd *)msg;
                    myprintf("== WlMlmeBeaconSendInd ==\n\n");
                    break;
                }
                
                case 0x8D: {
                    WlMlmeBeaconRecvInd *pInd = (WlMlmeBeaconRecvInd *)msg;
                    myprintf("== WlMlmeBeaconRecvInd ==\n\n");
                    break;
                }
            }
            
            free(pReq);
        }
        UpdateWiFi();
        
        //u64 tick = OS_GetTick();
        //fifoSendDatamsg(FIFO_USER_01, sizeof(tick), (u8*)&tick);
        //UpdateWiFi();
		swiWaitForVBlank();
	}
	return 0;
}