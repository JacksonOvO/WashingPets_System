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

#include <string.h>

#define COMM485_TASK_STACK             512U
#define COMM485_TASK_PRIORITY          osPriorityNormal
#define COMM485_QUEUE_LENGTH          32U
#define COMM485_SENSOR_PERIOD_MS       1000U
#define COMM485_SENSOR_SLOT_PERIOD_MS  250U
#define COMM485_SENSOR_STALE_MS        5000U
#define COMM485_MAX_COMMANDS_PER_LOOP     1U
#define COMM485_COMMAND_RETRY_MAX         2U
#define COMM485_RETRY_BACKOFF_BASE_MS    250U
#define COMM485_RETRY_BACKOFF_MAX_MS    2000U
#define COMM485_DEBUG_ENABLE           0U
#define COMM485_PROP_VALVE_REG_ANGLE   0x0001U
#define COMM485_PROP_VALVE_MAX_ANGLE   90U
#define COMM485_BOOSTER_REG_FREQ       0x0000U
#define COMM485_BOOSTER_REG_FEEDBACK_PRESSURE 0x1010U
#define COMM485_BOOSTER_REG_SET_PRESSURE      0x101FU
#define COMM485_BOOSTER_REG_CONTROL    0x2000U
#define COMM485_BOOSTER_CMD_FORWARD    0x0001U
#define COMM485_BOOSTER_CMD_STOP       0x0006U
#define COMM485_BOOSTER_DEFAULT_PRESSURE_X10 50U

typedef enum
{
  COMM485_CMD_SET_PERCENT = 1,
  COMM485_CMD_SET_PROP_VALVE_ANGLE,
  COMM485_CMD_SET_BOOSTER_PERCENT,
  COMM485_CMD_SET_BOOSTER_RUNNING
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

static const osThreadAttr_t comm485_task_attributes = {
  .name = "comm485",
  .stack_size = COMM485_TASK_STACK * 4U,
  .priority = COMM485_TASK_PRIORITY,
};

#if (APP_DRY_RUN_ENABLE != 0U)
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

HAL_StatusTypeDef TaskComm485_RequestSetPercent(uint8_t station,
                                                uint8_t percent)
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
  if ((last_percent_by_station[station] == percent) ||
      ((pending_percent_valid_by_station[station] != 0U) &&
       (pending_percent_by_station[station] == percent)))
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
  uint32_t tick = booster_pump_feedback_tick;

  if ((booster_pump_feedback_valid == 0U) ||
      ((HAL_GetTick() - tick) > COMM485_SENSOR_STALE_MS))
  {
    return 0U;
  }
  return booster_pump_feedback_pressure_x10;
}

uint8_t TaskComm485_GetLastBoosterPumpRunning(void)
{
  return last_booster_pump_running;
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
  if ((angle != 0U) && (AutoDevice_ReadInput(AUTO_INPUT_TANK_HIGH) == 0U))
  {
    return HAL_BUSY;
  }
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
#if (APP_DRY_RUN_ENABLE != 0U)
  (void)force;
  if (percent > 100U)
  {
    percent = 100U;
  }
  last_booster_pump_percent = percent;
  booster_pump_feedback_pressure_x10 = percent;
  booster_pump_feedback_valid = 1U;
  booster_pump_feedback_tick = HAL_GetTick();
  pending_booster_pump_percent = percent;
  pending_booster_pump_percent_valid = 0U;
  return HAL_OK;
#else
  Comm485Command command;

  if (comm485_queue == NULL)
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
      ((last_booster_pump_percent == percent) ||
       ((pending_booster_pump_percent_valid != 0U) &&
        (pending_booster_pump_percent == percent))))
  {
    return HAL_OK;
  }

  command.id = COMM485_CMD_SET_BOOSTER_PERCENT;
  command.station = RS485_STATION_BOOSTER_PUMP;
  command.percent = percent;
  command.angle = 0U;
  command.enabled = 0U;
  command.retry_count = 0U;
  if (osMessageQueuePut(comm485_queue, &command, 0U, 0U) == osOK)
  {
    pending_booster_pump_percent = percent;
    pending_booster_pump_percent_valid = 1U;
    return HAL_OK;
  }
  return HAL_BUSY;
#endif
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
#if (APP_DRY_RUN_ENABLE != 0U)
  (void)force;
  last_booster_pump_running = (enabled != 0U) ? 1U : 0U;
  pending_booster_pump_running = last_booster_pump_running;
  pending_booster_pump_running_valid = 0U;
  return HAL_OK;
