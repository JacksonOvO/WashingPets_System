#include "auto_stages.h"

#include "app_param.h"
#include "auto_device.h"

static uint32_t bath_start_tick;
static uint32_t feed_start_tick;
static uint32_t flush_start_tick;
static uint32_t bath_step_tick;

static void BathReset(void)
{
  bath_start_tick = 0U;
  feed_start_tick = 0U;
  flush_start_tick = 0U;
  bath_step_tick = 0U;
}

static void BathInit(void)
{
  BathReset();
}

static HAL_StatusTypeDef BathResume(uint16_t step_id)
{
  const AppRecipe_t *recipe = AppParam_GetActiveRecipe();

  if ((step_id < 30U) || (step_id >= 110U))
  {
    return HAL_OK;
  }

  AutoDevice_SetAllShowerValves(1U);
  if (AutoDevice_SetBoosterPumpPercent(recipe->bath_pressure_level) != HAL_OK)
  {
    return HAL_ERROR;
  }

  if (step_id == 85U)
  {
    return AutoDevice_SetBoosterPumpRunning(0U);
  }

  if (AutoDevice_SetBoosterPumpRunning(1U) != HAL_OK)
  {
    return HAL_ERROR;
  }

  if (step_id < 90U)
  {
    if (AutoDevice_SetOutput(AUTO_OUTPUT_DISINFECTANT_VALVE, 1U) != HAL_OK)
    {
      return HAL_ERROR;
    }
    if ((step_id >= 60U) &&
        (AutoDevice_SetOutput(AUTO_OUTPUT_SCREW_PUMP, 1U) != HAL_OK))
    {
      return HAL_ERROR;
    }
  }
  return HAL_OK;
}

static void stop_bath(void)
{
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_SCREW_PUMP, 0U);
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_BATH_LIQUID_VALVE, 0U);
  (void)AutoDevice_SetBoosterPumpRunning(0U);
  AutoDevice_SetAllShowerValves(0U);
}

static AppFlowStepResult CheckAllow(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  if ((AutoDevice_ReadInput(AUTO_INPUT_UPPER_DOOR_CLOSED) == 0U) ||
      (AutoDevice_ReadInput(AUTO_INPUT_LOWER_DOOR_CLOSED) == 0U))
  {
    stop_bath();
    AutoDevice_SetFault(AUTO_FAULT_OUTPUT_COMMAND_FAILED);
    return APP_FLOW_STEP_FAIL;
  }
  return APP_FLOW_STEP_DONE;
}

static AppFlowStepResult CheckLiquid(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  return (AutoDevice_ReadInput(AUTO_INPUT_BATH_LIQUID_LOW) == 0U) ?
         APP_FLOW_STEP_DONE :
         (AutoDevice_SetFault(AUTO_FAULT_BATH_LIQUID_LOW),
          APP_FLOW_STEP_FAIL);
}

static AppFlowStepResult WaterPathOn(AppFlowContext *ctx, const AppFlowStep *step)
{
  const AppRecipe_t *recipe = AppParam_GetActiveRecipe();
  (void)ctx;
  (void)step;
  AutoDevice_SetAllShowerValves(1U);
  (void)AutoDevice_SetBoosterPumpPercent(recipe->bath_pressure_level);
  (void)AutoDevice_SetBoosterPumpRunning(1U);
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_DISINFECTANT_VALVE, 1U);
  return APP_FLOW_STEP_DONE;
}

static void set_bath_pumps(uint8_t enabled)
{
  const AppRecipe_t *recipe = AppParam_GetActiveRecipe();

  if (enabled != 0U)
  {
    (void)AutoDevice_SetBoosterPumpPercent(recipe->bath_pressure_level);
    (void)AutoDevice_SetBoosterPumpRunning(1U);
    (void)AutoDevice_SetOutput(AUTO_OUTPUT_SCREW_PUMP, 1U);
  }
  else
  {
    (void)AutoDevice_SetBoosterPumpRunning(0U);
    (void)AutoDevice_SetOutput(AUTO_OUTPUT_SCREW_PUMP, 0U);
  }
}

