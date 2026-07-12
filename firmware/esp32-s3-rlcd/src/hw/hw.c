#include "hw.h"

#include <driver/gpio.h>



bool hwInit(void)
{
  bspInit();

  // 딥슬립 웨이크(리셋) 후 홀드돼 있던 LCD(DC5/CS40/RST41)·앰프(46) 핀을 해제해
  // 드라이버가 다시 제어할 수 있게 한다. 콜드부팅에선 홀드가 없어 무해한 no-op.
  gpio_hold_dis(5);
  gpio_hold_dis(40);
  gpio_hold_dis(41);
  gpio_hold_dis(46);
  gpio_deep_sleep_hold_dis();

  cliInit();
  logInit();    
  uartInit();
  for (int i=0; i<HW_UART_MAX_CH; i++)
  {
    uartOpen(i, 115200);
  }
  
  delay(10);

  logOpen(HW_LOG_CH, 115200);
  logPrintf("\r\n[ Firmware Begin... ]\r\n");
  logPrintf("Booting..Name \t\t: %s\r\n", _DEF_BOARD_NAME);
  logPrintf("Booting..Ver  \t\t: %s\r\n", _DEF_FIRMWATRE_VERSION);  
  logPrintf("Booting..Date \t\t: %s\r\n", __DATE__); 
  logPrintf("Booting..Time \t\t: %s\r\n", __TIME__);   

  buttonInit();
  adcInit();
  batteryInit();

#ifdef _USE_HW_SD
  sdInit();
#endif
#ifdef _USE_HW_FATFS
  fatfsInit();
#endif
  shtc3Init();

#ifdef _USE_HW_RTC
  rtcInit();
  rtcSyncSystemFromRtc();
#endif

  lcdInit();
  
  return true;
}