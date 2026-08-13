#ifndef TASK_CLEAN_H
#define TASK_CLEAN_H

#include <stdint.h>

void TaskClean_Create(void);
void TaskClean_Request(void);
void TaskClean_Stop(void);
void TaskClean_RequestRestart(void);
void TaskClean_RequestResume(void);
void TaskClean_ClearFault(void);
uint8_t TaskClean_IsRunning(void);
uint8_t TaskClean_IsFailed(void);
uint16_t TaskClean_GetCurrentStepId(void);

#endif
