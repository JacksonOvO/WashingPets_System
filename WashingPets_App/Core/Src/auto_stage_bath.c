#include "auto_stages.h"

#include "app_param.h"
#include "auto_device.h"
#include "task_output.h"
#include "cmsis_os.h"

typedef enum { BATH_OFF, BATH_FEED, BATH_WATER } BathOutputPhase;
static AutoOutputPoint selected_liquid_valve(void)
{
  return (AutoOutputPoint)(AUTO_OUTPUT_BATH_LIQUID_VALVE +
                           AutoDevice_GetBathLiquidSelection());
}

static BathOutputPhase output_phase;
static uint32_t phase_tick;
static uint32_t completed_cycles;

static HAL_StatusTypeDef wait_booster(uint8_t enabled)
{
  if (!AppParam_BoosterPumpUsesRs485()) return HAL_OK;
  uint32_t started = HAL_GetTick();
  while (AutoDevice_IsBoosterPumpRunning() != enabled)
  {
    if (AutoDevice_IsEmergencyStopped() || AutoDevice_IsPaused() ||
        HAL_GetTick() - started >= 1500U) return HAL_ERROR;
    osDelay(10U);
  }
  return HAL_OK;
}

/* Flush the output queue before timing a phase or enabling the other pump. */
static HAL_StatusTypeDef apply_phase(BathOutputPhase phase)
{
  if ((AutoDevice_SetOutput(AUTO_OUTPUT_BATH_PUMP, 0U) != HAL_OK) ||
      (AutoDevice_SetOutput(selected_liquid_valve(), 0U) != HAL_OK) ||
      (AutoDevice_SetBoosterPumpRunning(0U) != HAL_OK) ||
      (TaskOutput_Sync(200U) != HAL_OK) || (wait_booster(0U) != HAL_OK))
  {
    return HAL_ERROR;
  }
  if (phase == BATH_FEED)
  {
    if ((AutoDevice_SetOutput(selected_liquid_valve(), 1U) != HAL_OK) ||
        (AutoDevice_SetOutput(AUTO_OUTPUT_BATH_PUMP, 1U) != HAL_OK))
    {
      return HAL_ERROR;
    }
  }
  else if (phase == BATH_WATER)
  {
    if ((AutoDevice_SetBoosterPumpPercent(AppParam_GetActiveRecipe()->bath_pressure_level) != HAL_OK) ||
        (AutoDevice_SetBoosterPumpRunning(1U) != HAL_OK))
    {
      return HAL_ERROR;
    }
  }
  if (wait_booster(phase == BATH_WATER) != HAL_OK) return HAL_ERROR;
  if ((TaskOutput_Sync(200U) != HAL_OK) ||
      (TaskOutput_GetActual(AUTO_OUTPUT_BATH_PUMP) != (phase == BATH_FEED)) ||
      (TaskOutput_GetActual(selected_liquid_valve()) != (phase == BATH_FEED)) ||
      (AutoDevice_IsBoosterPumpRunning() != (phase == BATH_WATER)))
  {
    return HAL_ERROR;
  }
  output_phase = phase;
  return HAL_OK;
}

static AppFlowStepResult fail_bath(AutoFaultCode fault)
{
  AutoDevice_AllDangerousOutputsOff();
  output_phase = BATH_OFF;
  AutoDevice_SetFault(fault);
  return APP_FLOW_STEP_FAIL;
}

static AppFlowStepResult jump_to(AppFlowContext *ctx, uint16_t step)
{
  return AppFlow_JumpToStep(ctx, step) ? APP_FLOW_STEP_JUMP :
         fail_bath(AUTO_FAULT_FLOW_JUMP_FAILED);
}

static AppFlowStepResult check_running(void)
{
  const AppRecipe_t *recipe = AppParam_GetActiveRecipe();
  AutoSensorData data;

  if ((AutoDevice_ReadInput(AUTO_INPUT_UPPER_DOOR_CLOSED) == 0U) ||
      (AutoDevice_ReadInput(AUTO_INPUT_LOWER_DOOR_CLOSED) == 0U))
  {
    return fail_bath(AUTO_FAULT_DOOR_NOT_CLOSED);
  }
  if (AutoDevice_ReadInput(AUTO_INPUT_TANK_LOW) != 0U)
  {
    return fail_bath(AUTO_FAULT_TANK_LOW_PROTECT);
  }
  if ((AutoDevice_GetSensorData(&data) == 0U) ||
      (data.pipe_temperature_valid == 0U) ||
      (data.pipe_temperature_x10 < recipe->tank_target_temp_x10 - (int16_t)recipe->pipe_temp_tolerance_x10) ||
      (data.pipe_temperature_x10 > recipe->tank_target_temp_x10 + (int16_t)recipe->pipe_temp_tolerance_x10))
  {
    return fail_bath(AUTO_FAULT_PIPE_TEMP_NOT_READY);
  }
  return APP_FLOW_STEP_DONE;
}

