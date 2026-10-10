#include "task_self_test.h"

#include "app_param.h"
#include "auto_device.h"
#include "auto_process.h"
#include "cmsis_os.h"
#include "rs485_config.h"
#include "task_clean.h"
#include "task_comm485.h"
#include "task_output.h"
#include "task_water_tank.h"

#include <string.h>

#define SELF_TEST_TASK_STACK       1024U
#define SELF_TEST_TASK_PRIORITY    osPriorityBelowNormal
#define SELF_TEST_EVENT_REQUEST    0x00000001U
#define SELF_TEST_LOOP_MS          20U
#define SELF_TEST_PRESSURE_RELEASE_MS 1000U

#define TEST_BIT_LEFT_FAN          (1UL << 0)
#define TEST_BIT_RIGHT_FAN         (1UL << 1)
#define TEST_BIT_BOTTOM_FAN        (1UL << 2)
#define TEST_BIT_LEFT_HEATER       (1UL << 3)
#define TEST_BIT_RIGHT_HEATER      (1UL << 4)
#define TEST_BIT_BOTTOM_HEATER     (1UL << 5)
#define TEST_BIT_BOOSTER           (1UL << 6)
#define TEST_BIT_FILL_PATH         (1UL << 7)
#define TEST_BIT_SHOWER_UPPER      (1UL << 8)
#define TEST_BIT_SHOWER_MIDDLE     (1UL << 9)
#define TEST_BIT_SHOWER_LOWER      (1UL << 10)
#define TEST_BIT_SHOWER_REAR       (1UL << 11)
#define TEST_BIT_CLEAN_TOP         (1UL << 12)
#define TEST_BIT_CLEAN_FRONT_UPPER (1UL << 13)
#define TEST_BIT_CLEAN_FRONT_LOWER (1UL << 14)
#define TEST_BIT_CLEAN_REAR        (1UL << 15)
#define TEST_BIT_BYPASS            (1UL << 16)
#define TEST_BIT_HOT_PATH          (1UL << 17)

typedef enum
{
  TEST_STATUS_PASS = 0,
  TEST_STATUS_FAIL,
  TEST_STATUS_ABORT,
  TEST_STATUS_RETRY
} TestStatus;

typedef struct
{
  AutoOutputPoint output;
  uint32_t bit;
} ValveTestPoint;

static const uint8_t fan_stations[] = {
  RS485_STATION_BOTTOM_FAN
};
static const uint8_t heater_stations[] = {
  RS485_STATION_BOTTOM_HEATER
};
static const uint32_t fan_bits[] = {
  TEST_BIT_BOTTOM_FAN
};
static const uint32_t heater_bits[] = {
  TEST_BIT_BOTTOM_HEATER
};
static const ValveTestPoint valve_tests[] = {
  {AUTO_OUTPUT_SHOWER_UPPER, TEST_BIT_SHOWER_UPPER},
  {AUTO_OUTPUT_SHOWER_MIDDLE, TEST_BIT_SHOWER_MIDDLE},
  {AUTO_OUTPUT_SELF_CLEAN_TOP_VALVE, TEST_BIT_CLEAN_TOP},
  {AUTO_OUTPUT_SELF_CLEAN_LOWER_VALVE, TEST_BIT_CLEAN_FRONT_UPPER},
  {AUTO_OUTPUT_SELF_CLEAN_TOILET_VALVE, TEST_BIT_CLEAN_FRONT_LOWER},
  {AUTO_OUTPUT_BYPASS_VALVE, TEST_BIT_BYPASS}
};

static osThreadId_t self_test_task_handle;
static osEventFlagsId_t self_test_event;
static volatile uint8_t running;
static volatile uint8_t paused;
static volatile uint8_t stop_requested;
static volatile uint8_t requested_mode;
static volatile uint16_t current_step_id;
static volatile uint16_t progress_x10;
static volatile uint32_t tested_bitmap;
static volatile uint32_t passed_bitmap;
static volatile uint8_t result_pending;
static SelfTestResult last_result;
static uint32_t next_result_id;
static uint8_t completed_items;
static uint8_t total_items;

static const osThreadAttr_t self_test_task_attributes = {
  .name = "selfTest",
  .stack_size = SELF_TEST_TASK_STACK * 4U,
  .priority = SELF_TEST_TASK_PRIORITY,
};

