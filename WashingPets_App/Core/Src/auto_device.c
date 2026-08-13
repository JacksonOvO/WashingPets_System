#include "auto_device.h"

#include <string.h>
#include "app_param.h"
#include "task_output.h"
#include "rs485_config.h"
#include "task_comm485.h"

#define AUTO_INPUT_DEBOUNCE_MS       30U
#define AUTO_LEVEL_INPUT_FILTER_MS 3000U

typedef struct
{
  GPIO_TypeDef *port;
  uint16_t pin;
} AutoGpio;

/*
 * 输入映射表。
 * 消毒液低液位使用PA3。
 * 其余点位严格按照最新输入表。所有输入均为低电平有效并使用内部上拉。
 */
static const AutoGpio input_table[AUTO_INPUT_COUNT] = {
  [AUTO_INPUT_DISINFECTANT_LOW] = {GPIOA, GPIO_PIN_3},
  [AUTO_INPUT_BATH_LIQUID_LOW] = {GPIOA, GPIO_PIN_4},
  [AUTO_INPUT_FRAGRANCE_LOW] = {GPIOA, GPIO_PIN_5},
  [AUTO_INPUT_SOFTENER_LOW] = {GPIOA, GPIO_PIN_6},
  [AUTO_INPUT_DEODORIZER_LOW] = {GPIOA, GPIO_PIN_7},
  [AUTO_INPUT_CONDITIONER_LOW] = {GPIOC, GPIO_PIN_4},
  [AUTO_INPUT_TANK_LOW] = {GPIOC, GPIO_PIN_5},
  [AUTO_INPUT_TANK_MIDDLE] = {GPIOB, GPIO_PIN_0},
  [AUTO_INPUT_TANK_HIGH] = {GPIOB, GPIO_PIN_1},
  [AUTO_INPUT_EMERGENCY_STOP] = {GPIOB, GPIO_PIN_2},
  [AUTO_INPUT_DRAIN_PIPE_LEVEL] = {GPIOF, GPIO_PIN_12},
  [AUTO_INPUT_UPPER_LIGHT_CURTAIN] = {GPIOF, GPIO_PIN_13},
  [AUTO_INPUT_LOWER_LIGHT_CURTAIN] = {GPIOF, GPIO_PIN_14},
  [AUTO_INPUT_UPPER_DOOR_CLOSED] = {GPIOF, GPIO_PIN_15},
  [AUTO_INPUT_LOWER_DOOR_CLOSED] = {GPIOG, GPIO_PIN_0},
  [AUTO_INPUT_LEFT_LOCK_CLOSED] = {GPIOG, GPIO_PIN_1},
  [AUTO_INPUT_RIGHT_LOCK_CLOSED] = {GPIOE, GPIO_PIN_7},
  [AUTO_INPUT_REAR_LOCK_1_CLOSED] = {GPIOE, GPIO_PIN_8},
  [AUTO_INPUT_REAR_LOCK_2_CLOSED] = {GPIOE, GPIO_PIN_9},
};

static AutoSensorData sensor_data;
static uint32_t sensor_lock_primask;
static volatile uint8_t command_flags[AUTO_COMMAND_COUNT];
static volatile uint8_t recipe_selected_flag;
static volatile uint8_t paused_flag;
static volatile uint8_t stop_requested_flag;
static volatile AutoFaultCode last_fault_code;
static uint8_t flow_target_percent;
static uint32_t process_pause_start_tick;
static uint32_t process_paused_total_ms;
static volatile uint8_t input_stable[AUTO_INPUT_COUNT];
static uint8_t input_candidate[AUTO_INPUT_COUNT];
static uint32_t input_change_tick[AUTO_INPUT_COUNT];
#if (APP_DRY_RUN_ENABLE != 0U)
static uint8_t dry_run_upper_door_closed;
static uint8_t dry_run_lower_door_closed;
#endif

