#ifndef TASK_SELF_TEST_H
#define TASK_SELF_TEST_H

#include <stdint.h>

typedef enum { SELF_TEST_MODE_QUICK = 0, SELF_TEST_MODE_DEEP = 1 } SelfTestMode;
typedef enum { SELF_TEST_RESULT_COMPLETE = 0, SELF_TEST_RESULT_ABORTED = 1 } SelfTestResultState;

typedef struct
{
  uint32_t result_id;
  uint8_t mode;
  uint8_t state;
  uint32_t tested_bitmap;
  uint32_t passed_bitmap;
} SelfTestResult;

void TaskSelfTest_Create(void);
uint8_t TaskSelfTest_CanStart(SelfTestMode mode);
void TaskSelfTest_Request(SelfTestMode mode);
void TaskSelfTest_Pause(void);
void TaskSelfTest_Resume(void);
void TaskSelfTest_Stop(void);
uint8_t TaskSelfTest_IsRunning(void);
uint8_t TaskSelfTest_IsPaused(void);
uint16_t TaskSelfTest_GetCurrentStepId(void);
uint16_t TaskSelfTest_GetProgressX10(void);
uint8_t TaskSelfTest_GetPendingResult(SelfTestResult *result);
void TaskSelfTest_AcknowledgeResult(uint32_t result_id);

#endif
