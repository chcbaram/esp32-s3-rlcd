#include "ap.h"
#include "power/power.h"
#include "network/wifi/wifi.h"

LOG_MODULE_REGISTER(ap, LOG_LEVEL_DBG);


void apInit(void)
{
  moduleInit();
}

void apMain(void)
{
  // WiFi 동기(필요한 경우)가 끝날 때까지 대기 후 딥슬립. 동기 불필요 시 즉시 통과.
  uint32_t pre_time = millis();
  while (!wifiSyncDone() && (millis() - pre_time) < 20000)
  {
    delay(100);
  }

  delay(500); // 시계 화면 첫 렌더 대기

  powerSleepToNextMinute(); // 딥슬립 진입 (복귀하지 않음, 웨이크 시 재부팅)

  while (1)
  {
    delay(1000);
  }
}
