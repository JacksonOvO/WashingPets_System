#include "task_water_tank.h"

#include "app_param.h"
#include "auto_device.h"
#include "auto_process.h"
#include "cmsis_os.h"
#include "main.h"
#include "task_clean.h"
#include <stdio.h>
#include <string.h>
#include "task_comm485.h"
#include "task_output.h"
#include "task_watchdog.h"

/* 水箱恒温线程配置。 */
#define WATER_TANK_TASK_STACK       768U
#define WATER_TANK_TASK_PRIORITY    osPriorityBelowNormal
#define WATER_TANK_PERIOD_MS        1000U
#define WATER_TANK_LOG_ENABLE       0U

/* 水箱后台温控策略，单位均为 0.1℃。 */
#define WATER_TANK_LOW_START_X10    340   /* 低于 34.0℃ 开始补热水。 */
#define WATER_TANK_HIGH_START_X10   380   /* 高于 38.0℃ 开始补冷水。 */
#define WATER_TANK_STANDBY_TARGET_X10 360 /* 自动模式空闲时，水箱固定待机目标36.0℃。 */
#define WATER_TANK_STANDBY_TOLERANCE_X10 20 /* 自动模式空闲时，水箱固定待机范围36.0℃±2.0℃。 */

/*
 * 水箱水温响应慢，不能按混合水温每秒微调比例阀。
 * 采用水箱温度分段粗调，并限制比例阀最小调节周期。
 */
#define WATER_TANK_HOT_FAST_UNTIL_X10       340  /* 低于34℃：全热水快速升温。 */
#define WATER_TANK_HOT_MEDIUM_UNTIL_X10     350  /* 34~35℃：降低进水温度。 */
#define WATER_TANK_HOT_SLOW_UNTIL_X10       355  /* 35~35.5℃：继续缓慢靠近目标。 */
#define WATER_TANK_MIX_TARGET_NEAR_X10      355  /* 水箱接近目标后，进水箱混合水目标略低一点，避免水箱继续升高。 */
#define WATER_TANK_MIX_TARGET_SLOW_X10      375
#define WATER_TANK_MIX_TARGET_MEDIUM_X10    400
#define WATER_TANK_MIX_TOL_X10                5
#define WATER_TANK_RECIRC_START_X10         330  /* 中液位已到后，低于33.0℃才允许重新启动旁通内循环。 */
#define WATER_TANK_RECIRC_STOP_X10          355  /* 旁通内循环到35.0℃提前停止，避免55℃回水过冲。 */
#define WATER_TANK_VALVE_ADJUST_INTERVAL_MS 30000U

#define WATER_TANK_VALVE_MIN_ANGLE    0U
#define WATER_TANK_VALVE_MAX_ANGLE   90U
#define WATER_TANK_VALVE_MOVE_STEP    3U
#define WATER_TANK_VALVE_UNKNOWN    0xFFU
#define WATER_TANK_HOT_DRAIN_MS    20000U
#define WATER_TANK_HIGH_STOP_DELAY_MS 15000U
#define WATER_TANK_BOOSTER_PRESSURE_X10 50U
#define WATER_TANK_TARGET_UNKNOWN  ((int16_t)-32768)

typedef enum
{
  WATER_TANK_STATE_IDLE = 0,
  WATER_TANK_STATE_HEATING,
  WATER_TANK_STATE_COOLING,
  WATER_TANK_STATE_RECIRC_HEATING,
  WATER_TANK_STATE_HOT_DRAIN
} WaterTankState;

typedef enum
{
  WATER_TANK_LOG_START = 0,
  WATER_TANK_LOG_NOT_AUTO,
  WATER_TANK_LOG_EMERGENCY,
  WATER_TANK_LOG_SENSOR_INVALID,
  WATER_TANK_LOG_MIDDLE_REACHED,
  WATER_TANK_LOG_IDLE_RANGE,
  WATER_TANK_LOG_HEAT,
  WATER_TANK_LOG_COOL,
  WATER_TANK_LOG_STOP_TARGET,
  WATER_TANK_LOG_RECIRC_HEAT,
  WATER_TANK_LOG_HOT_DRAIN
} WaterTankLogReason;

