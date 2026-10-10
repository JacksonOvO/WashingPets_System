#ifndef THYRISTOR_H
#define THYRISTOR_H

#include "stm32f4xx_hal.h"

#define THYRISTOR_REG_OUTPUT_VOLTAGE   0x0000U
#define THYRISTOR_REG_STATION_ADDRESS  0x0002U
#define THYRISTOR_REG_PWM_FREQUENCY    0x0003U
#define THYRISTOR_REG_BAUD_RATE        0x0014U
#define THYRISTOR_MAX_OUTPUT_VOLTAGE   240U

typedef enum
{
  THYRISTOR_OK = 0,
  THYRISTOR_ERROR_PARAM,
  THYRISTOR_ERROR_TIMEOUT,
  THYRISTOR_ERROR_RESPONSE,
  THYRISTOR_ERROR_CRC,
  THYRISTOR_ERROR_EXCEPTION
} ThyristorStatus;

void Thyristor_Init(UART_HandleTypeDef *huart);
ThyristorStatus Thyristor_WriteRegister(uint8_t station, uint16_t reg,
                                        uint16_t value);
ThyristorStatus Thyristor_ReadRegister(uint8_t station, uint16_t reg,
                                       uint16_t *value);
/* Bounded Modbus block transfers, also accepting QX's CRC-checked exception frame. */
#define THYRISTOR_MAX_REGISTERS 16U
ThyristorStatus Thyristor_WriteRegisters(uint8_t station, uint16_t reg,
                                       const uint16_t *values, uint16_t count);
ThyristorStatus Thyristor_ReadRegisters(uint8_t station, uint16_t reg,
                                      uint16_t *values, uint16_t count);
ThyristorStatus Thyristor_SetVoltagePercent(uint8_t station, uint8_t percent);
ThyristorStatus Thyristor_GetVoltagePercent(uint8_t station, uint8_t *percent);

/* Override these hooks when the RS485 transceiver requires a DE/RE GPIO. */
void Thyristor_SetTransmitMode(void);
void Thyristor_SetReceiveMode(void);
void Thyristor_OnTransmit(const uint8_t *data, uint16_t length);
void Thyristor_OnReceive(const uint8_t *data, uint16_t length);

#endif
