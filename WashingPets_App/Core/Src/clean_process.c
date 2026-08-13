#include "clean_process.h"

#include "app_param.h"
#include "auto_device.h"
#include "auto_stages.h"
#include "rs485_config.h"
#include "task_comm485.h"

#include <string.h>

#define SELF_CLEAN_FLOW_CHECK_ENABLE       0U
#define SELF_CLEAN_VENT_LOW_PERCENT       20U
#define SELF_CLEAN_DRY_TARGET_PERCENT    100U
#define SELF_CLEAN_DRY_HEATER_PERCENT    100U
#define SELF_CLEAN_FAN_RAMP_STEP_PERCENT   5U
#define SELF_CLEAN_FAN_RAMP_INTERVAL_MS  200U
#define SELF_CLEAN_UV_TIME_MS          3000U
#define SELF_CLEAN_COOL_AIR_TIME_MS    10000U
#define SELF_CLEAN_TANK_HIGH_DIAG_MS  300000U
#define SELF_CLEAN_BOOSTER_PRESSURE_X10  70U

static AppFlowContext clean_flow;
static uint32_t phase_tick;
static uint8_t dry_fan_percent;
static uint8_t tank_high_wait_reported;

static AppFlowStepResult CheckTankHigh(AppFlowContext *, const AppFlowStep *);
static AppFlowStepResult CheckAllow(AppFlowContext *, const AppFlowStep *);
static AppFlowStepResult DisinfectantOn(AppFlowContext *, const AppFlowStep *);
static AppFlowStepResult PumpsOn(AppFlowContext *, const AppFlowStep *);
static AppFlowStepResult CheckFlow(AppFlowContext *, const AppFlowStep *);
static AppFlowStepResult TopValveOn(AppFlowContext *, const AppFlowStep *);
static AppFlowStepResult WaitZone(AppFlowContext *, const AppFlowStep *);
static AppFlowStepResult SwitchLeftUpper(AppFlowContext *, const AppFlowStep *);
static AppFlowStepResult SwitchLeftMiddle(AppFlowContext *, const AppFlowStep *);
static AppFlowStepResult SwitchRear(AppFlowContext *, const AppFlowStep *);
static AppFlowStepResult PumpsOff(AppFlowContext *, const AppFlowStep *);
static AppFlowStepResult WaterPathOff(AppFlowContext *, const AppFlowStep *);
static AppFlowStepResult CheckEmptyAgain(AppFlowContext *, const AppFlowStep *);
static AppFlowStepResult UvOn(AppFlowContext *, const AppFlowStep *);
static AppFlowStepResult WaitUv(AppFlowContext *, const AppFlowStep *);
static AppFlowStepResult VentLow(AppFlowContext *, const AppFlowStep *);
static AppFlowStepResult DryHeaterOn(AppFlowContext *, const AppFlowStep *);
static AppFlowStepResult RampDryFan(AppFlowContext *, const AppFlowStep *);
static AppFlowStepResult WaitDry(AppFlowContext *, const AppFlowStep *);
static AppFlowStepResult DryHeaterOff(AppFlowContext *, const AppFlowStep *);
static AppFlowStepResult KeepCooling(AppFlowContext *, const AppFlowStep *);
static AppFlowStepResult WaitCooling(AppFlowContext *, const AppFlowStep *);
static AppFlowStepResult StopAir(AppFlowContext *, const AppFlowStep *);
static AppFlowStepResult ReportDone(AppFlowContext *, const AppFlowStep *);
static AppFlowStepResult FailForbidden(AppFlowContext *, const AppFlowStep *);
static AppFlowStepResult FailFlow(AppFlowContext *, const AppFlowStep *);
static AppFlowStepResult FailStopped(AppFlowContext *, const AppFlowStep *);
static AppFlowStepResult FailDisinfection(AppFlowContext *, const AppFlowStep *);
static AppFlowStepResult Finish(AppFlowContext *, const AppFlowStep *);

