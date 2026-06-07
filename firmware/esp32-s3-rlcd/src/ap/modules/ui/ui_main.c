#include "ap_def.h"
#include <time.h>
#include <zephyr/posix/sys/time.h>

#define MENU_NONE        (-1)
#define MENU_CLOCK_INDEX (MENU_COUNT - 1)
#define UI_DELAY_MS      5

typedef enum
{
  MENU_SYS_INFO = 0,
  MENU_LED_CTRL,
  MENU_SENSOR_STATUS,
  MENU_LCD_SETTINGS,
  MENU_CLOCK_MODE,

  MENU_COUNT
} menu_index_t;

typedef void (*menu_func_t)(void);

typedef struct
{
  const char *p_name;    // 메뉴판 출력 문자열
  menu_func_t draw_func; // 선택 시 실행할 함수 포인터
} menu_item_t;

static bool uiInit(void);
static void uiThread(void const *arg);
static void drawMenuSysInfo(void);
static void drawMenuLedCtrl(void);
static void drawMenuSensorStatus(void);
static void drawMenuLcdSettings(void);
static void drawMenuClockMode(void);
static void drawMainMenuPlatform(void);
static void drawStatusBar(const char *p_title);
static void drawFullClockScreen(void);

// Zephyr 커널 스레드 제어 변수
static K_THREAD_STACK_DEFINE(thread_stack, _HW_DEF_RTOS_THREAD_MEM_UI);
static struct k_thread thread_data;

// 메뉴 마스터 테이블 선언
const menu_item_t menu_table[] = {
  {"1. SYSTEM INFO",   drawMenuSysInfo     },
  {"2. LED CONTROLS",  drawMenuLedCtrl     },
  {"3. SENSOR STATUS", drawMenuSensorStatus},
  {"4. LCD SETTINGS",  drawMenuLcdSettings },
  {"5. CLOCK MODE",    drawMenuClockMode   }
};

// UI 런타임 상태 관리 변수
int current_selection = MENU_SYS_INFO;
int selected_menu     = MENU_NONE;

// 버튼 상태 감지 변수
bool prev_btn_down   = false;
bool prev_btn_select = false;

MODULE_DEF(ui){
  .name     = "ui",
  .priority = MODULE_PRI_LOW,
  .init     = uiInit};

static bool uiInit(void)
{
  bool ret;

  k_tid_t tid = k_thread_create(&thread_data, thread_stack,
                                K_THREAD_STACK_SIZEOF(thread_stack),
                                (k_thread_entry_t)uiThread,
                                NULL, NULL, NULL,
                                _HW_DEF_RTOS_THREAD_PRI_UI, 0, K_NO_WAIT);

  ret = tid != NULL ? true : false;
  logPrintf("[%s] uiInit()\n", ret ? "OK" : "E_");

  return ret;
}

/**
 * @brief 상단 타이틀 및 실시간 시간 정보 바 드로잉
 */
static void drawStatusBar(const char *p_title)
{
  int bar_height = 22;
  lcdDrawFillRect(0, 0, LCD_WIDTH, bar_height, white);

  if (p_title != NULL)
  {
    lcdPrintf(8, 3, black, p_title);
  }

  struct timespec ts;
  struct tm       tm_ref;

  if (clock_gettime(CLOCK_REALTIME, &ts) == 0)
  {
    localtime_r(&ts.tv_sec, &tm_ref);

    int time_x_pos = LCD_WIDTH - 119;
    if (time_x_pos < 60) time_x_pos = 120;

    lcdPrintf(time_x_pos, 3, black, "%02d-%02d %02d:%02d:%02d",
              tm_ref.tm_mon + 1,
              tm_ref.tm_mday,
              tm_ref.tm_hour,
              tm_ref.tm_min,
              tm_ref.tm_sec);
  }
  else
  {
    int time_x_pos = LCD_WIDTH - 95;
    lcdPrintf(time_x_pos, 3, black, "00-00 00:00");
  }
}

/**
 * @brief 레이아웃 타이포그래피가 적용된 전체 화면 시계 렌더링
 */
