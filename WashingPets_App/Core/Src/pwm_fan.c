#include "pwm_fan.h"
#include "rs485_config.h"

#include <string.h>

typedef enum
{
  PWM_INIT_STOP,
  PWM_INIT_DUTY,
  PWM_INIT_VERSION,
  PWM_INIT_CONFIG,
  PWM_INIT_DECIMALS_WRITE,
  PWM_INIT_DECIMALS_VERIFY,
  PWM_INIT_FREQUENCY,
  PWM_INIT_VERIFY,
  PWM_READY
} PwmFanPhase;

static volatile PwmFanPhase phase;
static uint16_t module_version;
static uint16_t expected_duty[4];
static uint16_t expected_control[4];
static uint8_t (*output_allowed)(void);
static void (*progress)(void);

static ThyristorStatus read_regs(uint16_t reg, uint16_t *values, uint16_t count)
{
  if (progress != NULL) progress();
  HAL_Delay(4U); /* At least 3.5 characters of RTU silence at 9600 8N1. */
  return Thyristor_ReadRegisters(RS485_STATION_FAN_PWM, reg, values, count);
}

static ThyristorStatus write_regs(uint16_t reg, const uint16_t *values, uint16_t count)
{
  if (progress != NULL) progress();
  HAL_Delay(4U);
  return Thyristor_WriteRegisters(RS485_STATION_FAN_PWM, reg, values, count);
}

static ThyristorStatus failed(ThyristorStatus status)
{
  if (status != THYRISTOR_OK) phase = PWM_INIT_STOP;
  return status;
}

static ThyristorStatus check_config(void)
{
  uint16_t values[2];
  ThyristorStatus status;
  if (module_version < 345U) return THYRISTOR_ERROR_RESPONSE;
  status = read_regs(0x001AU, values, 2U);
  if (status != THYRISTOR_OK) return status;
  return ((values[0] == FAN_PWM_FREQUENCY_DECIMALS) && (values[1] == 0U)) ?
         THYRISTOR_OK : THYRISTOR_ERROR_RESPONSE;
}

static ThyristorStatus check_outputs(void)
{
  uint16_t values[16];
  ThyristorStatus status = read_regs(0U, values, 16U);
  if (status != THYRISTOR_OK) return status;
  for (uint8_t i = 0U; i < 4U; ++i)
  {
    if ((values[i] != expected_duty[i]) ||
        (values[4U + 2U * i] != (FAN_PWM_FREQUENCY_REGISTER >> 16U)) ||
        (values[5U + 2U * i] != (FAN_PWM_FREQUENCY_REGISTER & 0xFFFFU)) ||
        (values[12U + i] != expected_control[i]))
      return THYRISTOR_ERROR_RESPONSE;
  }
  return THYRISTOR_OK;
}

void PwmFan_Init(uint8_t (*allow_output)(void), void (*heartbeat)(void))
{
  output_allowed = allow_output;
  progress = heartbeat;
  phase = PWM_INIT_STOP;
  module_version = 0U;
  memset(expected_duty, 0, sizeof(expected_duty));
  memset(expected_control, 0, sizeof(expected_control));
}

uint8_t PwmFan_IsReady(void)
{
  return (phase == PWM_READY) ? 1U : 0U;
}