static const AppFlowStep self_clean_steps[] = {
  AUTO_STEP(5,   "SELF_CLEAN_CHECK_TANK_HIGH",   0U, 0U, CheckTankHigh,     "确认水箱高液位"),
  AUTO_STEP(10,  "SELF_CLEAN_CHECK_ALLOW",       0U, 0U, CheckAllow,        "确认舱门关闭且舱内无目标"),
  AUTO_STEP(20,  "SELF_CLEAN_DISINFECTANT_ON",   0U, 0U, DisinfectantOn,    "打开消毒液阀"),
  AUTO_STEP(30,  "SELF_CLEAN_PUMPS_ON",          0U, 0U, PumpsOn,           "启动增压泵"),
  AUTO_STEP(40,  "SELF_CLEAN_CHECK_FLOW",        0U, 0U, CheckFlow,         "确认自清洁水路流量"),
  AUTO_STEP(50,  "SELF_CLEAN_TOP_ON",            0U, 0U, TopValveOn,        "打开顶部清洁阀"),
  AUTO_STEP(60,  "SELF_CLEAN_WAIT_TOP",          0U, 0U, WaitZone,          "等待顶部清洁时间"),
  AUTO_STEP(70,  "SELF_CLEAN_LEFT_UPPER_ON",     0U, 0U, SwitchLeftUpper,   "切换到左侧上层清洁"),
  AUTO_STEP(80,  "SELF_CLEAN_WAIT_LEFT_UPPER",   0U, 0U, WaitZone,          "等待左侧上层清洁时间"),
  AUTO_STEP(90,  "SELF_CLEAN_LEFT_MIDDLE_ON",    0U, 0U, SwitchLeftMiddle,  "切换到左侧夹层清洁"),
  AUTO_STEP(100, "SELF_CLEAN_WAIT_LEFT_MIDDLE",  0U, 0U, WaitZone,          "等待左侧夹层清洁时间"),
  AUTO_STEP(110, "SELF_CLEAN_REAR_ON",           0U, 0U, SwitchRear,        "切换到后端清洁"),
  AUTO_STEP(120, "SELF_CLEAN_WAIT_REAR",         0U, 0U, WaitZone,          "等待后端清洁时间"),
  AUTO_STEP(130, "SELF_CLEAN_PUMPS_OFF",         0U, 0U, PumpsOff,          "关闭增压泵和蠕动泵"),
  AUTO_STEP(140, "SELF_CLEAN_WATER_PATH_OFF",    0U, 0U, WaterPathOff,      "关闭后端清洁阀和消毒液阀"),
  AUTO_STEP(150, "SELF_CLEAN_CHECK_EMPTY_AGAIN", 0U, 0U, CheckEmptyAgain,   "消杀前再次确认舱门和舱内状态"),
  AUTO_STEP(160, "SELF_CLEAN_UV_ON",             0U, 0U, UvOn,              "打开紫外线"),
  AUTO_STEP(170, "SELF_CLEAN_WAIT_UV",           0U, 0U, WaitUv,            "等待紫外线消杀时间"),
  AUTO_STEP(190, "SELF_CLEAN_VENT_LOW",          0U, 0U, VentLow,           "关闭紫外线并以20%风量开始通风"),
  AUTO_STEP(240, "SELF_CLEAN_DRY_HEATER_ON",     0U, 0U, DryHeaterOn,       "打开加热管"),
  AUTO_STEP(250, "SELF_CLEAN_RAMP_DRY_FAN",      0U, 0U, RampDryFan,        "线性提高风机功率"),
  AUTO_STEP(260, "SELF_CLEAN_WAIT_DRY",          0U, 0U, WaitDry,           "等待烘干时间到达"),
  AUTO_STEP(270, "SELF_CLEAN_DRY_HEATER_OFF",    0U, 0U, DryHeaterOff,      "关闭加热管"),
  AUTO_STEP(280, "SELF_CLEAN_KEEP_COOLING",      0U, 0U, KeepCooling,       "保持风机冷却"),
  AUTO_STEP(290, "SELF_CLEAN_WAIT_COOLING",      0U, 0U, WaitCooling,       "等待冷风延时"),
  AUTO_STEP(300, "SELF_CLEAN_STOP_AIR",          0U, 0U, StopAir,           "关闭风机和加热"),
  AUTO_STEP(310, "REPORT_SELF_CLEAN_DONE",       0U, 0U, ReportDone,        "上报自清洁完成"),
  AUTO_STEP(320, "SELF_CLEAN_FORBIDDEN",         0U, 0U, FailForbidden,     "自清洁条件不允许"),
  AUTO_STEP(330, "SELF_CLEAN_FLOW_FAULT",        0U, 0U, FailFlow,          "自清洁流量异常"),
  AUTO_STEP(340, "SELF_CLEAN_STOPPED",           0U, 0U, FailStopped,       "停止或急停处理"),
  AUTO_STEP(350, "SELF_CLEAN_DISINFECT_FORBID",  0U, 0U, FailDisinfection,  "臭氧紫外线启动条件不允许"),
  AUTO_STEP(370, "SELF_CLEAN_END",               0U, 0U, Finish,            "自清洁流程结束"),
};

