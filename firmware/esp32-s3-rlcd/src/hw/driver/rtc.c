#include "rtc.h"


#ifdef _USE_HW_RTC
#include <zephyr/drivers/rtc.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/timeutil.h>
#include <zephyr/posix/sys/time.h>
#include <time.h>
#include <errno.h>

#include "cli.h"


// PCF85063A : Zephyr 드라이버가 Control_1의 STOP 비트를 건드리지 않아, STOP=1 이면
// 시각 카운터가 멈춘다. 초기화 시 직접 STOP=0 으로 써서 발진기/카운터를 시작시킨다.
#define PCF85063A_I2C_ADDR   0x51
#define PCF85063A_CTRL1_REG  0x00
#define PCF85063A_RAM_REG    0x03   // 범용 RAM 1바이트 (RTC 전원 유지 동안 보존)
#define RTC_BOOT_MAGIC       0xA5   // 이 값이 있으면 딥슬립 웨이크, 없으면 콜드부팅

static const struct device *const rtc_dev = DEVICE_DT_GET(DT_NODELABEL(rtc0));
static const struct device *const rtc_i2c = DEVICE_DT_GET(DT_NODELABEL(i2c0));
static bool is_init = false;


#if CLI_USE(HW_RTC)
static void cliRtc(cli_args_t *args);
#endif


bool rtcInit(void)
{
  setenv("TZ", "KST-9", 1);
  tzset();

  is_init = device_is_ready(rtc_dev);

  // Control_1 = 0x00 : STOP=0(카운터 동작), 24시간, CAP_SEL=7pF → 발진기/시각 진행 시작
  if (is_init && device_is_ready(rtc_i2c))
  {
    uint8_t buf[2] = {PCF85063A_CTRL1_REG, 0x00};
    i2c_write(rtc_i2c, buf, sizeof(buf), PCF85063A_I2C_ADDR);
  }

  logPrintf("[%s] rtcInit()\n", is_init ? "OK" : "E_");

#if CLI_USE(HW_RTC)
  cliAdd("rtc", cliRtc);
#endif
  return is_init;
}

bool rtcIsInit(void)
{
  return is_init;
}

// PCF85063A RAM 바이트(0x03)를 이용해 콜드부팅(전원인가) vs 딥슬립 웨이크를 판별한다.
// RAM 은 RTC 전원이 살아있는 딥슬립 동안 유지되고, 전원 완전 차단(백업 없음) 시 소실된다.
bool rtcIsColdBoot(void)
{
  static bool checked = false;
  static bool cold    = true;

  if (!checked)
  {
    checked = true;

    if (device_is_ready(rtc_i2c))
    {
      uint8_t magic = 0;

      if (i2c_reg_read_byte(rtc_i2c, PCF85063A_I2C_ADDR, PCF85063A_RAM_REG, &magic) == 0)
        cold = (magic != RTC_BOOT_MAGIC);

      i2c_reg_write_byte(rtc_i2c, PCF85063A_I2C_ADDR, PCF85063A_RAM_REG, RTC_BOOT_MAGIC);
    }
  }
  return cold;
}

bool rtcSyncSystemFromRtc(void)
{
  struct rtc_time rt;
  int             ret;

  if (!is_init)
    return false;

  ret = rtc_get_time(rtc_dev, &rt);
  if (ret < 0)
  {
    // -ENODATA(-61) = 초 레지스터 OS 플래그 set : 크리스털 미발진 또는 시간 미설정으로
    // RTC가 시간을 세지 못하는 상태. 그 외는 I2C 통신 오류.
    logPrintf("[E_] rtcSyncSystemFromRtc fail : ret=%d %s\n", ret,
              ret == -ENODATA ? "(OS flag: RTC not counting)" : "(i2c error)");
    return false;
  }

  logPrintf("[OK] rtc read : %04d-%02d-%02d %02d:%02d:%02d\n",
            rt.tm_year + 1900, rt.tm_mon + 1, rt.tm_mday,
            rt.tm_hour, rt.tm_min, rt.tm_sec);

  struct tm tm_ref = {
    .tm_sec  = rt.tm_sec,
    .tm_min  = rt.tm_min,
    .tm_hour = rt.tm_hour,
    .tm_mday = rt.tm_mday,
    .tm_mon  = rt.tm_mon,
    .tm_year = rt.tm_year,
  };

  struct timespec ts = {
    .tv_sec  = timeutil_timegm(&tm_ref),
    .tv_nsec = 0,
  };

  return clock_settime(CLOCK_REALTIME, &ts) == 0;
}

bool rtcSyncRtcFromSystem(void)
{
  struct timespec ts;
  struct tm       tm_ref;

  if (!is_init || clock_gettime(CLOCK_REALTIME, &ts) != 0)
    return false;

  gmtime_r(&ts.tv_sec, &tm_ref);

  struct rtc_time rt = {
    .tm_sec  = tm_ref.tm_sec,
    .tm_min  = tm_ref.tm_min,
    .tm_hour = tm_ref.tm_hour,
    .tm_mday = tm_ref.tm_mday,
    .tm_mon  = tm_ref.tm_mon,
    .tm_year = tm_ref.tm_year,
    .tm_wday = tm_ref.tm_wday,
    .tm_yday = tm_ref.tm_yday,
    .tm_nsec = 0,
  };

  return rtc_set_time(rtc_dev, &rt) == 0;
}

#if CLI_USE(HW_RTC)
void cliRtc(cli_args_t *args)
{
  bool ret = false;

  if (args->argc == 1 && args->isStr(0, "info"))
  {
    struct rtc_time rt;

    cliPrintf("rtc init : %d\n", is_init);
    if (is_init && rtc_get_time(rtc_dev, &rt) == 0)
    {
      struct tm utc = {
        .tm_sec = rt.tm_sec, .tm_min = rt.tm_min, .tm_hour = rt.tm_hour,
        .tm_mday = rt.tm_mday, .tm_mon = rt.tm_mon, .tm_year = rt.tm_year,
      };
      time_t    epoch = timeutil_timegm(&utc);
      struct tm local;
      localtime_r(&epoch, &local);

      cliPrintf("rtc time : %04d-%02d-%02d %02d:%02d:%02d (local)\n",
                local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                local.tm_hour, local.tm_min, local.tm_sec);
    }
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "sync"))
  {
    cliPrintf("system <- rtc : %s\n", rtcSyncSystemFromRtc() ? "OK" : "Fail");
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("rtc info\n");
    cliPrintf("rtc sync\n");
  }
}
#endif

#endif
