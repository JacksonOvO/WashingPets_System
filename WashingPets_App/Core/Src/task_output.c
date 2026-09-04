#include "task_output.h"

#include "cmsis_os.h"
#include "rs485_config.h"
#include "task_comm485.h"
#include "task_watchdog.h"

/* 输出任务栈深度；CMSIS-RTOS2的stack_size按字节填写，创建时再乘4。 */
#define OUTPUT_TASK_STACK       768U
/* 输出涉及急停和门机构，优先级高于普通自动流程任务。 */
#define OUTPUT_TASK_PRIORITY    osPriorityNormal
/* 队列需要容纳一次流程切换时连续提交的多路阀门和泵命令。 */
#define OUTPUT_QUEUE_LENGTH     64U
/* 即使没有新命令也每10ms复查急停、光幕和门到位输入。 */
#define OUTPUT_RECHECK_MS       10U
#define OUTPUT_SYNC_FLAG        0x00000001U
/* 水箱到高液位后继续补水15秒，再由输出互锁强制关进水阀。 */
#define OUTPUT_TANK_HIGH_STOP_DELAY_MS 15000U

/* 单个输出点与STM32实际GPIO之间的硬件映射。 */
typedef struct
{
  GPIO_TypeDef *port;                 /* GPIO端口，例如GPIOB。 */
  uint16_t pin;                       /* GPIO引脚掩码，例如GPIO_PIN_12。 */
  uint8_t keep_on_during_emergency;   /* 1=急停时允许保持；0=急停时强制关闭。 */
} OutputHw;

/* 输出任务内部消息类型，所有运行期状态变化都通过消息队列串行处理。 */
typedef enum
{
  OUTPUT_COMMAND_SET = 0,       /* 修改自动或手动来源的某一个点。 */
  OUTPUT_COMMAND_SET_MODE,      /* 切换自动/手动输出仲裁模式。 */
  OUTPUT_COMMAND_CLEAR_SOURCE,  /* 清除某一来源的全部请求。 */
  OUTPUT_COMMAND_ALL_OFF,       /* 清除自动和手动两套请求。 */
  OUTPUT_COMMAND_SYNC           /* 同步屏障：确认前序输出命令已处理。 */
} OutputCommandType;

/* 自动流程、上位机手动控制发送给输出任务的一条消息。 */
typedef struct
{
  OutputCommandType type;   /* 本条消息要执行的操作。 */
  AppOutputSource source;   /* 请求来自自动流程还是手动控制。 */
  AutoOutputPoint point;    /* 目标输出点编号。 */
  uint8_t enabled;          /* 逻辑状态：1=开启，0=关闭。 */
  osThreadId_t sync_thread;  /* 同步等待线程；仅 OUTPUT_COMMAND_SYNC 使用。 */
  uint32_t sync_flag;        /* 同步回执标志；仅 OUTPUT_COMMAND_SYNC 使用。 */
} OutputCommand;

/*
 * 最终输出点位表，严格对应用户提供的硬件表。
 * 所有点均为低电平有效：逻辑开启写GPIO_PIN_RESET，逻辑关闭写GPIO_PIN_SET。
 * PB4配置为普通GPIO后不能再使用完整JTAG，但SWD下载和调试不受影响。
 * PC0用于OUT39热水器阀，PC1用于主板电源控制。
 */