/* One initialization transaction per call; sensors and urgent stop retain bus access. */
ThyristorStatus PwmFan_Service(void)
{
  const uint16_t zero[4] = {0U, 0U, 0U, 0U};
  uint16_t frequencies[8];
  ThyristorStatus status = THYRISTOR_OK;
  switch (phase)
  {
    case PWM_INIT_STOP:
      status = write_regs(0x000CU, zero, 4U);
      if (status == THYRISTOR_OK)
      {
        memset(expected_control, 0, sizeof(expected_control));
        phase = PWM_INIT_DUTY;
      }
      break;
    case PWM_INIT_DUTY:
      status = write_regs(0U, zero, 4U);
      if (status == THYRISTOR_OK)
      {
        memset(expected_duty, 0, sizeof(expected_duty));
        phase = PWM_INIT_VERSION;
      }
      break;
    case PWM_INIT_VERSION:
      status = read_regs(0x0022U, &module_version, 1U);
      if (status == THYRISTOR_OK) phase = PWM_INIT_CONFIG;
      break;
    case PWM_INIT_CONFIG:
    {
      uint16_t values[2];
      if (module_version < 345U) return failed(THYRISTOR_ERROR_RESPONSE);
      status = read_regs(0x001AU, values, 2U);
      if (status != THYRISTOR_OK) break;
      if (values[1] != 0U) return failed(THYRISTOR_ERROR_RESPONSE);
      phase = (values[0] == FAN_PWM_FREQUENCY_DECIMALS) ?
              PWM_INIT_FREQUENCY : PWM_INIT_DECIMALS_WRITE;
      break;
    }
    case PWM_INIT_DECIMALS_WRITE:
      if (progress != NULL) progress();
      HAL_Delay(4U);
      status = Thyristor_WriteRegister(RS485_STATION_FAN_PWM, 0x001AU,
                                      FAN_PWM_FREQUENCY_DECIMALS);
      if (status == THYRISTOR_OK) phase = PWM_INIT_DECIMALS_VERIFY;
      break;
    case PWM_INIT_DECIMALS_VERIFY:
      status = check_config();
      if (status == THYRISTOR_OK) phase = PWM_INIT_FREQUENCY;
      break;
    case PWM_INIT_FREQUENCY:
      for (uint8_t i = 0U; i < 4U; ++i)
      {
        frequencies[2U * i] = (uint16_t)(FAN_PWM_FREQUENCY_REGISTER >> 16U);
        frequencies[2U * i + 1U] = (uint16_t)FAN_PWM_FREQUENCY_REGISTER;
      }
      status = write_regs(0x0004U, frequencies, 8U);
      if (status == THYRISTOR_OK) phase = PWM_INIT_VERIFY;
      break;
    case PWM_INIT_VERIFY:
      status = check_outputs();
      if (status == THYRISTOR_OK) phase = PWM_READY;
      break;
    case PWM_READY:
      break;
  }
  return failed(status);
}

ThyristorStatus PwmFan_StopAll(void)
{
  const uint16_t zero[4] = {0U, 0U, 0U, 0U};
  ThyristorStatus control_status = write_regs(0x000CU, zero, 4U);
  /* Also attempt zero duty if disabling control failed. Never claim success then. */
  ThyristorStatus duty_status = write_regs(0U, zero, 4U);
  if (control_status == THYRISTOR_OK)
    memset(expected_control, 0, sizeof(expected_control));
  if (duty_status == THYRISTOR_OK)
    memset(expected_duty, 0, sizeof(expected_duty));
  return failed(control_status != THYRISTOR_OK ? control_status : duty_status);
}

ThyristorStatus PwmFan_SetPercent(uint8_t channel, uint8_t percent)
{
  uint16_t duty = (uint16_t)percent * 10U;
  uint16_t control = (percent == 0U) ? 0U : 0xFFFFU;
  uint16_t actual;
  ThyristorStatus status;
  if ((channel >= 4U) || (percent > 100U)) return THYRISTOR_ERROR_PARAM;
  if (percent == 0U)
  {
    status = write_regs((uint16_t)(0x000CU + channel), &control, 1U);
    if (status != THYRISTOR_OK) return failed(status);
    expected_control[channel] = 0U;
  }
  else if ((PwmFan_IsReady() == 0U) || (output_allowed == NULL) ||
           (output_allowed() == 0U))
  {
    return THYRISTOR_ERROR_PARAM;
  }
  status = write_regs(channel, &duty, 1U);
  if (status != THYRISTOR_OK) return failed(status);
  expected_duty[channel] = duty;
  if (percent != 0U)
  {
    /* A stop can arrive while a UART transaction blocks. Recheck before enable. */
    if (output_allowed() == 0U)
    {
      (void)PwmFan_StopAll();
      return THYRISTOR_ERROR_PARAM;
    }
    status = write_regs((uint16_t)(0x000CU + channel), &control, 1U);
    if (status != THYRISTOR_OK) return failed(status);
    expected_control[channel] = control;
  }
  status = read_regs(channel, &actual, 1U);
  if (status != THYRISTOR_OK) return failed(status);
  if (actual != duty) return failed(THYRISTOR_ERROR_RESPONSE);
  status = read_regs((uint16_t)(0x000CU + channel), &actual, 1U);
  if (status != THYRISTOR_OK) return failed(status);
  return failed(actual == control ? THYRISTOR_OK : THYRISTOR_ERROR_RESPONSE);
}

ThyristorStatus PwmFan_Probe(void)
{
  ThyristorStatus status;
  if (PwmFan_IsReady() == 0U) return THYRISTOR_ERROR_PARAM;
  status = check_outputs();
  if (status == THYRISTOR_OK) status = check_config();
  return failed(status);
}