static uint8_t level_reached(AutoInputPoint point)
{
  return (AutoDevice_ReadInput(point) == 0U) ? 1U : 0U;
}

static uint8_t levels_are_consistent(void)
{
  uint8_t low = level_reached(AUTO_INPUT_TANK_LOW);
  uint8_t middle = level_reached(AUTO_INPUT_TANK_MIDDLE);
  uint8_t high = level_reached(AUTO_INPUT_TANK_HIGH);

  return (((high != 0U) && ((middle == 0U) || (low == 0U))) ||
          ((middle != 0U) && (low == 0U))) ? 0U : 1U;
}

static uint8_t cabin_is_safe(void)
{
  return ((AutoDevice_IsEmergencyStopped() == 0U) &&
          (AutoDevice_ReadInput(AUTO_INPUT_UPPER_DOOR_CLOSED) != 0U) &&
          (AutoDevice_ReadInput(AUTO_INPUT_LOWER_DOOR_CLOSED) != 0U)) ? 1U : 0U;
}

static void close_water_outputs(void)
{
  uint8_t index;

  AutoDevice_SetAllShowerValves(0U);
  for (index = 2U; index < (uint8_t)(sizeof(valve_tests) / sizeof(valve_tests[0])); ++index)
  {
    (void)AutoDevice_SetOutput(valve_tests[index].output, 0U);
  }
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_TAP_WATER_VALVE, 0U);
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_STANDBY2_VALVE, 0U);
}

static void self_test_outputs_off(void)
{
  uint8_t index;

  TaskComm485_SetSelfTestPressurePriority(0U);
  (void)AutoDevice_SetBlower(0U);
  (void)AutoDevice_SetTopFanRunning(0U);
  (void)AutoDevice_SetBoosterPumpRunning(0U);
  (void)TaskComm485_RequestSetProportionalValveAngle(0U);
  close_water_outputs();
  for (index = 0U; index < 1U; ++index)
  {
    (void)TaskComm485_ForceSetPercent(heater_stations[index], 0U);
    (void)TaskComm485_ForceSetPercent(fan_stations[index], 0U);
  }
  (void)TaskOutput_Sync(1000U);
}

static TestStatus poll_control(void)
{
  TaskComm485EnvironmentData environment = {0};

  if (stop_requested != 0U)
  {
    return TEST_STATUS_ABORT;
  }
  if (cabin_is_safe() == 0U)
  {
    AutoDevice_ReportFault(AUTO_FAULT_SELF_TEST_NOT_ALLOWED,
                           AUTO_REPORT_SENSOR_FAULT);
    return TEST_STATUS_ABORT;
  }
  if (levels_are_consistent() == 0U)
  {
    AutoDevice_ReportFault(AUTO_FAULT_SELF_TEST_LEVEL_INVALID,
                           AUTO_REPORT_SENSOR_FAULT);
    return TEST_STATUS_ABORT;
  }
  (void)TaskComm485_GetEnvironmentData(&environment);
  if ((environment.incab_valid != 0U) &&
      (environment.incab_temperature_x10 >=
       AppParam_GetSelfTestConfig()->heater_max_temp_x10))
  {
    AutoDevice_ReportFault(AUTO_FAULT_SELF_TEST_OVER_TEMPERATURE,
                           AUTO_REPORT_SENSOR_FAULT);
    return TEST_STATUS_ABORT;
  }
  if (paused != 0U)
  {
    self_test_outputs_off();
    while ((paused != 0U) && (stop_requested == 0U))
    {
      osDelay(SELF_TEST_LOOP_MS);
    }
    return (stop_requested != 0U) ? TEST_STATUS_ABORT : TEST_STATUS_RETRY;
  }
  return TEST_STATUS_PASS;
}

static void item_done(void)
{
  if (completed_items < total_items)
  {
    completed_items++;
  }
  progress_x10 = (uint16_t)(((uint32_t)completed_items * 1000U) / total_items);
}

