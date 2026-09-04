#include "app_tasks.h"

#include "cmsis_os.h"
#include "task_auto.h"
#include "task_clean.h"
#include "task_comm485.h"
#include "task_host232.h"
#include "task_life_support.h"
#include "task_output.h"
#include "task_self_test.h"
#include "task_water_tank.h"
#include "task_watchdog.h"

#define APP_ENABLE_WATER_TANK_PLACEHOLDER  1U
#define APP_ENABLE_LIFE_SUPPORT_PLACEHOLDER 0U

/*
 * 创建所有业务线程。
 *
 * main.c 只负责 HAL、时钟、GPIO、USART、RTOS 内核这些基础初始化。
 * 具体业务统一拆到各个 task_xxx.c 文件里。
 */
void AppTasks_Create(void)
{
  /*
   * 创建输出仲裁线程。
   * 所有普通 GPIO 输出最终都从这个线程统一判断和输出。
   */
  TaskOutput_Create();

  /*
   * 创建 USART2 上位机通讯线程。
   * 包含上位机协议解析、IAP 升级入口、普通调试命令入口。
   */
  TaskHost232_Create();

  /*
   * 创建 USART1/RS485 通讯线程。
   * 可控硅和后续 485 传感器/执行器都从这个线程排队访问总线。
   */
  TaskComm485_Create();

  /*
   * 创建自动流程线程。
   * 入舱准备、入舱流程、洗前准备、冲洗、沐浴、清洗、吹干、出舱流程都由它调度。
   */
  TaskAuto_Create();

  /*
   * 创建自清洁线程。
   * 自动流程全部结束后会触发它执行一次自清洁。
   */
  TaskClean_Create();

  TaskSelfTest_Create();

  /*
   * 创建水箱恒温后台线程。
   * 待机时维护水箱水位和温度，用水阶段需要暂停避免抢阀门/水泵。
   */
#if (APP_ENABLE_WATER_TANK_PLACEHOLDER != 0U)
  TaskWaterTank_Create();
#endif

  /*
   * 创建舱内生命保障线程。
   * 负责氧气、温度、湿度、噪音等安全保护，优先级高于普通自动流程。
   */
#if (APP_ENABLE_LIFE_SUPPORT_PLACEHOLDER != 0U)
  TaskLifeSupport_Create();
#endif

  TaskWatchdog_Create();
}
