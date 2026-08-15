#include "auto_stages.h"

#include "app_param.h"
#include "auto_device.h"

static void WashReadyInit(void);
static void WashReadyReset(void);

static AppFlowStepResult RequestRecipeSelection(AppFlowContext *, const AppFlowStep *);
static AppFlowStepResult WaitRecipeSelected(AppFlowContext *, const AppFlowStep *);
static AppFlowStepResult WaitTankHighLevel(AppFlowContext *, const AppFlowStep *);
static AppFlowStepResult WaitTankTempReady(AppFlowContext *, const AppFlowStep *);
static AppFlowStepResult StopReadyDevices(AppFlowContext *, const AppFlowStep *);
static AppFlowStepResult ReportReady(AppFlowContext *, const AppFlowStep *);

static const AppFlowStep steps[] = {
  AUTO_STEP(10, "REQUEST_RECIPE_SELECTION", 0U, 0U, RequestRecipeSelection, "请求上位机选择本次运行配方和沐浴液"),
  AUTO_STEP(20, "WAIT_RECIPE_SELECTED", 0U, 0U, WaitRecipeSelected, "等待上位机下发配方和沐浴液选择信号"),
  AUTO_STEP(30, "WAIT_TANK_HIGH_LEVEL", 0U, 500U, WaitTankHighLevel, "等待水箱后台温控补水到高液位"),
  AUTO_STEP(40, "WAIT_TANK_TEMP_READY", 0U, 500U, WaitTankTempReady, "等待水箱后台温控按当前配方调温达标"),
  AUTO_STEP(170, "STOP_WASH_READY_DEVICES", 0U, 0U, StopReadyDevices, "关闭洗前准备流程自身使用的输出"),
  AUTO_STEP(180, "REPORT_WASH_READY_DONE", 0U, 0U, ReportReady, "上报洗前准备完成"),
};

static const AutoStageDefinition stage = {
  "WashReady",
  steps,
  (uint16_t)(sizeof(steps) / sizeof(steps[0])),
  WashReadyInit,
  WashReadyReset
};

static void WashReadyReset(void)
{
}

static void WashReadyInit(void)
{
  WashReadyReset();
}

static void stop_wash_ready_devices(void)
{
  AutoDevice_SetAllShowerValves(0U);
  (void)AutoDevice_SetBoosterPumpRunning(0U);
}

static AppFlowStepResult jump(AppFlowContext *ctx, uint16_t id)
{
  return (AppFlow_JumpToStep(ctx, id) != 0U) ? APP_FLOW_STEP_JUMP : APP_FLOW_STEP_FAIL;
}

static AppFlowStepResult RequestRecipeSelection(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  AutoDevice_RequestHostInteraction(6U);
  return APP_FLOW_STEP_DONE;
}

static AppFlowStepResult WaitRecipeSelected(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)step;

  if (AutoDevice_ConsumeRecipeSelectedFlag() == 0U)
  {
    return APP_FLOW_STEP_RUNNING;
  }

  return jump(ctx, 30U);
}

static AppFlowStepResult WaitTankHighLevel(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;

  if (AutoDevice_ReadInput(AUTO_INPUT_TANK_HIGH) == 0U)
  {
    return APP_FLOW_STEP_DONE;
  }

  return APP_FLOW_STEP_RUNNING;
}

static AppFlowStepResult WaitTankTempReady(AppFlowContext *ctx, const AppFlowStep *step)
{
  AutoSensorData data;
  const AppRecipe_t *recipe = AppParam_GetActiveRecipe();
  int16_t low_limit;
  int16_t high_limit;

  (void)ctx;
  (void)step;

  (void)AutoDevice_GetSensorData(&data);
  if (data.tank_temperature_valid == 0U)
  {
    AutoDevice_SetFault(AUTO_FAULT_TANK_TEMP_SENSOR_INVALID);
    return APP_FLOW_STEP_FAIL;
  }

  low_limit = recipe->tank_target_temp_x10 -
              (int16_t)recipe->tank_temp_tolerance_x10;
  high_limit = recipe->tank_target_temp_x10 +
               (int16_t)recipe->tank_temp_tolerance_x10;

  if ((data.tank_temperature_x10 >= low_limit) &&
      (data.tank_temperature_x10 <= high_limit))
  {
    return APP_FLOW_STEP_DONE;
  }

  return APP_FLOW_STEP_RUNNING;
}

static AppFlowStepResult StopReadyDevices(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  stop_wash_ready_devices();
  return APP_FLOW_STEP_DONE;
}

static AppFlowStepResult ReportReady(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  AutoDevice_Report(AUTO_REPORT_WASH_READY_DONE);
  return APP_FLOW_STEP_DONE;
}

const AutoStageDefinition *AutoStage_WashReady_Get(void)
{
  return &stage;
}

