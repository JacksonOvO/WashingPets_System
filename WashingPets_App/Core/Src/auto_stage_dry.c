#include "auto_stages.h"

#include "app_param.h"
#include "auto_device.h"
#include "rs485_config.h"
#include "task_comm485.h"
#include "task_output.h"
#include "task_water_tank.h"

#define DRY_HEATER_FULL_PERCENT         100U
#define DRY_HEATER_HOLD_MIN_PERCENT      25U
#define DRY_TEMP_CONTROL_INTERVAL_MS   5000U
#define DRY_FAN_START_PERCENT            50U
#define DRY_FAN_FULL_PERCENT            100U
#define DRY_TOP_EXHAUST_PERCENT           70U
#define DRY_DEFAULT_TARGET_X10           335

static uint32_t phase_tick;
static uint32_t dry_start_tick;
static uint32_t control_tick;
static int16_t last_temp_x10;
static uint32_t last_temp_tick;
static uint8_t heater_percent;
static uint8_t fan_percent;
static uint8_t blower_enabled;

static void DryReset(void)
{
  phase_tick = 0U;
  dry_start_tick = 0U;
  control_tick = 0U;
  last_temp_x10 = 0;
  last_temp_tick = 0U;
  heater_percent = 0U;
  fan_percent = 0U;
  blower_enabled = 0U;
}

static void DryInit(void)
{
  DryReset();
  TaskWaterTank_RequestSelfCleanPrepare();
}

static HAL_StatusTypeDef set_fans(uint8_t percent)
{
  if (TaskComm485_RequestSetPercent(RS485_STATION_BOTTOM_FAN, percent) != HAL_OK)
  {
    AutoDevice_SetFault(AUTO_FAULT_485_BOTTOM_FAN_COMM);
    return HAL_ERROR;
  }
  fan_percent = percent;
  return HAL_OK;
}

static HAL_StatusTypeDef set_exhaust_fan(uint8_t on)
{
  if ((on != 0U) &&
      (TaskOutput_SetTopExhaustPercent(DRY_TOP_EXHAUST_PERCENT) != HAL_OK))
  {
    AutoDevice_SetFault(AUTO_FAULT_485_TOP_EXHAUST_COMM);
    return HAL_ERROR;
  }
  if (AutoDevice_SetTopFanRunning(on) != HAL_OK)
  {
    AutoDevice_SetFault(AUTO_FAULT_OUTPUT_COMMAND_FAILED);
    return HAL_ERROR;
  }
  if ((on == 0U) &&
      (TaskOutput_SetTopExhaustPercent(DRY_FAN_FULL_PERCENT) != HAL_OK))
  {
    AutoDevice_SetFault(AUTO_FAULT_OUTPUT_COMMAND_FAILED);
    return HAL_ERROR;
  }
  return HAL_OK;
}

static HAL_StatusTypeDef set_heaters(uint8_t percent)
{

  if (TaskComm485_RequestSetPercent(RS485_STATION_BOTTOM_HEATER, percent) != HAL_OK)
  {
    AutoDevice_SetFault(AUTO_FAULT_485_BOTTOM_HEATER_COMM);
    return HAL_ERROR;
  }
  return HAL_OK;
}

static HAL_StatusTypeDef DryResume(uint16_t step_id)
{
  if (AutoDevice_SetBlower(blower_enabled) != HAL_OK ||
      AutoDevice_SetOutput(AUTO_OUTPUT_SWING_MOTOR, blower_enabled) != HAL_OK) return HAL_ERROR;
  if ((step_id < 20U) || (step_id >= 100U))
  {
    return HAL_OK;
  }

  if ((step_id < 90U) &&
      ((set_fans(fan_percent) != HAL_OK) ||
       (set_heaters(heater_percent) != HAL_OK)))
  {
    return HAL_ERROR;
  }

  if ((step_id >= 25U) && (step_id < 95U) &&
      (set_exhaust_fan(1U) != HAL_OK))
  {
    return HAL_ERROR;
  }
  return HAL_OK;
}

static void dry_safe_stop(void)
{
  blower_enabled = 0U;
  (void)AutoDevice_SetBlower(0U);
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_SWING_MOTOR, 0U);
  (void)set_heaters(0U);
  (void)set_fans(0U);
  (void)set_exhaust_fan(0U);
}

