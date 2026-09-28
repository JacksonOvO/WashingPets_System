#include "task_comm485.h"

#include "app_param.h"
#include "auto_device.h"
#include "cmsis_os.h"
#include "main.h"
#include "oxygen_sensor.h"
#include "rs485_config.h"
#include "temperature_module.h"
#include "thyristor.h"
#include "task_watchdog.h"
#include "task_self_test.h"

#include <string.h>

#define COMM485_TASK_STACK             512U
#define COMM485_TASK_PRIORITY          osPriorityNormal
#define COMM485_QUEUE_LENGTH          32U
#define COMM485_SENSOR_PERIOD_MS       1000U
#define COMM485_SENSOR_SLOT_PERIOD_MS  250U
#define COMM485_SELF_TEST_PRESSURE_PERIOD_MS 100U
#define COMM485_SENSOR_STALE_MS        5000U
#define COMM485_PROBE_PERIOD_MS        250U
#define COMM485_PROBE_GAP_MS           200U
#define COMM485_MAX_COMMANDS_PER_LOOP     1U
#define COMM485_COMMAND_RETRY_MAX         2U
#define COMM485_RETRY_BACKOFF_BASE_MS    250U
#define COMM485_RETRY_BACKOFF_MAX_MS    2000U
#define COMM485_SAFE_STOP_RETRY_DELAY_MS  20U
#define COMM485_PROP_VALVE_FAILURE_LIMIT  (COMM485_COMMAND_RETRY_MAX + 1U)
#define COMM485_DEBUG_ENABLE           1U
#define COMM485_PROP_VALVE_REG_ANGLE   0x0001U
#define COMM485_PROP_VALVE_MAX_ANGLE   90U

typedef enum
{
  COMM485_CMD_SET_PERCENT = 1,
  COMM485_CMD_SET_PROP_VALVE_ANGLE
} Comm485CommandId;

typedef struct
{
  Comm485CommandId id;
  uint8_t station;
  uint8_t percent;
  uint8_t angle;
  uint8_t enabled;
  uint8_t retry_count;
} Comm485Command;

extern UART_HandleTypeDef huart3;

static osThreadId_t comm485_task_handle;
static osMessageQueueId_t comm485_queue;
static osMutexId_t environment_mutex;
#if (APP_DRY_RUN_ENABLE == 0U)
static TaskComm485EnvironmentData environment_data;
#endif
static uint8_t current_percent;
static uint32_t sensor_poll_tick;
static uint32_t temperature_module_update_tick;
static uint8_t sensor_poll_slot;
static volatile uint8_t safe_stop_requested;
static volatile uint32_t nonzero_inhibit_reasons;
static volatile uint8_t last_percent_by_station[248];
static volatile uint8_t pending_percent_by_station[248];
static volatile uint8_t pending_percent_valid_by_station[248];
static volatile uint8_t last_proportional_valve_angle;
static volatile uint8_t pending_proportional_valve_angle;
static volatile uint8_t pending_proportional_valve_angle_valid;
static volatile uint8_t last_booster_pump_percent;
static volatile uint8_t booster_pump_feedback_pressure_x10;
static volatile uint8_t booster_pump_feedback_valid;
static volatile uint32_t booster_pump_feedback_tick;
static volatile uint8_t pending_booster_pump_percent;
static volatile uint8_t pending_booster_pump_percent_valid;
static volatile uint8_t last_booster_pump_running;
static volatile uint8_t pending_booster_pump_running;
static volatile uint8_t pending_booster_pump_running_valid;
static volatile uint8_t rs485_actuator_mode;
static uint8_t station_failure_count[248];
static uint32_t station_next_retry_tick[248];
static volatile uint32_t station_success_count[248];
static volatile uint8_t self_test_pressure_priority;
static uint8_t proportional_valve_failure_count;

static const osThreadAttr_t comm485_task_attributes = {
  .name = "comm485",
  .stack_size = COMM485_TASK_STACK * 4U,
  .priority = COMM485_TASK_PRIORITY,
};

static void health_update_prop_valve(uint8_t ok);

#if (APP_DRY_RUN_ENABLE != 0U)
static void health_update_prop_valve(uint8_t ok)
{
  (void)ok;
}

static void dry_run_fill_environment(TaskComm485EnvironmentData *data)
{
  uint32_t now = HAL_GetTick();

  memset(data, 0, sizeof(*data));
  data->temperature_x10 = 300;
  data->humidity_x10 = 550U;
  data->oxygen_x10 = 209U;
  data->update_tick = now;
  data->valid = 1U;
  data->incab_temperature_x10 = 300;
  data->incab_humidity_x10 = 550U;
  data->incab_update_tick = now;
  data->incab_valid = 1U;
  data->ozone_x100 = 0U;
  data->ozone_update_tick = now;
  data->ozone_valid = 1U;
}

static void dry_run_zero_actuator_cache(void)
{
  uint32_t now = HAL_GetTick();

  memset((void *)last_percent_by_station, 0, sizeof(last_percent_by_station));
  memset((void *)pending_percent_by_station, 0, sizeof(pending_percent_by_station));
  memset((void *)pending_percent_valid_by_station, 0, sizeof(pending_percent_valid_by_station));
  last_proportional_valve_angle = 0U;
  pending_proportional_valve_angle = 0U;
  pending_proportional_valve_angle_valid = 0U;
  last_booster_pump_percent = 0U;
  booster_pump_feedback_pressure_x10 = 0U;
  booster_pump_feedback_valid = 1U;
  booster_pump_feedback_tick = now;
  pending_booster_pump_percent = 0U;
  pending_booster_pump_percent_valid = 0U;
  last_booster_pump_running = 0U;
  pending_booster_pump_running = 0U;
  pending_booster_pump_running_valid = 0U;
}
#endif

static uint32_t inhibit_reasons_read(void)
{
  uint32_t primask = __get_PRIMASK();
  uint32_t reasons;

  __disable_irq();
  reasons = nonzero_inhibit_reasons;
  if (primask == 0U)
  {
    __enable_irq();
  }
  return reasons;
}

void TaskComm485_InhibitNonzero(uint32_t reasons)
{
  uint32_t primask = __get_PRIMASK();

  __disable_irq();
  nonzero_inhibit_reasons |= reasons;
  if (primask == 0U)
  {
    __enable_irq();
  }
}

void TaskComm485_ReleaseNonzero(uint32_t reasons)
{
  uint32_t primask = __get_PRIMASK();

  __disable_irq();
  nonzero_inhibit_reasons &= ~reasons;
  if (primask == 0U)
  {
    __enable_irq();
  }
}

uint32_t TaskComm485_GetInhibitReasons(void)
{
#if (APP_DRY_RUN_ENABLE != 0U)
  return 0U;
#else
  return inhibit_reasons_read();
#endif
}

