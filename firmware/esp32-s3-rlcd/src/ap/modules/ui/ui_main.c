#include "ap_def.h"

#include <zephyr/posix/sys/time.h> 
#include <time.h>


static bool uiInit(void);
static void uiThread(void const *arg);

MODULE_DEF(ui) 
{
  .name = "ui",
  .priority = MODULE_PRI_LOW,
  .init = uiInit
};

static K_THREAD_STACK_DEFINE(thread_stack, _HW_DEF_RTOS_THREAD_MEM_UI);
static struct k_thread thread_data;


// --- 메뉴 구성 정의 ---
const char *menu_items[] = {
  "1. SYSTEM INFO",
  "2. LED CONTROLS",
  "3. SENSOR STATUS",
  "4. LCD SETTINGS"};
const int MENU_COUNT = sizeof(menu_items) / sizeof(menu_items[0]);

// --- 상태 관리 변수 ---
int current_selection = 0;  // 현재 하이라이트된 메뉴 인덱스 (0 ~ 3)
int selected_menu     = -1; // 선택(확정) 완료된 메뉴 번호 (-1은 선택 대기 상태)

// --- 버튼 엣지 디텍션을 위한 이전 상태 저장 변수 ---
bool prev_btn_down   = false;
bool prev_btn_select = false;


bool uiInit(void)
{
  bool ret;


  k_tid_t tid = k_thread_create(&thread_data, thread_stack,
                                  K_THREAD_STACK_SIZEOF(thread_stack),
                                  (k_thread_entry_t)uiThread,
                                  NULL, NULL, NULL,
                                  _HW_DEF_RTOS_THREAD_PRI_UI, 0, K_NO_WAIT);

  ret = tid != NULL ? true:false;                                  
  logPrintf("[%s] uiInit()\n", ret ? "OK":"E_");

  return ret;
}

void drawStatusBar(const char *p_title)
{
  // 1. 상단바 배경 그리기 (white = 흑백 LCD에서 블랙 바 영역 생성)
  int bar_height = 22;
  lcdDrawFillRect(0, 0, LCD_WIDTH, bar_height, white);

  // 2. 왼쪽 타이틀 텍스트 출력 (black = 흑백 LCD에서 흰색 글씨로 표현됨)
  if (p_title != NULL)
  {
    lcdPrintf(8, 3, black, p_title);
  }

  // 3. [시간 연동] POSIX 시스템 클럭에서 실시간 Epoch Time 가져오기
  struct timespec ts;
  struct tm       tm_ref;

  if (clock_gettime(CLOCK_REALTIME, &ts) == 0)
  {
    // 캘린더 시간 구조체(년, 월, 일, 시, 분)로 변환 (타임존 세팅 자동 반영)
    localtime_r(&ts.tv_sec, &tm_ref);

    // 오른쪽 끝 정렬을 위한 X 좌표 계산 (약 11글자 분량 여백 확보)
    int time_x_pos = LCD_WIDTH - 95;
    if (time_x_pos < 60) time_x_pos = 140;

    // wifi.c의 포맷 규격을 활용하여 "MM-DD HH:MM" 형태로 출력
    lcdPrintf(time_x_pos, 3, black, "%02d-%02d %02d:%02d",
              tm_ref.tm_mon + 1,
              tm_ref.tm_mday,
              tm_ref.tm_hour,
              tm_ref.tm_min);
  }
  else
  {
    // 시간 획득 실패 시 방어용 텍스트
    int time_x_pos = LCD_WIDTH - 95;
    lcdPrintf(time_x_pos, 3, black, "00-00 00:00");
  }
}

