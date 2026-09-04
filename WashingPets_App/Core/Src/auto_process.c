#include "auto_process.h"

#include "app_flow.h"
#include "app_param.h"
#include "auto_device.h"
#include "auto_stages.h"
#include "task_clean.h"
#include "task_comm485.h"
#include "task_output.h"
#include "task_water_tank.h"

#include <string.h>

/* 当前正在运行的小流程上下文，交给 app_flow.c 执行步骤表。 */
static AppFlowContext auto_flow;
static const AutoStageDefinition *current_stage_definition;
static uint8_t stage_active;

/* 当前正在执行 auto_stage_getters[] 中的哪个小流程。 */
static volatile uint16_t current_stage;

/* 当前小流程的启动 tick，用于计算本阶段已运行时间。 */
static volatile uint32_t stage_start_tick;

/* 自动流程运行标志，1 表示完整流程或单段测试正在运行。 */
static volatile uint8_t auto_running;

/* 自动流程完成标志，1 表示当前完整流程或单段测试已完成。 */
static volatile uint8_t auto_finished;

/* 自动流程失败标志，1 表示某个步骤执行失败。 */
static volatile uint8_t auto_failed;

/* 自清洁触发标志，防止完整自动流程结束后重复触发自清洁。 */
static volatile uint8_t clean_requested;

/* 完整自动流程启动请求标志，由上位机或手动入口置位。 */
static volatile uint8_t full_request;

/* 单段测试模式标志，1 表示当前只跑某一个小流程，跑完不继续后续阶段。 */
static volatile uint8_t stage_test_mode;
static volatile uint8_t restart_current_stage_request;
static volatile uint8_t reset_request;
static volatile uint8_t abort_request;
static volatile uint8_t terminate_request;
static volatile uint8_t resume_request;

/* 每个小流程的单独测试请求标志，手动调试时置位对应元素。 */
static volatile uint8_t stage_test_request[AUTO_PROCESS_STAGE_COUNT];

typedef const AutoStageDefinition *(*AutoStageGetter)(void);

/* 顺序必须与 AutoProcessStage 枚举完全一致。 */
static const AutoStageGetter auto_stage_getters[AUTO_PROCESS_STAGE_COUNT] = {
  AutoStage_EntryEnv_Get,
  AutoStage_Entry_Get,
  AutoStage_WashReady_Get,
  AutoStage_Spray_Get,
  AutoStage_Bath_Get,
  AutoStage_Clean_Get,
  AutoStage_Dry_Get,
  AutoStage_ExitPrepare_Get,
  AutoStage_Exit_Get,
};

static void clear_stage_test_requests(void)
{
  uint16_t index;

  for (index = 0U; index < AUTO_PROCESS_STAGE_COUNT; ++index)
  {
    stage_test_request[index] = 0U;
  }
}

static void reset_current_stage(void)
{
  if ((stage_active != 0U) &&
      (current_stage_definition != NULL) &&
      (current_stage_definition->reset != NULL))
  {
    current_stage_definition->reset();
  }
  stage_active = 0U;
  current_stage_definition = NULL;
}

static void start_stage(uint16_t stage_index)
{
  const AutoStageDefinition *stage;

  if (stage_index >= AUTO_PROCESS_STAGE_COUNT)
  {
    return;
  }

  /* 记录当前正在执行的小流程编号，方便后续判断流程是否结束和切换下一阶段。 */
  reset_current_stage();
  current_stage = stage_index;
  /* 记录本阶段启动时间，供剩余时间统计使用。 */
  stage_start_tick = AutoDevice_GetProcessTick();
  TaskComm485_ReleaseNonzero(COMM485_INHIBIT_STARTUP |
                             COMM485_INHIBIT_SAFE_STOP |
                             COMM485_INHIBIT_MODE_CHANGE);
  stage = auto_stage_getters[current_stage]();
  current_stage_definition = stage;
  if (stage->init != NULL)
  {
    stage->init();
  }
  stage_active = 1U;

  /*
   * 初始化通用流程执行器。
   * 这里把当前小流程的名称、步骤表、步骤数量传给 app_flow.c。
   */
  AppFlow_Init(&auto_flow,
               stage->name,
               stage->steps,
               stage->count);

  /*
   * 启动当前小流程。
   * AppFlow_Start() 会把步骤索引清零，并从第一个步骤开始执行。
   */
  AppFlow_Start(&auto_flow);
}

