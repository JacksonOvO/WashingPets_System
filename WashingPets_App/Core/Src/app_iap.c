#include "app_iap.h"

#include "app_param.h"
#include "auto_device.h"
#include "auto_process.h"
#include "cmsis_os.h"
#include "iap_protocol.h"
#include "rs485_config.h"
#include "task_clean.h"
#include "task_comm485.h"
#include "task_output.h"

#include <stddef.h>
#include <string.h>

#define APP_IAP_HEADER_SIZE          4U
#define APP_IAP_CRC_SIZE             2U
#define APP_IAP_ENTER_MAGIC_SIZE     4U
#define APP_IAP_PARAM_ADDRESS        0x080C0000U
#define APP_IAP_METADATA_ADDRESS     0x080E0000U
#define APP_IAP_APP_MAX_SIZE         (APP_IAP_PARAM_ADDRESS - APP_IAP_APP_ADDRESS)
#define APP_IAP_MAX_CHUNK_SIZE       128U
#define APP_IAP_BOOT_VERSION_UNKNOWN 0xFFFFFFFFU
#define APP_IAP_METADATA_MAGIC       0x424C4D44U
#define APP_IAP_METADATA_VALID       0x3FFFFFFFU
#define APP_STATUS_PAYLOAD_SIZE      12U
#define APP_INPUT_BITMAP_SIZE        ((AUTO_INPUT_COUNT + 7U) / 8U)
#define APP_OUTPUT_POINT_COUNT       (AUTO_OUTPUT_COUNT - 1U)
#define APP_OUTPUT_BITMAP_SIZE       ((APP_OUTPUT_POINT_COUNT + 7U) / 8U)
#define APP_IO_PAYLOAD_SIZE          (2U + APP_INPUT_BITMAP_SIZE + APP_OUTPUT_BITMAP_SIZE)
#define APP_STATE_CHECK_PERIOD_MS    10U
#define APP_STATE_HEARTBEAT_MS       5000U
#define APP_OUTPUT_SYNC_TIMEOUT_MS   100U
#define APP_INTERACTION_RESEND_MS    1000U

#define APP_BUSINESS_CMD_QUERY_STATUS       0x30U
#define APP_BUSINESS_CMD_SET_WORK_MODE      0x31U
#define APP_BUSINESS_CMD_PROCESS_CONTROL    0x32U
#define APP_BUSINESS_CMD_PROCESS_INTERACT   0x33U
#define APP_BUSINESS_CMD_MANUAL_OUTPUT      0x34U
#define APP_BUSINESS_CMD_QUERY_IO_STATUS    0x35U
#define APP_BUSINESS_CMD_MANUAL_RS485       0x36U
#define APP_BUSINESS_CMD_SET_ACTUATOR_MODE  0x37U
#define APP_BUSINESS_CMD_BOOSTER_CONTROL    0x38U
#define APP_BUSINESS_CMD_GET_PARAM_BLOCK    0x40U
#define APP_BUSINESS_CMD_SET_PARAM_BLOCK    0x41U
#define APP_BUSINESS_CMD_SAVE_PARAMS        0x42U
#define APP_BUSINESS_CMD_RESTORE_DEFAULTS   0x43U

#define APP_BUSINESS_RSP_DEVICE_STATUS      0xA1U
#define APP_BUSINESS_RSP_PARAM_BLOCK        0xA2U
#define APP_BUSINESS_RSP_INTERACTION        0xA3U
#define APP_BUSINESS_RSP_FAULT_EVENT        0xA4U
#define APP_BUSINESS_RSP_TELEMETRY          0xA5U
#define APP_BUSINESS_RSP_IO_STATUS          0xA6U

typedef enum
{
  APP_IAP_WAIT_HEAD_0 = 0,
  APP_IAP_WAIT_HEAD_1,
  APP_IAP_READ_HEADER,
  APP_IAP_READ_PAYLOAD,
  APP_IAP_READ_CRC
} AppIapParserState;

typedef struct
{
  uint8_t work_mode;
  uint8_t run_state;
  uint8_t process_type;
  uint8_t stage_id;
  uint16_t step_id;
  uint8_t step_state;
  uint8_t recipe_id;
  uint16_t progress_x10;
  uint8_t result;
  uint8_t reserved;
} BusinessDeviceStatus;

typedef struct
{
  uint32_t event_id;
  uint8_t event_type;
  uint8_t severity;
  uint16_t primary_fault_code;
  uint32_t fault_bitmap;
} BusinessFaultEvent;

typedef struct
{
  uint32_t magic;
  uint32_t target;
  uint32_t version;
  uint32_t size;
  uint32_t image_crc;
  uint32_t record_crc;
  uint32_t status;
} AppFirmwareMetadata;

static UART_HandleTypeDef *app_iap_uart;
static AppIapParserState parser_state;
static uint8_t header[APP_IAP_HEADER_SIZE];
static uint8_t payload[IAP_MAX_PAYLOAD_SIZE];
static uint8_t crc_bytes[APP_IAP_CRC_SIZE];
static uint16_t payload_length;
static uint16_t receive_index;
static osMutexId_t tx_mutex;
static uint8_t business_tx_sequence;
static uint32_t fault_bitmap;
static uint32_t fault_event_id;
static uint8_t last_status_payload[APP_STATUS_PAYLOAD_SIZE];
static uint8_t last_io_payload[APP_IO_PAYLOAD_SIZE];
static uint8_t status_snapshot_valid;
static uint8_t io_snapshot_valid;
static volatile uint8_t status_report_requested;
static uint32_t interaction_request_id;
static uint8_t interaction_pending;
static uint8_t interaction_type_pending;
static uint8_t interaction_stage_id;
static uint16_t interaction_step_id;
static uint32_t interaction_tick;
static uint32_t state_check_tick;
static uint32_t status_heartbeat_tick;
static uint32_t io_heartbeat_tick;
#if (APP_IAP_TELEMETRY_ENABLE != 0U)
static uint32_t telemetry_tick;
static uint8_t telemetry_last_sequence;
static uint8_t telemetry_ack_received;
static uint32_t telemetry_ack_tick;
#endif

static void write_u16_le(uint8_t *data, uint16_t value)
{
  data[0] = (uint8_t)value;
  data[1] = (uint8_t)(value >> 8U);
}

static void write_u32_le(uint8_t *data, uint32_t value)
{
  data[0] = (uint8_t)value;
  data[1] = (uint8_t)(value >> 8U);
  data[2] = (uint8_t)(value >> 16U);
  data[3] = (uint8_t)(value >> 24U);
}

static uint16_t read_u16_le(const uint8_t *data)
{
  return (uint16_t)data[0] | ((uint16_t)data[1] << 8U);
}

static uint32_t read_u32_le(const uint8_t *data)
{
  return ((uint32_t)data[0]) |
         ((uint32_t)data[1] << 8U) |
         ((uint32_t)data[2] << 16U) |
         ((uint32_t)data[3] << 24U);
}

static uint16_t crc16_ccitt_false(const uint8_t *data, uint16_t length)
{
  uint16_t crc = 0xFFFFU;
  uint8_t bit;

  while (length-- > 0U)
  {
    crc ^= (uint16_t)(*data++) << 8U;
    for (bit = 0U; bit < 8U; ++bit)
    {
      crc = ((crc & 0x8000U) != 0U) ?
            (uint16_t)((crc << 1U) ^ 0x1021U) :
            (uint16_t)(crc << 1U);
    }
  }
  return crc;
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, uint32_t length)
{
  uint32_t index;
  uint8_t bit;

  for (index = 0U; index < length; ++index)
  {
    crc ^= data[index];
    for (bit = 0U; bit < 8U; ++bit)
    {
      crc = ((crc & 1U) != 0U) ? ((crc >> 1U) ^ 0xEDB88320U) : (crc >> 1U);
    }
  }
  return crc;
}

static uint32_t crc32_data(const uint8_t *data, uint32_t length)
{
  return crc32_update(0xFFFFFFFFU, data, length) ^ 0xFFFFFFFFU;
}

static const AppFirmwareMetadata *get_app_metadata(void)
{
  return (const AppFirmwareMetadata *)APP_IAP_METADATA_ADDRESS;
}

static uint8_t app_metadata_is_valid(const AppFirmwareMetadata *metadata)
{
  if ((metadata->magic != APP_IAP_METADATA_MAGIC) ||
      (metadata->target != APP_IAP_TARGET_ID) ||
      (metadata->status != APP_IAP_METADATA_VALID) ||
      (metadata->size < 8U) ||
      (metadata->size > APP_IAP_APP_MAX_SIZE))
  {
    return 0U;
  }

  return (metadata->record_crc ==
          crc32_data((const uint8_t *)metadata,
                     offsetof(AppFirmwareMetadata, record_crc))) ? 1U : 0U;
}

