#include "auto_stages.h"

#include "app_param.h"
#include "auto_device.h"
#include "task_output.h"

#define SPRAY_PIPE_TEMP_LOW_X10   280
#define SPRAY_PIPE_TEMP_HIGH_X10  390
#define SPRAY_TEMP_FAULT_CONFIRM_MS 3000U

static void SprayInit(void);
static void SprayReset(void);
static AppFlowStepResult CheckAllow(AppFlowContext *, const AppFlowStep *);
static AppFlowStepResult CheckTemp(AppFlowContext *, const AppFlowStep *);
static AppFlowStepResult ValvesPumpOn(AppFlowContext *, const AppFlowStep *);
static AppFlowStepResult TimerStart(AppFlowContext *, const AppFlowStep *);
static AppFlowStepResult Monitor(AppFlowContext *, const AppFlowStep *);
static AppFlowStepResult ValvesPumpOff(AppFlowContext *, const AppFlowStep *);
static AppFlowStepResult ReportDone(AppFlowContext *, const AppFlowStep *);

static HAL_StatusTypeDef SprayResume(uint16_t step_id)
{
  const AppRecipe_t *recipe = AppParam_GetActiveRecipe();

  if ((step_id < 30U) || (step_id >= 150U))
  {
    return HAL_OK;
  }
  AutoDevice_SetAllShowerValves(1U);
  if (AutoDevice_SetBoosterPumpPercent(recipe->spray_pressure_level) != HAL_OK)
  {
    return HAL_ERROR;
  }
  return AutoDevice_SetBoosterPumpRunning(1U);
}

static const AppFlowStep steps[] = {
  AUTO_STEP(10, "SPRAY_CHECK_ALLOW", 0U, 0U, CheckAllow, "检查上下门状态"),
  AUTO_STEP(20, "SPRAY_CHECK_TEMP", 0U, 0U, CheckTemp, "检查管路水温"),
  AUTO_STEP(30, "SPRAY_VALVES_PUMP_ON", 0U, SPRAY_VALVES_SETTLE_MS, ValvesPumpOn, "打开全部淋浴阀并同步启动增压泵"),
  AUTO_STEP(90, "SPRAY_TIMER_START", 0U, 0U, TimerStart, "开始冲洗计时"),
  AUTO_STEP(110, "SPRAY_MONITOR", 0U, 0U, Monitor, "持续监测管路水温并等待冲洗时间到达"),
  AUTO_STEP(150, "SPRAY_VALVES_PUMP_OFF", 0U, 0U, ValvesPumpOff, "关闭增压泵和全部淋浴阀"),
  AUTO_STEP(160, "REPORT_SPRAY_DONE", 0U, SPRAY_REPORT_DELAY_MS, ReportDone, "上报冲洗完成"),
};

static const AutoStageDefinition stage = {
  "Spraying", steps, (uint16_t)(sizeof(steps) / sizeof(steps[0])),
  SprayInit, SprayReset, SprayResume
};

static uint32_t run_start_tick;
static uint32_t temp_fault_start_tick;

static void SprayReset(void)
{
  run_start_tick = 0U;
  temp_fault_start_tick = 0U;
}

static void SprayInit(void)
{
  SprayReset();
}

static void safe_stop(void)
{
  AutoDevice_AllDangerousOutputsOff();
}

static AppFlowStepResult CheckAllow(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  if ((AutoDevice_ReadInput(AUTO_INPUT_UPPER_DOOR_CLOSED) == 0U) ||
      (AutoDevice_ReadInput(AUTO_INPUT_LOWER_DOOR_CLOSED) == 0U))
  {
    safe_stop();
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
    safe_stop();
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
  /* The water-tank task owns refill valves during washing. */
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_BYPASS_VALVE, 0U);
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_SELF_CLEAN_TOILET_VALVE, 0U);
  AutoDevice_SetAllShowerValves(1U);
  (void)AutoDevice_SetBoosterPumpPercent(recipe->spray_pressure_level);
  (void)AutoDevice_SetBoosterPumpRunning(1U);
  return APP_FLOW_STEP_DONE;
}

static AppFlowStepResult TimerStart(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  run_start_tick = AutoDevice_GetProcessTick();
  return APP_FLOW_STEP_DONE;
}

static AppFlowStepResult Monitor(AppFlowContext *ctx, const AppFlowStep *step)
{
  AutoSensorData data;
  uint8_t temperature_bad;
  const AppRecipe_t *recipe = AppParam_GetActiveRecipe();
  
  (void)ctx;
  (void)step;

  (void)AutoDevice_GetSensorData(&data);
  temperature_bad = ((data.pipe_temperature_valid == 0U) ||
                     (data.pipe_temperature_x10 < (recipe->tank_target_temp_x10 - (int16_t)recipe->pipe_temp_tolerance_x10)) ||
                     (data.pipe_temperature_x10 > (recipe->tank_target_temp_x10 + (int16_t)recipe->pipe_temp_tolerance_x10))) ? 1U : 0U;
  if (temperature_bad != 0U)
  {
    if (temp_fault_start_tick == 0U)
    {
      temp_fault_start_tick = AutoDevice_GetProcessTick();
      return APP_FLOW_STEP_RUNNING;
    }
    if ((AutoDevice_GetProcessTick() - temp_fault_start_tick) < SPRAY_TEMP_FAULT_CONFIRM_MS)
    {
      return APP_FLOW_STEP_RUNNING;
    }

    safe_stop();
    AutoDevice_SetFault((data.pipe_temperature_valid == 0U) ?
                        AUTO_FAULT_PIPE_TEMP_SENSOR_INVALID :
                        AUTO_FAULT_PIPE_TEMP_NOT_READY);
    return APP_FLOW_STEP_FAIL;
  }
  temp_fault_start_tick = 0U;
  if ((AutoDevice_GetProcessTick() - run_start_tick) >= recipe->spray_time_ms)
  {
    return APP_FLOW_STEP_DONE;
  }
  return APP_FLOW_STEP_RUNNING;
}

static AppFlowStepResult ValvesPumpOff(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  (void)AutoDevice_SetBoosterPumpRunning(0U);
  AutoDevice_SetAllShowerValves(0U);
  return APP_FLOW_STEP_DONE;
}

static AppFlowStepResult ReportDone(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  AutoDevice_Report(AUTO_REPORT_SPRAY_DONE);
  return APP_FLOW_STEP_DONE;
}

const AutoStageDefinition *AutoStage_Spray_Get(void)
{
  return &stage;
}
