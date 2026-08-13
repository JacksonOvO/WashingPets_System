#include "task_host232.h"

#include "app_iap.h"
#include "cmsis_os.h"
#include "main.h"
#include "task_watchdog.h"

#define HOST232_TASK_STACK      512U
#define HOST232_TASK_PRIORITY   osPriorityNormal

extern UART_HandleTypeDef huart1;

static osThreadId_t host232_task_handle;

static const osThreadAttr_t host232_task_attributes = {
  .name = "host232",
  .stack_size = HOST232_TASK_STACK * 4U,
  .priority = HOST232_TASK_PRIORITY,
};

static void Host232Task(void *argument)
{
  uint8_t data;

  (void)argument;
  AppIap_Init(&huart1);

  for (;;)
  {
    TaskWatchdog_Heartbeat(TASK_WATCHDOG_HOST232);
    /* USART2 收到的数据由 main.c 的中断回调放入环形缓存，这里只负责取走并交给协议解析器。 */
    while (UART2_Read(&data, 1U) == 1U)
    {
      AppIap_ProcessByte(data);
    }

    /* 周期性发送业务遥测帧。 */
    AppIap_Service();
    osDelay(1U);
  }
}

void TaskHost232_Create(void)
{
  host232_task_handle = osThreadNew(Host232Task, NULL, &host232_task_attributes);
  (void)host232_task_handle;
}
