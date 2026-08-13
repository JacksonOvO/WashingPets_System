#include "auto_stages.h"

#include "app_param.h"
#include "auto_device.h"
#include "main.h"
#include "rs485_config.h"
#include "task_comm485.h"

#include <string.h>

#define ENTRY_OUTDOOR_TEMP_TOLERANCE_X10 50
#define ENTRY_HUM_HIGH_X10             850U
#define ENTRY_OXYGEN_LOW_X10           195U
#define ENTRY_OXYGEN_HIGH_X10          235U

#define ENTRY_FAN_VENT_PERCENT         100U
#define ENTRY_FAN_HEAT_PERCENT         100U
#define ENTRY_FAN_OXYGEN_PERCENT       40U
#define ENTRY_HEATER_PERCENT           100U

#define ENTRY_RECHECK_MS               1000U
#define ENTRY_COOL_RECHECK_MS          5000U
#define ENTRY_FAN_PREHEAT_MS           2000U
#define ENTRY_FAN_COOLDOWN_MS          10000U
#define ENTRY_AIR_FRESH_MS             5000U
#define ENTRY_ENV_TOTAL_TIMEOUT_MS     1800000U
#define ENTRY_PIPE_DRAIN_TEMP_DIFF_X10 20
#define ENTRY_PIPE_DRAIN_FIXED_MS      15000U
#define ENTRY_PIPE_DRAIN_PRESSURE_X10  50U

static AppFlowStepResult Action_CheckPipeTankTempBeforeDrain(AppFlowContext *ctx, const AppFlowStep *step);
static AppFlowStepResult Action_PipeDrainOpen(AppFlowContext *ctx, const AppFlowStep *step);
static AppFlowStepResult Action_PipeDrainPumpOn(AppFlowContext *ctx, const AppFlowStep *step);
static AppFlowStepResult Action_WaitPipeMatchTankTemp(AppFlowContext *ctx, const AppFlowStep *step);
static AppFlowStepResult Action_PipeDrainStop(AppFlowContext *ctx, const AppFlowStep *step);
static AppFlowStepResult Action_WaitEnvironmentData(AppFlowContext *ctx, const AppFlowStep *step);
static AppFlowStepResult Action_CheckTempHumidity(AppFlowContext *ctx, const AppFlowStep *step);
static AppFlowStepResult Action_CoolAndDehumidify(AppFlowContext *ctx, const AppFlowStep *step);
static AppFlowStepResult Action_HeatAndDehumidify(AppFlowContext *ctx, const AppFlowStep *step);
static AppFlowStepResult Action_CoolTemperature(AppFlowContext *ctx, const AppFlowStep *step);
static AppFlowStepResult Action_HeatTemperature(AppFlowContext *ctx, const AppFlowStep *step);
static AppFlowStepResult Action_StopTempHumidity(AppFlowContext *ctx, const AppFlowStep *step);
static AppFlowStepResult Action_CheckOxygen(AppFlowContext *ctx, const AppFlowStep *step);
static AppFlowStepResult Action_OxygenLow(AppFlowContext *ctx, const AppFlowStep *step);
static AppFlowStepResult Action_OxygenHigh(AppFlowContext *ctx, const AppFlowStep *step);
static AppFlowStepResult Action_OxygenNormal(AppFlowContext *ctx, const AppFlowStep *step);
static AppFlowStepResult Action_AirFreshOn(AppFlowContext *ctx, const AppFlowStep *step);
static AppFlowStepResult Action_AirFreshOff(AppFlowContext *ctx, const AppFlowStep *step);
static AppFlowStepResult Action_ReportReady(AppFlowContext *ctx, const AppFlowStep *step);
static void EntryEnvInit(void);
static void EntryEnvReset(void);
static HAL_StatusTypeDef EntryEnvResume(uint16_t step_id);