/*
 * 初始化自动流程控制器。
 *
 * 上电后只初始化状态，不自动启动流程。
 * 后续由 AutoProcess_RequestFull() 或 AutoProcess_RequestStageTest() 触发。
 */
void AutoProcess_Init(void)
{
  uint16_t index;

  /* 当前阶段默认从 0 开始，也就是入舱准备。 */
  current_stage = 0U;
  /* 当前阶段尚未启动，启动时间清零。 */
  stage_start_tick = 0U;
  /* 自动流程当前未运行。 */
  auto_running = 0U;
  /* 清除流程完成标志。 */
  auto_finished = 0U;
  /* 清除流程失败标志。 */
  auto_failed = 0U;
  /* 清除自清洁触发标志，避免重复触发自清洁。 */
  clean_requested = 0U;
  /* 清除完整自动流程请求。 */
  full_request = 0U;
  /* 清除单段测试模式标志。 */
  stage_test_mode = 0U;
  restart_current_stage_request = 0U;
  resume_request = 0U;
  reset_request = 0U;
  abort_request = 0U;
  terminate_request = 0U;
  current_stage_definition = NULL;
  stage_active = 0U;

  for (index = 0U; index < AUTO_PROCESS_STAGE_COUNT; ++index)
  {
    /* 清除每一个小流程的手动测试触发标志。 */
    stage_test_request[index] = 0U;
  }
}

/*
 * 启动完整自动流程。
 *
 * 完整流程会从“入舱准备”开始，按工艺顺序一直跑到“出舱流程”。
 * 全部完成后会自动触发一次自清洁。
 */
void AutoProcess_Start(void)
{
  AutoDevice_ClearFault();
  /* 完整流程启动前切到自动输出仲裁，自动请求才允许作用于硬件。 */
  TaskOutput_SetAutoMode(1U);
  /* 标记自动流程正在运行。 */
  auto_running = 1U;
  /* 清除完成标志。 */
  auto_finished = 0U;
  /* 清除失败标志。 */
  auto_failed = 0U;
  /* 清除自清洁触发标志，确保本次完整流程结束后能触发一次自清洁。 */
  clean_requested = 0U;
  /* 完整流程不是单段测试模式。 */
  stage_test_mode = 0U;
  restart_current_stage_request = 0U;
  resume_request = 0U;
  /* 从第 0 个阶段开始，也就是入舱准备。 */
  start_stage(0U);
}

/*
 * 请求执行完整自动流程。
 *
 * 这个接口只置位请求标志，不直接启动。
 * 真正启动发生在 AutoProcess_Process() 中，避免跨线程直接改运行状态。
 */
void AutoProcess_Reset(void)
{
  AutoDevice_ClearFault();
  /*
   * 清空通用步骤执行器的索引、阶段计时和完成/失败状态。
   * 当前小流程的私有计时和缓存由其 reset 回调统一清零。
   */
  reset_current_stage();
  memset(&auto_flow, 0, sizeof(auto_flow));
  auto_running = 0U;
  auto_finished = 0U;
  auto_failed = 0U;
  clean_requested = 0U;
  stage_test_mode = 0U;
  restart_current_stage_request = 0U;
  resume_request = 0U;
  current_stage = 0U;
  TaskWaterTank_ClearSelfCleanPrepare();
}

/* 单流程正常完成后统一安全收尾，并保持自动模式返回空闲调度层。 */
static void finish_stage_test(void)
{
  AutoDevice_AllDangerousOutputsOff();
  TaskOutput_ClearSource(APP_OUTPUT_SOURCE_AUTO);
  AutoDevice_ClearProcessSignals();
  AutoProcess_Reset();
}

