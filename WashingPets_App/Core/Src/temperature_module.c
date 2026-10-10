#include "temperature_module.h"
#include "app_param.h"
#include "rs485_config.h"

#define TEMP_MODULE_FUNC_READ             0x04U
#define TEMP_MODULE_REGISTER_COUNT        4U
#define TEMP_MODULE_REQUEST_LENGTH        8U
#define TEMP_MODULE_DATA_LENGTH           8U
#define TEMP_MODULE_RESPONSE_LENGTH       13U
#define TEMP_MODULE_EXCEPTION_LENGTH      5U
#define TEMP_MODULE_TIMEOUT_MS            500U
#define TEMP_MODULE_INVALID_VALUE         0xFFFFU
#define TEMP_MODULE_MIN_VALID_X10         (-400)
#define TEMP_MODULE_MAX_VALID_X10         1250
static UART_HandleTypeDef *temperature_module_uart;

static const int16_t fixed_temperature_x10[4] = {
  RS485_FIXED_INLET_TEMP_X10,
  RS485_FIXED_TANK_TEMP_X10,
  RS485_FIXED_PIPE_TEMP_X10,
  RS485_FIXED_SPARE_TEMP_X10
};

static uint16_t modbus_crc16(const uint8_t *data, uint16_t length)
{
  uint16_t crc = 0xFFFFU;
  uint16_t index;
  uint8_t bit;

  for (index = 0U; index < length; ++index)
  {
    crc ^= data[index];
    for (bit = 0U; bit < 8U; ++bit)
    {
      crc = ((crc & 1U) != 0U) ?
            (uint16_t)((crc >> 1U) ^ 0xA001U) : (uint16_t)(crc >> 1U);
    }
  }
  return crc;
}

static void append_crc(uint8_t *frame, uint16_t data_length)
{
  uint16_t crc = modbus_crc16(frame, data_length);

  frame[data_length] = (uint8_t)crc;
  frame[data_length + 1U] = (uint8_t)(crc >> 8U);
}

static uint8_t crc_is_valid(const uint8_t *frame, uint16_t length)
{
  uint16_t expected;

  if (length < 3U)
  {
    return 0U;
  }
  expected = (uint16_t)frame[length - 2U] |
             ((uint16_t)frame[length - 1U] << 8U);
  return (modbus_crc16(frame, (uint16_t)(length - 2U)) == expected) ? 1U : 0U;
}

static uint8_t temperature_raw_is_valid(uint16_t raw, int16_t *value)
{
  int16_t signed_value;

  if (raw == TEMP_MODULE_INVALID_VALUE)
  {
    return 0U;
  }

  signed_value = (int16_t)raw;
  if ((signed_value < (int16_t)TEMP_MODULE_MIN_VALID_X10) ||
      (signed_value > (int16_t)TEMP_MODULE_MAX_VALID_X10))
  {
    return 0U;
  }

  if (value != NULL)
  {
    *value = signed_value;
  }
  return 1U;
}

