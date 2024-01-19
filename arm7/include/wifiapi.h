#ifndef WIFI_API_H
#define WIFI_API_H

#include "Marionea.h"
#include <stdbool.h>

#pragma pack(push, 1)

typedef struct WMGameInfo {
    u16 magicNumber; // offset 00
    u8 ver; // offset 02
    u8 platform; // offset 03
    u32 ggid; // offset 04
    u16 tgid; // offset 08
    u8 userGameInfoLength; // offset 0a
    union {
        u8 gameNameCount_attribute; // offset 00
        u8 attribute; // offset 00
    }; // offset 0b
    u16 parentMaxSize; // offset 0c
    u16 childMaxSize; // offset 0e
    union {
        u16 userGameInfo[56]; // offset 00
        struct {
            u16 userName[4]; // offset 00
            u16 gameName[8]; // offset 08
            u16 padd1[44]; // offset 18
        } old_type; // offset 00
    }; // offset 10
} WMGameInfo;

typedef struct WMBssDesc {
    u16 length; // offset 00
    u16 rssi; // offset 02
    u8 bssid[6]; // offset 04
    u16 ssidLength; // offset 0a
    u8 ssid[32]; // offset 0c
    u16 capaInfo; // offset 2c
    struct {
        u16 basic; // offset 00
        u16 support; // offset 02
    } rateSet; // offset 2e
    u16 beaconPeriod; // offset 32
    u16 dtimPeriod; // offset 34
    u16 channel; // offset 36
    u16 cfpPeriod; // offset 38
    u16 cfpMaxDuration; // offset 3a
    u16 gameInfoLength; // offset 3c
    u16 otherElementCount; // offset 3e
    struct WMGameInfo gameInfo; // offset 40
} WMBssDesc;

typedef struct WMMPParam {
    u32 mask; // offset 00
    u16 minFrequency; // offset 04
    u16 frequency; // offset 06
    u16 maxFrequency; // offset 08
    u16 parentSize; // offset 0a
    u16 childSize; // offset 0c
    u16 parentInterval; // offset 0e
    u16 childInterval; // offset 10
    u16 parentVCount; // offset 12
    u16 childVCount; // offset 14
    u16 defaultRetryCount; // offset 16
    u8 minPollBmpMode; // offset 18
    u8 singlePacketMode; // offset 19
    u8 ignoreFatalErrorMode; // offset 1a
    u8 ignoreSizePrecheckMode; // offset 1b
} WMMPParam;

typedef struct WMMPTmpParam {
    u32 mask; // offset 00
    u16 minFrequency; // offset 04
    u16 frequency; // offset 06
    u16 maxFrequency; // offset 08
    u16 defaultRetryCount; // offset 0a
    u8 minPollBmpMode; // offset 0c
    u8 singlePacketMode; // offset 0d
    u8 ignoreFatalErrorMode; // offset 0e
    u8 reserved[1]; // offset 0f
} WMMPTmpParam;

typedef struct WMParentParam {
    u16* userGameInfo; // offset 00
    u16 userGameInfoLength; // offset 04
    u16 padding; // offset 06
    u32 ggid; // offset 08
    u16 tgid; // offset 0c
    u16 entryFlag; // offset 0e
    u16 maxEntry; // offset 10
    u16 multiBootFlag; // offset 12
    u16 KS_Flag; // offset 14
    u16 CS_Flag; // offset 16
    u16 beaconPeriod; // offset 18
    u16 rsv1[4]; // offset 1a
    u16 rsv2[8]; // offset 22
    u16 channel; // offset 32
    u16 parentMaxSize; // offset 34
    u16 childMaxSize; // offset 36
    u16 rsv[4]; // offset 38
} WMParentParam;

typedef struct WMMpRecvBuf {
    u16 rsv1[3]; // offset 00
    u16 length; // offset 06
    u16 rsv2[1]; // offset 08
    u16 ackTimeStamp; // offset 0a
    u16 timeStamp; // offset 0c
    u16 rate_rssi; // offset 0e
    u16 rsv3[2]; // offset 10
    u16 rsv4[2]; // offset 14
    u8 destAdrs[6]; // offset 18
    u8 srcAdrs[6]; // offset 1e
    u16 rsv5[3]; // offset 24
    u16 seqCtrl; // offset 2a
    u16 txop; // offset 2c
    u16 bitmap; // offset 2e
    u16 wmHeader; // offset 30
    u16 data[2]; // offset 32
} WMMpRecvBuf;

