#include <assert.h>
#include <stdio.h>
#include <string.h>
#define __MAIN_H
#include "../../Core/Src/app_flow.c"
#include "../../Core/Src/clean_process.c"

static AppRecipe_t recipe;
static uint8_t outputs[AUTO_OUTPUT_COUNT], power[248], top_on;
static uint32_t tick;
static uint8_t clean_low, disinfectant_low, paused;
static AutoFaultCode fault;
static AutoReport report;
const AppRecipe_t *AppParam_GetActiveRecipe(void) { return &recipe; }
uint32_t AutoDevice_GetProcessTick(void) { return tick; }
uint8_t AutoDevice_IsEmergencyStopped(void) { return 0; }
uint8_t AutoDevice_IsStopRequested(void) { return 0; }
uint8_t AutoDevice_IsPaused(void) { return paused; }
uint8_t AutoDevice_ReadInput(AutoInputPoint p)
{
  if (p == AUTO_INPUT_CLEAN_LIQUID_LOW) return clean_low;
  if (p == AUTO_INPUT_DISINFECTANT_LOW) return disinfectant_low;
  return p == AUTO_INPUT_UPPER_DOOR_CLOSED || p == AUTO_INPUT_LOWER_DOOR_CLOSED;
}
void AutoDevice_SetFault(AutoFaultCode f) { fault = f; }
AutoFaultCode AutoDevice_GetFault(void) { return fault; }
void AutoDevice_Report(AutoReport r) { report = r; }
void AutoDevice_ReportFault(AutoFaultCode f, AutoReport r) { (void)f; (void)r; }
void TaskComm485_ReleaseNonzero(uint32_t r) { (void)r; }
void AutoDevice_AllDangerousOutputsOff(void) { memset(outputs, 0, sizeof(outputs)); }
HAL_StatusTypeDef AutoDevice_SetOutput(AutoOutputPoint p, uint8_t on)
{ outputs[p] = on; return HAL_OK; }
HAL_StatusTypeDef AutoDevice_SetBlower(uint8_t on)
{ return AutoDevice_SetOutput(AUTO_OUTPUT_BLOWER, on); }
HAL_StatusTypeDef AutoDevice_SetTopFanRunning(uint8_t on) { top_on = on; return HAL_OK; }
HAL_StatusTypeDef AutoDevice_SetBoosterPumpRunning(uint8_t on)
{ return AutoDevice_SetOutput(AUTO_OUTPUT_BOOSTER_PUMP_RUN, on); }
HAL_StatusTypeDef AutoDevice_SetBoosterPumpPercent(uint8_t p) { (void)p; return HAL_OK; }
HAL_StatusTypeDef TaskComm485_RequestSetPercent(uint8_t s, uint8_t p)
{ assert(s == 12 || s == 16); power[s] = p; return HAL_OK; }

static void check_valves(uint8_t top, uint8_t lower, uint8_t toilet)
{
  assert(outputs[AUTO_OUTPUT_SELF_CLEAN_TOP_VALVE] == top);
  assert(outputs[AUTO_OUTPUT_SELF_CLEAN_LOWER_VALVE] == lower);
  assert(outputs[AUTO_OUTPUT_SELF_CLEAN_TOILET_VALVE] == toilet);
  assert(!outputs[AUTO_OUTPUT_BATH_PUMP]);
}

