#include "task_clean.h"

#include "auto_device.h"
#include "auto_process.h"
#include "clean_process.h"
#include "cmsis_os.h"
#include "task_comm485.h"

/*
 * 自清洁线程配置。
 *
 * 自清洁流程动作较多，后续接入真实动作函数后栈可能会增长，
 * 所以这里先给 768 个 word 的栈空间。
 */
#define CLEAN_TASK_STACK        768U
#define CLEAN_TASK_PRIORITY     osPriorityBelowNormal

/* 自清洁请求事件位。 */
#define CLEAN_EVENT_REQUEST     0x00000001U

/* 自清洁线程句柄，保存 osThreadNew() 返回值。 */
static osThreadId_t clean_task_handle;

/* 自清洁事件对象，用于让线程平时阻塞等待请求。 */
static osEventFlagsId_t clean_event;
static volatile uint8_t clean_running;
static volatile uint8_t clean_stop_requested;
static volatile uint8_t clean_restart_requested;
static volatile uint8_t clean_resume_requested;
static volatile uint8_t clean_failed;

/*
 * 自清洁线程属性。
 *
 * name：RTOS 线程名。
 * stack_size：线程栈大小。
 * priority：线程优先级。
 */
static const osThreadAttr_t clean_task_attributes = {
  .name = "clean",
  .stack_size = CLEAN_TASK_STACK * 4U,
  .priority = CLEAN_TASK_PRIORITY,
};

/*
 * 自清洁线程入口函数。
 *
 * 线程平时阻塞等待 CLEAN_EVENT_REQUEST，不占用 CPU。
 * 收到请求后，启动 clean_process.c 中定义的自清洁流程表。
 */
static void CleanTask(void *argument)
{
  /* 当前线程没有使用启动参数。 */
  (void)argument;

  /*
   * 初始化自清洁流程上下文。
   *
   * 这里先初始化一次，后续每次真正启动自清洁时，
   * CleanProcess_Start() 内部还会重新初始化并启动。
   */
  CleanProcess_Init();

  for (;;)
  {
    /*
     * 等待自清洁请求事件。
     *
     * osWaitForever 表示没有请求时一直阻塞。
     * 自动流程完整结束后会调用 TaskClean_Request() 设置该事件。
     */
    (void)osEventFlagsWait(clean_event,
                           CLEAN_EVENT_REQUEST,
                           osFlagsWaitAny,
                           osWaitForever);

    if (clean_stop_requested != 0U || TaskComm485_HardwareConfigBusy())
    {
      CleanProcess_Reset();
      clean_running = 0U;
      clean_failed = 0U;
      clean_stop_requested = 0U;
      continue;
    }

    /*
     * 启动一次自清洁流程。
     *
     * 自清洁步骤表定义在 clean_process.c 的 self_clean_steps[]。
     */
    CleanProcess_Start();
    clean_running = 1U;
    clean_failed = 0U;

    /*
     * 周期推进自清洁流程。
     *
     * 只要流程没有完成、没有失败，就持续调用 CleanProcess_Process()。
     */
    while ((CleanProcess_IsFinished() == 0U) &&
           (CleanProcess_IsFailed() == 0U) &&
           (clean_stop_requested == 0U))
    {
      if (clean_restart_requested != 0U)
      {
        clean_restart_requested = 0U;
        CleanProcess_Start();
      }

      if (clean_resume_requested != 0U)
      {
        clean_resume_requested = 0U;
        TaskComm485_ReleaseNonzero(COMM485_INHIBIT_PAUSE |
                                   COMM485_INHIBIT_SAFE_STOP);
        if (CleanProcess_Resume() != HAL_OK)
        {
          AutoDevice_AllDangerousOutputsOff();
          if (AutoDevice_GetFault() == AUTO_FAULT_NONE)
            AutoDevice_SetFault(AUTO_FAULT_OUTPUT_COMMAND_FAILED);
          AutoDevice_Report(AUTO_REPORT_SENSOR_FAULT);
          clean_failed = 1U;
          break;
        }
        AutoDevice_SetPaused(0U);
      }

      if ((AutoDevice_ReadInput(AUTO_INPUT_UPPER_DOOR_CLOSED) == 0U) ||
          (AutoDevice_ReadInput(AUTO_INPUT_LOWER_DOOR_CLOSED) == 0U))
      {
        AutoDevice_AllDangerousOutputsOff();
        AutoDevice_SetFault(AUTO_FAULT_SELF_CLEAN_NOT_ALLOWED);
        AutoDevice_Report(AUTO_REPORT_SENSOR_FAULT);
        clean_failed = 1U;
        break;
      }

      /* 推动自清洁流程执行一步。 */
      CleanProcess_Process();

      /* 10ms 周期执行，避免线程长期占用 CPU。 */
      osDelay(10U);
    }

    /* 自清洁成功后返回自动模式空闲层，等待新的全流程或单流程命令。 */
    if (CleanProcess_IsFinished() != 0U)
    {
      clean_failed = 0U;
      AutoProcess_RequestReset();
    }
    else if ((CleanProcess_IsFailed() != 0U) && (clean_stop_requested == 0U))
    {
      if (AutoDevice_GetFault() == AUTO_FAULT_NONE)
      {
        AutoDevice_SetFault(AUTO_FAULT_PROCESS_ABORTED);
      }
      AutoDevice_Report(AUTO_REPORT_SENSOR_FAULT);
      clean_failed = 1U;
    }
    else if (clean_stop_requested != 0U)
    {
      AutoDevice_AllDangerousOutputsOff();
      clean_failed = 0U;
    }
    CleanProcess_Reset();
    clean_running = 0U;
    clean_stop_requested = 0U;
  }
}