static osThreadId_t water_tank_task_handle;
static uint8_t current_valve_angle = WATER_TANK_VALVE_UNKNOWN;
static WaterTankState water_tank_state;
static uint8_t water_tank_recirc_active;
static uint32_t last_valve_adjust_tick;
static uint32_t hot_drain_start_tick;
static uint32_t high_level_reached_tick;
static uint8_t hot_drain_active;
static volatile uint8_t self_clean_prepare_requested;
static int16_t last_recipe_target_x10 = WATER_TANK_TARGET_UNKNOWN;
static uint8_t previous_auto_mode;
#if (WATER_TANK_LOG_ENABLE != 0U)
static WaterTankLogReason last_log_reason = WATER_TANK_LOG_START;
static int16_t last_log_tank_x10 = INT16_MIN;
static uint8_t last_log_low;
static uint8_t last_log_middle;
static uint8_t last_log_high;
#endif

static const osThreadAttr_t water_tank_task_attributes = {
  .name = "waterTank",
  .stack_size = WATER_TANK_TASK_STACK * 4U,
  .priority = WATER_TANK_TASK_PRIORITY,
};

/*
 * 水箱浮球接线为反逻辑：
 * 对水箱液位来说，稳定值0表示该限位已经到达，稳定值1表示未到对应液位。
 */
static uint8_t tank_level_reached(AutoInputPoint point)
{
  return (AutoDevice_ReadInput(point) == 0U) ? 1U : 0U;
}

static uint8_t tank_high_reached_after_delay(void)
{
  if (tank_level_reached(AUTO_INPUT_TANK_HIGH) == 0U)
  {
    high_level_reached_tick = 0U;
    return 0U;
  }

  if (high_level_reached_tick == 0U)
  {
    high_level_reached_tick = HAL_GetTick();
    return 0U;
  }

  return ((HAL_GetTick() - high_level_reached_tick) >=
          WATER_TANK_HIGH_STOP_DELAY_MS) ? 1U : 0U;
}

static int16_t recipe_tank_target_x10(const AppRecipe_t *recipe)
{
  return (recipe != NULL) ? recipe->tank_target_temp_x10 :
                            WATER_TANK_STANDBY_TARGET_X10;
}

static int16_t recipe_tank_tolerance_x10(const AppRecipe_t *recipe)
{
  uint16_t tolerance = (recipe != NULL) ? recipe->tank_temp_tolerance_x10 :
                       WATER_TANK_STANDBY_TOLERANCE_X10;

  if (tolerance < 5U)
  {
    tolerance = 5U;
  }

  return (int16_t)tolerance;
}

static uint8_t water_tank_is_after_clean_stage(void)
{
  uint16_t stage = AutoProcess_GetCurrentStage();

  return ((AutoProcess_IsRunning() != 0U) &&
          (stage > (uint16_t)AUTO_PROCESS_STAGE_CLEAN)) ? 1U : 0U;
}

static uint8_t water_tank_is_self_clean_waiting_tank_high(void)
{
  uint16_t step;

  if (TaskClean_IsRunning() == 0U)
  {
    return 0U;
  }

  step = TaskClean_GetCurrentStepId();
  return (step <= 5U) ? 1U : 0U;
}

static uint8_t water_tank_should_yield_to_self_clean(void)
{
  if (TaskClean_IsRunning() == 0U)
  {
    return 0U;
  }

  return (water_tank_is_self_clean_waiting_tank_high() == 0U) ? 1U : 0U;
}