static uint8_t nonzero_output_is_inhibited(void)
{
  return ((inhibit_reasons_read() != 0U) ||
          (AutoDevice_IsEmergencyStopped() != 0U)) ? 1U : 0U;
}

void Thyristor_SetTransmitMode(void)
{
  /* RE/DE is driven by the UART3_TX hardware line on this board. */
}

void Thyristor_SetReceiveMode(void)
{
  /* RE/DE is driven by the UART3_TX hardware line on this board. */
}

void OxygenSensor_SetTransmitMode(void)
{
  /* RE/DE is driven by the UART3_TX hardware line on this board. */
}

void OxygenSensor_SetReceiveMode(void)
{
  /* RE/DE is driven by the UART3_TX hardware line on this board. */
}

void TemperatureModule_SetTransmitMode(void)
{
  /* RE/DE is driven by the UART3_TX hardware line on this board. */
}

void TemperatureModule_SetReceiveMode(void)
{
  /* RE/DE is driven by the UART3_TX hardware line on this board. */
}

static void uart2_print_hex_line(const uint8_t *prefix,
                                 uint16_t prefix_length,
                                 const uint8_t *data,
                                 uint16_t length)
{
#if (COMM485_DEBUG_ENABLE != 0U)
  static const char hex[] = "0123456789ABCDEF";
  static const uint8_t line_end[] = "\r\n";
  uint8_t encoded[3];
  uint16_t index;

  (void)UART2_Send(prefix, prefix_length);
  for (index = 0U; index < length; ++index)
  {
    encoded[0] = (uint8_t)' ';
    encoded[1] = (uint8_t)hex[data[index] >> 4U];
    encoded[2] = (uint8_t)hex[data[index] & 0x0FU];
    (void)UART2_Send(encoded, sizeof(encoded));
  }
  (void)UART2_Send(line_end, (uint16_t)(sizeof(line_end) - 1U));
#else
  (void)prefix;
  (void)prefix_length;
  (void)data;
  (void)length;
#endif
}

void Thyristor_OnTransmit(const uint8_t *data, uint16_t length)
{
  static const uint8_t prefix[] = "USART3 TX:";

  uart2_print_hex_line(prefix, (uint16_t)(sizeof(prefix) - 1U), data, length);
}

void Thyristor_OnReceive(const uint8_t *data, uint16_t length)
{
  static const uint8_t prefix[] = "USART3 RX:";

  uart2_print_hex_line(prefix, (uint16_t)(sizeof(prefix) - 1U), data, length);
}

void OxygenSensor_OnTransmit(const uint8_t *data, uint16_t length)
{
  static const uint8_t prefix[] = "OXYGEN TX:";

  uart2_print_hex_line(prefix, (uint16_t)(sizeof(prefix) - 1U), data, length);
}

void OxygenSensor_OnReceive(const uint8_t *data, uint16_t length)
{
  static const uint8_t prefix[] = "OXYGEN RX:";

  uart2_print_hex_line(prefix, (uint16_t)(sizeof(prefix) - 1U), data, length);
}

void TemperatureModule_OnTransmit(const uint8_t *data, uint16_t length)
{
  static const uint8_t prefix[] = "TEMP TX:";

  uart2_print_hex_line(prefix, (uint16_t)(sizeof(prefix) - 1U), data, length);
}

void TemperatureModule_OnReceive(const uint8_t *data, uint16_t length)
{
  static const uint8_t prefix[] = "TEMP RX:";

  uart2_print_hex_line(prefix, (uint16_t)(sizeof(prefix) - 1U), data, length);
}

static HAL_StatusTypeDef request_set_percent(uint8_t station, uint8_t percent,
                                             uint8_t force)
{
#if (APP_DRY_RUN_ENABLE != 0U)
  if ((station == 0U) || (station > 247U))
  {
    return HAL_ERROR;
  }
  if (percent > 100U)
  {
    percent = 100U;
  }
  last_percent_by_station[station] = percent;
  pending_percent_by_station[station] = percent;
  pending_percent_valid_by_station[station] = 0U;
  return HAL_OK;
#else
  Comm485Command command;
  osStatus_t status;

  if ((comm485_queue == NULL) || (station == 0U) || (station > 247U))
  {
    return HAL_ERROR;
  }

  if (percent > 100U)
  {
    percent = 100U;
  }
  if ((percent != 0U) && (nonzero_output_is_inhibited() != 0U))
  {
    return HAL_BUSY;
  }
  if ((force == 0U) &&
      ((last_percent_by_station[station] == percent) ||
       ((pending_percent_valid_by_station[station] != 0U) &&
        (pending_percent_by_station[station] == percent))))
  {
    return HAL_OK;
  }
  command.id = COMM485_CMD_SET_PERCENT;
  command.station = station;
  command.percent = percent;
  command.angle = 0U;
  command.enabled = 0U;
  command.retry_count = 0U;
  status = osMessageQueuePut(comm485_queue, &command, 0U, 0U);
  if (status == osOK)
  {
    pending_percent_by_station[station] = percent;
    pending_percent_valid_by_station[station] = 1U;
    return HAL_OK;
  }
  return HAL_BUSY;
#endif
}

HAL_StatusTypeDef TaskComm485_RequestSetPercent(uint8_t station, uint8_t percent)
{
  return request_set_percent(station, percent, 0U);
}

HAL_StatusTypeDef TaskComm485_ForceSetPercent(uint8_t station, uint8_t percent)
{
  return request_set_percent(station, percent, 1U);
}

uint8_t TaskComm485_GetLastPercent(uint8_t station)
{
  if (station > 247U)
  {
    return 0U;
  }
  return last_percent_by_station[station];
}

uint8_t TaskComm485_GetLastProportionalValveAngle(void)
{
  return last_proportional_valve_angle;
}

uint8_t TaskComm485_GetLastBoosterPumpPercent(void)
{
  return last_booster_pump_percent;
}

uint8_t TaskComm485_GetBoosterPumpFeedbackPressure(void)
{
  return 0U;
}

uint8_t TaskComm485_GetLastBoosterPumpRunning(void)
{
  return last_booster_pump_running;
}

uint8_t TaskComm485_GetBoosterPumpFeedback(uint8_t *pressure_x10)
{
  if (pressure_x10 != NULL)
  {
    *pressure_x10 = 0U;
  }
  return 0U;
}

uint32_t TaskComm485_GetStationSuccessCount(uint8_t station)
{
  return (station <= 247U) ? station_success_count[station] : 0U;
}

void TaskComm485_SetSelfTestPressurePriority(uint8_t enabled)
{
  self_test_pressure_priority = 0U;
  (void)enabled;
  sensor_poll_tick = 0U;
}

