#ifndef PWM_FAN_H
#define PWM_FAN_H

#include "thyristor.h"

/* All functions run only in the existing comm485 task. Channel indices are 0..3. */
void PwmFan_Init(uint8_t (*allow_output)(void), void (*heartbeat)(void));
ThyristorStatus PwmFan_Service(void);
uint8_t PwmFan_IsReady(void);
ThyristorStatus PwmFan_SetPercent(uint8_t channel, uint8_t percent);
ThyristorStatus PwmFan_StopAll(void);
ThyristorStatus PwmFan_Probe(void);

#endif