void AutoProcess_Abort(void)
{
  AutoDevice_AllDangerousOutputsOff();
  AutoDevice_SetFault(AUTO_FAULT_PROCESS_ABORTED);
  reset_current_stage();
  auto_running = 0U;
  auto_finished = 0U;
  auto_failed = 1U;
  clean_requested = 0U;
  full_request = 0U;
  stage_test_mode = 0U;
  restart_current_stage_request = 0U;
  resume_request = 0U;
  clear_stage_test_requests();
  TaskWaterTank_ClearSelfCleanPrepare();

  TaskOutput_ClearSource(APP_OUTPUT_SOURCE_AUTO);
}

void AutoProcess_TerminateServiceToExit(void)
{
  AutoDevice_AllDangerousOutputsOff();
  reset_current_stage();
  memset(&auto_flow, 0, sizeof(auto_flow));

  if (current_stage < (uint16_t)AUTO_PROCESS_STAGE_ENTRY)
  {
    auto_running = 0U;
    auto_finished = 1U;
    auto_failed = 0U;
    clean_requested = 0U;
    full_request = 0U;
    stage_test_mode = 0U;
    restart_current_stage_request = 0U;
    resume_request = 0U;
    current_stage = 0U;
    clear_stage_test_requests();
    TaskWaterTank_ClearSelfCleanPrepare();
    return;
  }

  auto_running = 1U;
  auto_finished = 0U;
  auto_failed = 0U;
  clean_requested = 0U;
  full_request = 0U;
  stage_test_mode = 0U;
  restart_current_stage_request = 0U;
  resume_request = 0U;
  clear_stage_test_requests();
  TaskWaterTank_RequestSelfCleanPrepare();
  start_stage((uint16_t)AUTO_PROCESS_STAGE_EXIT);
}

void AutoProcess_RequestRestartCurrentStage(void)
{
  if (auto_running != 0U)
  {
    restart_current_stage_request = 1U;
  }
}

void AutoProcess_RequestReset(void)
{
  full_request = 0U;
  clear_stage_test_requests();
  reset_request = 1U;
}

void AutoProcess_RequestAbort(void)
{
  abort_request = 1U;
}

void AutoProcess_RequestTerminateServiceToExit(void)
{
  terminate_request = 1U;
}

void AutoProcess_RequestResume(void)
{
  if (auto_running != 0U)
  {
    resume_request = 1U;
  }
}

void AutoProcess_RequestFull(void)
{
  /*
   * 请求执行完整自动流程。
   *
   * 后续上位机“启动自动流程”命令可以调用这个接口。
   * 如果当前已有流程在运行，请求会被忽略，避免中途重入。
   */
  if (AutoDevice_IsEmergencyStopped() != 0U)
  {
    return;
  }

  /* 置位完整流程请求，自动流程线程下一次轮询时会启动完整流程。 */
  full_request = 1U;
}

/*
 * 请求单独测试某一个小流程。
 *
 * 例如：
 *   AutoProcess_RequestStageTest(AUTO_PROCESS_STAGE_SPRAY);
 * 表示只测试“冲洗”小流程。
 */
void AutoProcess_RequestStageTest(AutoProcessStage stage)
{
  /*
   * 单段流程调试请求。
   *
   * 手动调试时可以单独触发某一个小流程，例如只跑“冲洗”或只跑“吹干”。
   * 单段测试跑完后会停在完成状态，不会继续跑后面的阶段，也不会自动触发自清洁。
   */
  if ((AutoDevice_IsEmergencyStopped() == 0U) &&
      (stage < AUTO_PROCESS_STAGE_COUNT))
  {
    /* 置位指定小流程的测试标志，自动流程线程下一次轮询时会启动该小流程。 */
    stage_test_request[stage] = 1U;
  }
}

/*
 * 清除某个小流程的测试请求。
 *
 * 如果上位机误发了测试请求，或者还没启动前想取消，可以调用这个接口。
 */