/* 所有储液、蓄水箱和排水管液位信号均要求连续稳定3秒后才更新。 */
#if (APP_DRY_RUN_ENABLE == 0U)
static uint8_t is_level_input(AutoInputPoint point)
{
  if ((point >= AUTO_INPUT_DISINFECTANT_LOW) &&
      (point <= AUTO_INPUT_TANK_HIGH))
  {
    return 1U;
  }
  return (point == AUTO_INPUT_DRAIN_PIPE_LEVEL) ? 1U : 0U;
}
#endif

static void sensor_lock(void)
{
  sensor_lock_primask = __get_PRIMASK();
  __disable_irq();
}

static void sensor_unlock(void)
{
  if (sensor_lock_primask == 0U)
  {
    __enable_irq();
  }
}

/* 打开自动设备映射所使用的GPIOA～GPIOG外设时钟；无参数、无返回值。 */
static void enable_all_gpio_clocks(void)
{
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();
}

#if (APP_DRY_RUN_ENABLE != 0U)
static void dry_run_reset_inputs(void)
{
  dry_run_upper_door_closed = 1U;
  dry_run_lower_door_closed = 1U;
}

static uint8_t dry_run_input_value(AutoInputPoint point)
{
  switch (point)
  {
    case AUTO_INPUT_EMERGENCY_STOP:
      return 1U;

    case AUTO_INPUT_UPPER_DOOR_CLOSED:
      return dry_run_upper_door_closed;

    case AUTO_INPUT_LOWER_DOOR_CLOSED:
      return dry_run_lower_door_closed;

    case AUTO_INPUT_LEFT_LOCK_CLOSED:
    case AUTO_INPUT_RIGHT_LOCK_CLOSED:
    case AUTO_INPUT_REAR_LOCK_1_CLOSED:
    case AUTO_INPUT_REAR_LOCK_2_CLOSED:
      return 1U;

    default:
      return 0U;
  }
}

static void dry_run_fill_sensor_data(AutoSensorData *data)
{
  uint32_t now = HAL_GetTick();
  const AppRecipe_t *recipe = AppParam_GetActiveRecipe();
  int16_t tank_x10 = (recipe != NULL) ? recipe->tank_target_temp_x10 : 360;

  memset(data, 0, sizeof(*data));
  data->inlet_temperature_x10 = tank_x10;
  data->tank_temperature_x10 = tank_x10;
  data->pipe_temperature_x10 = tank_x10;
  data->spare_temperature_x10 = tank_x10;
  data->inlet_update_tick = now;
  data->tank_update_tick = now;
  data->pipe_update_tick = now;
  data->spare_update_tick = now;
  data->inlet_temperature_valid = 1U;
  data->tank_temperature_valid = 1U;
  data->pipe_temperature_valid = 1U;
  data->spare_temperature_valid = 1U;
}

static void dry_run_apply_command(AutoCommand command)
{
  switch (command)
  {
    case AUTO_COMMAND_OPEN_UPPER_DOOR:
      dry_run_upper_door_closed = 0U;
      break;

    case AUTO_COMMAND_OPEN_LOWER_DOOR:
      dry_run_lower_door_closed = 0U;
      break;

    case AUTO_COMMAND_CLOSE_UPPER_DOOR:
      dry_run_upper_door_closed = 1U;
      break;

    case AUTO_COMMAND_CLOSE_LOWER_DOOR:
      dry_run_lower_door_closed = 1U;
      break;

    case AUTO_COMMAND_AUTO_CLOSE_DOOR:
      dry_run_upper_door_closed = 1U;
      dry_run_lower_door_closed = 1U;
      break;

    default:
      break;
  }
}
#endif

/*
 * 初始化自动工艺输入GPIO和运行缓存。
 * 输出GPIO由task_output.c统一初始化和控制，本模块不再直接写输出引脚。
 */