static TestStatus wait_station_write(uint8_t station, uint8_t percent)
{
  const AppSelfTestConfig_t *config = AppParam_GetSelfTestConfig();
  uint32_t before = TaskComm485_GetStationSuccessCount(station);
  uint32_t start = HAL_GetTick();
  TestStatus control;

  if (TaskComm485_ForceSetPercent(station, percent) != HAL_OK)
  {
    return TEST_STATUS_FAIL;
  }
  while ((HAL_GetTick() - start) < config->communication_timeout_ms)
  {
    control = poll_control();
    if (control != TEST_STATUS_PASS)
    {
      return control;
    }
    if (TaskComm485_GetStationSuccessCount(station) != before)
    {
      return TEST_STATUS_PASS;
    }
    osDelay(SELF_TEST_LOOP_MS);
  }
  return TEST_STATUS_FAIL;
}

static TestStatus test_fan(uint8_t index)
{
  TestStatus status;

  current_step_id = (uint16_t)(10U + index);
  tested_bitmap |= fan_bits[index];
  status = wait_station_write(fan_stations[index],
                              AppParam_GetSelfTestConfig()->fast_fan_percent);
  (void)TaskComm485_ForceSetPercent(fan_stations[index], 0U);
  if (status == TEST_STATUS_PASS)
  {
    passed_bitmap |= fan_bits[index];
  }
  return status;
}

static TestStatus test_heater_quick(uint8_t index)
{
  TestStatus status;

  current_step_id = (uint16_t)(20U + index);
  tested_bitmap |= heater_bits[index];
  status = wait_station_write(heater_stations[index], 0U);
  if (status == TEST_STATUS_PASS)
  {
    passed_bitmap |= heater_bits[index];
  }
  return status;
}

static TestStatus test_heater_deep(uint8_t index)
{
  uint8_t fan_index;
  TestStatus write_status;
  const AppSelfTestConfig_t *config = AppParam_GetSelfTestConfig();
  TaskComm485EnvironmentData env = {0};
  int16_t baseline;
  uint32_t start;
  TestStatus control;

  current_step_id = (uint16_t)(30U + index);
  tested_bitmap |= heater_bits[index];
  for (fan_index = 0U; fan_index < 1U; ++fan_index)
  {
    if (TaskComm485_RequestSetPercent(fan_stations[fan_index], config->deep_fan_percent) != HAL_OK)
    {
      return TEST_STATUS_FAIL;
    }
  }
  (void)TaskComm485_GetEnvironmentData(&env);
  if (env.incab_valid == 0U)
  {
    return TEST_STATUS_FAIL;
  }
  baseline = env.incab_temperature_x10;
  write_status = wait_station_write(heater_stations[index], config->deep_heater_percent);
  if (write_status != TEST_STATUS_PASS)
  {
    (void)TaskComm485_ForceSetPercent(heater_stations[index], 0U);
    return write_status;
  }
  start = HAL_GetTick();
  while ((HAL_GetTick() - start) < config->heater_observe_ms)
  {
    control = poll_control();
    if (control != TEST_STATUS_PASS)
    {
      (void)TaskComm485_ForceSetPercent(heater_stations[index], 0U);
      return control;
    }
    (void)TaskComm485_GetEnvironmentData(&env);
    if (env.incab_valid != 0U)
    {
      if (env.incab_temperature_x10 >= config->heater_max_temp_x10)
      {
        (void)TaskComm485_ForceSetPercent(heater_stations[index], 0U);
        AutoDevice_ReportFault(AUTO_FAULT_SELF_TEST_OVER_TEMPERATURE,
                               AUTO_REPORT_SENSOR_FAULT);
        return TEST_STATUS_ABORT;
      }
      if ((env.incab_temperature_x10 - baseline) >=
          (int16_t)config->heater_min_rise_x10)
      {
        passed_bitmap |= heater_bits[index];
        (void)TaskComm485_ForceSetPercent(heater_stations[index], 0U);
        return TEST_STATUS_PASS;
      }
    }
    osDelay(SELF_TEST_LOOP_MS);
  }
  (void)TaskComm485_ForceSetPercent(heater_stations[index], 0U);
  return TEST_STATUS_FAIL;
}

static TestStatus monitor_pressure_safety(void)
{
  uint8_t pressure;
  if (AppParam_BoosterPumpUsesRs485() &&
      TaskComm485_GetBoosterPumpFeedback(&pressure) &&
      pressure > AppParam_GetSelfTestConfig()->pressure_over_x10)
  {
    self_test_outputs_off();
    AutoDevice_ReportFault(AUTO_FAULT_SELF_TEST_OVERPRESSURE, AUTO_REPORT_SENSOR_FAULT);
    return TEST_STATUS_ABORT;
  }
  return TEST_STATUS_PASS;
}

