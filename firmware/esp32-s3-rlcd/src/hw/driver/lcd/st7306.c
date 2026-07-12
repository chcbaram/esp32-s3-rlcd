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

  // 초기화 시 검정으로 지우지 않는다. 첫 화면 전송이 곧바로 실제 컨텐츠(시계)가 되도록 해
  // 부팅 시 "검정 프레임 → 컨텐츠" 2단계 깜빡임을 1단계로 줄인다.
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

#if 0
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

  /* * 상위 레이어의 입력 원본 이미지 크기(LCD_WIDTH x LCD_HEIGHT)를 기준으로 루프를 돕니다.
   * 예: 원본이 가로 312, 세로 400이라면 y는 400까지, x는 312까지 돕니다.
   */
  for (uint32_t y = 0; y < LCD_HEIGHT; y++)
  {
    for (uint32_t x = 0; x < LCD_WIDTH; x++)
    {
      // 1. 원본 RGB565 인덱스 매핑 및 Green 패스
      uint32_t src_idx = (y * LCD_WIDTH) + x;
      uint16_t rgb = p_rgb_buf[src_idx];


      // 2. 이진화 및 색상 반전 논리 (기존 논리 반영: 0일 때 0(블랙), 0이 아니면 1(화이트))
      uint8_t bit_val = (rgb == 0) ? 0 : 1;

      /* * 3. [90도 시계 방향 회전 좌표 변환]
       * rot_x: 회전 후의 가로 좌표 (0 ~ LCD_HEIGHT - 1)
       * rot_y: 회전 후의 세로 좌표 (0 ~ LCD_WIDTH - 1)
       */
      uint32_t rot_x = LCD_HEIGHT - 1 - y;
      uint32_t rot_y = x;

      /* * 4. [회전 기준 인덱스 및 비트 계산]
       * 화면을 돌렸기 때문에, 새로운 가로 한 줄의 바이트 수 규격은 
       * 원본 가로폭이 아니라 "회전된 가로폭" 즉, 기존의 'ST7306_BYTES_PER_LINE' 또는 'LCD_HEIGHT 기준 바이트 수'가 됩니다.
       * * 여기서는 하드웨어의 가로폭 규격에 맞춰 정의된 ST7306_BYTES_PER_LINE을 축으로 계산합니다.
       */
      uint32_t byte_idx = (rot_y * ST7306_BYTES_PER_LINE) + (rot_x / 8);      
      
      /* 피드백해주신 LSB-First 구조 (가장 왼쪽 픽셀이 Bit 0) 반영 */
      uint8_t  bit_idx  = (rot_x % 8); 

      // 5. 비트 세팅 (Overwrite)
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

  /* 6. Zephyr 드라이버 전송 디스크립터
   * 이미 하드웨어 디바이스 트리와 st7306Init 등에서 회전된 해상도를 인지하도록 
   * 전역 desc가 가로 400, 세로 312(혹은 세로 300) 구조로 세팅되어 있다면 
   * 이 전역 desc를 그대로 던지면 됩니다.
   */
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
  if (color != 0)
  {
    memset(mono_transfer_buffer, 0xFF, sizeof(mono_transfer_buffer));
  }
  else
  {
    memset(mono_transfer_buffer, 0x00, sizeof(mono_transfer_buffer));
  }

  int ret = display_write(display_dev, 0, 0, &desc, mono_transfer_buffer);
  if (ret != 0)
  {
  }

  if (frameCallBack != NULL)
  {
    frameCallBack();
  }
}


#endif