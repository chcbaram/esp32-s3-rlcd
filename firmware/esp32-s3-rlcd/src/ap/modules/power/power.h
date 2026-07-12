#ifndef POWER_H_
#define POWER_H_


#include <stdint.h>
#include <stdbool.h>


bool powerIsColdBoot(void);            // 콜드부팅(전원인가) vs 딥슬립 웨이크 구분
void powerDeepSleep(uint32_t sec);     // sec 후 타이머 웨이크로 딥슬립 (복귀 안 함)
void powerSleepToNextMinute(void);     // 다음 분 경계까지 딥슬립 (USER 버튼으로도 웨이크)
void powerSetStayAwake(bool enable);   // 딥슬립 억제 (디버그 모드)
bool powerStayAwake(void);


#endif