void AutoDevice_Init(void)
{
  GPIO_InitTypeDef init = {0};
  uint16_t index;

  enable_all_gpio_clocks();

  /* 低电平有效输入使用内部上拉，断线或未接入时默认保持“未触发”。 */
  for (index = 0U; index < AUTO_INPUT_COUNT; ++index)
  {
    init.Pin = input_table[index].pin;
    init.Mode = GPIO_MODE_INPUT;
    init.Pull = GPIO_PULLUP;
    init.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(input_table[index].port, &init);
    input_stable[index] = (HAL_GPIO_ReadPin(input_table[index].port,
                                            input_table[index].pin) == GPIO_PIN_RESET) ? 1U : 0U;
    input_candidate[index] = input_stable[index];
    input_change_tick[index] = HAL_GetTick();
  }

  memset(&sensor_data, 0, sizeof(sensor_data));
  sensor_lock_primask = 0U;
  memset((void *)command_flags, 0, sizeof(command_flags));
  recipe_selected_flag = 0U;
  paused_flag = 0U;
  stop_requested_flag = 0U;
  process_pause_start_tick = 0U;
  process_paused_total_ms = 0U;
  last_fault_code = AUTO_FAULT_NONE;
  flow_target_percent = 0U;
#if (APP_DRY_RUN_ENABLE != 0U)
  dry_run_reset_inputs();
  dry_run_fill_sensor_data(&sensor_data);
#endif
}

/*
 * 读取一个低电平有效输入点。
 * point为逻辑输入编号；返回1表示信号触发，返回0表示未触发或编号非法。
 */
uint8_t AutoDevice_ReadInputRaw(AutoInputPoint point)
{
  if (point >= AUTO_INPUT_COUNT)
  {
    return 0U;
  }
#if (APP_DRY_RUN_ENABLE != 0U)
  return dry_run_input_value(point);
#else
  return (HAL_GPIO_ReadPin(input_table[point].port,
                           input_table[point].pin) == GPIO_PIN_RESET) ? 1U : 0U;
#endif
}

/* 每10ms采样一次：液位连续稳定3秒后更新，其他输入连续稳定30ms后更新。 */
void AutoDevice_ProcessInputs(void)
{
  AutoInputPoint point;
  uint32_t now = HAL_GetTick();

  for (point = AUTO_INPUT_DISINFECTANT_LOW; point < AUTO_INPUT_COUNT; ++point)
  {
#if (APP_DRY_RUN_ENABLE != 0U)
    input_stable[point] = dry_run_input_value(point);
    input_candidate[point] = input_stable[point];
    input_change_tick[point] = now;
#else
    uint8_t raw = AutoDevice_ReadInputRaw(point);
    uint32_t filter_ms = (is_level_input(point) != 0U) ?
                         AUTO_LEVEL_INPUT_FILTER_MS :
                         AUTO_INPUT_DEBOUNCE_MS;

    if (raw != input_candidate[point])
    {
      input_candidate[point] = raw;
      input_change_tick[point] = now;
    }
    else if ((input_stable[point] != raw) &&
             ((now - input_change_tick[point]) >= filter_ms))
    {
      input_stable[point] = raw;
    }
#endif
  }
}

uint8_t AutoDevice_ReadInput(AutoInputPoint point)
{
#if (APP_DRY_RUN_ENABLE != 0U)
  return (point < AUTO_INPUT_COUNT) ? dry_run_input_value(point) : 0U;
#else
  return (point < AUTO_INPUT_COUNT) ? input_stable[point] : 0U;
#endif
}

/*
 * 自动流程提交一个逻辑输出请求。真正的GPIO写入、手自动仲裁和急停互锁
 * 全部由task_output.c完成，本函数绝不直接操作GPIO。
 */
HAL_StatusTypeDef AutoDevice_SetOutput(AutoOutputPoint point, uint8_t enabled)
{
  if (point >= AUTO_OUTPUT_COUNT)
  {
    return HAL_ERROR;
  }
  return TaskOutput_Request(APP_OUTPUT_SOURCE_AUTO, point, enabled);
}

