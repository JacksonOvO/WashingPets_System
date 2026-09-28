#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "stm32f4xx_hal.h"
#define __MAIN_H
HAL_StatusTypeDef UART2_Send(const uint8_t *, uint16_t);
#include "../../Core/Src/task_water_tank.c"

static uint32_t tick, inhibit;
static uint8_t auto_mode, running, paused, emergency, stopped, high_stop, low;
static uint8_t self_test, cleaning, auto_ack, valve_angle, requested_angle;
static uint16_t process_stage, clean_step;
static AutoFaultCode fault;
static AppRecipe_t recipe;
static AutoSensorData data;
static uint8_t outputs[AUTO_OUTPUT_COUNT];
static unsigned pump_commands, other_water_commands, alarms;

uint32_t HAL_GetTick(void) { return tick; }
uint8_t AutoProcess_IsRunning(void) { return running; }
uint16_t AutoProcess_GetCurrentStage(void) { return process_stage; }
uint16_t AutoProcess_GetCurrentStepId(void) { return 40; }
const AppRecipe_t *AppParam_GetActiveRecipe(void) { return &recipe; }
uint8_t AutoDevice_IsPaused(void) { return paused; }
uint8_t AutoDevice_IsEmergencyStopped(void) { return emergency; }
uint8_t AutoDevice_IsStopRequested(void) { return stopped; }
void AutoDevice_SetStopRequested(uint8_t on) { stopped = on; }
AutoFaultCode AutoDevice_GetFault(void) { return fault; }
void AutoDevice_ReportFault(AutoFaultCode code, AutoReport report)
{ (void)report; fault = code; ++alarms; }
uint8_t AutoDevice_GetSensorData(AutoSensorData *out) { *out = data; return 1; }
uint8_t AutoDevice_ReadInput(AutoInputPoint point)
{ return point == AUTO_INPUT_TANK_LOW ? low : 1; }
HAL_StatusTypeDef AutoDevice_SetOutput(AutoOutputPoint point, uint8_t on)
{
  if (point != AUTO_OUTPUT_TAP_WATER_VALVE && point != AUTO_OUTPUT_STANDBY2_VALVE)
    ++other_water_commands;
  outputs[point] = on;
  return HAL_OK;
}
HAL_StatusTypeDef AutoDevice_SetBoosterPumpRunning(uint8_t on)
{ outputs[AUTO_OUTPUT_BOOSTER_PUMP_RUN] = on; ++pump_commands; return HAL_OK; }
void AutoDevice_AllDangerousOutputsOff(void) { memset(outputs, 0, sizeof(outputs)); }
uint8_t TaskOutput_IsAutoMode(void) { return auto_mode; }
uint8_t TaskOutput_IsTankHighStopActive(void) { return high_stop; }
uint8_t TaskSelfTest_IsRunning(void) { return self_test; }
uint8_t TaskClean_IsRunning(void) { return cleaning; }
uint16_t TaskClean_GetCurrentStepId(void) { return clean_step; }
uint32_t TaskComm485_GetInhibitReasons(void) { return inhibit; }
void TaskComm485_ReleaseNonzero(uint32_t bits) { inhibit &= ~bits; }
uint8_t TaskComm485_GetLastProportionalValveAngle(void) { return valve_angle; }
HAL_StatusTypeDef TaskComm485_RequestSetProportionalValveAngle(uint8_t angle)
{ requested_angle = angle; if (auto_ack) valve_angle = angle; return HAL_OK; }

static void setup(void)
{
  tick = inhibit = 0;
  auto_mode = running = auto_ack = 1;
  previous_auto_mode = 1;
  paused = emergency = stopped = high_stop = low = self_test = cleaning = 0;
  fault = 0;
  pump_commands = other_water_commands = alarms = 0;
  memset(outputs, 0, sizeof(outputs));
  memset(&data, 0, sizeof(data));
  memset(&recipe, 0, sizeof(recipe));
  recipe.tank_target_temp_x10 = 400;
  recipe.tank_temp_tolerance_x10 = 20;
  data.spare_temperature_valid = data.tank_temperature_valid = 1;
  data.spare_temperature_x10 = 400;
  data.tank_temperature_x10 = 400;
  process_stage = AUTO_PROCESS_STAGE_SPRAY;
  wash_refill_active = wash_fill_running = 0;
  self_clean_prepare_requested = hot_drain_active = water_tank_recirc_active = 0;
  current_valve_angle = WATER_TANK_VALVE_UNKNOWN;
  valve_angle = requested_angle = 0;
  last_recipe_target_x10 = WATER_TANK_TARGET_UNKNOWN;
}

