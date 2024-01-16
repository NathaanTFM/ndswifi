#include <nds.h>
#include <stdlib.h>
#include <string.h>

#define MARIONEA_INTERNAL 1
#include "wifiapi.h"

extern void myprintf(const char *format, ...);

#undef REG_IME
#undef REG_IE
#undef REG_IF
#undef REG_VCOUNT
#undef REG_POWERCNT

u32 sharedMsgBuf[512];
u32 wvrWlWork[448]; // :80
u32 wvrWlStack[384]; // :81
WlStaElement wvrWlStaElement[16]; // :82
struct OSMessageQueue wvrSendMsgQueue, wvrRecvMsgQueue, wvrConfirmQueue, wvrIndicateQueue;

void *sendArr[20], *recvArr[20], *cfmArr[20], *indArr[20];

#define GET_REQ_CFM(reqType, reqLength, cfmType, cfmLength) \
    reqType *pReq = (reqType *)sharedMsgBuf; \
    cfmType *pCfm; \
    memset(pReq->wlRsv, 0, sizeof(pReq->wlRsv)); \
    pReq->header.length = (reqLength); \
    pCfm = (cfmType *)GET_CFM(pReq); \
    pCfm->header.length = (cfmLength);
    
#define GET_REQ_CFM_AUTO(reqType, cfmType) \
    GET_REQ_CFM(reqType, (sizeof(reqType) - 0x10) / 2, cfmType, (sizeof(cfmType) - 0x04) / 2)
    