static const OutputHw output_hw_table[AUTO_OUTPUT_COUNT] = {
  [AUTO_OUTPUT_EXHAUST_FAN] = {GPIOB, GPIO_PIN_12, 0U}, /* 排气扇，急停时强制关断。 */
  [AUTO_OUTPUT_OXYGEN_MACHINE] = {GPIOB, GPIO_PIN_13, 0U}, /* 制氧机，急停时强制关断。 */
  [AUTO_OUTPUT_AIR_FRESHENER] = {GPIOB, GPIO_PIN_14, 0U}, /* 空气清新剂。 */
  [AUTO_OUTPUT_MANUAL_CLEAN_VALVE] = {GPIOB, GPIO_PIN_15, 0U}, /* 手动清洁阀。 */
  [AUTO_OUTPUT_EXHAUST_UV_LAMP] = {GPIOD, GPIO_PIN_8, 0U}, /* 排风紫外线灯。 */
  [AUTO_OUTPUT_WATER_HEATER] = {GPIOD, GPIO_PIN_9, 0U}, /* 热水器。 */
  [AUTO_OUTPUT_TAP_WATER_VALVE] = {GPIOD, GPIO_PIN_10, 0U}, /* 自来水阀。 */
  [AUTO_OUTPUT_SCREW_PUMP] = {GPIOD, GPIO_PIN_11, 0U}, /* 投液螺动泵。 */
  [AUTO_OUTPUT_SHOWER_UPPER] = {GPIOD, GPIO_PIN_12, 0U}, /* 淋浴阀上。 */
  [AUTO_OUTPUT_SHOWER_MIDDLE] = {GPIOD, GPIO_PIN_13, 0U}, /* 淋浴阀中。 */
  [AUTO_OUTPUT_SHOWER_LOWER] = {GPIOB, GPIO_PIN_4, 0U}, /* 淋浴阀下。 */
  [AUTO_OUTPUT_SHOWER_REAR] = {GPIOB, GPIO_PIN_5, 0U}, /* 淋浴阀后。 */
  [AUTO_OUTPUT_BATH_LIQUID_VALVE] = {GPIOB, GPIO_PIN_6, 0U}, /* 沐浴液阀。 */
  [AUTO_OUTPUT_FRAGRANCE_VALVE] = {GPIOB, GPIO_PIN_7, 0U}, /* 芬香液阀。 */
  [AUTO_OUTPUT_SOFTENER_VALVE] = {GPIOB, GPIO_PIN_8, 0U}, /* 柔顺液阀。 */
  [AUTO_OUTPUT_DEODORIZER_VALVE] = {GPIOB, GPIO_PIN_9, 0U}, /* 除螨液阀。 */
  [AUTO_OUTPUT_CONDITIONER_VALVE] = {GPIOE, GPIO_PIN_0, 0U}, /* 护毛素阀。 */
  [AUTO_OUTPUT_DISINFECTANT_VALVE] = {GPIOE, GPIO_PIN_1, 0U}, /* 消毒液阀。 */
  [AUTO_OUTPUT_UV_LAMP] = {GPIOB, GPIO_PIN_3, 0U}, /* 紫外线灯。 */
  [AUTO_OUTPUT_SELF_CLEAN_TOP_VALVE] = {GPIOE, GPIO_PIN_2, 0U}, /* 自清洁顶部阀。 */
  [AUTO_OUTPUT_SELF_CLEAN_FRONT_UPPER_VALVE] = {GPIOE, GPIO_PIN_3, 0U}, /* 自清洁前侧上阀。 */
  [AUTO_OUTPUT_SELF_CLEAN_FRONT_LOWER_VALVE] = {GPIOE, GPIO_PIN_4, 0U}, /* 自清洁前侧下阀。 */
  [AUTO_OUTPUT_SELF_CLEAN_REAR_VALVE] = {GPIOE, GPIO_PIN_5, 0U}, /* 自清洁后端阀。 */
  [AUTO_OUTPUT_BYPASS_VALVE] = {GPIOE, GPIO_PIN_6, 0U}, /* 旁通阀。 */
  [AUTO_OUTPUT_OZONE] = {GPIOC, GPIO_PIN_13, 0U}, /* 臭氧发生器。 */
  [AUTO_OUTPUT_LIGHTING] = {GPIOC, GPIO_PIN_14, 1U}, /* 舱内照明，低电平有效，默认开启，急停保持开启。 */
  [AUTO_OUTPUT_FRONT_DOOR_LIGHT] = {GPIOC, GPIO_PIN_15, 0U}, /* 正门灯带，急停时强制关断。 */
  [AUTO_OUTPUT_LOGO_LIGHT] = {GPIOF, GPIO_PIN_0, 0U}, /* LOGO发光板，急停时强制关断。 */
  [AUTO_OUTPUT_FRONT_DOOR_HEATER] = {GPIOF, GPIO_PIN_1, 0U}, /* 正门加热。 */
  [AUTO_OUTPUT_DOOR_CLOSE] = {GPIOF, GPIO_PIN_2, 0U}, /* 自动关门输出。 */
  [AUTO_OUTPUT_DOOR_STOP] = {GPIOF, GPIO_PIN_3, 0U}, /* 关门停止输出。 */
  [AUTO_OUTPUT_UPPER_LOCK] = {GPIOF, GPIO_PIN_4, 0U}, /* 上门电磁锁。 */
  [AUTO_OUTPUT_LOWER_LOCK] = {GPIOF, GPIO_PIN_5, 0U}, /* 下门电磁锁。 */
  [AUTO_OUTPUT_LEFT_LOCK] = {GPIOF, GPIO_PIN_6, 0U}, /* 左电插锁。 */
  [AUTO_OUTPUT_RIGHT_LOCK] = {GPIOF, GPIO_PIN_7, 0U}, /* 右电插锁。 */
  [AUTO_OUTPUT_REAR_LOCK_1] = {GPIOF, GPIO_PIN_8, 0U}, /* 后电插锁1。 */
  [AUTO_OUTPUT_REAR_LOCK_2] = {GPIOF, GPIO_PIN_9, 0U}, /* 后电插锁2。 */
  [AUTO_OUTPUT_LOWER_DOOR_LIGHT] = {GPIOF, GPIO_PIN_10, 0U}, /* 下门灯板，急停时强制关断。 */
  [AUTO_OUTPUT_STANDBY2_VALVE] = {GPIOC, GPIO_PIN_0, 0U}, /* 热水器阀。 */
  [AUTO_OUTPUT_MAIN_POWER] = {GPIOC, GPIO_PIN_1, 0U}, /* 主板电源控制。 */
};