static void reset_parser(void)
{
  parser_state = APP_IAP_WAIT_HEAD_0;
  payload_length = 0U;
  receive_index = 0U;
}

void AppIap_TxLock(void)
{
  if (tx_mutex != NULL)
  {
    (void)osMutexAcquire(tx_mutex, osWaitForever);
  }
}

void AppIap_TxUnlock(void)
{
  if (tx_mutex != NULL)
  {
    (void)osMutexRelease(tx_mutex);
  }
}

static uint8_t next_business_sequence(void)
{
  return business_tx_sequence++;
}

static void send_frame(uint8_t command, uint8_t sequence,
                       const uint8_t *data, uint16_t length)
{
  uint8_t frame_header[6];
  uint8_t frame_crc[2];
  uint8_t crc_input[APP_IAP_HEADER_SIZE + IAP_MAX_PAYLOAD_SIZE];
  uint16_t crc;
  uint16_t index;

  frame_header[0] = IAP_HEAD_0;
  frame_header[1] = IAP_HEAD_1;
  frame_header[2] = command;
  frame_header[3] = sequence;
  write_u16_le(&frame_header[4], length);

  crc_input[0] = frame_header[2];
  crc_input[1] = frame_header[3];
  crc_input[2] = frame_header[4];
  crc_input[3] = frame_header[5];
  for (index = 0U; index < length; ++index)
  {
    crc_input[APP_IAP_HEADER_SIZE + index] = data[index];
  }
  crc = crc16_ccitt_false(crc_input, (uint16_t)(APP_IAP_HEADER_SIZE + length));
  write_u16_le(frame_crc, crc);

  AppIap_TxLock();
  (void)HAL_UART_Transmit(app_iap_uart, frame_header, sizeof(frame_header), 1000U);
  if (length > 0U)
  {
    (void)HAL_UART_Transmit(app_iap_uart, (uint8_t *)data, length, 1000U);
  }
  (void)HAL_UART_Transmit(app_iap_uart, frame_crc, sizeof(frame_crc), 1000U);
  AppIap_TxUnlock();
}

static void send_ack(uint8_t request, uint8_t sequence, uint32_t next_offset)
{
  uint8_t response[6];

  response[0] = request;
  response[1] = IAP_STATUS_OK;
  write_u32_le(&response[2], next_offset);
  send_frame(IAP_RSP_ACK, sequence, response, sizeof(response));
}

static void send_nack(uint8_t request, uint8_t sequence, uint8_t error,
                      uint8_t expected_sequence, uint32_t next_offset)
{
  uint8_t response[8];

  response[0] = request;
  response[1] = error;
  response[2] = expected_sequence;
  response[3] = 0U;
  write_u32_le(&response[4], next_offset);
  send_frame(IAP_RSP_NACK, sequence, response, sizeof(response));
}

static void send_device_info(uint8_t sequence)
{
  uint8_t response[28];

  response[0] = 0U;
  response[1] = IAP_PROTOCOL_MAJOR;
  response[2] = IAP_PROTOCOL_MINOR;
  response[3] = 1U;
  write_u32_le(&response[4], APP_IAP_TARGET_ID);
  write_u16_le(&response[8], APP_IAP_HARDWARE_VERSION);
  write_u32_le(&response[10], APP_IAP_BOOT_VERSION_UNKNOWN);
  write_u32_le(&response[14], APP_IAP_VERSION);
  write_u32_le(&response[18], APP_IAP_APP_ADDRESS);
  write_u32_le(&response[22], APP_IAP_APP_MAX_SIZE);
  write_u16_le(&response[26], APP_IAP_MAX_CHUNK_SIZE);
  send_frame(IAP_RSP_DEVICE_INFO, sequence, response, sizeof(response));
}

static void send_app_ready(uint8_t sequence)
{
  const AppFirmwareMetadata *metadata = get_app_metadata();
  uint32_t app_size = 0U;
  uint32_t app_crc32 = 0U;
  uint8_t response[22];

  if (app_metadata_is_valid(metadata) != 0U)
  {
    app_size = metadata->size;
    app_crc32 = metadata->image_crc;
  }

  response[0] = IAP_PROTOCOL_MAJOR;
  response[1] = IAP_PROTOCOL_MINOR;
  response[2] = 0U;
  response[3] = 0U;
  write_u32_le(&response[4], APP_IAP_TARGET_ID);
  write_u16_le(&response[8], APP_IAP_HARDWARE_VERSION);
  write_u32_le(&response[10], APP_IAP_VERSION);
  write_u32_le(&response[14], app_size);
  write_u32_le(&response[18], app_crc32);
  send_frame(IAP_RSP_APP_READY, sequence, response, sizeof(response));
}

static uint8_t enter_magic_is_valid(void)
{
  return ((payload_length == APP_IAP_ENTER_MAGIC_SIZE) &&
          (payload[0] == (uint8_t)'I') &&
          (payload[1] == (uint8_t)'A') &&
          (payload[2] == (uint8_t)'P') &&
          (payload[3] == (uint8_t)'1')) ? 1U : 0U;
}

static void enter_bootloader(uint8_t sequence)
{
  send_ack(IAP_CMD_ENTER_UPGRADE, sequence, 0U);

  while (__HAL_UART_GET_FLAG(app_iap_uart, UART_FLAG_TC) == RESET)
  {
  }

  __HAL_RCC_PWR_CLK_ENABLE();
  HAL_PWR_EnableBkUpAccess();
  __HAL_RCC_RTC_ENABLE();
  RTC->BKP0R = APP_IAP_REQUEST_MAGIC;
  __DSB();
  __ISB();
  if (RTC->BKP0R != APP_IAP_REQUEST_MAGIC)
  {
    HAL_PWR_DisableBkUpAccess();
    send_nack(IAP_CMD_ENTER_UPGRADE, sequence, IAP_ERR_FLASH, sequence, 0U);
    return;
  }
  HAL_PWR_DisableBkUpAccess();

  HAL_Delay(100U);
  __disable_irq();
  NVIC_SystemReset();
}

static uint8_t packet_crc_is_valid(void)
{
  uint8_t crc_input[APP_IAP_HEADER_SIZE + IAP_MAX_PAYLOAD_SIZE];
  uint16_t expected_crc;
  uint16_t actual_crc;
  uint16_t index;

  crc_input[0] = header[0];
  crc_input[1] = header[1];
  crc_input[2] = header[2];
  crc_input[3] = header[3];
  for (index = 0U; index < payload_length; ++index)
  {
    crc_input[APP_IAP_HEADER_SIZE + index] = payload[index];
  }

  actual_crc = crc16_ccitt_false(crc_input,
                                 (uint16_t)(APP_IAP_HEADER_SIZE + payload_length));
  expected_crc = (uint16_t)crc_bytes[0] | ((uint16_t)crc_bytes[1] << 8U);
  return (actual_crc == expected_crc) ? 1U : 0U;
}

static uint8_t get_work_mode(void)
{
  return (TaskOutput_IsAutoMode() != 0U) ? 2U : 1U;
}

static uint8_t get_run_state(void)
{
  if ((AutoProcess_IsFailed() != 0U) ||
      (TaskClean_IsFailed() != 0U))
  {
    return 7U;
  }
  if (AutoDevice_IsPaused() != 0U)
  {
    return 4U;
  }
  if ((AutoProcess_IsRunning() != 0U) ||
      (TaskClean_IsRunning() != 0U))
  {
    return 2U;
  }
  if (AutoProcess_IsFinished() != 0U)
  {
    return 6U;
  }
  return 0U;
}

static uint8_t get_process_type(void)
{
  if ((TaskClean_IsRunning() != 0U) ||
      (TaskClean_IsFailed() != 0U))
  {
    return 3U;
  }
  if (AutoProcess_IsRunning() != 0U)
  {
    return (AutoProcess_IsStageTest() != 0U) ? 2U : 1U;
  }
  if (TaskOutput_IsAutoMode() == 0U)
  {
    return 4U;
  }
  return 0U;
}

static uint8_t get_step_state(void)
{
  if ((AutoProcess_IsFailed() != 0U) ||
      (TaskClean_IsFailed() != 0U))
  {
    return 5U;
  }
  if (AutoDevice_IsPaused() != 0U)
  {
    return 3U;
  }
  if ((AutoProcess_IsRunning() != 0U) ||
      (TaskClean_IsRunning() != 0U))
  {
    return 1U;
  }
  if (AutoProcess_IsFinished() != 0U)
  {
    return 4U;
  }
  return 0U;
}

static uint8_t get_recipe_id(void)
{
  uint8_t recipe = (uint8_t)AppParam_Get()->active_recipe;

  if (recipe >= (uint8_t)APP_RECIPE_COUNT)
  {
    recipe = (uint8_t)APP_RECIPE_SHORT_SMALL;
  }
  return recipe;
}