static const AppFlowStep steps[] = {
  AUTO_STEP(5, "CHECK_PIPE_TANK_TEMP", 0U, 0U, Action_CheckPipeTankTempBeforeDrain, "先判断管路水温是否已接近水箱温度"),
  AUTO_STEP(10, "PIPE_DRAIN_OPEN", 0U, 0U, Action_PipeDrainOpen, "打开淋浴阀准备排出管路冷水"),
  AUTO_STEP(20, "PIPE_DRAIN_START_PUMP", 0U, 0U, Action_PipeDrainPumpOn, "启动增压泵排出管路冷水"),
  AUTO_STEP(30, "WAIT_PIPE_MATCH_TANK_TEMP", 0U, 500U, Action_WaitPipeMatchTankTemp, "等待管路水温接近当前水箱温度"),
  AUTO_STEP(40, "PIPE_DRAIN_STOP", 0U, 0U, Action_PipeDrainStop, "停止排冷水并关闭淋浴阀"),
  AUTO_STEP(50, "WAIT_ENV_DATA", 0U, 0U, Action_WaitEnvironmentData, "等待氧气和舱内温湿度数据有效"),
  AUTO_STEP(60, "CHECK_TEMP_HUMIDITY", 0U, 0U, Action_CheckTempHumidity, "根据舱内温湿度跳转到对应调节步骤"),
  AUTO_STEP(70, "TEMP_HIGH_HUM_HIGH", 0U, 0U, Action_CoolAndDehumidify, "温度高且湿度高时通风除湿"),
  AUTO_STEP(80, "TEMP_LOW_HUM_HIGH", 0U, 0U, Action_HeatAndDehumidify, "温度低且湿度高时加热除湿"),
  AUTO_STEP(90, "TEMP_HIGH_HUM_OK", 0U, 0U, Action_CoolTemperature, "湿度达标但温度高时通风降温"),
  AUTO_STEP(100, "TEMP_LOW_HUM_OK", 0U, 0U, Action_HeatTemperature, "湿度达标但温度低时加热升温"),
  AUTO_STEP(110, "STOP_TEMP_HUM_CTRL", 0U, 0U, Action_StopTempHumidity, "温湿度达标后关闭调温设备"),
  AUTO_STEP(120, "CHECK_OXYGEN", 0U, 0U, Action_CheckOxygen, "判断氧气浓度"),
  AUTO_STEP(130, "OXYGEN_LOW_CTRL", 0U, 0U, Action_OxygenLow, "氧气浓度低时打开制氧机和风机"),
  AUTO_STEP(140, "OXYGEN_HIGH_CTRL", 0U, 0U, Action_OxygenHigh, "氧气浓度高时通风换气"),
  AUTO_STEP(150, "STOP_OXYGEN_CTRL", 0U, 0U, Action_OxygenNormal, "氧气达标后关闭制氧机"),
  AUTO_STEP(160, "AIR_FRESH_ON", 0U, ENTRY_AIR_FRESH_MS, Action_AirFreshOn, "打开空气清新剂喷雾"),
  AUTO_STEP(170, "AIR_FRESH_OFF", 0U, 0U, Action_AirFreshOff, "关闭空气清新剂喷雾"),
  AUTO_STEP(180, "REPORT_ENV_READY", 0U, 2000U, Action_ReportReady, "入舱环境准备完成"),
};

static const AutoStageDefinition stage = {
  "EnterPrepare",
  steps,
  (uint16_t)(sizeof(steps) / sizeof(steps[0])),
  EntryEnvInit,
  EntryEnvReset,
  EntryEnvResume
};

static TaskComm485EnvironmentData environment;
static uint16_t active_action_step;
static uint32_t active_action_tick;
static uint8_t heaters_enabled;
static uint8_t cooldown_required;
static uint32_t stage_start_tick;
static uint32_t pipe_drain_start_tick;

static void EntryEnvReset(void)
{
  memset(&environment, 0, sizeof(environment));
  active_action_step = 0U;
  active_action_tick = 0U;
  heaters_enabled = 0U;
  cooldown_required = 0U;
  stage_start_tick = 0U;
  pipe_drain_start_tick = 0U;
}

