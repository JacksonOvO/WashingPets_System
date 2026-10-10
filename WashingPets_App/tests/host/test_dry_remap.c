#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../Core/Src/auto_stage_dry.c"

static AppRecipe_t recipe;
static uint8_t outputs[AUTO_OUTPUT_COUNT], power[248], top_target, env_valid = 1;
static int16_t cabin = 300;
static uint32_t tick;
static AutoFaultCode fault;
const AppRecipe_t *AppParam_GetActiveRecipe(void) { return &recipe; }
uint32_t AutoDevice_GetProcessTick(void) { return tick; }
uint8_t AutoDevice_ReadInput(AutoInputPoint p) { (void)p; return 1; }
void AutoDevice_SetFault(AutoFaultCode f) { fault = f; }
void AutoDevice_Report(AutoReport r) { (void)r; }
HAL_StatusTypeDef AutoDevice_SetBoosterPumpRunning(uint8_t on) { (void)on; return HAL_OK; }
HAL_StatusTypeDef AutoDevice_SetOutput(AutoOutputPoint p, uint8_t on)
{ outputs[p] = on; return HAL_OK; }
HAL_StatusTypeDef AutoDevice_SetBlower(uint8_t on) { return AutoDevice_SetOutput(AUTO_OUTPUT_BLOWER, on); }
HAL_StatusTypeDef TaskOutput_SetTopExhaustPercent(uint8_t p) { top_target = p; return HAL_OK; }
HAL_StatusTypeDef AutoDevice_SetTopFanRunning(uint8_t on)
{ power[13] = on ? top_target : 0; return HAL_OK; }
HAL_StatusTypeDef TaskComm485_RequestSetPercent(uint8_t s, uint8_t p)
{ assert(s == 12 || s == 16); power[s] = p; return HAL_OK; }
uint8_t TaskComm485_GetEnvironmentData(TaskComm485EnvironmentData *d)
{ memset(d, 0, sizeof(*d)); d->incab_valid = env_valid; d->incab_temperature_x10 = cabin; return 1; }

int main(void)
{
  recipe.dry_target_cabin_temp_x10 = 335;
  recipe.dry_time_ms = 20000;
  DryReset();
  FanFullOn(NULL, NULL);
  ExhaustFanOn(NULL, NULL);
  assert(outputs[AUTO_OUTPUT_BLOWER] && outputs[AUTO_OUTPUT_SWING_MOTOR] && power[13] == 70);
  cabin = 365; tick = 10000;
  assert(Monitor(NULL, NULL) == APP_FLOW_STEP_RUNNING);
  assert(outputs[AUTO_OUTPUT_BLOWER] && outputs[AUTO_OUTPUT_SWING_MOTOR] && !power[16]);
  memset(outputs, 0, sizeof(outputs));
  assert(DryResume(60) == HAL_OK && outputs[AUTO_OUTPUT_BLOWER] && outputs[AUTO_OUTPUT_SWING_MOTOR]);
  HeaterOff(NULL, NULL);
  assert(!outputs[AUTO_OUTPUT_BLOWER] && !outputs[AUTO_OUTPUT_SWING_MOTOR] && power[12] == 60 && power[13] == 70);
  FanOff(NULL, NULL); ExhaustFanOff(NULL, NULL);
  assert(!power[12] && !power[13] && top_target == 100);
  FanFullOn(NULL, NULL); ExhaustFanOn(NULL, NULL);
  env_valid = 0;
  assert(Monitor(NULL, NULL) == APP_FLOW_STEP_FAIL && fault == AUTO_FAULT_DRY_CABIN_TEMP_SENSOR_INVALID);
  assert(!outputs[AUTO_OUTPUT_BLOWER] && !outputs[AUTO_OUTPUT_SWING_MOTOR] && !power[13]);
  env_valid = 1; cabin = 380;
  FanFullOn(NULL, NULL); ExhaustFanOn(NULL, NULL);
  assert(Monitor(NULL, NULL) == APP_FLOW_STEP_FAIL && fault == AUTO_FAULT_DRY_TEMP_CONTROL_FAILED);
  assert(!outputs[AUTO_OUTPUT_BLOWER] && !outputs[AUTO_OUTPUT_SWING_MOTOR] && !power[16]);
  puts("Dry: continuous blower, swing coupling, pause restore, top70%, cold tail, normal finish and temperature safety PASS");
}