void AutoProcess_ClearStageTest(AutoProcessStage stage)
{
  if (stage < AUTO_PROCESS_STAGE_COUNT)
  {
    /* 清除指定阶段的测试标志。 */
    stage_test_request[stage] = 0U;
  }
}

/*
 * 查询某个小流程是否有待执行的测试请求。
 */
uint8_t AutoProcess_GetStageTestFlag(AutoProcessStage stage)
{
  if (stage >= AUTO_PROCESS_STAGE_COUNT)
  {
    /* 阶段编号非法时返回 0。 */
    return 0U;
  }

  /* 返回指定阶段的测试请求标志。 */
  return stage_test_request[stage];
}

/*
 * 查询自动流程是否正在运行。
 */
uint8_t AutoProcess_IsRunning(void)
{
  return auto_running;
}

uint8_t AutoProcess_IsStageTest(void)
{
  return stage_test_mode;
}

/*
 * 查询是否处于单阶段测试。
 *
 * 覆盖两种情况：
 * 1. 测试请求已提交但自动线程尚未启动（stage_test_request 置位）。
 * 2. 测试已经启动正在运行（stage_test_mode 置位）。
 * 故障后 stage_test_mode 保持置位直到复位，上位机据此持续显示单阶段测试模式。
 */
uint8_t AutoProcess_IsStageTestActive(void)
{
  uint16_t index;

  if (stage_test_mode != 0U)
  {
    return 1U;
  }

  for (index = 0U; index < AUTO_PROCESS_STAGE_COUNT; ++index)
  {
    if (stage_test_request[index] != 0U)
    {
      return 1U;
    }
  }

  return 0U;
}

uint16_t AutoProcess_GetCurrentStage(void)
{
  return current_stage;
}

uint16_t AutoProcess_GetCurrentStepId(void)
{
  if ((auto_finished != 0U) ||
      ((auto_running == 0U) && (auto_failed == 0U)))
  {
    return 0U;
  }

  return AppFlow_GetCurrentStepId(&auto_flow);
}

void AutoProcess_GetStatus(AutoProcessStatus *status)
{
  uint32_t primask;

  if (status == NULL)
  {
    return;
  }

  primask = __get_PRIMASK();
  __disable_irq();
  status->running = auto_running;
  status->finished = auto_finished;
  status->failed = auto_failed;
  status->stage_test_mode = stage_test_mode;
  status->stage = current_stage;
  status->step = ((auto_finished != 0U) ||
                  ((auto_running == 0U) && (auto_failed == 0U))) ?
                 0U : AppFlow_GetCurrentStepId(&auto_flow);
  if (primask == 0U)
  {
    __enable_irq();
  }
}

/*
 * 单个计时阶段的理论时长 = 配方时间(运行时读取当前激活配方) + 固定开销宏。
 * 非计时阶段(入舱、出舱等)返回0。
 */
static uint32_t stage_duration_ms(uint16_t stage)
{
  const AppRecipe_t *recipe = AppParam_GetActiveRecipe();

  switch (stage)
  {
    case AUTO_PROCESS_STAGE_SPRAY:
      return recipe->spray_time_ms + SPRAY_STAGE_FIXED_MS;
    case AUTO_PROCESS_STAGE_BATH:
      return recipe->liquid_soak_time_ms + recipe->bath_time_ms +
             BATH_STAGE_FIXED_MS;
    case AUTO_PROCESS_STAGE_CLEAN:
      return recipe->clean_time_ms + CLEAN_STAGE_FIXED_MS;
    case AUTO_PROCESS_STAGE_DRY:
      return recipe->dry_time_ms + DRY_STAGE_FIXED_MS;
    default:
      return 0U;
  }
}

uint32_t AutoProcess_GetTotalTimeMs(void)
{
  return stage_duration_ms((uint16_t)AUTO_PROCESS_STAGE_SPRAY) +
         stage_duration_ms((uint16_t)AUTO_PROCESS_STAGE_BATH) +
         stage_duration_ms((uint16_t)AUTO_PROCESS_STAGE_CLEAN) +
         stage_duration_ms((uint16_t)AUTO_PROCESS_STAGE_DRY);
}