static void drawFullClockScreen(void)
{
  struct timespec ts;
  struct tm       tm_ref;

  // 한글 요일 출력을 위한 문자열 배열
  const char *wday_str[] = {"일", "월", "화", "수", "목", "금", "토"};

  if (clock_gettime(CLOCK_REALTIME, &ts) != 0)
  {
    lcdPrintf(10, 10, white, "Clock Error");
    return;
  }
  localtime_r(&ts.tv_sec, &tm_ref);


  int   bat_soc   = batteryGetPercent();
  float bat_volts = batteryGetVoltage();


  // 1. 길쭉한 가로형 배터리 몸체 크기 설정 (기존 폭 28 -> 42로 확장)
  int icon_x = 340; // 400 해상도 우측 끝에 바짝 붙도록 조정
  int icon_y = 12;  // 상단 마진
  int icon_w = 42;  // 와이드 가로형 배터리 폭
  int icon_h = 16;  // 배터리 높이

  // 2. 가로형 외곽선 껍데기 그리기 (흰색 빈 사각형)
  lcdDrawRect(icon_x, icon_y, icon_w, icon_h, white);

  // 3. 배터리 우측 단자 코 그리기 (가로형 돌출부 정중앙 정렬)
  lcdDrawFillRect(icon_x + icon_w, icon_y + 5, 3, 6, white);

  // 4. 와이드 배터리 내부 잔량 게이지 채우기 (2픽셀 안쪽 인셋 마진)
  int gauge_max_w = icon_w - 4; // 최대 충전 폭 (38 픽셀로 대폭 정밀화)
  int gauge_h     = icon_h - 4; // 게이지 높이 (12 픽셀)
  int gauge_w     = (gauge_max_w * bat_soc) / 100; // 가로 비율 연산

  if (gauge_w > 0)
  {
    // 20% 이하 경고 적색, 평소에는 녹색 메인 컬러 가동
    uint16_t gauge_color = (bat_soc <= 20) ? red : green;
    lcdDrawFillRect(icon_x + 2, icon_y + 2, gauge_w, gauge_h, gauge_color);
  }

  // 5. 와이드 배터리 아이콘 좌측에 수치 정보 일렬 정렬 (아이콘 크기가 커져 좌측으로 더 전진)
  // 출력 결과 형태: "85%  3.92V  [ 🔋▮▮▮▮    +]"
  lcdPrintf(icon_x - 115, icon_y + 2, white, "%d%% %.2fV", bat_soc, (double)bat_volts);


  // 1. [날짜 + 요일] 400x300 가로/세로 정중앙 튜닝 (크기: 28.0f)
  // 포맷: "2026-06-07(일)" (총 14글자)
  // 28px 폰트의 가로 점유 폭을 계산하여 400 해상도 중심에 오도록 X를 60으로 이동
  int date_y = 100 - 20;
  lcdPrintfResize(60 + 20, date_y, white, 32.0f, "%04d-%02d-%02d (%s)",
                  tm_ref.tm_year + 1900,
                  tm_ref.tm_mon + 1,
                  tm_ref.tm_mday,
                  wday_str[tm_ref.tm_wday]);

  // 2. [시간:분:초] 400x300 가로/세로 정중앙 튜닝 (크기: 64.0f)
  // 포맷: "HH:MM:SS" (총 8글자)
  // 64px 대형 폰트 8글자가 400 해상도 한가운데 정확히 대칭 배정되도록 X를 48로 정밀 튜닝
  int time_y = 155 - 20; 
  lcdPrintfResize(48 + 20, time_y, white, 64.0f, "%02d:%02d:%02d",
                  tm_ref.tm_hour,
                  tm_ref.tm_min,
                  tm_ref.tm_sec);
}

/**
 * @brief 공통 메뉴판 베이스 스크린 드로잉
 */
static void drawMainMenuPlatform(void)
{
  drawStatusBar("MAIN MENU");
  int start_y     = 34;
  int menu_height = 20;

  for (int i = 0; i < MENU_COUNT; i++)
  {
    int item_y = start_y + (i * menu_height);
    if (i == current_selection)
    {
      lcdDrawFillRect(5, item_y, LCD_WIDTH - 10, menu_height - 2, white);
      lcdPrintf(15, item_y + 2, black, menu_table[i].p_name);
    }
    else
    {
      lcdDrawRect(5, item_y, LCD_WIDTH - 10, menu_height - 2, white);
      lcdPrintf(15, item_y + 2, white, menu_table[i].p_name);
    }
  }
  lcdDrawHLine(0, 136, LCD_WIDTH, white);
}

// 개별 메뉴 서브 렌더링 함수 콜백 그룹
static void drawMenuSysInfo(void)
{
  drawMainMenuPlatform();
  lcdPrintf(10, 144, white, "[SYS] FPS: %d", lcdGetFps());
  lcdPrintf(10, 158, white, "[SYS] Free: %d ms", lcdGetFpsTime() - lcdGetDrawTime());
}

static void drawMenuLedCtrl(void)
{
  drawMainMenuPlatform();
  lcdPrintf(10, 144, white, "LED Test Active...");
  lcdDrawFillRect(150, 144, 16, 16, white);
}

static void drawMenuSensorStatus(void)
{
  drawMainMenuPlatform();
  lcdPrintf(10, 144, white, "Sensor Data: OK");
  lcdPrintf(10, 158, white, "ADC Value: 1024");
}

static void drawMenuLcdSettings(void)
{
  drawMainMenuPlatform();
  lcdPrintf(10, 144, white, "Brightness: %d %%", lcdGetBackLight());
}

static void drawMenuClockMode(void)
{
  drawFullClockScreen();
}

/**
 * @brief UI 메인 스레드 루프 함수
 */
static void uiThread(void const *arg)
{
  bool init_ret = true;

  moduleIsReady();
  logPrintf("[%s] Thread Started : UI\n", init_ret ? "OK" : "E_");

  delay(2000);

  while (1)
  {
    // [0번 버튼]: 커서 다운 이동 또는 시계 화면 탈출
    bool curr_btn_down = buttonGetPressed(0);
    if (curr_btn_down == true && prev_btn_down == false)
    {
      if (selected_menu == MENU_CLOCK_INDEX)
      {
        selected_menu = MENU_NONE;
      }
      else
      {
        current_selection = (current_selection + 1) % MENU_COUNT;
        selected_menu     = MENU_NONE;
      }
    }
    prev_btn_down = curr_btn_down;

    // [1번 버튼]: 메뉴 확정 선택
    bool curr_btn_select = buttonGetPressed(1);
    if (curr_btn_select == true && prev_btn_select == false)
    {
      selected_menu = current_selection;
    }
    prev_btn_select = curr_btn_select;

    if (lcdDrawAvailable() == true)
    {
      lcdClearBuffer(black);

      if (selected_menu == MENU_NONE)
      {
        drawMainMenuPlatform();
      }
      else
      {
        if (menu_table[selected_menu].draw_func != NULL)
        {
          menu_table[selected_menu].draw_func();
        }
      }

      lcdRequestDraw();
    }
    delay(UI_DELAY_MS);
  }
}