/*
 * 创建自清洁线程。
 */
void TaskClean_Create(void)
{
  clean_running = 0U;
  clean_stop_requested = 0U;
  clean_restart_requested = 0U;
  clean_resume_requested = 0U;
  clean_failed = 0U;
  /*
   * 创建事件标志对象。
   * 后续 TaskClean_Request() 通过这个对象唤醒自清洁线程。
   */
  clean_event = osEventFlagsNew(NULL);

  /*
   * 创建自清洁线程。
   * 线程创建后会先运行到 osEventFlagsWait() 并阻塞等待请求。
   */
  clean_task_handle = osThreadNew(CleanTask, NULL, &clean_task_attributes);

  /* 当前暂时不检查线程句柄，显式使用一下避免告警。 */
  (void)clean_task_handle;
}

/*
 * 请求执行一次自清洁。
 *
 * 自动流程完整结束后会调用这个接口。
 * 后续上位机如果需要单独触发自清洁，也可以调用这个接口。
 */
void TaskClean_Request(void)
{
  if (TaskComm485_HardwareConfigBusy()) return;
  if (AutoDevice_IsEmergencyStopped() != 0U)
  {
    return;
  }

  if (clean_event != NULL)
  {
    clean_stop_requested = 0U;
    clean_restart_requested = 0U;
    clean_resume_requested = 0U;
    /*
     * 设置自清洁请求事件位。
     * 自清洁线程正在等待该事件，收到后会开始执行自清洁流程。
     */
    (void)osEventFlagsSet(clean_event, CLEAN_EVENT_REQUEST);
  }
}

void TaskClean_Stop(void)
{
  clean_stop_requested = 1U;
  clean_restart_requested = 0U;
  clean_resume_requested = 0U;
  AutoDevice_AllDangerousOutputsOff();
  if (clean_event != NULL)
  {
    /* 同时唤醒尚未开始的自清洁请求，让线程消费停止状态并回到空闲。 */
    (void)osEventFlagsClear(clean_event, CLEAN_EVENT_REQUEST);
  }
}

void TaskClean_RequestResume(void)
{
  if ((clean_running != 0U) && (clean_failed == 0U))
  {
    clean_resume_requested = 1U;
  }
}

void TaskClean_RequestRestart(void)
{
  if ((clean_running != 0U) && (clean_failed == 0U))
  {
    clean_restart_requested = 1U;
  }
}

void TaskClean_ClearFault(void)
{
  clean_failed = 0U;
}

uint8_t TaskClean_IsRunning(void)
{
  return clean_running;
}

uint8_t TaskClean_IsFailed(void)
{
  return clean_failed;
}

uint16_t TaskClean_GetCurrentStepId(void)
{
  return CleanProcess_GetCurrentStepId();
}