static AppFlowStepResult jump(AppFlowContext *ctx, uint16_t id)
{
  return (AppFlow_JumpToStep(ctx, id) != 0U) ?
         APP_FLOW_STEP_JUMP : APP_FLOW_STEP_FAIL;
}

static HAL_StatusTypeDef set_fans(uint8_t percent)
{
  if (TaskComm485_RequestSetPercent(RS485_STATION_LEFT_FAN, percent) != HAL_OK)
  {
    AutoDevice_SetFault(AUTO_FAULT_485_LEFT_FAN_COMM);
    return HAL_ERROR;
  }
  if (TaskComm485_RequestSetPercent(RS485_STATION_RIGHT_FAN, percent) != HAL_OK)
  {
    AutoDevice_SetFault(AUTO_FAULT_485_RIGHT_FAN_COMM);
    return HAL_ERROR;
  }
  if (TaskComm485_RequestSetPercent(RS485_STATION_BOTTOM_FAN, percent) != HAL_OK)
  {
    AutoDevice_SetFault(AUTO_FAULT_485_BOTTOM_FAN_COMM);
    return HAL_ERROR;
  }
  return HAL_OK;
}

static HAL_StatusTypeDef set_heaters(uint8_t percent)
{
  if (TaskComm485_RequestSetPercent(RS485_STATION_LEFT_HEATER, percent) != HAL_OK)
  {
    AutoDevice_SetFault(AUTO_FAULT_485_LEFT_HEATER_COMM);
    return HAL_ERROR;
  }
  if (TaskComm485_RequestSetPercent(RS485_STATION_RIGHT_HEATER, percent) != HAL_OK)
  {
    AutoDevice_SetFault(AUTO_FAULT_485_RIGHT_HEATER_COMM);
    return HAL_ERROR;
  }
  if (TaskComm485_RequestSetPercent(RS485_STATION_BOTTOM_HEATER, percent) != HAL_OK)
  {
    AutoDevice_SetFault(AUTO_FAULT_485_BOTTOM_HEATER_COMM);
    return HAL_ERROR;
  }
  return HAL_OK;
}

static void ultraviolet_set(uint8_t enabled)
{
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_UV_LAMP,
                             (enabled != 0U) ? 1U : 0U);
}

static void clean_safe_stop(void)
{
  ultraviolet_set(0U);
  (void)set_heaters(0U);
  (void)set_fans(0U);
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_EXHAUST_FAN, 0U);
  AutoDevice_AllDangerousOutputsOff();
}

