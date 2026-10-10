#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../Core/Src/task_output.c"

static uint32_t tick;
static uint8_t high_reached, emergency, pump_mode, cabin_invalid, low_missing;
static AutoFaultCode fault;
static uint8_t stop;
static int16_t cabin_temp = 300;
static uint8_t written[7][16];
static uint8_t top_percent;
static unsigned valve_zero_requests;
uint8_t AppParam_BoosterPumpUsesRs485(void) { return pump_mode; }
void TaskComm485_RequestSafeStop(void) { }
uint8_t TaskComm485_GetEnvironmentData(TaskComm485EnvironmentData *d)
{ memset(d, 0, sizeof(*d)); d->incab_valid = !cabin_invalid; d->incab_temperature_x10 = cabin_temp; return 1; }

uint32_t HAL_GetTick(void) { return tick; }
uint8_t AutoDevice_ReadInput(AutoInputPoint point)
{
  if (point == AUTO_INPUT_TANK_HIGH) return !high_reached;
  if (point == AUTO_INPUT_TANK_LOW) return low_missing;
  if (point == AUTO_INPUT_UPPER_DOOR_CLOSED || point == AUTO_INPUT_LOWER_DOOR_CLOSED) return 1;
  return 0;
}
uint8_t AutoDevice_IsEmergencyStopped(void) { return emergency; }
uint8_t TaskComm485_HardwareConfigBusy(void) { return 0; }
uint8_t AutoDevice_IsPaused(void) { return 0; }
uint8_t AutoDevice_IsStopRequested(void) { return 0; }
void AutoDevice_SetStopRequested(uint8_t on) { stop = on; }
void AutoDevice_SetFault(AutoFaultCode code) { fault = code; }
void AutoDevice_Report(AutoReport code) { (void)code; }
uint8_t TaskComm485_IsRs485ActuatorMode(void) { return 0; }
HAL_StatusTypeDef TaskComm485_RequestSetProportionalValveAngle(uint8_t angle)
{ assert(angle == 0); ++valve_zero_requests; return HAL_OK; }
HAL_StatusTypeDef TaskComm485_RequestSetPercent(uint8_t station, uint8_t percent)
{ assert(station == RS485_STATION_TOP_EXHAUST_FAN); top_percent = percent; return HAL_OK; }
void HAL_GPIO_Init(GPIO_TypeDef *port, GPIO_InitTypeDef *init) { (void)port; (void)init; }
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state)
{
  for (unsigned bit = 0; bit < 16; ++bit)
    if (pin & (1U << bit)) written[(uintptr_t)port][bit] = state == GPIO_PIN_SET;
}

static void apply_refill(void)
{
  auto_request[AUTO_OUTPUT_TAP_WATER_VALVE] = 1;
  auto_request[AUTO_OUTPUT_STANDBY2_VALVE] = 1;
  auto_request[AUTO_OUTPUT_BOOSTER_PUMP_RUN] = 1;
  output_apply_all();
  assert(TaskOutput_GetActual(AUTO_OUTPUT_BOOSTER_PUMP_RUN));
}

static void check_cutoff(uint32_t start)
{
  output_auto_mode = requested_auto_mode = 1;
  tank_high_level_tracking = tank_high_level_protect_latched = 0;
  valve_zero_requests = 0;
  high_reached = 1;
  tick = start;
  apply_refill();
  assert(!TaskOutput_IsTankHighStopActive());
  tick = start + 9999U;
  apply_refill();
  assert(TaskOutput_GetActual(AUTO_OUTPUT_TAP_WATER_VALVE));
  assert(TaskOutput_GetActual(AUTO_OUTPUT_STANDBY2_VALVE));
  tick = start + 10000U;
  apply_refill();
  assert(TaskOutput_IsTankHighStopActive());
  assert(!TaskOutput_GetActual(AUTO_OUTPUT_TAP_WATER_VALVE));
  assert(!TaskOutput_GetActual(AUTO_OUTPUT_STANDBY2_VALVE));
  assert(valve_zero_requests == 1);
  tick += 10;
  apply_refill();
  assert(valve_zero_requests == 1);
  high_reached = 0;
  tick += 10;
  apply_refill();
  assert(!TaskOutput_IsTankHighStopActive());
  assert(TaskOutput_GetActual(AUTO_OUTPUT_TAP_WATER_VALVE));
}

