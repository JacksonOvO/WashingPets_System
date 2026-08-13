#include "app_flow.h"

#include "auto_device.h"
#include "main.h"
#include <stdio.h>
#include <string.h>

/*
 * 流程调试打印开关。
 *
 * 0：关闭流程调试打印，正式运行建议关闭，避免串口2刷屏。
 * 1：打开流程调试打印，每个流程启动、结束、步骤执行都会打印到串口2。
 */
#define APP_FLOW_DEBUG_PRINT 0U

/*
 * 流程执行器内部阶段。
 *
 * 每一个 AppFlowStep 都会按下面顺序执行：
 * 1. 动作前延时。
 * 2. 执行动作函数。
 * 3. 动作后延时。
 */
typedef enum
{
  APP_FLOW_PHASE_DELAY_BEFORE = 0,
  APP_FLOW_PHASE_ACTION,
  APP_FLOW_PHASE_DELAY_AFTER
} AppFlowPhase;

/*
 * 判断某个延时时间是否已经到达。
 *
 * start_tick：进入某个阶段时记录的 HAL_GetTick()。
 * delay_ms：需要等待的毫秒数。
 */
static uint8_t time_reached(uint32_t start_tick, uint32_t delay_ms)
{
  /* 使用 tick 差值判断，兼容 HAL_GetTick() 自然回绕。 */
  return ((AutoDevice_GetProcessTick() - start_tick) >= delay_ms) ? 1U : 0U;
}

/*
 * 流程调试文本输出。
 *
 * 所有流程调试打印都从这里走，方便用 APP_FLOW_DEBUG_PRINT 一键开关。
 */
static void flow_print(const char *text)
{
#if APP_FLOW_DEBUG_PRINT
  if (text != NULL)
  {
    /* UART2_Send 是 main.c 提供的串口2发送接口，用于把调试信息发给上位机。 */
    (void)UART2_Send((const uint8_t *)text, (uint16_t)strlen(text));
  }
#else
  /* 关闭调试打印时，避免未使用参数告警。 */
  (void)text;
#endif
}

/*
 * 打印当前执行到哪一个流程步骤。
 *
 * ctx：流程上下文，里面有流程名称。
 * step：当前步骤，里面有步骤编号、步骤名和动作说明。
 */
static void flow_print_step(const AppFlowContext *ctx, const AppFlowStep *step)
{
#if APP_FLOW_DEBUG_PRINT
  char line[160];

  /*
   * snprintf 用于安全拼接调试字符串。
   * 使用 sizeof(line) 限制长度，避免字符串过长造成栈内存越界。
   */
  (void)snprintf(line,
                 sizeof(line),
                 "\r\nFLOW %s STEP %u %s\r\n",
                 ctx->flow_name,
                 (unsigned int)step->id,
                 step->name);

  /* 调用统一打印函数，是否真正输出由 APP_FLOW_DEBUG_PRINT 控制。 */
  flow_print(line);
#else
  /* 关闭调试打印时，避免未使用参数告警。 */
  (void)ctx;
  (void)step;
#endif
}

/*
 * 初始化流程上下文。
 *
 * 这个函数只绑定流程表、流程名、步骤数量，并清空运行状态。
 * 它不会启动流程。启动流程需要继续调用 AppFlow_Start()。
 */
void AppFlow_Init(AppFlowContext *ctx,
                  const char *flow_name,
                  const AppFlowStep *steps,
                  uint16_t step_count)
{
  if (ctx == NULL)
  {
    /* 上层传入空指针时直接返回，避免访问非法地址。 */
    return;
  }

  ctx->flow_name = flow_name;                  /* 保存流程名称，用于调试打印。 */
  ctx->steps = steps;                          /* 绑定当前流程的步骤表。 */
  ctx->step_count = step_count;                /* 保存步骤数量，防止执行时越界。 */
  ctx->current_index = 0U;                     /* 默认从第 0 个步骤开始。 */
  ctx->running = 0U;                           /* 初始化后流程尚未运行。 */
  ctx->finished = 0U;                          /* 清除完成标志。 */
  ctx->failed = 0U;                            /* 清除失败标志。 */
  ctx->phase = APP_FLOW_PHASE_DELAY_BEFORE;    /* 每个步骤都从动作前延时开始。 */
  ctx->action_called = 0U;                     /* 当前步骤动作尚未调用。 */
  ctx->phase_tick = AutoDevice_GetProcessTick(); /* 记录当前阶段起始 tick。 */
}

