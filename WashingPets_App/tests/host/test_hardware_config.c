#include <assert.h>
#include <stdio.h>
#include "../../Core/Src/task_comm485.c"

static AppParam_t params;
static AppHardwareConfig_t active;
static uint32_t now;
static uint8_t allowed = 1U, ready, fail_bus, fail_pwm, hang_pwm;
static unsigned commits, clears, updates, initializations, old_fan_stops, new_fan_stops;
static AutoFaultCode fault;
static uint8_t switching_to_legacy, pump_mode, pump_running, low_level;
static uint16_t pressure_set, pressure_feedback, wrong_readback, last_reg, last_value;
static unsigned writes, reads;
uint8_t AppParam_BoosterPumpUsesRs485(void) { return pump_mode; }
void AppParam_CommitBoosterPumpMode(uint8_t mode) { pump_mode = mode; }
uint8_t AutoDevice_IsBoosterPumpRunning(void) { return pump_running; }
uint8_t AutoDevice_ReadInput(AutoInputPoint p) { return p == AUTO_INPUT_TANK_LOW ? low_level : 1; }
uint8_t TaskOutput_IsAutoMode(void) { return 0; }
uint8_t TaskOutput_GetActual(AutoOutputPoint p) { (void)p; return pump_running; }
ThyristorStatus Thyristor_WriteRegister(uint8_t s, uint16_t r, uint16_t v)
{
  assert(s == 1 && (r == 0 || r == 0x2000));
  if (!r) pressure_set = v;
  else assert(v == 1 || v == 6);
  last_reg = r; last_value = v; ++writes;
  return fail_bus ? THYRISTOR_ERROR_TIMEOUT : THYRISTOR_OK;
}
ThyristorStatus Thyristor_ReadRegister(uint8_t s, uint16_t r, uint16_t *v)
{
  assert(s == 1 && (r == 0x100F || r == 0x1010));
  *v = r == 0x100F ? (wrong_readback ? wrong_readback : pressure_set) : pressure_feedback;
  ++reads;
  return fail_bus ? THYRISTOR_ERROR_TIMEOUT : THYRISTOR_OK;
}
int osMessageQueuePut(osMessageQueueId_t q, const void *v, uint8_t p, uint32_t t)
{ (void)q; (void)v; (void)p; (void)t; return osOK; }

uint32_t HAL_GetTick(void) { return now; }
uint32_t __get_PRIMASK(void) { return 0U; }
void __disable_irq(void) { }
void __enable_irq(void) { }
uint8_t AutoDevice_IsEmergencyStopped(void) { return !allowed; }
void AutoDevice_SetFault(AutoFaultCode f) { fault = f; }
AutoFaultCode AutoDevice_GetFault(void) { return fault; }
void AutoDevice_SetStopRequested(uint8_t value) { (void)value; }
void AutoDevice_Report(AutoReport event) { (void)event; }
void AutoDevice_ClearHostCommands(void) { ++clears; }
void AutoDevice_InvalidateTemperatureChannels(void) { }
void AutoDevice_UpdateTemperatureChannels(const int16_t v[4], const uint8_t valid[4])
{ (void)v; (void)valid; ++updates; }
const AppParam_t *AppParam_Get(void) { return &params; }
const AppHardwareConfig_t *AppParam_GetHardwareConfig(void) { return &active; }
uint8_t AppParam_ValidateHardwareConfig(const AppHardwareConfig_t *c)
{ return c && c->fan_pwm <= 1U && c->temperature_mask <= 15U &&
         c->lower_door_bypass <= 1U && c->host_command_bypass <= 1U; }
