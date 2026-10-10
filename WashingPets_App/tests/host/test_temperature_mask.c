#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../Core/Src/temperature_module.c"

static AppHardwareConfig_t hardware;
static UART_HandleTypeDef uart;
static uint32_t now, sends;
static uint8_t reply[13], pos, fail;
const AppHardwareConfig_t *AppParam_GetHardwareConfig(void) { return &hardware; }
uint32_t HAL_GetTick(void) { return now; }
void HAL_Delay(uint32_t ms) { now += ms; }
void TemperatureModule_SetTransmitMode(void) { }
void TemperatureModule_SetReceiveMode(void) { }
void TemperatureModule_OnTransmit(const uint8_t *d, uint16_t n) { (void)d; (void)n; }
void TemperatureModule_OnReceive(const uint8_t *d, uint16_t n) { (void)d; (void)n; }
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *u, uint8_t *d, uint16_t n, uint32_t t)
{
  (void)u; (void)t; ++sends; pos = 0U;
  assert(n == 8U && d[1] == 4U);
  if (fail) return HAL_TIMEOUT;
  reply[0] = d[0]; reply[1] = 4U; reply[2] = 8U;
  for (unsigned i = 0U; i < 4U; ++i)
  { reply[3+2*i] = 1U; reply[4+2*i] = (uint8_t)(10U+i); }
  append_crc(reply, 11U);
  return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_Receive(UART_HandleTypeDef *u, uint8_t *d, uint16_t n, uint32_t t)
{
  (void)u; (void)t; assert(pos + n <= sizeof(reply));
  memcpy(d, reply + pos, n); pos += n; ++now; return HAL_OK;
}
int main(void)
{
  TemperatureModuleData data;
  TemperatureModule_Init(&uart);
  for (unsigned mask = 0U; mask < 16U; ++mask)
  {
    hardware.temperature_mask = (uint8_t)mask;
    fail = 0U;
    unsigned before = sends;
    assert(TemperatureModule_ReadAll(5U, &data) == TEMPERATURE_MODULE_OK);
    assert(sends == before + (mask ? 1U : 0U));
    for (unsigned ch = 0U; ch < 4U; ++ch)
    {
      assert(data.valid[ch]);
      assert(data.temperature_x10[ch] == ((mask & (1U << ch)) ? 266 + (int)ch : fixed_temperature_x10[ch]));
    }
    fail = 1U;
    assert(TemperatureModule_ReadAll(5U, &data) ==
           (mask ? TEMPERATURE_MODULE_ERROR_TIMEOUT : TEMPERATURE_MODULE_OK));
    for (unsigned ch = 0U; ch < 4U; ++ch)
      assert(data.valid[ch] == !(mask & (1U << ch)));
  }
  puts("Temperature: all 16 runtime masks, real readings, bus loss and fully simulated/no bus PASS");
  return 0;
}