/*
 * 启动流程。
 *
 * 每次调用都会从第一个步骤重新开始执行。
 */
void AppFlow_Start(AppFlowContext *ctx)
{
  if ((ctx == NULL) || (ctx->steps == NULL) || (ctx->step_count == 0U))
  {
    /* 流程上下文或流程表不完整时不启动，避免后续访问空指针。 */
    return;
  }

  ctx->current_index = 0U;                     /* 从第一个步骤开始。 */
  ctx->running = 1U;                           /* 标记流程正在运行。 */
  ctx->finished = 0U;                          /* 清除完成标志。 */
  ctx->failed = 0U;                            /* 清除失败标志。 */
  ctx->phase = APP_FLOW_PHASE_DELAY_BEFORE;    /* 从动作前延时阶段开始。 */
  ctx->action_called = 0U;                     /* 允许当前步骤首次执行动作。 */
  ctx->phase_tick = AutoDevice_GetProcessTick(); /* 记录当前阶段起始 tick。 */

  /* 打印流程启动信息，默认关闭。 */
  flow_print("\r\nFLOW START\r\n");
}

/*
 * 按步骤ID跳转到当前步骤表中的任意步骤。
 * 跳转后目标步骤会从“动作前延时”阶段重新开始，原步骤不会再执行
 * 保持时间、动作后延时以及默认的 current_index++。
 */
uint8_t AppFlow_JumpToStep(AppFlowContext *ctx, uint16_t step_id)
{
  uint16_t index;

  if ((ctx == NULL) || (ctx->steps == NULL) ||
      (ctx->step_count == 0U) || (ctx->running == 0U))
  {
    return 0U;
  }

  for (index = 0U; index < ctx->step_count; ++index)
  {
    if (ctx->steps[index].id == step_id)
    {
      ctx->current_index = index;
      ctx->phase = APP_FLOW_PHASE_DELAY_BEFORE;
      ctx->phase_tick = AutoDevice_GetProcessTick();
      ctx->action_called = 0U;
      return 1U;
    }
  }

  return 0U;
}

/*
 * 周期执行流程。
 *
 * 这个函数由任务线程周期调用，例如每 10ms 调用一次。
 * 函数内部不做长时间阻塞，只判断当前阶段是否满足切换条件。
 */