void TaskComm485_SetRs485ActuatorMode(uint8_t enabled)
{
  rs485_actuator_mode = (enabled != 0U) ? 1U : 0U;
  if (rs485_actuator_mode == 0U)
  {
    last_percent_by_station[RS485_STATION_TOP_EXHAUST_FAN] = 0U;
    pending_percent_valid_by_station[RS485_STATION_TOP_EXHAUST_FAN] = 0U;
  }
}

uint8_t TaskComm485_IsRs485ActuatorMode(void)
{
  return rs485_actuator_mode;
}

HAL_StatusTypeDef TaskComm485_RequestSetProportionalValveAngle(uint8_t angle)
{
#if (APP_DRY_RUN_ENABLE != 0U)
  if (angle > COMM485_PROP_VALVE_MAX_ANGLE)
  {
    angle = COMM485_PROP_VALVE_MAX_ANGLE;
  }
  last_proportional_valve_angle = angle;
  pending_proportional_valve_angle = angle;
  pending_proportional_valve_angle_valid = 0U;
  return HAL_OK;
#else
  Comm485Command command;

  if (comm485_queue == NULL)
  {
    return HAL_ERROR;
  }
  if (angle > COMM485_PROP_VALVE_MAX_ANGLE)
  {
    angle = COMM485_PROP_VALVE_MAX_ANGLE;
  }
  if ((angle != 0U) && (nonzero_output_is_inhibited() != 0U))
  {
    return HAL_BUSY;
  }
  /*
   * 水箱高液位联锁（防溢流）已移回自动调温路径（task_water_tank.c 的
   * request_valve_angle）：TANK_HIGH 为反逻辑，高液位持续10秒后自动
   * 流程不再开混合阀。此处不再检查液位，工作台手动控制、单阶段自检在
   * 任意液位下都能驱动比例阀，便于验证阀体动作。
   */
  if ((last_proportional_valve_angle == angle) ||
      ((pending_proportional_valve_angle_valid != 0U) &&
       (pending_proportional_valve_angle == angle)))
  {
    return HAL_OK;
  }

  command.id = COMM485_CMD_SET_PROP_VALVE_ANGLE;
  command.station = RS485_STATION_PROPORTIONAL_VALVE;
  command.percent = 0U;
  command.angle = angle;
  command.enabled = 0U;
  command.retry_count = 0U;
  if (osMessageQueuePut(comm485_queue, &command, 0U, 0U) == osOK)
  {
    pending_proportional_valve_angle = angle;
    pending_proportional_valve_angle_valid = 1U;
    return HAL_OK;
  }
  return HAL_BUSY;
#endif
}

static HAL_StatusTypeDef request_set_booster_pump_percent(uint8_t percent,
                                                          uint8_t force)
{
  (void)force;
  if (percent > 100U)
  {
    percent = 100U;
  }
  last_booster_pump_percent = percent;
  pending_booster_pump_percent = percent;
  pending_booster_pump_percent_valid = 0U;
  return HAL_OK;
}

HAL_StatusTypeDef TaskComm485_RequestSetBoosterPumpPercent(uint8_t percent)
{
  return request_set_booster_pump_percent(percent, 0U);
}

HAL_StatusTypeDef TaskComm485_ForceSetBoosterPumpPercent(uint8_t percent)
{
  return request_set_booster_pump_percent(percent, 1U);
}

static HAL_StatusTypeDef request_set_booster_pump_running(uint8_t enabled,
                                                          uint8_t force)
{
  (void)force;
  last_booster_pump_running = (enabled != 0U) ? 1U : 0U;
  pending_booster_pump_running = last_booster_pump_running;
  pending_booster_pump_running_valid = 0U;
  return HAL_OK;
}

HAL_StatusTypeDef TaskComm485_RequestSetBoosterPumpRunning(uint8_t enabled)
{
  return request_set_booster_pump_running(enabled, 0U);
}

HAL_StatusTypeDef TaskComm485_ForceSetBoosterPumpRunning(uint8_t enabled)
{
  return request_set_booster_pump_running(enabled, 1U);
}

void TaskComm485_RequestSafeStop(void)
{
#if (APP_DRY_RUN_ENABLE != 0U)
  dry_run_zero_actuator_cache();
  safe_stop_requested = 0U;
#else
  uint32_t primask = __get_PRIMASK();

  __disable_irq();
  if ((nonzero_inhibit_reasons & COMM485_INHIBIT_SAFE_STOP) == 0U)
  {
    nonzero_inhibit_reasons |= COMM485_INHIBIT_SAFE_STOP;
    safe_stop_requested = 1U;
  }
  if (primask == 0U)
  {
    __enable_irq();
  }
#endif
}

uint8_t TaskComm485_GetEnvironmentData(TaskComm485EnvironmentData *data)
{
  if (data == NULL)
  {
    return 0U;
  }

#if (APP_DRY_RUN_ENABLE != 0U)
  dry_run_fill_environment(data);
  return 1U;
#else
  uint32_t now;

  if (environment_mutex == NULL)
  {
    return 0U;
  }

  if (osMutexAcquire(environment_mutex, osWaitForever) != osOK)
  {
    return 0U;
  }

  now = HAL_GetTick();
  if ((environment_data.valid != 0U) &&
      ((now - environment_data.update_tick) > COMM485_SENSOR_STALE_MS))
  {
    environment_data.valid = 0U;
  }
  if ((environment_data.incab_valid != 0U) &&
      ((now - environment_data.incab_update_tick) > COMM485_SENSOR_STALE_MS))
  {
    environment_data.incab_valid = 0U;
  }
  if ((environment_data.ozone_valid != 0U) &&
      ((now - environment_data.ozone_update_tick) > COMM485_SENSOR_STALE_MS))
  {
    environment_data.ozone_valid = 0U;
  }

  *data = environment_data;
  (void)osMutexRelease(environment_mutex);

  if (data->valid == 0U)
  {
    return 0U;
  }
  return 1U;
#endif
}

static HAL_StatusTypeDef apply_percent(uint8_t station, uint8_t percent)
{
  static const uint8_t ok_message[] = " SET OK\r\n";
  static const uint8_t error_message[] = " SET ERROR\r\n";
  const AppParam_t *param = AppParam_Get();

  if (Thyristor_SetVoltagePercent(station, percent) == THYRISTOR_OK)
  {
    /* 实体加减按键只跟踪参数区中默认可控硅的当前百分比。 */
    if (station == param->thyristor_station)
    {
      current_percent = percent;
    }
    last_percent_by_station[station] = percent;
    station_success_count[station]++;
    if (pending_percent_by_station[station] == percent)
    {
      pending_percent_valid_by_station[station] = 0U;
    }
    (void)UART2_Send(ok_message, (uint16_t)(sizeof(ok_message) - 1U));
    return HAL_OK;
  }
  else
  {
    (void)UART2_Send(error_message, (uint16_t)(sizeof(error_message) - 1U));
  }
  return HAL_ERROR;
}

