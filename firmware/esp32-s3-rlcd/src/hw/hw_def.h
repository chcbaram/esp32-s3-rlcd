#ifndef HW_DEF_H_
#define HW_DEF_H_


#include "bsp.h"


#define _DEF_FIRMWATRE_VERSION      "V260525R1"
#define _DEF_BOARD_NAME             "ESP32-S3-RLCD"





#define _USE_HW_RTOS
#if defined(CONFIG_DISK_DRIVER_SDMMC)
#define _USE_HW_SD
#endif
#if defined(CONFIG_FAT_FILESYSTEM_ELM)
#define _USE_HW_FATFS
#define _USE_HW_FILES
#endif
#define _USE_HW_BATTERY

// #define _USE_HW_LED
// #define      HW_LED_MAX_CH          1

#define _USE_HW_UART
#define      HW_UART_MAX_CH         2
#define      HW_UART_CH_USB         _DEF_UART1
#define      HW_UART_CH_NET         _DEF_UART2
#define      HW_UART_CH_CLI         HW_UART_CH_USB

#define _USE_HW_LOG
#define      HW_LOG_CH              HW_UART_CH_USB
#define      HW_LOG_BOOT_BUF_MAX    4096
#define      HW_LOG_LIST_BUF_MAX    4096

#define _USE_HW_CLI
#define      HW_CLI_CMD_LIST_MAX    64
#define      HW_CLI_CMD_NAME_MAX    16
#define      HW_CLI_LINE_HIS_MAX    8
#define      HW_CLI_LINE_BUF_MAX    64

#define _USE_HW_CLI_GUI
#define      HW_CLI_GUI_WIDTH       80
#define      HW_CLI_GUI_HEIGHT      24

#define _USE_HW_BUTTON
#define      HW_BUTTON_MAX_CH       BUTTON_PIN_MAX

#define _USE_HW_ADC                 
#define      HW_ADC_MAX_CH          ADC_PIN_MAX

#define _USE_HW_LCD
#define      HW_LCD_LVGL            1
#define      HW_LCD_LOGO            0
#define _USE_HW_ST7306
#define      HW_LCD_WIDTH           400
#define      HW_LCD_HEIGHT          300

#define _USE_HW_SHTC3
#define      HW_SHTC3_MAX_CH        1

#define _USE_HW_RTC


//-- CLI
//
#define _USE_CLI_HW_UART            1
#define _USE_CLI_HW_BUTTON          1
#define _USE_CLI_HW_SD              1
#define _USE_CLI_HW_FATFS           1
#define _USE_CLI_HW_ADC             1
#define _USE_CLI_HW_LOG             1
#define _USE_CLI_HW_SHTC3           1
#define _USE_CLI_HW_RTC             1


#define _HW_DEF_RTOS_THREAD_PRI_CLI           5
#define _HW_DEF_RTOS_THREAD_PRI_UART          5
#define _HW_DEF_RTOS_THREAD_PRI_BATTERY       5
#define _HW_DEF_RTOS_THREAD_PRI_LCD           5
#define _HW_DEF_RTOS_THREAD_PRI_UI            5

#define _HW_DEF_RTOS_THREAD_MEM_CLI           (6*1024)
#define _HW_DEF_RTOS_THREAD_MEM_UART          (2*1024)
#define _HW_DEF_RTOS_THREAD_MEM_BATTERY       (2*1024)
#define _HW_DEF_RTOS_THREAD_MEM_LCD           (4*1024)
#define _HW_DEF_RTOS_THREAD_MEM_UI            (6*1024)

typedef enum
{
  BTN_L,
  BTN_R,
  BUTTON_PIN_MAX,  
} ButtonPinName_t;

typedef enum
{
  BAT_ADC = 0,
  ADC_PIN_MAX
} AdcPinName_t;

#endif
