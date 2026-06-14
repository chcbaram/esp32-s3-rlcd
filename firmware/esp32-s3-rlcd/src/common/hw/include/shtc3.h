#ifndef SHTC3_H_
#define SHTC3_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"


#define SHTC3_MAX_CH    HW_SHTC3_MAX_CH


typedef struct
{
  float temp;
  float humidity;

  float temp_filtered;     
  float humidity_filtered; 
} shtc3_info_t;

bool shtc3Init(void);
bool shtc3IsInit(void);
bool shtc3GetInfo(uint8_t ch, shtc3_info_t *p_info);

#ifdef __cplusplus
}
#endif

#endif