static uint8_t water_tank_should_yield_to_process(void)
{
  uint16_t stage;

  if (AutoProcess_IsRunning() == 0U)
  {
    return 0U;
  }

  stage = AutoProcess_GetCurrentStage();
  return ((stage == (uint16_t)AUTO_PROCESS_STAGE_ENTRY_ENV) ||
          (stage == (uint16_t)AUTO_PROCESS_STAGE_SPRAY) ||
          (stage == (uint16_t)AUTO_PROCESS_STAGE_BATH) ||
          (stage == (uint16_t)AUTO_PROCESS_STAGE_CLEAN)) ? 1U : 0U;
}

static const AppRecipe_t *water_tank_active_recipe(void)
{
  uint16_t stage;
  uint16_t step;

  if (AutoProcess_IsRunning() == 0U)
  {
    return NULL;
  }

  stage = AutoProcess_GetCurrentStage();
  if (stage > (uint16_t)AUTO_PROCESS_STAGE_WASH_READY)
  {
    return AppParam_GetActiveRecipe();
  }

  if (stage == (uint16_t)AUTO_PROCESS_STAGE_WASH_READY)
  {
    step = AutoProcess_GetCurrentStepId();
    if (step >= 30U)
    {
      return AppParam_GetActiveRecipe();
    }
  }

  return NULL;
}

static AutoInputPoint water_tank_target_level_point(void)
{
  return AUTO_INPUT_TANK_HIGH;
}

static void water_tank_log(WaterTankLogReason reason,
                           const AutoSensorData *sensors)
{
#if (WATER_TANK_LOG_ENABLE != 0U)
  uint8_t low = AutoDevice_ReadInput(AUTO_INPUT_TANK_LOW);
  uint8_t middle = AutoDevice_ReadInput(AUTO_INPUT_TANK_MIDDLE);
  uint8_t high = AutoDevice_ReadInput(AUTO_INPUT_TANK_HIGH);
  int16_t tank_x10 = (sensors != NULL) ? sensors->tank_temperature_x10 : 0;
  int16_t mix_x10 = (sensors != NULL) ? sensors->spare_temperature_x10 : 0;
  char line[160];
  int length;

  if ((reason == last_log_reason) &&
      (tank_x10 == last_log_tank_x10) &&
      (low == last_log_low) &&
      (middle == last_log_middle) &&
      (high == last_log_high))
  {
    return;
  }

  last_log_reason = reason;
  last_log_tank_x10 = tank_x10;
  last_log_low = low;
  last_log_middle = middle;
  last_log_high = high;

  length = snprintf(line,
                    sizeof(line),
                    "WTANK reason=%u auto=%u emg=%u inlet=%d tank=%d mix=%d valid=%u/%u/%u low=%u mid=%u high=%u state=%u valve=%u\r\n",
                    (unsigned int)reason,
                    (unsigned int)TaskOutput_IsAutoMode(),
                    (unsigned int)AutoDevice_IsEmergencyStopped(),
                    (int)((sensors != NULL) ? sensors->inlet_temperature_x10 : 0),
                    (int)tank_x10,
                    (int)mix_x10,
                    (unsigned int)((sensors != NULL) ? sensors->inlet_temperature_valid : 0U),
                    (unsigned int)((sensors != NULL) ? sensors->tank_temperature_valid : 0U),
                    (unsigned int)((sensors != NULL) ? sensors->spare_temperature_valid : 0U),
                    (unsigned int)low,
                    (unsigned int)middle,
                    (unsigned int)high,
                    (unsigned int)water_tank_state,
                    (unsigned int)current_valve_angle);

  if (length > 0)
  {
    uint16_t send_length = (length < (int)sizeof(line)) ?
                           (uint16_t)length :
                           (uint16_t)(sizeof(line) - 1U);
    (void)UART2_Send((const uint8_t *)line, send_length);
  }
#else
  (void)reason;
  (void)sensors;
#endif
}

static uint8_t clamp_angle(int16_t angle)
{
  if (angle < (int16_t)WATER_TANK_VALVE_MIN_ANGLE)
  {
    return WATER_TANK_VALVE_MIN_ANGLE;
  }
  if (angle > (int16_t)WATER_TANK_VALVE_MAX_ANGLE)
  {
    return WATER_TANK_VALVE_MAX_ANGLE;
  }
  return (uint8_t)angle;
}

