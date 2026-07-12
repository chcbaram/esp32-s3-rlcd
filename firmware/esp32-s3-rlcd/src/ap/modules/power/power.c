#include "ap_def.h"
#include "power.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/poweroff.h>
#include <zephyr/posix/sys/time.h>
#include <esp_sleep.h>
#include <esp_attr.h>
#include <driver/gpio.h>
#include <time.h>


// 딥슬립 중에는 유지되고 전원 차단(콜드부팅) 시에만 초기화되는 RTC 메모리.
// 이 값이 매직과 다르면 콜드부팅, 같으면 딥슬립 웨이크로 판별한다.
#define POWER_BOOT_MAGIC  0xB00710ADu
static RTC_NOINIT_ATTR uint32_t power_boot_magic;


// LCD 제어핀 (devicetree 매핑) : DC=gpio0.5, CS=gpio1.8(=40), RST=gpio1.9(=41)
#define LCD_GPIO_DC   5
#define LCD_GPIO_CS   40
#define LCD_GPIO_RST  41

// NS4150B 스피커 앰프 셧다운 제어 : PA_CTRL = GPIO46 (LOW = shutdown)
#define PA_CTRL_GPIO  46


static bool powerInit(void);
static void cliPower(cli_args_t *args);


MODULE_DEF(power)
{
  .name     = "power",
  .priority = MODULE_PRI_LOW,
  .init     = powerInit,
};


void powerDeepSleep(uint32_t sec)
{
  gpio_hold_en(PA_CTRL_GPIO);
  gpio_hold_en(LCD_GPIO_DC);
  gpio_hold_en(LCD_GPIO_CS);
  gpio_hold_en(LCD_GPIO_RST);
  gpio_deep_sleep_hold_en();

  if (sec > 0)
    esp_sleep_enable_timer_wakeup((uint64_t)sec * 1000000ULL);

  sys_poweroff();
}

void powerSleepToNextMinute(void)
{
  struct timespec ts;
  uint32_t        sec = 60;

  if (clock_gettime(CLOCK_REALTIME, &ts) == 0)
  {
    struct tm tm_ref;
    localtime_r(&ts.tv_sec, &tm_ref);
    sec = 60 - tm_ref.tm_sec;
    if (sec == 0)
      sec = 60;
  }

  powerDeepSleep(sec);
}

bool powerIsColdBoot(void)
{
  static bool checked = false;
  static bool cold    = true;

  if (!checked)
  {
    cold             = (power_boot_magic != POWER_BOOT_MAGIC);
    power_boot_magic = POWER_BOOT_MAGIC;
    checked          = true;
  }
  return cold;
}

static bool powerInit(void)
{
  cliAdd("power", cliPower);
  logPrintf("[OK] powerInit()\n");
  return true;
}

static void cliPower(cli_args_t *args)
{
  bool ret = false;

  if (args->argc == 2 && args->isStr(0, "sleep"))
  {
    uint32_t sec = (uint32_t)args->getData(1);

    cliPrintf("deep sleep %u sec\n", sec);
    delay(100);
    powerDeepSleep(sec);
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("power sleep [sec]\n");
  }
}