int main(void)
{
  CleanProcess_Init();
  AppFlow_Start(&clean_flow);
  assert(CheckAllow(&clean_flow, NULL) == APP_FLOW_STEP_DONE);
  assert(outputs[AUTO_OUTPUT_FLOOR_LIFT]);
  for (unsigned round = 0; round < 2; ++round)
  {
    assert((round ? DisinfectantSprayOn(NULL, NULL) : CleanLiquidSprayOn(NULL, NULL)) == APP_FLOW_STEP_DONE);
    check_valves(1, 1, 1);
    assert(outputs[AUTO_OUTPUT_SELF_CLEAN_PUMP] && outputs[AUTO_OUTPUT_BOOSTER_PUMP_RUN]);
    assert(outputs[AUTO_OUTPUT_CONDITIONER_VALVE] == !round);
    assert(outputs[AUTO_OUTPUT_DISINFECTANT_VALVE] == round);
    LiquidPathOff(NULL, NULL);
    check_valves(0, 0, 0);
    assert(!outputs[AUTO_OUTPUT_SELF_CLEAN_PUMP] && !outputs[AUTO_OUTPUT_BOOSTER_PUMP_RUN]);
    TopRinseOn(NULL, NULL); check_valves(1, 0, 0);
    SwitchLeftUpperRinse(NULL, NULL); check_valves(0, 1, 0);
    SwitchLowerRearRinse(NULL, NULL); check_valves(0, 0, 1);
    RinsePathOff(NULL, NULL); check_valves(0, 0, 0);
    assert(outputs[AUTO_OUTPUT_FLOOR_LIFT]);
  }
  const uint16_t ids[] = {30, 70, 90, 110, 140, 180, 200, 220, 260, 290};
  for (unsigned i = 0; i < sizeof(ids)/sizeof(ids[0]); ++i)
  {
    assert(AppFlow_JumpToStep(&clean_flow, ids[i]));
    clean_safe_stop();
    assert(!outputs[AUTO_OUTPUT_FLOOR_LIFT]);
    assert(CleanProcess_Resume() == HAL_OK && outputs[AUTO_OUTPUT_FLOOR_LIFT]);
    assert(!outputs[AUTO_OUTPUT_SWING_MOTOR]);
    if (ids[i] == 290) assert(!outputs[AUTO_OUTPUT_UV_LAMP] && !outputs[AUTO_OUTPUT_BLOWER]);
  }
  clean_safe_stop();
  Finish(NULL, NULL);
  assert(!outputs[AUTO_OUTPUT_FLOOR_LIFT] && !top_on);
  for (unsigned round = 0; round < 2; ++round)
  {
    uint16_t start = round ? 130 : 20;
    uint16_t wait = round ? 140 : 30;
    AutoFaultCode expected = round ? AUTO_FAULT_DISINFECTANT_LOW : AUTO_FAULT_CLEAN_LIQUID_LOW;
    clean_low = !round; disinfectant_low = round; fault = AUTO_FAULT_NONE;
    assert((round ? DisinfectantSprayOn(NULL, NULL) : CleanLiquidSprayOn(NULL, NULL)) == APP_FLOW_STEP_FAIL);
    assert(fault == expected && report == AUTO_REPORT_SENSOR_FAULT);
    assert(!outputs[AUTO_OUTPUT_SELF_CLEAN_PUMP] && !outputs[AUTO_OUTPUT_BOOSTER_PUMP_RUN]);
    clean_low = round; disinfectant_low = !round;
    assert((round ? DisinfectantSprayOn(NULL, NULL) : CleanLiquidSprayOn(NULL, NULL)) == APP_FLOW_STEP_DONE);
    assert(outputs[AUTO_OUTPUT_SELF_CLEAN_PUMP]);
    AppFlow_Start(&clean_flow); assert(AppFlow_JumpToStep(&clean_flow, wait));
    clean_low = !round; disinfectant_low = round;
    CleanProcess_Process();
    assert(CleanProcess_IsFailed() && fault == expected);
    assert(!outputs[AUTO_OUTPUT_FLOOR_LIFT] && !top_on);
    AppFlow_Start(&clean_flow); assert(AppFlow_JumpToStep(&clean_flow, start));
    paused = 1; fault = AUTO_FAULT_NONE;
    CleanProcess_Process();
    assert(!CleanProcess_IsFailed() && fault == AUTO_FAULT_NONE);
    assert(CleanProcess_Resume() == HAL_ERROR && fault == expected);
    assert(!outputs[AUTO_OUTPUT_SELF_CLEAN_PUMP] && !outputs[AUTO_OUTPUT_FLOOR_LIFT]);
    paused = 0; clean_low = disinfectant_low = 1;
    assert(step_liquid_available(70) && step_liquid_available(180) && step_liquid_available(260));
  }
  puts("Self-clean: per-liquid precheck/running/resume protection, independent rounds, valves, lift and cooling PASS");
}