/*
 * 比例阀机械动作不适合 1°/2° 微调。
 * 当目标角度与当前角度差值小于 3° 时，先越过目标点至少 3°，
 * 下一轮再以 3° 回到目标点。例如 30° -> 29°：先到 26°，再到 29°。
 */
static uint8_t plan_angle_move(uint8_t target)
{
  int16_t delta;

  if (current_valve_angle == WATER_TANK_VALVE_UNKNOWN)
  {
    return target;
  }

  delta = (int16_t)target - (int16_t)current_valve_angle;
  if ((delta >= (int16_t)WATER_TANK_VALVE_MOVE_STEP) ||
      (delta <= -(int16_t)WATER_TANK_VALVE_MOVE_STEP) ||
      (delta == 0))
  {
    return target;
  }

  if (delta > 0)
  {
    return clamp_angle((int16_t)target + (int16_t)WATER_TANK_VALVE_MOVE_STEP);
  }
  return clamp_angle((int16_t)target - (int16_t)WATER_TANK_VALVE_MOVE_STEP);
}

static void request_valve_angle(uint8_t target, uint8_t force)
{
  uint8_t command_angle = plan_angle_move(target);
  uint32_t now = HAL_GetTick();

  if ((force == 0U) && (command_angle == current_valve_angle))
  {
    return;
  }

  if ((force == 0U) &&
      (last_valve_adjust_tick != 0U) &&
      ((now - last_valve_adjust_tick) < WATER_TANK_VALVE_ADJUST_INTERVAL_MS))
  {
    return;
  }

  if (TaskComm485_RequestSetProportionalValveAngle(command_angle) == HAL_OK)
  {
    current_valve_angle = command_angle;
    /*
     * 强制回零多发生在退出自动、急停、到达中液位等安全收尾场景。
     * 这些“关阀”动作不能占用后续调温节流窗口，否则刚切回自动时，
     * 目标角度会被30秒调节间隔挡住，比例阀保持0°导致补入55℃热水。
     */
    last_valve_adjust_tick = ((force != 0U) && (command_angle == 0U)) ? 0U : now;
  }
}

static int16_t target_mix_temp_for_tank_temp(int16_t tank_x10, int16_t tank_target_x10)
{
  int16_t fast_until = tank_target_x10 - 20;
  int16_t medium_until = tank_target_x10 - 10;
  int16_t slow_until = tank_target_x10 - 5;

  if (tank_x10 < fast_until)
  {
    return 550;  /* 快速升温阶段允许热水进入。 */
  }
  if (tank_x10 < medium_until)
  {
    return tank_target_x10 + 40;
  }
  if (tank_x10 < slow_until)
  {
    return tank_target_x10 + 15;
  }
  return tank_target_x10 - 5;
}