static HAL_StatusTypeDef EntryEnvResume(uint16_t step_id)
{
  (void)step_id;
  active_action_step = 0U;
  return HAL_OK;
}

static void EntryEnvInit(void)
{
  EntryEnvReset();
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

static int16_t abs_x10(int16_t value)
{
  return (value < 0) ? (int16_t)(-value) : value;
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

static void stop_pipe_drain_outputs(void)
{
  AutoDevice_SetAllShowerValves(0U);
  (void)AutoDevice_SetBoosterPumpRunning(0U);
}

static AppFlowStepResult Action_CheckPipeTankTempBeforeDrain(AppFlowContext *ctx, const AppFlowStep *step)
{
  AutoSensorData data;
  int16_t diff_x10;

  (void)step;
  (void)AutoDevice_GetSensorData(&data);

  if ((data.pipe_temperature_valid == 0U) ||
      (data.tank_temperature_valid == 0U))
  {
    return APP_FLOW_STEP_DONE;
  }

  diff_x10 = abs_x10((int16_t)(data.pipe_temperature_x10 - data.tank_temperature_x10));
  if (diff_x10 <= ENTRY_PIPE_DRAIN_TEMP_DIFF_X10)
  {
    stop_pipe_drain_outputs();
    return jump_to_step(ctx, 50U);
  }

  return APP_FLOW_STEP_DONE;
}

static AppFlowStepResult Action_PipeDrainOpen(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;

  if (pipe_drain_start_tick == 0U)
  {
    pipe_drain_start_tick = AutoDevice_GetProcessTick();
  }

  AutoDevice_SetAllShowerValves(1U);
  return APP_FLOW_STEP_DONE;
}

static AppFlowStepResult Action_PipeDrainPumpOn(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;

  (void)AutoDevice_SetBoosterPumpPercent(ENTRY_PIPE_DRAIN_PRESSURE_X10);
  (void)AutoDevice_SetBoosterPumpRunning(1U);
  return APP_FLOW_STEP_DONE;
}

static AppFlowStepResult Action_WaitPipeMatchTankTemp(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)step;

  if ((pipe_drain_start_tick != 0U) &&
      ((AutoDevice_GetProcessTick() - pipe_drain_start_tick) >= ENTRY_PIPE_DRAIN_FIXED_MS))
  {
    return jump_to_step(ctx, 40U);
  }

  return APP_FLOW_STEP_RUNNING;
}

static AppFlowStepResult Action_PipeDrainStop(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  stop_pipe_drain_outputs();
  return APP_FLOW_STEP_DONE;
}

static AppFlowStepResult Action_WaitEnvironmentData(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)step;
  active_action_step = 0U;

  if ((AutoDevice_GetProcessTick() - stage_start_tick) >= ENTRY_ENV_TOTAL_TIMEOUT_MS)
  {
    AutoDevice_SetFault(AUTO_FAULT_PROCESS_ABORTED);
    return APP_FLOW_STEP_FAIL;
  }

  if ((TaskComm485_GetEnvironmentData(&environment) != 0U) &&
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

static AppFlowStepResult Action_CheckTempHumidity(AppFlowContext *ctx, const AppFlowStep *step)
{
  int16_t temperature;
  int16_t outdoor_temperature;
  int16_t temperature_low;
  int16_t temperature_high;
  uint16_t humidity;

  (void)step;
  if ((TaskComm485_GetEnvironmentData(&environment) == 0U) ||
      (environment.valid == 0U) ||
      (environment.incab_valid == 0U))
  {
    AutoDevice_SetFault(AUTO_FAULT_CABIN_TEMP_SENSOR_INVALID);
    return APP_FLOW_STEP_FAIL;
  }

  temperature = environment.incab_temperature_x10;
  outdoor_temperature = environment.temperature_x10;
  temperature_low = (int16_t)(outdoor_temperature - ENTRY_OUTDOOR_TEMP_TOLERANCE_X10);
  temperature_high = (int16_t)(outdoor_temperature + ENTRY_OUTDOOR_TEMP_TOLERANCE_X10);
  humidity = environment.incab_humidity_x10;

  if (humidity > ENTRY_HUM_HIGH_X10)
  {
    return (temperature < temperature_low) ?
           jump_to_step(ctx, 80U) : jump_to_step(ctx, 70U);
  }
  if (temperature > temperature_high)
  {
    return jump_to_step(ctx, 90U);
  }
  if (temperature < temperature_low)
  {
    return jump_to_step(ctx, 100U);
  }
  return jump_to_step(ctx, 110U);
}

static AppFlowStepResult Action_CoolAndDehumidify(AppFlowContext *ctx, const AppFlowStep *step)
{
  if (action_begin(step) != 0U)
  {
    if (set_all_heaters(0U) != HAL_OK)
    {
      return APP_FLOW_STEP_FAIL;
    }
    heaters_enabled = 0U;
    if ((set_all_fans(ENTRY_FAN_VENT_PERCENT) != HAL_OK) ||
        (AutoDevice_SetOutput(AUTO_OUTPUT_EXHAUST_FAN, 1U) != HAL_OK))
    {
      return APP_FLOW_STEP_FAIL;
    }
  }

  return (action_time_reached(ENTRY_COOL_RECHECK_MS) != 0U) ?
         jump_to_step(ctx, 50U) : APP_FLOW_STEP_RUNNING;
}

static AppFlowStepResult Action_HeatAndDehumidify(AppFlowContext *ctx, const AppFlowStep *step)
{
  if (action_begin(step) != 0U)
  {
    if ((set_all_fans(ENTRY_FAN_HEAT_PERCENT) != HAL_OK) ||
        (AutoDevice_SetOutput(AUTO_OUTPUT_EXHAUST_FAN, 1U) != HAL_OK))
    {
      return APP_FLOW_STEP_FAIL;
    }
  }
  if (heaters_enabled != 0U)
  {
    return (action_time_reached(ENTRY_RECHECK_MS) != 0U) ?
           jump_to_step(ctx, 50U) : APP_FLOW_STEP_RUNNING;
  }
  if (action_time_reached(ENTRY_FAN_PREHEAT_MS) != 0U)
  {
    if (set_all_heaters(ENTRY_HEATER_PERCENT) != HAL_OK)
    {
      return APP_FLOW_STEP_FAIL;
    }
    heaters_enabled = 1U;
    return jump_to_step(ctx, 50U);
  }
  return APP_FLOW_STEP_RUNNING;
}

static AppFlowStepResult Action_CoolTemperature(AppFlowContext *ctx, const AppFlowStep *step)
{
  return Action_CoolAndDehumidify(ctx, step);
}

static AppFlowStepResult Action_HeatTemperature(AppFlowContext *ctx, const AppFlowStep *step)
{
  if (action_begin(step) != 0U)
  {
    if ((set_all_fans(ENTRY_FAN_HEAT_PERCENT) != HAL_OK) ||
        (AutoDevice_SetOutput(AUTO_OUTPUT_EXHAUST_FAN, 0U) != HAL_OK))
    {
      return APP_FLOW_STEP_FAIL;
    }
  }
  if (heaters_enabled != 0U)
  {
    return (action_time_reached(ENTRY_RECHECK_MS) != 0U) ?
           jump_to_step(ctx, 50U) : APP_FLOW_STEP_RUNNING;
  }
  if (action_time_reached(ENTRY_FAN_PREHEAT_MS) != 0U)
  {
    if (set_all_heaters(ENTRY_HEATER_PERCENT) != HAL_OK)
    {
      return APP_FLOW_STEP_FAIL;
    }
    heaters_enabled = 1U;
    return jump_to_step(ctx, 50U);
  }
  return APP_FLOW_STEP_RUNNING;
}

static AppFlowStepResult Action_StopTempHumidity(AppFlowContext *ctx, const AppFlowStep *step)
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
      (action_time_reached(ENTRY_FAN_COOLDOWN_MS) == 0U))
  {
    return APP_FLOW_STEP_RUNNING;
  }

  if (set_all_fans(0U) != HAL_OK)
  {
    return APP_FLOW_STEP_FAIL;
  }
  cooldown_required = 0U;
  return jump_to_step(ctx, 120U);
}

