#include "auto_stages.h"

#include "auto_device.h"

static uint8_t upper_locked;
static uint8_t lower_locked;

static void EntryReset(void)
{
  upper_locked = 0U;
  lower_locked = 0U;
}

static void EntryInit(void)
{
  EntryReset();
}

static AppFlowStepResult RequestOpenUpperDoor(AppFlowContext *, const AppFlowStep *);
static AppFlowStepResult WaitOpenUpperDoor(AppFlowContext *, const AppFlowStep *);
static AppFlowStepResult UnlockUpperDoor(AppFlowContext *, const AppFlowStep *);
static AppFlowStepResult WaitUpperDoorOpen(AppFlowContext *, const AppFlowStep *);
static AppFlowStepResult ReportUpperDoorOpen(AppFlowContext *, const AppFlowStep *);
static AppFlowStepResult RequestOpenLowerDoor(AppFlowContext *, const AppFlowStep *);
static AppFlowStepResult WaitOpenLowerDoor(AppFlowContext *, const AppFlowStep *);
static AppFlowStepResult UnlockLowerDoor(AppFlowContext *, const AppFlowStep *);
static AppFlowStepResult WaitLowerDoorOpen(AppFlowContext *, const AppFlowStep *);
static AppFlowStepResult ReportLowerDoorOpen(AppFlowContext *, const AppFlowStep *);
static AppFlowStepResult RequestCloseLowerDoor(AppFlowContext *, const AppFlowStep *);
static AppFlowStepResult WaitCloseLowerDoor(AppFlowContext *, const AppFlowStep *);
static AppFlowStepResult WaitLowerDoorClosed(AppFlowContext *, const AppFlowStep *);
static AppFlowStepResult LockLowerDoor(AppFlowContext *, const AppFlowStep *);
static AppFlowStepResult CheckLowerDoorLocked(AppFlowContext *, const AppFlowStep *);
static AppFlowStepResult RequestCloseUpperDoor(AppFlowContext *, const AppFlowStep *);
static AppFlowStepResult WaitCloseUpperDoor(AppFlowContext *, const AppFlowStep *);
static AppFlowStepResult WaitUpperDoorClosed(AppFlowContext *, const AppFlowStep *);
static AppFlowStepResult LockUpperDoor(AppFlowContext *, const AppFlowStep *);
static AppFlowStepResult CheckUpperDoorLocked(AppFlowContext *, const AppFlowStep *);
static AppFlowStepResult ReportEntryDone(AppFlowContext *, const AppFlowStep *);

static const AppFlowStep steps[] = {
  AUTO_STEP(10, "REQUEST_OPEN_UPPER_DOOR", 0U, 0U, RequestOpenUpperDoor, "请求上位机打开上舱门"),
  AUTO_STEP(20, "WAIT_OPEN_UPPER_DOOR_CMD", 0U, 0U, WaitOpenUpperDoor, "等待上位机发送开上舱门命令"),
  AUTO_STEP(30, "UNLOCK_UPPER_DOOR", 0U, 0U, UnlockUpperDoor, "释放上舱门电磁锁"),
  AUTO_STEP(40, "WAIT_UPPER_DOOR_OPEN", 0U, 0U, WaitUpperDoorOpen, "等待上舱门到位输入解除"),
  AUTO_STEP(50, "REPORT_UPPER_DOOR_OPEN", 0U, 0U, ReportUpperDoorOpen, "上舱门已打开"),
  AUTO_STEP(60, "REQUEST_OPEN_LOWER_DOOR", 0U, 0U, RequestOpenLowerDoor, "请求上位机打开下舱门"),
  AUTO_STEP(70, "WAIT_OPEN_LOWER_DOOR_CMD", 0U, 0U, WaitOpenLowerDoor, "等待上位机发送开下舱门命令"),
  AUTO_STEP(80, "UNLOCK_LOWER_DOOR", 0U, 0U, UnlockLowerDoor, "释放下舱门电磁锁"),
  AUTO_STEP(90, "WAIT_LOWER_DOOR_OPEN", 0U, 0U, WaitLowerDoorOpen, "等待下舱门到位输入解除"),
  AUTO_STEP(100, "REPORT_LOWER_DOOR_OPEN", 0U, 0U, ReportLowerDoorOpen, "下舱门已打开"),
  AUTO_STEP(110, "REQUEST_CLOSE_LOWER_DOOR", 0U, 0U, RequestCloseLowerDoor, "请求上位机关闭下舱门"),
  AUTO_STEP(120, "WAIT_CLOSE_LOWER_DOOR_CMD", 0U, 0U, WaitCloseLowerDoor, "等待上位机发送关下舱门命令"),
  AUTO_STEP(130, "WAIT_LOWER_DOOR_CLOSED", 0U, 0U, WaitLowerDoorClosed, "等待下舱门到位输入"),
  AUTO_STEP(140, "LOCK_LOWER_DOOR", 0U, 0U, LockLowerDoor, "触发下舱门电磁锁"),
  AUTO_STEP(150, "CHECK_LOWER_DOOR_LOCKED", 0U, 0U, CheckLowerDoorLocked, "检测下舱门到位输入"),
  AUTO_STEP(160, "REPORT_LOWER_DOOR_CLOSED", 0U, 0U, WaitLowerDoorClosed, "下舱门已关闭"),
  AUTO_STEP(170, "REQUEST_CLOSE_UPPER_DOOR", 0U, 0U, RequestCloseUpperDoor, "请求上位机关闭上舱门"),
  AUTO_STEP(180, "WAIT_CLOSE_UPPER_DOOR_CMD", 0U, 0U, WaitCloseUpperDoor, "等待上位机发送关上舱门命令"),
  AUTO_STEP(190, "WAIT_UPPER_DOOR_CLOSED", 0U, 0U, WaitUpperDoorClosed, "等待上舱门到位输入"),
  AUTO_STEP(200, "LOCK_UPPER_DOOR", 0U, 0U, LockUpperDoor, "触发上舱门电磁锁"),
  AUTO_STEP(210, "CHECK_UPPER_DOOR_LOCKED", 0U, 0U, CheckUpperDoorLocked, "检测上舱门到位输入"),
  AUTO_STEP(220, "REPORT_ENTRY_DONE", 0U, 0U, ReportEntryDone, "上舱门已关闭并上报入舱流程完成"),
};