static uint16_t get_progress_x10(void)
{
  uint16_t stage = AutoProcess_GetCurrentStage();
  uint16_t clean_step;

  if ((TaskClean_IsRunning() != 0U) ||
      (TaskClean_IsFailed() != 0U))
  {
    clean_step = TaskClean_GetCurrentStepId();
    if (clean_step >= 370U)
    {
      return 1000U;
    }
    return (uint16_t)(((uint32_t)clean_step * 1000U) / 370U);
  }

  if (AutoProcess_IsFailed() != 0U)
  {
    return 0U;
  }
  if (AutoProcess_IsFinished() != 0U)
  {
    return 1000U;
  }
  if (AutoProcess_IsRunning() == 0U)
  {
    return 0U;
  }
  if (stage >= (AUTO_PROCESS_STAGE_COUNT - 1U))
  {
    return 1000U;
  }
  return (uint16_t)((stage * 1000U) / (AUTO_PROCESS_STAGE_COUNT - 1U));
}

static uint8_t get_result(void)
{
  if ((AutoProcess_IsFailed() != 0U) ||
      (TaskClean_IsFailed() != 0U))
  {
    return 1U;
  }
  if (AutoProcess_IsFinished() != 0U)
  {
    return 0U;
  }
  return 0U;
}

static void build_device_status_payload(uint8_t payload_out[APP_STATUS_PAYLOAD_SIZE])
{
  BusinessDeviceStatus status = {0};

  status.work_mode = get_work_mode();
  status.run_state = get_run_state();
  status.process_type = get_process_type();
  if ((TaskClean_IsRunning() != 0U) ||
      (TaskClean_IsFailed() != 0U))
  {
    status.stage_id = (uint8_t)AUTO_PROCESS_STAGE_COUNT;
    status.step_id = TaskClean_GetCurrentStepId();
  }
  else
  {
    status.stage_id = (uint8_t)AutoProcess_GetCurrentStage();
    status.step_id = AutoProcess_GetCurrentStepId();
  }
  status.step_state = get_step_state();
  status.recipe_id = get_recipe_id();
  status.progress_x10 = get_progress_x10();
  status.result = get_result();
  status.reserved = 0U;

  payload_out[0] = status.work_mode;
  payload_out[1] = status.run_state;
  payload_out[2] = status.process_type;
  payload_out[3] = status.stage_id;
  write_u16_le(&payload_out[4], status.step_id);
  payload_out[6] = status.step_state;
  payload_out[7] = status.recipe_id;
  write_u16_le(&payload_out[8], status.progress_x10);
  payload_out[10] = status.result;
  payload_out[11] = status.reserved;
}

static void send_device_status_payload(uint8_t sequence,
                                       const uint8_t payload_out[APP_STATUS_PAYLOAD_SIZE])
{
  send_frame(APP_BUSINESS_RSP_DEVICE_STATUS, sequence,
             payload_out, APP_STATUS_PAYLOAD_SIZE);
  memcpy(last_status_payload, payload_out, APP_STATUS_PAYLOAD_SIZE);
  status_snapshot_valid = 1U;
  status_heartbeat_tick = HAL_GetTick();
}

static void send_device_status_with_seq(uint8_t sequence)
{
  uint8_t payload_out[APP_STATUS_PAYLOAD_SIZE];

  build_device_status_payload(payload_out);
  send_device_status_payload(sequence, payload_out);
}

static void send_param_block_response(uint8_t sequence, uint8_t group, uint8_t index)
{
  const AppParam_t *param = AppParam_Get();
  uint8_t payload_out[10U + sizeof(AppRecipe_t)];
  uint16_t data_length = 0U;
  uint32_t revision = AppParam_GetRevision();

  memset(payload_out, 0, sizeof(payload_out));
  payload_out[0] = group;
  payload_out[1] = index;
  write_u16_le(&payload_out[2], 1U);
  write_u32_le(&payload_out[4], revision);

  if (group == 0U)
  {
    payload_out[8] = 16U;
    payload_out[9] = 0U;
    payload_out[10] = param->default_percent;
    payload_out[11] = param->thyristor_station;
    write_u16_le(&payload_out[12], param->pwm_frequency);
    write_u32_le(&payload_out[14], param->auto_period_ms);
    write_u32_le(&payload_out[18], param->clean_period_ms);
    write_u32_le(&payload_out[22], 0U);
    data_length = 16U;
  }
  else if (group == 1U)
  {
    const AppRecipe_t *recipe = AppParam_GetRecipe((AppRecipeId)index);

    if (recipe == NULL)
    {
      return;
    }
    write_u16_le(&payload_out[8], (uint16_t)sizeof(AppRecipe_t));
    memcpy(&payload_out[10], recipe, sizeof(AppRecipe_t));
    data_length = (uint16_t)sizeof(AppRecipe_t);
  }
  else
  {
    return;
  }

  send_frame(APP_BUSINESS_RSP_PARAM_BLOCK, sequence, payload_out,
             (uint16_t)(10U + data_length));
}

#if (APP_IAP_TELEMETRY_ENABLE != 0U)
static void send_telemetry(uint8_t sequence)
{
  uint8_t payload_out[41];
  TaskComm485EnvironmentData env = {0};
  AutoSensorData sensors = {0};
  uint16_t valid_flags = 0U;

  memset(payload_out, 0, sizeof(payload_out));
  if (TaskComm485_GetEnvironmentData(&env) != 0U)
  {
    write_u16_le(&payload_out[0], (uint16_t)env.temperature_x10);
    write_u16_le(&payload_out[2], env.humidity_x10);
    write_u16_le(&payload_out[4], env.oxygen_x10);
    valid_flags |= 0x0007U;
  }
  if (AutoDevice_GetSensorData(&sensors) != 0U)
  {
    write_u16_le(&payload_out[8], (sensors.inlet_temperature_valid != 0U) ?
                 (uint16_t)sensors.inlet_temperature_x10 : 0xFFFFU);
    write_u16_le(&payload_out[10], (sensors.tank_temperature_valid != 0U) ?
                 (uint16_t)sensors.tank_temperature_x10 : 0xFFFFU);
    write_u16_le(&payload_out[12], (sensors.pipe_temperature_valid != 0U) ?
                 (uint16_t)sensors.pipe_temperature_x10 : 0xFFFFU);
    write_u16_le(&payload_out[14], (sensors.spare_temperature_valid != 0U) ?
                 (uint16_t)sensors.spare_temperature_x10 : 0xFFFFU);
    if (sensors.inlet_temperature_valid != 0U) valid_flags |= 0x0010U;
    if (sensors.tank_temperature_valid != 0U) valid_flags |= 0x0020U;
    if (sensors.pipe_temperature_valid != 0U) valid_flags |= 0x0040U;
    if (sensors.spare_temperature_valid != 0U) valid_flags |= 0x0080U;
  }
  else
  {
    write_u16_le(&payload_out[8], 0xFFFFU);
    write_u16_le(&payload_out[10], 0xFFFFU);
    write_u16_le(&payload_out[12], 0xFFFFU);
    write_u16_le(&payload_out[14], 0xFFFFU);
  }
  write_u16_le(&payload_out[16], (uint16_t)env.incab_temperature_x10);
  write_u16_le(&payload_out[18], env.incab_humidity_x10);
  write_u16_le(&payload_out[20], env.ozone_x100);
  if (env.incab_valid != 0U) valid_flags |= 0x0300U;
  if (env.ozone_valid != 0U) valid_flags |= 0x0400U;
  write_u16_le(&payload_out[6], 0U);
  write_u16_le(&payload_out[22], valid_flags);
  payload_out[24] = TaskComm485_GetLastPercent(RS485_STATION_LEFT_FAN);
  payload_out[25] = TaskComm485_GetLastPercent(RS485_STATION_RIGHT_FAN);
  payload_out[26] = TaskComm485_GetLastPercent(RS485_STATION_BOTTOM_FAN);
  payload_out[27] = TaskComm485_GetLastPercent(RS485_STATION_LEFT_HEATER);
  payload_out[28] = TaskComm485_GetLastPercent(RS485_STATION_RIGHT_HEATER);
  payload_out[29] = TaskComm485_GetLastPercent(RS485_STATION_BOTTOM_HEATER);
  payload_out[30] = TaskComm485_GetLastProportionalValveAngle();
  payload_out[31] = TaskComm485_GetLastPercent(RS485_STATION_TOP_EXHAUST_FAN);
  payload_out[32] = TaskComm485_GetLastBoosterPumpPercent();
  /* 尾部追加流程时间统计：总剩余时间和当前小流程剩余时间，单位毫秒，uint32 小端。 */
  write_u32_le(&payload_out[33], AutoProcess_GetRemainingTimeMs());
  write_u32_le(&payload_out[37], AutoProcess_GetCurrentStageRemainingTimeMs());
  send_frame(APP_BUSINESS_RSP_TELEMETRY, sequence, payload_out, sizeof(payload_out));
  telemetry_last_sequence = sequence;
  telemetry_ack_received = 0U;
}
#endif

