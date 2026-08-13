#ifndef CLEAN_PROCESS_H
#define CLEAN_PROCESS_H

#include "stm32f4xx_hal.h"

void CleanProcess_Init(void);
void CleanProcess_Reset(void);
void CleanProcess_Start(void);
HAL_StatusTypeDef CleanProcess_Resume(void);
void CleanProcess_Process(void);
uint8_t CleanProcess_IsFinished(void);
uint8_t CleanProcess_IsFailed(void);
uint16_t CleanProcess_GetCurrentStepId(void);

#endif