static const AutoStageDefinition stage = {
  "Entering", steps, (uint16_t)(sizeof(steps) / sizeof(steps[0])),
  EntryInit, EntryReset
};

static uint8_t consume_command(AutoCommand command)
{
  return (AutoDevice_ConsumeCommand(command) != 0U) ? 1U : 0U;
}

static uint8_t upper_door_closed(void)
{
  return (AutoDevice_ReadInput(AUTO_INPUT_UPPER_DOOR_CLOSED) != 0U) ? 1U : 0U;
}

static uint8_t lower_door_closed(void)
{
  return (AutoDevice_ReadInput(AUTO_INPUT_LOWER_DOOR_CLOSED) != 0U) ? 1U : 0U;
}

static AppFlowStepResult RequestOpenUpperDoor(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  AutoDevice_RequestHostInteraction(1U);
  return APP_FLOW_STEP_DONE;
}

static AppFlowStepResult WaitOpenUpperDoor(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  return (consume_command(AUTO_COMMAND_OPEN_UPPER_DOOR) != 0U) ?
         APP_FLOW_STEP_DONE : APP_FLOW_STEP_RUNNING;
}

static AppFlowStepResult UnlockUpperDoor(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  upper_locked = 0U;
  return (AutoDevice_SetOutput(AUTO_OUTPUT_UPPER_LOCK, 0U) == HAL_OK) ?
         APP_FLOW_STEP_DONE :
         (AutoDevice_SetFault(AUTO_FAULT_UPPER_LOCK_OUTPUT_FAILED),
          APP_FLOW_STEP_FAIL);
}

static AppFlowStepResult WaitUpperDoorOpen(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  return (upper_door_closed() == 0U) ?
         APP_FLOW_STEP_DONE : APP_FLOW_STEP_RUNNING;
}

static AppFlowStepResult ReportUpperDoorOpen(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  AutoDevice_Report(AUTO_REPORT_UPPER_DOOR_OPEN);
  return APP_FLOW_STEP_DONE;
}

static AppFlowStepResult RequestOpenLowerDoor(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  AutoDevice_RequestHostInteraction(2U);
  return APP_FLOW_STEP_DONE;
}

static AppFlowStepResult WaitOpenLowerDoor(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  return (consume_command(AUTO_COMMAND_OPEN_LOWER_DOOR) != 0U) ?
         APP_FLOW_STEP_DONE : APP_FLOW_STEP_RUNNING;
}

