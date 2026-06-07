#include "lcd/st7306.h"


#ifdef _USE_HW_ST7306

#include <zephyr/drivers/display.h>


#define ST7306_WIDTH       312
#define ST7306_HEIGHT      400

/* 1비트 흑백 변환 버퍼 크기 계산: (312 * 400) / 8 = 15,600 바이트 */
#define ST7306_BYTES_PER_LINE ((ST7306_WIDTH + 7) / 8)                
#define ST7306_1BIT_BUF_SIZE  (ST7306_BYTES_PER_LINE * ST7306_HEIGHT) 

#define DISPLAY_NODE DT_NODELABEL(st7306)


static void (*frameCallBack)(void) = NULL;


static bool     st7306Reset(void);
static void     st7306SetWindow(int32_t x0, int32_t y0, int32_t x1, int32_t y1);
static uint16_t st7306GetWidth(void);
static uint16_t st7306GetHeight(void);
static bool     st7306SendBuffer(uint8_t *p_data, uint32_t length, uint32_t timeout_ms);
static bool     st7306SetCallBack(void (*p_func)(void));
static void     st7306Fill(uint16_t color);

static uint8_t __attribute__((aligned(64))) mono_transfer_buffer[ST7306_1BIT_BUF_SIZE];
const struct device                        *display_dev = DEVICE_DT_GET(DISPLAY_NODE);
struct display_buffer_descriptor            desc;



bool st7306Init(void)
{
  bool ret;

  ret = st7306Reset();

  return ret;
}

bool st7306InitDriver(lcd_driver_t *p_driver)
{
  p_driver->init        = st7306Init;
  p_driver->reset       = st7306Reset;
  p_driver->setWindow   = st7306SetWindow;
  p_driver->getWidth    = st7306GetWidth;
  p_driver->getHeight   = st7306GetHeight;
  p_driver->setCallBack = st7306SetCallBack;
  p_driver->sendBuffer  = st7306SendBuffer;
  return true;
}

bool st7306WriteCmd(uint8_t cmd_data)
{
  return true;
}

bool st7306Reset(void)
{
  if (!device_is_ready(display_dev))
  {
    return false;
  }

  desc.buf_size = sizeof(mono_transfer_buffer);
  desc.width    = ST7306_WIDTH;
  desc.height   = ST7306_HEIGHT;
  desc.pitch    = ST7306_WIDTH;

  
  st7306Fill(black);

  display_blanking_off(display_dev);

  return true;
}

void st7306SetWindow(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
}

uint16_t st7306GetWidth(void)
{
  return LCD_WIDTH;
}

uint16_t st7306GetHeight(void)
{
  return LCD_HEIGHT;
}