/* RTOS对象句柄，仅由本模块创建和使用。 */
static osThreadId_t output_task_handle;
static osMessageQueueId_t output_queue;
/* 0=采用manual_request；1=采用auto_request。上电初始化为手动。 */
static volatile uint8_t output_auto_mode;
static volatile uint8_t requested_auto_mode;
static volatile uint8_t dangerous_off_requested;
static uint8_t tank_high_level_protect_latched;
static uint32_t tank_high_level_reached_tick;
/* 自动流程对38个点提出的最新逻辑请求。 */
static uint8_t auto_request[AUTO_OUTPUT_COUNT];
/* 上位机或本地手动操作对38个点提出的最新逻辑请求。 */
static uint8_t manual_request[AUTO_OUTPUT_COUNT];
/* 经过模式选择与全部互锁后，真正写到硬件的逻辑状态。 */
static volatile uint8_t actual_output[AUTO_OUTPUT_COUNT];
/* 防止main和输出线程重复初始化GPIO。 */
static uint8_t hardware_initialized;

static const osThreadAttr_t output_task_attributes = {
  .name = "output",
  .stack_size = OUTPUT_TASK_STACK * 4U,
  .priority = OUTPUT_TASK_PRIORITY,
};

/* 打开输出点涉及的GPIOB~GPIOG外设时钟。 */
#if (APP_DRY_RUN_ENABLE == 0U)
static void enable_gpio_clocks(void)
{
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();
}

static GPIO_PinState output_pin_state(AutoOutputPoint point, uint8_t enabled)
{
  (void)point;
  return (enabled != 0U) ? GPIO_PIN_RESET : GPIO_PIN_SET;
}
#endif

/*
 * 初始化全部输出硬件。
 * 可在RTOS启动前调用；函数可重复调用，但只有第一次真正配置GPIO。
 */
void TaskOutput_InitHardware(void)
{
#if (APP_DRY_RUN_ENABLE != 0U)
  actual_output[AUTO_OUTPUT_LIGHTING] = 1U;
  hardware_initialized = 1U;
  return;
#else
  GPIO_InitTypeDef init = {0};
  AutoOutputPoint point;

  if (hardware_initialized != 0U)
  {
    return;
  }

  enable_gpio_clocks();
  for (point = AUTO_OUTPUT_EXHAUST_FAN; point < AUTO_OUTPUT_COUNT; ++point)
  {
    const OutputHw *hw = &output_hw_table[point];
    uint8_t initial_target = (point == AUTO_OUTPUT_LIGHTING) ? 1U : 0U;

    /* 先写目标电平再切输出，避免初始化瞬间出现反向脉冲。 */
    HAL_GPIO_WritePin(hw->port, hw->pin, output_pin_state(point, initial_target));
    init.Pin = hw->pin;
    init.Mode = GPIO_MODE_OUTPUT_PP;
    init.Pull = GPIO_NOPULL;
    init.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(hw->port, &init);
    actual_output[point] = initial_target;
  }

  hardware_initialized = 1U;
#endif
}

