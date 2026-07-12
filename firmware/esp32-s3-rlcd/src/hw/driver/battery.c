#include "battery.h"

#ifdef _USE_HW_BATTERY
#include "adc.h"
#include "cli.h"

#define BAT_ADC_MAX_COUNT     10
#define BAT_SAMPLE_PERIOD_MS  1000
#define BAT_FILTER_THRESHOLD  5    // 퍼센트 변경 확정까지 필요한 연속 샘플 수

#ifdef _USE_HW_RTOS
#define lock()      k_mutex_lock(&mutex_lock, K_FOREVER);
#define unLock()    k_mutex_unlock(&mutex_lock);
static K_MUTEX_DEFINE(mutex_lock);
#else
#define lock()      
#define unLock()    
#endif

// 상태 머신용 명확한 enum 추가
typedef enum
{
  BAT_STATE_INIT = 0,
  BAT_STATE_IDLE,
  BAT_STATE_CHECK_CHANGE,
  BAT_STATE_UPDATE
} BatteryState_t;

// 룩업 테이블용 구조체 정의
typedef struct
{
  float voltage;
  int32_t percent;
} battery_lut_t;

typedef struct
{
  int32_t percent;
  float   voltage;
} battery_info_t;

#ifdef _USE_HW_CLI
static void cliBattery(cli_args_t *args);
#endif
static void batteryThread(void const *arg);
static int32_t batteryVoltageToPercent(float voltage);

static bool is_init = false;

// 전형적인 Li-Po 배터리의 10% 단위 OCV(Open-Circuit Voltage) 룩업 테이블
// 배터리 방전 곡선(방전 특성)에 맞춰 퍼센트가 비선형적으로 계산됩니다.
static const battery_lut_t battery_lut[] = {
  {4.10f, 100}, {4.05f, 90}, {3.96f, 80}, {3.89f, 70}, {3.82f, 60},
  {3.76f, 50},  {3.73f, 40}, {3.70f, 30}, {3.67f, 20}, {3.55f, 10}, {3.10f, 0}
};
#define BATTERY_LUT_SIZE (sizeof(battery_lut) / sizeof(battery_lut[0]))

static battery_info_t bat_info;
static float adc_vol_data[BAT_ADC_MAX_COUNT];
static uint8_t  adc_ch = 0;
static uint16_t buf_index = 0;

static K_THREAD_STACK_DEFINE(thread_stack, _HW_DEF_RTOS_THREAD_MEM_BATTERY);
static struct k_thread thread_data;


bool batteryInit(void)
{
  bool ret = true;

  for (int i=0; i<BAT_ADC_MAX_COUNT; i++)
  {
    adc_vol_data[i] = adcReadVoltage(adc_ch);
  }

  k_tid_t tid = k_thread_create(&thread_data, thread_stack,
                                  K_THREAD_STACK_SIZEOF(thread_stack),
                                  (k_thread_entry_t)batteryThread,
                                  NULL, NULL, NULL,
                                  _HW_DEF_RTOS_THREAD_PRI_BATTERY, 0, K_NO_WAIT);

  ret = tid != NULL ? true : false;  
  is_init = ret;

#ifdef _USE_HW_CLI
  cliAdd("battery", cliBattery);
#endif

  return ret;
}

bool batteryIsInit(void)
{
  return is_init;
}

bool batteryIsCharging(void)
{
  return true; 
}

int32_t batteryGetPercent(void)
{
  int32_t ret;
  lock();
  ret = bat_info.percent;
  unLock();
  return ret;
}

float batteryGetVoltage(void)
{
  float ret;
  lock();
  ret = bat_info.voltage;
  unLock();
  return ret;
}

