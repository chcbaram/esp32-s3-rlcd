#include "shtc3.h"




#ifdef _USE_HW_SHTC3
#include <zephyr/drivers/sensor.h>

#include "cli.h"
#include "cli_gui.h"


#define NAME_DEF(x)  x, #x

#ifdef _USE_HW_RTOS
#define lock()      k_mutex_lock(&mutex_lock, K_FOREVER);
#define unLock()    k_mutex_unlock(&mutex_lock);
static K_MUTEX_DEFINE(mutex_lock);
#else
#define lock()      
#define unLock()    
#endif

#define SHTC3_FILTER_ALPHA   0.01f



typedef struct
{
  struct device const *h_dev;

  float prev_temp;
  float prev_humidity;
  bool  is_first_read; // 최초 읽기인지 확인하는 플래그
} shtc3_tbl_t;


#if CLI_USE(HW_SHTC3)
static void cliCmd(cli_args_t *args);
#endif



static bool is_init = false;


static shtc3_tbl_t shtc3_tbl[SHTC3_MAX_CH] =
{
  { .h_dev = DEVICE_DT_GET(DT_NODELABEL(shtc3)), .is_first_read = true},
};


bool shtc3Init(void)
{
  bool ret = true;  


  for (int i=0; i<SHTC3_MAX_CH; i++)
  {
    if (!device_is_ready(shtc3_tbl[i].h_dev))
    {
      logPrintf("[E_] shtc3 : devivce %s not ready\n", shtc3_tbl[i].h_dev->name);
      ret = false;
      break;
    }

  }

  is_init = ret;

  logPrintf("[%s] shtc3Init()\n", is_init ? "OK":"E_"); 

#if CLI_USE(HW_SHTC3)
  cliAdd("shtc3", cliCmd);
#endif
  return ret;
}

bool shtc3IsInit(void)
{
  return is_init;
}

bool shtc3GetInfo(uint8_t ch, shtc3_info_t *p_info)
{
  struct sensor_value temp, humidity;


  if (sensor_sample_fetch(shtc3_tbl[ch].h_dev) < 0)
  {
    return false;
  }

  if (sensor_channel_get(shtc3_tbl[ch].h_dev, SENSOR_CHAN_AMBIENT_TEMP, &temp) < 0)
  {
    return false;
  }

  if (sensor_channel_get(shtc3_tbl[ch].h_dev, SENSOR_CHAN_HUMIDITY, &humidity) < 0)
  {
    return false;
  }

  // 1. Raw 데이터 변환
  p_info->temp     = (float)temp.val1 + (float)temp.val2 / 1000000.f;
  p_info->humidity = (float)humidity.val1 + (float)humidity.val2 / 1000000.f;

  // 2. EMA 필터 적용
  lock(); // 멀티스레드 환경 대비 (구현하신 lock 활용)

  if (shtc3_tbl[ch].is_first_read)
  {
    // 전원 켜고 첫 데이터라면 이전 값이 없으므로 현재 값을 그대로 필터 초기값으로 설정
    shtc3_tbl[ch].prev_temp     = p_info->temp;
    shtc3_tbl[ch].prev_humidity = p_info->humidity;
    shtc3_tbl[ch].is_first_read = false;
  }
  else
  {
    // EMA 공식 적용
    shtc3_tbl[ch].prev_temp = (SHTC3_FILTER_ALPHA * p_info->temp) +
                              ((1.0f - SHTC3_FILTER_ALPHA) * shtc3_tbl[ch].prev_temp);

    shtc3_tbl[ch].prev_humidity = (SHTC3_FILTER_ALPHA * p_info->humidity) +
                                  ((1.0f - SHTC3_FILTER_ALPHA) * shtc3_tbl[ch].prev_humidity);
  }

  // 3. 필터링된 데이터 채우기
  p_info->temp_filtered     = shtc3_tbl[ch].prev_temp;
  p_info->humidity_filtered = shtc3_tbl[ch].prev_humidity;

  unLock();

  return true;
}

#if CLI_USE(HW_SHTC3)
void cliCmd(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info") == true)
  {
    cliPrintf("shtc3 init : %d\n", is_init);
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "show") == true)
  {
    cliShowCursor(false);
    while(cliKeepLoop())
    {
      for (int i=0; i<SHTC3_MAX_CH; i++)
      {
        shtc3_info_t info;
        bool sensor_ret;


        sensor_ret = shtc3GetInfo(i, &info);

        cliPrintf("[%s] %d: temp %3.2f(%3.2f) humidity %3.2f(%3.2f) %% \n",
                  sensor_ret ? "OK" : "E_",
                  i,
                  (double)info.temp_filtered,
                  (double)info.temp,
                  (double)info.humidity_filtered,
                  (double)info.humidity);
      }
      delay(100);
      cliMoveUp(SHTC3_MAX_CH);
    }
    cliMoveDown(SHTC3_MAX_CH);
    cliShowCursor(true);
    ret = true;
  }

  if (ret != true)
  {
    cliPrintf("shtc3 info\n");
    cliPrintf("shtc3 show\n");
  }
}
#endif

#endif