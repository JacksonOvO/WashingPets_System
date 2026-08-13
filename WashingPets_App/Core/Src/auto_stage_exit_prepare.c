#include "auto_stages.h"

#include "auto_device.h"
#include "rs485_config.h"
#include "task_comm485.h"

#include <string.h>

/*
 * 出舱准备流程：
 * 和入舱准备环境调节逻辑一致，但没有等待上位机开始信号。
 */
#define EXIT_PREP_TEMP_DIFF_OK_X10       40
#define EXIT_PREP_HUM_HIGH_X10          800U
#define EXIT_PREP_OXYGEN_LOW_X10        195U
#define EXIT_PREP_OXYGEN_HIGH_X10       235U

#define EXIT_PREP_FAN_VENT_PERCENT      90U
#define EXIT_PREP_FAN_HEAT_PERCENT      90U
#define EXIT_PREP_FAN_OXYGEN_PERCENT    40U
#define EXIT_PREP_HEATER_PERCENT        100U

#define EXIT_PREP_RECHECK_MS            1000U
#define EXIT_PREP_FAN_PREHEAT_MS        2000U
#define EXIT_PREP_FAN_COOLDOWN_MS       10000U
#define EXIT_PREP_AIR_FRESH_MS          5000U
#define EXIT_PREP_TOTAL_TIMEOUT_MS       1800000U

static AppFlowStepResult WaitEnvironmentData(AppFlowContext *ctx, const AppFlowStep *step);
static AppFlowStepResult CheckTempHumidity(AppFlowContext *ctx, const AppFlowStep *step);
static AppFlowStepResult CoolAndDehumidify(AppFlowContext *ctx, const AppFlowStep *step);
static AppFlowStepResult HeatAndDehumidify(AppFlowContext *ctx, const AppFlowStep *step);
static AppFlowStepResult CoolTemperature(AppFlowContext *ctx, const AppFlowStep *step);
static AppFlowStepResult HeatTemperature(AppFlowContext *ctx, const AppFlowStep *step);
static AppFlowStepResult StopTempHumidity(AppFlowContext *ctx, const AppFlowStep *step);
static AppFlowStepResult CheckOxygen(AppFlowContext *ctx, const AppFlowStep *step);
static AppFlowStepResult OxygenLow(AppFlowContext *ctx, const AppFlowStep *step);
static AppFlowStepResult OxygenHigh(AppFlowContext *ctx, const AppFlowStep *step);
static AppFlowStepResult OxygenNormal(AppFlowContext *ctx, const AppFlowStep *step);
static AppFlowStepResult AirFreshOn(AppFlowContext *ctx, const AppFlowStep *step);
static AppFlowStepResult AirFreshOff(AppFlowContext *ctx, const AppFlowStep *step);
static AppFlowStepResult ReportDone(AppFlowContext *ctx, const AppFlowStep *step);
static void ExitPrepareInit(void);
static void ExitPrepareReset(void);
static HAL_StatusTypeDef ExitPrepareResume(uint16_t step_id);

static const AppFlowStep steps[] = {
  AUTO_STEP(10, "WAIT_ENV_DATA", 0U, 0U, WaitEnvironmentData, "等待7号氧气和9号舱内温湿度数据有效"),
  AUTO_STEP(20, "CHECK_TEMP_HUMIDITY", 0U, 0U, CheckTempHumidity, "根据9号舱内温湿度跳转到对应调节步骤"),
  AUTO_STEP(30, "TEMP_HIGH_HUM_HIGH", 0U, 0U, CoolAndDehumidify, "温度高且湿度高时通风除湿"),
  AUTO_STEP(40, "TEMP_LOW_HUM_HIGH", 0U, 0U, HeatAndDehumidify, "温度低且湿度高时升温除湿"),
  AUTO_STEP(50, "TEMP_HIGH_HUM_OK", 0U, 0U, CoolTemperature, "湿度达标但温度高时通风降温"),
  AUTO_STEP(60, "TEMP_LOW_HUM_OK", 0U, 0U, HeatTemperature, "湿度达标但温度低时升温"),
  AUTO_STEP(70, "STOP_TEMP_HUM_CTRL", 0U, 0U, StopTempHumidity, "温湿度达标后先关加热，冷却后关闭风机"),
  AUTO_STEP(80, "CHECK_OXYGEN", 0U, 0U, CheckOxygen, "判断氧气浓度并跳转到对应调节步骤"),
  AUTO_STEP(90, "OXYGEN_LOW_CTRL", 0U, 0U, OxygenLow, "氧气浓度低时打开制氧机和三路风机"),
  AUTO_STEP(100, "OXYGEN_HIGH_CTRL", 0U, 0U, OxygenHigh, "氧气浓度高时关闭制氧机并通风换气"),
  AUTO_STEP(110, "STOP_OXYGEN_CTRL", 0U, 0U, OxygenNormal, "氧气达标后关闭制氧机"),
  AUTO_STEP(120, "AIR_FRESH_ON", 0U, EXIT_PREP_AIR_FRESH_MS, AirFreshOn, "打开空气清新剂喷雾5秒"),
  AUTO_STEP(130, "AIR_FRESH_OFF", 0U, 0U, AirFreshOff, "关闭空气清新剂喷雾"),
  AUTO_STEP(140, "REPORT_EXIT_PREP_DONE", 0U, 0U, ReportDone, "上报出舱准备完成"),
};