static uint8_t cabin_is_empty_and_closed(void)
{
  if ((AutoDevice_ReadInput(AUTO_INPUT_UPPER_DOOR_CLOSED) == 0U) ||
      (AutoDevice_ReadInput(AUTO_INPUT_LOWER_DOOR_CLOSED) == 0U))
  {
    return 0U;
  }

  /*
   * 当前没有独立舱内红外/存在输入，暂用上下光幕作为存在检测代理。
   * 任一光幕触发均禁止自清洁和臭氧消杀。
   */
  if ((AutoDevice_ReadInput(AUTO_INPUT_UPPER_LIGHT_CURTAIN) != 0U) ||
      (AutoDevice_ReadInput(AUTO_INPUT_LOWER_LIGHT_CURTAIN) != 0U))
  {
    return 0U;
  }
  return 1U;
}

static AppFlowStepResult CheckTankHigh(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  if (AutoDevice_ReadInput(AUTO_INPUT_TANK_HIGH) == 0U)
  {
    return APP_FLOW_STEP_DONE;
  }
  if ((tank_high_wait_reported == 0U) &&
      ((AutoDevice_GetProcessTick() - ctx->phase_tick) >= SELF_CLEAN_TANK_HIGH_DIAG_MS))
  {
    AutoDevice_ReportFault(AUTO_FAULT_TANK_HIGH_WAIT_TOO_LONG,
                           AUTO_REPORT_TIMEOUT_FAULT);
    tank_high_wait_reported = 1U;
  }

  return APP_FLOW_STEP_RUNNING;
}

static uint32_t zone_clean_time_ms(uint16_t step_id)
{
  const AppRecipe_t *recipe = AppParam_GetActiveRecipe();

  switch (step_id)
  {
    case 60U:
      return recipe->self_clean_top_time_ms;
    case 80U:
      return recipe->self_clean_left_upper_time_ms;
    case 100U:
      return recipe->self_clean_left_middle_time_ms;
    case 120U:
      return recipe->self_clean_rear_time_ms;
    default:
      return 0U;
  }
}

static AppFlowStepResult CheckAllow(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)step;
  clean_safe_stop();
  /* 启动前清场完成，允许本轮自清洁后续重新下发485风机/加热命令。 */
  TaskComm485_ReleaseNonzero(COMM485_INHIBIT_SAFE_STOP);
  return (cabin_is_empty_and_closed() != 0U) ?
         APP_FLOW_STEP_DONE :
         (AutoDevice_SetFault(AUTO_FAULT_SELF_CLEAN_NOT_ALLOWED),
          jump(ctx, 320U));
}

static AppFlowStepResult DisinfectantOn(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  if (AutoDevice_ReadInput(AUTO_INPUT_DISINFECTANT_LOW) != 0U)
  {
    AutoDevice_SetFault(AUTO_FAULT_DISINFECTANT_LOW);
    return APP_FLOW_STEP_FAIL;
  }
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_DISINFECTANT_VALVE, 1U);
  return APP_FLOW_STEP_DONE;
}

static AppFlowStepResult PumpsOn(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  (void)AutoDevice_SetBoosterPumpPercent(SELF_CLEAN_BOOSTER_PRESSURE_X10);
  (void)AutoDevice_SetBoosterPumpRunning(1U);
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_SCREW_PUMP, 0U);
  return APP_FLOW_STEP_DONE;
}

static AppFlowStepResult CheckFlow(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)step;
#if (SELF_CLEAN_FLOW_CHECK_ENABLE != 0U)
  AutoDevice_SetFault(AUTO_FAULT_SELF_CLEAN_FLOW_ABNORMAL);
  return jump(ctx, 330U);
#else
  (void)ctx;
  return APP_FLOW_STEP_DONE;
#endif
}

static AppFlowStepResult TopValveOn(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_SELF_CLEAN_TOP_VALVE, 1U);
  phase_tick = AutoDevice_GetProcessTick();
  return APP_FLOW_STEP_DONE;
}

static AppFlowStepResult WaitZone(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  return ((AutoDevice_GetProcessTick() - phase_tick) >= zone_clean_time_ms(step->id)) ?
         APP_FLOW_STEP_DONE : APP_FLOW_STEP_RUNNING;
}

