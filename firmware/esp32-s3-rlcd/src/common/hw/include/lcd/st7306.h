#ifndef ST7306_H_
#define ST7306_H_

#include "hw_def.h"


#ifdef _USE_HW_ST7306

#include "lcd.h"


bool st7306Init(void);
bool st7306InitDriver(lcd_driver_t *p_driver);

#endif


#endif 
