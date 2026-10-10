#include <assert.h>
#include <stdio.h>
#include "../../Core/Src/task_comm485.c"

static uint32_t now;
static AutoFaultCode fault;
static uint8_t ready, probed, last_channel, last_power, bus_station, stopped;
static ThyristorStatus pwm_status = THYRISTOR_OK;
static AppParam_t params;
static AppHardwareConfig_t hardware = {FAN_USE_PWM_MODULE, 11U, 0U, 0U};
const AppHardwareConfig_t *AppParam_GetHardwareConfig(void) { return &hardware; }

uint32_t HAL_GetTick(void) { return now; }
uint32_t __get_PRIMASK(void) { return 0U; }
void __disable_irq(void) { }
void __enable_irq(void) { }
uint8_t AutoDevice_IsEmergencyStopped(void) { return 0U; }
void AutoDevice_SetFault(AutoFaultCode f) { fault = f; }
AutoFaultCode AutoDevice_GetFault(void) { return fault; }
void AutoDevice_SetStopRequested(uint8_t value) { stopped = value; }
void AutoDevice_Report(AutoReport event) { (void)event; }
const AppParam_t *AppParam_Get(void) { return &params; }
void TaskWatchdog_Heartbeat(TaskWatchdogSource id) { (void)id; }
HAL_StatusTypeDef UART2_Send(const uint8_t *data, uint16_t length)
{ (void)data; (void)length; return HAL_OK; }
void osDelay(uint32_t ms) { now += ms; }
ThyristorStatus Thyristor_SetVoltagePercent(uint8_t station, uint8_t percent)
{ bus_station = station; last_power = percent; return THYRISTOR_OK; }
ThyristorStatus Thyristor_ReadRegister(uint8_t station, uint16_t reg, uint16_t *value)
{ (void)reg; bus_station = station; *value = 0U; return THYRISTOR_OK; }
ThyristorStatus Thyristor_WriteRegister(uint8_t station, uint16_t reg, uint16_t value)
{ (void)reg; (void)value; bus_station = station; return THYRISTOR_OK; }
uint8_t PwmFan_IsReady(void) { return ready; }
ThyristorStatus PwmFan_SetPercent(uint8_t channel, uint8_t percent)
{ last_channel = channel; last_power = percent; return pwm_status; }
ThyristorStatus PwmFan_Probe(void) { ++probed; return pwm_status; }
ThyristorStatus PwmFan_StopAll(void) { last_power = 0U; return pwm_status; }

int main(void)
{
  now = 100U;
  nonzero_inhibit_reasons = 0U;
  rs485_actuator_mode = 1U;
  for (unsigned i = 0; i < 4; ++i)
  {
    const uint8_t removed[] = {10, 11, 14, 15};
    bus_station = 0;
    assert(apply_percent(removed[i], 70) == HAL_ERROR);
    assert(apply_percent(removed[i], 0) == HAL_OK && bus_station == 0);
  }
#if FAN_USE_PWM_MODULE
  assert(!TaskComm485_IsAllHealthy());
  assert(apply_percent(12, 50) == HAL_BUSY);
  assert(apply_percent(16, 80) == HAL_BUSY);
  ready = 1;
  assert(apply_percent(12, 70) == HAL_OK && last_channel == 2 && last_power == 70);
  assert(apply_percent(13, 70) == HAL_OK && last_channel == 3);
  assert(apply_percent(16, 80) == HAL_OK && bus_station == 16);
  actuator_probe_index = 0;
  process_actuator_probe();
  assert(probed == 1);
  bus_station = 0;
  process_actuator_probe();
  assert(probed == 1 && bus_station == 0);
  process_actuator_probe();
  assert(bus_station == 16);
  pwm_status = THYRISTOR_ERROR_TIMEOUT;
  process_actuator_probe();
  assert(fault == AUTO_FAULT_485_FAN_PWM_COMM && stopped);
  assert(safe_stop_requested && (nonzero_inhibit_reasons & COMM485_INHIBIT_FAULT));
#else
  assert(TaskComm485_IsAllHealthy());
  const uint8_t active[] = {12, 13, 16};
  for (unsigned i = 0; i < 3; ++i)
  {
    assert(apply_percent(active[i], 70) == HAL_OK && bus_station == active[i]);
    assert(TaskComm485_GetLastPercent(active[i]) == 70);
    process_actuator_probe();
    assert(bus_station == active[i]);
  }
  assert(probed == 0);
#endif
  hardware.fan_pwm = 0;
  assert(apply_percent(12, 40) == HAL_OK && bus_station == 12);
  hardware.fan_pwm = ready = 1;
  pwm_status = THYRISTOR_OK;
  bus_station = 0;
  assert(apply_percent(12, 40) == HAL_OK && bus_station == 0 && last_channel == 2);
  puts("Fan routing: removed stations never written/probed; bottom CH3/top CH4 and hot switch PASS");
  return 0;
}
