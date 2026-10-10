#include "auto_stages.h"

#include "app_param.h"
#include "auto_device.h"
#include "rs485_config.h"
#include "task_comm485.h"

#define CLEAN_DRY_PREHEAT_FAN_PERCENT       50U
#define CLEAN_DRY_PREHEAT_HEATER_PERCENT   100U
#define CLEAN_DRY_PREHEAT_LEAD_MS        60000U

static uint32_t clean_start_tick;
static uint8_t dry_preheat_started;
static void set_dry_preheat_outputs(uint8_t fan_percent, uint8_t heater_percent);

static void CleanReset(void)
{
  clean_start_tick = 0U;
  dry_preheat_started = 0U;
}

static void CleanInit(void)
{
  CleanReset();
}

static HAL_StatusTypeDef CleanResume(uint16_t step_id)
{
  const AppRecipe_t *recipe = AppParam_GetActiveRecipe();

  if ((step_id >= 30U) && (step_id < 110U))
  {
    AutoDevice_SetAllShowerValves(1U);
    if ((AutoDevice_SetBoosterPumpPercent(recipe->clean_pressure_level) != HAL_OK) ||
        (AutoDevice_SetBoosterPumpRunning(1U) != HAL_OK))
    {
      return HAL_ERROR;
    }
    if ((step_id >= 65U) && (dry_preheat_started != 0U))
    {
      set_dry_preheat_outputs(CLEAN_DRY_PREHEAT_FAN_PERCENT,
                              CLEAN_DRY_PREHEAT_HEATER_PERCENT);
    }
  }
  return HAL_OK;
}

static void clean_water_stop(void)
{
  (void)AutoDevice_SetBoosterPumpRunning(0U);
  AutoDevice_SetAllShowerValves(0U);
}

static void set_dry_preheat_outputs(uint8_t fan_percent, uint8_t heater_percent)
{
  TaskComm485_RequestSetPercent(RS485_STATION_BOTTOM_FAN, fan_percent);
  TaskComm485_RequestSetPercent(RS485_STATION_TOP_EXHAUST_FAN, fan_percent);
  TaskComm485_RequestSetPercent(RS485_STATION_BOTTOM_HEATER, heater_percent);
  (void)AutoDevice_SetBlower(heater_percent != 0U);
}

static void clean_stop(void)
{
  clean_water_stop();
  set_dry_preheat_outputs(0U, 0U);
}

static void start_dry_preheat(void)
{
  const AppRecipe_t *recipe = AppParam_GetActiveRecipe();
  uint32_t elapsed_ms = AutoDevice_GetProcessTick() - clean_start_tick;

  /* Use the pause-aware cleaning clock; never heat for a zero/finished cycle. */
  if ((dry_preheat_started != 0U) || (elapsed_ms >= recipe->clean_time_ms) ||
      ((recipe->clean_time_ms - elapsed_ms) > CLEAN_DRY_PREHEAT_LEAD_MS))
  {
    return;
  }

  dry_preheat_started = 1U;
  set_dry_preheat_outputs(CLEAN_DRY_PREHEAT_FAN_PERCENT,
                          CLEAN_DRY_PREHEAT_HEATER_PERCENT);
}

static AppFlowStepResult CheckAllow(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  if ((AutoDevice_ReadInput(AUTO_INPUT_UPPER_DOOR_CLOSED) == 0U) ||
      (AutoDevice_ReadInput(AUTO_INPUT_LOWER_DOOR_CLOSED) == 0U))
  {
    clean_stop();
    /* 门未关闭到位属于安全联锁失败，使用专用故障码，便于上位机定位。 */
    AutoDevice_SetFault(AUTO_FAULT_DOOR_NOT_CLOSED);
    return APP_FLOW_STEP_FAIL;
  }
  return APP_FLOW_STEP_DONE;
}

static AppFlowStepResult CheckTemp(AppFlowContext *ctx, const AppFlowStep *step)
{
  AutoSensorData data;
  const AppRecipe_t *recipe = AppParam_GetActiveRecipe();
  (void)ctx;
  (void)step;

  (void)AutoDevice_GetSensorData(&data);
  if ((data.pipe_temperature_valid == 0U) ||
      (data.pipe_temperature_x10 <
       (recipe->tank_target_temp_x10 - (int16_t)recipe->pipe_temp_tolerance_x10)) ||
      (data.pipe_temperature_x10 >
       (recipe->tank_target_temp_x10 + (int16_t)recipe->pipe_temp_tolerance_x10)))
  {
    clean_stop();
    AutoDevice_SetFault((data.pipe_temperature_valid == 0U) ?
                        AUTO_FAULT_PIPE_TEMP_SENSOR_INVALID :
                        AUTO_FAULT_PIPE_TEMP_NOT_READY);
    return APP_FLOW_STEP_FAIL;
  }
  return APP_FLOW_STEP_DONE;
}