/* 检查枚举值是否落在有效输出点范围内。 */
static uint8_t point_is_valid(AutoOutputPoint point)
{
  return ((point >= AUTO_OUTPUT_EXHAUST_FAN) &&
          (point < AUTO_OUTPUT_COUNT)) ? 1U : 0U;
}

static uint8_t output_is_dangerous(AutoOutputPoint point)
{
  switch (point)
  {
    case AUTO_OUTPUT_EXHAUST_FAN:
    case AUTO_OUTPUT_WATER_HEATER:
    case AUTO_OUTPUT_LIGHTING:
    case AUTO_OUTPUT_FRONT_DOOR_LIGHT:
    case AUTO_OUTPUT_LOGO_LIGHT:
    case AUTO_OUTPUT_UPPER_LOCK:
    case AUTO_OUTPUT_LOWER_LOCK:
    case AUTO_OUTPUT_LEFT_LOCK:
    case AUTO_OUTPUT_RIGHT_LOCK:
    case AUTO_OUTPUT_REAR_LOCK_1:
    case AUTO_OUTPUT_REAR_LOCK_2:
    case AUTO_OUTPUT_LOWER_DOOR_LIGHT:
    case AUTO_OUTPUT_MAIN_POWER:
      return 0U;

    default:
      return 1U;
  }
}

uint8_t TaskOutput_HasDangerousOutputActive(void)
{
  AutoOutputPoint point;

  for (point = AUTO_OUTPUT_EXHAUST_FAN; point < AUTO_OUTPUT_COUNT; ++point)
  {
    if ((output_is_dangerous(point) != 0U) &&
        (actual_output[point] != 0U))
    {
      return 1U;
    }
  }
  return 0U;
}

/* 自动模式下这些水路、药液和处理设备只允许在上下门都关闭后工作。 */
static uint8_t output_requires_closed_doors(AutoOutputPoint point)
{
  switch (point)
  {
    case AUTO_OUTPUT_SHOWER_UPPER:
    case AUTO_OUTPUT_SHOWER_MIDDLE:
    case AUTO_OUTPUT_SHOWER_LOWER:
    case AUTO_OUTPUT_SHOWER_REAR:
    case AUTO_OUTPUT_BATH_LIQUID_VALVE:
    case AUTO_OUTPUT_FRAGRANCE_VALVE:
    case AUTO_OUTPUT_SOFTENER_VALVE:
    case AUTO_OUTPUT_DEODORIZER_VALVE:
    case AUTO_OUTPUT_CONDITIONER_VALVE:
    case AUTO_OUTPUT_DISINFECTANT_VALVE:
    case AUTO_OUTPUT_UV_LAMP:
    case AUTO_OUTPUT_EXHAUST_UV_LAMP:
    case AUTO_OUTPUT_SCREW_PUMP:
    case AUTO_OUTPUT_SELF_CLEAN_TOP_VALVE:
    case AUTO_OUTPUT_SELF_CLEAN_FRONT_UPPER_VALVE:
    case AUTO_OUTPUT_SELF_CLEAN_FRONT_LOWER_VALVE:
    case AUTO_OUTPUT_SELF_CLEAN_REAR_VALVE:
    case AUTO_OUTPUT_OZONE:
      return 1U;

    default:
      return 0U;
  }
}

/*
 * 最终安全互锁入口。
 * 返回1表示允许使用请求状态，返回0表示本周期必须强制关闭该输出。
 */
static uint8_t tank_high_level_protect_active(void)
{
  /*
   * 蓄水箱液位为反逻辑：
   * 0 = 已到高液位，保持15秒后必须停止继续进水；
   * 1 = 未到高液位。
   */
  if (AutoDevice_ReadInput(AUTO_INPUT_TANK_HIGH) != 0U)
  {
    tank_high_level_reached_tick = 0U;
    return 0U;
  }

  if (tank_high_level_reached_tick == 0U)
  {
    tank_high_level_reached_tick = HAL_GetTick();
    return 0U;
  }

  return ((HAL_GetTick() - tank_high_level_reached_tick) >=
          OUTPUT_TANK_HIGH_STOP_DELAY_MS) ? 1U : 0U;
}