static uint8_t heat_valve_angle_for_sensors(const AutoSensorData *sensors, const AppRecipe_t *recipe)
{
  int16_t tank_x10 = (sensors != NULL) ? sensors->tank_temperature_x10 : 0;
  int16_t target_x10 = recipe_tank_target_x10(recipe);
  int16_t fast_until = target_x10 - 20;
  int16_t medium_until = target_x10 - 10;
  int16_t slow_until = target_x10 - 5;
  int16_t target_mix_x10 = target_mix_temp_for_tank_temp(tank_x10, target_x10);
  int16_t angle;

  if (tank_x10 < fast_until)
  {
    return 0U;   /* 全关比例阀，先用55℃热水快速把水箱拉起来。 */
  }

  if (tank_x10 < medium_until)
  {
    angle = 27;  /* 约40.5℃，比26°略柔和一点。 */
  }
  else if (tank_x10 < slow_until)
  {
    angle = 32;  /* 介于30°和34°之间，继续放慢。 */
  }
  else
  {
    angle = 37;  /* 约35.5℃，修正你现场37.7℃偏高的问题。 */
  }

  /*
   * 冷水进水口温度前馈：
   * 冬天冷水很低时，同样角度下混合水会偏冷，需要减少角度；
   * 夏天冷水较高时，同样角度下混合水会偏热，需要增加角度。
   */
  if ((sensors != NULL) && (sensors->inlet_temperature_valid != 0U))
  {
    if (sensors->inlet_temperature_x10 < 100)
    {
      angle -= 3;
    }
    else if (sensors->inlet_temperature_x10 > 250)
    {
      angle += 3;
    }
  }

  /*
   * 进水箱混合水温闭环微调：
   * CH4 是最终混合后进入水箱的水温，比单看比例阀角度更可靠。
   * request_valve_angle() 内部有30秒节流，所以这里不会高频抖动。
   */
  if ((sensors != NULL) && (sensors->spare_temperature_valid != 0U))
  {
    if (sensors->spare_temperature_x10 > (target_mix_x10 + WATER_TANK_MIX_TOL_X10))
    {
      angle += 3;
    }
    else if (sensors->spare_temperature_x10 < (target_mix_x10 - WATER_TANK_MIX_TOL_X10))
    {
      angle -= 3;
    }
  }

  return clamp_angle(angle);
}

static void water_tank_booster_off(void)
{
  (void)TaskComm485_RequestSetBoosterPumpRunning(0U);
}

static void water_tank_recirc_booster_on(void)
{
  (void)TaskComm485_ForceSetBoosterPumpPercent(WATER_TANK_BOOSTER_PRESSURE_X10);
  (void)TaskComm485_ForceSetBoosterPumpRunning(1U);
}

static void water_tank_outputs_off(void)
{
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_TAP_WATER_VALVE, 0U);
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_STANDBY2_VALVE, 0U);
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_BYPASS_VALVE, 0U);
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_SELF_CLEAN_REAR_VALVE, 0U);
  water_tank_booster_off();
  request_valve_angle(0U, 1U);
}

static void water_tank_gpio_outputs_off(void)
{
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_TAP_WATER_VALVE, 0U);
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_STANDBY2_VALVE, 0U);
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_BYPASS_VALVE, 0U);
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_SELF_CLEAN_REAR_VALVE, 0U);
}

static void water_tank_release_process_outputs(void)
{
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_TAP_WATER_VALVE, 0U);
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_STANDBY2_VALVE, 0U);
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_BYPASS_VALVE, 0U);
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_SELF_CLEAN_REAR_VALVE, 0U);
  request_valve_angle(0U, 1U);
}

static void start_hot_fill(const AutoSensorData *sensors, const AppRecipe_t *recipe)
{
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_BYPASS_VALVE, 0U);
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_SELF_CLEAN_REAR_VALVE, 0U);
  water_tank_booster_off();
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_TAP_WATER_VALVE, 1U);
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_STANDBY2_VALVE, 1U);
  if (sensors != NULL)
  {
    request_valve_angle(heat_valve_angle_for_sensors(sensors, recipe), 0U);
  }
}

static void start_cold_fill(void)
{
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_BYPASS_VALVE, 0U);
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_SELF_CLEAN_REAR_VALVE, 0U);
  water_tank_booster_off();
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_STANDBY2_VALVE, 0U);
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_TAP_WATER_VALVE, 1U);
  request_valve_angle(WATER_TANK_VALVE_MAX_ANGLE, 1U);
}

static void start_recirc_heat(void)
{
  /*
   * Middle level reached but tank temperature is still low:
   * tank -> water heater -> tank recirculation.
   */
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_TAP_WATER_VALVE, 0U);
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_STANDBY2_VALVE, 0U);
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_BYPASS_VALVE, 1U);
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_SELF_CLEAN_REAR_VALVE, 0U);
  water_tank_recirc_booster_on();
  request_valve_angle(0U, 1U);
}

