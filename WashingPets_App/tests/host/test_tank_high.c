#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../Core/Src/task_output.c"

static uint32_t tick;
static uint8_t high_reached;
static unsigned valve_zero_requests;
uint32_t HAL_GetTick(void) { return tick; }
uint8_t AutoDevice_ReadInput(AutoInputPoint point)
{
  if (point == AUTO_INPUT_TANK_HIGH) return !high_reached;
  if (point == AUTO_INPUT_UPPER_DOOR_CLOSED || point == AUTO_INPUT_LOWER_DOOR_CLOSED) return 1;
  return 0;
}
uint8_t AutoDevice_IsEmergencyStopped(void) { return 0; }
uint8_t AutoDevice_IsPaused(void) { return 0; }
uint8_t AutoDevice_IsStopRequested(void) { return 0; }
void AutoDevice_SetStopRequested(uint8_t on) { (void)on; }
void AutoDevice_SetFault(AutoFaultCode code) { (void)code; }
void AutoDevice_Report(AutoReport code) { (void)code; }
uint8_t TaskComm485_IsRs485ActuatorMode(void) { return 0; }
HAL_StatusTypeDef TaskComm485_RequestSetProportionalValveAngle(uint8_t angle)
{ assert(angle == 0); ++valve_zero_requests; return HAL_OK; }
HAL_StatusTypeDef TaskComm485_RequestSetPercent(uint8_t station, uint8_t percent)
{ (void)station; (void)percent; return HAL_OK; }
void HAL_GPIO_Init(GPIO_TypeDef *port, GPIO_InitTypeDef *init) { (void)port; (void)init; }
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state)
{ (void)port; (void)pin; (void)state; }

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
  puts("High level: real output arbitration, exact 10s cutoff, immediate re-enable, bounce and tick wrap PASS");
}