static AppFlowStepResult LiquidPumpOn(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_SCREW_PUMP, 1U);
  feed_start_tick = AutoDevice_GetProcessTick();
  return APP_FLOW_STEP_DONE;
}

static AppFlowStepResult MixRunning(AppFlowContext *ctx, const AppFlowStep *step)
{
  const AppRecipe_t *recipe = AppParam_GetActiveRecipe();
  (void)ctx;
  (void)step;

  if ((AutoDevice_GetProcessTick() - feed_start_tick) < recipe->liquid_soak_time_ms)
  {
    return APP_FLOW_STEP_RUNNING;
  }

  //(void)AutoDevice_SetOutput(AUTO_OUTPUT_SCREW_PUMP, 0U);
  //(void)AutoDevice_SetOutput(AUTO_OUTPUT_BATH_LIQUID_VALVE, 0U);
  bath_start_tick = AutoDevice_GetProcessTick();
  bath_step_tick = 0U;
  return APP_FLOW_STEP_DONE;
}

static uint8_t bath_temperature_ok(const AppRecipe_t *recipe)
{
  AutoSensorData data;

  (void)AutoDevice_GetSensorData(&data);
  if ((data.pipe_temperature_valid == 0U) ||
      (data.pipe_temperature_x10 <
       (recipe->tank_target_temp_x10 -
        (int16_t)recipe->pipe_temp_tolerance_x10)) ||
      (data.pipe_temperature_x10 >
       (recipe->tank_target_temp_x10 +
        (int16_t)recipe->pipe_temp_tolerance_x10)))
  {
    return 0U;
  }
  return 1U;
}

static AppFlowStepResult BathSpray(AppFlowContext *ctx, const AppFlowStep *step)
{
  const AppRecipe_t *recipe = AppParam_GetActiveRecipe();
  (void)step;

  if (bath_temperature_ok(recipe) == 0U)
  {
    stop_bath();
    AutoDevice_SetFault(AUTO_FAULT_PIPE_TEMP_NOT_READY);
    return APP_FLOW_STEP_FAIL;
  }
  if ((uint32_t)(AutoDevice_GetProcessTick() - bath_start_tick) >= recipe->bath_time_ms)
  {
    return APP_FLOW_STEP_DONE;
  }

  if (bath_step_tick == 0U)
  {
    bath_step_tick = AutoDevice_GetProcessTick();
    set_bath_pumps(1U);
  }

  if ((recipe->bath_on_time_ms == 0U) ||
      ((uint32_t)(AutoDevice_GetProcessTick() - bath_step_tick) >= recipe->bath_on_time_ms))
  {
    bath_step_tick = 0U;
  return (AppFlow_JumpToStep(ctx, 85U) != 0U) ?
           APP_FLOW_STEP_JUMP :
           (AutoDevice_SetFault(AUTO_FAULT_FLOW_JUMP_FAILED),
            APP_FLOW_STEP_FAIL);
  }
  return APP_FLOW_STEP_RUNNING;
}

static AppFlowStepResult BathPause(AppFlowContext *ctx, const AppFlowStep *step)
{
  const AppRecipe_t *recipe = AppParam_GetActiveRecipe();
  (void)step;

  if (bath_temperature_ok(recipe) == 0U)
  {
    stop_bath();
    AutoDevice_SetFault(AUTO_FAULT_PIPE_TEMP_NOT_READY);
    return APP_FLOW_STEP_FAIL;
  }

  if (bath_step_tick == 0U)
  {
    bath_step_tick = AutoDevice_GetProcessTick();
    set_bath_pumps(0U);
  }

  if ((recipe->bath_pause_time_ms != 0U) &&
      ((uint32_t)(AutoDevice_GetProcessTick() - bath_step_tick) < recipe->bath_pause_time_ms))
  {
    return APP_FLOW_STEP_RUNNING;
  }

  bath_step_tick = 0U;
  if ((uint32_t)(AutoDevice_GetProcessTick() - bath_start_tick) >= recipe->bath_time_ms)
  {
    return APP_FLOW_STEP_DONE;
  }
  return (AppFlow_JumpToStep(ctx, 80U) != 0U) ?
         APP_FLOW_STEP_JUMP :
         (AutoDevice_SetFault(AUTO_FAULT_FLOW_JUMP_FAILED),
          APP_FLOW_STEP_FAIL);
}

