#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../Core/Src/auto_stage_clean.c"

static AppRecipe_t recipe;
static uint32_t tick;
static uint8_t power[248], temp_valid;
static unsigned heat_commands;
static AutoFaultCode fault;
const AppRecipe_t *AppParam_GetActiveRecipe(void) { return &recipe; }
uint32_t AutoDevice_GetProcessTick(void) { return tick; }
uint8_t AutoDevice_ReadInput(AutoInputPoint point) { (void)point; return 1; }
uint8_t AutoDevice_GetSensorData(AutoSensorData *data)
{
  memset(data, 0, sizeof(*data));
  data->pipe_temperature_valid = temp_valid;
  data->pipe_temperature_x10 = 400;
  return 1;
}
void AutoDevice_SetFault(AutoFaultCode code) { fault = code; }
void AutoDevice_Report(AutoReport report) { (void)report; }
void AutoDevice_SetAllShowerValves(uint8_t on) { (void)on; }
HAL_StatusTypeDef AutoDevice_SetBoosterPumpPercent(uint8_t p) { (void)p; return HAL_OK; }
HAL_StatusTypeDef AutoDevice_SetBoosterPumpRunning(uint8_t on) { (void)on; return HAL_OK; }
HAL_StatusTypeDef TaskComm485_RequestSetPercent(uint8_t station, uint8_t percent)
{
  power[station] = percent;
  if (station == RS485_STATION_LEFT_HEATER && percent) ++heat_commands;
  return HAL_OK;
}

static void setup(uint32_t duration, uint32_t start)
{
  memset(&recipe, 0, sizeof(recipe));
  memset(power, 0, sizeof(power));
  recipe.clean_time_ms = duration;
  recipe.tank_target_temp_x10 = 400;
  recipe.pipe_temp_tolerance_x10 = 20;
  heat_commands = fault = 0;
  temp_valid = 1;
  tick = start;
  CleanInit();
  TimerStart(NULL, NULL);
  DryPreheat(NULL, NULL);
}

static void check_start(uint32_t duration, uint32_t start)
{
  uint32_t wait = duration > 60000 ? duration - 60000 : 0;
  setup(duration, start);
  if (wait)
  {
    assert(!dry_preheat_started && !heat_commands);
    assert(CleanResume(80) == HAL_OK && !heat_commands);
    tick = start + wait - 1;
    assert(Monitor(NULL, NULL) == APP_FLOW_STEP_RUNNING);
    assert(!heat_commands);
    tick = start + wait;
    assert(Monitor(NULL, NULL) == APP_FLOW_STEP_RUNNING);
  }
  assert(dry_preheat_started && heat_commands == 1);
  Monitor(NULL, NULL);
  assert(heat_commands == 1);
  memset(power, 0, sizeof(power));
  assert(CleanResume(80) == HAL_OK);
  assert(power[RS485_STATION_LEFT_FAN] == 50);
  assert(power[RS485_STATION_LEFT_HEATER] == 100);
  tick = start + duration;
  assert(Monitor(NULL, NULL) == APP_FLOW_STEP_DONE);
}

int main(void)
{
  check_start(120000, 0);
  check_start(90000, 0);
  check_start(60000, 0);
  check_start(30000, 0);
  check_start(120000, UINT32_MAX - 30000);
  setup(0, 0);
  assert(Monitor(NULL, NULL) == APP_FLOW_STEP_DONE && !heat_commands);
  setup(120000, 0);
  tick = 120000;
  assert(Monitor(NULL, NULL) == APP_FLOW_STEP_DONE && !heat_commands);
  setup(120000, 0);
  tick = 60000;
  temp_valid = 0;
  assert(Monitor(NULL, NULL) == APP_FLOW_STEP_FAIL && !heat_commands && fault);
  setup(30000, 0);
  temp_valid = 0;
  assert(Monitor(NULL, NULL) == APP_FLOW_STEP_FAIL);
  assert(power[RS485_STATION_LEFT_HEATER] == 0);
  puts("Cleaning: last-60s preheat, short/zero durations, resume, wrap and temperature fault PASS");
}