static AppFlowStepResult SwitchLeftUpper(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)step;
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_SELF_CLEAN_TOP_VALVE, 0U);
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_SELF_CLEAN_FRONT_UPPER_VALVE, 0U);
  return jump(ctx, 90U);
}

static AppFlowStepResult SwitchLeftMiddle(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_SELF_CLEAN_FRONT_UPPER_VALVE, 0U);
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_SELF_CLEAN_FRONT_LOWER_VALVE, 1U);
  phase_tick = AutoDevice_GetProcessTick();
  return APP_FLOW_STEP_DONE;
}

static AppFlowStepResult SwitchRear(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_SELF_CLEAN_FRONT_LOWER_VALVE, 0U);
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_SELF_CLEAN_REAR_VALVE, 1U);
  phase_tick = AutoDevice_GetProcessTick();
  return APP_FLOW_STEP_DONE;
}

static AppFlowStepResult PumpsOff(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  (void)AutoDevice_SetBoosterPumpRunning(0U);
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_SCREW_PUMP, 0U);
  return APP_FLOW_STEP_DONE;
}

static AppFlowStepResult WaterPathOff(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_SELF_CLEAN_REAR_VALVE, 0U);
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_DISINFECTANT_VALVE, 0U);
  return APP_FLOW_STEP_DONE;
}

static AppFlowStepResult CheckEmptyAgain(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)step;
  return (cabin_is_empty_and_closed() != 0U) ?
         APP_FLOW_STEP_DONE : jump(ctx, 350U);
}

static AppFlowStepResult UvOn(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_OZONE, 0U);
  ultraviolet_set(1U);
  phase_tick = AutoDevice_GetProcessTick();
  return APP_FLOW_STEP_DONE;
}

static AppFlowStepResult WaitUv(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  return ((AutoDevice_GetProcessTick() - phase_tick) >= SELF_CLEAN_UV_TIME_MS) ?
         APP_FLOW_STEP_DONE : APP_FLOW_STEP_RUNNING;
}

static AppFlowStepResult VentLow(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  ultraviolet_set(0U);
  dry_fan_percent = SELF_CLEAN_VENT_LOW_PERCENT;
  if ((set_fans(dry_fan_percent) != HAL_OK) ||
      (AutoDevice_SetOutput(AUTO_OUTPUT_EXHAUST_FAN, 1U) != HAL_OK))
  {
    if (AutoDevice_GetFault() == AUTO_FAULT_NONE)
    {
      AutoDevice_SetFault(AUTO_FAULT_FAN_OUTPUT_FAILED);
    }
    return APP_FLOW_STEP_FAIL;
  }
  phase_tick = AutoDevice_GetProcessTick();
  return APP_FLOW_STEP_DONE;
}

static AppFlowStepResult DryHeaterOn(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  if (set_heaters(SELF_CLEAN_DRY_HEATER_PERCENT) != HAL_OK)
  {
    if (AutoDevice_GetFault() == AUTO_FAULT_NONE)
    {
      AutoDevice_SetFault(AUTO_FAULT_HEATER_OUTPUT_FAILED);
    }
    return APP_FLOW_STEP_FAIL;
  }
  phase_tick = AutoDevice_GetProcessTick();
  return APP_FLOW_STEP_DONE;
}

static AppFlowStepResult RampDryFan(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  if (dry_fan_percent >= SELF_CLEAN_DRY_TARGET_PERCENT)
  {
    phase_tick = AutoDevice_GetProcessTick();
    return APP_FLOW_STEP_DONE;
  }
  if ((AutoDevice_GetProcessTick() - phase_tick) < SELF_CLEAN_FAN_RAMP_INTERVAL_MS)
  {
    return APP_FLOW_STEP_RUNNING;
  }
  dry_fan_percent = (uint8_t)(dry_fan_percent +
                              SELF_CLEAN_FAN_RAMP_STEP_PERCENT);
  if (dry_fan_percent > SELF_CLEAN_DRY_TARGET_PERCENT)
  {
    dry_fan_percent = SELF_CLEAN_DRY_TARGET_PERCENT;
  }
  if (set_fans(dry_fan_percent) != HAL_OK)
  {
    if (AutoDevice_GetFault() == AUTO_FAULT_NONE)
    {
      AutoDevice_SetFault(AUTO_FAULT_FAN_OUTPUT_FAILED);
    }
    return APP_FLOW_STEP_FAIL;
  }
  phase_tick = AutoDevice_GetProcessTick();
  return (dry_fan_percent >= SELF_CLEAN_DRY_TARGET_PERCENT) ?
         APP_FLOW_STEP_DONE : APP_FLOW_STEP_RUNNING;
}

