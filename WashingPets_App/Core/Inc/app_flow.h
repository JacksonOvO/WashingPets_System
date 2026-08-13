#ifndef APP_FLOW_H
#define APP_FLOW_H

#include "stm32f4xx_hal.h"

/*
 * 单个步骤动作函数的返回值。
 */
typedef enum
{
  APP_FLOW_STEP_RUNNING = 0,  /* 动作还没完成，流程保持在当前步骤继续等待 */
  APP_FLOW_STEP_DONE,         /* 动作完成，流程可以进入保持/后延时/下一步 */
  APP_FLOW_STEP_FAIL,         /* 动作失败，流程进入失败状态 */
  APP_FLOW_STEP_JUMP          /* 动作已调用跳转接口，流程从目标步骤重新开始 */
} AppFlowStepResult;

/* 前向声明：流程上下文结构体。 */
typedef struct AppFlowContext AppFlowContext;

/* 前向声明：流程步骤结构体。 */
typedef struct AppFlowStep AppFlowStep;

/*
 * 单个步骤的动作函数类型。
 *
 * ctx：
 *   当前流程上下文，可以读取当前流程名、当前步骤索引等运行信息。
 *
 * step：
 *   当前正在执行的步骤，可以读取步骤编号、步骤名称、动作说明和延时配置。
 */
typedef AppFlowStepResult (*AppFlowAction)(AppFlowContext *ctx,
                                           const AppFlowStep *step);

/*
 * 单个工艺动作步骤。
 *
 * 后续调试工艺流程时，最常改的就是这个结构对应的步骤表。
 */
struct AppFlowStep
{
  uint16_t id;                 /* 步骤编号，对应工艺表里的序号 */
  const char *name;            /* 步骤名称，用于串口调试打印和故障定位 */
  const char *description;     /* 动作说明，对应工艺表里的“动作”内容 */
  uint32_t delay_before_ms;    /* 动作前延时，动作执行前先等待这么久 */
  uint32_t delay_after_ms;     /* 动作后延时，给机构、水路或风机稳定时间 */
  AppFlowAction action;        /* 具体动作函数，返回 RUNNING/DONE/FAIL */
};

/*
 * 流程运行上下文。
 *
 * 每一条正在运行的流程，都需要一个独立的 AppFlowContext。
 */
struct AppFlowContext
{
  const char *flow_name;       /* 流程名称，例如“冲洗”“自清洁”“水箱恒温系统” */
  const AppFlowStep *steps;    /* 当前流程使用的步骤表指针 */
  uint16_t step_count;         /* 当前流程步骤总数量 */
  uint16_t current_index;      /* 当前执行到 steps[] 数组里的第几个步骤 */
  uint8_t running;             /* 流程运行标志，1 表示正在运行 */
  uint8_t finished;            /* 流程完成标志，1 表示全部步骤已完成 */
  uint8_t failed;              /* 流程失败标志，1 表示某一步返回失败 */
  uint8_t phase;               /* 当前步骤内部阶段：前延时/动作/保持/后延时 */
  uint8_t action_called;       /* 当前步骤动作是否已经至少调用过一次 */
  uint32_t phase_tick;         /* 当前阶段开始时的系统 tick，用于延时判断 */
};

void AppFlow_Init(AppFlowContext *ctx,
                  const char *flow_name,
                  const AppFlowStep *steps,
                  uint16_t step_count);
void AppFlow_Start(AppFlowContext *ctx);
void AppFlow_Process(AppFlowContext *ctx);

/*
 * 按步骤表中的 id 跳转。成功返回1，找不到步骤或参数无效返回0。
 * 动作函数成功调用后必须返回 APP_FLOW_STEP_JUMP。
 */
uint8_t AppFlow_JumpToStep(AppFlowContext *ctx, uint16_t step_id);

uint8_t AppFlow_IsFinished(const AppFlowContext *ctx);
uint8_t AppFlow_IsFailed(const AppFlowContext *ctx);
uint16_t AppFlow_GetCurrentStepId(const AppFlowContext *ctx);

AppFlowStepResult AppFlow_ActionTodo(AppFlowContext *ctx,
                                     const AppFlowStep *step);

#endif
