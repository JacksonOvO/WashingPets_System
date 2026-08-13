#include "task_life_support.h"

#include "app_flow.h"
#include "cmsis_os.h"

/* 生命保障线程配置。 */
#define LIFE_SUPPORT_TASK_STACK     768U
#define LIFE_SUPPORT_TASK_PRIORITY  osPriorityAboveNormal
#define LIFE_SUPPORT_PERIOD_MS      500U

/* 生命保障步骤表简化宏，默认延时为 0，动作函数先用占位函数。 */
#define STEP(id, name, desc) \
  { (id), (name), (desc), 0U, 0U, AppFlow_ActionTodo }

/* 生命保障线程句柄，保存 osThreadNew() 返回值。 */
static osThreadId_t life_support_task_handle;

/*
 * 生命保障流程上下文。
 *
 * 每一轮生命保障检查都会重新初始化并执行这个上下文。
 */
static AppFlowContext life_support_flow;

/*
 * 生命保障线程属性。
 *
 * 生命保障属于安全保护任务，优先级高于普通自动流程。
 */
static const osThreadAttr_t life_support_task_attributes = {
  .name = "life",
  .stack_size = LIFE_SUPPORT_TASK_STACK * 4U,
  .priority = LIFE_SUPPORT_TASK_PRIORITY,
};

/*
 * 舱内生命保障步骤表。
 *
 * 对应工艺文件 sheet“舱内生命保障系统”。
 * 它不是普通环境调节流程，而是安全保护流程。
 */
static const AppFlowStep life_support_steps[] = {
  STEP(10,  "CHECK_STAGE_ALLOW",     "判断当前自动流程阶段是否需要生命保障检测"),
  STEP(20,  "READ_LIFE_SENSOR",      "读取氧气、温度、湿度、噪音传感器数据"),
  STEP(30,  "CHECK_OXYGEN_SAFE",     "判断舱内氧气是否低于安全下限或严重下限"),
  STEP(40,  "OXYGEN_LOW_PROTECT",    "氧气低于安全下限时打开制氧机和进风扇"),
  STEP(50,  "OXYGEN_OK_STOP",        "氧气正常时关闭制氧机或保持当前状态"),
  STEP(60,  "CHECK_TEMP_SAFE",       "判断舱内温度是否低于安全下限或高于安全上限"),
  STEP(70,  "TEMP_LOW_PROTECT",      "温度低于安全下限时打开进风扇和进风加热管"),
  STEP(80,  "TEMP_OK_STOP_HEAT",     "温度未低于安全下限时关闭生命保障加热"),
  STEP(90,  "CHECK_HUMIDITY_SAFE",   "判断舱内湿度是否超过安全上限或严重上限"),
  STEP(100, "HUMIDITY_HIGH_PROTECT", "湿度超过安全上限时打开排风扇和进风扇"),
  STEP(110, "CHECK_NOISE_SAFE",      "判断当前噪音是否超过固定安全值"),
  STEP(120, "LIFE_STATE_KEEP",       "无安全风险时保持当前生命保障状态"),
  STEP(130, "NOISE_CONFIRM",         "持续检测噪音是否仍然超限"),
  STEP(140, "NO_RISK_NO_ADJUST",     "无安全风险时不主动调节温湿度，避免干扰主流程"),
  STEP(150, "SENSOR_ERROR_COUNT",    "记录传感器异常次数，判断是否连续异常"),
  STEP(160, "OXYGEN_SERIOUS_FAULT",  "氧气严重不足时停止自动流程并打开补氧保护"),
  STEP(170, "TEMP_SERIOUS_FAULT",    "温度过高时停止自动流程，关闭加热并通风"),
  STEP(180, "HUMIDITY_SERIOUS_FAULT","湿度严重异常时暂停或停止自动流程并通风"),
  STEP(190, "NOISE_SERIOUS_FAULT",   "噪音严重异常时停止自动流程并关闭相关设备"),
  STEP(200, "SENSOR_TIMEOUT_FAULT",  "关键传感器失效时停止自动流程并上报故障"),
  STEP(210, "LIFE_PERIOD_WAIT",      "周期等待 500ms~1000ms 后重新检测"),
};

/*
 * 生命保障线程入口。
 */
static void LifeSupportTask(void *argument)
{
  /* 当前线程没有使用启动参数。 */
  (void)argument;

  for (;;)
  {
    /*
     * 初始化一轮生命保障检查流程。
     */
    AppFlow_Init(&life_support_flow,
                 "舱内生命保障系统",
                 life_support_steps,
                 (uint16_t)(sizeof(life_support_steps) / sizeof(life_support_steps[0])));

    /* 启动本轮生命保障检查流程。 */
    AppFlow_Start(&life_support_flow);

    /*
     * 周期推进生命保障流程。
     * 后续真实动作接入后，这里会读取传感器并判断是否需要保护。
     */
    while ((AppFlow_IsFinished(&life_support_flow) == 0U) &&
           (AppFlow_IsFailed(&life_support_flow) == 0U))
    {
      /* 推动生命保障流程执行一步。 */
      AppFlow_Process(&life_support_flow);

      /* 10ms 小周期用于流程内部延时判断。 */
      osDelay(10U);
    }

    /*
     * 生命保障检查周期。
     * 工艺文件建议 500ms~1000ms，这里先取 500ms。
     */
    osDelay(LIFE_SUPPORT_PERIOD_MS);
  }
}

/*
 * 创建生命保障线程。
 */
void TaskLifeSupport_Create(void)
{
  /*
   * 创建生命保障后台线程。
   * 该线程优先级高于普通自动流程，用于后续安全保护。
   */
  life_support_task_handle = osThreadNew(LifeSupportTask,
                                         NULL,
                                         &life_support_task_attributes);

  /* 当前暂时不检查线程句柄，显式使用一下避免告警。 */
  (void)life_support_task_handle;
}