static AppFlowStepResult Action_CheckOxygen(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)step;
  if (TaskComm485_GetEnvironmentData(&environment) == 0U)
  {
    AutoDevice_SetFault(AUTO_FAULT_OXYGEN_SENSOR_INVALID);
    return APP_FLOW_STEP_FAIL;
  }
  if (environment.oxygen_x10 < ENTRY_OXYGEN_LOW_X10)
  {
    return jump_to_step(ctx, 130U);
  }
  if (environment.oxygen_x10 > ENTRY_OXYGEN_HIGH_X10)
  {
    return jump_to_step(ctx, 140U);
  }
  return jump_to_step(ctx, 150U);
}

static AppFlowStepResult Action_OxygenLow(AppFlowContext *ctx, const AppFlowStep *step)
{
  if (action_begin(step) != 0U)
  {
    if ((AutoDevice_SetOutput(AUTO_OUTPUT_OXYGEN_MACHINE, 1U) != HAL_OK) ||
        (AutoDevice_SetOutput(AUTO_OUTPUT_EXHAUST_FAN, 0U) != HAL_OK) ||
        (set_all_fans(ENTRY_FAN_OXYGEN_PERCENT) != HAL_OK))
    {
      return APP_FLOW_STEP_FAIL;
    }
  }

  return (action_time_reached(ENTRY_RECHECK_MS) != 0U) ?
         jump_to_step(ctx, 50U) : APP_FLOW_STEP_RUNNING;
}

