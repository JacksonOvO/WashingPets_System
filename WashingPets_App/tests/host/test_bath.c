#include <assert.h>
#include <stdio.h>
#include <string.h>
#define __MAIN_H
#include "../../Core/Src/app_flow.c"
#include "../../Core/Src/auto_stage_bath.c"

static AppRecipe_t recipe;
static uint8_t outputs[AUTO_OUTPUT_COUNT];
static uint32_t tick;
static uint8_t emergency, paused, stop_requested, low_water, door_open, sync_fail;
static AutoFaultCode fault;

const AppRecipe_t *AppParam_GetActiveRecipe(void) { return &recipe; }
uint32_t AutoDevice_GetProcessTick(void) { return tick; }
uint8_t AutoDevice_IsEmergencyStopped(void) { return emergency; }
uint8_t AutoDevice_IsStopRequested(void) { return stop_requested; }
uint8_t AutoDevice_IsPaused(void) { return paused; }
uint8_t AutoDevice_ReadInput(AutoInputPoint point)
{
  if (point == AUTO_INPUT_TANK_LOW) return low_water;
  if (point == AUTO_INPUT_UPPER_DOOR_CLOSED || point == AUTO_INPUT_LOWER_DOOR_CLOSED)
    return !door_open;
  return 0;
}
uint8_t AutoDevice_GetSensorData(AutoSensorData *data)
{
  memset(data, 0, sizeof(*data));
  data->pipe_temperature_valid = 1;
  data->pipe_temperature_x10 = 400;
  return 1;
}
void AutoDevice_SetFault(AutoFaultCode code) { fault = code; }
void AutoDevice_Report(AutoReport report) { (void)report; }
void AutoDevice_AllDangerousOutputsOff(void) { memset(outputs, 0, sizeof(outputs)); }
HAL_StatusTypeDef AutoDevice_SetOutput(AutoOutputPoint point, uint8_t on)
{
  if (recipe.bath_liquid_time_ms == 0 &&
      (point == AUTO_OUTPUT_SCREW_PUMP || point == AUTO_OUTPUT_DISINFECTANT_VALVE))
    assert(!on);
  outputs[point] = on;
  assert(!(outputs[AUTO_OUTPUT_SCREW_PUMP] && outputs[AUTO_OUTPUT_BOOSTER_PUMP_RUN]));
  return HAL_OK;
}
HAL_StatusTypeDef AutoDevice_SetBoosterPumpRunning(uint8_t on)
{
  return AutoDevice_SetOutput(AUTO_OUTPUT_BOOSTER_PUMP_RUN, on);
}
HAL_StatusTypeDef AutoDevice_SetBoosterPumpPercent(uint8_t p) { (void)p; return HAL_OK; }
void AutoDevice_SetAllShowerValves(uint8_t on)
{
  outputs[AUTO_OUTPUT_SHOWER_UPPER] = on;
}
HAL_StatusTypeDef TaskOutput_Sync(uint32_t ms) { (void)ms; return sync_fail ? HAL_ERROR : HAL_OK; }
uint8_t TaskOutput_GetActual(AutoOutputPoint point) { return outputs[point]; }

static void setup(AppFlowContext *ctx, uint32_t count)
{
  memset(&recipe, 0, sizeof(recipe));
  recipe.bath_cycle_count = count;
  recipe.bath_on_time_ms = 2000;
  recipe.bath_pause_time_ms = 1000;
  recipe.bath_liquid_time_ms = APP_RECIPE_DEFAULT_BATH_LIQUID_MS;
  recipe.tank_target_temp_x10 = 400;
  recipe.pipe_temp_tolerance_x10 = 20;
  emergency = paused = stop_requested = low_water = door_open = sync_fail = 0;
  fault = 0;
  tick = 0;
  AutoDevice_AllDangerousOutputsOff();
  stage.init();
  AppFlow_Init(ctx, stage.name, stage.steps, stage.count);
  AppFlow_Start(ctx);
}

static void run_cycles(uint32_t count, uint8_t pause_test, uint8_t wrap, uint32_t dose_ms)
{
  AppFlowContext ctx;
  uint32_t feed_ms = 0, water_ms = 0, pause_mask = 0;
  setup(&ctx, count);
  recipe.bath_liquid_time_ms = dose_ms;
  if (wrap) tick = UINT32_MAX - 2000U;
  for (unsigned i = 0; i < 100000 && !ctx.finished && !ctx.failed; ++i)
  {
    AppFlow_Process(&ctx);
    uint16_t step = AppFlow_GetCurrentStepId(&ctx);
    uint32_t bit = step == 70 ? 1U : step == 80 ? 2U : step == 85 ? 4U : 0U;
    if (pause_test && bit && !(pause_mask & bit) && ctx.action_called &&
        tick - phase_tick >= 500)
    {
      uint32_t saved_tick = phase_tick, saved_count = completed_cycles;
      BathOutputPhase saved_phase = output_phase;
      paused = 1;
      AutoDevice_AllDangerousOutputsOff();
      for (unsigned j = 0; j < 100; ++j) AppFlow_Process(&ctx);
      assert(stage.resume(step) == HAL_OK);
      assert(phase_tick == saved_tick && completed_cycles == saved_count);
      assert(output_phase == saved_phase);
      paused = 0;
      pause_mask |= bit;
    }
    feed_ms += outputs[AUTO_OUTPUT_SCREW_PUMP] * 10;
    water_ms += outputs[AUTO_OUTPUT_BOOSTER_PUMP_RUN] * 10;
    tick += 10;
  }
  assert(ctx.finished && !ctx.failed && !fault);
  assert(completed_cycles == count);
  assert(feed_ms == count * dose_ms);
  assert(water_ms == (count ? count * 2000 + 5000 : 0));
  assert(!outputs[AUTO_OUTPUT_SCREW_PUMP] && !outputs[AUTO_OUTPUT_BOOSTER_PUMP_RUN]);
  assert(!outputs[AUTO_OUTPUT_DISINFECTANT_VALVE] && !outputs[AUTO_OUTPUT_SHOWER_UPPER]);
  if (pause_test) assert(pause_mask == 7);
}

int main(void)
{
  AppFlowContext ctx;
  run_cycles(0, 0, 0, 3000);
  run_cycles(1, 0, 0, 3000);
  run_cycles(5, 1, 0, 3000);
  run_cycles(100, 0, 0, 3000);
  run_cycles(2, 0, 1, 5000);
  run_cycles(3, 1, 0, 1500);
  run_cycles(3, 0, 0, 0);
  run_cycles(1, 0, 0, 60000);
  for (unsigned failure = 0; failure < 5; ++failure)
  {
    setup(&ctx, 2);
    while (output_phase != BATH_FEED) { AppFlow_Process(&ctx); tick += 10; }
    if (failure == 0) emergency = 1;
    if (failure == 1) stop_requested = 1;
    if (failure == 2) low_water = 1;
    if (failure == 3) door_open = 1;
    if (failure == 4) sync_fail = 1;
    for (unsigned i = 0; i < 1000 && !ctx.failed; ++i)
    { AppFlow_Process(&ctx); tick += 10; }
    assert(ctx.failed);
    assert(!outputs[AUTO_OUTPUT_SCREW_PUMP] && !outputs[AUTO_OUTPUT_BOOSTER_PUMP_RUN]);
  }
  puts("Bath: cycles, exact dosing/water times, pause/resume, tick wrap and safe stop PASS");
}
