#include "ap.h"
#include "power/power.h"
#include "network/wifi/wifi.h"

LOG_MODULE_REGISTER(ap, LOG_LEVEL_DBG);

#define AP_LONG_PRESS_MS  1500   // USER 버튼 롱프레스 판정 시간
#define AP_USER_BUTTON    0      // buttonGetPressed(0) = GPIO18 (USER)


void apInit(void)
{
  moduleInit();
}

// USER 버튼 롱프레스(~1.5초) 시 디버그(계속 깨어있기) 모드를 토글한다.
static void apCheckLongPress(void)
{
  if (!buttonGetPressed(AP_USER_BUTTON))
    return;

  uint32_t pre_time = millis();
  bool     toggled  = false;

  while (buttonGetPressed(AP_USER_BUTTON))
  {
    if (!toggled && (millis() - pre_time) >= AP_LONG_PRESS_MS)
    {
      powerSetStayAwake(!powerStayAwake());
      toggled = true;
    }
    delay(50);
  }
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

  // 버튼 웨이크 후 롱프레스면 디버그 모드 진입. 디버그 모드 동안은 딥슬립하지 않고
  // 깨어있으며(CLI/콘솔 가능), 다시 롱프레스하면 해제되어 딥슬립으로 복귀한다.
  do
  {
    apCheckLongPress();
    delay(50);
  } while (powerStayAwake());

  powerSleepToNextMinute(); // 딥슬립 진입 (복귀하지 않음, 웨이크 시 재부팅)

  while (1)
  {
    delay(1000);
  }
}