#pragma pack(pop)

extern OSMessageQueue wvrSendMsgQueue, wvrRecvMsgQueue, wvrConfirmQueue, wvrIndicateQueue;

WlMlmeResetCfm * WiFi_API_MlmeReset(u16 mib); 
WlMlmePowerMgtCfm * WiFi_API_MlmePowerMgt(u16 pwrMgtMode, u16 wakeUp, u16 recieveDtims); 
WlMlmeScanCfm * WiFi_API_MlmeScan(u16 *bssid, u16 ssidLength, u8 *ssid, u16 scanType, u8 *channelList, u16 maxChannelTime); 
WlMlmeJoinCfm * WiFi_API_MlmeJoin(u16 timeOut, WlBssDesc *bssDesc); 
WlMlmeAuthCfm * WiFi_API_MlmeAuth(u16 *peerMacAdrs, u16 algorithm, u16 timeOut); 
WlMlmeDeAuthCfm * WiFi_API_MlmeDeAuth(u16 *peerMacAdrs, u16 reasonCode); 
WlMlmeAssCfm * WiFi_API_MlmeAss(u16 *peerMacAdrs, u16 listenInterval, u16 timeOut); 
WlMlmeStartCfm * WiFi_API_MlmeStart(u16 ssidLength, u8 *ssid, u16 beaconPeriod, u16 dtimPeriod, u16 channel, u16 basicRateSet, u16 supportRateSet, u16 gameInfoLength, struct WMGameInfo *gameInfo); 
WlMlmeMeasChanCfm * WiFi_API_MlmeMeasChan(u16 ccaMode, u16 edThreshold, u16 measureTime, u8 *channelList); 
WlMaDataCfm * WiFi_API_MaData(WlTxFrame *frame); 
WlMaKeyDataCfm * WiFi_API_MaKeyData(u16 length, u16 wmHeader, u16 *keyDatap); 
WlMaMpCfm * WiFi_API_MaMp(u16 resume, u16 retryLimit, u16 txop, u16 pollBitmap, u16 tmptt, u16 currTsf, u16 dataLength, u16 wmHeader, u16 *datap); 
WlMaClrDataCfm * WiFi_API_MaClrData(u16 flag); 
WlParamSetCfm * WiFi_API_ParamSetAll(WlParamSetAllReq *req); 
WlParamSetCfm * WiFi_API_ParamSetWepKeyId(u16 wepKeyId); 
WlParamSetCfm * WiFi_API_ParamSetBeaconLostTh(u16 beaconLostTh); 
WlParamSetCfm * WiFi_API_ParamSetSsidMask(u8 *mask); 
WlParamSetCfm * WiFi_API_ParamSetPreambleType(u16 type); 
WlParamSetCfm * WiFi_API_ParamSetLifeTime(u16 tableNumber, u16 camLifeTime, u16 frameLifeTime); 
WlParamSetCfm * WiFi_API_ParamSetMaxConn(u16 count); 
WlParamSetCfm * WiFi_API_ParamSetBeaconSendRecvInd(u16 enableMessage); 
WlParamSetCfm * WiFi_API_ParamSetNullKeyMode(u16 mode); 
WlParamSetCfm * WiFi_API_ParamSetBeaconPeriod(u16 beaconPeriod); 
WlParamSetCfm * WiFi_API_ParamSetGameInfo(u16 gameInfoLength, u16 *gameInfo); 
WlParamGetMacAddressCfm * WiFi_API_ParamGetMacAddress(); 
WlParamGetEnableChannelCfm * WiFi_API_ParamGetEnableChannel(); 
WlParamGetModeCfm * WiFi_API_ParamGetMode(); 
WlDevShutdownCfm * WiFi_API_DevShutdown(); 
WlDevIdleCfm * WiFi_API_DevIdle(); 
WlDevClass1Cfm * WiFi_API_DevClass1(); 
WlDevRebootCfm * WiFi_API_DevReboot(); 
WlDevClrInfoCfm * WiFi_API_DevClrInfo(); 
WlDevGetVerInfoCfm * WiFi_API_DevGetVerInfo(); 
WlDevGetInfoCfm * WiFi_API_DevGetInfo(); 
WlDevGetStateCfm * WiFi_API_DevGetState(); 

