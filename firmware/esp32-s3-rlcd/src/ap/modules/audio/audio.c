#include "ap_def.h"

#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>


// 이 프로젝트는 오디오를 사용하지 않는다. 상시 3.3V 레일에 붙은 코덱들이
// idle 로 수 mA 를 소모하므로 부팅 시 파워다운 시켜 저전력 상태로 둔다.
#define ES7210_ADDR   0x40   // 4채널 오디오 ADC
#define ES8311_ADDR   0x18   // 오디오 코덱 (CE 하이 시 0x19 일 수 있음)
#define PA_CTRL_PIN   14      // NS4150B CTRL = GPIO46 = gpio1.14 (LOW = shutdown)


static const struct device *const i2c   = DEVICE_DT_GET(DT_NODELABEL(i2c0));
static const struct device *const gpio1 = DEVICE_DT_GET(DT_NODELABEL(gpio1));


static bool audioInit(void);
static void cliAudio(cli_args_t *args);


MODULE_DEF(audio)
{
  .name     = "audio",
  .priority = MODULE_PRI_LOW,
  .init     = audioInit,
};


static int audioReg(uint8_t addr, uint8_t reg, uint8_t val)
{
  uint8_t buf[2] = {reg, val};
  return i2c_write(i2c, buf, sizeof(buf), addr);
}

static int audioEs7210PowerDown(uint8_t addr)
{
  int err = 0;
  err |= audioReg(addr, 0x4B, 0xFF); // MIC1,2 power down
  err |= audioReg(addr, 0x4C, 0xFF); // MIC3,4 power down
  err |= audioReg(addr, 0x01, 0x7F); // clock off
  err |= audioReg(addr, 0x06, 0x07); // power down
  err |= audioReg(addr, 0x40, 0xC0); // analog / VMID down
  err |= audioReg(addr, 0x41, 0x70); // MIC1,2 bias off
  err |= audioReg(addr, 0x42, 0x70); // MIC3,4 bias off
  return err;
}

static int audioEs8311PowerDown(uint8_t addr)
{
  int err = 0;
  err |= audioReg(addr, 0x45, 0x00);
  err |= audioReg(addr, 0x0E, 0xFF); // analog power down
  err |= audioReg(addr, 0x12, 0x02);
  err |= audioReg(addr, 0x14, 0x00);
  err |= audioReg(addr, 0x0D, 0xFA); // power down
  err |= audioReg(addr, 0x15, 0x00);
  err |= audioReg(addr, 0x37, 0x08);
  err |= audioReg(addr, 0x00, 0x00); // reset / standby
  return err;
}

static void audioAmpShutdown(void)
{
  if (device_is_ready(gpio1))
    gpio_pin_configure(gpio1, PA_CTRL_PIN, GPIO_OUTPUT_INACTIVE);
}

static bool audioPowerDownAll(void)
{
  int e7 = audioEs7210PowerDown(ES7210_ADDR);
  int e8 = audioEs8311PowerDown(ES8311_ADDR);
  if (e8 != 0)
    e8 = audioEs8311PowerDown(0x19);

  audioAmpShutdown();

  return (e7 == 0);
}

static bool audioInit(void)
{
  bool ret = true;

  if (!device_is_ready(i2c))
  {
    logPrintf("[E_] audioInit() i2c not ready\n");
    ret = false;
  }
  else
  {
    audioPowerDownAll();
    logPrintf("[OK] audioInit()\n");
  }

  cliAdd("audio", cliAudio);
  return ret;
}

static void cliAudio(cli_args_t *args)
{
  bool ret = false;

  if (args->argc == 1 && args->isStr(0, "off"))
  {
    int e7 = audioEs7210PowerDown(ES7210_ADDR);
    int e8 = audioEs8311PowerDown(ES8311_ADDR);
    int e9 = audioEs8311PowerDown(0x19);
    audioAmpShutdown();
    cliPrintf("es7210(0x40):%d es8311(0x18):%d es8311(0x19):%d\n", e7, e8, e9);
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "scan"))
  {
    for (uint8_t addr = 0x08; addr < 0x78; addr++)
    {
      uint8_t dummy;
      if (i2c_read(i2c, &dummy, 1, addr) == 0)
        cliPrintf("  ack : 0x%02X\n", addr);
    }
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("audio off\n");
    cliPrintf("audio scan\n");
  }
}