void uiThread(void const *arg)
{
  bool init_ret = true;


  moduleIsReady();

  logPrintf("[%s] Thread Started : UI\n", init_ret ? "OK":"E_" );

  delay(2000);
  
  while(1)
  {
    // [0번 버튼]: 메뉴 이동 (DOWN)
    bool curr_btn_down = buttonGetPressed(0);
    if (curr_btn_down == true && prev_btn_down == false)
    {
      // 버튼을 누르는 순간 진입
      current_selection = (current_selection + 1) % MENU_COUNT;

      // 만약 메뉴를 고른 상태(`selected_menu` 상태)에서 다시 다운 버튼을 누르면 메뉴판으로 복귀
      selected_menu = -1;
    }
    prev_btn_down = curr_btn_down; // 상태 업데이트

    // [1번 버튼]: 메뉴 선택 (SELECT)
    bool curr_btn_select = buttonGetPressed(1);
    if (curr_btn_select == true && prev_btn_select == false)
    {
      // 버튼을 누르는 순간 진입 (현재 하이라이트된 메뉴를 확정)
      selected_menu = current_selection;
    }
    prev_btn_select = curr_btn_select; // 상태 업데이트


    if (lcdDrawAvailable() == true)
    {
      // 화면 전체를 검은색으로 초기화
      lcdClearBuffer(black);

      drawStatusBar("MAIN MENU");

      // // 상단 타이틀 바
      // lcdDrawFillRect(0, 0, LCD_WIDTH, 22, darkblue);
      // lcdPrintf(10, 3, white, "DEVICE MAIN MENU");

      // 메뉴판 그리기 영역 (Y축 기준 35픽셀 아래부터 시작)
      int start_y     = 38;
      int menu_height = 24;

      for (int i = 0; i < MENU_COUNT; i++)
      {
        int item_y = start_y + (i * menu_height);

        if (i == current_selection)
        {
          // 현재 커서가 위치한 메뉴: 초록색 배경에 검은색 글씨
          lcdDrawFillRect(5, item_y, LCD_WIDTH - 10, menu_height - 2, white);
          lcdPrintf(15, item_y + 4, black, menu_items[i]);
        }
        else
        {
          // 커서가 없는 일반 메뉴: 회색 테두리에 흰색 글씨
          lcdDrawRect(5, item_y, LCD_WIDTH - 10, menu_height - 2, gray);
          lcdPrintf(15, item_y + 4, white, menu_items[i]);
        }
      }

      lcdDrawHLine(0, 140, LCD_WIDTH, gray); // 구분선

      if (selected_menu == -1)
      {
        // 아직 아무것도 선택하지 않았을 때 안내 메시지
        lcdPrintf(10, 150, yellow, "Press BTN1 to Select.");
      }
      else
      {
        // 1번 버튼(SELECT)을 눌러 메뉴가 확정되었을 때의 개별 화면 처리
        switch (selected_menu)
        {
          case 0:                                   // SYSTEM INFO 선택됨
            lcdPrintf(10, 150, lightblue, "[SYS] FPS: %d", lcdGetFps());
            lcdPrintf(10, 166, lightblue, "[SYS] Free: %d ms", lcdGetFpsTime() - lcdGetDrawTime());
            break;

          case 1:                                   // LED CONTROLS 선택됨
            lcdPrintf(10, 150, pink, "LED Test Active...");
            lcdDrawFillRect(150, 150, 20, 20, red); // LCD 상에 가상 LED 표시
            break;

          case 2:                                   // SENSOR STATUS 선택됨
            lcdPrintf(10, 150, orange, "Sensor Data: OK");
            lcdPrintf(10, 166, white, "ADC Value: 1024");
            break;

          case 3:                                   // LCD SETTINGS 선택됨
            lcdPrintf(10, 150, beige, "Brightness: %d %%", lcdGetBackLight());
            break;
        }
      }

      // 하단 최하단 상태바 (현재 위치 디버깅용)
      lcdPrintf(5, LCD_HEIGHT - 16, gray, "Cur: %d | Sel: %d", current_selection, selected_menu);

      // 렌더링 요청
      lcdRequestDraw();
    }
  delay(5);
  }
}

