#ifndef WIFI_H_
#define WIFI_H_


#include "ap_def.h"



#ifdef __cplusplus
extern "C" {
#endif


bool wifiInit(void);
bool wifiIsConnected(void);
bool wifiSyncDone(void);
bool wifiWrite(void *p_data, uint32_t length);
bool wifiPrintf(const char *fmt, ...);
const char *wifiGetName(void);
const char *wifiGetIPAddress(void);
int8_t wifiGetRssi(void);

#ifdef __cplusplus
}
#endif

#endif