#ifndef AUTO_STAGES_H
#define AUTO_STAGES_H

#include "app_flow.h"

typedef struct
{
  const char *name;
  const AppFlowStep *steps;
  uint16_t count;
  void (*init)(void);
  void (*reset)(void);
  HAL_StatusTypeDef (*resume)(uint16_t step_id);
} AutoStageDefinition;

#define AUTO_STEP(id, name, before, after, action_func, desc) \
  { (id), (name), (desc), (before), (after), (action_func) }

/*
 * 冲洗/沐浴/清洗/烘干四个阶段的固定延时，全部集中在这里定义。
 * 步骤表和阶段固定开销宏都引用这些宏：
 * 改这里任意一个数值，实际运行延时和上报的总时间/剩余时间会一起变化，
 * 不需要再改其他代码。
 */

/* 冲洗阶段固定延时 */
#define SPRAY_VALVES_SETTLE_MS     500U   /* 开淋浴阀和增压泵后的稳定延时 */
#define SPRAY_REPORT_DELAY_MS     2000U   /* 上报冲洗完成后的延时 */

/* 沐浴阶段固定延时 */
#define BATH_WATER_PATH_DELAY_MS   500U   /* 开水路后的稳定延时 */
#define BATH_PIPE_FLUSH_TIME_MS   5000U   /* 冲净药液管路时间 */
#define BATH_REPORT_DELAY_MS      2000U   /* 上报沐浴完成后的延时 */

/* 清洗阶段固定延时 */
#define CLEAN_VALVES_SETTLE_MS     500U   /* 开淋浴阀和增压泵后的稳定延时 */
#define CLEAN_REPORT_DELAY_MS     5000U   /* 上报清洗完成后的延时 */

/* 烘干阶段固定延时 */
#define DRY_FAN_RAMP_TIME_MS      10000U  /* 风机从启动功率升到100%的时间 */
#define DRY_FAN_FULL_HOLD_TIME_MS 120000U /* 风机100%保持时间 */
#define DRY_HEATER_COOLDOWN_TIME_MS 60000U /* 关闭加热棒后的风冷时间 */

/*
 * 各阶段固定开销 = 该阶段全部固定延时之和。
 * 阶段总时长 = 配方时间(运行时从当前配方读取) + 对应固定开销宏。
 */
#define SPRAY_STAGE_FIXED_MS \
  (SPRAY_VALVES_SETTLE_MS + SPRAY_REPORT_DELAY_MS)
#define BATH_STAGE_FIXED_MS \
  (BATH_WATER_PATH_DELAY_MS + BATH_PIPE_FLUSH_TIME_MS + BATH_REPORT_DELAY_MS)
#define CLEAN_STAGE_FIXED_MS \
  (CLEAN_VALVES_SETTLE_MS + CLEAN_REPORT_DELAY_MS)
#define DRY_STAGE_FIXED_MS \
  (DRY_FAN_RAMP_TIME_MS + DRY_FAN_FULL_HOLD_TIME_MS + DRY_HEATER_COOLDOWN_TIME_MS)

const AutoStageDefinition *AutoStage_EntryEnv_Get(void);
const AutoStageDefinition *AutoStage_Entry_Get(void);
const AutoStageDefinition *AutoStage_WashReady_Get(void);
const AutoStageDefinition *AutoStage_Spray_Get(void);
const AutoStageDefinition *AutoStage_Bath_Get(void);
const AutoStageDefinition *AutoStage_Clean_Get(void);
const AutoStageDefinition *AutoStage_Dry_Get(void);
const AutoStageDefinition *AutoStage_ExitPrepare_Get(void);
const AutoStageDefinition *AutoStage_Exit_Get(void);

#endif
