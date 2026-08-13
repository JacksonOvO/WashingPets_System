#ifndef OXYGEN_SENSOR_H
#define OXYGEN_SENSOR_H

#include "stm32f4xx_hal.h"

typedef enum
{
  OXYGEN_SENSOR_OK = 0,
  OXYGEN_SENSOR_ERROR_PARAM,
  OXYGEN_SENSOR_ERROR_TIMEOUT,
  OXYGEN_SENSOR_ERROR_RESPONSE,
  OXYGEN_SENSOR_ERROR_CRC,
  OXYGEN_SENSOR_ERROR_EXCEPTION
} OxygenSensorStatus;

typedef struct
{
  uint16_t humidity_x10;
  int16_t temperature_x10;
  uint16_t oxygen_x10;
} OxygenSensorData;

void OxygenSensor_Init(UART_HandleTypeDef *huart);

/*
 * Read humidity, temperature and oxygen in one Modbus RTU transaction.
 * Values retain the sensor's x10 scale:
 *   humidity_x10    = 359  means 35.9 %RH
 *   temperature_x10 = -75  means -7.5 degC
 *   oxygen_x10      = 100  means 10.0 %VOL
 *
 * Call this function only from the task that owns the RS485 bus.
 */
OxygenSensorStatus OxygenSensor_ReadAll(uint8_t station,
                                        OxygenSensorData *data);

/*
 * Read the ozone concentration register 0x0002.
 * The 0-10 ppm sensor returns an unsigned value in 0.01 ppm units:
 *   ozone_x100 = 100 means 1.00 ppm.
 */
OxygenSensorStatus OxygenSensor_ReadOzone(uint8_t station,
                                          uint16_t *ozone_x100);

/* Override these hooks when the RS485 transceiver uses a DE/RE GPIO. */
void OxygenSensor_SetTransmitMode(void);
void OxygenSensor_SetReceiveMode(void);
void OxygenSensor_OnTransmit(const uint8_t *data, uint16_t length);
void OxygenSensor_OnReceive(const uint8_t *data, uint16_t length);

#endif
