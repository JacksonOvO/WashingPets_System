#include "task_auto.h"

#include "app_param.h"
#include "auto_device.h"
#include "auto_process.h"
#include "cmsis_os.h"
#include "task_clean.h"
#include "task_output.h"
#include "task_watchdog.h"

/*
 * 自动流程线程配置。
 *
 * stack_size 使用 CMSIS-RTOS 的字节单位，所以创建线程时乘以 4U。
 * 当前自动流程主要跑状态机和表驱动逻辑，后续如果动作函数里局部变量较多，
 * 可以适当加大 AUTO_TASK_STACK。
 */
#define AUTO_TASK_STACK     768U
#define AUTO_TASK_PRIORITY  osPriorityBelowNormal

/* 自动流程线程句柄，保存 osThreadNew() 返回值。 */
static osThreadId_t auto_task_handle;
static uint8_t emergency_stop_latched;

/*
 * 自动流程线程属性。
 *
 * name：RTOS 线程名，方便调试查看。
 * stack_size：线程栈大小。
 * priority：线程优先级。
 */
static const osThreadAttr_t auto_task_attributes = {
  .name = "auto",
  .stack_size = AUTO_TASK_STACK * 4U,
  .priority = AUTO_TASK_PRIORITY,
};

static void AutoTask_ProcessEmergencyStop(void)
{
  if (AutoDevice_IsEmergencyStopped() == 0U)
  {
    emergency_stop_latched = 0U;
    return;
  }

  AutoDevice_SetPaused(0U);
  AutoDevice_SetStopRequested(1U);

  if (emergency_stop_latched == 0U)
  {
    TaskClean_Stop();
    AutoDevice_AllDangerousOutputsOff();
    TaskOutput_AllOff();
    TaskOutput_SetAutoMode(0U);
    AutoProcess_Abort();
    emergency_stop_latched = 1U;
  }
}

static void AutoTask_ProcessDoorSafety(void)
{
  uint16_t stage;

  if (AutoProcess_IsRunning() == 0U)
  {
    return;
  }

  stage = AutoProcess_GetCurrentStage();
  if ((stage == (uint16_t)AUTO_PROCESS_STAGE_ENTRY_ENV) ||
      (stage == (uint16_t)AUTO_PROCESS_STAGE_ENTRY) ||
      (stage == (uint16_t)AUTO_PROCESS_STAGE_EXIT))
  {
    return;
  }

  if ((AutoDevice_ReadInput(AUTO_INPUT_UPPER_DOOR_CLOSED) == 0U) ||
      (AutoDevice_ReadInput(AUTO_INPUT_LOWER_DOOR_CLOSED) == 0U))
  {
    AutoDevice_AllDangerousOutputsOff();
    AutoDevice_SetFault(AUTO_FAULT_OUTPUT_COMMAND_FAILED);
    AutoDevice_Report(AUTO_REPORT_SENSOR_FAULT);
    AutoProcess_Abort();
  }
}

/*
 * 自动流程线程入口函数。
 *
 * 本线程不直接写 GPIO，不直接操作 USART1。
 * - 普通输出统一走 task_output.c。
 * - 485 设备统一走 task_comm485.c。
 * - 自动流程顺序统一由 auto_process.c 的阶段表控制。
 */
static void AutoTask(void *argument)
{
  /* 当前线程没有使用启动参数，显式丢弃避免编译器告警。 */
  (void)argument;

  /*
   * 初始化自动流程控制器。
   *
   * 注意：这里只初始化，不自动启动完整流程。
   * 后续由 AutoProcess_RequestFull() 启动完整流程，
   * 或由 AutoProcess_RequestStageTest() 单独测试某一个小流程。
   */
  AutoProcess_Init();
  emergency_stop_latched = 0U;

  for (;;)
  {
    TaskWatchdog_Heartbeat(TASK_WATCHDOG_AUTO);
    /*
     * 处理参数延时保存。
     *
     * app_param.c 中修改参数后不会立即写 Flash，而是设置保存请求。
     * 这里周期调用 AppParam_Task()，到达延时后统一写入 Flash，
     * 避免连续调参时频繁擦写 Flash。
     */
    AutoTask_ProcessEmergencyStop();
    AutoTask_ProcessDoorSafety();
    AppParam_Task();

    /*
     * 推动自动流程状态机运行。
     *
     * 如果当前没有流程运行，它会检查是否有完整流程请求或单段测试请求。
     * 如果当前正在运行，它会推进当前小流程一步。
     */
    AutoProcess_Process();

    /*
     * 线程周期延时。
     *
     * 10ms 对工艺状态机足够细，也不会让线程一直占 CPU。
     */
    osDelay(10U);
  }
}

/*
 * 创建自动流程线程。
 *
 * app_tasks.c 会在 osKernelStart() 之前调用本函数。
 */
void TaskAuto_Create(void)
{
  /*
   * 创建自动流程线程。
   * AutoTask 是线程入口，auto_task_attributes 是线程配置。
   */
  auto_task_handle = osThreadNew(AutoTask, NULL, &auto_task_attributes);

  /* 当前暂时不检查线程句柄，显式使用一下避免告警。 */
  (void)auto_task_handle;
}