static HAL_StatusTypeDef apply_proportional_valve_angle(uint8_t angle)
{
  uint16_t value;
  ThyristorStatus status;

  if (angle > COMM485_PROP_VALVE_MAX_ANGLE)
  {
    angle = COMM485_PROP_VALVE_MAX_ANGLE;
  }
  value = (uint16_t)angle * 100U;
  status = Thyristor_WriteRegister(RS485_STATION_PROPORTIONAL_VALVE,
                                   COMM485_PROP_VALVE_REG_ANGLE,
                                   value);
  if (status == THYRISTOR_OK)
  {
    last_proportional_valve_angle = angle;
    if (pending_proportional_valve_angle == angle)
    {
      pending_proportional_valve_angle_valid = 0U;
    }
    health_update_prop_valve(1U);
    return HAL_OK;
  }
  health_update_prop_valve(0U);
  return HAL_ERROR;
}

static void apply_proportional_valve_safe_zero(void)
{
  uint8_t attempt;

  for (attempt = 0U; attempt < COMM485_PROP_VALVE_FAILURE_LIMIT; ++attempt)
  {
    if (apply_proportional_valve_angle(0U) == HAL_OK)
    {
      return;
    }
    TaskWatchdog_Heartbeat(TASK_WATCHDOG_COMM485);
    if ((attempt + 1U) < COMM485_PROP_VALVE_FAILURE_LIMIT)
    {
      osDelay(COMM485_SAFE_STOP_RETRY_DELAY_MS);
    }
  }
}

static void clear_pending_command(const Comm485Command *command)
{
  if (command->id == COMM485_CMD_SET_PERCENT)
  {
    if (pending_percent_by_station[command->station] == command->percent)
    {
      pending_percent_valid_by_station[command->station] = 0U;
    }
  }
  else if (command->id == COMM485_CMD_SET_PROP_VALVE_ANGLE)
  {
    if (pending_proportional_valve_angle == command->angle)
    {
      pending_proportional_valve_angle_valid = 0U;
    }
  }
}

static uint8_t command_is_nonzero(const Comm485Command *command)
{
  if (command->id == COMM485_CMD_SET_PERCENT)
  {
    return (command->percent != 0U) ? 1U : 0U;
  }
  if (command->id == COMM485_CMD_SET_PROP_VALVE_ANGLE)
  {
    return (command->angle != 0U) ? 1U : 0U;
  }
  return 0U;
}

static uint8_t command_is_superseded(const Comm485Command *command)
{
  if (command->id == COMM485_CMD_SET_PERCENT)
  {
    return ((pending_percent_valid_by_station[command->station] != 0U) &&
            (pending_percent_by_station[command->station] != command->percent)) ? 1U : 0U;
  }
  if (command->id == COMM485_CMD_SET_PROP_VALVE_ANGLE)
  {
    return ((pending_proportional_valve_angle_valid != 0U) &&
            (pending_proportional_valve_angle != command->angle)) ? 1U : 0U;
  }
  return 1U;
}

static AutoFaultCode station_fault_code(uint8_t station)
{
  switch (station)
  {
    case RS485_STATION_LEFT_FAN: return AUTO_FAULT_485_LEFT_FAN_COMM;
    case RS485_STATION_RIGHT_FAN: return AUTO_FAULT_485_RIGHT_FAN_COMM;
    case RS485_STATION_BOTTOM_FAN: return AUTO_FAULT_485_BOTTOM_FAN_COMM;
    case RS485_STATION_TOP_EXHAUST_FAN: return AUTO_FAULT_485_TOP_EXHAUST_COMM;
    case RS485_STATION_LEFT_HEATER: return AUTO_FAULT_485_LEFT_HEATER_COMM;
    case RS485_STATION_RIGHT_HEATER: return AUTO_FAULT_485_RIGHT_HEATER_COMM;
    case RS485_STATION_BOTTOM_HEATER: return AUTO_FAULT_485_BOTTOM_HEATER_COMM;
    case RS485_STATION_PROPORTIONAL_VALVE: return AUTO_FAULT_485_PROPORTIONAL_VALVE_COMM;
    default: return AUTO_FAULT_OUTPUT_COMMAND_FAILED;
  }
}

static void mark_station_success(uint8_t station)
{
  station_failure_count[station] = 0U;
  station_next_retry_tick[station] = 0U;
}

static void mark_station_failure(uint8_t station)
{
  uint8_t shift;
  uint32_t delay_ms;

  if (station_failure_count[station] < 0xFFU)
  {
    station_failure_count[station]++;
  }
  shift = (station_failure_count[station] > 4U) ? 3U :
          (uint8_t)(station_failure_count[station] - 1U);
  delay_ms = COMM485_RETRY_BACKOFF_BASE_MS << shift;
  if (delay_ms > COMM485_RETRY_BACKOFF_MAX_MS)
  {
    delay_ms = COMM485_RETRY_BACKOFF_MAX_MS;
  }
  station_next_retry_tick[station] = HAL_GetTick() + delay_ms;
}

static uint8_t station_retry_due(uint8_t station)
{
  uint32_t due = station_next_retry_tick[station];

  return ((due == 0U) || ((int32_t)(HAL_GetTick() - due) >= 0)) ? 1U : 0U;
}

static void report_dropped_command(const Comm485Command *command)
{
  if (TaskSelfTest_IsRunning() != 0U)
  {
    clear_pending_command(command);
    return;
  }
  AutoDevice_SetFault(station_fault_code(command->station));
  AutoDevice_SetStopRequested(1U);
  AutoDevice_Report(AUTO_REPORT_SENSOR_FAULT);
  TaskComm485_InhibitNonzero(COMM485_INHIBIT_FAULT);
  TaskComm485_RequestSafeStop();
}

static uint8_t retry_or_drop_command(Comm485Command *command)
{
  if (command->retry_count < COMM485_COMMAND_RETRY_MAX)
  {
    command->retry_count++;
    if (osMessageQueuePut(comm485_queue, command, 0U, 0U) == osOK)
    {
      return 0U;
    }
  }
  clear_pending_command(command);
  report_dropped_command(command);
  return 1U;
}