static uint8_t output_interlock_allow(AutoOutputPoint point, uint8_t enabled)
{
  if (enabled == 0U)
  {
    return 1U;
  }

  /* 急停时，所有仍由程序控制的输出均强制关断。 */
  if (AutoDevice_IsEmergencyStopped() != 0U)
  {
    return (output_hw_table[point].keep_on_during_emergency != 0U) ? 1U : 0U;
  }

  /* 手动模式允许逐点调试；自动模式的危险工艺输出必须满足门到位。 */
  if (((point == AUTO_OUTPUT_TAP_WATER_VALVE) ||
       (point == AUTO_OUTPUT_STANDBY2_VALVE)) &&
      (tank_high_level_protect_active() != 0U))
  {
    return 0U;
  }

  if ((output_auto_mode != 0U) &&
      (output_requires_closed_doors(point) != 0U) &&
      ((AutoDevice_ReadInput(AUTO_INPUT_UPPER_DOOR_CLOSED) == 0U) ||
       (AutoDevice_ReadInput(AUTO_INPUT_LOWER_DOOR_CLOSED) == 0U)))
  {
    return 0U;
  }

  /* 关门过程中光幕被遮挡或门已到位，立即撤销关门机构输出。 */
  if ((point == AUTO_OUTPUT_DOOR_CLOSE) &&
      ((AutoDevice_ReadInput(AUTO_INPUT_UPPER_LIGHT_CURTAIN) != 0U) ||
       (AutoDevice_ReadInput(AUTO_INPUT_LOWER_LIGHT_CURTAIN) != 0U) ||
       ((AutoDevice_ReadInput(AUTO_INPUT_UPPER_DOOR_CLOSED) != 0U) &&
        (AutoDevice_ReadInput(AUTO_INPUT_LOWER_DOOR_CLOSED) != 0U))))
  {
    return 0U;
  }

  return 1U;
}

/* 根据当前模式从自动请求或手动请求中选出目标状态。 */
static uint8_t output_decide_target(AutoOutputPoint point)
{
  /* 急停：舱内照明等明确允许保持的点继续开启，其余点强制关闭。 */
  if (AutoDevice_IsEmergencyStopped() != 0U)
  {
    return (output_hw_table[point].keep_on_during_emergency != 0U) ? 1U : 0U;
  }

  /*
   * OUT6 是热水器继电器，热水器本体需要保持上电待机。
   * 是否真正加热由水流和热水器电磁阀共同决定。
   * 急停时由上方分支直接返回 0（不做保持）。
   */
  if (point == AUTO_OUTPUT_WATER_HEATER)
  {
    return 1U;
  }

  return (output_auto_mode != 0U) ? auto_request[point] : manual_request[point];
}

/*
 * 计算并执行一个输出点。
 * 本函数是运行期间普通业务GPIO唯一的HAL_GPIO_WritePin出口。
 */
static void output_apply(AutoOutputPoint point)
{
  uint8_t target;
  uint8_t previous;
  const OutputHw *hw;

  target = output_decide_target(point);
  previous = actual_output[point];
  hw = &output_hw_table[point];

  if (output_interlock_allow(point, target) == 0U)
  {
    target = 0U;
  }

  actual_output[point] = target;
  if ((TaskComm485_IsRs485ActuatorMode() != 0U) &&
      (point == AUTO_OUTPUT_EXHAUST_FAN))
  {
    if (target != previous)
    {
      (void)TaskComm485_RequestSetPercent(RS485_STATION_TOP_EXHAUST_FAN,
                                          (target != 0U) ? 100U : 0U);
    }
    return;
  }
#if (APP_DRY_RUN_ENABLE != 0U)
  (void)hw;
  return;
#else
  HAL_GPIO_WritePin(hw->port,
                    hw->pin,
                    output_pin_state(point, target));
#endif
}

