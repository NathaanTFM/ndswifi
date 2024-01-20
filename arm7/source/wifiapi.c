#include <stdlib.h>
#include <string.h>

#define MARIONEA_INTERNAL 1
#include "wifiapi.h"
#include "alarm.h"
#include "valarm.h"
#include "timer.h"

/* Commands */
static struct WiFiWork {
    OSMessageQueue sendMsgQueue;
    OSMessageQueue recvMsgQueue;
    OSMessageQueue confirmQueue;
    OSMessageQueue requestQueue;
    
    /* 1: MP parent 2: MP child  3: ??? child */
    u16 mode;
    u16 state;
    u16 rate;
    u16 enableChannel;
    u16 allowedChannel;
    u16 preamble;
    u16 wep_flag;
    u16 wepMode, wepKeyId;
    u16 wepKey[4][10];
    u16 pwrMgtMode;
    u16 parentMacAddress[3];
    u16 aid;
    u16 curr_tgid;
    u16 linkLevel; // todo
    u16 child_bitmap; // list of children, or 1 if mp child
    u16 beaconIndicateFlag;
    u16 sendQueueInUse;
    u16 minRssi;
    u16 rssiCounter;
    u16 valarm_queuedFlag;
    u16 VSyncFlag;
    u16 MacAddress[3];
    u32 miscFlags;
    u16 portSeqNo[16][8];
    u16 v_tsf;
    u16 v_tsf_bak;
    u16 v_remain;
    u16 childMacAddress[15][6];
    u16 ks_flag;
    u32 dcf_flag;
    u16 valarm_counter;
    u16 scan_channel; // most likely unused
    
    // multiplayer related
    u16 mp_flag; // 1 if we're currently in MP
    u16 mp_readyBitmap;
    u64 mp_lifeTimeTick;
    u64 mp_lastRecvTick[16]; // one per aid?
    u16 mp_parentSize;
    u16 mp_parentMaxSize;
    u16 mp_childSize;
    u16 mp_childMaxSize;
    u16 mp_sendSize;
    u16 mp_maxSendSize;
    u16 mp_recvSize;
    u16 mp_maxRecvSize;
    u16 mp_waitAckFlag;
    u16 mp_vsyncOrderedFlag;
    u16 mp_vsyncFlag;
    u16 mp_newFrameFlag;
    u16 mp_pingFlag;
    u16 mp_pingCounter;
    u16 mp_sentDataFlag;
    u16 mp_bufferEmptyFlag;
    u16 mp_resumeFlag;
    WMMpRecvBuf *mp_recvBuf[2];
    u16 mp_recvBufSize;
    u16 mp_recvBufSel;
    u16 mp_isPolledFlag;
    u32 *mp_sendBuf;
    u16 mp_sendBufSize;
    u16 mp_count;
    u16 mp_limitCount;
    u16 mp_prevPollBitmap;
    u16 mp_prevWmHeader;
    u16 mp_freq;
    u16 mp_minFreq;
    u16 mp_maxFreq;
    u16 mp_childInterval;
    u64 mp_childIntervalTick;
    u16 mp_parentInterval;
    u64 mp_parentIntervalTick;
    u16 mp_parentVCount;
    u16 mp_childVCount;
    u16 mp_defaultRetryCount;
    u16 mp_minPollBmpMode;
    u16 mp_singlePacketMode;
    u16 mp_ignoreFatalErrorMode;
    u16 mp_ignoreSizePrecheckMode;
    u16 mp_current_freq;
    u16 mp_current_minFreq;
    u16 mp_current_maxFreq;
    u16 mp_current_defaultRetryCount;
    u16 mp_current_ignoreFatalErrorMode;
    u16 mp_current_minPollBmpMode;
    u16 mp_current_singlePacketMode;
    u16 mp_ackTime;
    
    WMParentParam pparam;
    
    // contains the bssdesc we're connecting to
    WMBssDesc bssDesc;
    
    // buffer for scanning?
    WMBssDesc *infoBuf;
    
    // alarms
    VAlarm valarm;
    Alarm mpAckAlarm, mpIntervalAlarm;
    
    
} wifiWork;

extern void myprintf(const char *format, ...);

static void StabilizeBeacon();
static void SetVTSF();
static void CalcVRemain();
static void ExpandVRemain();
static void FromVAlarmToMainThread();
static void ParentVAlarmMP(void *unused);
static void ParentAdjustVSync(void *unused);
static void ChildVAlarmMP(void *unused);
static void ChildAdjustVSync2(void *unused);
static void ChildAdjustVSync1(void *unused);

static void memset16(void *target, u16 value, size_t length) {
    u16 *target16 = (u16 *)target;
    while (length > 0) {
        *(target16++) = value;
        length -= 2;
    }
}

static void memset32(void *target, u32 value, size_t length) {
    u32 *target32 = (u32 *)target;
    while (length > 0) {
        *(target32++) = value;
        length -= 4;
    }
}

#undef REG_IME
#undef REG_IE
#undef REG_IF
#undef REG_VCOUNT
#undef REG_POWERCNT
#include <nds.h>

u32 sharedMsgBuf[512];
u32 wvrWlWork[448]; // :80
u32 wvrWlStack[384]; // :81
WlStaElement wvrWlStaElement[16]; // :82

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
    
    while (!OS_SendMessage(&wifiWork.sendMsgQueue, req, 0)) UpdateWiFi();
    
    while (!OS_ReceiveMessage(&wifiWork.confirmQueue, &msg, 0)) {
        // we don't have thread contexts; update wifi
        UpdateWiFi();
    }
}