/* 按“先加热器、后风机、最后比例阀”的顺序把485执行器写成安全状态。 */
static void apply_all_actuators_zero(void)
{
  pending_percent_valid_by_station[RS485_STATION_LEFT_HEATER] = 0U;
  pending_percent_valid_by_station[RS485_STATION_RIGHT_HEATER] = 0U;
  pending_percent_valid_by_station[RS485_STATION_BOTTOM_HEATER] = 0U;
  pending_percent_valid_by_station[RS485_STATION_LEFT_FAN] = 0U;
  pending_percent_valid_by_station[RS485_STATION_RIGHT_FAN] = 0U;
  pending_percent_valid_by_station[RS485_STATION_BOTTOM_FAN] = 0U;
  apply_percent(RS485_STATION_LEFT_HEATER, 0U);
  TaskWatchdog_Heartbeat(TASK_WATCHDOG_COMM485);
  apply_percent(RS485_STATION_RIGHT_HEATER, 0U);
  TaskWatchdog_Heartbeat(TASK_WATCHDOG_COMM485);
  apply_percent(RS485_STATION_BOTTOM_HEATER, 0U);
  TaskWatchdog_Heartbeat(TASK_WATCHDOG_COMM485);
  apply_percent(RS485_STATION_LEFT_FAN, 0U);
  TaskWatchdog_Heartbeat(TASK_WATCHDOG_COMM485);
  apply_percent(RS485_STATION_RIGHT_FAN, 0U);
  TaskWatchdog_Heartbeat(TASK_WATCHDOG_COMM485);
  apply_percent(RS485_STATION_BOTTOM_FAN, 0U);
  TaskWatchdog_Heartbeat(TASK_WATCHDOG_COMM485);
#if (RS485_TOP_EXHAUST_PRESENT != 0U)
  if (rs485_actuator_mode != 0U)
  {
    pending_percent_valid_by_station[RS485_STATION_TOP_EXHAUST_FAN] = 0U;
    apply_percent(RS485_STATION_TOP_EXHAUST_FAN, 0U);
    TaskWatchdog_Heartbeat(TASK_WATCHDOG_COMM485);
  }
#endif
  last_booster_pump_running = 0U;
  last_booster_pump_percent = 0U;
  pending_booster_pump_running_valid = 0U;
  pending_booster_pump_percent_valid = 0U;
  pending_proportional_valve_angle_valid = 0U;
  apply_proportional_valve_safe_zero();
  TaskWatchdog_Heartbeat(TASK_WATCHDOG_COMM485);
}

/*
 * 每秒读取一次站号为 7 的氧气/温湿度传感器。
 * 读取结果保存在缓存中，其它线程只能通过
 * TaskComm485_GetEnvironmentData() 获取，不能直接访问 USART1。
 */
#if (APP_DRY_RUN_ENABLE == 0U)

/*
 * 健康巡检：跟踪传感器源与执行器从站在线状态。
 * 时间制判定：COMM485_HEALTH_FAULT_AFTER_MS 内没有一次成功读取即置对应源
 * 故障并经 AutoDevice 上报 A4；连续 2 次读取成功才确认恢复清位，
 * 防止不稳定总线上偶发成功读造成报警抖动。
 * bitmap_bits 对应协议 13.1 故障类别位。
 */
typedef enum
{
  COMM485_HEALTH_INLET_TEMP = 0,
  COMM485_HEALTH_TANK_TEMP,
  COMM485_HEALTH_PIPE_TEMP,
  COMM485_HEALTH_SPARE_TEMP,
  COMM485_HEALTH_OXYGEN,
  COMM485_HEALTH_INCAB_TRH,
  COMM485_HEALTH_OZONE,
  COMM485_HEALTH_PROP_VALVE,
  COMM485_HEALTH_LEFT_FAN,
  COMM485_HEALTH_RIGHT_FAN,
  COMM485_HEALTH_BOTTOM_FAN,
  COMM485_HEALTH_TOP_EXHAUST_FAN,
  COMM485_HEALTH_LEFT_HEATER,
  COMM485_HEALTH_RIGHT_HEATER,
  COMM485_HEALTH_BOTTOM_HEATER,
  COMM485_HEALTH_SOURCE_COUNT
} Comm485HealthSource;

typedef struct
{
  uint16_t fault_code;
  uint32_t bitmap_bits;
} Comm485HealthSourceInfo;

/* 时间制判定：该间隔内没有一次成功读取即判故障。
 * 取值必须大于巡检最长一轮周期的两倍，否则在线时会误报；
 * 以后从站数量增多导致一轮周期变长时要同步调大。
 * 当前执行器 8 站 × 250ms 一轮约 2s，两倍约 4s，
 * 取 5000ms 留出余量，容忍约两次偶发漏读不误报。 */
#define COMM485_HEALTH_FAULT_AFTER_MS 5000U
#define COMM485_HEALTH_RECOVER_COUNT  2U

static const Comm485HealthSourceInfo health_source_info[COMM485_HEALTH_SOURCE_COUNT] =
{
  { (uint16_t)AUTO_FAULT_INLET_TEMP_SENSOR_INVALID, (1UL << 18) },
  { (uint16_t)AUTO_FAULT_TANK_TEMP_SENSOR_INVALID, (1UL << 5) },
  { (uint16_t)AUTO_FAULT_PIPE_TEMP_SENSOR_INVALID, (1UL << 6) },
  { (uint16_t)AUTO_FAULT_SPARE_TEMP_SENSOR_INVALID, (1UL << 19) },
  { (uint16_t)AUTO_FAULT_OXYGEN_SENSOR_INVALID, (1UL << 11) },
  { (uint16_t)AUTO_FAULT_CABIN_TEMP_SENSOR_INVALID, (1UL << 12) },
  { (uint16_t)AUTO_FAULT_OZONE_SENSOR_INVALID, (1UL << 14) },
  { (uint16_t)AUTO_FAULT_485_PROPORTIONAL_VALVE_COMM, (1UL << 7) | (1UL << 14) },
  { (uint16_t)AUTO_FAULT_485_LEFT_FAN_COMM, (1UL << 14) },
  { (uint16_t)AUTO_FAULT_485_RIGHT_FAN_COMM, (1UL << 14) },
  { (uint16_t)AUTO_FAULT_485_BOTTOM_FAN_COMM, (1UL << 14) },
  { (uint16_t)AUTO_FAULT_485_TOP_EXHAUST_COMM, (1UL << 14) },
  { (uint16_t)AUTO_FAULT_485_LEFT_HEATER_COMM, (1UL << 14) },
  { (uint16_t)AUTO_FAULT_485_RIGHT_HEATER_COMM, (1UL << 14) },
  { (uint16_t)AUTO_FAULT_485_BOTTOM_HEATER_COMM, (1UL << 14) },
};

static uint32_t health_last_ok_tick[COMM485_HEALTH_SOURCE_COUNT];
static uint8_t health_faulted[COMM485_HEALTH_SOURCE_COUNT];
static uint8_t health_ok_count[COMM485_HEALTH_SOURCE_COUNT];
static volatile uint32_t health_fault_bitmap;
static uint8_t actuator_probe_index;
static uint32_t actuator_probe_tick;
static uint32_t bus_last_response_tick;