static AppFlowStepResult ValvesPumpOn(AppFlowContext *ctx, const AppFlowStep *step)
{
  const AppRecipe_t *recipe = AppParam_GetActiveRecipe();
  (void)ctx;
  (void)step;
  AutoDevice_SetAllShowerValves(1U);
  (void)AutoDevice_SetBoosterPumpPercent(recipe->clean_pressure_level);
  (void)AutoDevice_SetBoosterPumpRunning(1U);
  return APP_FLOW_STEP_DONE;
}

static AppFlowStepResult TimerStart(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  clean_start_tick = AutoDevice_GetProcessTick();
  return APP_FLOW_STEP_DONE;
}

static AppFlowStepResult Monitor(AppFlowContext *ctx, const AppFlowStep *step)
{
  AutoSensorData data;
  const AppRecipe_t *recipe = AppParam_GetActiveRecipe();
  uint32_t elapsed_ms;
  (void)ctx;
  (void)step;

  (void)AutoDevice_GetSensorData(&data);
  if ((data.pipe_temperature_valid == 0U) ||
      (data.pipe_temperature_x10 <
       (recipe->tank_target_temp_x10 -
        (int16_t)recipe->pipe_temp_tolerance_x10)) ||
      (data.pipe_temperature_x10 >
       (recipe->tank_target_temp_x10 +
        (int16_t)recipe->pipe_temp_tolerance_x10)))
  {
    clean_stop();
    AutoDevice_SetFault((data.pipe_temperature_valid == 0U) ?
                        AUTO_FAULT_PIPE_TEMP_SENSOR_INVALID :
                        AUTO_FAULT_PIPE_TEMP_NOT_READY);
    return APP_FLOW_STEP_FAIL;
  }

  elapsed_ms = AutoDevice_GetProcessTick() - clean_start_tick;
  if (elapsed_ms >= recipe->clean_time_ms)
  {
    return APP_FLOW_STEP_DONE;
  }

  start_dry_preheat();
  return APP_FLOW_STEP_RUNNING;
}

static AppFlowStepResult DryPreheat(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  start_dry_preheat();
  return APP_FLOW_STEP_DONE;
}

static AppFlowStepResult ValvesPumpOff(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  clean_water_stop();
  return APP_FLOW_STEP_DONE;
}

static AppFlowStepResult ReportDone(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  AutoDevice_Report(AUTO_REPORT_CLEAN_DONE);
  return APP_FLOW_STEP_DONE;
}

static const AppFlowStep steps[] = {
  AUTO_STEP(10, "CLEAN_CHECK_ALLOW", 0U, 0U, CheckAllow, "检查上下门状态"),
  AUTO_STEP(20, "CLEAN_CHECK_TEMP", 0U, 0U, CheckTemp, "检查管路水温"),
  AUTO_STEP(30, "CLEAN_VALVES_PUMP_ON", 0U, CLEAN_VALVES_SETTLE_MS, ValvesPumpOn, "打开全部淋浴阀并同步启动增压泵"),
  AUTO_STEP(60, "CLEAN_TIMER_START", 0U, 0U, TimerStart, "开始清洗计时"),
  AUTO_STEP(65, "CLEAN_DRY_PREHEAT", 0U, 0U, DryPreheat, "检查是否进入清洗结束前60秒预热窗口"),
  AUTO_STEP(80, "CLEAN_MONITOR", 0U, 0U, Monitor, "监测水温和清洗时间，剩余60秒启动预热"),
  AUTO_STEP(110, "CLEAN_VALVES_PUMP_OFF", 0U, 0U, ValvesPumpOff, "关闭增压泵和全部淋浴阀"),
  AUTO_STEP(120, "REPORT_CLEAN_DONE", 0U, CLEAN_REPORT_DELAY_MS, ReportDone, "上报清洗完成"),
};

static const AutoStageDefinition stage = {
  "Cleaning", steps, (uint16_t)(sizeof(steps) / sizeof(steps[0])),
  CleanInit, CleanReset, CleanResume
};

const AutoStageDefinition *AutoStage_Clean_Get(void)
{
  return &stage;
}