/* 同时开关上、中、下、后四路淋浴阀，确保三个洗护流程采用一致水路。 */
void AutoDevice_SetAllShowerValves(uint8_t enabled)
{
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_SHOWER_UPPER, enabled);
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_SHOWER_MIDDLE, enabled);
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_SHOWER_LOWER, enabled);
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_SHOWER_REAR, enabled);
}

HAL_StatusTypeDef AutoDevice_SetBoosterPumpPercent(uint8_t percent)
{
  return TaskComm485_RequestSetBoosterPumpPercent(percent);
}

HAL_StatusTypeDef AutoDevice_SetBoosterPumpRunning(uint8_t enabled)
{
  return TaskOutput_Request(APP_OUTPUT_SOURCE_AUTO,
                            AUTO_OUTPUT_BOOSTER_PUMP_RUN,
                            enabled);
}

/*
 * 执行水路危险输出的统一安全关闭。
 * 关闭喷淋、加热、泵、投液和阀门，并把RS485流量阀目标降为0%。
 */
void AutoDevice_AllDangerousOutputsOff(void)
{
  /*
   * GPIO危险输出由输出线程在下一个10ms周期统一撤销，避免逐点消息
   * 塞满队列；485先清掉旧命令，再优先发送三路加热/风机关闭。
   */
  TaskOutput_DangerousOff();
  TaskComm485_RequestSafeStop();
  (void)AutoDevice_SetFlowPercent(0U);
}

/* 更新水箱温度缓存；value单位0.1℃，valid表示本次数据是否可信。 */
void AutoDevice_UpdateTankTemperature(int16_t value, uint8_t valid)
{
  sensor_lock();
  sensor_data.tank_temperature_x10 = (valid != 0U) ? value : (int16_t)0xFFFFU;
  sensor_data.tank_temperature_valid = (valid != 0U) ? 1U : 0U;
  sensor_data.tank_update_tick = HAL_GetTick();
  sensor_unlock();
}

/* 更新管路温度缓存，并记录本次更新时间，供后续驱动和超时判断使用。 */
void AutoDevice_UpdatePipeTemperature(int16_t value, uint8_t valid)
{
  sensor_lock();
  sensor_data.pipe_temperature_x10 = (valid != 0U) ? value : (int16_t)0xFFFFU;
  sensor_data.pipe_temperature_valid = (valid != 0U) ? 1U : 0U;
  sensor_data.pipe_update_tick = HAL_GetTick();
  sensor_unlock();
}

void AutoDevice_UpdateTemperatureChannels(const int16_t value[4],
                                          const uint8_t valid[4])
{
  uint32_t now;

  if ((value == NULL) || (valid == NULL))
  {
    return;
  }
  now = HAL_GetTick();
  sensor_lock();
  if (valid[0] != 0U)
  {
    sensor_data.inlet_temperature_x10 = value[0];
  }
  if (valid[1] != 0U)
  {
    sensor_data.tank_temperature_x10 = value[1];
  }
  if (valid[2] != 0U)
  {
    sensor_data.pipe_temperature_x10 = value[2];
  }
  if (valid[3] != 0U)
  {
    sensor_data.spare_temperature_x10 = value[3];
  }
  sensor_data.inlet_temperature_valid = (valid[0] != 0U) ? 1U : 0U;
  sensor_data.tank_temperature_valid = (valid[1] != 0U) ? 1U : 0U;
  sensor_data.pipe_temperature_valid = (valid[2] != 0U) ? 1U : 0U;
  sensor_data.spare_temperature_valid = (valid[3] != 0U) ? 1U : 0U;
  sensor_data.inlet_update_tick = now;
  sensor_data.tank_update_tick = now;
  sensor_data.pipe_update_tick = now;
  sensor_data.spare_update_tick = now;
  sensor_unlock();
}