WlMlmeResetCfm * WiFi_API_MlmeReset(u16 mib) {
    AUTO_INIT(WlMlmeReset);
    pReq->header.code = pCfm->header.code = 0;
    pReq->mib = mib;
    execute(pReq);
    return pCfm;
}
WlMlmePowerMgtCfm * WiFi_API_MlmePowerMgt(u16 pwrMgtMode, u16 wakeUp, u16 recieveDtims) {
    AUTO_INIT(WlMlmePowerMgt);
    pReq->header.code = pCfm->header.code = 1;
    pReq->pwrMgtMode = pwrMgtMode;
    pReq->wakeUp = wakeUp;
    pReq->recieveDtims = recieveDtims;
    execute(pReq);
    return pCfm;
}
WlMlmeScanCfm * WiFi_API_MlmeScan(u16 *bssid, u16 ssidLength, u8 *ssid, u16 scanType, u8 *channelList, u16 maxChannelTime) {
    GET_REQ_CFM(WlMlmeScanReq, 0x31, WlMlmeScanCfm, sizeof(sharedMsgBuf) / 2 - pReq->header.length - 0x20);
    pReq->header.code = pCfm->header.code = 2;
    memcpy(pReq->bssid, bssid, sizeof(pReq->bssid));
    pReq->ssidLength = ssidLength;
    if (ssid)
        memcpy(pReq->ssid, ssid, ssidLength);
    pReq->scanType = scanType;
    memcpy(pReq->channelList, channelList, sizeof(pReq->channelList));
    pReq->maxChannelTime = maxChannelTime;
    pReq->bssidMaskCount = 0;
    
    execute(pReq);
    return pCfm;
}
WlMlmeJoinCfm * WiFi_API_MlmeJoin(u16 timeOut, WlBssDesc *bssDesc) {
    AUTO_INIT(WlMlmeJoin);
    pReq->header.code = pCfm->header.code = 3;
    pReq->timeOut = timeOut;
    memcpy(&pReq->bssDesc, bssDesc, sizeof(pReq->bssDesc));
    execute(pReq);
    return pCfm;
}
WlMlmeAuthCfm * WiFi_API_MlmeAuth(u16 *peerMacAdrs, u16 algorithm, u16 timeOut) {
    AUTO_INIT(WlMlmeAuth);
    pReq->header.code = pCfm->header.code = 4;
    memcpy(pReq->peerMacAdrs, peerMacAdrs, sizeof(pReq->peerMacAdrs));
    pReq->algorithm = algorithm;
    pReq->timeOut = timeOut;
    execute(pReq);
    return pCfm;
}
WlMlmeDeAuthCfm * WiFi_API_MlmeDeAuth(u16 *peerMacAdrs, u16 reasonCode) {
    AUTO_INIT(WlMlmeDeAuth);
    pReq->header.code = pCfm->header.code = 5;
    memcpy(pReq->peerMacAdrs, peerMacAdrs, sizeof(pReq->peerMacAdrs));
    pReq->reasonCode = reasonCode;
    execute(pReq);
    return pCfm;
}
WlMlmeAssCfm * WiFi_API_MlmeAss(u16 *peerMacAdrs, u16 listenInterval, u16 timeOut) {
    AUTO_INIT(WlMlmeAss);
    pReq->header.code = pCfm->header.code = 6;
    memcpy(pReq->peerMacAdrs, peerMacAdrs, sizeof(pReq->peerMacAdrs));
    pReq->listenInterval = listenInterval;
    pReq->timeOut = timeOut;
    execute(pReq);
    return pCfm;
}
WlMlmeStartCfm * WiFi_API_MlmeStart(u16 ssidLength, u8 *ssid, u16 beaconPeriod, u16 dtimPeriod, u16 channel, u16 basicRateSet, u16 supportRateSet, u16 gameInfoLength, struct WMGameInfo *gameInfo) {
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
WlMlmeMeasChanCfm * WiFi_API_MlmeMeasChan(u16 ccaMode, u16 edThreshold, u16 measureTime, u8 *channelList) {
    AUTO_INIT(WlMlmeMeasChan);
    pReq->header.code = pCfm->header.code = 10;
    pReq->ccaMode = ccaMode;
    pReq->edThreshold = edThreshold;
    pReq->measureTime = measureTime;
    memcpy(pReq->channelList, channelList, sizeof(pReq->channelList));
    execute(pReq);
    return pCfm;
}
WlMaDataCfm * WiFi_API_MaData(WlTxFrame *frame) {
    AUTO_INIT(WlMaData);
    pReq->header.code = pCfm->header.code = 0x100 + 0;
    memcpy(&pReq->frame, frame, sizeof(pReq->frame));
    execute(pReq);
    return pCfm;
}
WlMaKeyDataCfm * WiFi_API_MaKeyData(u16 length, u16 wmHeader, u16 *keyDatap) {
    AUTO_INIT(WlMaKeyData);
    pReq->header.code = pCfm->header.code = 0x100 + 1;
    pReq->length = length;
    pReq->wmHeader = wmHeader;
    pReq->keyDatap = keyDatap;
    execute(pReq);
    return pCfm;
}
WlMaMpCfm * WiFi_API_MaMp(u16 resume, u16 retryLimit, u16 txop, u16 pollBitmap, u16 tmptt, u16 currTsf, u16 dataLength, u16 wmHeader, u16 *datap) {
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
WlMaClrDataCfm * WiFi_API_MaClrData(u16 flag) {
    AUTO_INIT(WlMaClrData);
    pReq->header.code = pCfm->header.code = 0x100 + 4;
    pReq->flag = flag;
    execute(pReq);
    return pCfm;
}
WlParamSetCfm * WiFi_API_ParamSetAll(WlParamSetAllReq *req) {
    GET_REQ_CFM_AUTO(WlParamSetAllReq, WlParamSetCfm);
    pReq->header.code = pCfm->header.code = 0x200 + 0;
    memcpy((u8*)pReq+offsetof(WlParamSetAllReq, staMacAdrs), (u8*)req+offsetof(WlParamSetAllReq, staMacAdrs), sizeof(WlParamSetAllReq)-offsetof(WlParamSetAllReq, staMacAdrs));
    execute(pReq);
    return pCfm;
}
WlParamSetCfm * WiFi_API_ParamSetWepKeyId(u16 wepKeyId) {
    GET_REQ_CFM_AUTO(WlParamSetWepKeyIdReq, WlParamSetCfm);
    pReq->header.code = pCfm->header.code = 0x200 + 7;
    pReq->wepKeyId = wepKeyId;
    execute(pReq);
    return pCfm;
}
WlParamSetCfm * WiFi_API_ParamSetBeaconLostTh(u16 beaconLostTh) {
    GET_REQ_CFM_AUTO(WlParamSetBeaconLostThReq, WlParamSetCfm);
    pReq->header.code = pCfm->header.code = 0x200 + 11;
    pReq->beaconLostTh = beaconLostTh;
    execute(pReq);
    return pCfm;
}
WlParamSetCfm * WiFi_API_ParamSetSsidMask(u8 *mask) {
    GET_REQ_CFM_AUTO(WlParamSetSsidMaskReq, WlParamSetCfm);
    pReq->header.code = pCfm->header.code = 0x200 + 13;
    memcpy(pReq->mask, mask, sizeof(pReq->mask));
    execute(pReq);
    return pCfm;
}
WlParamSetCfm * WiFi_API_ParamSetPreambleType(u16 type) {
    GET_REQ_CFM_AUTO(WlParamSetPreambleTypeReq, WlParamSetCfm);
    pReq->header.code = pCfm->header.code = 0x200 + 14;
    pReq->type = type;
    execute(pReq);
    return pCfm;
}
WlParamSetCfm * WiFi_API_ParamSetLifeTime(u16 tableNumber, u16 camLifeTime, u16 frameLifeTime) {
    GET_REQ_CFM_AUTO(WlParamSetLifeTimeReq, WlParamSetCfm);
    pReq->header.code = pCfm->header.code = 0x200 + 17;
    pReq->tableNumber = tableNumber;
    pReq->camLifeTime = camLifeTime;
    pReq->frameLifeTime = frameLifeTime;
    execute(pReq);
    return pCfm;
}
WlParamSetCfm * WiFi_API_ParamSetMaxConn(u16 count) {
    GET_REQ_CFM_AUTO(WlParamSetMaxConnReq, WlParamSetCfm);
    pReq->header.code = pCfm->header.code = 0x200 + 18;
    pReq->count = count;
    execute(pReq);
    return pCfm;
}
WlParamSetCfm * WiFi_API_ParamSetBeaconSendRecvInd(u16 enableMessage) {
    GET_REQ_CFM_AUTO(WlParamSetBeaconSendRecvIndReq, WlParamSetCfm);
    pReq->header.code = pCfm->header.code = 0x200 + 21;
    pReq->enableMessage = enableMessage;
    execute(pReq);
    return pCfm;
}
WlParamSetCfm * WiFi_API_ParamSetNullKeyMode(u16 mode) {
    GET_REQ_CFM_AUTO(WlParamSetNullKeyModeReq, WlParamSetCfm);
    pReq->header.code = pCfm->header.code = 0x200 + 22;
    pReq->mode = mode;
    execute(pReq);
    return pCfm;
}
WlParamSetCfm * WiFi_API_ParamSetBeaconPeriod(u16 beaconPeriod) {
    GET_REQ_CFM_AUTO(WlParamSetBeaconPeriodReq, WlParamSetCfm);
    pReq->header.code = pCfm->header.code = 0x200 + 0x40 + 2;
    pReq->beaconPeriod = beaconPeriod;
    execute(pReq);
    return pCfm;
}
WlParamSetCfm * WiFi_API_ParamSetGameInfo(u16 gameInfoLength, u16 *gameInfo) {
    GET_REQ_CFM(WlParamSetGameInfoReq, 1 + ((gameInfoLength + 1) / 2), WlParamSetCfm, 1);
    pReq->header.code = pCfm->header.code = 0x200 + 0x40 + 5;
    pReq->gameInfoLength = gameInfoLength;
    memcpy(pReq->gameInfo, gameInfo, gameInfoLength);
    execute(pReq);
    return pCfm;
}
WlParamGetMacAddressCfm * WiFi_API_ParamGetMacAddress() {
    GET_REQ_CFM_AUTO(WlCmdReq, WlParamGetMacAddressCfm);
    pReq->header.code = pCfm->header.code = 0x200 + 0x80 + 1;
    execute(pReq);
    return pCfm;
}
WlParamGetEnableChannelCfm * WiFi_API_ParamGetEnableChannel() {
    GET_REQ_CFM_AUTO(WlCmdReq, WlParamGetEnableChannelCfm);
    pReq->header.code = pCfm->header.code = 0x200 + 0x80 + 3;
    execute(pReq);
    return pCfm;
}
WlParamGetModeCfm * WiFi_API_ParamGetMode() {
    GET_REQ_CFM_AUTO(WlCmdReq, WlParamGetModeCfm);
    pReq->header.code = pCfm->header.code = 0x200 + 0x80 + 4;
    execute(pReq);
    return pCfm;
}
WlDevShutdownCfm * WiFi_API_DevShutdown() {
    GET_REQ_CFM_AUTO(WlCmdReq, WlDevShutdownCfm);
    pReq->header.code = pCfm->header.code = 0x300 + 1;
    execute(pReq);
    return pCfm;
}
WlDevIdleCfm * WiFi_API_DevIdle() {
    GET_REQ_CFM_AUTO(WlCmdReq, WlDevIdleCfm);
    pReq->header.code = pCfm->header.code = 0x300 + 2;
    execute(pReq);
    return pCfm;
}
WlDevClass1Cfm * WiFi_API_DevClass1() {
    GET_REQ_CFM_AUTO(WlCmdReq, WlDevClass1Cfm);
    pReq->header.code = pCfm->header.code = 0x300 + 3;
    execute(pReq);
    return pCfm;
}
WlDevRebootCfm * WiFi_API_DevReboot() {
    GET_REQ_CFM_AUTO(WlCmdReq, WlDevRebootCfm);
    pReq->header.code = pCfm->header.code = 0x300 + 4;
    execute(pReq);
    return pCfm;
}
WlDevClrInfoCfm * WiFi_API_DevClrInfo() {
    GET_REQ_CFM_AUTO(WlCmdReq, WlDevClrInfoCfm);
    pReq->header.code = pCfm->header.code = 0x300 + 5;
    execute(pReq);
    return pCfm;
}
WlDevGetVerInfoCfm * WiFi_API_DevGetVerInfo() {
    GET_REQ_CFM_AUTO(WlCmdReq, WlDevGetVerInfoCfm);
    pReq->header.code = pCfm->header.code = 0x300 + 6;
    execute(pReq);
    return pCfm;
}
WlDevGetInfoCfm * WiFi_API_DevGetInfo() {
    GET_REQ_CFM_AUTO(WlCmdReq, WlDevGetInfoCfm);
    pReq->header.code = pCfm->header.code = 0x300 + 7;
    execute(pReq);
    return pCfm;
}
WlDevGetStateCfm * WiFi_API_DevGetState() {
    GET_REQ_CFM_AUTO(WlCmdReq, WlDevGetStateCfm);
    pReq->header.code = pCfm->header.code = 0x300 + 8;
    execute(pReq);
    return pCfm;
}

void WiFi_SetChildSize(u16 childSize) {
    wifiWork.mp_childSize = childSize;
    
    if (wifiWork.aid == 0) {
        wifiWork.mp_recvSize = childSize + 2;
    } else {
        wifiWork.mp_sendSize = childSize + 2;
    }
}

void WiFi_SetParentSize(u16 parentSize) {
    wifiWork.mp_parentSize = parentSize;
    
    if (wifiWork.aid == 0) {
        wifiWork.mp_sendSize = parentSize + 2;
    } else {
        wifiWork.mp_recvSize = parentSize + 2;
    }
}

void WiFi_SetChildMaxSize(u16 childMaxSize) {
    if (childMaxSize > 512)
        childMaxSize = 512;
    
    wifiWork.mp_childMaxSize = childMaxSize;
    wifiWork.mp_childSize = childMaxSize;
    
    if (wifiWork.aid == 0) {
        // We're the parent
        wifiWork.mp_maxRecvSize = childMaxSize + 2;
        wifiWork.mp_recvSize = childMaxSize + 2;
        
    } else {
        // We're a child
        wifiWork.mp_maxSendSize = childMaxSize + 2;
        wifiWork.mp_sendSize = childMaxSize + 2;
    }
}

void WiFi_SetParentMaxSize(u16 parentMaxSize) {
    if (parentMaxSize > 512)
        parentMaxSize = 512;
    
    wifiWork.mp_parentMaxSize = parentMaxSize;
    wifiWork.mp_parentSize = parentMaxSize;
    
    if (wifiWork.aid == 0) {
        // We're the parent
        wifiWork.mp_maxSendSize = parentMaxSize + 4;
        wifiWork.mp_sendSize = parentMaxSize + 4;
        
    } else {
        // We're a child
        wifiWork.mp_maxRecvSize = parentMaxSize + 4;
        wifiWork.mp_recvSize = parentMaxSize + 4;
    }
}

void WiFi_ResetSizeVars() {
    wifiWork.mp_sendSize = 0;
    wifiWork.mp_recvSize = 0;
    wifiWork.mp_parentSize = 0;
    wifiWork.mp_childSize = 0;
    wifiWork.mp_maxSendSize = 0;
    wifiWork.mp_maxRecvSize = 0;
    wifiWork.mp_parentMaxSize = 0;
    wifiWork.mp_childMaxSize = 0;
}

bool WiFi_SetAllParams(bool bScan) {
    WlParamSetAllReq req;
    
    memcpy(req.staMacAdrs, wifiWork.MacAddress, sizeof(req.staMacAdrs));
    req.retryLimit = 7;
    req.enableChannel = wifiWork.enableChannel;
    req.rate = wifiWork.rate;
    req.mode = wifiWork.mode;
    
    if (wifiWork.wep_flag != 0) {
        req.wepMode = wifiWork.wepMode;
        req.wepKeyId = wifiWork.wepKeyId;
        memcpy(req.wepKey, wifiWork.wepKey, sizeof(req.wepKey));
        req.authAlgo = 1;
        
    } else {
        req.wepMode = 0;
        req.wepKeyId = 0;
        memset16(req.wepKey, 0, sizeof(req.wepKey));
        req.authAlgo = 0;
    }
    
    req.beaconType = 1;
    req.probeRes = 1;
    
    if (wifiWork.mode == 1) { // parent
        req.beaconLostTh = 0;
    } else {
        req.beaconLostTh = 16;
    }
    req.activeZoneTime = 10;
    
    if (bScan) {
        memset16(req.ssidMask, 0, sizeof(req.ssidMask));
        
    } else {
        memset16(req.ssidMask, 0, 8);
        memset16(req.ssidMask + 8, 0xFFFF, sizeof(req.ssidMask)-8);
    }
    
    req.preambleType = wifiWork.preamble;    
    WlParamSetCfm *pCfm = WiFi_API_ParamSetAll(&req);
    
    if (pCfm->resultCode != 0) {
        myprintf("result %i\n", pCfm->resultCode);
        // TODO: error handler
        return false;
    }
    
    return true;
}

void WiFi_CopyParentParam(WMGameInfo *gameInfo, WMParentParam *pparam) {
    gameInfo->ggid = pparam->ggid;
    gameInfo->tgid = pparam->tgid;
    
    u16 flag = 0;
    if (pparam->entryFlag)
        flag |= 1;
    if (pparam->multiBootFlag)
        flag |= 2;
    if (pparam->KS_Flag)
        flag |= 4;
    //if (pparam->CS_Flag)
    //    flag |= 8;
    
    gameInfo->attribute = flag;
    gameInfo->userGameInfoLength = pparam->userGameInfoLength;
    gameInfo->magicNumber = 1;
    gameInfo->ver = 1;
    gameInfo->platform = 0;
    gameInfo->parentMaxSize = pparam->parentMaxSize;
    
    if (pparam->multiBootFlag && pparam->childMaxSize >= 8) {
        gameInfo->childMaxSize = 8;
    } else {
        gameInfo->childMaxSize = pparam->childMaxSize;
    }
    
    if (gameInfo->userGameInfoLength != 0) {
        memcpy(gameInfo->userGameInfo, pparam->userGameInfo, (gameInfo->userGameInfoLength + 1) & ~1);
    }
}

bool WiFi_CheckMacAddress(u16 *macAdr) {
    return memcmp(macAdr, wifiWork.MacAddress, 6) == 0;
}

void WiFi_InitAlarm() {
    createAlarm(&wifiWork.mpIntervalAlarm);
    createAlarm(&wifiWork.mpAckAlarm);
}

void WiFi_RequestResumeMP() {
    // TODO
}
    
int WiFi_Initialize(u32 miscFlags) {
    // TODO local messages
    
    WiFi_CommonInit(miscFlags);
    if (!WiFi_CommonWlIdle()) {
        return 1;
    }
    
    wifiWork.state = 2;
    return 0;
}

void WiFi_Reset() {
    // TODO not my priority
}

void WiFi_End() {
    // TODO not my priority
}

void WiFi_SetParentParam() {
    // TODO, has one arg (msg)
}

void WiFi_StartParent() {
    // TODO, has one arg (msg)
}

void WiFi_EndParent() {
    // TODO
}

int WiFi_StartScanEx(u16 channelList, WMBssDesc *scanBuf, u16 scanBufSize, u16 maxChannelTime, u16 bssid[3], u16 scanType, u16 ssidLength, u8 ssid[32], u16 ssidMatchLength) {
    if (wifiWork.state != 2 && wifiWork.state != 3 && wifiWork.state != 5) {
        return 1;
    }
    
    wifiWork.infoBuf = scanBuf;
    wifiWork.scan_channel = channelList; // unused?
    
    u16 bssidCopy[3];
    memcpy(bssidCopy, bssid, 3);
    
    bool bUnk = false;
    if (scanType == 2 || scanType == 3) {
        bUnk = true;
        scanType -= 2;
    }
    
    if (bssidCopy[0] != 0xFFFF && bssidCopy[0] & 1) {
        // [ARM7] WM_StartScan: assigned Bssid is MulticastAddress. LSB is cleared.
        bssidCopy[0] &= ~1;
    }
    
    // Seems like it also checks scanBuf alignment, scanBufSize (must be >= 0x40)
    u16 enabledChannels = (wifiWork.enableChannel & (channelList << 1));
    if (enabledChannels == 0) {
        return 8;
    }
    
    wifiWork.mode = 2;
    
    WlDevGetStateCfm *pCfm = WiFi_API_DevGetState();
    if (pCfm->resultCode != 0) {
        return 2;
    }
    
    if (pCfm->state == 0x10) {
        if (!WiFi_SetAllParams(true))
            return 3;
        
        WlDevClass1Cfm *pCfm2 = WiFi_API_DevClass1();
        if (pCfm2->resultCode != 0) {
            // error handling, damn
            return 4;
        }
        
        wifiWork.state = 3;
        WlMlmePowerMgtCfm *pCfm3 = WiFi_API_MlmePowerMgt(1, 0, 1);
        if (pCfm3->resultCode != 0) {
            // error handling
            return 5;
        }
        
        wifiWork.pwrMgtMode = 1;
    }
    
    if (scanType == 0) {
        if (wifiWork.preamble == 1) {
            WlParamSetCfm *pCfm4 = WiFi_API_ParamSetPreambleType(0);
            if (pCfm4->resultCode != 0)
                return 7;
            
            wifiWork.preamble = 0;
        }
        
    } else {
        if (wifiWork.preamble == 0) {
            WlParamSetCfm *pCfm4 = WiFi_API_ParamSetPreambleType(1);
            if (pCfm4->resultCode != 0)
                return 7;
            
            wifiWork.preamble = 1;
        }
    }
    
    if (bUnk) {
        // fill mask, ParamSetSsidMask, todo
    }
    
    wifiWork.state = 5;
    
    u8 channelList2[16];
    u8 *curChannel = channelList2;
    for (int channel = 1; channel < 15; channel++) {
        if (enabledChannels & (1 << channel)) {
            myprintf("chan %i\n", channel);
            *(curChannel++) = channel;
        }
    }
    *curChannel = 0;
    
    WlMlmeScanCfm *pScanCfm = WiFi_API_MlmeScan(bssidCopy, ssidLength, ssid, scanType, channelList2, maxChannelTime);
    if (pScanCfm->resultCode != 0) {
        return 9;
    }
    
    if (pScanCfm->bssDescCount) {
        WlBssDesc *bssDesc = pScanCfm->bssDescList;
        u8 *buf = (u8 *)wifiWork.infoBuf;
        memset(buf, 0, scanBufSize);
        
        for (int i = 0; i < pScanCfm->bssDescCount; i++) {
            u32 size = bssDesc->length * 2;
            
            // custom checks cause our MlmeScan sucks
            if (size > scanBufSize)
                break;
            
            
            scanBufSize -= size;
            memcpy(buf, bssDesc, size);
            
            // todo check ssid, might be set to ssidCopy if invalid?
            
            // bssDesc[i] = buf
            // rssi
            
            buf += size;
            bssDesc = (WlBssDesc *)((u8 *)bssDesc + size);
            // it tries to realign ; won't do for now
        }
        
        // we can't return the number of returned values for now,
        // do this temporarily
        return -pScanCfm->bssDescCount;
        
    } else {
        return 0;
    }
}

int WiFi_StartScan(u16 channel, WMBssDesc *scanBuf, u16 maxChannelTime, u16 bssid[3]) {
    if (wifiWork.state != 2 && wifiWork.state != 3 && wifiWork.state != 5) {
        return 7;
    }
    
    wifiWork.infoBuf = scanBuf;
    wifiWork.scan_channel = channel; // unused?
    
    u16 bssidCopy[3];
    memcpy(bssidCopy, bssid, sizeof(bssidCopy));
    
    if (bssidCopy[0] != 0xFFFF && bssidCopy[0] & 1) {
        // [ARM7] WM_StartScan: assigned Bssid is MulticastAddress. LSB is cleared.
        bssidCopy[0] &= ~1;
    }
    
    if (channel == 0 || (wifiWork.enableChannel & (1 << channel)) == 0) {
        return 1;
    }
    
    wifiWork.mode = 2;
    
    WlDevGetStateCfm *pCfm = WiFi_API_DevGetState();
    if (pCfm->resultCode != 0) {
        // error handling!!
        return 2;
    }
    
    if (pCfm->state == 0x10) {
        if (!WiFi_SetAllParams(false))
            return 3;
        
        WlDevClass1Cfm *pCfm2 = WiFi_API_DevClass1();
        if (pCfm2->resultCode != 0) {
            // error handling, damn
            return 4;
        }
        
        wifiWork.state = 3;
        WlMlmePowerMgtCfm *pCfm3 = WiFi_API_MlmePowerMgt(1, 0, 1);
        if (pCfm3->resultCode != 0) {
            // error handling
            return 5;
        }
        
        wifiWork.pwrMgtMode = 1;
    }
    
    wifiWork.state = 5;
    
    u8 channelList[16];
    memset(channelList, 0, sizeof(channelList));
    channelList[0] = channel;
    
    WlMlmeScanCfm *pScanCfm = WiFi_API_MlmeScan(bssidCopy, 0, NULL, 1, channelList, maxChannelTime);
    if (pScanCfm->resultCode != 0) {
        return 6;
    }
    
    if (pScanCfm->bssDescCount) {
        memset(&wifiWork.infoBuf->gameInfo, 0, 0x80);
        memcpy(wifiWork.infoBuf, &pScanCfm->bssDescList[0], 2 * pScanCfm->bssDescList[0].length);
        
        // GetLinkLevel
        // AddRssiToRandomPool
        // TODO: also return everything it should be returning
        
    } else {
        return -1; // this is *not* supposed to be an error 
        // but i didn't implement anything else for now
    }
    
    return 0;
}

int WiFi_EndScan() {
    if (wifiWork.state != 5)
        return 1;
    
    WlDevIdleCfm *pCfm = WiFi_API_DevIdle();
    if (pCfm->resultCode != 0) {
        return 2;
    }
    
    wifiWork.state = 2;
    if (wifiWork.preamble == 0) {
        WlParamSetCfm *pCfm2 = WiFi_API_ParamSetPreambleType(1);
        if (pCfm2->resultCode != 0) {
            return 3;
        }
        wifiWork.preamble = 1;
    }
    
    return 0;
}

int WiFi_StartConnectEx(WMBssDesc *bssDesc, u8 ssid[24], int powerSave, u16 authMode) {
    if (wifiWork.state != 2 || (wifiWork.miscFlags & 1) != 0) {
        return 1;
    }
    
    memcpy(&wifiWork.bssDesc, bssDesc, sizeof(WMBssDesc));
    if (wifiWork.bssDesc.gameInfoLength >= 0x10 && (wifiWork.bssDesc.gameInfo.attribute & 1) == 0) {
        return 2;
    }
    
    u16 chanbit = (1 << wifiWork.bssDesc.channel);
    if ((wifiWork.enableChannel & chanbit) == 0 || ((chanbit >> 1) & 0x1FFF) == 0) {
        // channel not enabled and/or invalid
        return 8;
    }
    
    if (wifiWork.rate == 1) {
        if (wifiWork.bssDesc.rateSet.basic & 1)
            wifiWork.rate = 1;
        else
            wifiWork.rate = 2;
        
    } else {
        if (wifiWork.bssDesc.rateSet.basic & 2)
            wifiWork.rate = 2;
        else
            wifiWork.rate = 1;
    }
    
    wifiWork.preamble = (wifiWork.bssDesc.capaInfo & 0x20) != 0;
    
    // This got removed
    //wifiWork.wep_flag = (wifiWork.bssDesc.capaInfo & 0x10) != 0;
    
    if (wifiWork.bssDesc.gameInfoLength == 0)
        wifiWork.mode = 3;
    else
        wifiWork.mode = 2;
    
    if (!WiFi_SetAllParams(false)) {
        // driver error has already been handled?
        return 3;
    }
    
    WlParamSetCfm *pCfm = WiFi_API_ParamSetNullKeyMode(0);
    if (pCfm->resultCode != 0) {
        // TODO error handler
        return 4;
    }
    
    if (wifiWork.bssDesc.gameInfoLength < 0x10) {
        u16 beaconLostTh = 1;
        if (wifiWork.bssDesc.beaconPeriod != 0) {
            beaconLostTh = (10000 / wifiWork.bssDesc.beaconPeriod) + 1;
            if (beaconLostTh > 255) beaconLostTh = 255;
        }
        
        pCfm = WiFi_API_ParamSetBeaconLostTh(beaconLostTh);
        if (pCfm->resultCode != 0) {
            // TODO error handler
            return 5;
        }
    }
    
    WlDevClass1Cfm *pCfm2 = WiFi_API_DevClass1();
    if (pCfm2->resultCode != 0) {
        // TODO error handler
        return 6;
    }
    
    wifiWork.state = 3;
    u16 pwrMgtMode = powerSave != 0;
    
    WlMlmePowerMgtCfm *pCfm3 = WiFi_API_MlmePowerMgt(pwrMgtMode, 0, 1);
    if (pCfm3->resultCode != 0) {
        // TODO error handler
        return 7;
    }
    
    wifiWork.pwrMgtMode = pwrMgtMode;
    
    // attempt to join now
    WlBssDesc joinBss;
    memcpy(&joinBss, &wifiWork.bssDesc, sizeof(joinBss));
    
    if (wifiWork.mode == 2) {
        // set SSID if we're a mp child
        joinBss.ssidLength = 32;
        memcpy(joinBss.ssid, &wifiWork.bssDesc.gameInfo.ggid, 4);
        memcpy(joinBss.ssid + 4, &wifiWork.bssDesc.gameInfo.tgid, 2);
        memset(joinBss.ssid + 6, 0, 2);
        memcpy(joinBss.ssid + 8, ssid, 24);
    }
    
    WlMlmeJoinCfm *pCfm4 = WiFi_API_MlmeJoin(2000, &joinBss);
    if (pCfm4->resultCode != 0 || pCfm4->statusCode != 0) {
        // TODO error handler
        return 9;
    }
    
    // join OK, let's try to authenticate now
    memcpy(wifiWork.parentMacAddress, pCfm4->peerMacAdrs, 6);
    
    WlMlmeAuthCfm *pCfm5 = WiFi_API_MlmeAuth(wifiWork.parentMacAddress, authMode, 2000);
    if (pCfm5->resultCode != 0 || pCfm5->statusCode != 0) {
        // TODO error handler
        return 10;
    }
    
    // auth OK, attempt to associate
    WlMlmeAssCfm *pAssCfm = WiFi_API_MlmeAss(wifiWork.parentMacAddress, 1, 2000);
    if (pAssCfm->resultCode != 0 || pAssCfm->statusCode != 0) {
        // TODO error handler
        return 11;
    }
    
    // interrupts are disabled on this part on real func
    int x = enterCriticalSection();
    wifiWork.aid = pAssCfm->aid;
    wifiWork.curr_tgid = wifiWork.bssDesc.gameInfo.tgid;
    memset16(wifiWork.portSeqNo, 1, 0x10);
    
    // TODO wifiWork.linkLevel
    
    wifiWork.child_bitmap = 1;
    wifiWork.mp_readyBitmap = 1;
    if (wifiWork.mp_lifeTimeTick != 0) {
        wifiWork.mp_lastRecvTick[0] = getTick() | 1;
    }
    
    wifiWork.state = 8; // probably connected or something like that
    
    // I have no idea what this is doing
    u16 parentMaxSize = 0, childMaxSize = 0;
    if (wifiWork.bssDesc.gameInfo.attribute & 4) {
        // KS flag set, apparently
        parentMaxSize = 42;
        childMaxSize = 6;
    }
    
    WiFi_SetParentMaxSize(wifiWork.bssDesc.gameInfo.parentMaxSize + parentMaxSize);
    WiFi_SetChildMaxSize(wifiWork.bssDesc.gameInfo.childMaxSize + childMaxSize);
    
    wifiWork.beaconIndicateFlag = 1;
    leaveCriticalSection(x);
    return 0; // ggwp
}

void WiFi_IndicateDisconnectionFromMyself(int parent, u16 aid, void *mac) {
    // TODO, seems like its arm9 related
}

void WiFi_DisconnectCore() {
    // TODO, has args
}

void WiFi_Disconnect() {
    // TODO, has args
}

static void SetTmpParam(WMMPTmpParam *tmpParam) {
    u32 mask = tmpParam->mask;

    // max frequency
    u16 maxFreq = wifiWork.mp_maxFreq;
    if (mask & 4) maxFreq = tmpParam->maxFrequency;
    if (maxFreq == 0) maxFreq = 16;

    // min frequency
    u16 minFreq = wifiWork.mp_minFreq;
    if (mask & 1) minFreq = tmpParam->minFrequency;        
    if (minFreq == 0) minFreq = 16;
    if (minFreq > maxFreq) minFreq = maxFreq;
    
    // frequency
    u16 freq = wifiWork.mp_freq;
    if (mask & 2) freq = tmpParam->frequency;
    if (freq == 0) freq = 16;
    if (freq > maxFreq) freq = maxFreq;
    
    wifiWork.mp_current_freq = freq;
    wifiWork.mp_current_minFreq = minFreq;
    wifiWork.mp_current_maxFreq = maxFreq;

    // count
    if (wifiWork.mp_count > maxFreq)
        wifiWork.mp_count = maxFreq;
    
    // default retry count
    if (mask & 0x200)
        wifiWork.mp_defaultRetryCount = tmpParam->defaultRetryCount;
    
    if (mask & 0x400)
        wifiWork.mp_minPollBmpMode = tmpParam->minPollBmpMode;
    
    if (mask & 0x800)
        wifiWork.mp_singlePacketMode = tmpParam->singlePacketMode;
    
    if (mask & 0x1000)
        wifiWork.mp_ignoreFatalErrorMode = tmpParam->ignoreFatalErrorMode;
}

int WiFi_StartMP(u32 *recvBuf, u32 recvBufSize, u32 *sendBuf, u32 sendBufSize, WMMPParam *param, WMMPTmpParam *tmpParam) {
    if (!wifiWork.mp_ignoreSizePrecheckMode) {
        if (sendBufSize < ((wifiWork.mp_maxSendSize + 31) & ~31)) {
            return 1;
        }
        if (wifiWork.aid == 0) {
            if (recvBufSize < (((wifiWork.mp_maxRecvSize + 12) * wifiWork.pparam.maxEntry + 10 + 31) & ~31))
                return 2;
            
        } else {
            if (recvBufSize < ((wifiWork.mp_maxRecvSize + 50 + 31) & ~31))
                return 3;
        }
    }
    
    // precheck ok
    // if mp child, check channel
    if (wifiWork.mode == 2 && (wifiWork.allowedChannel & (1 << wifiWork.bssDesc.channel >> 1)) == 0) {
        return 4;
    }
    
    if (wifiWork.mp_flag) {
        wifiWork.mp_flag = 0;
        WiFi_CleanSendQueue(0xFFFF);
        // This shouldn't happen, maybe a warning?
    }
    
    WiFi_InitSendQueue();
    
    int x = enterCriticalSection();
    WiFi_SetMPParameterCore(param, 0);
    
    // this is neww
    if (wifiWork.state != 9 && wifiWork.state != 10) {
        SetTmpParam(tmpParam);
    }
    
    if (wifiWork.state != 7 && wifiWork.state != 8) {
        leaveCriticalSection(x);
        return 5;
    }
    
    wifiWork.mp_waitAckFlag = 0;
    wifiWork.mp_vsyncOrderedFlag = 0;
    wifiWork.mp_vsyncFlag = 1;
    wifiWork.mp_newFrameFlag = 0;
    wifiWork.mp_pingFlag = 0;
    wifiWork.mp_pingCounter = 60;
    wifiWork.sendQueueInUse = 0;
    wifiWork.mp_sentDataFlag = 0;
    wifiWork.mp_bufferEmptyFlag = 0;
    wifiWork.mp_isPolledFlag = 0;
    wifiWork.mp_resumeFlag = 0;
    wifiWork.mp_recvBuf[0] = (WMMpRecvBuf *)recvBuf;
    wifiWork.mp_recvBufSize = recvBufSize;
    wifiWork.mp_recvBuf[1] = (WMMpRecvBuf *)((u8*)recvBuf + recvBufSize);
    wifiWork.mp_recvBufSel = 0;
    wifiWork.mp_sendBuf = sendBuf;
    wifiWork.mp_sendBufSize = sendBufSize;
    wifiWork.mp_count = 0;
    wifiWork.mp_limitCount = 0;
    wifiWork.mp_prevPollBitmap = 0;
    wifiWork.mp_prevWmHeader = 0;
    wifiWork.minRssi = 0xFFFF;
    wifiWork.rssiCounter = 1;
    
    u64 tick = getTick() | 1;
    for (int i = 0; i < 16; i++) {
        wifiWork.mp_lastRecvTick[i] = tick;
    }
    
    // SetThreadPriorityHigh(), but we have no threads!
    wifiWork.valarm_queuedFlag = 0;
    WiFi_SetVAlarm();
    
    // actually, it's set to 10 if 8 or set to 9 if 7
    wifiWork.state += 2;
    
    leaveCriticalSection(x);
    
    WlParamSetCfm *pCfm = WiFi_API_ParamSetNullKeyMode(0);
    if (pCfm->resultCode != 0) {
        // TODO error handler
    }
    
    return 0;
}

void WiFi_SetMPData() {
    // todo, has a parameter
}

void WiFi_EndMP() {
    // todo
}

void WiFi_StartDCF() {
    // TODO, but will probably not implement DCF
    // has one arg
}

void WiFi_SetDCFData() {
    // TODO, has one arg, won't implement?
}

void WiFi_EndDCF() {
}

void WiFi_SetWEPKeyEx() {
    // TODO, has one arg, etc
}

void WiFi_SetWEPKey() {
    // TODO, has one arg, etc
}

void WiFi_SetGameInfo() {
    // TODO, one arg, etc
}

void WiFi_SetBeaconTxRxInd() {
    // TODO, one arg
}

void WiFi_StartTestMode() {
    // won't do?
}

void WiFi_StopTestMode() {
    // won't do?
}

void WiFi_SetLifeTime() {
    // TODO
}

void WiFi_MeasureChannel() {
    // TODO, one arg, maybe not
}

void WiFi_InitWirelessCounter() {
}

void WiFi_GetWirelessCounter() {
}

void WiFi_SetVAlarm() {
    if (wifiWork.mode == 1) {
        setVAlarm(&wifiWork.valarm, 209, 263, ParentAdjustVSync, NULL);
    } else if (wifiWork.mode == 2) {
        wifiWork.VSyncFlag = 0;
        setVAlarm(&wifiWork.valarm, 200, 263, ChildAdjustVSync1, NULL);
        wifiWork.v_remain = 0;
    }
}

void WiFi_CancelVAlarm() {
    cancelVAlarm(&wifiWork.valarm);
}

void WiFi_InitVAlarm() {
    createVAlarm(&wifiWork.valarm);
}

void WiFi_KickNextMP_Resume(/* has arg */) {
}

void WiFi_KickNextMP_Child() {
}

void WiFi_KickNextMP_Parent(/* has arg */) {
}

void WiFi_VAlarmSetMPData() {
}

void WiFi_ParsePortPacket(u16 aid, u16 wmHeader, u16 *data, u16 length, void *recvBuf) {
    // We don't have recvBuf yet, TODO
}

void WiFi_CleanSendQueue(u16 aidBitmap) {
}

void WiFi_FlushSendQueue(int timeout, u16 pollBitmap) {
}

void WiFi_PutSendQueue(int childBitmap, u16 priority, u16 port, int destBitmap, u16 *sendData, u16 sendDataSize, void (*callback)(void *), void *arg) {
}

void WiFi_ResumeMaMP(u16 pollBitmap) {
}

void WiFi_SendMaMP(u16 pollBitmap) {
}

void WiFi_SendMaKeyData() {
}

void WiFi_InitSendQueue() {
}

void WiFi_SetEntry(/* args */) {
}

void WiFi_AutoDeAuth(/* args */) {
}

void WiFi_CommonInit(u32 miscFlags) {
    int x = enterCriticalSection();
    
    bool bCleanQueue = false;
    
    if (wifiWork.mp_flag == 1) {
        wifiWork.mp_flag = 0;
        bCleanQueue = true;
        WiFi_CancelVAlarm();
        //WiFi_SetThreadPriorityLow();
    }
    
    wifiWork.child_bitmap = 0;
    wifiWork.mp_readyBitmap = 0;
    wifiWork.ks_flag = 0;
    wifiWork.dcf_flag = 0;
    wifiWork.VSyncFlag = 0;
    wifiWork.valarm_queuedFlag = 0;
    wifiWork.beaconIndicateFlag = 0;
    wifiWork.mp_minFreq = 1;
    wifiWork.mp_freq = 1;
    wifiWork.mp_maxFreq = 6;
    wifiWork.mp_defaultRetryCount = 0;
    wifiWork.mp_minPollBmpMode = 0;
    wifiWork.mp_singlePacketMode = 0;
    wifiWork.mp_ignoreFatalErrorMode = 0;
    wifiWork.mp_ignoreSizePrecheckMode = 0;
    wifiWork.mp_current_minFreq = wifiWork.mp_minFreq;
    wifiWork.mp_current_freq = wifiWork.mp_freq;
    wifiWork.mp_current_maxFreq = wifiWork.mp_maxFreq;
    wifiWork.mp_current_defaultRetryCount = wifiWork.mp_defaultRetryCount;
    wifiWork.mp_current_minPollBmpMode = wifiWork.mp_minPollBmpMode;
    wifiWork.mp_current_singlePacketMode = wifiWork.mp_singlePacketMode;
    wifiWork.mp_current_ignoreFatalErrorMode = wifiWork.mp_ignoreFatalErrorMode;
    wifiWork.wep_flag = 0;
    wifiWork.wepMode = 0;
    memset(wifiWork.wepKey, 0, sizeof(wifiWork.wepKey));
    
    WiFi_ResetSizeVars();
    wifiWork.mp_parentVCount = 260;
    wifiWork.mp_childVCount = 240;
    wifiWork.mp_parentInterval = 1000;
    wifiWork.mp_childInterval = 0;
    
    wifiWork.mp_parentIntervalTick = 523;
    wifiWork.mp_childIntervalTick = 0;
    wifiWork.pwrMgtMode = 0;
    wifiWork.preamble = 1;
    wifiWork.miscFlags = miscFlags;
    
    leaveCriticalSection(x);
    
    if (bCleanQueue)
        WiFi_CleanSendQueue(0xFFFF);
    
    // TODO clear requestBuf
    
    memset16(wifiWork.portSeqNo, 1, sizeof(wifiWork.portSeqNo));
    WiFi_InitAlarm();
    // mutex, we don't have thread
    WiFi_InitVAlarm();
    
    // Set LED Pattern if miscFlags & 2 == 0
    wifiWork.state = 1;
}

void WiFi_Enable(/* args */) {
}

void WiFi_Disable() {
}

bool WiFi_CommonWlIdle() {
    WlDevRebootCfm *pCfm = WiFi_API_DevReboot();
    if (pCfm->resultCode != 0) {
        return false;
    }
    
    WlDevIdleCfm *pCfm2 = WiFi_API_DevIdle();
    if (pCfm2->resultCode != 0) {
        return false;
    }
    
    StabilizeBeacon();
    
    WlParamGetEnableChannelCfm *pCfm3 = WiFi_API_ParamGetEnableChannel();
    if (pCfm3->resultCode != 0) {
        return false;
    }
    
    wifiWork.enableChannel = pCfm3->enableChannel;
    
    // channels 1, 7 and 13
    wifiWork.allowedChannel = (1 << 0) | (1 << 6) | (1 << 12); // uhhh, it uses an awful function
    wifiWork.allowedChannel &= wifiWork.enableChannel; // not present in og
    
    WiFi_API_ParamSetLifeTime(0xFFFF, 40, 5);
    wifiWork.mp_lifeTimeTick = 2094625;
    wifiWork.rate = 2;
    wifiWork.preamble = 1;
    
    // We probably don't need version so I'm removing it
    
    WlParamGetMacAddressCfm *pCfm4 = WiFi_API_ParamGetMacAddress();
    if (pCfm4->resultCode != 0) {
        return false;
    }
    
    memcpy(wifiWork.MacAddress, pCfm4->staMacAdrs, 6);
    
    WlParamSetCfm *pCfm5 = WiFi_API_ParamSetBeaconSendRecvInd(1);
    if (pCfm5->resultCode != 0) {
        return false;
    }
    
    return true;
}

void WiFi_PowerOn() {
}

void WiFi_PowerOff() {
}

int WiFi_SetMPParameterCore(WMMPParam *param, WMMPParam *old_param) {
    int err = 0;
    u16 mask = param->mask;
    
    if (wifiWork.state != 9 && wifiWork.state != 10 && (param->mask & 0x2C00) != 0) {
        err = 1;
        mask &= ~0x2C00;
    }
    
    int x = enterCriticalSection();
    if (old_param) {
        old_param->mask = 0x3FFF;
        old_param->minFrequency = wifiWork.mp_minFreq;
        old_param->frequency = wifiWork.mp_freq;
        old_param->maxFrequency = wifiWork.mp_maxFreq;
        old_param->parentSize = wifiWork.mp_parentSize;
        old_param->childSize = wifiWork.mp_childSize;
        old_param->parentInterval = wifiWork.mp_parentInterval;
        old_param->childInterval = wifiWork.mp_childInterval;
        old_param->parentVCount = wifiWork.mp_parentVCount;
        old_param->childVCount = wifiWork.mp_childVCount;
        old_param->defaultRetryCount = wifiWork.mp_defaultRetryCount;
        old_param->minPollBmpMode = wifiWork.mp_minPollBmpMode;
        old_param->singlePacketMode = wifiWork.mp_singlePacketMode;
        old_param->ignoreFatalErrorMode = wifiWork.mp_ignoreFatalErrorMode;
        old_param->ignoreSizePrecheckMode = wifiWork.mp_ignoreSizePrecheckMode;
    }
    
    #define DEFAULT(value, default) ((value) == 0 ? (default) : (value))
    
    if (mask & 1) {
        wifiWork.mp_minFreq = DEFAULT(param->minFrequency, 16);
        wifiWork.mp_current_minFreq = wifiWork.mp_minFreq;
    }
    
    if (mask & 2) {
        wifiWork.mp_freq = DEFAULT(param->frequency, 16);
        wifiWork.mp_current_freq = wifiWork.mp_freq;
        
        if (wifiWork.mp_count > wifiWork.mp_freq)
            wifiWork.mp_count = wifiWork.mp_freq;
    }
    
    if (mask & 4) {
        wifiWork.mp_maxFreq = DEFAULT(param->maxFrequency, 16);
        wifiWork.mp_current_maxFreq = wifiWork.mp_maxFreq;
        
        if (wifiWork.mp_count > wifiWork.mp_maxFreq)
            wifiWork.mp_count = wifiWork.mp_maxFreq;
    }
    
    #undef DEFAULT
    
    if (mask & 8) {
        if (wifiWork.mp_parentMaxSize >= ((param->parentSize + 1) & ~1))
            WiFi_SetParentSize(param->parentSize);
        else
            err |= 2;
    }
    
    if (mask & 0x10) {
        if (0x200 /*wifiWork.mp_childMaxSize*/ >= ((param->childSize + 1) & ~1))
            WiFi_SetChildSize(param->childSize);
        else
            err |= 2;
    }
    
    if (mask & 0x20) {
        if (param->parentInterval <= 10000) {
            wifiWork.mp_parentInterval = param->parentInterval;
            wifiWork.mp_parentIntervalTick = (33514 * (u64)param->parentInterval) >> 16;
        }
    }
    
    if (mask & 0x40) {
        if (param->childInterval <= 10000) {
            wifiWork.mp_childInterval = param->childInterval;
            wifiWork.mp_childIntervalTick = (33514 * (u64)param->childInterval) >> 16;
        }
    }
    
    if (mask & 0x80) {
        if (param->parentVCount <= 190 || (param->parentVCount >= 220 && param->parentVCount <= 262))
            wifiWork.mp_parentVCount = param->parentVCount;
        else
            err |= 2;
    }
    
    if (mask & 0x100) {
        if (param->childVCount <= 190 || (param->childVCount >= 220 && param->childVCount <= 262))
            wifiWork.mp_childVCount = param->childVCount;
        else
            err |= 2;
    }
    
    if (mask & 0x200) {
        wifiWork.mp_defaultRetryCount = param->defaultRetryCount;
        wifiWork.mp_current_defaultRetryCount = wifiWork.mp_defaultRetryCount;
    }
    
    if (mask & 0x400) {
        wifiWork.mp_minPollBmpMode = param->minPollBmpMode;
    }
    
    if (mask & 0x800) {
        wifiWork.mp_singlePacketMode = param->singlePacketMode;
    }
    
    if (mask & 0x1000) {
        wifiWork.mp_ignoreFatalErrorMode = param->ignoreFatalErrorMode;
        wifiWork.mp_current_ignoreFatalErrorMode = wifiWork.mp_ignoreFatalErrorMode;
    }
    
    if (mask & 0x2000) {
        wifiWork.mp_ignoreSizePrecheckMode = param->ignoreSizePrecheckMode;
    }
    
    leaveCriticalSection(x);
    return err;
}

void WiFi_SetMPParameter(/* param */) {
}

void WiFi_SetBeaconPeriod(/* param */) {
}

void WiFi_AutoDisconnect(/* param */) {
}

void WiFi_SetPowerSaveMode(/* param */) {
}

void WiFi_StartTestRxMode() {
    // Won't do
}

void WiFi_StopTestRxMode() {
    // Won't do
}

u16 WiFi_GetAllowedChannel(u16 bitField) {
    // what the fuck
    return 0;
}

/**********************/

static void StabilizeBeacon() {
    W_CONFIG_124h = 200;
    W_CONFIG_128h = 2000;
    W_CONFIG_150h = 514;
}

static void SetVTSF() {
    u16 reg = REG_VCOUNT;
    u16 low0 = W_US_COUNT0;
    u16 high = W_US_COUNT1;
    u16 low1 = W_US_COUNT0;
    
    if (low0 > low1)
        high = W_US_COUNT1;
    
    global_vtsf_var = (2 * (low1 | (high << 16)) - 127 * reg) >> 7;
}

static void CalcVRemain() {    
    wifiWork.v_tsf <<= 6;
    
    u16 low0 = W_US_COUNT0;
    u16 high = W_US_COUNT1;
    u16 low1 = W_US_COUNT0;
    
    if (low0 > low1)
        high = W_US_COUNT1;
    
    u32 next_0line_tsf = (low1 | (high << 16));
    u16 vcount = REG_VCOUNT;
    next_0line_tsf &= 0x3FFFC0;
    next_0line_tsf = ((2 * next_0line_tsf + 127 * (263 - vcount)) / 2) & 0x3FFFC0;
    
    if (wifiWork.v_tsf > next_0line_tsf) {
        wifiWork.v_remain = 0;
        
    } else {
        for (int i = 1; i < 30; i++) {
            wifiWork.v_tsf += 16715;
            
            if (wifiWork.v_tsf > next_0line_tsf) {
                wifiWork.v_remain = wifiWork.v_tsf - next_0line_tsf;
                if (wifiWork.v_remain > 16398)
                    wifiWork.v_remain = 0;
                
                return;
            }
        }
        
        wifiWork.v_remain = 0;
    }
}

static void ExpandVRemain() {
    u16 reg = REG_VCOUNT;
    if (reg >= 208 && reg < 212) {
        if (wifiWork.v_remain >= 0x7F) {
            int i;
            for (i = 0; i < 7; i++) {
                if (wifiWork.v_remain < 63 * i + 127) break;
            }
            REG_VCOUNT = reg + 1 - i;
            wifiWork.v_remain -= 63 * i;
        }
    }
}

static void FromVAlarmToMainThread() {
    myprintf("hello\n");
}

static void ParentVAlarmMP(void *unused) {
    if (wifiWork.mp_flag == 1) {
        setVAlarm(&wifiWork.valarm, 209, 263, ParentAdjustVSync, NULL);
        FromVAlarmToMainThread();
    }
}

static void ParentAdjustVSync(void *unused) {
    if (wifiWork.valarm_counter < 60) {
        wifiWork.valarm_counter++;
    } else {
        u16 reg = REG_VCOUNT;
        if (reg >= 209 && reg < 212) {
            REG_VCOUNT = reg;
            wifiWork.valarm_counter = 0;
        }
    }
    SetVTSF();
    setVAlarm(&wifiWork.valarm, wifiWork.mp_parentVCount, 263, ParentVAlarmMP, NULL);
}

static void ChildVAlarmMP(void *unused) {
    if (wifiWork.mp_flag == 1) {
        setVAlarm(&wifiWork.valarm, 200, 263, ChildAdjustVSync1, NULL);
        if (wifiWork.mp_lifeTimeTick != 0) {
            u64 tick = getTick() | 1;
            
            if (wifiWork.mp_lastRecvTick[0] != 0 && (tick - wifiWork.mp_lastRecvTick[0]) > wifiWork.mp_lifeTimeTick) {
                wifiWork.mp_lastRecvTick[0] = 0;
                
                // TODO: internal auto disconnect message
                return;
            }
        }
        
        FromVAlarmToMainThread();
    }
}

static void ChildAdjustVSync2(void *unused) {
    ExpandVRemain();
    if (wifiWork.v_remain >= 127) {
        wifiWork.VSyncFlag = 0;
    }
    setVAlarm(&wifiWork.valarm, wifiWork.mp_childVCount, 263, ChildVAlarmMP, NULL);
}

static void ChildAdjustVSync1(void *unused) {
    wifiWork.v_tsf = global_vtsf_var;
    if (wifiWork.v_tsf_bak != wifiWork.v_tsf) {
        wifiWork.v_tsf_bak = wifiWork.v_tsf;
        CalcVRemain();
    }
    if (wifiWork.v_remain <= 127) {
        wifiWork.VSyncFlag = 1;
        setVAlarm(&wifiWork.valarm, wifiWork.mp_childVCount, 263, ChildVAlarmMP, NULL);
    } else {
        setVAlarm(&wifiWork.valarm, 208, 263, ChildAdjustVSync2, NULL);
    }
}

/**********/

static u32 my_alloc(u32 sz) {
    u32 ret = (u32)malloc(sz);
    //myprintf("ac: %lu @ %p\n", sz, ret);
    return ret;
}

static u32 my_free(void *ptr) {
    free(ptr);
    return 0;
}

void InitWiFi() {
    // Make sure it's zeroed
    memset(&wifiWork, 0, sizeof(wifiWork)); 
    
    InitMsgQueue(&wifiWork.sendMsgQueue, sendArr, 20);
    InitMsgQueue(&wifiWork.recvMsgQueue, recvArr, 20);
    InitMsgQueue(&wifiWork.confirmQueue, cfmArr, 20);
    InitMsgQueue(&wifiWork.requestQueue, indArr, 20);
    
    WlInit wlInit; 
    wlInit.workingMemAdrs = (u32)wvrWlWork;
    wlInit.stack = (void *)((u32)wvrWlStack + sizeof(wvrWlStack));
    wlInit.stacksize = sizeof(wvrWlStack);
    
    wlInit.sendMsgQueuep = &wifiWork.sendMsgQueue;
    wlInit.recvMsgQueuep = &wifiWork.recvMsgQueue;
    
    wlInit.heapType = 1;
    wlInit.heapFunc.ext.alloc = my_alloc;
    wlInit.heapFunc.ext.free = my_free;
    
    wlInit.camAdrs = wvrWlStaElement;
    wlInit.camSize = sizeof(wvrWlStaElement);
    
    // Real wifi creates indicate thread and request thread
    
    u32 ret = WL_InitDriver(&wlInit);
    
    // then the loop starts with this
    TASK_MAN* pTaskMan = &wlMan->TaskMan; // r8 - :140
    pTaskMan->NextPri = 0;
    pTaskMan->CurrTaskID = 0;
}

static void DriverLoop() {
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

static void IndicateMlmeAuthenticate(WlCmdReq *req) {
    // It's sent to ARM9
}

static void IndicateMlmeDeAuthenticate(WlCmdReq *req) {
    WlMlmeDeAuthInd *ind = (WlMlmeDeAuthInd *)req;
    
    if (wifiWork.state == 7 || wifiWork.state == 9) {
        int x = enterCriticalSection();
        int aid;
        
        for (aid = 1; aid < 16; aid++) {
            if ((wifiWork.child_bitmap & (1 << aid)) != 0 && memcmp(ind->peerMacAdrs, wifiWork.childMacAddress[aid-1], 6) == 0) {
                wifiWork.child_bitmap &= ~(1 << aid);
                wifiWork.mp_readyBitmap &= ~(1 << aid);
                wifiWork.mp_lastRecvTick[aid] = 0;
                memset(wifiWork.childMacAddress[aid-1], 0, 6);
                break;
            }
        }
        
        leaveCriticalSection(x);
        
        if (aid) {
            // transmit to ARM9
        }
        
    } else {
        int x = enterCriticalSection();
        
        if (wifiWork.child_bitmap != 0) {
            bool bCleanQueue = false;
            if (wifiWork.mp_flag == 1) {
                wifiWork.mp_flag = 0;
                bCleanQueue = true;
                WiFi_CancelVAlarm();
                // SetThreadPriorityLow
            }
            wifiWork.child_bitmap = 0;
            wifiWork.mp_readyBitmap = 0;
            wifiWork.ks_flag = 0;
            wifiWork.dcf_flag = 0;
            wifiWork.VSyncFlag = 0;
            WiFi_ResetSizeVars();
            wifiWork.beaconIndicateFlag = 0;
            wifiWork.state = 3;
            
            leaveCriticalSection(x);
            
            // Transmit to ARM9
            
            if (bCleanQueue)
                WiFi_CleanSendQueue(1);
            
        } else {
            leaveCriticalSection(x);
        }
    }
}

static void IndicateMlmeAssociate(WlCmdReq *req) {
    WlMlmeAssInd *ind = (WlMlmeAssInd *)req;
    
    if (ind->aid > 0 && ind->aid < 16) {
        if (wifiWork.pparam.entryFlag) {
            int x = enterCriticalSection();
            wifiWork.child_bitmap |= (1 << ind->aid);
            wifiWork.mp_readyBitmap &= ~(1 << ind->aid);
            u64 tick = getTick() | 1;
            wifiWork.mp_lastRecvTick[ind->aid] = tick;
            memcpy(wifiWork.childMacAddress[ind->aid - 1], ind->peerMacAdrs, 6);
            leaveCriticalSection(x);
            
            memset(wifiWork.portSeqNo[ind->aid], 0, 0x10);
            
            // callback to ARM9
            
        } else {
            // internal request to WiFi_AutoDeAuth
        }
        
    } else {
        // invalid aid %d !
    }
}

static void IndicateMlmeReAssociate(WlCmdReq *req) {
    // callback to ARM9
}

static void IndicateMlmeDisAssociate(WlCmdReq *req) {
    // callback to ARM9
}

static void IndicateMlmeBeaconLost(WlCmdReq *req) {
}

static void IndicateMlmeBeaconSend(WlCmdReq *req) {
}

static void IndicateMlmeBeaconRecv(WlCmdReq *req) {
}

static void IndicateMaData(WlCmdReq *req) {
    WlMaDataInd *ind = (WlMaDataInd *)req;
    
    if (wifiWork.dcf_flag) {
        // RSSI...
        
        if (!WiFi_CheckMacAddress(ind->frame.srcAdrs) && ind->frame.length <= 0x5E4) {
            //wifiWork.dcf_recvBufSel ^= 1;
            // DCF stuff, then ARM9 callback
        }
    }
}

static void MaMultiPollAckAlarmCallback(void *unused) {
}

static void IndicateMaMultiPoll(WlCmdReq *req) {
    // RSSI...
    WlMaMpInd *ind = (WlMaMpInd *)ind;
    
    if (wifiWork.mp_flag) {
        if (wifiWork.mp_vsyncFlag == 1)
            wifiWork.mp_vsyncFlag = 0;
        
        u16 emptyFlag = wifiWork.mp_bufferEmptyFlag;
        
        wifiWork.mp_recvBufSel ^= 1;
        WMMpRecvBuf *recvBuf = wifiWork.mp_recvBuf[wifiWork.mp_recvBufSel];
        
        u32 size = ind->frame.length + 48;
        if (size > wifiWork.mp_recvBufSize) {
            size = wifiWork.mp_recvBufSize;
        }
        
        memcpy(recvBuf, &ind->frame, size);
        
        int x = enterCriticalSection();
        
        bool bUnk = false;
        
        if (wifiWork.mp_waitAckFlag) {
            bUnk = true;
            cancelAlarm(&wifiWork.mpAckAlarm);
        }
        
        wifiWork.mp_waitAckFlag = 1;
        wifiWork.mp_ackTime = recvBuf->ackTimeStamp;
        wifiWork.mp_isPolledFlag = ((ind->frame.txKeySts & 0x2000) == 0x2000);
        
        // idk the formula
        u32 tmp = 16 * ((u16)(recvBuf->ackTimeStamp - recvBuf->timeStamp) + 128);
        u64 tick = (33514 * (u64)tmp) >> 16;
        
        setAlarm(&wifiWork.mpAckAlarm, tick, MaMultiPollAckAlarmCallback, NULL);
        
        // mp_setDataFlag has been removed
        wifiWork.mp_bufferEmptyFlag = ((ind->frame.txKeySts & 0x2800) == 0x2800);
        wifiWork.mp_sentDataFlag = ((ind->frame.txKeySts & 0x6000) == 0x6000);
        
        if (wifiWork.mp_isPolledFlag) {
            s32 childSize = ((recvBuf->txop - 102) / 4) - 32;
            if (childSize >= 0) {
                if (childSize > wifiWork.mp_childMaxSize)
                    childSize = wifiWork.mp_childMaxSize;
                
                if (childSize != wifiWork.mp_childSize)
                    WiFi_SetChildSize(childSize);
            }
        }
        
        leaveCriticalSection(x);
        
        if (bUnk) {
            if (emptyFlag)
                WiFi_FlushSendQueue(1, 0);
            
            // arm9 stuff here
        }
        
        if (wifiWork.mp_isPolledFlag) {
            // isn't this useless?
            memcpy(recvBuf->destAdrs, ind->frame.destAdrs, 6);
            memcpy(recvBuf->srcAdrs, ind->frame.srcAdrs, 6);
            
            if (recvBuf->length < 2) {
                recvBuf->length = 0;
                
                // arm9 stuff here
                
            } else {
                recvBuf->length -= 2;
                wifiWork.mp_vsyncOrderedFlag = ((recvBuf->wmHeader & 0x8000) == 0x8000);
                
                // arm9 stuff
                
                if (recvBuf->length != 0)
                    WiFi_ParsePortPacket(0, recvBuf->wmHeader, recvBuf->data, recvBuf->length, recvBuf);
            }
            
            if (wifiWork.mp_lifeTimeTick != 0) {
                wifiWork.mp_lastRecvTick[0] = getTick() | 1;
            }
            
        } else {
            if (recvBuf->length >= 2) {
                wifiWork.mp_vsyncOrderedFlag = ((recvBuf->wmHeader & 0x8000) == 0x8000);
            }
        }
    }
}

static void IndicateMaMultiPollEnd(WlCmdReq *req) {
}

static void IndicateMaMultiPollAck(WlCmdReq *req) {
}

static void IndicateMaFatalErr(WlCmdReq *req) {
}

static void IndicateFuncDummy(WlCmdReq *req) {
}

static void Indicate(WlCmdReq *req) {
    if (wifiWork.state == 1)
        return;
    
    switch (req->header.code) {
        case 0x85:
            IndicateMlmeAuthenticate(req);
            break;
            
        case 0x86:
            IndicateMlmeDeAuthenticate(req);
            break;
            
        case 0x87:
            IndicateMlmeAssociate(req);
            break;
            
        case 0x88:
            IndicateMlmeReAssociate(req);
            break;
            
        case 0x89:
            IndicateMlmeDisAssociate(req);
            break;
            
        case 0x8C:
            IndicateMlmeBeaconLost(req);
            break;
            
        case 0x8D:
            IndicateMlmeBeaconSend(req);
            break;
            
        case 0x8E:
            IndicateMlmeBeaconRecv(req);
            break;
            
        case 0x180:
            IndicateMaData(req);
            break;
            
        case 0x182:
            IndicateMaMultiPoll(req);
            break;
            
        case 0x184:
            IndicateMaMultiPollEnd(req);
            break;
            
        case 0x185:
            IndicateMaMultiPollAck(req);
            break;
            
        case 0x186:
            IndicateMaFatalErr(req);
            break;
            
        default:
            IndicateFuncDummy(req);
    }
}

static void IndicateThreadLoop() {
    // Check for messages
    void *msg;
    while (OS_ReceiveMessage(&wifiWork.recvMsgQueue, &msg, 0)) {
        WlCmdReq *pReq = (WlCmdReq *)msg;
        if ((pReq->header.code & ~0x17F) == 0x80) {
            Indicate(pReq);
            free(pReq);
            
        } else {
            if (!OS_SendMessage(&wifiWork.confirmQueue, msg, 0))
                myprintf("Confirm queue is full!\n");
        }
    }
}

void UpdateWiFi() { // TaskMan.c:136
    // Call the WiFi loop
    DriverLoop();
    IndicateThreadLoop();
}