static int16_t dry_recipe_target_x10(const AppRecipe_t *recipe)
{
  return (recipe != NULL) ? recipe->dry_target_cabin_temp_x10 :
                            DRY_DEFAULT_TARGET_X10;
}

static uint8_t dry_base_heater_percent(int16_t temperature_x10,
                                       int16_t target_x10)
{
  if (temperature_x10 >= (target_x10 + 25)) return 0U;
  if (temperature_x10 >= (target_x10 + 15)) return DRY_HEATER_HOLD_MIN_PERCENT;
  if (temperature_x10 >= (target_x10 + 5)) return 40U;
  if (temperature_x10 >= target_x10) return 55U;
  if (temperature_x10 >= (target_x10 - 5)) return 70U;
  if (temperature_x10 >= (target_x10 - 15)) return 85U;
  return DRY_HEATER_FULL_PERCENT;
}

static uint8_t dry_adjust_for_rise_rate(uint8_t base_percent,
                                        int16_t temperature_x10,
                                        uint32_t now,
                                        int16_t target_x10)
{
  int32_t delta_x10;
  uint32_t elapsed_ms;
  int32_t rise_x10_per_min;

  if ((last_temp_tick == 0U) || (now == last_temp_tick))
  {
    last_temp_x10 = temperature_x10;
    last_temp_tick = now;
    return base_percent;
  }

  elapsed_ms = now - last_temp_tick;
  if (elapsed_ms < DRY_TEMP_CONTROL_INTERVAL_MS)
  {
    return base_percent;
  }

  delta_x10 = (int32_t)temperature_x10 - (int32_t)last_temp_x10;
  rise_x10_per_min = (delta_x10 * 60000L) / (int32_t)elapsed_ms;
  last_temp_x10 = temperature_x10;
  last_temp_tick = now;

  if (temperature_x10 >= (target_x10 + 25))
  {
    return 0U;
  }
  if ((temperature_x10 >= (target_x10 + 15)) && (rise_x10_per_min >= 3L))
  {
    return DRY_HEATER_HOLD_MIN_PERCENT;
  }
  if ((temperature_x10 >= (target_x10 - 5)) && (rise_x10_per_min >= 5L))
  {
    return (base_percent > (DRY_HEATER_HOLD_MIN_PERCENT + 20U)) ?
           (uint8_t)(base_percent - 20U) : DRY_HEATER_HOLD_MIN_PERCENT;
  }
  return base_percent;
}

static void dry_apply_temperature_control(const TaskComm485EnvironmentData *environment,
                                          const AppRecipe_t *recipe)
{
  uint8_t percent;
  uint32_t now = AutoDevice_GetProcessTick();
  int16_t target_x10 = dry_recipe_target_x10(recipe);

  if ((now - control_tick) < DRY_TEMP_CONTROL_INTERVAL_MS)
  {
    return;
  }

  percent = dry_base_heater_percent(environment->incab_temperature_x10,
                                    target_x10);
  percent = dry_adjust_for_rise_rate(percent,
                                     environment->incab_temperature_x10,
                                     now,
                                     target_x10);

  if ((percent != heater_percent) || (control_tick == 0U))
  {
    if (set_heaters(percent) == HAL_OK)
    {
      heater_percent = percent;
    }
  }
  control_tick = now;
}

static AppFlowStepResult CheckAllow(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  (void)AutoDevice_SetBoosterPumpRunning(0U);
  if ((AutoDevice_ReadInput(AUTO_INPUT_UPPER_DOOR_CLOSED) == 0U) ||
      (AutoDevice_ReadInput(AUTO_INPUT_LOWER_DOOR_CLOSED) == 0U))
  {
    dry_safe_stop();
    /* 门未关闭到位属于安全联锁失败，使用专用故障码，便于上位机定位。 */
    AutoDevice_SetFault(AUTO_FAULT_DOOR_NOT_CLOSED);
    return APP_FLOW_STEP_FAIL;
  }
  return APP_FLOW_STEP_DONE;
}