int main(void)
{
  check_cutoff(0);
  check_cutoff(UINT32_MAX - 5000U);
  high_reached = 1; tick = 50000; apply_refill();
  high_reached = 0; tick = 55000; apply_refill();
  high_reached = 1; tick = 56000; apply_refill();
  tick = 65000; apply_refill(); assert(!TaskOutput_IsTankHighStopActive());
  tick = 66000; apply_refill(); assert(TaskOutput_IsTankHighStopActive());
  assert(AUTO_OUTPUT_COUNT == 46 && AUTO_OUTPUT_UPPER_LOCK == 40);
  for (unsigned i = 1; i < AUTO_OUTPUT_COUNT; ++i)
  {
    assert(output_hw_table[i].port && output_hw_table[i].pin);
    assert(output_pin_state(i, 1) == GPIO_PIN_RESET && output_pin_state(i, 0) == GPIO_PIN_SET);
    for (unsigned j = 1; j < i; ++j)
      assert(output_hw_table[i].port != output_hw_table[j].port ||
             output_hw_table[i].pin != output_hw_table[j].pin);
  }
  output_auto_mode = requested_auto_mode = 0;
  TaskOutput_InitHardware();
  for (unsigned i = AUTO_OUTPUT_SPARE_1; i <= AUTO_OUTPUT_SPARE_8; ++i)
    assert(!actual_output[i]);
  for (unsigned i = AUTO_OUTPUT_UPPER_LOCK; i <= AUTO_OUTPUT_REAR_LOCK_2; ++i)
  {
    manual_request[i] = 1;
    output_apply(i);
  }
  OutputCommand mode = {.type = OUTPUT_COMMAND_SET_MODE, .enabled = 1};
  output_process_command(&mode);
  output_apply_all();
  for (unsigned i = AUTO_OUTPUT_UPPER_LOCK; i <= AUTO_OUTPUT_REAR_LOCK_2; ++i)
    assert(actual_output[i]);
  mode.type = OUTPUT_COMMAND_CLEAR_SOURCE;
  mode.source = APP_OUTPUT_SOURCE_AUTO;
  output_process_command(&mode);
  output_apply_all();
  for (unsigned i = AUTO_OUTPUT_UPPER_LOCK; i <= AUTO_OUTPUT_REAR_LOCK_2; ++i)
    assert(actual_output[i]);
  mode.type = OUTPUT_COMMAND_SET_TOP_EXHAUST_PERCENT;
  mode.enabled = 70;
  output_process_command(&mode);
  mode.type = OUTPUT_COMMAND_SET_TOP_RUNNING;
  mode.enabled = 1;
  output_process_command(&mode);
  assert(top_percent == 70);
  auto_request[AUTO_OUTPUT_BLOWER] = auto_request[AUTO_OUTPUT_SWING_MOTOR] = 1;
  output_apply_all();
  assert(actual_output[AUTO_OUTPUT_BLOWER] && actual_output[AUTO_OUTPUT_SWING_MOTOR]);
  cabin_invalid = 1;
  output_apply_all();
  assert(!actual_output[AUTO_OUTPUT_BLOWER] && !actual_output[AUTO_OUTPUT_SWING_MOTOR]);
  assert(fault == AUTO_FAULT_DRY_CABIN_TEMP_SENSOR_INVALID && stop);
  cabin_invalid = 0;
  output_apply_all();
  fault = AUTO_FAULT_NONE; stop = 0;
  auto_request[AUTO_OUTPUT_BLOWER] = auto_request[AUTO_OUTPUT_SWING_MOTOR] = 1;
  cabin_temp = 450;
  output_apply_all();
  assert(!actual_output[AUTO_OUTPUT_BLOWER] && !actual_output[AUTO_OUTPUT_SWING_MOTOR]);
  assert(fault == AUTO_FAULT_DRY_TEMP_CONTROL_FAILED && stop);
  cabin_temp = 300;
  output_apply_all();
  fault = AUTO_FAULT_NONE; stop = 0;
  auto_request[AUTO_OUTPUT_BOOSTER_PUMP_RUN] = 1;
  output_apply_all();
  assert(actual_output[AUTO_OUTPUT_BOOSTER_PUMP_RUN]);
  low_missing = 1;
  output_apply_all();
  assert(!actual_output[AUTO_OUTPUT_BOOSTER_PUMP_RUN]);
  assert(fault == AUTO_FAULT_TANK_LOW_PROTECT && stop);
  low_missing = 0;
  output_apply_all();
  auto_request[AUTO_OUTPUT_BOOSTER_PUMP_RUN] = 1;
  pump_mode = 1;
  output_apply_all();
  assert(!actual_output[AUTO_OUTPUT_BOOSTER_PUMP_RUN]);
  emergency = 1;
  output_apply_all();
  for (unsigned i = 1; i < AUTO_OUTPUT_COUNT; ++i)
    assert(actual_output[i] == (i == AUTO_OUTPUT_LIGHTING));
  assert(!written[(uintptr_t)GPIOB][4] && written[(uintptr_t)GPIOC][14]);
  puts("Outputs: 45 unique active-low pins, six-lock handover, top70%, blower protection, GPIO/485 exclusion and emergency lighting PASS");
  puts("High level: real output arbitration, exact 10s cutoff, immediate re-enable, bounce and tick wrap PASS");
}