static void handle_host_ack(uint8_t sequence,
                            const uint8_t *data,
                            uint16_t length)
{
#if (APP_IAP_TELEMETRY_ENABLE != 0U)
  if ((length >= 6U) &&
      (data[0] == APP_BUSINESS_RSP_TELEMETRY) &&
      (data[1] == IAP_STATUS_OK) &&
      (sequence == telemetry_last_sequence))
  {
    telemetry_ack_received = 1U;
    telemetry_ack_tick = HAL_GetTick();
  }
#else
  (void)sequence;
  (void)data;
  (void)length;
#endif
}

static void handle_host_nack(uint8_t sequence,
                             const uint8_t *data,
                             uint16_t length)
{
  (void)sequence;
  (void)data;
  (void)length;
}

static void build_io_status_payload(uint8_t payload_out[APP_IO_PAYLOAD_SIZE])
{
  AutoInputPoint input;
  AutoOutputPoint output;
  uint16_t index = 0U;

  memset(payload_out, 0, APP_IO_PAYLOAD_SIZE);
  payload_out[0] = (uint8_t)AUTO_INPUT_COUNT;
  payload_out[1] = (uint8_t)APP_OUTPUT_POINT_COUNT;

  for (input = AUTO_INPUT_DISINFECTANT_LOW; input < AUTO_INPUT_COUNT; ++input)
  {
    if (AutoDevice_ReadInput(input) != 0U)
    {
      payload_out[2U + (index / 8U)] |=
          (uint8_t)(1U << (index % 8U));
    }
    index++;
  }

  index = 0U;
  for (output = AUTO_OUTPUT_EXHAUST_FAN; output < AUTO_OUTPUT_COUNT; ++output)
  {
    if (TaskOutput_GetActual(output) != 0U)
    {
      payload_out[2U + APP_INPUT_BITMAP_SIZE + (index / 8U)] |=
          (uint8_t)(1U << (index % 8U));
    }
    index++;
  }
}

static void send_io_status_payload(uint8_t sequence,
                                   const uint8_t payload_out[APP_IO_PAYLOAD_SIZE])
{
  send_frame(APP_BUSINESS_RSP_IO_STATUS, sequence,
             payload_out, APP_IO_PAYLOAD_SIZE);
  memcpy(last_io_payload, payload_out, APP_IO_PAYLOAD_SIZE);
  io_snapshot_valid = 1U;
  io_heartbeat_tick = HAL_GetTick();
}

static void send_io_status(uint8_t sequence)
{
  uint8_t payload_out[APP_IO_PAYLOAD_SIZE];

  build_io_status_payload(payload_out);
  send_io_status_payload(sequence, payload_out);
}

static void send_fault_event(uint8_t sequence, AutoReport report)
{
  uint8_t payload_out[12];
  uint8_t severity = 2U;
  uint16_t primary_fault = (uint16_t)AutoDevice_GetFault();
  uint32_t event_bitmap = fault_bitmap;

  memset(payload_out, 0, sizeof(payload_out));
  fault_event_id++;
  write_u32_le(&payload_out[0], fault_event_id);
  payload_out[4] = 1U;

  if (report == AUTO_REPORT_SENSOR_FAULT)
  {
    severity = 3U;
    if (primary_fault == 0U)
    {
      primary_fault = 1U;
    }
    event_bitmap |= (1UL << 5);
  }
  else if (report == AUTO_REPORT_TIMEOUT_FAULT)
  {
    severity = 2U;
    if (primary_fault == 0U)
    {
      primary_fault = 2U;
    }
    event_bitmap |= (1UL << 16);
  }

  payload_out[5] = severity;
  write_u16_le(&payload_out[6], primary_fault);
  write_u32_le(&payload_out[8], event_bitmap);
  fault_bitmap = event_bitmap;
  send_frame(APP_BUSINESS_RSP_FAULT_EVENT, sequence, payload_out, sizeof(payload_out));
}

static void send_interaction_request(uint8_t sequence)
{
  uint8_t payload_out[8];

  memset(payload_out, 0, sizeof(payload_out));
  write_u32_le(&payload_out[0], interaction_request_id);
  payload_out[4] = interaction_type_pending;
  payload_out[5] = interaction_stage_id;
  write_u16_le(&payload_out[6], interaction_step_id);
  send_frame(APP_BUSINESS_RSP_INTERACTION, sequence, payload_out, sizeof(payload_out));
  interaction_tick = HAL_GetTick();
}

static void handle_query_status(uint8_t sequence, const uint8_t *data, uint16_t length)
{
  if (length != 0U)
  {
    send_nack(APP_BUSINESS_CMD_QUERY_STATUS, sequence, IAP_ERR_BAD_LENGTH, sequence, 0U);
    return;
  }
  send_device_status_with_seq(sequence);
}

static void lock_cabin_door_locks_for_auto_mode(void)
{
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_UPPER_LOCK, 1U);
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_LOWER_LOCK, 1U);
}

static void handle_set_work_mode(uint8_t sequence, const uint8_t *data, uint16_t length)
{
  uint8_t target_mode;

  if (length < 2U)
  {
    send_nack(APP_BUSINESS_CMD_SET_WORK_MODE, sequence, IAP_ERR_BAD_LENGTH, sequence, 0U);
    return;
  }

  target_mode = data[0];
  if ((target_mode != 0U) && (target_mode != 1U) &&
      (target_mode != 2U) && (target_mode != 3U))
  {
    send_nack(APP_BUSINESS_CMD_SET_WORK_MODE, sequence, IAP_ERR_BAD_CMD, sequence, 0U);
    return;
  }

  if (target_mode == 2U)
  {
    /* 急停时即使设备先前已经处于自动模式，也必须拒绝自动模式命令。 */
    if (AutoDevice_IsEmergencyStopped() != 0U)
    {
      send_nack(APP_BUSINESS_CMD_SET_WORK_MODE, sequence, IAP_ERR_BAD_STATE, sequence, 0U);
      return;
    }

    /* Re-sending auto mode while already in auto should be a no-op. */
    if (TaskOutput_IsAutoMode() != 0U)
    {
      lock_cabin_door_locks_for_auto_mode();
      send_ack(APP_BUSINESS_CMD_SET_WORK_MODE, sequence, (uint32_t)get_work_mode());
      return;
    }

    AutoDevice_SetPaused(0U);
    AutoDevice_SetStopRequested(0U);
    /*
     * 从手动切回自动时先撤销手动485遗留功率。清零完成前底层禁止非零命令；
     * 后续真正启动自动流程时，start_stage() 才会重新允许自动485控制。
     */
    TaskComm485_InhibitNonzero(COMM485_INHIBIT_MODE_CHANGE);
    TaskComm485_RequestSafeStop();
    AutoProcess_RequestReset();
    TaskOutput_SetAutoMode(1U);
    lock_cabin_door_locks_for_auto_mode();
  }
  else
  {
    TaskComm485_InhibitNonzero(COMM485_INHIBIT_MODE_CHANGE);
    TaskComm485_RequestSafeStop();
    TaskClean_Stop();
    AutoDevice_SetPaused(0U);
    AutoDevice_SetStopRequested(0U);
    AutoProcess_RequestReset();
    TaskOutput_ClearSource(APP_OUTPUT_SOURCE_AUTO);
    TaskOutput_SetAutoMode(0U);
  }

  send_ack(APP_BUSINESS_CMD_SET_WORK_MODE, sequence, (uint32_t)get_work_mode());
}