static void keep_recirc_heat(void)
{
  if (water_tank_state != WATER_TANK_STATE_RECIRC_HEATING)
  {
    start_recirc_heat();
  }
  water_tank_state = WATER_TANK_STATE_RECIRC_HEATING;
}

static void start_hot_drain(void)
{
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_TAP_WATER_VALVE, 0U);
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_STANDBY2_VALVE, 0U);
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_BYPASS_VALVE, 0U);
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_SELF_CLEAN_REAR_VALVE, 1U);
  water_tank_booster_off();
  request_valve_angle(0U, 1U);
}

static void protect_booster_pump(void)
{
  /*
   * 低液位未到位时为1，表示水位低于低液位，任何状态下都不能运行增压泵。
   */
  if (AutoDevice_ReadInput(AUTO_INPUT_TANK_LOW) != 0U)
  {
    water_tank_booster_off();
  }
}

static void process_water_tank(void)
{
  AutoSensorData sensors;
  const AppRecipe_t *recipe = water_tank_active_recipe();
  AutoInputPoint target_level_point;
  uint8_t target_level_reached;
  uint8_t high_reached;
  uint8_t cold_prepare_mode;
  int16_t target_x10;
  int16_t tolerance_x10;
  int16_t low_start_x10;
  int16_t high_start_x10;
  int16_t recirc_start_x10;
  int16_t recirc_stop_x10;

  uint8_t auto_mode = TaskOutput_IsAutoMode();

  protect_booster_pump();

  if ((auto_mode != 0U) && (previous_auto_mode == 0U))
  {
    TaskComm485_ReleaseNonzero(COMM485_INHIBIT_STARTUP |
                               COMM485_INHIBIT_SAFE_STOP |
                               COMM485_INHIBIT_MODE_CHANGE);
  }
  previous_auto_mode = auto_mode;

  if (auto_mode == 0U)
  {
    water_tank_state = WATER_TANK_STATE_IDLE;
    water_tank_recirc_active = 0U;
    hot_drain_active = 0U;
    hot_drain_start_tick = 0U;
    high_level_reached_tick = 0U;
    last_recipe_target_x10 = WATER_TANK_TARGET_UNKNOWN;
    water_tank_gpio_outputs_off();
    water_tank_log(WATER_TANK_LOG_NOT_AUTO, NULL);
    return;
  }

  if (AutoDevice_IsEmergencyStopped() != 0U)
  {
    water_tank_state = WATER_TANK_STATE_IDLE;
    water_tank_recirc_active = 0U;
    hot_drain_active = 0U;
    hot_drain_start_tick = 0U;
    high_level_reached_tick = 0U;
    last_recipe_target_x10 = WATER_TANK_TARGET_UNKNOWN;
    water_tank_outputs_off();
    water_tank_log(WATER_TANK_LOG_EMERGENCY, NULL);
    return;
  }

  if (TaskComm485_GetInhibitReasons() != 0U)
  {
    water_tank_state = WATER_TANK_STATE_IDLE;
    water_tank_recirc_active = 0U;
    hot_drain_active = 0U;
    hot_drain_start_tick = 0U;
    high_level_reached_tick = 0U;
    water_tank_outputs_off();
    return;
  }

  /*
   * 入舱准备排管路水、冲洗、沐浴、清洗这些阶段会直接控制水泵和水路阀。
   * 水箱后台此时必须让出输出控制权，不能再按恒温策略写泵/阀门，
   * 否则会把流程步骤刚打开的增压泵或阀门关掉。
   */
  if (water_tank_should_yield_to_process() != 0U)
  {
    water_tank_state = WATER_TANK_STATE_IDLE;
    water_tank_recirc_active = 0U;
    hot_drain_active = 0U;
    hot_drain_start_tick = 0U;
    high_level_reached_tick = 0U;
    water_tank_release_process_outputs();
    return;
  }

  if (water_tank_should_yield_to_self_clean() != 0U)
  {
    water_tank_state = WATER_TANK_STATE_IDLE;
    water_tank_recirc_active = 0U;
    hot_drain_active = 0U;
    hot_drain_start_tick = 0U;
    high_level_reached_tick = 0U;
    return;
  }

  high_reached = tank_high_reached_after_delay();
  if ((self_clean_prepare_requested != 0U) &&
      (AutoProcess_IsRunning() == 0U) &&
      (TaskClean_IsRunning() == 0U))
  {
    self_clean_prepare_requested = 0U;
  }

  cold_prepare_mode = ((self_clean_prepare_requested != 0U) ||
                       (water_tank_is_after_clean_stage() != 0U) ||
                       (water_tank_is_self_clean_waiting_tank_high() != 0U)) ?
                      1U : 0U;

  /*
   * 清洗流程结束后的自动阶段，为自清洁准备大量用水：
   * 只补冷水到上液位，不做水温调节。
   */
  if (cold_prepare_mode != 0U)
  {
    water_tank_recirc_active = 0U;
    if (high_reached != 0U)
    {
      self_clean_prepare_requested = 0U;
      water_tank_state = WATER_TANK_STATE_IDLE;
      water_tank_outputs_off();
      water_tank_log(WATER_TANK_LOG_STOP_TARGET, NULL);
    }
    else
    {
      water_tank_state = WATER_TANK_STATE_COOLING;
      start_cold_fill();
      water_tank_log(WATER_TANK_LOG_COOL, NULL);
    }
    return;
  }

  if ((AutoDevice_GetSensorData(&sensors) == 0U) ||
      (sensors.tank_temperature_valid == 0U))
  {
    water_tank_state = WATER_TANK_STATE_IDLE;
    hot_drain_active = 0U;
    hot_drain_start_tick = 0U;
    high_level_reached_tick = 0U;
    water_tank_outputs_off();
    water_tank_log(WATER_TANK_LOG_SENSOR_INVALID, &sensors);
    return;
  }

  target_level_point = water_tank_target_level_point();
  target_level_reached = (target_level_point == AUTO_INPUT_TANK_HIGH) ?
                         high_reached : tank_level_reached(target_level_point);
  target_x10 = recipe_tank_target_x10(recipe);
  tolerance_x10 = recipe_tank_tolerance_x10(recipe);
  low_start_x10 = target_x10 - tolerance_x10;
  high_start_x10 = target_x10 + tolerance_x10;
  recirc_start_x10 = low_start_x10;
  if (recirc_start_x10 < WATER_TANK_RECIRC_START_X10)
  {
    recirc_start_x10 = WATER_TANK_RECIRC_START_X10;
  }
  recirc_stop_x10 = (recipe != NULL) ? (target_x10 - 5) : target_x10;

  if (last_recipe_target_x10 == WATER_TANK_TARGET_UNKNOWN)
  {
    if (sensors.tank_temperature_x10 > high_start_x10)
    {
      hot_drain_active = 1U;
      hot_drain_start_tick = 0U;
    }
  }
  else if ((target_x10 < last_recipe_target_x10) &&
           (sensors.tank_temperature_x10 > high_start_x10))
  {
    hot_drain_active = 1U;
    hot_drain_start_tick = 0U;
  }
  last_recipe_target_x10 = target_x10;

  if (hot_drain_active != 0U)
  {
    if (hot_drain_start_tick == 0U)
    {
      hot_drain_start_tick = HAL_GetTick();
    }

    if ((AutoDevice_ReadInput(AUTO_INPUT_TANK_LOW) != 0U) ||
        ((HAL_GetTick() - hot_drain_start_tick) >= WATER_TANK_HOT_DRAIN_MS))
    {
      hot_drain_active = 0U;
      hot_drain_start_tick = 0U;
      water_tank_state = WATER_TANK_STATE_IDLE;
      water_tank_outputs_off();
      water_tank_log(WATER_TANK_LOG_STOP_TARGET, &sensors);
      return;
    }

    water_tank_recirc_active = 0U;
    water_tank_state = WATER_TANK_STATE_HOT_DRAIN;
    start_hot_drain();
    water_tank_log(WATER_TANK_LOG_HOT_DRAIN, &sensors);
    return;
  }

  if (high_reached != 0U)
  {
    target_level_reached = 1U;
  }

  if (target_level_reached != 0U)
  {
    if (water_tank_recirc_active != 0U)
    {
      if (sensors.tank_temperature_x10 >= recirc_stop_x10)
      {
        water_tank_recirc_active = 0U;
        water_tank_state = WATER_TANK_STATE_IDLE;
        water_tank_outputs_off();
        water_tank_log(WATER_TANK_LOG_MIDDLE_REACHED, &sensors);
      }
      else
      {
        keep_recirc_heat();
        water_tank_log(WATER_TANK_LOG_RECIRC_HEAT, &sensors);
      }
    }
    else if (sensors.tank_temperature_x10 < recirc_start_x10)
    {
      water_tank_recirc_active = 1U;
      keep_recirc_heat();
      water_tank_log(WATER_TANK_LOG_RECIRC_HEAT, &sensors);
    }
    else
    {
      water_tank_state = WATER_TANK_STATE_IDLE;
      water_tank_outputs_off();
      water_tank_log(WATER_TANK_LOG_MIDDLE_REACHED, &sensors);
    }
    return;
  }

  water_tank_recirc_active = 0U;

  /*
   * 只要没有到中液位，就必须继续补水。
   * 温度只决定补水温度：
   * - 水箱明显偏高时才补冷水；
   * - 其余情况补热水/目标混合水，避免水箱已经接近36℃时被冷水拉低。
   */
  if (sensors.tank_temperature_x10 > high_start_x10)
  {
    water_tank_state = WATER_TANK_STATE_COOLING;
    start_cold_fill();
    water_tank_log(WATER_TANK_LOG_COOL, &sensors);
  }
  else
  {
    water_tank_state = WATER_TANK_STATE_HEATING;
    start_hot_fill(&sensors, recipe);
    water_tank_log(WATER_TANK_LOG_HEAT, &sensors);
  }
}