static void health_update_source(Comm485HealthSource source, uint8_t ok)
{
  const Comm485HealthSourceInfo *info = &health_source_info[source];

  if (ok != 0U)
  {
    bus_last_response_tick = HAL_GetTick();
    health_last_ok_tick[source] = bus_last_response_tick;
    if (health_faulted[source] != 0U)
    {
      /* 恢复需连续成功 COMM485_HEALTH_RECOVER_COUNT 次，
       * 防止不稳定总线上偶发一次成功读造成报警抖动。 */
      if (health_ok_count[source] < 255U)
      {
        health_ok_count[source]++;
      }
      if (health_ok_count[source] >= COMM485_HEALTH_RECOVER_COUNT)
      {
        health_ok_count[source] = 0U;
        health_faulted[source] = 0U;
        health_fault_bitmap &= ~info->bitmap_bits;
        /*
         * 恢复同时同步故障锁存：若当前锁存的正是本源的故障码，则回退为
         * 剩余仍在断连的从站故障码（无则清零），避免位图已清而 last_fault_code
         * 残留，导致上位机收到“更新”而非“解除”。若锁存的是流程级故障码则不动，
         * 防止被健康恢复误覆盖。
         */
        if ((AutoFaultCode)info->fault_code == AutoDevice_GetFault())
        {
          AutoDevice_SetFault(
              (AutoFaultCode)TaskComm485_GetFirstHealthFault());
        }
      }
    }
    else
    {
      health_ok_count[source] = 0U;
    }
    return;
  }

  health_ok_count[source] = 0U;
  if (health_faulted[source] != 0U)
  {
    return;
  }
  /* 时间制判定：COMM485_HEALTH_FAULT_AFTER_MS 内没有一次成功读取，
   * 判定对应传感器/从站断连。 */
  if ((HAL_GetTick() - health_last_ok_tick[source]) >= COMM485_HEALTH_FAULT_AFTER_MS)
  {
    health_faulted[source] = 1U;
    health_fault_bitmap |= info->bitmap_bits;
    AutoDevice_SetFault((AutoFaultCode)info->fault_code);
    AutoDevice_Report(AUTO_REPORT_SENSOR_FAULT);
  }
}

static void health_update_prop_valve(uint8_t ok)
{
  const Comm485HealthSourceInfo *info =
      &health_source_info[COMM485_HEALTH_PROP_VALVE];

  if (ok != 0U)
  {
    bus_last_response_tick = HAL_GetTick();
    health_last_ok_tick[COMM485_HEALTH_PROP_VALVE] = bus_last_response_tick;
    proportional_valve_failure_count = 0U;
    health_ok_count[COMM485_HEALTH_PROP_VALVE] = 0U;
    if (health_faulted[COMM485_HEALTH_PROP_VALVE] != 0U)
    {
      health_faulted[COMM485_HEALTH_PROP_VALVE] = 0U;
      health_fault_bitmap &= ~info->bitmap_bits;
      if ((AutoFaultCode)info->fault_code == AutoDevice_GetFault())
      {
        AutoDevice_SetFault(
            (AutoFaultCode)TaskComm485_GetFirstHealthFault());
      }
    }
    return;
  }

  if (proportional_valve_failure_count < COMM485_PROP_VALVE_FAILURE_LIMIT)
  {
    proportional_valve_failure_count++;
  }
  if ((proportional_valve_failure_count >= COMM485_PROP_VALVE_FAILURE_LIMIT) &&
      (health_faulted[COMM485_HEALTH_PROP_VALVE] == 0U))
  {
    health_faulted[COMM485_HEALTH_PROP_VALVE] = 1U;
    health_fault_bitmap |= info->bitmap_bits;
    AutoDevice_SetFault((AutoFaultCode)info->fault_code);
    AutoDevice_Report(AUTO_REPORT_SENSOR_FAULT);
  }
}

static void process_oxygen_sensor(void)
{
  OxygenSensorData sensor_data;
  OxygenSensorStatus status;
  uint32_t now = HAL_GetTick();

  status = OxygenSensor_ReadAll(RS485_STATION_OXYGEN_SENSOR, &sensor_data);
  health_update_source(COMM485_HEALTH_OXYGEN,
                       (status == OXYGEN_SENSOR_OK) ? 1U : 0U);
  now = HAL_GetTick();

  if (environment_mutex == NULL)
  {
    return;
  }
  if (osMutexAcquire(environment_mutex, osWaitForever) != osOK)
  {
    return;
  }

  if (status == OXYGEN_SENSOR_OK)
  {
    environment_data.humidity_x10 = sensor_data.humidity_x10;
    environment_data.temperature_x10 = sensor_data.temperature_x10;
    environment_data.oxygen_x10 = sensor_data.oxygen_x10;
    environment_data.update_tick = now;
    environment_data.valid = 1U;
  }
  else if ((environment_data.valid != 0U) &&
           ((now - environment_data.update_tick) > COMM485_SENSOR_STALE_MS))
  {
    environment_data.valid = 0U;
  }

  (void)osMutexRelease(environment_mutex);
}

static void process_incab_trh_sensor(void)
{
  OxygenSensorData sensor_data;
  OxygenSensorStatus status;
  uint32_t now = HAL_GetTick();

  status = OxygenSensor_ReadAll(RS485_STATION_INCABTRH_SENSOR, &sensor_data);
  health_update_source(COMM485_HEALTH_INCAB_TRH,
                       (status == OXYGEN_SENSOR_OK) ? 1U : 0U);
  now = HAL_GetTick();

  if (environment_mutex == NULL)
  {
    return;
  }
  if (osMutexAcquire(environment_mutex, osWaitForever) != osOK)
  {
    return;
  }

  if (status == OXYGEN_SENSOR_OK)
  {
    environment_data.incab_humidity_x10 = sensor_data.humidity_x10;
    environment_data.incab_temperature_x10 = sensor_data.temperature_x10;
    environment_data.incab_update_tick = now;
    environment_data.incab_valid = 1U;
  }
  else if ((environment_data.incab_valid != 0U) &&
           ((now - environment_data.incab_update_tick) > COMM485_SENSOR_STALE_MS))
  {
    environment_data.incab_valid = 0U;
  }

  (void)osMutexRelease(environment_mutex);
}

static void process_ozone_sensor(void)
{
  uint16_t ozone_x100;
  OxygenSensorStatus status;
  uint32_t now = HAL_GetTick();

  status = OxygenSensor_ReadOzone(RS485_STATION_OZONE_SENSOR, &ozone_x100);
  health_update_source(COMM485_HEALTH_OZONE,
                       (status == OXYGEN_SENSOR_OK) ? 1U : 0U);
  now = HAL_GetTick();

  if (environment_mutex == NULL)
  {
    return;
  }
  if (osMutexAcquire(environment_mutex, osWaitForever) != osOK)
  {
    return;
  }

  if (status == OXYGEN_SENSOR_OK)
  {
    environment_data.ozone_x100 = ozone_x100;
    environment_data.ozone_update_tick = now;
    environment_data.ozone_valid = 1U;
  }
  else if ((environment_data.ozone_valid != 0U) &&
           ((now - environment_data.ozone_update_tick) > COMM485_SENSOR_STALE_MS))
  {
    environment_data.ozone_valid = 0U;
  }

  (void)osMutexRelease(environment_mutex);
}

