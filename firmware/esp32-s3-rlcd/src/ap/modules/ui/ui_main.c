#include "ap_def.h"
#include "network/wifi/wifi.h"

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

  // -------------------------------------------------------------------------
  // [추가] 좌측 상단 동적 WiFi 레이아웃 렌더링
  // -------------------------------------------------------------------------
  // 시스템의 WiFi 연결 상태 및 IP 주소를 가져오는 함수를 호출한다고 가정합니다.
  bool        is_wifi_connected = wifiIsConnected();
  const char *wifi_ip           = wifiGetIPAddress(); 
  int8_t      wifi_rssi         = wifiGetRssi();

  int wifi_x = 10;
  int wifi_y = 12;

  if (is_wifi_connected == false)
  {
    // 연결 안 됨: 경고용 적색(red) 또는 흰색으로 메시지 출력
    lcdPrintf(wifi_x, wifi_y + 2, red, "WiFi Not Connected");
  }
  else
  {
    // RSSI 수치에 따른 활성화 막대 개수 계산 (0 ~ 4개)
    int active_bars = 0;
    if (wifi_rssi >= -55)
      active_bars = 4; // 신호 매우 강함 (Full)
    else if (wifi_rssi >= -70)
      active_bars = 3; // 신호 강함
    else if (wifi_rssi >= -85)
      active_bars = 2; // 신호 약함
    else if (wifi_rssi > -95)
      active_bars = 1; // 신호 매우 약함
    else
      active_bars = 0; // 신호 없음

    int bar_w = 4;     // 막대 두께
    int bar_g = 2;     // 막대 간격

    // 1번 막대 (가장 낮음): active_bars가 1 이상이면 green, 아니면 gray(비활성 상태 슬롯 표시)
    uint16_t color_b1 = (active_bars >= 1) ? green : gray;
    lcdDrawFillRect(wifi_x, wifi_y + 11, bar_w, 5, color_b1);

    // 2번 막대
    uint16_t color_b2 = (active_bars >= 2) ? green : gray;
    lcdDrawFillRect(wifi_x + (bar_w + bar_g), wifi_y + 8, bar_w, 8, color_b2);

    // 3번 막대
    uint16_t color_b3 = (active_bars >= 3) ? green : gray;
    lcdDrawFillRect(wifi_x + (bar_w + bar_g) * 2, wifi_y + 4, bar_w, 12, color_b3);

    // 4번 막대 (가장 높음)
    uint16_t color_b4 = (active_bars >= 4) ? green : gray;
    lcdDrawFillRect(wifi_x + (bar_w + bar_g) * 3, wifi_y, bar_w, 16, color_b4);

    // 안테나 아이콘 우측에 IP 주소 연동 출력
    lcdPrintf(wifi_x + 28, wifi_y + 2, white, "%d %s", wifi_rssi, wifi_ip);
  }

  // -------------------------------------------------------------------------
  // 우측 상단 가로형 배터리 레이아웃 (기존 유지)
  // -------------------------------------------------------------------------
  int icon_x = 340; 
  int icon_y = 12;  
  int icon_w = 42;  
  int icon_h = 16;  

  lcdDrawRect(icon_x, icon_y, icon_w, icon_h, white);
  lcdDrawFillRect(icon_x + icon_w, icon_y + 5, 3, 6, white);

  int gauge_max_w = icon_w - 4; 
  int gauge_h     = icon_h - 4; 
  int gauge_w     = (gauge_max_w * bat_soc) / 100; 

  if (gauge_w > 0)
  {
    uint16_t gauge_color = (bat_soc <= 20) ? red : green;
    lcdDrawFillRect(icon_x + 2, icon_y + 2, gauge_w, gauge_h, gauge_color);
  }

  lcdPrintf(icon_x - 100, icon_y + 2, white, "%d%% %.2fV", bat_soc, (double)bat_volts);

  // -------------------------------------------------------------------------
  // 중앙 메인 클락 표시 영역 (기존 유지)
  // -------------------------------------------------------------------------
  int date_y = 100 - 20;
  lcdPrintfResize(60 + 20, date_y, white, 32.0f, "%04d-%02d-%02d (%s)",
                  tm_ref.tm_year + 1900,
                  tm_ref.tm_mon + 1,
                  tm_ref.tm_mday,
                  wday_str[tm_ref.tm_wday]);

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