uint32_t AutoProcess_GetRemainingTimeMs(void)
{
  uint32_t remaining;
  uint32_t duration;
  uint32_t elapsed_ms;
  uint16_t stage;

  if ((auto_running == 0U) || (auto_failed != 0U))
  {
    /* 空闲、已完成或失败状态，没有剩余时间。 */
    return 0U;
  }

  if (current_stage < (uint16_t)AUTO_PROCESS_STAGE_SPRAY)
  {
    /* 入舱准备/入舱/洗前准备等水温的阶段时间不可预估，先按总时间上报。 */
    return AutoProcess_GetTotalTimeMs();
  }

  if (current_stage > (uint16_t)AUTO_PROCESS_STAGE_DRY)
  {
    /* 烘干已结束，剩余时间为0。 */
    return 0U;
  }

  /* 当前阶段剩余时间，已运行时间封顶不超过阶段理论时长。 */
  duration = stage_duration_ms(current_stage);
  elapsed_ms = AutoDevice_GetProcessTick() - stage_start_tick;
  remaining = (elapsed_ms < duration) ? (duration - elapsed_ms) : 0U;

  if (stage_test_mode != 0U)
  {
    /* 单段测试跑完当前阶段就停，只报当前阶段的剩余时间。 */
    return remaining;
  }

  /* 完整流程：累加后续阶段一直到烘干的理论时长。 */
  for (stage = current_stage + 1U;
       stage <= (uint16_t)AUTO_PROCESS_STAGE_DRY;
       ++stage)
  {
    remaining += stage_duration_ms(stage);
  }

  return remaining;
}

uint32_t AutoProcess_GetCurrentStageRemainingTimeMs(void)
{
  uint32_t duration;
  uint32_t elapsed_ms;

  if ((auto_running == 0U) || (auto_failed != 0U))
  {
    return 0U;
  }

  if ((current_stage < (uint16_t)AUTO_PROCESS_STAGE_SPRAY) ||
      (current_stage > (uint16_t)AUTO_PROCESS_STAGE_DRY))
  {
    return 0U;
  }

  duration = stage_duration_ms(current_stage);
  elapsed_ms = AutoDevice_GetProcessTick() - stage_start_tick;

  return (elapsed_ms < duration) ? (duration - elapsed_ms) : 0U;
}

/*
 * 检查是否有等待启动的流程请求。
 *
 * 优先级：
 * 1. 完整自动流程请求。
 * 2. 单段小流程测试请求，按阶段顺序扫描。
 */
static uint8_t start_pending_request(void)
{
  uint16_t index;

  if ((auto_running != 0U) || (auto_failed != 0U))
  {
    /* 正在运行或处于失败状态时，不接受新的启动请求。 */
    return 0U;
  }

  if (full_request != 0U)
  {
    /* 消耗完整流程请求标志，避免重复启动。 */
    full_request = 0U;
    /* 启动完整自动流程。 */
    AutoProcess_Start();
    return 1U;
  }

  for (index = 0U; index < AUTO_PROCESS_STAGE_COUNT; ++index)
  {
    if (stage_test_request[index] != 0U)
    {
      /* 消耗当前小流程测试请求标志，避免重复启动。 */
      stage_test_request[index] = 0U;
      /* 标记流程正在运行。 */
      auto_running = 1U;
      /* 清除完成标志。 */
      auto_finished = 0U;
      /* 清除失败标志。 */
      auto_failed = 0U;
      /* 单段测试不触发自清洁，因此清除自清洁请求标志。 */
      clean_requested = 0U;
      /* 标记当前是单段测试模式。 */
      stage_test_mode = 1U;
      /* 直接启动指定阶段。 */
      start_stage(index);
      return 1U;
    }
  }

  /* 没有任何待启动请求。 */
  return 0U;
}

/*
 * 自动流程周期处理函数。
 *
 * task_auto.c 会周期调用这个函数。
 * 它负责：
 * - 等待并启动完整流程请求。
 * - 等待并启动单段测试请求。
 * - 推动当前小流程运行。
 * - 当前小流程完成后切换下一个小流程。
 * - 完整流程全部完成后触发自清洁。
 */