static TestStatus drain_below_middle(void)
{
  const AppSelfTestConfig_t *config = AppParam_GetSelfTestConfig();
  uint32_t start;
  TestStatus control;

  if (level_reached(AUTO_INPUT_TANK_MIDDLE) == 0U)
  {
    return TEST_STATUS_PASS;
  }
  close_water_outputs();
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_SELF_CLEAN_TOILET_VALVE, 1U);
  (void)AutoDevice_SetBoosterPumpPercent(config->valve_pressure_x10);
  if (AutoDevice_SetBoosterPumpRunning(1U) != HAL_OK)
  {
    self_test_outputs_off();
    return TEST_STATUS_FAIL;
  }
  start = HAL_GetTick();
  while ((HAL_GetTick() - start) < config->fill_timeout_ms)
  {
    control = poll_control();
    if ((control != TEST_STATUS_PASS) ||
        (monitor_pressure_safety() != TEST_STATUS_PASS))
    {
      self_test_outputs_off();
      return (control != TEST_STATUS_PASS) ? control : TEST_STATUS_ABORT;
    }
    if (level_reached(AUTO_INPUT_TANK_MIDDLE) == 0U)
    {
      self_test_outputs_off();
      return TEST_STATUS_PASS;
    }
    if (level_reached(AUTO_INPUT_TANK_LOW) == 0U)
    {
      self_test_outputs_off();
      AutoDevice_ReportFault(AUTO_FAULT_SELF_TEST_LEVEL_INVALID,
                             AUTO_REPORT_SENSOR_FAULT);
      return TEST_STATUS_ABORT;
    }
    osDelay(SELF_TEST_LOOP_MS);
  }
  self_test_outputs_off();
  return TEST_STATUS_FAIL;
}

static TestStatus fill_to_middle(uint8_t record_result)
{
  const AppSelfTestConfig_t *config = AppParam_GetSelfTestConfig();
  uint32_t start;
  TestStatus control;

  if (record_result != 0U)
  {
    current_step_id = 50U;
    tested_bitmap |= TEST_BIT_FILL_PATH;
  }
  if (level_reached(AUTO_INPUT_TANK_MIDDLE) != 0U)
  {
    control = drain_below_middle();
    if (control != TEST_STATUS_PASS)
    {
      return control;
    }
  }
  close_water_outputs();
  (void)TaskComm485_RequestSetProportionalValveAngle(config->proportional_valve_angle);
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_TAP_WATER_VALVE, 1U);
  start = HAL_GetTick();
  while ((HAL_GetTick() - start) < config->fill_timeout_ms)
  {
    control = poll_control();
    if (control != TEST_STATUS_PASS)
    {
      self_test_outputs_off();
      return control;
    }
    if (level_reached(AUTO_INPUT_TANK_MIDDLE) != 0U)
    {
      self_test_outputs_off();
      if (record_result != 0U)
      {
        passed_bitmap |= TEST_BIT_FILL_PATH;
      }
      return TEST_STATUS_PASS;
    }
    if (level_reached(AUTO_INPUT_TANK_HIGH) != 0U)
    {
      self_test_outputs_off();
      AutoDevice_ReportFault(AUTO_FAULT_SELF_TEST_LEVEL_INVALID,
                             AUTO_REPORT_SENSOR_FAULT);
      return TEST_STATUS_ABORT;
    }
    osDelay(SELF_TEST_LOOP_MS);
  }
  self_test_outputs_off();
  return TEST_STATUS_FAIL;
}