void AutoDevice_InvalidateTemperatureChannels(void)
{
  uint32_t now = HAL_GetTick();

  sensor_lock();
  sensor_data.inlet_temperature_valid = 0U;
  sensor_data.tank_temperature_valid = 0U;
  sensor_data.pipe_temperature_valid = 0U;
  sensor_data.spare_temperature_valid = 0U;
  sensor_data.inlet_update_tick = now;
  sensor_data.tank_update_tick = now;
  sensor_data.pipe_update_tick = now;
  sensor_data.spare_update_tick = now;
  sensor_unlock();
}

/* 将温度传感器缓存完整复制给调用者；空指针返回0，复制成功返回1。 */
uint8_t AutoDevice_GetSensorData(AutoSensorData *data)
{
  if (data == NULL)
  {
    return 0U;
  }
#if (APP_DRY_RUN_ENABLE != 0U)
  dry_run_fill_sensor_data(data);
  return 1U;
#else
  sensor_lock();
  *data = sensor_data;
  sensor_unlock();
  return 1U;
#endif
}

/*
 * RS485流量阀底层发送弱函数。
 * 当前未接协议时返回HAL_BUSY，使流程停在设置步骤等待，而不是误认为设置成功。
 */
__weak HAL_StatusTypeDef AutoDevice_FlowValveWritePercent(uint8_t percent)
{
  /* 预留：后续在RS485流量控制阀驱动中重写本函数。 */
  (void)percent;
  return HAL_BUSY;
}

/*
 * 设置流量阀目标开度。
 * 输入自动限制到0～100%；只有底层返回HAL_OK后才更新软件目标缓存。
 */
HAL_StatusTypeDef AutoDevice_SetFlowPercent(uint8_t percent)
{
  if (percent > 100U)
  {
    percent = 100U;
  }

#if (AUTO_FLOW_VALVE_ENABLE == 0U)
  /*
   * 当前设备未安装流量阀。只更新软件目标值，让自动流程立即通过，
   * 不调用底层RS485驱动，也不会因为弱函数返回HAL_BUSY而卡住。
   */
  flow_target_percent = percent;
  return HAL_OK;
#else
  HAL_StatusTypeDef status;

  status = AutoDevice_FlowValveWritePercent(percent);
  if (status == HAL_OK)
  {
    flow_target_percent = percent;
  }
  return status;
#endif
}

/* 返回最近一次底层确认设置成功的流量阀目标百分比。 */
uint8_t AutoDevice_GetFlowTargetPercent(void)
{
  return flow_target_percent;
}

/* 由上位机解析层置位一条流程命令；非法编号直接忽略。 */
void AutoDevice_SetCommand(AutoCommand command)
{
  if (command < AUTO_COMMAND_COUNT)
  {
    command_flags[command] = 1U;
#if (APP_DRY_RUN_ENABLE != 0U)
    dry_run_apply_command(command);
#endif
  }
}

/*
 * 读取并清除一条流程命令。
 * 返回1表示本次成功消费命令，确保同一条命令不会被流程重复执行。
 */
uint8_t AutoDevice_ConsumeCommand(AutoCommand command)
{
  if (command >= AUTO_COMMAND_COUNT)
  {
    return 0U;
  }

#if (AUTO_DEVICE_HOST_COMMAND_BYPASS != 0U)
  /* 调试旁路开启：清除可能残留的真实命令，并直接报告命令已收到。 */
  command_flags[command] = 0U;
  return 1U;
#else
  uint8_t result;
  result = command_flags[command];
  command_flags[command] = 0U;
  return result;
#endif
}

void AutoDevice_SetRecipeSelectedFlag(void)
{
  recipe_selected_flag = 1U;
}

uint8_t AutoDevice_ConsumeRecipeSelectedFlag(void)
{
  uint8_t result = recipe_selected_flag;
  recipe_selected_flag = 0U;
  return result;
}

