#ifndef WIFI_API_H
#define WIFI_API_H

#include "Marionea.h"

struct WMGameInfo {
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
};

extern struct OSMessageQueue wvrSendMsgQueue, wvrRecvMsgQueue, wvrConfirmQueue, wvrIndicateQueue;

WlMlmeResetCfm * WiFi_MlmeReset(u16 mib); 
WlMlmePowerMgtCfm * WiFi_MlmePowerMgt(u16 pwrMgtMode, u16 wakeUp, u16 recieveDtims); 
WlMlmeScanCfm * WiFi_MlmeScan(u32 bufSize, u16 *bssid, u16 ssidLength, u8 *ssid, u16 scanType, u8 *channelList, u16 maxChannelTime); 
WlMlmeJoinCfm * WiFi_MlmeJoin(u16 timeOut, WlBssDesc *bssDesc); 
WlMlmeAuthCfm * WiFi_MlmeAuth(u16 *peerMacAdrs, u16 algorithm, u16 timeOut); 
WlMlmeDeAuthCfm * WiFi_MlmeDeAuth(u16 *peerMacAdrs, u16 reasonCode); 
WlMlmeAssCfm * WiFi_MlmeAss(u16 *peerMacAdrs, u16 listenInterval, u16 timeOut); 
WlMlmeStartCfm * WiFi_MlmeStart(u16 ssidLength, u8 *ssid, u16 beaconPeriod, u16 dtimPeriod, u16 channel, u16 basicRateSet, u16 supportRateSet, u16 gameInfoLength, struct WMGameInfo *gameInfo); 
WlMlmeMeasChanCfm * WiFi_MlmeMeasChan(u16 ccaMode, u16 edThreshold, u16 measureTime, u8 *channelList); 
WlMaDataCfm * WiFi_MaData(WlTxFrame *frame); 
WlMaKeyDataCfm * WiFi_MaKeyData(u16 length, u16 wmHeader, u16 *keyDatap); 
WlMaMpCfm * WiFi_MaMp(u16 resume, u16 retryLimit, u16 txop, u16 pollBitmap, u16 tmptt, u16 currTsf, u16 dataLength, u16 wmHeader, u16 *datap); 
WlMaClrDataCfm * WiFi_MaClrData(u16 flag); 
WlParamSetCfm * WiFi_ParamSetAll(WlParamSetAllReq *req); 
WlParamSetCfm * WiFi_ParamSetWepKeyId(u16 wepKeyId); 
WlParamSetCfm * WiFi_ParamSetBeaconLostTh(u16 beaconLostTh); 
WlParamSetCfm * WiFi_ParamSetSsidMask(u8 *mask); 
WlParamSetCfm * WiFi_ParamSetPreambleType(u16 type); 
WlParamSetCfm * WiFi_ParamSetLifeTime(u16 tableNumber, u16 camLifeTime, u16 frameLifeTime); 
WlParamSetCfm * WiFi_ParamSetMaxConn(u16 count); 
WlParamSetCfm * WiFi_ParamSetBeaconSendRecvInd(u16 enableMessage); 
WlParamSetCfm * WiFi_ParamSetNullKeyMode(u16 mode); 
WlParamSetCfm * WiFi_ParamSetBeaconPeriod(u16 beaconPeriod); 
WlParamSetCfm * WiFi_ParamSetGameInfo(u16 gameInfoLength, u16 *gameInfo); 
WlParamGetMacAddressCfm * WiFi_ParamGetMacAddress(); 
WlParamGetEnableChannelCfm * WiFi_ParamGetEnableChannel(); 
WlParamGetModeCfm * WiFi_ParamGetMode(); 
WlDevShutdownCfm * WiFi_DevShutdown(); 
WlDevIdleCfm * WiFi_DevIdle(); 
WlDevClass1Cfm * WiFi_DevClass1(); 
WlDevRebootCfm * WiFi_DevReboot(); 
WlDevClrInfoCfm * WiFi_DevClrInfo(); 
WlDevGetVerInfoCfm * WiFi_DevGetVerInfo(); 
WlDevGetInfoCfm * WiFi_DevGetInfo(); 
WlDevGetStateCfm * WiFi_DevGetState(); 

void InitWiFi();
void UpdateWiFi();

#endif