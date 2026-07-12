#include "rtc.h"


#ifdef _USE_HW_RTC
#include <zephyr/drivers/rtc.h>
#include <zephyr/sys/timeutil.h>
#include <zephyr/posix/sys/time.h>
#include <time.h>

#include "cli.h"


static const struct device *const rtc_dev = DEVICE_DT_GET(DT_NODELABEL(rtc0));
static bool is_init = false;


#if CLI_USE(HW_RTC)
static void cliRtc(cli_args_t *args);
#endif


bool rtcInit(void)
{
  setenv("TZ", "KST-9", 1);
  tzset();

  is_init = device_is_ready(rtc_dev);

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

bool rtcSyncSystemFromRtc(void)
{
  struct rtc_time rt;

  if (!is_init || rtc_get_time(rtc_dev, &rt) < 0)
    return false;

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
