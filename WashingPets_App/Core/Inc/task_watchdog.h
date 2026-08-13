#ifndef TASK_WATCHDOG_H
#define TASK_WATCHDOG_H

#include <stdint.h>

typedef enum
{
  TASK_WATCHDOG_OUTPUT = 0,
  TASK_WATCHDOG_HOST232,
  TASK_WATCHDOG_COMM485,
  TASK_WATCHDOG_AUTO,
  TASK_WATCHDOG_WATER_TANK,
  TASK_WATCHDOG_COUNT
} TaskWatchdogSource;

void TaskWatchdog_Create(void);
void TaskWatchdog_Heartbeat(TaskWatchdogSource source);
void TaskWatchdog_EmergencyOutputsOff(void);
void TaskWatchdog_EmergencyReset(void) __attribute__((noreturn));

#endif