static AppFlowStepResult Action_OxygenHigh(AppFlowContext *ctx, const AppFlowStep *step)
{
  if (action_begin(step) != 0U)
  {
    if ((AutoDevice_SetOutput(AUTO_OUTPUT_OXYGEN_MACHINE, 0U) != HAL_OK) ||
        (set_all_heaters(0U) != HAL_OK))
    {
      return APP_FLOW_STEP_FAIL;
    }
    heaters_enabled = 0U;
    if ((set_all_fans(ENTRY_FAN_VENT_PERCENT) != HAL_OK) ||
        (AutoDevice_SetOutput(AUTO_OUTPUT_EXHAUST_FAN, 1U) != HAL_OK))
    {
      return APP_FLOW_STEP_FAIL;
    }
  }

  return (action_time_reached(ENTRY_RECHECK_MS) != 0U) ?
         jump_to_step(ctx, 50U) : APP_FLOW_STEP_RUNNING;
}

static AppFlowStepResult Action_OxygenNormal(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)step;
  if (AutoDevice_SetOutput(AUTO_OUTPUT_OXYGEN_MACHINE, 0U) != HAL_OK)
  {
    return APP_FLOW_STEP_FAIL;
  }
  return jump_to_step(ctx, 160U);
}

static AppFlowStepResult Action_AirFreshOn(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  return (AutoDevice_SetOutput(AUTO_OUTPUT_AIR_FRESHENER, 1U) == HAL_OK) ?
         APP_FLOW_STEP_DONE : APP_FLOW_STEP_RUNNING;
}

static AppFlowStepResult Action_AirFreshOff(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  return (AutoDevice_SetOutput(AUTO_OUTPUT_AIR_FRESHENER, 0U) == HAL_OK) ?
         APP_FLOW_STEP_DONE : APP_FLOW_STEP_RUNNING;
}

static AppFlowStepResult Action_ReportReady(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  return APP_FLOW_STEP_DONE;
}

const AutoStageDefinition *AutoStage_EntryEnv_Get(void)
{
  return &stage;
}