uint8_t AppParam_HardwareConfigAllowed(void) { return allowed; }
void AppParam_CommitHardwareConfig(const AppHardwareConfig_t *c) { active = *c; ++commits; }
void TaskWatchdog_Heartbeat(TaskWatchdogSource id) { (void)id; }
HAL_StatusTypeDef UART2_Send(const uint8_t *data, uint16_t length)
{ (void)data; (void)length; return HAL_OK; }
void osDelay(uint32_t ms) { now += ms; }
int osMessageQueueReset(osMessageQueueId_t q) { (void)q; return osOK; }
ThyristorStatus Thyristor_SetVoltagePercent(uint8_t station, uint8_t percent)
{
  assert(percent == 0U);
  if (station >= 10U && station <= 13U)
  {
    if (switching_to_legacy) ++new_fan_stops;
    else ++old_fan_stops;
  }
  return fail_bus ? THYRISTOR_ERROR_TIMEOUT : THYRISTOR_OK;
}
uint8_t PwmFan_IsReady(void) { return ready; }
ThyristorStatus PwmFan_SetPercent(uint8_t ch, uint8_t percent)
{ (void)ch; assert(!percent); return THYRISTOR_OK; }
ThyristorStatus PwmFan_StopAll(void)
{ return fail_pwm ? THYRISTOR_ERROR_TIMEOUT : THYRISTOR_OK; }
void PwmFan_Init(uint8_t (*allow)(void), void (*heartbeat)(void))
{ assert(!allow()); heartbeat(); ready = 0U; ++initializations; }
ThyristorStatus PwmFan_Service(void)
{
  if (fail_pwm) return THYRISTOR_ERROR_TIMEOUT;
  ready = !hang_pwm;
  return THYRISTOR_OK;
}
TemperatureModuleStatus TemperatureModule_ReadAll(uint8_t s, TemperatureModuleData *d)
{ (void)s; memset(d, 0, sizeof(*d)); return TEMPERATURE_MODULE_OK; }

static HAL_StatusTypeDef finish(void)
{
  HAL_StatusTypeDef result = HAL_ERROR;
  for (unsigned i = 0; i < 30U && hardware_config_state != 3U; ++i)
  { process_hardware_config(); now += 5U; }
  assert(TaskComm485_HardwareConfigBusy());
  assert(TaskComm485_TakeHardwareConfigResult(&result));
  assert(!TaskComm485_HardwareConfigBusy());
  assert(!(TaskComm485_GetInhibitReasons() & COMM485_INHIBIT_CONFIG));
  assert(!TaskComm485_TakeHardwareConfigResult(&result));
  return result;
}