static void handle_process_control(uint8_t sequence, const uint8_t *data, uint16_t length)
{
  uint16_t action;
  uint16_t stage_id;

  if (length < 4U)
  {
    send_nack(APP_BUSINESS_CMD_PROCESS_CONTROL, sequence, IAP_ERR_BAD_LENGTH, sequence, 0U);
    return;
  }

  action = read_u16_le(&data[0]);
  stage_id = read_u16_le(&data[2]);

  /* 急停期间拒绝启动、暂停、继续、结束、单流程及自清洁等所有流程命令。 */
  if (AutoDevice_IsEmergencyStopped() != 0U)
  {
    send_nack(APP_BUSINESS_CMD_PROCESS_CONTROL, sequence,
              IAP_ERR_BAD_STATE, sequence, action);
    return;
  }

  switch (action)
  {
    case 1U:
      if ((TaskOutput_IsAutoMode() == 0U) ||
          (AutoProcess_IsFailed() != 0U) ||
          (TaskClean_IsFailed() != 0U))
      {
        send_nack(APP_BUSINESS_CMD_PROCESS_CONTROL, sequence, IAP_ERR_BAD_STATE, sequence, 0U);
        break;
      }
      if ((AutoProcess_IsRunning() != 0U) ||
          (TaskClean_IsRunning() != 0U))
      {
        send_nack(APP_BUSINESS_CMD_PROCESS_CONTROL, sequence,
                  IAP_ERR_BUSY, sequence, 0U);
        break;
      }
      AutoDevice_SetPaused(0U);
      AutoProcess_RequestReset();
      if (TaskOutput_Sync(APP_OUTPUT_SYNC_TIMEOUT_MS) != HAL_OK)
      {
        send_nack(APP_BUSINESS_CMD_PROCESS_CONTROL, sequence,
                  IAP_ERR_BUSY, sequence, action);
        break;
      }
      AutoProcess_RequestFull();
      send_ack(APP_BUSINESS_CMD_PROCESS_CONTROL, sequence, (uint32_t)1U);
      break;

    case 2U:
      if ((TaskOutput_IsAutoMode() == 0U) ||
          ((AutoProcess_IsRunning() == 0U) &&
           (TaskClean_IsRunning() == 0U)))
      {
        send_nack(APP_BUSINESS_CMD_PROCESS_CONTROL, sequence,
                  IAP_ERR_BAD_STATE, sequence, 0U);
        break;
      }
      TaskComm485_InhibitNonzero(COMM485_INHIBIT_PAUSE);
      AutoDevice_SetPaused(1U);
      AutoDevice_AllDangerousOutputsOff();
      send_ack(APP_BUSINESS_CMD_PROCESS_CONTROL, sequence, (uint32_t)2U);
      break;

    case 3U:
      if ((TaskOutput_IsAutoMode() == 0U) ||
          (AutoDevice_IsPaused() == 0U) ||
          ((AutoProcess_IsRunning() == 0U) &&
           (TaskClean_IsRunning() == 0U)))
      {
        send_nack(APP_BUSINESS_CMD_PROCESS_CONTROL, sequence,
                  IAP_ERR_BAD_STATE, sequence, 0U);
        break;
      }
      /* 继续只解除暂停，保留当前流程、当前步骤和等待计时状态。
       * 不能重启当前阶段，否则暂停在 120 步后继续会重新回到 10 步。
       */
      if (AutoProcess_IsRunning() != 0U)
      {
        AutoProcess_RequestResume();
      }
      else
      {
        TaskClean_RequestResume();
      }
      send_ack(APP_BUSINESS_CMD_PROCESS_CONTROL, sequence, (uint32_t)3U);
      break;

    case 4U:
      if (TaskOutput_IsAutoMode() == 0U)
      {
        send_nack(APP_BUSINESS_CMD_PROCESS_CONTROL, sequence,
                  IAP_ERR_BAD_STATE, sequence, 0U);
        break;
      }
      TaskClean_Stop();
      AutoDevice_AllDangerousOutputsOff();
      if (TaskOutput_ClearSourceSync(APP_OUTPUT_SOURCE_AUTO,
                                     APP_OUTPUT_SYNC_TIMEOUT_MS) != HAL_OK)
      {
        send_nack(APP_BUSINESS_CMD_PROCESS_CONTROL, sequence,
                  IAP_ERR_BUSY, sequence, action);
        break;
      }
      AutoDevice_SetPaused(0U);
      AutoDevice_SetStopRequested(0U);
      AutoProcess_RequestReset();
      send_ack(APP_BUSINESS_CMD_PROCESS_CONTROL, sequence, (uint32_t)4U);
      break;

    case 5U:
      if (stage_id >= (uint16_t)AUTO_PROCESS_STAGE_COUNT)
      {
        send_nack(APP_BUSINESS_CMD_PROCESS_CONTROL, sequence,
                  IAP_ERR_CONFIG, sequence, stage_id);
        break;
      }
      if (TaskOutput_IsAutoMode() == 0U)
      {
        send_nack(APP_BUSINESS_CMD_PROCESS_CONTROL, sequence,
                  IAP_ERR_BAD_STATE, sequence, stage_id);
        break;
      }
      if ((AutoProcess_IsRunning() != 0U) ||
          (TaskClean_IsRunning() != 0U))
      {
        send_nack(APP_BUSINESS_CMD_PROCESS_CONTROL, sequence,
                  IAP_ERR_BUSY, sequence, stage_id);
        break;
      }
      AutoDevice_AllDangerousOutputsOff();
      if (TaskOutput_ClearSourceSync(APP_OUTPUT_SOURCE_AUTO,
                                     APP_OUTPUT_SYNC_TIMEOUT_MS) != HAL_OK)
      {
        send_nack(APP_BUSINESS_CMD_PROCESS_CONTROL, sequence,
                  IAP_ERR_BUSY, sequence, stage_id);
        break;
      }
      AutoDevice_SetPaused(0U);
      AutoDevice_SetStopRequested(0U);
      AutoProcess_RequestReset();
      AutoProcess_RequestStageTest((AutoProcessStage)stage_id);
      send_ack(APP_BUSINESS_CMD_PROCESS_CONTROL, sequence, (uint32_t)stage_id);
      break;

    case 6U:
      if ((TaskOutput_IsAutoMode() == 0U) ||
          (TaskClean_IsFailed() != 0U))
      {
        send_nack(APP_BUSINESS_CMD_PROCESS_CONTROL, sequence,
                  IAP_ERR_BAD_STATE, sequence, 0U);
        break;
      }
      if ((AutoProcess_IsRunning() != 0U) ||
          (TaskClean_IsRunning() != 0U))
      {
        send_nack(APP_BUSINESS_CMD_PROCESS_CONTROL, sequence,
                  IAP_ERR_BUSY, sequence, 0U);
        break;
      }
      AutoDevice_AllDangerousOutputsOff();
      if (TaskOutput_ClearSourceSync(APP_OUTPUT_SOURCE_AUTO,
                                     APP_OUTPUT_SYNC_TIMEOUT_MS) != HAL_OK)
      {
        send_nack(APP_BUSINESS_CMD_PROCESS_CONTROL, sequence,
                  IAP_ERR_BUSY, sequence, action);
        break;
      }
      AutoDevice_SetPaused(0U);
      AutoDevice_SetStopRequested(0U);
      AutoProcess_RequestReset();
      TaskClean_Request();
      send_ack(APP_BUSINESS_CMD_PROCESS_CONTROL, sequence, (uint32_t)6U);
      break;

    case 7U:
      fault_bitmap = 0U;
      AutoDevice_ClearFault();
      TaskClean_Stop();
      TaskClean_ClearFault();
      AutoDevice_SetPaused(0U);
      AutoDevice_SetStopRequested(0U);
      AutoProcess_RequestReset();
      send_ack(APP_BUSINESS_CMD_PROCESS_CONTROL, sequence, (uint32_t)7U);
      break;

    case 8U:
      if ((TaskOutput_IsAutoMode() == 0U) ||
          (AutoProcess_IsRunning() == 0U) ||
          (TaskClean_IsRunning() != 0U))
      {
        send_nack(APP_BUSINESS_CMD_PROCESS_CONTROL, sequence,
                  IAP_ERR_BAD_STATE, sequence, 0U);
        break;
      }
      TaskClean_Stop();
      AutoDevice_SetPaused(0U);
      AutoDevice_SetStopRequested(0U);
      AutoDevice_AllDangerousOutputsOff();
      if (TaskOutput_ClearSourceSync(APP_OUTPUT_SOURCE_AUTO,
                                     APP_OUTPUT_SYNC_TIMEOUT_MS) != HAL_OK)
      {
        send_nack(APP_BUSINESS_CMD_PROCESS_CONTROL, sequence,
                  IAP_ERR_BUSY, sequence, action);
        break;
      }
      AutoProcess_RequestTerminateServiceToExit();
      send_ack(APP_BUSINESS_CMD_PROCESS_CONTROL, sequence, (uint32_t)8U);
      break;

    default:
      send_nack(APP_BUSINESS_CMD_PROCESS_CONTROL, sequence, IAP_ERR_BAD_CMD, sequence, 0U);
      break;
  }
}