#else
  Comm485Command command;

  if (comm485_queue == NULL)
  {
    return HAL_ERROR;
  }
  if ((enabled != 0U) && (nonzero_output_is_inhibited() != 0U))
  {
    return HAL_BUSY;
  }
  command.enabled = (enabled != 0U) ? 1U : 0U;
  if ((force == 0U) &&
      ((last_booster_pump_running == command.enabled) ||
       ((pending_booster_pump_running_valid != 0U) &&
        (pending_booster_pump_running == command.enabled))))
  {
    return HAL_OK;
  }

  command.id = COMM485_CMD_SET_BOOSTER_RUNNING;
  command.station = RS485_STATION_BOOSTER_PUMP;
  command.percent = 0U;
  command.angle = 0U;
  command.retry_count = 0U;
  if (osMessageQueuePut(comm485_queue, &command, 0U, 0U) == osOK)
  {
    pending_booster_pump_running = command.enabled;
    pending_booster_pump_running_valid = 1U;
    return HAL_OK;
  }
  return HAL_BUSY;
#endif
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
  TaskComm485_InhibitNonzero(COMM485_INHIBIT_SAFE_STOP);
  safe_stop_requested = 1U;
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
    return HAL_OK;
  }
  return HAL_ERROR;
}

static HAL_StatusTypeDef apply_booster_pump_percent(uint8_t percent)
{
  uint16_t value;
  uint16_t readback;
  ThyristorStatus status;

  if (percent > 100U)
  {
    percent = 100U;
  }
  value = (uint16_t)percent;
  status = Thyristor_WriteRegister(RS485_STATION_BOOSTER_PUMP,
                                   COMM485_BOOSTER_REG_FREQ,
                                   value);
  if (status != THYRISTOR_OK)
  {
    return HAL_ERROR;
  }

  status = Thyristor_ReadRegister(RS485_STATION_BOOSTER_PUMP,
                                  COMM485_BOOSTER_REG_SET_PRESSURE,
                                  &readback);
  if ((status != THYRISTOR_OK) || ((uint8_t)readback != percent))
  {
    return HAL_ERROR;
  }

  last_booster_pump_percent = percent;
  if (pending_booster_pump_percent == percent)
  {
    pending_booster_pump_percent_valid = 0U;
  }
  return HAL_OK;
}

