#ifndef HW_H_
#define HW_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"


#include "led.h"
#include "qbuffer.h"
#include "log.h"
#include "uart.h"
#include "cli.h"
#include "button.h"
#include "sd.h"
#include "fatfs.h"
#include "files.h"
#include "adc.h"
#include "battery.h"
#include "lcd.h"
#include "shtc3.h"
#include "rtc.h"

bool hwInit(void);


#ifdef __cplusplus
}
#endif

#endif