static void handle_process_interaction(uint8_t sequence,
                                       const uint8_t *data,
                                       uint16_t length)
{
  uint32_t request_id;
  uint8_t interaction_type;
  uint8_t result;
  uint16_t value;

  if (length < 8U)
  {
    send_nack(APP_BUSINESS_CMD_PROCESS_INTERACT, sequence, IAP_ERR_BAD_LENGTH, sequence, 0U);
    return;
  }

  request_id = read_u32_le(&data[0]);
  interaction_type = data[4];
  result = data[5];
  value = read_u16_le(&data[6]);

  /* 急停期间不接收开关门、继续及配方确认等流程交互。 */
  if (AutoDevice_IsEmergencyStopped() != 0U)
  {
    send_nack(APP_BUSINESS_CMD_PROCESS_INTERACT, sequence,
              IAP_ERR_BAD_STATE, sequence, request_id);
    return;
  }

  if (interaction_pending != 0U)
  {
    if ((request_id != interaction_request_id) ||
        ((interaction_type != interaction_type_pending) &&
         !((interaction_type == 7U) &&
           ((interaction_type_pending == 3U) ||
            (interaction_type_pending == 4U)))))
    {
      send_nack(APP_BUSINESS_CMD_PROCESS_INTERACT, sequence,
                IAP_ERR_CONFIG, sequence, interaction_request_id);
      return;
    }
  }

  if (result == 0U)
  {
    if (interaction_pending != 0U)
    {
      interaction_pending = 0U;
    }
    send_ack(APP_BUSINESS_CMD_PROCESS_INTERACT, sequence, request_id);
    return;
  }

  switch (interaction_type)
  {
    case 1U:
      AutoDevice_SetCommand(AUTO_COMMAND_OPEN_UPPER_DOOR);
      break;

    case 2U:
      AutoDevice_SetCommand(AUTO_COMMAND_OPEN_LOWER_DOOR);
      break;

    case 3U:
      AutoDevice_SetCommand(AUTO_COMMAND_CLOSE_LOWER_DOOR);
      break;

    case 4U:
      AutoDevice_SetCommand(AUTO_COMMAND_CLOSE_UPPER_DOOR);
      break;

    case 5U:
      AutoDevice_SetCommand(AUTO_COMMAND_CONTINUE);
      break;

    case 6U:
      if (value >= APP_RECIPE_COUNT)
      {
      send_nack(APP_BUSINESS_CMD_PROCESS_INTERACT, sequence,
                  IAP_ERR_CONFIG, sequence, request_id);
        return;
      }
      AppParam_SelectActiveRecipe((AppRecipeId)value);
      AutoDevice_SetRecipeSelectedFlag();
      break;

    case 7U:
      AutoDevice_SetCommand(AUTO_COMMAND_AUTO_CLOSE_DOOR);
      break;

    default:
      send_nack(APP_BUSINESS_CMD_PROCESS_INTERACT, sequence, IAP_ERR_BAD_CMD, sequence, request_id);
      return;
  }

  if (interaction_pending != 0U)
  {
    interaction_pending = 0U;
  }
  send_ack(APP_BUSINESS_CMD_PROCESS_INTERACT, sequence, request_id);
}

static void handle_manual_output(uint8_t sequence, const uint8_t *data, uint16_t length)
{
  uint16_t output_id;
  uint8_t action;

  if (length < 8U)
  {
    send_nack(APP_BUSINESS_CMD_MANUAL_OUTPUT, sequence, IAP_ERR_BAD_LENGTH, sequence, 0U);
    return;
  }

  if (TaskOutput_IsAutoMode() != 0U)
  {
    send_nack(APP_BUSINESS_CMD_MANUAL_OUTPUT, sequence, IAP_ERR_BAD_STATE, sequence, 0U);
    return;
  }

  if (AutoDevice_IsEmergencyStopped() != 0U)
  {
    send_nack(APP_BUSINESS_CMD_MANUAL_OUTPUT, sequence,
              IAP_ERR_BAD_STATE, sequence, 0U);
    return;
  }

  output_id = read_u16_le(&data[0]);
  action = data[2];
  if ((output_id < (uint16_t)AUTO_OUTPUT_EXHAUST_FAN) ||
      (output_id >= (uint16_t)AUTO_OUTPUT_COUNT))
  {
    send_nack(APP_BUSINESS_CMD_MANUAL_OUTPUT, sequence, IAP_ERR_CONFIG, sequence, 0U);
    return;
  }

  if (action > 1U)
  {
    send_nack(APP_BUSINESS_CMD_MANUAL_OUTPUT, sequence, IAP_ERR_CONFIG, sequence, 0U);
    return;
  }

  if (TaskOutput_Request(APP_OUTPUT_SOURCE_MANUAL,
                         (AutoOutputPoint)output_id,
                         action) != HAL_OK)
  {
    send_nack(APP_BUSINESS_CMD_MANUAL_OUTPUT, sequence, IAP_ERR_BUSY, sequence, 0U);
    return;
  }

  send_ack(APP_BUSINESS_CMD_MANUAL_OUTPUT, sequence, (uint32_t)output_id);
}

/*
 * 手动控制RS485执行器：
 * 数据区为“站号uint16小端 + 数值uint16小端”。
 * - 站号1～6：数值为可控硅百分比，范围0～100。
 * - 站号11：数值为比例阀角度，范围0～90。
 */
static void handle_manual_rs485(uint8_t sequence,
                                const uint8_t *data,
                                uint16_t length)
{
  uint16_t station;
  uint16_t value;
  HAL_StatusTypeDef status;

  if (length != 4U)
  {
    send_nack(APP_BUSINESS_CMD_MANUAL_RS485, sequence,
              IAP_ERR_BAD_LENGTH, sequence, 0U);
    return;
  }

  if ((TaskOutput_IsAutoMode() != 0U) ||
      (AutoDevice_IsEmergencyStopped() != 0U))
  {
    send_nack(APP_BUSINESS_CMD_MANUAL_RS485, sequence,
              IAP_ERR_BAD_STATE, sequence, 0U);
    return;
  }

  station = read_u16_le(&data[0]);
  value = read_u16_le(&data[2]);

  if ((station >= RS485_STATION_LEFT_FAN) &&
      (station <= RS485_STATION_BOTTOM_HEATER))
  {
    if (value > 100U)
    {
      send_nack(APP_BUSINESS_CMD_MANUAL_RS485, sequence,
                IAP_ERR_CONFIG, sequence, station);
      return;
    }

    if (value != 0U)
    {
      TaskComm485_ReleaseNonzero(COMM485_INHIBIT_STARTUP |
                                 COMM485_INHIBIT_SAFE_STOP |
                                 COMM485_INHIBIT_MODE_CHANGE);
    }
    status = TaskComm485_RequestSetPercent((uint8_t)station,
                                           (uint8_t)value);
  }
  else if (station == RS485_STATION_BOOSTER_PUMP)
  {
    if (value > 100U)
    {
      send_nack(APP_BUSINESS_CMD_MANUAL_RS485, sequence,
                IAP_ERR_CONFIG, sequence, station);
      return;
    }

    if (value != 0U)
    {
      TaskComm485_ReleaseNonzero(COMM485_INHIBIT_STARTUP |
                                 COMM485_INHIBIT_SAFE_STOP |
                                 COMM485_INHIBIT_MODE_CHANGE);
    }
    status = TaskComm485_RequestSetBoosterPumpPercent((uint8_t)value);
  }
  else if (station == RS485_STATION_PROPORTIONAL_VALVE)
  {
    TaskComm485_ReleaseNonzero(COMM485_INHIBIT_STARTUP |
                               COMM485_INHIBIT_SAFE_STOP |
                               COMM485_INHIBIT_MODE_CHANGE);
    if (value > 90U)
    {
      send_nack(APP_BUSINESS_CMD_MANUAL_RS485, sequence,
                IAP_ERR_CONFIG, sequence, station);
      return;
    }
    status = TaskComm485_RequestSetProportionalValveAngle((uint8_t)value);
  }
  else
  {
    send_nack(APP_BUSINESS_CMD_MANUAL_RS485, sequence,
              IAP_ERR_CONFIG, sequence, station);
    return;
  }

  if (status != HAL_OK)
  {
    send_nack(APP_BUSINESS_CMD_MANUAL_RS485, sequence,
              IAP_ERR_BUSY, sequence, station);
    return;
  }

  send_ack(APP_BUSINESS_CMD_MANUAL_RS485, sequence, station);
}

static void handle_set_actuator_mode(uint8_t sequence,
                                     const uint8_t *data,
                                     uint16_t length)
{
  uint16_t mode;

  if (length < 2U)
  {
    send_nack(APP_BUSINESS_CMD_SET_ACTUATOR_MODE, sequence,
              IAP_ERR_BAD_LENGTH, sequence, 0U);
    return;
  }

  mode = read_u16_le(&data[0]);
  if (mode > 1U)
  {
    send_nack(APP_BUSINESS_CMD_SET_ACTUATOR_MODE, sequence,
              IAP_ERR_CONFIG, sequence, mode);
    return;
  }

  if (mode == 0U)
  {
    (void)TaskComm485_RequestSetPercent(RS485_STATION_TOP_EXHAUST_FAN, 0U);
  }
  TaskComm485_SetRs485ActuatorMode((uint8_t)mode);
  send_ack(APP_BUSINESS_CMD_SET_ACTUATOR_MODE,
           sequence,
           (uint32_t)TaskComm485_IsRs485ActuatorMode());
}