void AutoProcess_Process(void)
{
  uint16_t step_id;

  if (abort_request != 0U)
  {
    abort_request = 0U;
    AutoProcess_Abort();
    return;
  }

  if (terminate_request != 0U)
  {
    terminate_request = 0U;
    AutoProcess_TerminateServiceToExit();
    return;
  }

  if (reset_request != 0U)
  {
    reset_request = 0U;
    AutoProcess_Reset();
  }

  if ((resume_request != 0U) && (auto_running != 0U))
  {
    resume_request = 0U;
    step_id = AppFlow_GetCurrentStepId(&auto_flow);
    TaskComm485_ReleaseNonzero(COMM485_INHIBIT_PAUSE |
                               COMM485_INHIBIT_SAFE_STOP);
    if ((current_stage_definition != NULL) &&
        (current_stage_definition->resume != NULL) &&
        (current_stage_definition->resume(step_id) != HAL_OK))
    {
      AutoDevice_AllDangerousOutputsOff();
      AutoDevice_SetFault(AUTO_FAULT_OUTPUT_COMMAND_FAILED);
      AutoDevice_Report(AUTO_REPORT_SENSOR_FAULT);
      reset_current_stage();
      auto_running = 0U;
      auto_failed = 1U;
      return;
    }
    AutoDevice_SetPaused(0U);
  }

  if ((restart_current_stage_request != 0U) && (auto_running != 0U))
  {
    restart_current_stage_request = 0U;
    start_stage(current_stage);
    return;
  }

  if ((auto_running == 0U) || (auto_finished != 0U) || (auto_failed != 0U))
  {
    /* 当前没有流程运行时，检查有没有新的完整流程或单段测试请求。 */
    (void)start_pending_request();
    return;
  }

  /* 推动当前小流程执行一步。 */
  AppFlow_Process(&auto_flow);
  if (AppFlow_IsFailed(&auto_flow) != 0U)
  {
    if (AutoDevice_GetFault() == AUTO_FAULT_NONE)
    {
      AutoDevice_SetFault(AUTO_FAULT_PROCESS_ABORTED);
    }
    AutoDevice_Report(AUTO_REPORT_SENSOR_FAULT);
    /* 当前小流程失败，则整个自动流程进入失败状态。 */
    reset_current_stage();
    auto_failed = 1U;
    auto_running = 0U;
    return;
  }

  if (AppFlow_IsFinished(&auto_flow) == 0U)
  {
    /* 当前小流程还没执行完，本周期不切换阶段。 */
    return;
  }

  reset_current_stage();

  if (stage_test_mode != 0U)
  {
    /* 单流程完成后返回自动模式空闲调度层，不继续后续阶段。 */
    finish_stage_test();
    return;
  }

  /* 完整自动流程模式下，当前小流程完成后切换到下一个阶段。 */
  current_stage++;
  if (current_stage < AUTO_PROCESS_STAGE_COUNT)
  {
    /* 还有后续阶段，继续启动下一小流程。 */
    start_stage(current_stage);
  }
  else
  {
    /* 所有自动小流程都跑完。 */
    auto_running = 0U;
    auto_finished = 1U;

    /*
     * 工艺要求：每次自动流程全部执行完成后，必须执行一次自清洁。
     * 这里不直接跑自清洁，而是通知自清洁线程，让两个流程解耦。
     */
    if (clean_requested == 0U)
    {
      /* 标记已经触发过自清洁，避免重复触发。 */
      clean_requested = 1U;
      /* 通知自清洁线程启动自清洁流程。 */
      TaskClean_Request();
    }
  }
}

/*
 * 查询自动流程是否完成。
 */
uint8_t AutoProcess_IsFinished(void)
{
  return auto_finished;
}

/*
 * 查询自动流程是否失败。
 */
uint8_t AutoProcess_IsFailed(void)
{
  return auto_failed;
}