static AppFlowStepResult WaitDry(AppFlowContext *ctx, const AppFlowStep *step)
{
  uint32_t elapsed;
  uint32_t dry_time;

  (void)ctx;
  (void)step;
  elapsed = AutoDevice_GetProcessTick() - phase_tick;
  dry_time = AppParam_GetActiveRecipe()->self_clean_dry_time_ms;
  return (elapsed >= dry_time) ? APP_FLOW_STEP_DONE : APP_FLOW_STEP_RUNNING;
}

static AppFlowStepResult DryHeaterOff(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  if (set_heaters(0U) != HAL_OK)
  {
    if (AutoDevice_GetFault() == AUTO_FAULT_NONE)
    {
      AutoDevice_SetFault(AUTO_FAULT_HEATER_OUTPUT_FAILED);
    }
    return APP_FLOW_STEP_FAIL;
  }
  phase_tick = AutoDevice_GetProcessTick();
  return APP_FLOW_STEP_DONE;
}

static AppFlowStepResult KeepCooling(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  if (set_fans(SELF_CLEAN_DRY_TARGET_PERCENT) != HAL_OK)
  {
    if (AutoDevice_GetFault() == AUTO_FAULT_NONE)
    {
      AutoDevice_SetFault(AUTO_FAULT_FAN_OUTPUT_FAILED);
    }
    return APP_FLOW_STEP_FAIL;
  }
  return APP_FLOW_STEP_DONE;
}

static AppFlowStepResult WaitCooling(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  return ((AutoDevice_GetProcessTick() - phase_tick) >= SELF_CLEAN_COOL_AIR_TIME_MS) ?
         APP_FLOW_STEP_DONE : APP_FLOW_STEP_RUNNING;
}

static AppFlowStepResult StopAir(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  if ((set_heaters(0U) != HAL_OK) ||
      (set_fans(0U) != HAL_OK) ||
      (AutoDevice_SetOutput(AUTO_OUTPUT_EXHAUST_FAN, 0U) != HAL_OK))
  {
    if (AutoDevice_GetFault() == AUTO_FAULT_NONE)
    {
      AutoDevice_SetFault(AUTO_FAULT_OUTPUT_COMMAND_FAILED);
    }
    return APP_FLOW_STEP_FAIL;
  }
  return APP_FLOW_STEP_DONE;
}

static AppFlowStepResult ReportDone(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)step;
  AutoDevice_Report(AUTO_REPORT_SELF_CLEAN_DONE);
  return jump(ctx, 370U);
}

static AppFlowStepResult fail_and_stop(AutoReport report)
{
  (void)report;
  clean_safe_stop();
  return APP_FLOW_STEP_FAIL;
}

static AppFlowStepResult FailForbidden(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  AutoDevice_SetFault(AUTO_FAULT_SELF_CLEAN_NOT_ALLOWED);
  return fail_and_stop(AUTO_REPORT_SENSOR_FAULT);
}

static AppFlowStepResult FailFlow(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  AutoDevice_SetFault(AUTO_FAULT_SELF_CLEAN_FLOW_ABNORMAL);
  return fail_and_stop(AUTO_REPORT_SENSOR_FAULT);
}

static AppFlowStepResult FailStopped(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  clean_safe_stop();
  AutoDevice_SetFault(AUTO_FAULT_PROCESS_ABORTED);
  return APP_FLOW_STEP_FAIL;
}