static AppFlowStepResult FanFullOn(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  if (set_fans(DRY_FAN_START_PERCENT) != HAL_OK)
  {
    return APP_FLOW_STEP_FAIL;
  }
  blower_enabled = 1U;
  if (AutoDevice_SetBlower(1U) != HAL_OK ||
      AutoDevice_SetOutput(AUTO_OUTPUT_SWING_MOTOR, 1U) != HAL_OK) return APP_FLOW_STEP_FAIL;
  heater_percent = DRY_HEATER_FULL_PERCENT;
  if (set_heaters(heater_percent) != HAL_OK)
  {
    return APP_FLOW_STEP_FAIL;
  }
  phase_tick = AutoDevice_GetProcessTick();
  return APP_FLOW_STEP_DONE;
}

static AppFlowStepResult ExhaustFanOn(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  return (set_exhaust_fan(1U) == HAL_OK) ? APP_FLOW_STEP_DONE :
                                           APP_FLOW_STEP_FAIL;
}

static AppFlowStepResult RampFanToFull(AppFlowContext *ctx, const AppFlowStep *step)
{
  uint32_t elapsed_ms;
  uint8_t percent;
  (void)ctx;
  (void)step;

  elapsed_ms = AutoDevice_GetProcessTick() - phase_tick;
  if (elapsed_ms >= DRY_FAN_RAMP_TIME_MS)
  {
    if (set_fans(DRY_FAN_FULL_PERCENT) != HAL_OK)
    {
      return APP_FLOW_STEP_FAIL;
    }
    phase_tick = AutoDevice_GetProcessTick();
    return APP_FLOW_STEP_DONE;
  }

  percent = (uint8_t)(DRY_FAN_START_PERCENT +
            ((elapsed_ms * (DRY_FAN_FULL_PERCENT - DRY_FAN_START_PERCENT)) /
             DRY_FAN_RAMP_TIME_MS));
  if (percent != fan_percent)
  {
    if (set_fans(percent) != HAL_OK)
    {
      return APP_FLOW_STEP_FAIL;
    }
  }
  return APP_FLOW_STEP_RUNNING;
}

static AppFlowStepResult HoldFanFull(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  if (set_fans(DRY_FAN_FULL_PERCENT) != HAL_OK)
  {
    return APP_FLOW_STEP_FAIL;
  }
  return ((AutoDevice_GetProcessTick() - phase_tick) >= DRY_FAN_FULL_HOLD_TIME_MS) ?
         APP_FLOW_STEP_DONE : APP_FLOW_STEP_RUNNING;
}

static AppFlowStepResult FanTargetHeaterFullOn(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  if (set_fans(AppParam_GetActiveRecipe()->dry_fan_percent) != HAL_OK)
  {
    return APP_FLOW_STEP_FAIL;
  }
  heater_percent = DRY_HEATER_FULL_PERCENT;
  if (set_heaters(heater_percent) != HAL_OK)
  {
    return APP_FLOW_STEP_FAIL;
  }
  dry_start_tick = AutoDevice_GetProcessTick();
  phase_tick = dry_start_tick;
  control_tick = 0U;
  last_temp_tick = 0U;
  return APP_FLOW_STEP_DONE;
}

static AppFlowStepResult DryRunning(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  return APP_FLOW_STEP_DONE;
}

static AppFlowStepResult Monitor(AppFlowContext *ctx, const AppFlowStep *step)
{
  TaskComm485EnvironmentData environment;
  const AppRecipe_t *recipe = AppParam_GetActiveRecipe();
  int16_t target_x10 = dry_recipe_target_x10(recipe);
  (void)ctx;
  (void)step;

  if ((TaskComm485_GetEnvironmentData(&environment) == 0U) ||
      (environment.incab_valid == 0U))
  {
    dry_safe_stop();
    AutoDevice_SetFault(AUTO_FAULT_DRY_CABIN_TEMP_SENSOR_INVALID);
    return APP_FLOW_STEP_FAIL;
  }

  if (environment.incab_temperature_x10 >= (target_x10 + 45))
  {
    dry_safe_stop();
    AutoDevice_SetFault(AUTO_FAULT_DRY_TEMP_CONTROL_FAILED);
    return APP_FLOW_STEP_FAIL;
  }

  if (environment.incab_temperature_x10 >= (target_x10 + 25))
  {
    if (heater_percent != 0U)
    {
      heater_percent = 0U;
      if (set_heaters(0U) != HAL_OK)
      {
        return APP_FLOW_STEP_FAIL;
      }
    }
  }
  else
  {
    dry_apply_temperature_control(&environment, recipe);
  }

  return ((AutoDevice_GetProcessTick() - dry_start_tick) >= AppParam_GetActiveRecipe()->dry_time_ms) ?
         APP_FLOW_STEP_DONE : APP_FLOW_STEP_RUNNING;
}

