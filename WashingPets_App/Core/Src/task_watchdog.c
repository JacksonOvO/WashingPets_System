#include "task_watchdog.h"

#include "cmsis_os.h"
#include "stm32f4xx_hal.h"

#define WATCHDOG_TASK_STACK          256U
#define WATCHDOG_CHECK_PERIOD_MS    1000U
#define WATCHDOG_HEARTBEAT_STALE_MS 6000U

static volatile uint32_t heartbeat_tick[TASK_WATCHDOG_COUNT];

static const osThreadAttr_t watchdog_task_attributes = {
  .name = "watchdog",
  .stack_size = WATCHDOG_TASK_STACK * 4U,
  .priority = osPriorityAboveNormal,
};

static void iwdg_start(void)
{
  IWDG->KR = 0xCCCCU;
  IWDG->KR = 0x5555U;
  IWDG->PR = 4U;
  IWDG->RLR = 4095U;
  while (IWDG->SR != 0U)
  {
  }
  IWDG->KR = 0xAAAAU;
}

static void WatchdogTask(void *argument)
{
  uint32_t now;
  uint8_t source;
  uint8_t healthy;

  (void)argument;
  iwdg_start();

  for (;;)
  {
    osDelay(WATCHDOG_CHECK_PERIOD_MS);
    now = HAL_GetTick();
    healthy = 1U;
    for (source = 0U; source < (uint8_t)TASK_WATCHDOG_COUNT; ++source)
    {
      if ((now - heartbeat_tick[source]) > WATCHDOG_HEARTBEAT_STALE_MS)
      {
        healthy = 0U;
        break;
      }
    }
    if (healthy != 0U)
    {
      IWDG->KR = 0xAAAAU;
    }
  }
}

void TaskWatchdog_Create(void)
{
  uint32_t now = HAL_GetTick();
  uint8_t source;

  for (source = 0U; source < (uint8_t)TASK_WATCHDOG_COUNT; ++source)
  {
    heartbeat_tick[source] = now;
  }
  (void)osThreadNew(WatchdogTask, NULL, &watchdog_task_attributes);
}

void TaskWatchdog_Heartbeat(TaskWatchdogSource source)
{
  if (source < TASK_WATCHDOG_COUNT)
  {
    heartbeat_tick[source] = HAL_GetTick();
  }
}

void TaskWatchdog_EmergencyOutputsOff(void)
{
  GPIOB->BSRR = GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 |
                GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_12 |
                GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
  /* PC14舱内照明低电平有效，即使紧急关断也保持开启。 */
  GPIOC->BSRR = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_13 | GPIO_PIN_15 |
                ((uint32_t)GPIO_PIN_14 << 16U);
  GPIOD->BSRR = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 |
                GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13;
  GPIOE->BSRR = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 |
                GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6;
  GPIOF->BSRR = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 |
                GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7 |
                GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10;
  __DSB();
}

void TaskWatchdog_EmergencyReset(void)
{
  __disable_irq();
  TaskWatchdog_EmergencyOutputsOff();
  NVIC_SystemReset();
  for (;;)
  {
  }
}