int main(void)
{
  now = 100U;
  active = (AppHardwareConfig_t){0U, 11U, 0U, 0U};
  AppHardwareConfig_t target = {1U, 15U, 0U, 0U};
  assert(TaskComm485_RequestHardwareConfig(NULL) == HAL_ERROR);
  allowed = 0U;
  assert(TaskComm485_RequestHardwareConfig(&target) == HAL_BUSY);
  allowed = 1U;
  fail_bus = 1U;
  assert(TaskComm485_RequestHardwareConfig(&target) == HAL_OK);
  assert(TaskComm485_RequestHardwareConfig(&target) == HAL_BUSY);
  assert(finish() == HAL_ERROR && !active.fan_pwm && !commits);
  fail_bus = 0U;
  fail_pwm = 1U;
  assert(TaskComm485_RequestHardwareConfig(&target) == HAL_OK);
  assert(finish() == HAL_ERROR && !active.fan_pwm && !commits);
  assert(old_fan_stops == 2U && initializations == 1U);
  fail_pwm = 0U;
  hang_pwm = 1U;
  assert(TaskComm485_RequestHardwareConfig(&target) == HAL_OK);
  for (unsigned i = 0; i < 8U; ++i) process_hardware_config();
  now += 10001U;
  assert(finish() == HAL_BUSY && !commits);
  hang_pwm = 0U;
  assert(TaskComm485_RequestHardwareConfig(&target) == HAL_OK);
  allowed = 0U;
  assert(finish() == HAL_BUSY && !commits);
  allowed = 1U;
  assert(TaskComm485_RequestHardwareConfig(&target) == HAL_OK);
  assert(finish() == HAL_OK && active.fan_pwm && commits == 1U);
  assert(clears == 1U && updates == 1U && ready);

  target.fan_pwm = 0U;
  switching_to_legacy = 1U;
  fail_pwm = 1U;
  assert(TaskComm485_RequestHardwareConfig(&target) == HAL_OK);
  assert(finish() == HAL_ERROR && active.fan_pwm && commits == 1U);
  fail_pwm = 0U;
  assert(TaskComm485_RequestHardwareConfig(&target) == HAL_OK);
  for (unsigned i = 0; i < 3U; ++i) process_hardware_config();
  fail_bus = 1U;
  assert(finish() == HAL_ERROR && active.fan_pwm && commits == 1U);
  fail_bus = 0U;
  new_fan_stops = 0U;
  assert(TaskComm485_RequestHardwareConfig(&target) == HAL_OK);
  assert(finish() == HAL_OK && !active.fan_pwm && commits == 2U);
  assert(new_fan_stops == 2U);

  /* No driver switch is required for sensor and command bypass changes. */
  target.lower_door_bypass = target.host_command_bypass = 1U;
  target.temperature_mask = 0U;
  fail_bus = 1U;
  assert(TaskComm485_RequestHardwareConfig(&target) == HAL_OK);
  assert(finish() == HAL_OK && commits == 3U);
  assert(!memcmp(&active, &target, sizeof(active)));
  assert(clears == 3U && updates == 3U);
  fail_bus = 0;
  assert(TaskComm485_RequestBoosterPumpMode(1) == HAL_OK);
  assert(finish() == HAL_OK && pump_mode == 1);
  pump_running = 1;
  assert(TaskComm485_RequestBoosterPumpMode(0) == HAL_BUSY);
  pump_running = 0;
  fail_bus = 1;
  assert(TaskComm485_RequestBoosterPumpMode(0) == HAL_OK);
  assert(finish() == HAL_ERROR && pump_mode == 1);
  fail_bus = 0;
  assert(TaskComm485_RequestBoosterPumpMode(0) == HAL_OK);
  assert(finish() == HAL_OK && pump_mode == 0);
  pump_mode = 1;
  nonzero_inhibit_reasons = 0;
  comm485_queue = (osMessageQueueId_t)1;
  assert(apply_booster_pump_percent(10) == HAL_OK && pressure_set == 10);
  wrong_readback = 266; /* Must compare all 16 bits, not a truncated byte. */
  assert(apply_booster_pump_percent(10) == HAL_ERROR);
  wrong_readback = 0;
  assert(apply_booster_pump_running(1) == HAL_OK && last_reg == 0x2000 && last_value == 1);
  low_level = 1;
  assert(apply_booster_pump_running(1) == HAL_BUSY);
  low_level = 0;
  assert(apply_booster_pump_running(0) == HAL_OK && last_value == 6);
  pending_booster_pump_running_valid = 1;
  pending_booster_pump_running = 1;
  assert(TaskComm485_RequestSetBoosterPumpRunning(0) == HAL_OK);
  assert(booster_stop_requested && !pending_booster_pump_running);
  assert(TaskComm485_RequestSetBoosterPumpRunning(1) == HAL_OK && pending_booster_pump_running);
  pressure_feedback = 31;
  process_booster_pump_feedback();
  uint8_t pressure = 0;
  assert(TaskComm485_GetBoosterPumpFeedback(&pressure) && pressure == 31);
  pump_mode = 0;
  unsigned old_writes = writes, old_reads = reads;
  process_booster_pump_feedback();
  assert(!TaskComm485_GetBoosterPumpFeedback(&pressure));
  assert(TaskComm485_RequestSetBoosterPumpRunning(1) == HAL_OK);
  assert(TaskComm485_RequestSetBoosterPumpPercent(30) == HAL_OK);
  assert(writes == old_writes && reads == old_reads);
  puts("Pump: write0000/read100F, run2000=1/6, priority stop, low level, real1010 and no GPIO polling PASS");
  puts("Hardware: busy/emergency/timeout, old/new driver failures, both directions and atomic commit PASS");
  return 0;
}