static void process_temperature_module(void)
{
#if (RS485_TEMPERATURE_MODULE_PRESENT == 0U)
  const int16_t temperature_x10[4] = {
    RS485_FIXED_INLET_TEMP_X10,
    RS485_FIXED_TANK_TEMP_X10,
    RS485_FIXED_PIPE_TEMP_X10,
    RS485_FIXED_SPARE_TEMP_X10
  };
  const uint8_t valid[4] = {1U, 1U, 1U, 1U};

  AutoDevice_UpdateTemperatureChannels(temperature_x10, valid);
  temperature_module_update_tick = HAL_GetTick();
  health_update_source(COMM485_HEALTH_INLET_TEMP, 1U);
  health_update_source(COMM485_HEALTH_TANK_TEMP, 1U);
  health_update_source(COMM485_HEALTH_PIPE_TEMP, 1U);
  health_update_source(COMM485_HEALTH_SPARE_TEMP, 1U);
#else
  TemperatureModuleData data = {0};

  if (TemperatureModule_ReadAll(RS485_STATION_TEMPERATURE, &data) ==
      TEMPERATURE_MODULE_OK)
  {
    AutoDevice_UpdateTemperatureChannels(data.temperature_x10, data.valid);
    temperature_module_update_tick = HAL_GetTick();
    health_update_source(COMM485_HEALTH_INLET_TEMP, data.valid[0]);
    health_update_source(COMM485_HEALTH_TANK_TEMP, data.valid[1]);
    health_update_source(COMM485_HEALTH_PIPE_TEMP, data.valid[2]);
    health_update_source(COMM485_HEALTH_SPARE_TEMP, data.valid[3]);
  }
  else
  {
    /* 模块通信失败：四个通道各自按 5 秒无响应判定跳故障，无需额外的模块级故障码。 */
    health_update_source(COMM485_HEALTH_INLET_TEMP, 0U);
    health_update_source(COMM485_HEALTH_TANK_TEMP, 0U);
    health_update_source(COMM485_HEALTH_PIPE_TEMP, 0U);
    health_update_source(COMM485_HEALTH_SPARE_TEMP, 0U);
    if ((temperature_module_update_tick != 0U) &&
        ((HAL_GetTick() - temperature_module_update_tick) > COMM485_SENSOR_STALE_MS))
    {
      AutoDevice_InvalidateTemperatureChannels();
      temperature_module_update_tick = 0U;
    }
  }
#endif
}

/*
 * 执行器从站主动探测：轮询每槽只读探测一站，不改变任何输出，
 * 用于空闲时发现从站断连。
 */
static void process_actuator_probe(void)
{
  static const uint8_t probe_stations[] = {
    /*
     * 比例阀不参与只读巡检：现场阀体可正常响应 0x06 写角度，但部分型号
     * 不支持 0x03 读反馈寄存器。比例阀在线状态改由写角度成功/失败判定。
     */
    RS485_STATION_LEFT_FAN,
    RS485_STATION_RIGHT_FAN,
    RS485_STATION_BOTTOM_FAN,
#if (RS485_TOP_EXHAUST_PRESENT != 0U)
    RS485_STATION_TOP_EXHAUST_FAN,
#endif
    RS485_STATION_LEFT_HEATER,
    RS485_STATION_RIGHT_HEATER,
    RS485_STATION_BOTTOM_HEATER
  };
  static const uint16_t probe_registers[] = {
    THYRISTOR_REG_OUTPUT_VOLTAGE,
    THYRISTOR_REG_OUTPUT_VOLTAGE,
    THYRISTOR_REG_OUTPUT_VOLTAGE,
#if (RS485_TOP_EXHAUST_PRESENT != 0U)
    THYRISTOR_REG_OUTPUT_VOLTAGE,
#endif
    THYRISTOR_REG_OUTPUT_VOLTAGE,
    THYRISTOR_REG_OUTPUT_VOLTAGE,
    THYRISTOR_REG_OUTPUT_VOLTAGE
  };
  static const uint8_t probe_sources[] = {
    (uint8_t)COMM485_HEALTH_LEFT_FAN,
    (uint8_t)COMM485_HEALTH_RIGHT_FAN,
    (uint8_t)COMM485_HEALTH_BOTTOM_FAN,
#if (RS485_TOP_EXHAUST_PRESENT != 0U)
    (uint8_t)COMM485_HEALTH_TOP_EXHAUST_FAN,
#endif
    (uint8_t)COMM485_HEALTH_LEFT_HEATER,
    (uint8_t)COMM485_HEALTH_RIGHT_HEATER,
    (uint8_t)COMM485_HEALTH_BOTTOM_HEATER
  };
  uint16_t probe_value;
  uint8_t index = actuator_probe_index;
  ThyristorStatus status;

  actuator_probe_index =
      (uint8_t)((index + 1U) %
                (uint8_t)(sizeof(probe_stations) / sizeof(probe_stations[0])));
  /* 探测项统一用 0x03 读探测，不写任何寄存器、无副作用。 */
  status = Thyristor_ReadRegister(probe_stations[index], probe_registers[index],
                                  &probe_value);
  health_update_source((Comm485HealthSource)probe_sources[index],
                       (status == THYRISTOR_OK) ? 1U : 0U);
}

#endif


static void process_next_sensor(void)
{
#if (APP_DRY_RUN_ENABLE != 0U)
  int16_t temperature_x10[4] = {300, 360, 360, 360};
  uint8_t valid[4] = {1U, 1U, 1U, 1U};

  AutoDevice_UpdateTemperatureChannels(temperature_x10, valid);
  return;
#else
  uint32_t now = HAL_GetTick();

  if (self_test_pressure_priority != 0U)
  {
    if ((now - sensor_poll_tick) >= COMM485_SELF_TEST_PRESSURE_PERIOD_MS)
    {
      sensor_poll_tick = now;
      process_actuator_probe();
    }
    return;
  }

  if ((now - sensor_poll_tick) >= COMM485_SENSOR_SLOT_PERIOD_MS)
  {
    sensor_poll_tick = now;

    switch (sensor_poll_slot)
    {
      case 0U:
        process_oxygen_sensor();
        break;

      case 1U:
        process_temperature_module();
        break;

      case 2U:
        process_incab_trh_sensor();
        break;

      case 3U:
        process_ozone_sensor();
        break;

      default:
        break;
    }

    ++sensor_poll_slot;
    if (sensor_poll_slot >= 4U)
    {
      sensor_poll_slot = 0U;
    }
  }

  /*
   * 执行器探测独立节拍（250ms 一站）。总线上最近有从站应答时才顺延
   * 一拍，避免 9600 波特率背靠背事务；断线时所有事务都超时，
   * 背靠背无害，全速扫描。顺延超过一个传感器槽位周期则强制执行，
   * 防止探测被饿死。
   */
  if ((now - actuator_probe_tick) >= COMM485_PROBE_PERIOD_MS)
  {
    if (((now - bus_last_response_tick) >= COMM485_PROBE_GAP_MS) ||
        ((now - actuator_probe_tick) >=
         (COMM485_PROBE_PERIOD_MS + COMM485_SENSOR_SLOT_PERIOD_MS)))
    {
      actuator_probe_tick = now;
      process_actuator_probe();
    }
  }
#endif
}