/*
 * 清除一轮流程可能遗留的上位机交互和运行控制信号。
 * 不清除传感器缓存与输入滤波状态，避免返回空闲层后丢失真实设备状态。
 */
void AutoDevice_ClearProcessSignals(void)
{
  AutoCommand command;

  for (command = AUTO_COMMAND_START; command < AUTO_COMMAND_COUNT; ++command)
  {
    command_flags[command] = 0U;
  }
  recipe_selected_flag = 0U;
  paused_flag = 0U;
  stop_requested_flag = 0U;
  flow_target_percent = 0U;
#if (APP_DRY_RUN_ENABLE != 0U)
  dry_run_reset_inputs();
#endif
}

/* 设置自动流程暂停标志，非0统一转换为1。 */
void AutoDevice_SetPaused(uint8_t paused)
{
  uint8_t new_paused = (paused != 0U) ? 1U : 0U;
  uint32_t now = HAL_GetTick();

  if ((new_paused != 0U) && (paused_flag == 0U))
  {
    process_pause_start_tick = now;
  }
  else if ((new_paused == 0U) && (paused_flag != 0U))
  {
    process_paused_total_ms += now - process_pause_start_tick;
    process_pause_start_tick = 0U;
  }
  paused_flag = new_paused;
}

/* 设置自动流程停止请求；停止与暂停分开处理，停止通常会进入安全故障。 */
void AutoDevice_SetStopRequested(uint8_t requested)
{
  stop_requested_flag = (requested != 0U) ? 1U : 0U;
}

/* 返回当前暂停标志。 */
uint8_t AutoDevice_IsPaused(void)
{
  return paused_flag;
}

/* 返回当前停止请求标志。 */
uint8_t AutoDevice_IsStopRequested(void)
{
  return stop_requested_flag;
}

/*
 * PB2急停采用失电安全接法：
 * 正常时外部常闭回路把PB2拉低；急停按下、线路断开或信号丢失时，
 * 内部上拉使PB2变高，此时返回1表示急停。
 */
uint8_t AutoDevice_IsEmergencyStopped(void)
{
#if (APP_DRY_RUN_ENABLE != 0U)
  return 0U;
#else
  return (AutoDevice_ReadInputRaw(AUTO_INPUT_EMERGENCY_STOP) == 0U) ? 1U : 0U;
#endif
}

uint32_t AutoDevice_GetProcessTick(void)
{
  uint32_t now = HAL_GetTick();
  uint32_t paused_ms = process_paused_total_ms;

  if (paused_flag != 0U)
  {
    paused_ms += now - process_pause_start_tick;
  }
  return now - paused_ms;
}

/* 上位机状态上报弱钩子；通信协议接入后由协议模块提供同名强函数覆盖。 */
__weak void AutoDevice_OnReport(AutoReport report)
{
  /* 预留：上位机业务协议完成后重写本函数发送状态帧。 */
  (void)report;
}

__weak void AutoDevice_RequestHostInteraction(uint8_t interaction_type)
{
  (void)interaction_type;
}

/* 校验上报编号后调用协议钩子，防止非法枚举进入通信层。 */
void AutoDevice_Report(AutoReport report)
{
  if (report < AUTO_REPORT_COUNT)
  {
    AutoDevice_OnReport(report);
  }
}

void AutoDevice_SetFault(AutoFaultCode code)
{
  last_fault_code = code;
}

AutoFaultCode AutoDevice_GetFault(void)
{
  return last_fault_code;
}

void AutoDevice_ClearFault(void)
{
  last_fault_code = AUTO_FAULT_NONE;
  TaskComm485_ReleaseNonzero(COMM485_INHIBIT_FAULT);
}

void AutoDevice_ReportFault(AutoFaultCode code, AutoReport report)
{
  AutoDevice_SetFault(code);
  AutoDevice_Report(report);
}