static void handle_booster_control(uint8_t sequence,
                                   const uint8_t *data,
                                   uint16_t length)
{
  uint16_t enabled;
  HAL_StatusTypeDef status;

  if (length < 2U)
  {
    send_nack(APP_BUSINESS_CMD_BOOSTER_CONTROL, sequence,
              IAP_ERR_BAD_LENGTH, sequence, 0U);
    return;
  }

  enabled = read_u16_le(&data[0]);
  if (enabled > 1U)
  {
    send_nack(APP_BUSINESS_CMD_BOOSTER_CONTROL, sequence,
              IAP_ERR_CONFIG, sequence, enabled);
    return;
  }

  if (enabled != 0U)
  {
    TaskComm485_ReleaseNonzero(COMM485_INHIBIT_STARTUP |
                               COMM485_INHIBIT_SAFE_STOP |
                               COMM485_INHIBIT_MODE_CHANGE);
  }
  status = TaskComm485_RequestSetBoosterPumpRunning((uint8_t)enabled);
  if (status != HAL_OK)
  {
    send_nack(APP_BUSINESS_CMD_BOOSTER_CONTROL, sequence,
              IAP_ERR_BUSY, sequence, enabled);
    return;
  }

  send_ack(APP_BUSINESS_CMD_BOOSTER_CONTROL, sequence, enabled);
}

static void handle_query_io_status(uint8_t sequence, const uint8_t *data, uint16_t length)
{
  if (length != 0U)
  {
    send_nack(APP_BUSINESS_CMD_QUERY_IO_STATUS, sequence, IAP_ERR_BAD_LENGTH, sequence, 0U);
    return;
  }

  send_io_status(sequence);
}

static void handle_get_param_block(uint8_t sequence, const uint8_t *data, uint16_t length)
{
  if (length < 2U)
  {
    send_nack(APP_BUSINESS_CMD_GET_PARAM_BLOCK, sequence, IAP_ERR_BAD_LENGTH, sequence, 0U);
    return;
  }

  send_param_block_response(sequence, data[0], data[1]);
}

static void handle_set_param_block(uint8_t sequence, const uint8_t *data, uint16_t length)
{
  uint8_t group;
  uint8_t index;
  uint16_t protocol_version;
  uint32_t revision;
  uint16_t data_length;

  if (length < 10U)
  {
    send_nack(APP_BUSINESS_CMD_SET_PARAM_BLOCK, sequence, IAP_ERR_BAD_LENGTH, sequence, 0U);
    return;
  }

  group = data[0];
  index = data[1];
  protocol_version = read_u16_le(&data[2]);
  revision = read_u32_le(&data[4]);
  data_length = read_u16_le(&data[8]);

  if (protocol_version != 1U)
  {
    send_nack(APP_BUSINESS_CMD_SET_PARAM_BLOCK, sequence, IAP_ERR_CONFIG, sequence, 0U);
    return;
  }
  if (revision != AppParam_GetRevision())
  {
    send_nack(APP_BUSINESS_CMD_SET_PARAM_BLOCK, sequence, IAP_ERR_SEQ_CONFLICT, sequence,
              AppParam_GetRevision());
    return;
  }

  if ((group == 0U) && (data_length == 16U) && (index == 0U))
  {
    AppParam_SetGlobalConfig(data[10], data[11],
                             read_u16_le(&data[12]),
                             read_u32_le(&data[14]),
                             read_u32_le(&data[18]));
    AppParam_SetRevision(revision + 1U);
    send_ack(APP_BUSINESS_CMD_SET_PARAM_BLOCK, sequence, revision + 1U);
    return;
  }

  if ((group == 1U) && (data_length == sizeof(AppRecipe_t)) && (index < APP_RECIPE_COUNT))
  {
    AppRecipe_t recipe;

    memcpy(&recipe, &data[10], sizeof(recipe));
    if (AppParam_SetRecipeDirect((AppRecipeId)index, &recipe) != HAL_OK)
    {
      send_nack(APP_BUSINESS_CMD_SET_PARAM_BLOCK, sequence, IAP_ERR_CONFIG, sequence, 0U);
      return;
    }
    AppParam_SetRevision(revision + 1U);
    send_ack(APP_BUSINESS_CMD_SET_PARAM_BLOCK, sequence, revision + 1U);
    return;
  }

  send_nack(APP_BUSINESS_CMD_SET_PARAM_BLOCK, sequence, IAP_ERR_CONFIG, sequence, 0U);
}

static void handle_save_params(uint8_t sequence, const uint8_t *data, uint16_t length)
{
  uint32_t revision;

  if (length < 4U)
  {
    send_nack(APP_BUSINESS_CMD_SAVE_PARAMS, sequence, IAP_ERR_BAD_LENGTH, sequence, 0U);
    return;
  }

  revision = read_u32_le(&data[0]);
  if (revision != AppParam_GetRevision())
  {
    send_nack(APP_BUSINESS_CMD_SAVE_PARAMS, sequence, IAP_ERR_SEQ_CONFLICT, sequence,
              AppParam_GetRevision());
    return;
  }

  if (AppParam_CanSaveNow() == 0U)
  {
    send_nack(APP_BUSINESS_CMD_SAVE_PARAMS, sequence, IAP_ERR_BUSY, sequence, revision);
    return;
  }

  if (AppParam_SaveNow() != HAL_OK)
  {
    send_nack(APP_BUSINESS_CMD_SAVE_PARAMS, sequence, IAP_ERR_FLASH, sequence, revision);
    return;
  }

  send_ack(APP_BUSINESS_CMD_SAVE_PARAMS, sequence, revision);
}

static void handle_restore_defaults(uint8_t sequence, const uint8_t *data, uint16_t length)
{
  if ((length < 4U) ||
      (data[0] != (uint8_t)'D') ||
      (data[1] != (uint8_t)'F') ||
      (data[2] != (uint8_t)'L') ||
      (data[3] != (uint8_t)'T'))
  {
    send_nack(APP_BUSINESS_CMD_RESTORE_DEFAULTS, sequence, IAP_ERR_BAD_LENGTH, sequence, 0U);
    return;
  }

  if ((AutoProcess_IsRunning() != 0U) || (TaskClean_IsRunning() != 0U))
  {
    send_nack(APP_BUSINESS_CMD_RESTORE_DEFAULTS, sequence,
              IAP_ERR_BUSY, sequence, 0U);
    return;
  }

  AppParam_RestoreDefaults();
  send_ack(APP_BUSINESS_CMD_RESTORE_DEFAULTS, sequence, AppParam_GetRevision());
}

static void process_business_packet(uint8_t command, uint8_t sequence)
{
  switch (command)
  {
    case APP_BUSINESS_CMD_QUERY_STATUS:
      handle_query_status(sequence, payload, payload_length);
      break;

    case APP_BUSINESS_CMD_SET_WORK_MODE:
      handle_set_work_mode(sequence, payload, payload_length);
      break;

    case APP_BUSINESS_CMD_PROCESS_CONTROL:
      handle_process_control(sequence, payload, payload_length);
      break;

    case APP_BUSINESS_CMD_PROCESS_INTERACT:
      handle_process_interaction(sequence, payload, payload_length);
      break;

    case APP_BUSINESS_CMD_MANUAL_OUTPUT:
      handle_manual_output(sequence, payload, payload_length);
      break;

    case APP_BUSINESS_CMD_QUERY_IO_STATUS:
      handle_query_io_status(sequence, payload, payload_length);
      break;

    case APP_BUSINESS_CMD_MANUAL_RS485:
      handle_manual_rs485(sequence, payload, payload_length);
      break;

    case APP_BUSINESS_CMD_SET_ACTUATOR_MODE:
      handle_set_actuator_mode(sequence, payload, payload_length);
      break;

    case APP_BUSINESS_CMD_BOOSTER_CONTROL:
      handle_booster_control(sequence, payload, payload_length);
      break;

    case APP_BUSINESS_CMD_GET_PARAM_BLOCK:
      handle_get_param_block(sequence, payload, payload_length);
      break;

    case APP_BUSINESS_CMD_SET_PARAM_BLOCK:
      handle_set_param_block(sequence, payload, payload_length);
      break;

    case APP_BUSINESS_CMD_SAVE_PARAMS:
      handle_save_params(sequence, payload, payload_length);
      break;

    case APP_BUSINESS_CMD_RESTORE_DEFAULTS:
      handle_restore_defaults(sequence, payload, payload_length);
      break;

    default:
      send_nack(command, sequence, IAP_ERR_BAD_CMD, sequence, 0U);
      break;
  }
}