static TemperatureModuleStatus exchange(const uint8_t request[8],
                                        uint8_t response[13],
                                        uint16_t *response_length)
{
  uint8_t byte;
  uint8_t header_index = 0U;
  uint16_t remaining;
  uint32_t start_tick;

  if ((temperature_module_uart == NULL) || (response_length == NULL))
  {
    return TEMPERATURE_MODULE_ERROR_PARAM;
  }

  __HAL_UART_CLEAR_OREFLAG(temperature_module_uart);
  __HAL_UART_FLUSH_DRREGISTER(temperature_module_uart);
  TemperatureModule_OnTransmit(request, TEMP_MODULE_REQUEST_LENGTH);
  TemperatureModule_SetTransmitMode();
  HAL_Delay(1U);

  if (HAL_UART_Transmit(temperature_module_uart, (uint8_t *)request,
                        TEMP_MODULE_REQUEST_LENGTH,
                        TEMP_MODULE_TIMEOUT_MS) != HAL_OK)
  {
    TemperatureModule_SetReceiveMode();
    return TEMPERATURE_MODULE_ERROR_TIMEOUT;
  }

  start_tick = HAL_GetTick();
  while (__HAL_UART_GET_FLAG(temperature_module_uart, UART_FLAG_TC) == RESET)
  {
    if ((HAL_GetTick() - start_tick) >= TEMP_MODULE_TIMEOUT_MS)
    {
      TemperatureModule_SetReceiveMode();
      return TEMPERATURE_MODULE_ERROR_TIMEOUT;
    }
  }
  TemperatureModule_SetReceiveMode();

  start_tick = HAL_GetTick();
  while ((HAL_GetTick() - start_tick) < TEMP_MODULE_TIMEOUT_MS)
  {
    if (HAL_UART_Receive(temperature_module_uart, &byte, 1U, 10U) != HAL_OK)
    {
      continue;
    }
    if (header_index == 0U)
    {
      if (byte == request[0])
      {
        response[0] = byte;
        header_index = 1U;
      }
    }
    else if ((byte == TEMP_MODULE_FUNC_READ) ||
             (byte == (uint8_t)(TEMP_MODULE_FUNC_READ | 0x80U)))
    {
      response[1] = byte;
      header_index = 2U;
      break;
    }
    else if (byte == request[0])
    {
      response[0] = byte;
    }
    else
    {
      header_index = 0U;
    }
  }
  if (header_index != 2U)
  {
    return TEMPERATURE_MODULE_ERROR_TIMEOUT;
  }

  if (HAL_UART_Receive(temperature_module_uart, &response[2], 1U,
                       TEMP_MODULE_TIMEOUT_MS) != HAL_OK)
  {
    return TEMPERATURE_MODULE_ERROR_TIMEOUT;
  }
  if ((response[1] & 0x80U) != 0U)
  {
    remaining = 2U;
    *response_length = TEMP_MODULE_EXCEPTION_LENGTH;
  }
  else
  {
    if (response[2] != TEMP_MODULE_DATA_LENGTH)
    {
      return TEMPERATURE_MODULE_ERROR_RESPONSE;
    }
    remaining = TEMP_MODULE_DATA_LENGTH + 2U;
    *response_length = TEMP_MODULE_RESPONSE_LENGTH;
  }
  if (HAL_UART_Receive(temperature_module_uart, &response[3], remaining,
                       TEMP_MODULE_TIMEOUT_MS) != HAL_OK)
  {
    return TEMPERATURE_MODULE_ERROR_TIMEOUT;
  }
  TemperatureModule_OnReceive(response, *response_length);
  if (crc_is_valid(response, *response_length) == 0U)
  {
    return TEMPERATURE_MODULE_ERROR_CRC;
  }
  return ((response[1] & 0x80U) != 0U) ?
         TEMPERATURE_MODULE_ERROR_EXCEPTION : TEMPERATURE_MODULE_OK;
}

void TemperatureModule_Init(UART_HandleTypeDef *huart)
{
  temperature_module_uart = huart;
  TemperatureModule_SetReceiveMode();
}

TemperatureModuleStatus TemperatureModule_ReadAll(
    uint8_t station, TemperatureModuleData *data)
{
  uint8_t request[TEMP_MODULE_REQUEST_LENGTH] = {
    0U, TEMP_MODULE_FUNC_READ, 0U, 0U, 0U, TEMP_MODULE_REGISTER_COUNT, 0U, 0U
  };
  uint8_t response[TEMP_MODULE_RESPONSE_LENGTH];
  uint16_t response_length;
  uint16_t raw;
  int16_t temperature_x10;
  uint8_t channel;
  TemperatureModuleStatus status;

  if ((station == 0U) || (station > 247U) || (data == NULL))
  {
    return TEMPERATURE_MODULE_ERROR_PARAM;
  }
  /* Disabled channels remain simulated even if the physical module is absent. */
  for (channel = 0U; channel < 4U; ++channel)
  {
    data->valid[channel] = (AppParam_GetHardwareConfig()->temperature_mask & (1U << channel)) == 0U;
    data->temperature_x10[channel] = data->valid[channel] ?
        fixed_temperature_x10[channel] : (int16_t)TEMP_MODULE_INVALID_VALUE;
  }
  if (AppParam_GetHardwareConfig()->temperature_mask == 0U) return TEMPERATURE_MODULE_OK;
  request[0] = station;
  append_crc(request, 6U);
  status = exchange(request, response, &response_length);
  if (status != TEMPERATURE_MODULE_OK)
  {
    return status;
  }
  if ((response_length != TEMP_MODULE_RESPONSE_LENGTH) ||
      (response[0] != station) || (response[1] != TEMP_MODULE_FUNC_READ))
  {
    return TEMPERATURE_MODULE_ERROR_RESPONSE;
  }

  for (channel = 0U; channel < 4U; ++channel)
  {
    raw = ((uint16_t)response[3U + channel * 2U] << 8U) |
          response[4U + channel * 2U];
    if ((AppParam_GetHardwareConfig()->temperature_mask & (1U << channel)) == 0U)
    {
      data->temperature_x10[channel] = fixed_temperature_x10[channel];
      data->valid[channel] = 1U;
    }
    else
    {
      if (temperature_raw_is_valid(raw, &temperature_x10) != 0U)
      {
        data->temperature_x10[channel] = temperature_x10;
        data->valid[channel] = 1U;
      }
      else
      {
        data->temperature_x10[channel] = (int16_t)TEMP_MODULE_INVALID_VALUE;
        data->valid[channel] = 0U;
      }
    }
  }
  return TEMPERATURE_MODULE_OK;
}