static TestStatus test_valve(const ValveTestPoint *test, uint8_t index)
{
  const AppSelfTestConfig_t *config = AppParam_GetSelfTestConfig();
  uint32_t start;
  uint32_t stable_start = 0U;
  TestStatus control;

  if (!AppParam_BoosterPumpUsesRs485()) return TEST_STATUS_PASS;
  current_step_id = (uint16_t)(60U + index);
  tested_bitmap |= test->bit;
  tested_bitmap |= TEST_BIT_BOOSTER;
  if (level_reached(AUTO_INPUT_TANK_LOW) == 0U)
  {
    control = fill_to_middle(0U);
    if (control != TEST_STATUS_PASS)
    {
      return control;
    }
  }
  close_water_outputs();
  (void)AutoDevice_SetOutput(test->output, 1U);
  if (TaskOutput_Sync(1000U) != HAL_OK)
  {
    close_water_outputs();
    return TEST_STATUS_FAIL;
  }
  uint32_t writes = TaskComm485_GetStationSuccessCount(RS485_STATION_BOOSTER_PUMP);
  TaskComm485_SetSelfTestPressurePriority(1U);
  if (TaskComm485_ForceSetBoosterPumpPercent(config->valve_pressure_x10) != HAL_OK)
  {
    self_test_outputs_off();
    return TEST_STATUS_FAIL;
  }
  start = HAL_GetTick();
  while (TaskComm485_GetStationSuccessCount(RS485_STATION_BOOSTER_PUMP) == writes)
  {
    control = poll_control();
    if (control != TEST_STATUS_PASS ||
        HAL_GetTick() - start >= config->communication_timeout_ms)
    {
      self_test_outputs_off();
      return control == TEST_STATUS_PASS ? TEST_STATUS_FAIL : control;
    }
    osDelay(SELF_TEST_LOOP_MS);
  }
  if (AutoDevice_SetBoosterPumpRunning(1U) != HAL_OK)
  {
    self_test_outputs_off();
    return TEST_STATUS_FAIL;
  }
  start = HAL_GetTick();
  while ((HAL_GetTick() - start) < config->pressure_timeout_ms)
  {
    control = poll_control();
    if (control != TEST_STATUS_PASS)
    {
      self_test_outputs_off();
      return control;
    }
    uint8_t pressure;
    if (monitor_pressure_safety() == TEST_STATUS_ABORT) return TEST_STATUS_ABORT;
    if (!TaskComm485_GetBoosterPumpFeedback(&pressure) ||
        pressure < config->pressure_min_x10 || pressure > config->pressure_max_x10)
    {
      stable_start = 0U;
    }
    else if (stable_start == 0U)
    {
      stable_start = HAL_GetTick();
    }
    else if ((HAL_GetTick() - stable_start) >= config->pressure_stable_ms)
    {
      passed_bitmap |= test->bit;
      passed_bitmap |= TEST_BIT_BOOSTER;
      self_test_outputs_off();
      osDelay(SELF_TEST_PRESSURE_RELEASE_MS);
      return TEST_STATUS_PASS;
    }
    if (level_reached(AUTO_INPUT_TANK_LOW) == 0U)
    {
      self_test_outputs_off();
      AutoDevice_ReportFault(AUTO_FAULT_SELF_TEST_LEVEL_INVALID,
                             AUTO_REPORT_SENSOR_FAULT);
      return TEST_STATUS_ABORT;
    }
    osDelay(SELF_TEST_LOOP_MS);
  }
  self_test_outputs_off();
  return TEST_STATUS_FAIL;
}

static TestStatus test_hot_path(void)
{
  const AppSelfTestConfig_t *config = AppParam_GetSelfTestConfig();
  AutoSensorData sensors;
  int16_t last_temperature;
  uint8_t trend_count = 0U;
  uint32_t last_update_tick;
  uint32_t start;
  TestStatus control;

  current_step_id = 100U;
  tested_bitmap |= TEST_BIT_HOT_PATH;
  control = drain_below_middle();
  if (control != TEST_STATUS_PASS)
  {
    return control;
  }
  if ((AutoDevice_GetSensorData(&sensors) == 0U) ||
      (sensors.tank_temperature_valid == 0U))
  {
    return TEST_STATUS_FAIL;
  }
  last_temperature = sensors.tank_temperature_x10;
  last_update_tick = sensors.tank_update_tick;
  close_water_outputs();
  (void)TaskComm485_RequestSetProportionalValveAngle(
      TaskWaterTank_GetHotFillValveAngle());
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_TAP_WATER_VALVE, 1U);
  (void)AutoDevice_SetOutput(AUTO_OUTPUT_STANDBY2_VALVE, 1U);
  start = HAL_GetTick();
  while ((HAL_GetTick() - start) < config->hot_observe_ms)
  {
    control = poll_control();
    if (control != TEST_STATUS_PASS)
    {
      self_test_outputs_off();
      return control;
    }
    if (level_reached(AUTO_INPUT_TANK_HIGH) != 0U)
    {
      self_test_outputs_off();
      return TEST_STATUS_FAIL;
    }
    if ((AutoDevice_GetSensorData(&sensors) != 0U) &&
        (sensors.tank_temperature_valid != 0U) &&
        (sensors.tank_update_tick != last_update_tick))
    {
      last_update_tick = sensors.tank_update_tick;
      if (sensors.tank_temperature_x10 > last_temperature)
      {
        trend_count++;
      }
      else if (sensors.tank_temperature_x10 < last_temperature)
      {
        trend_count = 0U;
      }
      last_temperature = sensors.tank_temperature_x10;
      if (trend_count >= config->hot_trend_samples)
      {
        passed_bitmap |= TEST_BIT_HOT_PATH;
        self_test_outputs_off();
        return TEST_STATUS_PASS;
      }
    }
    osDelay(SELF_TEST_LOOP_MS);
  }
  self_test_outputs_off();
  return TEST_STATUS_FAIL;
}