void WiFi_SetChildSize(u16 childSize);
void WiFi_SetParentSize(u16 parentSize);
void WiFi_SetChildMaxSize(u16 childMaxSize);
void WiFi_SetParentMaxSize(u16 parentMaxSize);
void WiFi_ResetSizeVars();
bool WiFi_SetAllParams(bool bScan);
void WiFi_CopyParentParam(WMGameInfo *gameInfo, WMParentParam *pparam);
bool WiFi_CheckMacAddress(u16 *macAdr);
void WiFi_InitAlarm();
void WiFi_RequestResumeMP();
int WiFi_Initialize();
void WiFi_Reset();
void WiFi_End();
void WiFi_SetParentParam();
void WiFi_StartParent();
void WiFi_EndParent();
int WiFi_StartScanEx(u16 channelList, WMBssDesc *scanBuf, u16 scanBufSize, u16 maxChannelTime, u16 bssid[3], u16 scanType, u16 ssidLength, u8 ssid[32], u16 ssidMatchLength);
int WiFi_StartScan(u16 channel, WMBssDesc *scanBuf, u16 maxChannelTime, u16 bssid[3]);
int WiFi_EndScan();
int WiFi_StartConnectEx(WMBssDesc *bssDesc, u8 ssid[24], int powerSave, u16 authMode);
void WiFi_IndicateDisconnectionFromMyself(int parent, u16 aid, void *mac);
void WiFi_DisconnectCore();
void WiFi_Disconnect();
int WiFi_StartMP(u32 *recvBuf, u32 recvBufSize, u32 *sendBuf, u32 sendBufSize, WMMPParam *param, WMMPTmpParam *tmpParam);
void WiFi_SetMPData();
void WiFi_EndMP();
void WiFi_StartDCF();
void WiFi_SetDCFData();
void WiFi_EndDCF();
void WiFi_SetWEPKeyEx();
void WiFi_SetWEPKey();
void WiFi_SetGameInfo();
void WiFi_SetBeaconTxRxInd();
void WiFi_StartTestMode();
void WiFi_StopTestMode();
void WiFi_SetLifeTime();
void WiFi_MeasureChannel();
void WiFi_InitWirelessCounter();
void WiFi_GetWirelessCounter();
void WiFi_SetVAlarm();
void WiFi_CancelVAlarm();
void WiFi_InitVAlarm();
void WiFi_KickNextMP_Resume();
void WiFi_KickNextMP_Child();
void WiFi_KickNextMP_Parent();
void WiFi_VAlarmSetMPData();
void WiFi_ParsePortPacket(u16 aid, u16 wmHeader, u16 *data, u16 length, void *recvBuf);
void WiFi_CleanSendQueue(u16 aidBitmap);
void WiFi_FlushSendQueue(int timeout, u16 pollBitmap);
void WiFi_PutSendQueue(int childBitmap, u16 priority, u16 port, int destBitmap, u16 *sendData, u16 sendDataSize, void (*callback)(void *), void *arg);
void WiFi_ResumeMaMP(u16 pollBitmap);
void WiFi_SendMaMP(u16 pollBitmap);
void WiFi_SendMaKeyData();
void WiFi_InitSendQueue();
void WiFi_SetEntry();
void WiFi_AutoDeAuth();
void WiFi_CommonInit(u32 miscFlags);
void WiFi_Enable();
void WiFi_Disable();
bool WiFi_CommonWlIdle();
void WiFi_PowerOn();
void WiFi_PowerOff();
int WiFi_SetMPParameterCore(WMMPParam *param, WMMPParam *old_param);
void WiFi_SetMPParameter();
void WiFi_SetBeaconPeriod();
void WiFi_AutoDisconnect();
void WiFi_SetPowerSaveMode();
void WiFi_StartTestRxMode();
void WiFi_StopTestRxMode();
u16 WiFi_GetAllowedChannel(u16 bitField);

void InitWiFi();
void UpdateWiFi();

#endif