static void WaterTankTask(void *argument)
{
#if (WATER_TANK_LOG_ENABLE != 0U)
  static const uint8_t start_message[] = "WTANK TASK START\r\n";
#endif

  (void)argument;

#if (WATER_TANK_LOG_ENABLE != 0U)
  (void)UART2_Send(start_message, (uint16_t)(sizeof(start_message) - 1U));
#endif

  current_valve_angle = WATER_TANK_VALVE_UNKNOWN;
  water_tank_state = WATER_TANK_STATE_IDLE;
  water_tank_recirc_active = 0U;
  high_level_reached_tick = 0U;
  request_valve_angle(0U, 1U);

  for (;;)
  {
    TaskWatchdog_Heartbeat(TASK_WATCHDOG_WATER_TANK);
    process_water_tank();
    osDelay(WATER_TANK_PERIOD_MS);
  }
}

void TaskWaterTank_Create(void)
{
  self_clean_prepare_requested = 0U;
  previous_auto_mode = 0U;
  water_tank_task_handle = osThreadNew(WaterTankTask,
                                       NULL,
                                       &water_tank_task_attributes);
  if (water_tank_task_handle == NULL)
  {
    static const uint8_t fail_message[] = "WTANK CREATE FAIL\r\n";
    (void)UART2_Send(fail_message, (uint16_t)(sizeof(fail_message) - 1U));
  }
  (void)water_tank_task_handle;
}

void TaskWaterTank_RequestSelfCleanPrepare(void)
{
  self_clean_prepare_requested = 1U;
}

void TaskWaterTank_ClearSelfCleanPrepare(void)
{
  self_clean_prepare_requested = 0U;
}