static const AutoStageDefinition stage = {
  "ExitPrepare", steps, (uint16_t)(sizeof(steps) / sizeof(steps[0])),
  ExitPrepareInit, ExitPrepareReset, ExitPrepareResume
};

static TaskComm485EnvironmentData environment;
static uint16_t active_action_step;
static uint32_t active_action_tick;
static uint8_t heaters_enabled;
static uint8_t cooldown_required;
static uint32_t stage_start_tick;

static void ExitPrepareReset(void)
{
  memset(&environment, 0, sizeof(environment));
  active_action_step = 0U;
  active_action_tick = 0U;
  heaters_enabled = 0U;
  cooldown_required = 0U;
  stage_start_tick = 0U;
}

static HAL_StatusTypeDef ExitPrepareResume(uint16_t step_id)
{
  (void)step_id;
  active_action_step = 0U;
  return HAL_OK;
}

static void ExitPrepareInit(void)
{
  ExitPrepareReset();
  stage_start_tick = AutoDevice_GetProcessTick();
}

static AppFlowStepResult jump_to_step(AppFlowContext *ctx, uint16_t step_id)
{
  return (AppFlow_JumpToStep(ctx, step_id) != 0U) ?
         APP_FLOW_STEP_JUMP : APP_FLOW_STEP_FAIL;
}

static uint8_t action_begin(const AppFlowStep *step)
{
  if (active_action_step == step->id)
  {
    return 0U;
  }
  active_action_step = step->id;
  active_action_tick = AutoDevice_GetProcessTick();
  return 1U;
}

static uint8_t action_time_reached(uint32_t delay_ms)
{
  return ((AutoDevice_GetProcessTick() - active_action_tick) >= delay_ms) ? 1U : 0U;
}

static HAL_StatusTypeDef set_all_fans(uint8_t percent)
{
  if (TaskComm485_RequestSetPercent(RS485_STATION_LEFT_FAN, percent) != HAL_OK)
  {
    AutoDevice_SetFault(AUTO_FAULT_485_LEFT_FAN_COMM);
    return HAL_ERROR;
  }
  if (TaskComm485_RequestSetPercent(RS485_STATION_RIGHT_FAN, percent) != HAL_OK)
  {
    AutoDevice_SetFault(AUTO_FAULT_485_RIGHT_FAN_COMM);
    return HAL_ERROR;
  }
  if (TaskComm485_RequestSetPercent(RS485_STATION_BOTTOM_FAN, percent) != HAL_OK)
  {
    AutoDevice_SetFault(AUTO_FAULT_485_BOTTOM_FAN_COMM);
    return HAL_ERROR;
  }
  return HAL_OK;
}

static HAL_StatusTypeDef set_all_heaters(uint8_t percent)
{
  if (TaskComm485_RequestSetPercent(RS485_STATION_LEFT_HEATER, percent) != HAL_OK)
  {
    AutoDevice_SetFault(AUTO_FAULT_485_LEFT_HEATER_COMM);
    return HAL_ERROR;
  }
  if (TaskComm485_RequestSetPercent(RS485_STATION_RIGHT_HEATER, percent) != HAL_OK)
  {
    AutoDevice_SetFault(AUTO_FAULT_485_RIGHT_HEATER_COMM);
    return HAL_ERROR;
  }
  if (TaskComm485_RequestSetPercent(RS485_STATION_BOTTOM_HEATER, percent) != HAL_OK)
  {
    AutoDevice_SetFault(AUTO_FAULT_485_BOTTOM_HEATER_COMM);
    return HAL_ERROR;
  }
  return HAL_OK;
}

static AppFlowStepResult WaitEnvironmentData(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)step;
  active_action_step = 0U;

  if ((AutoDevice_GetProcessTick() - stage_start_tick) >= EXIT_PREP_TOTAL_TIMEOUT_MS)
  {
    AutoDevice_SetFault(AUTO_FAULT_PROCESS_ABORTED);
    return APP_FLOW_STEP_FAIL;
  }

  if ((TaskComm485_GetEnvironmentData(&environment) != 0U) &&
      (environment.valid != 0U) &&
      (environment.incab_valid != 0U))
  {
    return APP_FLOW_STEP_DONE;
  }
  if ((AutoDevice_GetProcessTick() - ctx->phase_tick) >= 5000U)
  {
    AutoDevice_SetFault(AUTO_FAULT_CABIN_TEMP_SENSOR_INVALID);
    return APP_FLOW_STEP_FAIL;
  }
  return APP_FLOW_STEP_RUNNING;
}