/* 重新检查并刷新全部输出点，急停等输入变化无需等待新输出命令。 */
static void output_apply_all(void)
{
  AutoOutputPoint point;

  /* 急停直接把输出仲裁层退回手动空闲态，不能停留在自动模式。 */
  if (AutoDevice_IsEmergencyStopped() != 0U)
  {
    output_auto_mode = 0U;
    requested_auto_mode = 0U;
  }

  if (dangerous_off_requested != 0U)
  {
    for (point = AUTO_OUTPUT_EXHAUST_FAN; point < AUTO_OUTPUT_COUNT; ++point)
    {
      if (output_is_dangerous(point) != 0U)
      {
        auto_request[point] = 0U;
        manual_request[point] = 0U;
      }
    }
    dangerous_off_requested = 0U;
  }

  if (tank_high_level_protect_active() != 0U)
  {
    auto_request[AUTO_OUTPUT_TAP_WATER_VALVE] = 0U;
    manual_request[AUTO_OUTPUT_TAP_WATER_VALVE] = 0U;
    auto_request[AUTO_OUTPUT_STANDBY2_VALVE] = 0U;
    manual_request[AUTO_OUTPUT_STANDBY2_VALVE] = 0U;

    if (tank_high_level_protect_latched == 0U)
    {
      (void)TaskComm485_RequestSetProportionalValveAngle(0U);
      tank_high_level_protect_latched = 1U;
    }
  }
  else
  {
    tank_high_level_protect_latched = 0U;
  }

  for (point = AUTO_OUTPUT_EXHAUST_FAN; point < AUTO_OUTPUT_COUNT; ++point)
  {
    /* 急停出现后清除危险请求，急停释放时不会自动恢复动作。 */
    if ((AutoDevice_IsEmergencyStopped() != 0U) &&
        (point != AUTO_OUTPUT_LIGHTING))
    {
      auto_request[point] = 0U;
      manual_request[point] = 0U;
    }
    if ((output_auto_mode != 0U) &&
        (output_requires_closed_doors(point) != 0U) &&
        ((AutoDevice_ReadInput(AUTO_INPUT_UPPER_DOOR_CLOSED) == 0U) ||
         (AutoDevice_ReadInput(AUTO_INPUT_LOWER_DOOR_CLOSED) == 0U)))
    {
      /* 门打开后撤销自动请求，防止门重新关闭时危险设备自行恢复。 */
      auto_request[point] = 0U;
    }
    output_apply(point);
  }
}

/* 清空指定来源的全部逻辑请求，不直接选择模式。 */
static void output_clear_source(AppOutputSource source)
{
  AutoOutputPoint point;
  uint8_t *request = (source == APP_OUTPUT_SOURCE_MANUAL) ?
                     manual_request : auto_request;

  for (point = AUTO_OUTPUT_EXHAUST_FAN; point < AUTO_OUTPUT_COUNT; ++point)
  {
    request[point] = 0U;
  }
  request[AUTO_OUTPUT_LIGHTING] = 1U;
}

/* 在输出线程上下文中串行执行一条队列命令。 */
static void output_process_command(const OutputCommand *command)
{
  if (command == NULL)
  {
    return;
  }

  switch (command->type)
  {
    case OUTPUT_COMMAND_SET:
      if (point_is_valid(command->point) != 0U)
      {
        if (command->source == APP_OUTPUT_SOURCE_MANUAL)
        {
          manual_request[command->point] = command->enabled;
        }
        else
        {
          auto_request[command->point] = command->enabled;
        }
      }
      break;

    case OUTPUT_COMMAND_SET_MODE:
      if ((command->enabled != 0U) &&
          (AutoDevice_IsEmergencyStopped() != 0U))
      {
        break;
      }
      output_auto_mode = command->enabled;
      requested_auto_mode = output_auto_mode;
      /* 新模式从全关闭开始，防止恢复很早以前遗留的请求。 */
      output_clear_source((output_auto_mode != 0U) ?
                          APP_OUTPUT_SOURCE_AUTO : APP_OUTPUT_SOURCE_MANUAL);
      tank_high_level_reached_tick = 0U;
      tank_high_level_protect_latched = 0U;
      break;

    case OUTPUT_COMMAND_CLEAR_SOURCE:
      output_clear_source(command->source);
      break;

    case OUTPUT_COMMAND_ALL_OFF:
      output_clear_source(APP_OUTPUT_SOURCE_AUTO);
      output_clear_source(APP_OUTPUT_SOURCE_MANUAL);
      tank_high_level_reached_tick = 0U;
      tank_high_level_protect_latched = 0U;
      break;

    case OUTPUT_COMMAND_SYNC:
      break;

    default:
      break;
  }
}

