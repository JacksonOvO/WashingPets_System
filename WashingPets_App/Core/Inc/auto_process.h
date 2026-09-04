#ifndef AUTO_PROCESS_H
#define AUTO_PROCESS_H

#include "stm32f4xx_hal.h"

/*
 * 自动流程的小流程编号。
 *
 * 手动调试时，可以用这些枚举值单独触发某一段流程。
 */
typedef enum
{
  AUTO_PROCESS_STAGE_ENTRY_ENV = 0,     /* 入舱准备 */
  AUTO_PROCESS_STAGE_ENTRY,             /* 入舱流程 */
  AUTO_PROCESS_STAGE_WASH_READY,        /* 洗前准备 */
  AUTO_PROCESS_STAGE_SPRAY,             /* 冲洗 */
  AUTO_PROCESS_STAGE_BATH,              /* 沐浴 */
  AUTO_PROCESS_STAGE_CLEAN,             /* 清洗 */
  AUTO_PROCESS_STAGE_DRY,               /* 吹干 */
  AUTO_PROCESS_STAGE_EXIT_PREPARE,      /* 出舱准备 */
  AUTO_PROCESS_STAGE_EXIT,              /* 出舱流程 */
  AUTO_PROCESS_STAGE_COUNT              /* 小流程数量，必须放在最后 */
} AutoProcessStage;

typedef struct
{
  uint8_t running;
  uint8_t finished;
  uint8_t failed;
  uint8_t stage_test_mode;
  uint16_t stage;
  uint16_t step;
} AutoProcessStatus;

void AutoProcess_Init(void);
void AutoProcess_Start(void);
void AutoProcess_Process(void);
uint8_t AutoProcess_IsFinished(void);
uint8_t AutoProcess_IsFailed(void);
void AutoProcess_Reset(void);
void AutoProcess_Abort(void);
void AutoProcess_TerminateServiceToExit(void);
void AutoProcess_RequestRestartCurrentStage(void);
void AutoProcess_RequestReset(void);
void AutoProcess_RequestAbort(void);
void AutoProcess_RequestTerminateServiceToExit(void);
void AutoProcess_RequestResume(void);

void AutoProcess_RequestFull(void);
void AutoProcess_RequestStageTest(AutoProcessStage stage);
void AutoProcess_ClearStageTest(AutoProcessStage stage);
uint8_t AutoProcess_GetStageTestFlag(AutoProcessStage stage);
uint8_t AutoProcess_IsRunning(void);
uint8_t AutoProcess_IsStageTest(void);
/* 查询是否处于单阶段测试（含已提交但尚未启动的测试请求）。 */
uint8_t AutoProcess_IsStageTestActive(void);
uint16_t AutoProcess_GetCurrentStage(void);
uint16_t AutoProcess_GetCurrentStepId(void);
void AutoProcess_GetStatus(AutoProcessStatus *status);

/*
 * 冲洗+沐浴+清洗+烘干四个阶段的总时间，单位毫秒。
 * 配方时间运行时从当前激活配方读取，固定延时来自 auto_stages.h 的
 * XXX_STAGE_FIXED_MS 宏，换配方或改宏后返回值自动跟着变。
 */
uint32_t AutoProcess_GetTotalTimeMs(void);

/*
 * 距离烘干结束的剩余时间，单位毫秒。
 * 未运行/失败/已完成返回0；冲洗之前的阶段返回总时间；
 * 冲洗~烘干期间返回 当前阶段剩余 + 后续到烘干各阶段时长；
 * 单段测试模式只返回当前测试阶段的剩余时间。
 */
uint32_t AutoProcess_GetRemainingTimeMs(void);
uint32_t AutoProcess_GetCurrentStageRemainingTimeMs(void);

#endif
