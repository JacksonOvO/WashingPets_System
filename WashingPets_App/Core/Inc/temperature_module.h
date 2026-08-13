#ifndef TEMPERATURE_MODULE_H
#define TEMPERATURE_MODULE_H

#include "stm32f4xx_hal.h"

typedef enum
{
  TEMPERATURE_MODULE_OK = 0,
  TEMPERATURE_MODULE_ERROR_PARAM,
  TEMPERATURE_MODULE_ERROR_TIMEOUT,
  TEMPERATURE_MODULE_ERROR_RESPONSE,
  TEMPERATURE_MODULE_ERROR_CRC,
  TEMPERATURE_MODULE_ERROR_EXCEPTION
} TemperatureModuleStatus;

typedef struct
{
  int16_t temperature_x10[4];
  uint8_t valid[4];
} TemperatureModuleData;

void TemperatureModule_Init(UART_HandleTypeDef *huart);
TemperatureModuleStatus TemperatureModule_ReadAll(
    uint8_t station, TemperatureModuleData *data);

void TemperatureModule_SetTransmitMode(void);
void TemperatureModule_SetReceiveMode(void);
void TemperatureModule_OnTransmit(const uint8_t *data, uint16_t length);
void TemperatureModule_OnReceive(const uint8_t *data, uint16_t length);

#endif