static AppFlowStepResult LiquidOff(AppFlowContext *ctx, const AppFlowStep *step)
{
  const AppRecipe_t *recipe = AppParam_GetActiveRecipe();
  (void)ctx;
  (void)step;
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_SCREW_PUMP, 0U);
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_DISINFECTANT_VALVE, 0U);
  (void)AutoDevice_SetBoosterPumpPercent(recipe->bath_pressure_level);
  (void)AutoDevice_SetBoosterPumpRunning(1U);
  flush_start_tick = AutoDevice_GetProcessTick();
  return APP_FLOW_STEP_DONE;
}

static AppFlowStepResult FlushPipe(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  return ((AutoDevice_GetProcessTick() - flush_start_tick) >= BATH_PIPE_FLUSH_TIME_MS) ?
         APP_FLOW_STEP_DONE : APP_FLOW_STEP_RUNNING;
}

static AppFlowStepResult StopAll(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  stop_bath();
  return APP_FLOW_STEP_DONE;
}

static AppFlowStepResult ReportDone(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  AutoDevice_Report(AUTO_REPORT_BATH_DONE);
  return APP_FLOW_STEP_DONE;
}

static const AppFlowStep steps[] = {
  AUTO_STEP(10, "BATH_CHECK_ALLOW", 0U, 0U, CheckAllow, "检查上下门状态"),
  AUTO_STEP(20, "BATH_CHECK_LIQUID", 0U, 0U, CheckLiquid, "检查沐浴液低液位信号"),
  AUTO_STEP(30, "BATH_WATER_PATH_ON", 0U, BATH_WATER_PATH_DELAY_MS, WaterPathOn, "打开淋浴阀、增压泵和沐浴液阀"),
  AUTO_STEP(60, "BATH_LIQUID_PUMP_ON", 0U, 0U, LiquidPumpOn, "启动蠕动泵定量投放沐浴液"),
  AUTO_STEP(70, "BATH_MIX_RUNNING", 0U, 0U, MixRunning, "达到投液时间后开始沐浴计时"),
  AUTO_STEP(80, "BATH_SPRAY", 0U, 0U, BathSpray, "按配方出水时间喷淋并检查水温"),
  AUTO_STEP(85, "BATH_PAUSE", 0U, 0U, BathPause, "按配方暂停时间静止并检查沐浴总时间"),
  AUTO_STEP(90, "BATH_LIQUID_PUMP_OFF", 0U, 0U, LiquidOff, "关闭投液泵和沐浴液阀"),
  AUTO_STEP(100, "BATH_FLUSH_PIPE", 0U, 0U, FlushPipe, "保持清水喷淋冲净药液管路"),
  AUTO_STEP(110, "BATH_STOP_ALL", 0U, 0U, StopAll, "关闭增压泵和全部淋浴阀"),
  AUTO_STEP(120, "REPORT_BATH_DONE", 0U, BATH_REPORT_DELAY_MS, ReportDone, "上报沐浴阶段完成"),
};

static const AutoStageDefinition stage = {
  "Bathing", steps, (uint16_t)(sizeof(steps) / sizeof(steps[0])),
  BathInit, BathReset, BathResume
};

const AutoStageDefinition *AutoStage_Bath_Get(void)
{
  return &stage;
}