static void publish_result(SelfTestMode mode, SelfTestResultState state)
{
  next_result_id++;
  if (next_result_id == 0U)
  {
    next_result_id = 1U;
  }
  last_result.result_id = next_result_id;
  last_result.mode = (uint8_t)mode;
  last_result.state = (uint8_t)state;
  last_result.tested_bitmap = tested_bitmap;
  last_result.passed_bitmap = passed_bitmap;
  result_pending = 1U;
}

static TestStatus run_with_retry(TestStatus (*action)(uint8_t), uint8_t index)
{
  TestStatus status;
  do
  {
    status = action(index);
  } while (status == TEST_STATUS_RETRY);
  return status;
}

static void execute_self_test(SelfTestMode mode)
{
  TestStatus status = TEST_STATUS_PASS;
  uint8_t index;
  uint8_t valve_count = AppParam_BoosterPumpUsesRs485() ?
      ((mode == SELF_TEST_MODE_DEEP) ? 6U : 2U) : 0U;

  tested_bitmap = 0U;
  passed_bitmap = 0U;
  completed_items = 0U;
  total_items = 3U + valve_count + (AppParam_BoosterPumpUsesRs485() ? 1U : 0U) +
                (mode == SELF_TEST_MODE_DEEP ? 1U : 0U);
  progress_x10 = 0U;
  current_step_id = 1U;
  TaskComm485_ReleaseNonzero(COMM485_INHIBIT_STARTUP |
                             COMM485_INHIBIT_SAFE_STOP |
                             COMM485_INHIBIT_MODE_CHANGE |
                             COMM485_INHIBIT_PAUSE);
  self_test_outputs_off();
  TaskComm485_ReleaseNonzero(COMM485_INHIBIT_SAFE_STOP);

  for (index = 0U; index < 1U; ++index)
  {
    status = run_with_retry(test_fan, index);
    if (status == TEST_STATUS_ABORT) break;
    item_done();
  }
  for (index = 0U; (index < 1U) && (status != TEST_STATUS_ABORT); ++index)
  {
    status = run_with_retry((mode == SELF_TEST_MODE_DEEP) ?
                            test_heater_deep : test_heater_quick, index);
    if (status == TEST_STATUS_ABORT) break;
    item_done();
  }
  if (status != TEST_STATUS_ABORT)
  {
    do { status = fill_to_middle(1U); } while (status == TEST_STATUS_RETRY);
    if (status != TEST_STATUS_ABORT) item_done();
  }
  for (index = 0U; (index < valve_count) && (status != TEST_STATUS_ABORT); ++index)
  {
    do { status = test_valve(&valve_tests[index], index); }
    while (status == TEST_STATUS_RETRY);
    if (status == TEST_STATUS_ABORT) break;
    item_done();
  }
  if (status != TEST_STATUS_ABORT && AppParam_BoosterPumpUsesRs485())
  {
    item_done();
  }
  if ((mode == SELF_TEST_MODE_DEEP) && (status != TEST_STATUS_ABORT))
  {
    do { status = test_hot_path(); } while (status == TEST_STATUS_RETRY);
    if (status != TEST_STATUS_ABORT) item_done();
  }
  self_test_outputs_off();
  if (status == TEST_STATUS_ABORT)
  {
    AutoDevice_AllDangerousOutputsOff();
    TaskComm485_RequestSafeStop();
  }
  if (status != TEST_STATUS_ABORT)
  {
    progress_x10 = 1000U;
  }
  publish_result(mode, (status == TEST_STATUS_ABORT) ?
                        SELF_TEST_RESULT_ABORTED : SELF_TEST_RESULT_COMPLETE);
}