static AppFlowStepResult FailDisinfection(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  AutoDevice_SetFault(AUTO_FAULT_SELF_CLEAN_DISINFECTION_NOT_ALLOWED);
  return fail_and_stop(AUTO_REPORT_SENSOR_FAULT);
}

static AppFlowStepResult Finish(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  return APP_FLOW_STEP_DONE;
}

void CleanProcess_Init(void)
{
  CleanProcess_Reset();
  AppFlow_Init(&clean_flow,
               "SelfClean",
               self_clean_steps,
               (uint16_t)(sizeof(self_clean_steps) /
                          sizeof(self_clean_steps[0])));
}

void CleanProcess_Reset(void)
{
  memset(&clean_flow, 0, sizeof(clean_flow));
  phase_tick = 0U;
  dry_fan_percent = 0U;
  tank_high_wait_reported = 0U;
}

void CleanProcess_Start(void)
{
  TaskComm485_ReleaseNonzero(COMM485_INHIBIT_STARTUP |
                             COMM485_INHIBIT_SAFE_STOP |
                             COMM485_INHIBIT_MODE_CHANGE);
  CleanProcess_Init();
  AppFlow_Start(&clean_flow);
}

HAL_StatusTypeDef CleanProcess_Resume(void)
{
  uint16_t step_id = AppFlow_GetCurrentStepId(&clean_flow);

  if ((step_id >= 20U) && (step_id < 140U))
  {
    (void)AutoDevice_SetOutput(AUTO_OUTPUT_DISINFECTANT_VALVE, 1U);
    if ((step_id >= 30U) &&
        ((AutoDevice_SetBoosterPumpPercent(SELF_CLEAN_BOOSTER_PRESSURE_X10) != HAL_OK) ||
         (AutoDevice_SetBoosterPumpRunning(1U) != HAL_OK)))
    {
      return HAL_ERROR;
    }
    if ((step_id >= 50U) && (step_id < 70U))
    {
      (void)AutoDevice_SetOutput(AUTO_OUTPUT_SELF_CLEAN_TOP_VALVE, 1U);
    }
    else if ((step_id >= 70U) && (step_id < 90U))
    {
      (void)AutoDevice_SetOutput(AUTO_OUTPUT_SELF_CLEAN_FRONT_UPPER_VALVE, 1U);
    }
    else if ((step_id >= 90U) && (step_id < 110U))
    {
      (void)AutoDevice_SetOutput(AUTO_OUTPUT_SELF_CLEAN_FRONT_LOWER_VALVE, 1U);
    }
    else if (step_id >= 110U)
    {
      (void)AutoDevice_SetOutput(AUTO_OUTPUT_SELF_CLEAN_REAR_VALVE, 1U);
    }
  }
  else if ((step_id >= 160U) && (step_id < 190U))
  {
    ultraviolet_set(1U);
  }
  else if ((step_id >= 190U) && (step_id < 300U))
  {
    if ((set_fans(dry_fan_percent) != HAL_OK) ||
        (AutoDevice_SetOutput(AUTO_OUTPUT_EXHAUST_FAN, 1U) != HAL_OK))
    {
      return HAL_ERROR;
    }
    if ((step_id >= 240U) && (step_id < 270U) &&
        (set_heaters(SELF_CLEAN_DRY_HEATER_PERCENT) != HAL_OK))
    {
      return HAL_ERROR;
    }
  }
  return HAL_OK;
}

void CleanProcess_Process(void)
{
  AppFlow_Process(&clean_flow);
}

uint8_t CleanProcess_IsFinished(void)
{
  return AppFlow_IsFinished(&clean_flow);
}

uint8_t CleanProcess_IsFailed(void)
{
  return AppFlow_IsFailed(&clean_flow);
}

uint16_t CleanProcess_GetCurrentStepId(void)
{
  return AppFlow_GetCurrentStepId(&clean_flow);
}