static void Comm485Task(void *argument)
{
  Comm485Command command;

  (void)argument;
#if (APP_DRY_RUN_ENABLE == 0U)
  Thyristor_Init(&huart3);
  OxygenSensor_Init(&huart3);
  TemperatureModule_Init(&huart3);
#endif

  /*
   * 上电不能假设从站已经自行复位。主动把三路加热器和三路风机全部写0%，
   * 并保持非零输出禁止状态，直到自动流程或合法手动485命令明确释放。
   */
  current_percent = 0U;
#if (APP_DRY_RUN_ENABLE != 0U)
  dry_run_zero_actuator_cache();
#else
  apply_all_actuators_zero();
#endif
  sensor_poll_tick = HAL_GetTick();
  temperature_module_update_tick = 0U;
  sensor_poll_slot = 0U;

  for (;;)
  {
    uint8_t command_count = 0U;

    TaskWatchdog_Heartbeat(TASK_WATCHDOG_COMM485);

    if (safe_stop_requested != 0U)
    {
      /*
       * 同一条485总线只能串行发送；这里优先关闭三路加热，再关闭风机。
       * 即使此前存在大量普通命令，也不会排在这些关闭命令前面。
       */
      safe_stop_requested = 0U;
      (void)osMessageQueueReset(comm485_queue);
      apply_all_actuators_zero();
    }

    while ((command_count < COMM485_MAX_COMMANDS_PER_LOOP) &&
           (osMessageQueueGet(comm485_queue, &command, NULL, 0U) == osOK))
    {
      command_count++;
      HAL_StatusTypeDef apply_status = HAL_ERROR;

      if (safe_stop_requested != 0U)
      {
        break;
      }
      if (command_is_superseded(&command) != 0U)
      {
        continue;
      }
      if ((command_is_nonzero(&command) != 0U) &&
          (nonzero_output_is_inhibited() != 0U))
      {
        clear_pending_command(&command);
        continue;
      }
      if ((command_is_nonzero(&command) != 0U) &&
          (station_retry_due(command.station) == 0U))
      {
        if (osMessageQueuePut(comm485_queue, &command, 0U, 0U) != osOK)
        {
          clear_pending_command(&command);
          report_dropped_command(&command);
        }
        continue;
      }

      if (command.id == COMM485_CMD_SET_PERCENT)
      {
        apply_status = apply_percent(command.station, command.percent);
      }
      else if (command.id == COMM485_CMD_SET_PROP_VALVE_ANGLE)
      {
        apply_status = apply_proportional_valve_angle(command.angle);
      }
      if (apply_status == HAL_OK)
      {
        mark_station_success(command.station);
      }
      else
      {
        mark_station_failure(command.station);
        (void)retry_or_drop_command(&command);
      }
    }
    process_next_sensor();
    osDelay(5U);
  }
}

void TaskComm485_Create(void)
{
  safe_stop_requested = 0U;
  nonzero_inhibit_reasons = COMM485_INHIBIT_STARTUP;
  rs485_actuator_mode = 1U;
  memset(station_failure_count, 0, sizeof(station_failure_count));
  memset(station_next_retry_tick, 0, sizeof(station_next_retry_tick));
  memset((void *)station_success_count, 0, sizeof(station_success_count));
  self_test_pressure_priority = 0U;
  proportional_valve_failure_count = 0U;
#if (APP_DRY_RUN_ENABLE == 0U)
  {
    uint32_t boot_tick = HAL_GetTick();
    uint8_t health_index;

    for (health_index = 0U;
         health_index < (uint8_t)COMM485_HEALTH_SOURCE_COUNT; ++health_index)
    {
      health_last_ok_tick[health_index] = boot_tick;
    }
  }
  bus_last_response_tick = 0U;
  memset(health_faulted, 0, sizeof(health_faulted));
  memset(health_ok_count, 0, sizeof(health_ok_count));
  health_fault_bitmap = 0U;
  actuator_probe_index = 0U;
  actuator_probe_tick = 0U;
#endif
  environment_mutex = osMutexNew(NULL);
  comm485_queue = osMessageQueueNew(COMM485_QUEUE_LENGTH,
                                    sizeof(Comm485Command),
                                    NULL);
  comm485_task_handle = osThreadNew(Comm485Task, NULL, &comm485_task_attributes);
  (void)comm485_task_handle;
}


uint32_t TaskComm485_GetHealthBitmap(void)
{
#if (APP_DRY_RUN_ENABLE != 0U)
  return 0U;
#else
  return health_fault_bitmap;
#endif
}

uint8_t TaskComm485_IsAllHealthy(void)
{
  return (TaskComm485_GetHealthBitmap() == 0U) ? 1U : 0U;
}

uint16_t TaskComm485_GetFirstHealthFault(void)
{
#if (APP_DRY_RUN_ENABLE != 0U)
  return 0U;
#else
  uint8_t index;

  for (index = 0U; index < (uint8_t)COMM485_HEALTH_SOURCE_COUNT; ++index)
  {
    if (health_faulted[index] != 0U)
    {
      return health_source_info[index].fault_code;
    }
  }
  return 0U;
#endif
}

/*
 * 故障复位时调用：清除巡检故障标记、失败计数与位图，
 * 使仍然断连的从站在随后几秒内重新置位并再次上报。
 */
void TaskComm485_ResetHealthFaults(void)
{
#if (APP_DRY_RUN_ENABLE != 0U)
  return;
#else
  uint8_t index;

  for (index = 0U; index < (uint8_t)COMM485_HEALTH_SOURCE_COUNT; ++index)
  {
    health_last_ok_tick[index] = HAL_GetTick();
    health_faulted[index] = 0U;
    health_ok_count[index] = 0U;
  }
  health_fault_bitmap = 0U;
  /* 复位巡检后位图已清空，同步清掉健康故障锁存，避免故障码残留。 */
  AutoDevice_SetFault(AUTO_FAULT_NONE);
#endif
}