#define AUTO_INIT(type) \
    GET_REQ_CFM_AUTO(type ## Req, type ## Cfm)

static void execute(void *req) {
    void *msg;
    void *tmp;
    
    while (!OS_SendMessage(&wvrSendMsgQueue, req, 0)) UpdateWiFi();
    
    while (!OS_ReceiveMessage(&wvrConfirmQueue, &msg, 0)) {
        // we don't have thread contexts; update wifi
        UpdateWiFi();
    }
}

WlMlmeResetCfm * WiFi_MlmeReset(u16 mib) {
    AUTO_INIT(WlMlmeReset);
    pReq->header.code = pCfm->header.code = 0;
    pReq->mib = mib;
    execute(pReq);
    return pCfm;
}
WlMlmePowerMgtCfm * WiFi_MlmePowerMgt(u16 pwrMgtMode, u16 wakeUp, u16 recieveDtims) {
    AUTO_INIT(WlMlmePowerMgt);
    pReq->header.code = pCfm->header.code = 1;
    pReq->pwrMgtMode = pwrMgtMode;
    pReq->wakeUp = wakeUp;
    pReq->recieveDtims = recieveDtims;
    execute(pReq);
    return pCfm;
}
WlMlmeScanCfm * WiFi_MlmeScan(u32 bufSize, u16 *bssid, u16 ssidLength, u8 *ssid, u16 scanType, u8 *channelList, u16 maxChannelTime) {
    GET_REQ_CFM(WlMlmeScanReq, 0x31, WlMlmeScanCfm, sizeof(sharedMsgBuf) / 2 - pReq->header.length - 0x20);
    pReq->header.code = pCfm->header.code = 2;
    memcpy(pReq->bssid, bssid, sizeof(pReq->bssid));
    pReq->ssidLength = ssidLength;
    memcpy(pReq->ssid, ssid, ssidLength);
    pReq->scanType = scanType;
    memcpy(pReq->channelList, channelList, sizeof(pReq->channelList));
    pReq->maxChannelTime = maxChannelTime;
    pReq->bssidMaskCount = 0;
    
    execute(pReq);
    return pCfm;
}
WlMlmeJoinCfm * WiFi_MlmeJoin(u16 timeOut, WlBssDesc *bssDesc) {
    AUTO_INIT(WlMlmeJoin);
    pReq->header.code = pCfm->header.code = 3;
    pReq->timeOut = timeOut;
    memcpy(&pReq->bssDesc, bssDesc, sizeof(pReq->bssDesc));
    execute(pReq);
    return pCfm;
}
WlMlmeAuthCfm * WiFi_MlmeAuth(u16 *peerMacAdrs, u16 algorithm, u16 timeOut) {
    AUTO_INIT(WlMlmeAuth);
    pReq->header.code = pCfm->header.code = 4;
    memcpy(pReq->peerMacAdrs, peerMacAdrs, sizeof(pReq->peerMacAdrs));
    pReq->algorithm = algorithm;
    pReq->timeOut = timeOut;
    execute(pReq);
    return pCfm;
}
WlMlmeDeAuthCfm * WiFi_MlmeDeAuth(u16 *peerMacAdrs, u16 reasonCode) {
    AUTO_INIT(WlMlmeDeAuth);
    pReq->header.code = pCfm->header.code = 5;
    memcpy(pReq->peerMacAdrs, peerMacAdrs, sizeof(pReq->peerMacAdrs));
    pReq->reasonCode = reasonCode;
    execute(pReq);
    return pCfm;
}
WlMlmeAssCfm * WiFi_MlmeAss(u16 *peerMacAdrs, u16 listenInterval, u16 timeOut) {
    AUTO_INIT(WlMlmeAss);
    pReq->header.code = pCfm->header.code = 6;
    memcpy(pReq->peerMacAdrs, peerMacAdrs, sizeof(pReq->peerMacAdrs));
    pReq->listenInterval = listenInterval;
    pReq->timeOut = timeOut;
    execute(pReq);
    return pCfm;
}
WlMlmeStartCfm * WiFi_MlmeStart(u16 ssidLength, u8 *ssid, u16 beaconPeriod, u16 dtimPeriod, u16 channel, u16 basicRateSet, u16 supportRateSet, u16 gameInfoLength, struct WMGameInfo *gameInfo) {
    GET_REQ_CFM(WlMlmeStartReq, 0x17 + ((gameInfoLength + 1) / 2), WlMlmeStartCfm, 1);
    pReq->header.code = pCfm->header.code = 9;
    pReq->ssidLength = ssidLength;
    memcpy(pReq->ssid, ssid, ssidLength);
    pReq->beaconPeriod = beaconPeriod;
    pReq->dtimPeriod = dtimPeriod;
    pReq->channel = channel;
    pReq->basicRateSet = basicRateSet;
    pReq->supportRateSet = supportRateSet;
    pReq->gameInfoLength = gameInfoLength;
    memcpy(pReq->gameInfo, gameInfo, gameInfoLength);
    execute(pReq);
    return pCfm;
}
WlMlmeMeasChanCfm * WiFi_MlmeMeasChan(u16 ccaMode, u16 edThreshold, u16 measureTime, u8 *channelList) {
    AUTO_INIT(WlMlmeMeasChan);
    pReq->header.code = pCfm->header.code = 10;
    pReq->ccaMode = ccaMode;
    pReq->edThreshold = edThreshold;
    pReq->measureTime = measureTime;
    memcpy(pReq->channelList, channelList, sizeof(pReq->channelList));
    execute(pReq);
    return pCfm;
}
WlMaDataCfm * WiFi_MaData(WlTxFrame *frame) {
    AUTO_INIT(WlMaData);
    pReq->header.code = pCfm->header.code = 0x100 + 0;
    memcpy(&pReq->frame, frame, sizeof(pReq->frame));
    execute(pReq);
    return pCfm;
}
WlMaKeyDataCfm * WiFi_MaKeyData(u16 length, u16 wmHeader, u16 *keyDatap) {
    AUTO_INIT(WlMaKeyData);
    pReq->header.code = pCfm->header.code = 0x100 + 1;
    pReq->length = length;
    pReq->wmHeader = wmHeader;
    pReq->keyDatap = keyDatap;
    execute(pReq);
    return pCfm;
}
WlMaMpCfm * WiFi_MaMp(u16 resume, u16 retryLimit, u16 txop, u16 pollBitmap, u16 tmptt, u16 currTsf, u16 dataLength, u16 wmHeader, u16 *datap) {
    AUTO_INIT(WlMaMp);
    pReq->header.code = pCfm->header.code = 0x100 + 2;
    pReq->resume = resume;
    pReq->retryLimit = retryLimit;
    pReq->txop = txop;
    pReq->pollBitmap = pollBitmap;
    pReq->tmptt = tmptt;
    pReq->currTsf = currTsf;
    pReq->dataLength = dataLength;
    pReq->wmHeader = wmHeader;
    pReq->datap = datap;
    execute(pReq);
    return pCfm;
}
WlMaClrDataCfm * WiFi_MaClrData(u16 flag) {
    AUTO_INIT(WlMaClrData);
    pReq->header.code = pCfm->header.code = 0x100 + 4;
    pReq->flag = flag;
    execute(pReq);
    return pCfm;
}
WlParamSetCfm * WiFi_ParamSetAll(WlParamSetAllReq *req) {
    GET_REQ_CFM_AUTO(WlParamSetAllReq, WlParamSetCfm);
    pReq->header.code = pCfm->header.code = 0x200 + 0;
    memcpy((u8*)pReq+offsetof(WlParamSetAllReq, staMacAdrs), (u8*)req+offsetof(WlParamSetAllReq, staMacAdrs), sizeof(WlParamSetAllReq)-offsetof(WlParamSetAllReq, staMacAdrs));
    execute(pReq);
    return pCfm;
}
WlParamSetCfm * WiFi_ParamSetWepKeyId(u16 wepKeyId) {
    GET_REQ_CFM_AUTO(WlParamSetWepKeyIdReq, WlParamSetCfm);
    pReq->header.code = pCfm->header.code = 0x200 + 7;
    pReq->wepKeyId = wepKeyId;
    execute(pReq);
    return pCfm;
}
WlParamSetCfm * WiFi_ParamSetBeaconLostTh(u16 beaconLostTh) {
    GET_REQ_CFM_AUTO(WlParamSetBeaconLostThReq, WlParamSetCfm);
    pReq->header.code = pCfm->header.code = 0x200 + 11;
    pReq->beaconLostTh = beaconLostTh;
    execute(pReq);
    return pCfm;
}
WlParamSetCfm * WiFi_ParamSetSsidMask(u8 *mask) {
    GET_REQ_CFM_AUTO(WlParamSetSsidMaskReq, WlParamSetCfm);
    pReq->header.code = pCfm->header.code = 0x200 + 13;
    memcpy(pReq->mask, mask, sizeof(pReq->mask));
    execute(pReq);
    return pCfm;
}
WlParamSetCfm * WiFi_ParamSetPreambleType(u16 type) {
    GET_REQ_CFM_AUTO(WlParamSetPreambleTypeReq, WlParamSetCfm);
    pReq->header.code = pCfm->header.code = 0x200 + 14;
    pReq->type = type;
    execute(pReq);
    return pCfm;
}
WlParamSetCfm * WiFi_ParamSetLifeTime(u16 tableNumber, u16 camLifeTime, u16 frameLifeTime) {
    GET_REQ_CFM_AUTO(WlParamSetLifeTimeReq, WlParamSetCfm);
    pReq->header.code = pCfm->header.code = 0x200 + 17;
    pReq->tableNumber = tableNumber;
    pReq->camLifeTime = camLifeTime;
    pReq->frameLifeTime = frameLifeTime;
    execute(pReq);
    return pCfm;
}
WlParamSetCfm * WiFi_ParamSetMaxConn(u16 count) {
    GET_REQ_CFM_AUTO(WlParamSetMaxConnReq, WlParamSetCfm);
    pReq->header.code = pCfm->header.code = 0x200 + 18;
    pReq->count = count;
    execute(pReq);
    return pCfm;
}
WlParamSetCfm * WiFi_ParamSetBeaconSendRecvInd(u16 enableMessage) {
    GET_REQ_CFM_AUTO(WlParamSetBeaconSendRecvIndReq, WlParamSetCfm);
    pReq->header.code = pCfm->header.code = 0x200 + 21;
    pReq->enableMessage = enableMessage;
    execute(pReq);
    return pCfm;
}
WlParamSetCfm * WiFi_ParamSetNullKeyMode(u16 mode) {
    GET_REQ_CFM_AUTO(WlParamSetNullKeyModeReq, WlParamSetCfm);
    pReq->header.code = pCfm->header.code = 0x200 + 22;
    pReq->mode = mode;
    execute(pReq);
    return pCfm;
}
WlParamSetCfm * WiFi_ParamSetBeaconPeriod(u16 beaconPeriod) {
    GET_REQ_CFM_AUTO(WlParamSetBeaconPeriodReq, WlParamSetCfm);
    pReq->header.code = pCfm->header.code = 0x200 + 0x40 + 2;
    pReq->beaconPeriod = beaconPeriod;
    execute(pReq);
    return pCfm;
}
WlParamSetCfm * WiFi_ParamSetGameInfo(u16 gameInfoLength, u16 *gameInfo) {
    GET_REQ_CFM(WlParamSetGameInfoReq, 1 + ((gameInfoLength + 1) / 2), WlParamSetCfm, 1);
    pReq->header.code = pCfm->header.code = 0x200 + 0x40 + 5;
    pReq->gameInfoLength = gameInfoLength;
    memcpy(pReq->gameInfo, gameInfo, gameInfoLength);
    execute(pReq);
    return pCfm;
}
WlParamGetMacAddressCfm * WiFi_ParamGetMacAddress() {
}
WlParamGetEnableChannelCfm * WiFi_ParamGetEnableChannel() {
}
WlParamGetModeCfm * WiFi_ParamGetMode() {
}
WlDevShutdownCfm * WiFi_DevShutdown() {
    GET_REQ_CFM_AUTO(WlCmdReq, WlDevShutdownCfm);
    pReq->header.code = pCfm->header.code = 0x300 + 1;
    execute(pReq);
    return pCfm;
}
WlDevIdleCfm * WiFi_DevIdle() {
    GET_REQ_CFM_AUTO(WlCmdReq, WlDevIdleCfm);
    pReq->header.code = pCfm->header.code = 0x300 + 2;
    execute(pReq);
    return pCfm;
}
WlDevClass1Cfm * WiFi_DevClass1() {
    GET_REQ_CFM_AUTO(WlCmdReq, WlDevClass1Cfm);
    pReq->header.code = pCfm->header.code = 0x300 + 3;
    execute(pReq);
    return pCfm;
}
WlDevRebootCfm * WiFi_DevReboot() {
    GET_REQ_CFM_AUTO(WlCmdReq, WlDevRebootCfm);
    pReq->header.code = pCfm->header.code = 0x300 + 4;
    execute(pReq);
    return pCfm;
}
WlDevClrInfoCfm * WiFi_DevClrInfo() {
    GET_REQ_CFM_AUTO(WlCmdReq, WlDevClrInfoCfm);
    pReq->header.code = pCfm->header.code = 0x300 + 5;
    execute(pReq);
    return pCfm;
}
WlDevGetVerInfoCfm * WiFi_DevGetVerInfo() {
    GET_REQ_CFM_AUTO(WlCmdReq, WlDevGetVerInfoCfm);
    pReq->header.code = pCfm->header.code = 0x300 + 6;
    execute(pReq);
    return pCfm;
}
WlDevGetInfoCfm * WiFi_DevGetInfo() {
    GET_REQ_CFM_AUTO(WlCmdReq, WlDevGetInfoCfm);
    pReq->header.code = pCfm->header.code = 0x300 + 7;
    execute(pReq);
    return pCfm;
}
WlDevGetStateCfm * WiFi_DevGetState() {
    GET_REQ_CFM_AUTO(WlCmdReq, WlDevGetStateCfm);
    pReq->header.code = pCfm->header.code = 0x300 + 8;
    execute(pReq);
    return pCfm;
}

static u32 my_alloc(u32 sz) {
    void *ret = (u32)malloc(sz);
    //myprintf("ac: %lu @ %p\n", sz, ret);
    return ret;
}

static u32 my_free(void *ptr) {
    free(ptr);
}

void InitWiFi() {
    InitMsgQueue(&wvrSendMsgQueue, sendArr, 20);
    InitMsgQueue(&wvrRecvMsgQueue, recvArr, 20);
    InitMsgQueue(&wvrConfirmQueue, cfmArr, 20);
    InitMsgQueue(&wvrIndicateQueue, indArr, 20);
    
    WlInit wlInit; 
    wlInit.workingMemAdrs = (u32)wvrWlWork;
    wlInit.stack = (void *)((u32)wvrWlStack + sizeof(wvrWlStack));
    wlInit.stacksize = sizeof(wvrWlStack);
    
    wlInit.sendMsgQueuep = &wvrSendMsgQueue;
    wlInit.recvMsgQueuep = &wvrRecvMsgQueue;
    
    wlInit.heapType = 1;
    wlInit.heapFunc.ext.alloc = my_alloc;
    wlInit.heapFunc.ext.free = my_free;
    
    wlInit.camAdrs = wvrWlStaElement;
    wlInit.camSize = sizeof(wvrWlStaElement);
    
    u32 ret = WL_InitDriver(&wlInit);
    
    // then the loop starts with this
    TASK_MAN* pTaskMan = &wlMan->TaskMan; // r8 - :140
    
    // then the loop starts with this
    pTaskMan->NextPri = 0;
    pTaskMan->CurrTaskID = 0;
}

static void WiFiLoop() {
    TASK_MAN* pTaskMan = &wlMan->TaskMan; // r8 - :140
    void* msg; // None - :150
    if (OS_ReceiveMessage(wlMan->pRecvMsgQueue, &msg, 0)) {
        ExecuteMessage(&msg);
    }
    
    u32 x = OS_DisableIrqMask(0x1000010);
    pTaskMan->TaskPri = pTaskMan->NextPri;
    
    if (pTaskMan->EnQ[pTaskMan->TaskPri] == 0xFFFF) {
        if (pTaskMan->NextPri < 3) { // bugfix
            pTaskMan->NextPri++;
        }
        OS_EnableIrqMask(x);
        
    } else {
        OS_EnableIrqMask(x);
        pTaskMan->CurrTaskID = DeleteTask(pTaskMan->TaskPri);
        pTaskMan->TaskTbl[pTaskMan->CurrTaskID].pTaskFunc();
        pTaskMan->CurrTaskID = 0xFFFF;
    }
}

void UpdateWiFi() { // TaskMan.c:136
    // Call the WiFi loop
    WiFiLoop();
    
    // Check for messages
    void *msg;
    while (OS_ReceiveMessage(&wvrRecvMsgQueue, &msg, 0)) {
        WlCmdReq *pReq = (WlCmdReq *)msg;
        if ((pReq->header.code & ~0x17F) == 0x80) {
            myprintf("msg %p (code %u)\n", msg, pReq->header.code);
            if (!OS_SendMessage(&wvrIndicateQueue, msg, 0))
                myprintf("Indicate queue is full!\n");
                
        } else {
            if (!OS_SendMessage(&wvrConfirmQueue, msg, 0))
                myprintf("Confirm queue is full!\n");
        }
    }
    
}

/*

    WlMlmeScanReq *pReq = (WlMlmeScanReq*)msg;
    WlMlmeScanCfm *pCfm = (WlMlmeScanCfm*)GET_CFM(pReq);
    
    /*fifoSendValue32(FIFO_USER_02, (u32)pCfm->header.code);
    if (pCfm->header.code == 2) {
        fifoSendValue32(FIFO_USER_02, (u32)pCfm->header.length);
        fifoSendValue32(FIFO_USER_02, (u32)pCfm->resultCode);
        fifoSendValue32(FIFO_USER_02, (u32)pCfm->bssDescCount);
        
    }*/
    /*fifoSendValue32(FIFO_USER_02, (u32)pReq);
    fifoSendValue32(FIFO_USER_02, (u32)pReq->header.code);
    fifoSendValue32(FIFO_USER_02, (u32)pReq->header.length);
    fifoSendValue32(FIFO_USER_02, (u32)pCfm);*/
    
/*
    if (pCfm->header.code == 2) {
        for (int i = 0; i < pCfm->bssDescCount; i++) {
            fifoSendDatamsg(FIFO_USER_02, pCfm->bssDescList[i].ssidLength, pCfm->bssDescList[i].ssid);
        }
    }
*/


int getTaskId() {
    return wlMan->TaskMan.CurrTaskID;
}