static AppFlowStepResult CheckTempHumidity(AppFlowContext *ctx, const AppFlowStep *step)
{
  int16_t incab_temperature;
  int16_t outdoor_temperature;
  int16_t temperature_diff;
  uint16_t humidity;

  (void)step;
  if ((TaskComm485_GetEnvironmentData(&environment) == 0U) ||
      (environment.valid == 0U) ||
      (environment.incab_valid == 0U))
  {
    AutoDevice_SetFault(AUTO_FAULT_CABIN_TEMP_SENSOR_INVALID);
    return APP_FLOW_STEP_FAIL;
  }

  incab_temperature = environment.incab_temperature_x10;
  outdoor_temperature = environment.temperature_x10;
  temperature_diff = (int16_t)(incab_temperature - outdoor_temperature);
  humidity = environment.incab_humidity_x10;
  if (humidity > EXIT_PREP_HUM_HIGH_X10)
  {
    return (temperature_diff < (int16_t)-EXIT_PREP_TEMP_DIFF_OK_X10) ?
           jump_to_step(ctx, 40U) : jump_to_step(ctx, 30U);
  }
  if (temperature_diff > EXIT_PREP_TEMP_DIFF_OK_X10)
  {
    return jump_to_step(ctx, 50U);
  }
  if (temperature_diff < (int16_t)-EXIT_PREP_TEMP_DIFF_OK_X10)
  {
    return jump_to_step(ctx, 60U);
  }
  return jump_to_step(ctx, 70U);
}

static AppFlowStepResult CoolAndDehumidify(AppFlowContext *ctx, const AppFlowStep *step)
{
  if (action_begin(step) != 0U)
  {
    if (set_all_heaters(0U) != HAL_OK)
    {
      return APP_FLOW_STEP_FAIL;
    }
    heaters_enabled = 0U;
    if ((set_all_fans(EXIT_PREP_FAN_VENT_PERCENT) != HAL_OK) ||
        (AutoDevice_SetOutput(AUTO_OUTPUT_EXHAUST_FAN, 1U) != HAL_OK))
    {
      return APP_FLOW_STEP_FAIL;
    }
  }
  return (action_time_reached(EXIT_PREP_RECHECK_MS) != 0U) ?
         jump_to_step(ctx, 10U) : APP_FLOW_STEP_RUNNING;
}

static AppFlowStepResult HeatAndDehumidify(AppFlowContext *ctx, const AppFlowStep *step)
{
  if (action_begin(step) != 0U)
  {
    if ((set_all_fans(EXIT_PREP_FAN_HEAT_PERCENT) != HAL_OK) ||
        (AutoDevice_SetOutput(AUTO_OUTPUT_EXHAUST_FAN, 1U) != HAL_OK))
    {
      return APP_FLOW_STEP_FAIL;
    }
  }
  if (heaters_enabled != 0U)
  {
    return (action_time_reached(EXIT_PREP_RECHECK_MS) != 0U) ?
           jump_to_step(ctx, 10U) : APP_FLOW_STEP_RUNNING;
  }
  if (action_time_reached(EXIT_PREP_FAN_PREHEAT_MS) != 0U)
  {
    if (set_all_heaters(EXIT_PREP_HEATER_PERCENT) != HAL_OK)
    {
      return APP_FLOW_STEP_FAIL;
    }
    heaters_enabled = 1U;
    return jump_to_step(ctx, 10U);
  }
  return APP_FLOW_STEP_RUNNING;
}

static AppFlowStepResult CoolTemperature(AppFlowContext *ctx, const AppFlowStep *step)
{
  return CoolAndDehumidify(ctx, step);
}

static AppFlowStepResult HeatTemperature(AppFlowContext *ctx, const AppFlowStep *step)
{
  if (action_begin(step) != 0U)
  {
    if ((set_all_fans(EXIT_PREP_FAN_HEAT_PERCENT) != HAL_OK) ||
        (AutoDevice_SetOutput(AUTO_OUTPUT_EXHAUST_FAN, 0U) != HAL_OK))
    {
      return APP_FLOW_STEP_FAIL;
    }
  }
  if (heaters_enabled != 0U)
  {
    return (action_time_reached(EXIT_PREP_RECHECK_MS) != 0U) ?
           jump_to_step(ctx, 10U) : APP_FLOW_STEP_RUNNING;
  }
  if (action_time_reached(EXIT_PREP_FAN_PREHEAT_MS) != 0U)
  {
    if (set_all_heaters(EXIT_PREP_HEATER_PERCENT) != HAL_OK)
    {
      return APP_FLOW_STEP_FAIL;
    }
    heaters_enabled = 1U;
    return jump_to_step(ctx, 10U);
  }
  return APP_FLOW_STEP_RUNNING;
}