static AppFlowStepResult UnlockLowerDoor(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  lower_locked = 0U;
  return (AutoDevice_SetOutput(AUTO_OUTPUT_LOWER_LOCK, 0U) == HAL_OK) ?
         APP_FLOW_STEP_DONE :
         (AutoDevice_SetFault(AUTO_FAULT_LOWER_LOCK_OUTPUT_FAILED),
          APP_FLOW_STEP_FAIL);
}

static AppFlowStepResult WaitLowerDoorOpen(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  return (lower_door_closed() == 0U) ?
         APP_FLOW_STEP_DONE : APP_FLOW_STEP_RUNNING;
}

static AppFlowStepResult ReportLowerDoorOpen(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  AutoDevice_Report(AUTO_REPORT_LOWER_DOOR_OPEN);
  return APP_FLOW_STEP_DONE;
}

static AppFlowStepResult RequestCloseLowerDoor(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  AutoDevice_RequestHostInteraction(3U);
  return APP_FLOW_STEP_DONE;
}

static AppFlowStepResult WaitCloseLowerDoor(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  return (consume_command(AUTO_COMMAND_CLOSE_LOWER_DOOR) != 0U) ?
         APP_FLOW_STEP_DONE : APP_FLOW_STEP_RUNNING;
}

static AppFlowStepResult WaitLowerDoorClosed(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  return (lower_door_closed() != 0U) ?
         APP_FLOW_STEP_DONE : APP_FLOW_STEP_RUNNING;
}

static AppFlowStepResult LockLowerDoor(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  if (AutoDevice_SetOutput(AUTO_OUTPUT_LOWER_LOCK, 1U) != HAL_OK)
  {
    AutoDevice_SetFault(AUTO_FAULT_LOWER_LOCK_OUTPUT_FAILED);
    return APP_FLOW_STEP_FAIL;
  }
  lower_locked = 1U;
  AutoDevice_Report(AUTO_REPORT_LOWER_DOOR_LOCKED);
  return APP_FLOW_STEP_DONE;
}

static AppFlowStepResult CheckLowerDoorLocked(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)step;
  if ((lower_locked != 0U) && (lower_door_closed() != 0U))
  {
    return APP_FLOW_STEP_DONE;
  }
  lower_locked = 0U;
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_LOWER_LOCK, 0U);
  return (AppFlow_JumpToStep(ctx, 130U) != 0U) ?
         APP_FLOW_STEP_JUMP :
         (AutoDevice_SetFault(AUTO_FAULT_FLOW_JUMP_FAILED),
          APP_FLOW_STEP_FAIL);
}

static AppFlowStepResult RequestCloseUpperDoor(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  AutoDevice_RequestHostInteraction(4U);
  return APP_FLOW_STEP_DONE;
}

static AppFlowStepResult WaitCloseUpperDoor(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  return (consume_command(AUTO_COMMAND_CLOSE_UPPER_DOOR) != 0U) ?
         APP_FLOW_STEP_DONE : APP_FLOW_STEP_RUNNING;
}

static AppFlowStepResult WaitUpperDoorClosed(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  return (upper_door_closed() != 0U) ?
         APP_FLOW_STEP_DONE : APP_FLOW_STEP_RUNNING;
}

static AppFlowStepResult LockUpperDoor(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  if (AutoDevice_SetOutput(AUTO_OUTPUT_UPPER_LOCK, 1U) != HAL_OK)
  {
    AutoDevice_SetFault(AUTO_FAULT_UPPER_LOCK_OUTPUT_FAILED);
    return APP_FLOW_STEP_FAIL;
  }
  upper_locked = 1U;
  AutoDevice_Report(AUTO_REPORT_UPPER_DOOR_LOCKED);
  return APP_FLOW_STEP_DONE;
}

static AppFlowStepResult CheckUpperDoorLocked(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)step;
  if ((upper_locked != 0U) && (upper_door_closed() != 0U))
  {
    return APP_FLOW_STEP_DONE;
  }
  upper_locked = 0U;
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_UPPER_LOCK, 0U);
  return (AppFlow_JumpToStep(ctx, 190U) != 0U) ?
         APP_FLOW_STEP_JUMP :
         (AutoDevice_SetFault(AUTO_FAULT_FLOW_JUMP_FAILED),
          APP_FLOW_STEP_FAIL);
}

static AppFlowStepResult ReportEntryDone(AppFlowContext *ctx, const AppFlowStep *step)
{
  (void)ctx;
  (void)step;
  AutoDevice_Report(AUTO_REPORT_ENTRY_DONE);
  return APP_FLOW_STEP_DONE;
}

const AutoStageDefinition *AutoStage_Entry_Get(void)
{
  return &stage;
}
