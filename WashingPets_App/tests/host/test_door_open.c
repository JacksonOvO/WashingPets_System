#include <assert.h>
#include <stdio.h>
#include <string.h>
#define __MAIN_H
#include "../../Core/Src/auto_device.c"
#include "../../Core/Src/app_flow.c"
#if DOOR_TEST_EXIT
#include "../../Core/Src/auto_stage_exit.c"
#else
#include "../../Core/Src/auto_stage_entry.c"
#endif

static uint32_t tick;
static unsigned stopped;
static AppHardwareConfig_t hardware;
uint32_t HAL_GetTick(void) { return tick; }
uint32_t __get_PRIMASK(void) { return 0; }
void __disable_irq(void) {}
void __enable_irq(void) {}
const AppHardwareConfig_t *AppParam_GetHardwareConfig(void) { return &hardware; }
void TaskOutput_DangerousOff(void) { ++stopped; }
void TaskComm485_RequestSafeStop(void) {}
static void signals(uint8_t opened, uint8_t closed)
{
  input_stable[AUTO_INPUT_EMERGENCY_STOP] = 1;
  input_stable[AUTO_INPUT_UPPER_DOOR_OPEN] = opened;
  input_stable[AUTO_INPUT_UPPER_DOOR_CLOSED] = closed;
}
int main(void)
{
  AppFlowContext ctx = {0};
  for (unsigned combo = 0; combo < 4; ++combo)
  {
    signals(combo & 1, combo >> 1);
    for (unsigned wrap = 0; wrap < 2; ++wrap)
    {
      tick = wrap ? UINT32_MAX - 10000U : 100;
      ctx.phase_tick = AutoDevice_GetProcessTick();
      stopped = 0;
      AutoDevice_SetFault(AUTO_FAULT_NONE);
      tick += 29999;
      assert(WaitUpperDoorOpen(&ctx, NULL) == (combo == 1 ? APP_FLOW_STEP_DONE : APP_FLOW_STEP_RUNNING));
      ++tick;
      assert(WaitUpperDoorOpen(&ctx, NULL) == (combo == 1 ? APP_FLOW_STEP_DONE : APP_FLOW_STEP_FAIL));
      assert(AutoDevice_GetFault() == (combo == 1 ? AUTO_FAULT_NONE : AUTO_FAULT_UPPER_DOOR_OPEN_TIMEOUT));
      assert(stopped == (combo == 1 ? 0 : 1));
    }
  }
  signals(0, 0);
  tick = 100; process_paused_total_ms = 0; paused_flag = 0;
  const AppFlowStep step[] = {AUTO_STEP(40, "WAIT_OPEN", 0, 0, WaitUpperDoorOpen, "")};
  AppFlow_Init(&ctx, "DOOR", step, 1); AppFlow_Start(&ctx);
  AppFlow_Process(&ctx);
  tick += 10000; AppFlow_Process(&ctx);
  AutoDevice_SetPaused(1);
  tick += 60000; AppFlow_Process(&ctx);
  assert(!AppFlow_IsFailed(&ctx));
  AutoDevice_SetPaused(0);
  tick += 19999; AppFlow_Process(&ctx);
  assert(!AppFlow_IsFailed(&ctx));
  ++tick; AppFlow_Process(&ctx);
  assert(AppFlow_IsFailed(&ctx));
  assert(AutoDevice_GetFault() == AUTO_FAULT_UPPER_DOOR_OPEN_TIMEOUT);
  puts(DOOR_TEST_EXIT ? "Exit: real open/closed signals, half-open/contradiction, 30s timeout, pause and wrap PASS" :
                       "Entry: real open/closed signals, half-open/contradiction, 30s timeout, pause and wrap PASS");
}