static void SelfTestTask(void *argument)
{
  (void)argument;
  for (;;)
  {
    (void)osEventFlagsWait(self_test_event, SELF_TEST_EVENT_REQUEST,
                           osFlagsWaitAny, osWaitForever);
    execute_self_test((SelfTestMode)requested_mode);
    running = 0U;
    paused = 0U;
    stop_requested = 0U;
    current_step_id = 0U;
    AutoProcess_RequestReset();
  }
}

void TaskSelfTest_Create(void)
{
  running = 0U;
  paused = 0U;
  stop_requested = 0U;
  requested_mode = (uint8_t)SELF_TEST_MODE_QUICK;
  current_step_id = 0U;
  progress_x10 = 0U;
  result_pending = 0U;
  next_result_id = 0U;
  memset(&last_result, 0, sizeof(last_result));
  self_test_event = osEventFlagsNew(NULL);
  self_test_task_handle = osThreadNew(SelfTestTask, NULL,
                                      &self_test_task_attributes);
  (void)self_test_task_handle;
}

uint8_t TaskSelfTest_CanStart(SelfTestMode mode)
{
  TaskComm485EnvironmentData environment = {0};
  AutoSensorData sensors = {0};

  if (TaskComm485_HardwareConfigBusy() || (mode > SELF_TEST_MODE_DEEP) || (running != 0U) ||
      (TaskOutput_IsAutoMode() == 0U) ||
      (AutoProcess_IsRunning() != 0U) || (TaskClean_IsRunning() != 0U) ||
      (AutoDevice_GetFault() != AUTO_FAULT_NONE) ||
      (cabin_is_safe() == 0U) || (levels_are_consistent() == 0U))
  {
    return 0U;
  }
  if (mode == SELF_TEST_MODE_DEEP)
  {
    (void)TaskComm485_GetEnvironmentData(&environment);
    if ((environment.incab_valid == 0U) ||
        (AutoDevice_GetSensorData(&sensors) == 0U) ||
        (sensors.tank_temperature_valid == 0U))
    {
      return 0U;
    }
  }
  return 1U;
}

void TaskSelfTest_Request(SelfTestMode mode)
{
  if ((self_test_event != NULL) && (TaskSelfTest_CanStart(mode) != 0U))
  {
    running = 1U;
    stop_requested = 0U;
    paused = 0U;
    requested_mode = (uint8_t)mode;
    result_pending = 0U;
    (void)osEventFlagsSet(self_test_event, SELF_TEST_EVENT_REQUEST);
  }
}

void TaskSelfTest_Pause(void)
{
  if (running != 0U)
  {
    paused = 1U;
    self_test_outputs_off();
  }
}

void TaskSelfTest_Resume(void)
{
  if (running != 0U)
  {
    paused = 0U;
  }
}

void TaskSelfTest_Stop(void)
{
  if (running != 0U)
  {
    stop_requested = 1U;
    paused = 0U;
    self_test_outputs_off();
  }
}

uint8_t TaskSelfTest_IsRunning(void) { return running; }
uint8_t TaskSelfTest_IsPaused(void) { return paused; }
uint16_t TaskSelfTest_GetCurrentStepId(void) { return current_step_id; }
uint16_t TaskSelfTest_GetProgressX10(void) { return progress_x10; }

uint8_t TaskSelfTest_GetPendingResult(SelfTestResult *result)
{
  if ((result == NULL) || (result_pending == 0U))
  {
    return 0U;
  }
  *result = last_result;
  return 1U;
}

void TaskSelfTest_AcknowledgeResult(uint32_t result_id)
{
  if ((result_pending != 0U) && (last_result.result_id == result_id))
  {
    result_pending = 0U;
  }
}