// 선형 보간법(Linear Interpolation)을 적용한 룩업 테이블 퍼센트 연산 함수
static int32_t batteryVoltageToPercent(float voltage)
{
  // 최대/최소 예외 처리
  if (voltage >= battery_lut[0].voltage) return 100;
  if (voltage <= battery_lut[BATTERY_LUT_SIZE - 1].voltage) return 0;

  // 테이블 구간 검색 후 매핑
  for (size_t i = 0; i < BATTERY_LUT_SIZE - 1; i++)
  {
    if (voltage >= battery_lut[i + 1].voltage)
    {
      float v_high = battery_lut[i].voltage;
      float v_low  = battery_lut[i + 1].voltage;
      int32_t p_high = battery_lut[i].percent;
      int32_t p_low  = battery_lut[i + 1].percent;

      // 선형 보간 수식 계산
      float ratio = (voltage - v_low) / (v_high - v_low);
      return p_low + (int32_t)(ratio * (p_high - p_low));
    }
  }
  return 0;
}

void batteryThread(void const *arg)
{
  float bat_vol;
  uint8_t dif_cnt = 0;
  BatteryState_t state = BAT_STATE_INIT;
  int32_t target_percent = 0;

  while(1) 
  {
    // 1. ADC 샘플링 및 평균 필터 적용
    adc_vol_data[buf_index] = adcReadVoltage(adc_ch);
    buf_index = (buf_index + 1) % BAT_ADC_MAX_COUNT;

    float sum = 0.f;
    for (int i=0; i<BAT_ADC_MAX_COUNT; i++)
    {
      sum += adc_vol_data[i];
    }
    bat_vol = sum / BAT_ADC_MAX_COUNT; 

    // 2. 룩업 테이블 기반 실시간 가상 퍼센트 계산
    int32_t current_percent = batteryVoltageToPercent(bat_vol);

    // 3. 전압은 필터링 상태와 무관하게 항상 최신 상태로 실시간 업데이트 (개선 포인트)
    lock();    
    bat_info.voltage = bat_vol;
    unLock();

    // 4. 개선된 상태 머신 (퍼센트 안정화 필터링)
    switch(state)
    {
      case BAT_STATE_INIT:
        lock();        
        bat_info.percent = current_percent;
        unLock();
        target_percent = current_percent;
        state = BAT_STATE_IDLE;
        break;

      case BAT_STATE_IDLE:
        if (bat_info.percent != current_percent)
        {
          target_percent = current_percent; // 기준점 설정
          dif_cnt = 0;
          state = BAT_STATE_CHECK_CHANGE;
        }
        break;
      
      case BAT_STATE_CHECK_CHANGE:
        // 타겟 퍼센트가 유지되는 동안에만 카운트를 올립니다.
        if (current_percent == target_percent)
        {
          dif_cnt++;
          if (dif_cnt >= BAT_FILTER_THRESHOLD)
          {
            state = BAT_STATE_UPDATE;
          }
        }
        else
        {
          // 만약 검증 중 전압이 요동쳐서 또 퍼센트가 바뀌면, 새로운 타겟으로 갱신 후 카운트 리셋
          target_percent = current_percent;
          dif_cnt = 0;
        }
        break;

      case BAT_STATE_UPDATE:
        lock();
        bat_info.percent = target_percent;
        unLock();
        state = BAT_STATE_IDLE;
        break;

      default:
        state = BAT_STATE_IDLE;
        break;
    }

    delay(BAT_SAMPLE_PERIOD_MS);
  }
}

#ifdef _USE_HW_CLI
void cliBattery(cli_args_t *args)
{
  bool ret = false;

  if (args->argc == 1 && args->isStr(0, "info") == true)
  {
    cliPrintf("battery init : %d\n", is_init);
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "show") == true)
  {
    while(cliKeepLoop())
    {
      // (double) 명시적 형변환 유지
      cliPrintf("%03d%% %1.2fV\n", batteryGetPercent(), (double)batteryGetVoltage());
      delay(100);
    }
    ret = true;
  }

  if (ret != true)
  {
    cliPrintf("battery info\n");
    cliPrintf("battery show\n");
  }
}
#endif

#endif