static void sample(uint32_t now, int16_t mix)
{
  tick = data.spare_update_tick = now;
  data.spare_temperature_x10 = mix;
  process_water_tank();
}

int main(void)
{
  setup();
  sample(0, 400);
  assert(valve_angle == 37 && outputs[AUTO_OUTPUT_TAP_WATER_VALVE]);
  assert(outputs[AUTO_OUTPUT_STANDBY2_VALVE]);
  sample(4999, 420); assert(valve_angle == 37);
  sample(5000, 420); assert(valve_angle == 40);
  sample(10000, 380); assert(valve_angle == 37);
  sample(15000, 410); assert(valve_angle == 37);
  sample(20000, 390); assert(valve_angle == 37);
  process_stage = AUTO_PROCESS_STAGE_BATH;
  sample(25000, 400);
  process_stage = AUTO_PROCESS_STAGE_CLEAN;
  sample(30000, 400);
  assert(pump_commands == 0 && other_water_commands == 0);
  high_stop = 1;
  sample(31000, 400);
  assert(!outputs[AUTO_OUTPUT_TAP_WATER_VALVE] && !outputs[AUTO_OUTPUT_STANDBY2_VALVE]);
  assert(valve_angle == 0 && pump_commands == 0);
  high_stop = 0;
  sample(32000, 400);
  assert(outputs[AUTO_OUTPUT_TAP_WATER_VALVE] && valve_angle == 37);
  /* Simulate a cutoff and level drop entirely between two tank-task ticks. */
  valve_angle = 0;
  auto_ack = 0;
  sample(33000, 400);
  assert(!outputs[AUTO_OUTPUT_STANDBY2_VALVE] && requested_angle == 37);
  valve_angle = requested_angle;
  sample(34000, 400);
  assert(outputs[AUTO_OUTPUT_STANDBY2_VALVE]);
  auto_ack = 1;
  process_stage = AUTO_PROCESS_STAGE_DRY;
  sample(35000, 400);
  assert(outputs[AUTO_OUTPUT_TAP_WATER_VALVE] && !outputs[AUTO_OUTPUT_STANDBY2_VALVE]);
  assert(valve_angle == 90);
  high_stop = 1;
  sample(36000, 400);
  assert(!outputs[AUTO_OUTPUT_TAP_WATER_VALVE] && valve_angle == 0);

  for (unsigned failure = 0; failure < 6; ++failure)
  {
    setup();
    sample(0, 400);
    if (failure == 0) data.spare_temperature_valid = 0;
    if (failure == 1) tick = 5001;
    if (failure == 2) paused = 1;
    if (failure == 3) stopped = 1;
    if (failure == 4) emergency = 1;
    if (failure == 5) inhibit = COMM485_INHIBIT_FAULT;
    process_water_tank();
    assert(!outputs[AUTO_OUTPUT_TAP_WATER_VALVE] && !outputs[AUTO_OUTPUT_STANDBY2_VALVE]);
    assert(valve_angle == 0);
    if (failure < 2)
    {
      assert(fault == AUTO_FAULT_SPARE_TEMP_SENSOR_INVALID && stopped && alarms == 1);
      process_water_tank(); assert(alarms == 1);
    }
  }
  setup(); self_test = 1; sample(0, 400);
  assert(!outputs[AUTO_OUTPUT_TAP_WATER_VALVE]);
  setup(); cleaning = 1; clean_step = 20; sample(0, 400);
  assert(!outputs[AUTO_OUTPUT_TAP_WATER_VALVE]);
  setup(); process_stage = AUTO_PROCESS_STAGE_ENTRY_ENV; sample(0, 400);
  assert(!outputs[AUTO_OUTPUT_TAP_WATER_VALVE]);
  setup(); auto_mode = 0; sample(0, 400);
  assert(!outputs[AUTO_OUTPUT_TAP_WATER_VALVE]);
  puts("Tank: CH4 deadband/direction, stage ownership, high-level resume, cold fill, faults and interlocks PASS");
}