static AppFlowStepResult StopTempHumidity(AppFlowContext *ctx, const AppFlowStep *step)
{
  if (action_begin(step) != 0U)
  {
    cooldown_required = heaters_enabled;
    if (set_all_heaters(0U) != HAL_OK)
    {
      return APP_FLOW_STEP_FAIL;
    }
    heaters_enabled = 0U;
    if (AutoDevice_SetOutput(AUTO_OUTPUT_EXHAUST_FAN, 0U) != HAL_OK)
    {
      return APP_FLOW_STEP_FAIL;
    }
  }
  if ((cooldown_required != 0U) &&
      (action_time_reached(EXIT_PREP_FAN_COOLDOWN_MS) == 0U))
  {
    return APP_FLOW_STEP_RUNNING;
  }
  if (set_all_fans(0U) != HAL_OK)
  {
    return APP_FLOW_STEP_FAIL;
  }
  cooldown_required = 0U;
  return jump_to_step(ctx, 80U);
}

static AppFlowStepResult CheckOxygen(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)step;
  if (TaskComm485_GetEnvironmentData(&environment) == 0U)
  {
    AutoDevice_SetFault(AUTO_FAULT_OXYGEN_SENSOR_INVALID);
    return APP_FLOW_STEP_FAIL;
  }
  if (environment.oxygen_x10 < EXIT_PREP_OXYGEN_LOW_X10)
  {
    return jump_to_step(ctx, 90U);
  }
  if (environment.oxygen_x10 > EXIT_PREP_OXYGEN_HIGH_X10)
  {
    return jump_to_step(ctx, 100U);
  }
  return jump_to_step(ctx, 110U);
}

static AppFlowStepResult OxygenLow(AppFlowContext *ctx, const AppFlowStep *step)
{
  if (action_begin(step) != 0U)
  {
    if ((AutoDevice_SetOutput(AUTO_OUTPUT_OXYGEN_MACHINE, 1U) != HAL_OK) ||
        (AutoDevice_SetOutput(AUTO_OUTPUT_EXHAUST_FAN, 0U) != HAL_OK) ||
        (set_all_fans(EXIT_PREP_FAN_OXYGEN_PERCENT) != HAL_OK))
    {
      return APP_FLOW_STEP_FAIL;
    }
  }
  return (action_time_reached(EXIT_PREP_RECHECK_MS) != 0U) ?
         jump_to_step(ctx, 10U) : APP_FLOW_STEP_RUNNING;
}

static AppFlowStepResult OxygenHigh(AppFlowContext *ctx, const AppFlowStep *step)
{
  if (action_begin(step) != 0U)
  {
    if ((AutoDevice_SetOutput(AUTO_OUTPUT_OXYGEN_MACHINE, 0U) != HAL_OK) ||
        (set_all_heaters(0U) != HAL_OK))
    {
      return APP_FLOW_STEP_FAIL;
    }
    heaters_enabled = 0U;
    if ((set_all_fans(EXIT_PREP_FAN_VENT_PERCENT) != HAL_OK) ||
        (AutoDevice_SetOutput(AUTO_OUTPUT_EXHAUST_FAN, 1U) != HAL_OK))
    {
      return APP_FLOW_STEP_FAIL;
    }
  }
  return (action_time_reached(EXIT_PREP_RECHECK_MS) != 0U) ?
         jump_to_step(ctx, 10U) : APP_FLOW_STEP_RUNNING;
}

static AppFlowStepResult OxygenNormal(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)step;
  if (AutoDevice_SetOutput(AUTO_OUTPUT_OXYGEN_MACHINE, 0U) != HAL_OK)
  {
    return APP_FLOW_STEP_FAIL;
  }
  return jump_to_step(ctx, 120U);
}

static AppFlowStepResult AirFreshOn(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  return (AutoDevice_SetOutput(AUTO_OUTPUT_AIR_FRESHENER, 1U) == HAL_OK) ?
         APP_FLOW_STEP_DONE : APP_FLOW_STEP_RUNNING;
}

static AppFlowStepResult AirFreshOff(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  return (AutoDevice_SetOutput(AUTO_OUTPUT_AIR_FRESHENER, 0U) == HAL_OK) ?
         APP_FLOW_STEP_DONE : APP_FLOW_STEP_RUNNING;
}

static AppFlowStepResult ReportDone(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  AutoDevice_Report(AUTO_REPORT_EXIT_PREPARE_DONE);
  return APP_FLOW_STEP_DONE;
}

const AutoStageDefinition *AutoStage_ExitPrepare_Get(void)
{
  return &stage;
}