static AppFlowStepResult HeaterOff(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  blower_enabled = 0U;
  if (AutoDevice_SetBlower(0U) != HAL_OK ||
      AutoDevice_SetOutput(AUTO_OUTPUT_SWING_MOTOR, 0U) != HAL_OK) return APP_FLOW_STEP_FAIL;
  heater_percent = 0U;
  if ((set_fans(60U) != HAL_OK) ||
      (set_heaters(0U) != HAL_OK))
  {
    return APP_FLOW_STEP_FAIL;
  }
  phase_tick = AutoDevice_GetProcessTick();
  return APP_FLOW_STEP_DONE;
}

static AppFlowStepResult CoolAir(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  return ((AutoDevice_GetProcessTick() - phase_tick) >= DRY_HEATER_COOLDOWN_TIME_MS) ?
         APP_FLOW_STEP_DONE : APP_FLOW_STEP_RUNNING;
}

static AppFlowStepResult FanOff(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  if (set_fans(0U) != HAL_OK)
  {
    return APP_FLOW_STEP_FAIL;
  }
  return APP_FLOW_STEP_DONE;
}

static AppFlowStepResult ExhaustFanOff(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  return (set_exhaust_fan(0U) == HAL_OK) ? APP_FLOW_STEP_DONE :
                                           APP_FLOW_STEP_FAIL;
}

static AppFlowStepResult ReportDone(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  AutoDevice_Report(AUTO_REPORT_DRY_DONE);
  return APP_FLOW_STEP_DONE;
}

static const AppFlowStep steps[] = {
  AUTO_STEP(10, "DRY_CHECK_ALLOW", 0U, 0U, CheckAllow, "检查上下门状态"),
  AUTO_STEP(20, "DRY_FAN_RAMP_START", 0U, 0U, FanFullOn, "风机50%启动，加热棒100%保持"),
  AUTO_STEP(25, "DRY_EXHAUST_FAN_ON", 0U, 0U, ExhaustFanOn, "打开排风扇"),
  AUTO_STEP(30, "DRY_FAN_RAMP_TO_FULL", 0U, 0U, RampFanToFull, "风机2分钟内逐步升到100%"),
  AUTO_STEP(35, "DRY_FAN_FULL_HOLD", 0U, 0U, HoldFanFull, "风机100%运行2分钟"),
  AUTO_STEP(40, "DRY_TARGET_FAN_HEATER_FULL", 0U, 0U, FanTargetHeaterFullOn, "风机降到配方目标，开始温控"),
  AUTO_STEP(50, "DRY_RUNNING", 0U, 0U, DryRunning, "进入烘干运行状态"),
  AUTO_STEP(60, "DRY_TEMP_CONTROL", 0U, 0U, Monitor, "舱内温度保持配方目标温度"),
  AUTO_STEP(70, "DRY_HEATER_OFF", 0U, 0U, HeaterOff, "关闭吹水机和底部加热棒，底部低速冷却"),
  AUTO_STEP(80, "DRY_COOL_AIR", 0U, 0U, CoolAir, "保持风机和排风扇冷却"),
  AUTO_STEP(90, "DRY_FAN_OFF", 0U, 0U, FanOff, "关闭底部风机"),
  AUTO_STEP(95, "DRY_EXHAUST_FAN_OFF", 0U, 0U, ExhaustFanOff, "关闭排风扇"),
  AUTO_STEP(100, "REPORT_DRY_DONE", 0U, 0U, ReportDone, "上报烘干完成"),
};

static const AutoStageDefinition stage = {
  "Drying", steps, (uint16_t)(sizeof(steps) / sizeof(steps[0])),
  DryInit, DryReset, DryResume
};

const AutoStageDefinition *AutoStage_Dry_Get(void)
{
  return &stage;
}