/*
 * 非阻塞投递内部命令。
 * HAL_OK=已进入队列；HAL_BUSY=队列已满；HAL_ERROR=队列尚未创建或参数为空。
 */
static HAL_StatusTypeDef output_queue_command(const OutputCommand *command)
{
  if ((output_queue == NULL) || (command == NULL))
  {
    return HAL_ERROR;
  }
  return (osMessageQueuePut(output_queue, command, 0U, 0U) == osOK) ?
         HAL_OK : HAL_BUSY;
}

/*
 * 异步切换输出模式：0=手动，非0=自动。
 * 新模式对应的旧请求会被清空，因此模式切换后不会恢复历史动作。
 */
void TaskOutput_SetAutoMode(uint8_t auto_mode)
{
  OutputCommand command = {0};

  /* 急停期间不接受切入自动模式。 */
  if ((auto_mode != 0U) && (AutoDevice_IsEmergencyStopped() != 0U))
  {
    return;
  }

  command.type = OUTPUT_COMMAND_SET_MODE;
  command.enabled = (auto_mode != 0U) ? 1U : 0U;
  if (output_queue_command(&command) == HAL_OK)
  {
    requested_auto_mode = command.enabled;
  }
}

/* 返回当前已经由输出线程生效的模式。 */
uint8_t TaskOutput_IsAutoMode(void)
{
  return requested_auto_mode;
}

/*
 * 提交单点逻辑输出请求。
 * source决定请求写入自动数组还是手动数组；只有当前模式对应的来源能作用于硬件。
 * 本接口只负责入队，返回HAL_OK并不表示GPIO已经完成翻转。
 */
HAL_StatusTypeDef TaskOutput_Request(AppOutputSource source,
                                     AutoOutputPoint point,
                                     uint8_t enabled)
{
  OutputCommand command = {0};

  if ((point_is_valid(point) == 0U) ||
      ((source != APP_OUTPUT_SOURCE_AUTO) &&
       (source != APP_OUTPUT_SOURCE_MANUAL)))
  {
    return HAL_ERROR;
  }

  /* 急停时只允许关闭输出，任何来源的开启请求都被拒绝。 */
  if ((enabled != 0U) && (AutoDevice_IsEmergencyStopped() != 0U))
  {
    return HAL_BUSY;
  }

  command.type = OUTPUT_COMMAND_SET;
  command.source = source;
  command.point = point;
  command.enabled = (enabled != 0U) ? 1U : 0U;
  return output_queue_command(&command);
}

/* 读取互锁处理后的实际逻辑状态；无效点位固定返回0。 */
uint8_t TaskOutput_GetActual(AutoOutputPoint point)
{
  return (point_is_valid(point) != 0U) ? actual_output[point] : 0U;
}

/* 异步清空自动来源或手动来源的全部输出请求。 */
void TaskOutput_ClearSource(AppOutputSource source)
{
  OutputCommand command = {0};
  command.type = OUTPUT_COMMAND_CLEAR_SOURCE;
  command.source = source;
  (void)output_queue_command(&command);
}

HAL_StatusTypeDef TaskOutput_Sync(uint32_t timeout_ms)
{
  OutputCommand command = {0};
  osThreadId_t caller = osThreadGetId();
  uint32_t flags;

  if ((caller == NULL) || (output_queue == NULL))
  {
    return HAL_ERROR;
  }

  if (caller == output_task_handle)
  {
    output_apply_all();
    return HAL_OK;
  }

  (void)osThreadFlagsClear(OUTPUT_SYNC_FLAG);
  command.type = OUTPUT_COMMAND_SYNC;
  command.sync_thread = caller;
  command.sync_flag = OUTPUT_SYNC_FLAG;

  if (output_queue_command(&command) != HAL_OK)
  {
    return HAL_BUSY;
  }

  flags = osThreadFlagsWait(OUTPUT_SYNC_FLAG,
                            osFlagsWaitAny,
                            timeout_ms);
  return ((flags & OUTPUT_SYNC_FLAG) != 0U) ? HAL_OK : HAL_BUSY;
}

