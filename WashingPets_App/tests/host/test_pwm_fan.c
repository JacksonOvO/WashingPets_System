#define FAN_USE_PWM_MODULE 1U
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../Core/Src/thyristor.c"
#include "../../Core/Src/pwm_fan.c"

static UART_HandleTypeDef uart;
static uint16_t registers[0x24];
static uint8_t response_bytes[80];
static uint16_t rx_count, rx_pos;
static uint32_t tick, transactions;
static uint8_t allowed, response_mode, stop_after_duty;
static uint8_t last_station, last_function;
static uint16_t last_reg, last_count;
static uint32_t decimal_writes;

uint32_t HAL_GetTick(void) { return tick; }
void HAL_Delay(uint32_t ms) { tick += ms; }
static uint8_t allow(void) { return allowed; }
static void heartbeat(void) { }

HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *u, uint8_t *data,
                                    uint16_t length, uint32_t timeout)
{
  (void)u; (void)timeout;
  assert(crc_is_valid(data, length));
  ++transactions;
  last_station = data[0]; last_function = data[1];
  last_reg = ((uint16_t)data[2] << 8U) | data[3];
  last_count = ((uint16_t)data[4] << 8U) | data[5];
  rx_pos = 0U;
  rx_count = 0U;
  if (response_mode == 2U) return HAL_OK;
  if ((response_mode == 3U) || (response_mode == 4U))
  {
    response_bytes[0] = data[0];
    response_bytes[1] = data[1] | 0x80U;
    response_bytes[2] = 1U;
    if (response_mode == 3U)
    {
      append_crc(response_bytes, 3U); rx_count = 5U;
    }
    else
    {
      response_bytes[3] = 2U;
      append_crc(response_bytes, 4U); rx_count = 6U;
    }
    return HAL_OK;
  }
  assert(last_reg < 0x24U);
  if (data[1] == 0x10U)
  {
    assert(length == 9U + 2U * last_count);
    assert(data[6] == 2U * last_count);
    assert(last_reg + last_count <= 16U); /* Never save/change EEPROM. */
    if (last_reg == 4U)
      for (unsigned i = 12U; i < 16U; ++i) assert(registers[i] == 0U);
    for (unsigned i = 0U; i < last_count; ++i)
      registers[last_reg+i] = ((uint16_t)data[7U+2U*i] << 8U) | data[8U+2U*i];
    if (stop_after_duty && last_reg < 4U) allowed = 0U;
    memcpy(response_bytes, data, 6U);
    if (response_mode == 5U) response_bytes[3] ^= 1U;
    append_crc(response_bytes, 6U); rx_count = 8U;
  }
  else if (data[1] == 0x06U)
  {
    assert(length == 8U);
    if (last_reg == 0x1AU) ++decimal_writes;
    registers[last_reg] = last_count;
    memcpy(response_bytes, data, 8U); rx_count = 8U;
  }
  else
  {
    assert(data[1] == 0x03U && length == 8U);
    assert(last_reg + last_count <= 0x24U);
    response_bytes[0] = data[0]; response_bytes[1] = data[1];
    response_bytes[2] = (uint8_t)(last_count * 2U);
    for (unsigned i = 0U; i < last_count; ++i)
    {
      response_bytes[3U+2U*i] = (uint8_t)(registers[last_reg+i] >> 8U);
      response_bytes[4U+2U*i] = (uint8_t)registers[last_reg+i];
    }
    if (response_mode == 6U) response_bytes[2] = 250U;
    append_crc(response_bytes, (uint16_t)(3U + last_count * 2U));
    rx_count = (uint16_t)(5U + last_count * 2U);
  }
  if (response_mode == 1U) response_bytes[rx_count-1U] ^= 1U;
  return HAL_OK;
}

HAL_StatusTypeDef HAL_UART_Receive(UART_HandleTypeDef *u, uint8_t *data,
                                   uint16_t length, uint32_t timeout)
{
  (void)u;
  if (rx_pos + length > rx_count) { tick += timeout; return HAL_TIMEOUT; }
  memcpy(data, response_bytes + rx_pos, length);
  rx_pos += length;
  ++tick;
  return HAL_OK;
}

static void reset_module(uint16_t version)
{
  memset(registers, 0, sizeof(registers));
  registers[0x22] = version;
  for (unsigned i = 0U; i < 4U; ++i)
  {
    registers[i] = 500U;
    registers[12U+i] = 0xFFFFU; /* Simulate unsafe saved startup state. */
  }
  response_mode = 0U; stop_after_duty = 0U; allowed = 1U;
  decimal_writes = 0U;
  Thyristor_Init(&uart);
  PwmFan_Init(allow, heartbeat);
}