#if 1
bool st7306SendBuffer(uint8_t *p_data, uint32_t length, uint32_t timeout_ms)
{
  if (p_data == NULL)
  {
    return false;
  }

  uint16_t *p_rgb_buf = (uint16_t *)p_data;

  for (uint32_t y = 0; y < ST7306_HEIGHT; y++)
  {
    for (uint32_t x = 0; x < ST7306_WIDTH - 12; x++)
    {
      // 1. 원본 RGB565 인덱스 매핑 및 Green 패스
      uint32_t src_idx = (y * LCD_WIDTH) + x;
      uint16_t rgb = p_rgb_buf[src_idx];


      // 2. 이진화 및 색상 반전 (0일 때 화이트(1))
      uint8_t bit_val = (rgb == 0) ? 0 : 1;

      /* * 3. [핵심 교정] 300픽셀 정렬 바이트 인덱스 계산
       * 한 줄당 38바이트씩 증가하도록 설계하여 하드웨어 스트림 버퍼와 동기화합니다.
       */
      uint32_t byte_idx = (y * ST7306_BYTES_PER_LINE) + (x / 8);      
      uint8_t  bit_idx  = (x % 8); 

      // 4. 비트 세팅
      if (bit_val) 
      {
        mono_transfer_buffer[byte_idx] |= (1 << bit_idx);
      }
      else 
      {
        mono_transfer_buffer[byte_idx] &= ~(1 << bit_idx);
      }
    }
  }

  int ret = display_write(display_dev, 0, 0, &desc, mono_transfer_buffer);
  if (ret != 0)
  {
    return false;
  }

  if (frameCallBack != NULL)
  {
    frameCallBack();
  }
  return true;
}
#else
bool st7306SendBuffer(uint8_t *p_data, uint32_t length, uint32_t timeout_ms)
{
  if (p_data == NULL)
  {
    return false;
  }

  uint16_t *p_rgb_buf = (uint16_t *)p_data;

  /* * [주의] Green 스킵(기존 픽셀 유지)을 구현하기 위해
   * 매번 버퍼 전체를 0으로 미는 memset(mono_transfer_buffer, 0, ...)은 제외합니다.
   * 만약 완전히 새로운 프레임을 그리는 구조라면 lcd.c에서 배경을 먼저 그려서 보내야 합니다.
   */

  for (uint32_t y = 0; y < ST7306_HEIGHT; y++)
  {
    for (uint32_t x = 0; x < ST7306_WIDTH; x++)
    {
      // 1. 원본 RGB565 버퍼의 1차원 인덱스 및 색상 추출
      uint32_t src_idx = (y * LCD_WIDTH) + x;
      uint16_t rgb     = p_rgb_buf[src_idx];

      /* * [조건 1] Green 색상(0x07E0)이면 해당 픽셀은 건너뜁니다.
       * (모노크롬 버퍼의 해당 위치 값을 업데이트하지 않고 유지)
       */
      if (rgb == green)
      {
        continue;
      }

      /* * [조건 3] 0과 0이 아닌 것으로만 흑백 처리
       * 기존의 색상 반전 규칙을 유지합니다:
       * 0(Black)이 들어오면 -> 화이트(1)로 팩킹
       * 0이 아닌 값(기타 컬러) -> 블랙(0)으로 팩킹
       */
      uint8_t bit_val = (rgb == 0) ? 1 : 0;

      /* * [조건 2] 90도 시계 방향 회전 좌표 계산
       * New_X = Height - 1 - Old_Y
       * New_Y = Old_X
       */
      uint32_t rot_x = ST7306_HEIGHT - 1 - y;
      uint32_t rot_y = x;

      // 회전된 화면 기준 가로폭(ST7306_HEIGHT)으로 1차원 비트 인덱스 계산
      uint32_t dest_pixel_idx = (rot_y * ST7306_HEIGHT) + rot_x;

      uint32_t byte_idx = dest_pixel_idx / 8;
      uint8_t  bit_idx  = 7 - (dest_pixel_idx % 8);

      // 비트 업데이트 (0 혹은 1로 확실하게 덮어쓰기)
      if (bit_val)
      {
        mono_transfer_buffer[byte_idx] |= (1 << bit_idx);  // 화이트 세팅
      }
      else
      {
        mono_transfer_buffer[byte_idx] &= ~(1 << bit_idx); // 블랙 세팅
      }
    }
  }

  int ret = display_write(display_dev, 0, 0, &desc, mono_transfer_buffer);
  if (ret != 0)
  {
    return false;
  }

  if (frameCallBack != NULL)
  {
    frameCallBack();
  }
  return true;
}
#endif

bool st7306SetCallBack(void (*p_func)(void))
{
  frameCallBack = p_func;

  return true;
}

void st7306Fill(uint16_t color)
{
  /* * 어차피 흑백이므로 컬러 값이 0(Black)이면 0x00으로,
   * 0이 아닌 모든 값(White 또는 기타 컬러)은 0xFF로 버퍼를 채웁니다.
   */
  if (color != 0)
  {
    // 1비트 버퍼의 모든 비트를 1로 채움 (White)
    memset(mono_transfer_buffer, 0xFF, sizeof(mono_transfer_buffer));
  }
  else
  {
    // 1비트 버퍼의 모든 비트를 0으로 채움 (Black)
    memset(mono_transfer_buffer, 0x00, sizeof(mono_transfer_buffer));
  }

  for (int i=0; i<10; i++)
  {
    mono_transfer_buffer[i] = 0xFF;
  }

  for (int i=0; i<10; i++)
  {
    mono_transfer_buffer[ST7306_BYTES_PER_LINE * 2 + i] = 0xFF;
  }

  /* Zephyr 표준 API를 호출해 하드웨어로 즉시 전송 */
  int ret = display_write(display_dev, 0, 0, &desc, mono_transfer_buffer);
  if (ret != 0)
  {
    // 필요 시 에러 로깅 추가
  }

  /* 상위 파이프라인 콜백 처리 */
  if (frameCallBack != NULL)
  {
    frameCallBack();
  }
}


#endif