void AppFlow_Process(AppFlowContext *ctx)
{
  const AppFlowStep *step;
  AppFlowStepResult result;

  if ((ctx == NULL) || (ctx->running == 0U) ||
      (ctx->finished != 0U) || (ctx->failed != 0U))
  {
    /* 未运行、已完成、已失败或上下文无效时，不处理。 */
    return;
  }

  /*
   * 急停和上位机安全停止是所有自动阶段的最高优先级条件。
   * 放在通用执行器中可以覆盖入舱、洗护、吹干、自清洁等全部步骤，
   * 避免某个阶段遗漏输入判断后仍继续执行输出。
   */
  if ((AutoDevice_IsEmergencyStopped() != 0U) ||
      (AutoDevice_IsStopRequested() != 0U))
  {
    AutoDevice_AllDangerousOutputsOff();
    ctx->running = 0U;
    ctx->failed = 1U;
    flow_print("\r\nFLOW SAFETY STOP\r\n");
    return;
  }

  /* 安全条件检查完成后才允许暂停，保证暂停期间急停仍能立即生效。 */
  if (AutoDevice_IsPaused() != 0U)
  {
    return;
  }

  if (ctx->current_index >= ctx->step_count)
  {
    /* 当前步骤索引超过步骤数量，说明流程已经全部执行完成。 */
    ctx->running = 0U;
    ctx->finished = 1U;

    /* 打印流程完成信息，默认关闭。 */
    flow_print("\r\nFLOW DONE\r\n");
    return;
  }

  /* 获取当前步骤，后续所有延时和动作都围绕这个步骤执行。 */
  step = &ctx->steps[ctx->current_index];

  switch ((AppFlowPhase)ctx->phase)
  {
    case APP_FLOW_PHASE_DELAY_BEFORE:
      /* 判断动作前延时是否到达。调试初期 delay_before_ms 通常为 0。 */
      if (time_reached(ctx->phase_tick, step->delay_before_ms) != 0U)
      {
        ctx->phase = APP_FLOW_PHASE_ACTION;    /* 延时到达，进入动作执行阶段。 */
        ctx->action_called = 0U;               /* 允许动作阶段打印一次步骤信息。 */
      }
      break;

    case APP_FLOW_PHASE_ACTION:
      if (ctx->action_called == 0U)
      {
        /* 第一次进入动作阶段时打印当前步骤，方便调试定位。 */
        flow_print_step(ctx, step);
        ctx->action_called = 1U;
      }

      /*
       * 调用步骤动作函数。
       * 如果 action 为空，则认为该步骤是纯占位步骤，直接完成。
       */
      result = (step->action != NULL) ?
               step->action(ctx, step) : APP_FLOW_STEP_DONE;

      if (result == APP_FLOW_STEP_DONE)
      {
        ctx->phase = APP_FLOW_PHASE_DELAY_AFTER; /* 动作完成，进入动作后延时。 */
        ctx->phase_tick = AutoDevice_GetProcessTick(); /* 记录动作后延时起始 tick。 */
      }
      else if (result == APP_FLOW_STEP_FAIL)
      {
        AutoDevice_AllDangerousOutputsOff();
        ctx->running = 0U;                     /* 停止流程。 */
        ctx->failed = 1U;                      /* 标记流程失败。 */
        flow_print("\r\nFLOW FAIL\r\n");       /* 打印失败信息，默认关闭。 */
      }
      else if (result == APP_FLOW_STEP_JUMP)
      {
        /*
         * AppFlow_JumpToStep() 已经设置好目标索引和阶段状态。
         * 此处不能再进入 HOLD，也不能执行默认的 current_index++。
         */
      }
      else
      {
        /* 动作仍在运行，例如等待传感器到位、等待上位机命令或等待超时判断。 */
      }
      break;

    case APP_FLOW_PHASE_DELAY_AFTER:
      /* 判断动作后延时是否到达，给机构、水路、风机留稳定时间。 */
      if (time_reached(ctx->phase_tick, step->delay_after_ms) != 0U)
      {
        ctx->current_index++;                    /* 当前步骤结束，切换到下一步。 */
        ctx->phase = APP_FLOW_PHASE_DELAY_BEFORE;/* 下一步仍从动作前延时开始。 */
        ctx->phase_tick = AutoDevice_GetProcessTick(); /* 记录下一步动作前延时起始 tick。 */
        ctx->action_called = 0U;                 /* 下一步动作尚未调用。 */
      }
      break;

    default:
      /* phase 异常时直接失败，避免流程状态错乱后继续动作。 */
      AutoDevice_AllDangerousOutputsOff();
      ctx->running = 0U;
      ctx->failed = 1U;
      break;
  }
}

/*
 * 查询流程是否完成。
 */
uint8_t AppFlow_IsFinished(const AppFlowContext *ctx)
{
  /* ctx 为空时返回未完成，避免上层误判。 */
  return ((ctx != NULL) && (ctx->finished != 0U)) ? 1U : 0U;
}

/*
 * 查询流程是否失败。
 */
uint8_t AppFlow_IsFailed(const AppFlowContext *ctx)
{
  /* ctx 为空时返回未失败，避免上层误判。 */
  return ((ctx != NULL) && (ctx->failed != 0U)) ? 1U : 0U;
}

uint16_t AppFlow_GetCurrentStepId(const AppFlowContext *ctx)
{
  if ((ctx == NULL) || (ctx->steps == NULL) ||
      (ctx->current_index >= ctx->step_count))
  {
    return 0U;
  }

  return ctx->steps[ctx->current_index].id;
}

/*
 * 通用占位动作。
 *
 * 当前阶段先把工艺流程框架搭起来，真实控制动作后续逐步填入。
 */
AppFlowStepResult AppFlow_ActionTodo(AppFlowContext *ctx,
                                     const AppFlowStep *step)
{
  (void)ctx;    /* 当前占位动作暂时不需要流程上下文。 */
  (void)step;   /* 当前占位动作暂时不需要步骤参数。 */

  /*
   * 后续真实动作接入建议：
   * - 普通 DO 输出：调用 TaskOutput_Request()。
   * - 485 设备/传感器：通过 task_comm485.c 的接口排队。
   * - 上位机交互：通过 host232 协议上报状态或等待命令。
   * - 条件等待：返回 APP_FLOW_STEP_RUNNING，条件满足后再返回 DONE。
   */
  return APP_FLOW_STEP_DONE;
}