static void initialize(void)
{
  for (unsigned i = 0U; i < 12U && !PwmFan_IsReady(); ++i)
    assert(PwmFan_Service() == THYRISTOR_OK);
  assert(PwmFan_IsReady());
  for (unsigned i = 0U; i < 4U; ++i)
  {
    assert(registers[i] == 0U && registers[12U+i] == 0U);
    assert(registers[4U+2U*i] == 0U && registers[5U+2U*i] == 10000U);
  }
  assert(registers[0x1AU] == 1U);
}

int main(void)
{
  uint16_t value;
  reset_module(400U);
  assert(PwmFan_SetPercent(0U, 50U) != THYRISTOR_OK);
  initialize();
  assert(decimal_writes == 1U);
  PwmFan_Init(allow, heartbeat);
  initialize();
  assert(decimal_writes == 1U); /* Persistent value already correct. */
  const uint8_t percentages[] = {0U, 10U, 50U, 70U, 100U};
  for (uint8_t ch = 0U; ch < 4U; ++ch)
    for (unsigned p = 0U; p < sizeof(percentages); ++p)
    {
      uint8_t percent = percentages[p];
      assert(PwmFan_SetPercent(ch, percent) == THYRISTOR_OK);
      assert(last_station == 30U);
      assert(registers[ch] == (uint16_t)percent * 10U);
      assert(registers[ch+12U] == (percent ? 0xFFFFU : 0U));
      assert(PwmFan_Probe() == THYRISTOR_OK);
    }
  assert(PwmFan_SetPercent(4U, 20U) == THYRISTOR_ERROR_PARAM);
  assert(PwmFan_SetPercent(0U, 101U) == THYRISTOR_ERROR_PARAM);
  allowed = 0U;
  assert(PwmFan_SetPercent(1U, 30U) != THYRISTOR_OK);
  assert(PwmFan_StopAll() == THYRISTOR_OK);
  allowed = 1U; stop_after_duty = 1U;
  assert(PwmFan_SetPercent(0U, 50U) != THYRISTOR_OK);
  for (unsigned i = 0U; i < 4U; ++i)
    assert(registers[i] == 0U && registers[12U+i] == 0U);
  stop_after_duty = 0U; allowed = 1U;
  assert(PwmFan_SetPercent(3U, 70U) == THYRISTOR_OK);
  registers[11U] = 500U; /* Detect module reset or configuration drift. */
  assert(PwmFan_Probe() == THYRISTOR_ERROR_RESPONSE && !PwmFan_IsReady());
  initialize();
  for (uint8_t mode = 1U; mode <= 6U; ++mode)
  {
    response_mode = mode;
    ThyristorStatus s = Thyristor_ReadRegisters(30U, 0U, &value, 1U);
    if (mode == 5U)
      s = Thyristor_WriteRegisters(30U, 0U, &value, 1U);
    assert(s != THYRISTOR_OK);
    if (mode == 3U || mode == 4U) assert(s == THYRISTOR_ERROR_EXCEPTION);
  }
  response_mode = 2U;
  assert(PwmFan_Probe() == THYRISTOR_ERROR_TIMEOUT && !PwmFan_IsReady());
  response_mode = 0U; initialize();
  unsigned before = transactions;
  assert(Thyristor_ReadRegisters(0U, 0U, &value, 1U) == THYRISTOR_ERROR_PARAM);
  assert(Thyristor_ReadRegisters(30U, 0U, &value, 17U) == THYRISTOR_ERROR_PARAM);
  assert(Thyristor_WriteRegisters(30U, 0xFFFFU, &value, 2U) == THYRISTOR_ERROR_PARAM);
  assert(before == transactions);
  assert(Thyristor_SetVoltagePercent(14U, 50U) == THYRISTOR_OK);
  assert(last_station == 14U && last_function == 6U && registers[0] == 120U);
  uint8_t percent;
  assert(Thyristor_GetVoltagePercent(14U, &percent) == THYRISTOR_OK && percent == 50U);
  reset_module(344U);
  assert(PwmFan_Service() == THYRISTOR_OK);
  assert(PwmFan_Service() == THYRISTOR_OK);
  assert(PwmFan_Service() == THYRISTOR_OK);
  assert(PwmFan_Service() == THYRISTOR_ERROR_RESPONSE && !PwmFan_IsReady());
  reset_module(400U); registers[0x1A] = 1U;
  initialize();
  assert(decimal_writes == 0U);
  registers[0x1A] = 0U;
  assert(PwmFan_Probe() == THYRISTOR_ERROR_RESPONSE && !PwmFan_IsReady());
  reset_module(400U); registers[0x1B] = 1U;
  assert(PwmFan_Service() == THYRISTOR_OK);
  assert(PwmFan_Service() == THYRISTOR_OK);
  assert(PwmFan_Service() == THYRISTOR_OK);
  assert(PwmFan_Service() == THYRISTOR_ERROR_RESPONSE && !PwmFan_IsReady());
  puts("PWM: init, 4 channels, readback, stop race, reset, CRC, exceptions, bounds and legacy thyristor PASS");
  return 0;
}
