#ifndef TEST_CMSIS_OS_H
#define TEST_CMSIS_OS_H
#include <stdint.h>
typedef void *osThreadId_t;
typedef void *osMessageQueueId_t;
typedef struct { const char *name; uint32_t stack_size; int priority; } osThreadAttr_t;
enum { osPriorityBelowNormal, osPriorityNormal, osOK = 0, osFlagsWaitAny = 0 };
osThreadId_t osThreadNew(void (*)(void *), void *, const osThreadAttr_t *);
osThreadId_t osThreadGetId(void);
void osDelay(uint32_t);
uint32_t osThreadFlagsSet(osThreadId_t, uint32_t);
uint32_t osThreadFlagsWait(uint32_t, uint32_t, uint32_t);
uint32_t osThreadFlagsClear(uint32_t);
osMessageQueueId_t osMessageQueueNew(uint32_t, uint32_t, const void *);
int osMessageQueuePut(osMessageQueueId_t, const void *, uint8_t, uint32_t);
int osMessageQueueGet(osMessageQueueId_t, void *, uint8_t *, uint32_t);
#endif