static void BathReset(void)
{
  output_phase = BATH_OFF;
  phase_tick = 0U;
  completed_cycles = 0U;
}

static HAL_StatusTypeDef BathResume(uint16_t step_id)
{
  if ((step_id < 30U) || (step_id >= 110U))
  {
    return HAL_OK;
  }
  if (check_running() == APP_FLOW_STEP_FAIL)
  {
    return HAL_ERROR;
  }
  AutoDevice_SetAllShowerValves(0U);
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_SHOWER_MIDDLE, 1U);
  /* The process clock excludes pauses; keep the phase, timer and cycle count. */
  if (apply_phase(output_phase) != HAL_OK)
  {
    (void)fail_bath(AUTO_FAULT_OUTPUT_COMMAND_FAILED);
    return HAL_ERROR;
  }
  return HAL_OK;
}

static AppFlowStepResult CheckAllow(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)step;
  return AppParam_GetActiveRecipe()->bath_cycle_count == 0U ?
         jump_to(ctx, 110U) : check_running();
}

static AppFlowStepResult CheckLiquid(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  return (AppParam_GetActiveRecipe()->bath_liquid_time_ms == 0U ||
          AutoDevice_ReadInput((AutoInputPoint)(AUTO_INPUT_BATH_LIQUID_LOW + AutoDevice_GetBathLiquidSelection())) == 0U) ?
         APP_FLOW_STEP_DONE : fail_bath(AUTO_FAULT_BATH_LIQUID_LOW);
}

static AppFlowStepResult WaterPathOn(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  if ((AutoDevice_SetOutput(AUTO_OUTPUT_BATH_LIQUID_VALVE, 0U) != HAL_OK) ||
      (AutoDevice_SetOutput(AUTO_OUTPUT_FRAGRANCE_VALVE, 0U) != HAL_OK) ||
      (AutoDevice_SetOutput(AUTO_OUTPUT_SOFTENER_VALVE, 0U) != HAL_OK) ||
      (AutoDevice_SetOutput(AUTO_OUTPUT_DEODORIZER_VALVE, 0U) != HAL_OK))
  {
    return fail_bath(AUTO_FAULT_OUTPUT_COMMAND_FAILED);
  }
  AutoDevice_SetAllShowerValves(0U);
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_SHOWER_MIDDLE, 1U);
  return apply_phase(BATH_OFF) == HAL_OK ? APP_FLOW_STEP_DONE :
         fail_bath(AUTO_FAULT_OUTPUT_COMMAND_FAILED);
}

static AppFlowStepResult LiquidPumpOn(AppFlowContext *ctx, const AppFlowStep *step)
{
  if (AppParam_GetActiveRecipe()->bath_liquid_time_ms == 0U)
  {
    return jump_to(ctx, 80U);
  }
  if ((check_running() == APP_FLOW_STEP_FAIL) ||
      (CheckLiquid(ctx, step) == APP_FLOW_STEP_FAIL))
  {
    return APP_FLOW_STEP_FAIL;
  }
  if (apply_phase(BATH_FEED) != HAL_OK)
  {
    return fail_bath(AUTO_FAULT_OUTPUT_COMMAND_FAILED);
  }
  phase_tick = AutoDevice_GetProcessTick();
  return APP_FLOW_STEP_DONE;
}

static AppFlowStepResult MixRunning(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  if (check_running() == APP_FLOW_STEP_FAIL) return APP_FLOW_STEP_FAIL;
  if ((AutoDevice_GetProcessTick() - phase_tick) < AppParam_GetActiveRecipe()->bath_liquid_time_ms)
  {
    return APP_FLOW_STEP_RUNNING;
  }
  return apply_phase(BATH_OFF) == HAL_OK ? APP_FLOW_STEP_DONE :
         fail_bath(AUTO_FAULT_OUTPUT_COMMAND_FAILED);
}

