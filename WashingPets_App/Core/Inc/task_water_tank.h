#ifndef TASK_WATER_TANK_H
#define TASK_WATER_TANK_H

#include <stdint.h>

void TaskWaterTank_Create(void);
void TaskWaterTank_RequestSelfCleanPrepare(void);
void TaskWaterTank_ClearSelfCleanPrepare(void);
uint8_t TaskWaterTank_GetHotFillValveAngle(void);

#endif