HAL_StatusTypeDef TaskOutput_ClearSourceSync(AppOutputSource source,
                                             uint32_t timeout_ms)
{
  OutputCommand command = {0};

  command.type = OUTPUT_COMMAND_CLEAR_SOURCE;
  command.source = source;
  if (output_queue_command(&command) != HAL_OK)
  {
    return HAL_BUSY;
  }

  return TaskOutput_Sync(timeout_ms);
}

/* 异步清空两套请求，下一次输出线程运行时全部回到关闭状态。 */
void TaskOutput_AllOff(void)
{
  OutputCommand command = {0};
  command.type = OUTPUT_COMMAND_ALL_OFF;
  (void)output_queue_command(&command);
}

void TaskOutput_DangerousOff(void)
{
  dangerous_off_requested = 1U;
}

/*
 * 输出线程入口：
 * 1. 上电默认手动且两套请求全关闭；
 * 2. 周期更新输入消抖；
 * 3. 接收并处理一条输出命令；
 * 4. 每10ms重新应用全部安全互锁和GPIO状态。
 */
static void OutputTask(void *argument)
{
  OutputCommand command;
  uint8_t command_received;
  osThreadId_t sync_thread;
  uint32_t sync_flag;
  (void)argument;

  TaskOutput_InitHardware();
  output_auto_mode = 0U;
  requested_auto_mode = 0U;
  dangerous_off_requested = 0U;
  tank_high_level_protect_latched = 0U;
  tank_high_level_reached_tick = 0U;
  output_clear_source(APP_OUTPUT_SOURCE_AUTO);
  output_clear_source(APP_OUTPUT_SOURCE_MANUAL);
  auto_request[AUTO_OUTPUT_LIGHTING] = 1U;
  manual_request[AUTO_OUTPUT_LIGHTING] = 1U;
  auto_request[AUTO_OUTPUT_UPPER_LOCK] = 1U;
  auto_request[AUTO_OUTPUT_LOWER_LOCK] = 1U;
  output_apply_all();

  for (;;)
  {
    TaskWatchdog_Heartbeat(TASK_WATCHDOG_OUTPUT);
    AutoDevice_ProcessInputs();
    command_received = 0U;
    sync_thread = NULL;
    sync_flag = 0U;
    if (osMessageQueueGet(output_queue,
                          &command,
                          NULL,
                          OUTPUT_RECHECK_MS) == osOK)
    {
      command_received = 1U;
      if (command.type == OUTPUT_COMMAND_SYNC)
      {
        sync_thread = command.sync_thread;
        sync_flag = command.sync_flag;
      }
      output_process_command(&command);
    }
    output_apply_all();
    if ((command_received != 0U) &&
        (sync_thread != NULL) &&
        (sync_flag != 0U))
    {
      (void)osThreadFlagsSet(sync_thread, sync_flag);
    }
  }
}

/* 创建输出消息队列和输出线程，由AppTasks_Create()在调度器启动前调用。 */
void TaskOutput_Create(void)
{
  TaskOutput_InitHardware();
  output_auto_mode = 0U;
  requested_auto_mode = 0U;
  dangerous_off_requested = 0U;
  tank_high_level_protect_latched = 0U;
  tank_high_level_reached_tick = 0U;
  auto_request[AUTO_OUTPUT_LIGHTING] = 1U;
  manual_request[AUTO_OUTPUT_LIGHTING] = 1U;
  auto_request[AUTO_OUTPUT_UPPER_LOCK] = 1U;
  auto_request[AUTO_OUTPUT_LOWER_LOCK] = 1U;
  output_queue = osMessageQueueNew(OUTPUT_QUEUE_LENGTH,
                                   sizeof(OutputCommand),
                                   NULL);
  output_task_handle = osThreadNew(OutputTask, NULL, &output_task_attributes);
  (void)output_task_handle;
}