static HAL_StatusTypeDef apply_booster_pump_running(uint8_t enabled)
{
  ThyristorStatus status;

  if ((enabled != 0U) && (last_booster_pump_percent == 0U))
  {
    if (apply_booster_pump_percent(COMM485_BOOSTER_DEFAULT_PRESSURE_X10) != HAL_OK)
    {
      return HAL_ERROR;
    }
  }
  status = Thyristor_WriteRegister(RS485_STATION_BOOSTER_PUMP,
                                   COMM485_BOOSTER_REG_CONTROL,
                                   (enabled != 0U) ?
                                   COMM485_BOOSTER_CMD_FORWARD :
                                   COMM485_BOOSTER_CMD_STOP);
  if (status == THYRISTOR_OK)
  {
    last_booster_pump_running = (enabled != 0U) ? 1U : 0U;
    if (pending_booster_pump_running == last_booster_pump_running)
    {
      pending_booster_pump_running_valid = 0U;
    }
    return HAL_OK;
  }
  return HAL_ERROR;
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
  else if (command->id == COMM485_CMD_SET_BOOSTER_PERCENT)
  {
    if (pending_booster_pump_percent == command->percent)
    {
      pending_booster_pump_percent_valid = 0U;
    }
  }
  else if (command->id == COMM485_CMD_SET_BOOSTER_RUNNING)
  {
    if (pending_booster_pump_running == command->enabled)
    {
      pending_booster_pump_running_valid = 0U;
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
  if (command->id == COMM485_CMD_SET_BOOSTER_PERCENT)
  {
    return (command->percent != 0U) ? 1U : 0U;
  }
  if (command->id == COMM485_CMD_SET_BOOSTER_RUNNING)
  {
    return (command->enabled != 0U) ? 1U : 0U;
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
  if (command->id == COMM485_CMD_SET_BOOSTER_PERCENT)
  {
    return ((pending_booster_pump_percent_valid != 0U) &&
            (pending_booster_pump_percent != command->percent)) ? 1U : 0U;
  }
  if (command->id == COMM485_CMD_SET_BOOSTER_RUNNING)
  {
    return ((pending_booster_pump_running_valid != 0U) &&
            (pending_booster_pump_running != command->enabled)) ? 1U : 0U;
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
    case RS485_STATION_BOOSTER_PUMP: return AUTO_FAULT_485_BOOSTER_PUMP_COMM;
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
  if (rs485_actuator_mode != 0U)
  {
    pending_percent_valid_by_station[RS485_STATION_TOP_EXHAUST_FAN] = 0U;
    apply_percent(RS485_STATION_TOP_EXHAUST_FAN, 0U);
    TaskWatchdog_Heartbeat(TASK_WATCHDOG_COMM485);
  }
  pending_booster_pump_running_valid = 0U;
  pending_booster_pump_percent_valid = 0U;
  apply_booster_pump_running(0U);
  TaskWatchdog_Heartbeat(TASK_WATCHDOG_COMM485);
  apply_booster_pump_percent(0U);
  TaskWatchdog_Heartbeat(TASK_WATCHDOG_COMM485);
  pending_proportional_valve_angle_valid = 0U;
  apply_proportional_valve_angle(0U);
  TaskWatchdog_Heartbeat(TASK_WATCHDOG_COMM485);
}

/*
 * 每秒读取一次站号为 7 的氧气/温湿度传感器。
 * 读取结果保存在缓存中，其它线程只能通过
 * TaskComm485_GetEnvironmentData() 获取，不能直接访问 USART1。
 */
#if (APP_DRY_RUN_ENABLE == 0U)
static void process_oxygen_sensor(void)
{
  OxygenSensorData sensor_data;
  OxygenSensorStatus status;
  uint32_t now = HAL_GetTick();

  status = OxygenSensor_ReadAll(RS485_STATION_OXYGEN_SENSOR, &sensor_data);
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

static void process_booster_pump_feedback(void)
{
  uint16_t pressure_x10;
  uint32_t now = HAL_GetTick();

  if (Thyristor_ReadRegister(RS485_STATION_BOOSTER_PUMP,
                             COMM485_BOOSTER_REG_FEEDBACK_PRESSURE,
                             &pressure_x10) == THYRISTOR_OK)
  {
    booster_pump_feedback_pressure_x10 = (pressure_x10 > 100U) ?
                                         100U : (uint8_t)pressure_x10;
    booster_pump_feedback_tick = now;
    booster_pump_feedback_valid = 1U;
  }
  else if ((booster_pump_feedback_valid != 0U) &&
           ((now - booster_pump_feedback_tick) > COMM485_SENSOR_STALE_MS))
  {
    booster_pump_feedback_valid = 0U;
  }
}

static void process_temperature_module(void)
{
  TemperatureModuleData data = {0};

  if (TemperatureModule_ReadAll(RS485_STATION_TEMPERATURE, &data) ==
      TEMPERATURE_MODULE_OK)
  {
    AutoDevice_UpdateTemperatureChannels(data.temperature_x10, data.valid);
    temperature_module_update_tick = HAL_GetTick();
  }
  else if ((temperature_module_update_tick != 0U) &&
           ((HAL_GetTick() - temperature_module_update_tick) > COMM485_SENSOR_STALE_MS))
  {
    AutoDevice_InvalidateTemperatureChannels();
    temperature_module_update_tick = 0U;
  }
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

  if ((now - sensor_poll_tick) < COMM485_SENSOR_SLOT_PERIOD_MS)
  {
    return;
  }
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
      process_booster_pump_feedback();
      break;
  }

  ++sensor_poll_slot;
  if (sensor_poll_slot >= 5U)
  {
    sensor_poll_slot = 0U;
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
      else if (command.id == COMM485_CMD_SET_BOOSTER_PERCENT)
      {
        apply_status = apply_booster_pump_percent(command.percent);
      }
      else if (command.id == COMM485_CMD_SET_BOOSTER_RUNNING)
      {
        apply_status = apply_booster_pump_running(command.enabled);
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
  environment_mutex = osMutexNew(NULL);
  comm485_queue = osMessageQueueNew(COMM485_QUEUE_LENGTH,
                                    sizeof(Comm485Command),
                                    NULL);
  comm485_task_handle = osThreadNew(Comm485Task, NULL, &comm485_task_attributes);
  (void)comm485_task_handle;
}