static void process_packet(void)
{
  uint8_t command = header[0];
  uint8_t sequence = header[1];

  if (packet_crc_is_valid() == 0U)
  {
    send_nack(command, sequence, IAP_ERR_BAD_CRC16, sequence, 0U);
    return;
  }

  switch (command)
  {
    case IAP_CMD_QUERY_INFO:
      if (payload_length == 0U)
      {
        send_device_info(sequence);
      }
      else
      {
        send_nack(command, sequence, IAP_ERR_BAD_LENGTH, sequence, 0U);
      }
      break;

    case IAP_CMD_ENTER_UPGRADE:
      if (enter_magic_is_valid() != 0U)
      {
        enter_bootloader(sequence);
      }
      else
      {
        send_nack(command, sequence, IAP_ERR_BAD_LENGTH, sequence, 0U);
      }
      break;

    case IAP_CMD_APP_START_CONFIRM:
      send_ack(command, sequence, 0U);
      break;

    case IAP_RSP_ACK:
      handle_host_ack(sequence, payload, payload_length);
      break;

    case IAP_RSP_NACK:
      handle_host_nack(sequence, payload, payload_length);
      break;

    case APP_BUSINESS_CMD_QUERY_STATUS:
    case APP_BUSINESS_CMD_SET_WORK_MODE:
    case APP_BUSINESS_CMD_PROCESS_CONTROL:
    case APP_BUSINESS_CMD_PROCESS_INTERACT:
    case APP_BUSINESS_CMD_MANUAL_OUTPUT:
    case APP_BUSINESS_CMD_QUERY_IO_STATUS:
    case APP_BUSINESS_CMD_MANUAL_RS485:
    case APP_BUSINESS_CMD_SET_ACTUATOR_MODE:
    case APP_BUSINESS_CMD_BOOSTER_CONTROL:
    case APP_BUSINESS_CMD_GET_PARAM_BLOCK:
    case APP_BUSINESS_CMD_SET_PARAM_BLOCK:
    case APP_BUSINESS_CMD_SAVE_PARAMS:
    case APP_BUSINESS_CMD_RESTORE_DEFAULTS:
      process_business_packet(command, sequence);
      break;

    default:
      send_nack(command, sequence, IAP_ERR_BAD_CMD, sequence, 0U);
      break;
  }
}

void AppIap_Init(UART_HandleTypeDef *huart)
{
  uint32_t now;

  app_iap_uart = huart;
  if (tx_mutex == NULL)
  {
    tx_mutex = osMutexNew(NULL);
  }
  reset_parser();
#if (APP_IAP_TELEMETRY_ENABLE != 0U)
  telemetry_tick = HAL_GetTick();
#endif
  business_tx_sequence = 0U;
#if (APP_IAP_TELEMETRY_ENABLE != 0U)
  telemetry_last_sequence = 0U;
  telemetry_ack_received = 0U;
  telemetry_ack_tick = 0U;
#endif
  fault_bitmap = 0U;
  fault_event_id = 0U;
  AutoDevice_ClearFault();
  status_snapshot_valid = 0U;
  io_snapshot_valid = 0U;
  status_report_requested = 1U;
  now = HAL_GetTick();
  state_check_tick = now;
  status_heartbeat_tick = now;
  io_heartbeat_tick = now;
  send_app_ready(next_business_sequence());
}

void AppIap_ProcessByte(uint8_t byte)
{
  switch (parser_state)
  {
    case APP_IAP_WAIT_HEAD_0:
      if (byte == IAP_HEAD_0)
      {
        parser_state = APP_IAP_WAIT_HEAD_1;
      }
      else
      {
        AppIap_OnBusinessByte(byte);
      }
      break;

    case APP_IAP_WAIT_HEAD_1:
      if (byte == IAP_HEAD_1)
      {
        parser_state = APP_IAP_READ_HEADER;
        receive_index = 0U;
      }
      else
      {
        reset_parser();
      }
      break;

    case APP_IAP_READ_HEADER:
      header[receive_index++] = byte;
      if (receive_index == APP_IAP_HEADER_SIZE)
      {
        payload_length = (uint16_t)header[2] | ((uint16_t)header[3] << 8U);
        receive_index = 0U;
        if (payload_length > IAP_MAX_PAYLOAD_SIZE)
        {
          send_nack(header[0], header[1], IAP_ERR_BAD_LENGTH, header[1], 0U);
          reset_parser();
        }
        else
        {
          parser_state = (payload_length == 0U) ? APP_IAP_READ_CRC : APP_IAP_READ_PAYLOAD;
        }
      }
      break;

    case APP_IAP_READ_PAYLOAD:
      payload[receive_index++] = byte;
      if (receive_index == payload_length)
      {
        parser_state = APP_IAP_READ_CRC;
        receive_index = 0U;
      }
      break;

    case APP_IAP_READ_CRC:
      crc_bytes[receive_index++] = byte;
      if (receive_index == APP_IAP_CRC_SIZE)
      {
        process_packet();
        reset_parser();
      }
      break;

    default:
      reset_parser();
      break;
  }
}

void AppIap_Service(void)
{
  uint32_t now = HAL_GetTick();

#if (APP_IAP_TELEMETRY_ENABLE != 0U)
  if ((uint32_t)(now - telemetry_tick) >= 1000U)
  {
    telemetry_tick = now;
    send_telemetry(next_business_sequence());
  }
#else
  /* Telemetry upload is disabled by APP_IAP_TELEMETRY_ENABLE. */
#endif

  if ((interaction_pending != 0U) &&
      ((TaskOutput_IsAutoMode() == 0U) ||
       (AutoDevice_IsEmergencyStopped() != 0U) ||
       ((AutoProcess_IsRunning() == 0U) && (TaskClean_IsRunning() == 0U))))
  {
    interaction_pending = 0U;
  }

  if ((interaction_pending != 0U) &&
      ((uint32_t)(now - interaction_tick) >= APP_INTERACTION_RESEND_MS))
  {
    send_interaction_request(next_business_sequence());
  }

  if ((uint32_t)(now - state_check_tick) >= APP_STATE_CHECK_PERIOD_MS)
  {
    uint8_t status_payload[APP_STATUS_PAYLOAD_SIZE];
    uint8_t io_payload[APP_IO_PAYLOAD_SIZE];
    uint8_t status_changed;
    uint8_t io_changed;

    state_check_tick = now;
    build_device_status_payload(status_payload);
    build_io_status_payload(io_payload);

    status_changed = ((status_snapshot_valid == 0U) ||
                      (memcmp(status_payload, last_status_payload,
                              APP_STATUS_PAYLOAD_SIZE) != 0)) ? 1U : 0U;
    io_changed = ((io_snapshot_valid == 0U) ||
                  (memcmp(io_payload, last_io_payload,
                          APP_IO_PAYLOAD_SIZE) != 0)) ? 1U : 0U;

    if ((status_changed != 0U) ||
        (status_report_requested != 0U) ||
        ((uint32_t)(now - status_heartbeat_tick) >= APP_STATE_HEARTBEAT_MS))
    {
      status_report_requested = 0U;
      send_device_status_payload(next_business_sequence(), status_payload);
    }

    if ((io_changed != 0U) ||
        ((uint32_t)(now - io_heartbeat_tick) >= APP_STATE_HEARTBEAT_MS))
    {
      send_io_status_payload(next_business_sequence(), io_payload);
    }
  }
}

__weak void AppIap_OnBusinessByte(uint8_t byte)
{
  (void)byte;
}

void AutoDevice_RequestHostInteraction(uint8_t interaction_type)
{
  if ((interaction_type == 0U) || (interaction_type > 7U))
  {
    return;
  }

  if ((interaction_pending != 0U) &&
      (interaction_type_pending == interaction_type))
  {
    send_interaction_request(next_business_sequence());
    return;
  }

  ++interaction_request_id;
  if (interaction_request_id == 0U)
  {
    interaction_request_id = 1U;
  }
  interaction_pending = 1U;
  interaction_type_pending = interaction_type;
  interaction_stage_id = (uint8_t)AutoProcess_GetCurrentStage();
  interaction_step_id = AutoProcess_GetCurrentStepId();
  send_interaction_request(next_business_sequence());
}

void AutoDevice_OnReport(AutoReport report)
{
  switch (report)
  {
    case AUTO_REPORT_SENSOR_FAULT:
    case AUTO_REPORT_TIMEOUT_FAULT:
      send_fault_event(next_business_sequence(), report);
      status_report_requested = 1U;
      break;

    default:
      status_report_requested = 1U;
      break;
  }
}
