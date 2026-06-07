#include "ap.h"

LOG_MODULE_REGISTER(ap, LOG_LEVEL_DBG);

void lcdMain(void);


void apInit(void)
{  
  moduleInit();
}

void apMain(void)
{
  // lcdMain();

  while(1)
  {
    delay(500);
  }
}

#if 0
#include <zephyr/logging/log.h>
#include <zephyr/drivers/display.h>

#define DISPLAY_NODE DT_NODELABEL(st7306)

#define DISPLAY_WIDTH  312
#define DISPLAY_HEIGHT 400
#define BUFFER_SIZE    ((DISPLAY_WIDTH * DISPLAY_HEIGHT) / 8)

static uint8_t video_buffer[BUFFER_SIZE];


void lcdMain(void)
{
    const struct device *display_dev = DEVICE_DT_GET(DISPLAY_NODE);
    struct display_buffer_descriptor desc;


    if (!device_is_ready(display_dev)) {
        logPrintf("[E_] Display device 'ST7306' is not ready!\n");
        return;
    }

    logPrintf("ST7306 디스플레이 초기화 성공.\n");

    /* 디스플레이 화면을 켜는 명령 */
    display_blanking_off(display_dev);

    /* 버퍼에 그리기 예시 (단색 채우기 등) */
    struct display_capabilities caps;
    display_get_capabilities(display_dev, &caps);
    
    logPrintf("해상도: %d x %d\n", caps.x_resolution, caps.y_resolution);

    desc.buf_size = sizeof(video_buffer);
    desc.width = DISPLAY_WIDTH;
    desc.height = DISPLAY_HEIGHT;
    desc.pitch = DISPLAY_WIDTH;

    logPrintf("[INFO] Starting Black & White toggle loop...\n");

    while (1)
    {
      /* 1. Fill screen with BLACK */
      logPrintf("[INFO] Toggling screen to BLACK...\n");
      memset(video_buffer, 0x00, sizeof(video_buffer));

      if (display_write(display_dev, 0, 0, &desc, video_buffer) != 0)
      {
        logPrintf("[ERROR] Failed to write BLACK frame to display.\n");
      }

      k_sleep(K_MSEC(2000));

      /* 2. Fill screen with WHITE */
      logPrintf("[INFO] Toggling screen to WHITE...\n");
      memset(video_buffer, 0xFF, sizeof(video_buffer));

      if (display_write(display_dev, 0, 0, &desc, video_buffer) != 0)
      {
        logPrintf("[ERROR] Failed to write WHITE frame to display.\n");
      }

      k_sleep(K_MSEC(2000));
    }
}
#endif