static AppFlowStepResult BathSpray(AppFlowContext *ctx, const AppFlowStep *step)
{
  const AppRecipe_t *recipe = AppParam_GetActiveRecipe();
  (void)step;
  if (check_running() == APP_FLOW_STEP_FAIL) return APP_FLOW_STEP_FAIL;
  if (output_phase != BATH_WATER)
  {
    if (apply_phase(BATH_WATER) != HAL_OK)
      return fail_bath(AUTO_FAULT_OUTPUT_COMMAND_FAILED);
    phase_tick = AutoDevice_GetProcessTick();
  }
  if ((AutoDevice_GetProcessTick() - phase_tick) < recipe->bath_on_time_ms)
    return APP_FLOW_STEP_RUNNING;
  if (apply_phase(BATH_OFF) != HAL_OK)
    return fail_bath(AUTO_FAULT_OUTPUT_COMMAND_FAILED);
  ++completed_cycles;
  phase_tick = AutoDevice_GetProcessTick();
  return completed_cycles >= recipe->bath_cycle_count ?
         jump_to(ctx, 90U) : APP_FLOW_STEP_DONE;
}

static AppFlowStepResult BathPause(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)step;
  if (check_running() == APP_FLOW_STEP_FAIL) return APP_FLOW_STEP_FAIL;
  if ((AutoDevice_GetProcessTick() - phase_tick) < AppParam_GetActiveRecipe()->bath_pause_time_ms)
    return APP_FLOW_STEP_RUNNING;
  return jump_to(ctx, 60U);
}

static AppFlowStepResult LiquidOff(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  if (check_running() == APP_FLOW_STEP_FAIL) return APP_FLOW_STEP_FAIL;
  if (apply_phase(BATH_WATER) != HAL_OK)
    return fail_bath(AUTO_FAULT_OUTPUT_COMMAND_FAILED);
  phase_tick = AutoDevice_GetProcessTick();
  return APP_FLOW_STEP_DONE;
}

static AppFlowStepResult FlushPipe(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  if (check_running() == APP_FLOW_STEP_FAIL) return APP_FLOW_STEP_FAIL;
  if ((AutoDevice_GetProcessTick() - phase_tick) < BATH_PIPE_FLUSH_TIME_MS)
    return APP_FLOW_STEP_RUNNING;
  return apply_phase(BATH_OFF) == HAL_OK ? APP_FLOW_STEP_DONE :
         fail_bath(AUTO_FAULT_OUTPUT_COMMAND_FAILED);
}

static AppFlowStepResult StopAll(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  AutoDevice_SetAllShowerValves(0U);
  return apply_phase(BATH_OFF) == HAL_OK ? APP_FLOW_STEP_DONE :
         fail_bath(AUTO_FAULT_OUTPUT_COMMAND_FAILED);
}

static AppFlowStepResult ReportDone(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  AutoDevice_Report(AUTO_REPORT_BATH_DONE);
  return APP_FLOW_STEP_DONE;
}

static const AppFlowStep steps[] = {
  AUTO_STEP(10, "BATH_CHECK_ALLOW", 0U, 0U, CheckAllow, "检查门、水位和水温，零次跳过"),
  AUTO_STEP(20, "BATH_CHECK_LIQUID", 0U, 0U, CheckLiquid, "检查沐浴液低液位信号"),
  AUTO_STEP(30, "BATH_WATER_PATH_ON", 0U, BATH_WATER_PATH_DELAY_MS, WaterPathOn, "打开淋浴阀，两泵保持关闭"),
  AUTO_STEP(60, "BATH_LIQUID_PUMP_ON", 0U, 0U, LiquidPumpOn, "每轮打开所选药液阀和沐浴蠕动泵"),
  AUTO_STEP(70, "BATH_MIX_RUNNING", 0U, 0U, MixRunning, "按配方加液时间关闭沐浴蠕动泵和所选药液阀"),
  AUTO_STEP(80, "BATH_SPRAY", 0U, 0U, BathSpray, "增压泵按配方出水时间喷水，完成一轮"),
  AUTO_STEP(85, "BATH_PAUSE", 0U, 0U, BathPause, "暂停后回到投液，末轮不暂停"),
  AUTO_STEP(90, "BATH_LIQUID_PUMP_OFF", 0U, 0U, LiquidOff, "开始末尾清水冲管"),
  AUTO_STEP(100, "BATH_FLUSH_PIPE", 0U, 0U, FlushPipe, "保持清水喷淋5秒冲净管路"),
  AUTO_STEP(110, "BATH_STOP_ALL", 0U, 0U, StopAll, "关闭增压泵和全部淋浴阀"),
  AUTO_STEP(120, "REPORT_BATH_DONE", 0U, BATH_REPORT_DELAY_MS, ReportDone, "上报沐浴阶段完成"),
};

static const AutoStageDefinition stage = {
  "Bathing", steps, (uint16_t)(sizeof(steps) / sizeof(steps[0])),
  BathReset, BathReset, BathResume
};

const AutoStageDefinition *AutoStage_Bath_Get(void)
{
  return &stage;
}
