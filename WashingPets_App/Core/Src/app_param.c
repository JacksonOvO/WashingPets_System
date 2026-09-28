#include "app_param.h"

#include "auto_device.h"
#include "auto_process.h"
#include "auto_stages.h"
#include "rs485_config.h"
#include "task_clean.h"
#include "task_comm485.h"
#include "task_output.h"
#include "task_self_test.h"

#include <stddef.h>
#include <string.h>

#define APP_PARAM_SAVE_DELAY_MS 1000U
#define APP_RECIPE_MAX_TIME_MS  86400000U

_Static_assert(sizeof(AppRecipe_t) == 84U,
               "AppRecipe_t layout changed; update host parameter protocol");
_Static_assert(offsetof(AppRecipe_t, bath_cycle_count) == 24U,
               "bath cycle count must retain the legacy time field offset");
_Static_assert(offsetof(AppRecipe_t, bath_liquid_time_ms) == 36U,
               "bath dosing time must reuse the legacy soak field offset");

/*
 * 参数模块说明：
 * 1. 参数固定保存在 0x080C0000，也就是 STM32F407ZG 的 Flash Sector 10。
 * 2. 链接脚本已经把 App 代码结束地址限制到 0x080C0000，避免覆盖参数区。
 * 3. Bootloader 升级 App 时只要不擦除 Sector 10，参数就能保留。
 * 4. 参数改变后先打保存请求标记，由 AppParam_Task() 延时统一写入，
 *    避免按钮或上位机连续设置时频繁擦写 Flash。
 */
static AppParam_t app_param;
static AppParam_t param_write_image;
static volatile uint8_t save_requested;
static volatile uint8_t save_in_progress;
static volatile uint32_t param_change_counter;
static uint32_t save_request_tick;

#define APP_PARAM_LEGACY_V4             0x0004U
#define APP_PARAM_LEGACY_V5             0x0005U
#define APP_PARAM_LEGACY_V6             0x0006U
#define APP_PARAM_LEGACY_RECIPE_COUNT   4U

typedef struct
{
  int16_t tank_target_temp_x10;
  uint16_t tank_temp_tolerance_x10;
  uint16_t pipe_temp_tolerance_x10;
  uint8_t spray_pressure_level;
  uint8_t bath_pressure_level;
  uint8_t clean_pressure_level;
  uint8_t dry_fan_percent;
  int16_t dry_target_cabin_temp_x10;
  uint32_t spray_time_ms;
  uint32_t spray_on_time_ms;
  uint32_t spray_pause_time_ms;
  uint32_t bath_time_ms;
  uint32_t bath_on_time_ms;
  uint32_t bath_pause_time_ms;
  uint32_t liquid_soak_time_ms;
  uint32_t clean_time_ms;
  uint32_t dry_time_ms;
  uint32_t self_clean_top_time_ms;
  uint32_t self_clean_left_upper_time_ms;
  uint32_t self_clean_left_middle_time_ms;
  uint32_t self_clean_rear_time_ms;
  uint32_t self_clean_dry_time_ms;
} AppRecipeLegacyV6_t;

_Static_assert(sizeof(AppRecipeLegacyV6_t) == 68U,
               "legacy recipe layout must match V6 protocol");
_Static_assert(offsetof(AppRecipe_t, self_clean_liquid_pressure_x10) ==
               sizeof(AppRecipeLegacyV6_t),
               "new recipe fields must stay appended for migration");

typedef struct
{
  uint32_t magic;
  uint16_t version;
  uint16_t length;
  uint32_t crc32;
  uint8_t default_percent;
  uint8_t thyristor_station;
  uint16_t pwm_frequency;
  uint32_t auto_period_ms;
  uint32_t clean_period_ms;
  uint8_t active_recipe;
  uint8_t reserved0[3];
  AppRecipeLegacyV6_t recipes[APP_PARAM_LEGACY_RECIPE_COUNT];
  uint8_t reserved[16];
} AppParamLegacyV4_t;

typedef struct
{
  uint32_t magic;
  uint16_t version;
  uint16_t length;
  uint32_t crc32;
  uint8_t default_percent;
  uint8_t thyristor_station;
  uint16_t pwm_frequency;
  uint32_t auto_period_ms;
  uint32_t clean_period_ms;
  uint8_t active_recipe;
  uint8_t reserved0[3];
  AppRecipeLegacyV6_t recipes[APP_RECIPE_COUNT];
  uint8_t reserved[16];
} AppParamLegacyV5_t;

typedef struct
{
  uint32_t magic;
  uint16_t version;
  uint16_t length;
  uint32_t crc32;
  uint8_t default_percent;
  uint8_t thyristor_station;
  uint16_t pwm_frequency;
  uint32_t auto_period_ms;
  uint32_t clean_period_ms;
  uint8_t active_recipe;
  uint8_t reserved0[3];
  AppRecipeLegacyV6_t recipes[APP_RECIPE_COUNT];
  AppSelfTestConfig_t self_test;
  uint8_t reserved[16];
} AppParamLegacyV6_t;

static uint32_t read_u32_le(const uint8_t *data)
{
  return ((uint32_t)data[0]) |
         ((uint32_t)data[1] << 8U) |
         ((uint32_t)data[2] << 16U) |
         ((uint32_t)data[3] << 24U);
}

static void write_u32_le(uint8_t *data, uint32_t value)
{
  data[0] = (uint8_t)value;
  data[1] = (uint8_t)(value >> 8U);
  data[2] = (uint8_t)(value >> 16U);
  data[3] = (uint8_t)(value >> 24U);
}

/* 按字节计算CRC32，用于判断Flash参数是否完整；返回更新后的CRC值。 */
static uint32_t crc32_update(uint32_t crc, const uint8_t *data, uint32_t length)
{
  uint32_t bit;

  while (length-- > 0U)
  {
    crc ^= *data++;
    for (bit = 0U; bit < 8U; ++bit)
    {
      crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
  }
  return crc;
}

/* 计算参数有效数据区CRC，不包含magic、版本、长度及crc32字段本身。 */
static uint32_t param_crc32(const AppParam_t *param)
{
  const uint8_t *start = (const uint8_t *)&param->default_percent;
  uint32_t length = (uint32_t)sizeof(AppParam_t) -
                    (uint32_t)offsetof(AppParam_t, default_percent);

  return crc32_update(0xFFFFFFFFU, start, length) ^ 0xFFFFFFFFU;
}

static uint32_t legacy_v4_crc32(const AppParamLegacyV4_t *param)
{
  const uint8_t *start = (const uint8_t *)&param->default_percent;
  uint32_t length = (uint32_t)sizeof(AppParamLegacyV4_t) -
                    (uint32_t)offsetof(AppParamLegacyV4_t, default_percent);

  return crc32_update(0xFFFFFFFFU, start, length) ^ 0xFFFFFFFFU;
}

static uint32_t legacy_v5_crc32(const AppParamLegacyV5_t *param)
{
  const uint8_t *start = (const uint8_t *)&param->default_percent;
  uint32_t length = (uint32_t)sizeof(AppParamLegacyV5_t) -
                    (uint32_t)offsetof(AppParamLegacyV5_t, default_percent);

  return crc32_update(0xFFFFFFFFU, start, length) ^ 0xFFFFFFFFU;
}

static uint32_t legacy_v6_crc32(const AppParamLegacyV6_t *param)
{
  const uint8_t *start = (const uint8_t *)&param->default_percent;
  uint32_t length = (uint32_t)sizeof(AppParamLegacyV6_t) -
                    (uint32_t)offsetof(AppParamLegacyV6_t, default_percent);

  return crc32_update(0xFFFFFFFFU, start, length) ^ 0xFFFFFFFFU;
}

static void fill_self_clean_recipe_defaults(AppRecipe_t *recipe)
{
  recipe->self_clean_liquid_pressure_x10 = 40U;
  recipe->self_clean_rinse_pressure_x10 = 60U;
  recipe->self_clean_reserved[0] = 0U;
  recipe->self_clean_reserved[1] = 0U;
  recipe->self_clean_liquid_spray_time_ms = 15000U;
  recipe->self_clean_disinfect_spray_time_ms = 15000U;
  recipe->self_clean_soak_time_ms = 5000U;
}

static uint32_t legacy_bath_cycles(uint32_t total_ms, uint32_t on_ms, uint32_t pause_ms)
{
  uint64_t period = (uint64_t)on_ms + pause_ms;
  uint64_t cycles;

  if ((total_ms == 0U) || (on_ms == 0U)) return 0U;
  cycles = ((uint64_t)total_ms + period - 1U) / period;
  return cycles > APP_RECIPE_MAX_BATH_CYCLES ? APP_RECIPE_MAX_BATH_CYCLES : (uint32_t)cycles;
}

static void copy_legacy_recipe(AppRecipe_t *dst, const AppRecipeLegacyV6_t *src)
{
  memset(dst, 0, sizeof(*dst));
  memcpy(dst, src, sizeof(*src));
  dst->bath_cycle_count = legacy_bath_cycles(src->bath_time_ms, src->bath_on_time_ms,
                                            src->bath_pause_time_ms);
  dst->bath_liquid_time_ms = APP_RECIPE_DEFAULT_BATH_LIQUID_MS;
  fill_self_clean_recipe_defaults(dst);
}

uint8_t AppParam_ValidateRecipe(const AppRecipe_t *recipe)
{
  if (recipe == NULL)
  {
    return 0U;
  }
  if ((recipe->tank_target_temp_x10 < -400) ||
      (recipe->tank_target_temp_x10 > 1000) ||
      (recipe->tank_temp_tolerance_x10 > 1000U) ||
      (recipe->pipe_temp_tolerance_x10 > 1000U) ||
      (recipe->dry_target_cabin_temp_x10 < -400) ||
      (recipe->dry_target_cabin_temp_x10 > 1000))
  {
    return 0U;
  }
  if ((recipe->spray_pressure_level > 100U) ||
      (recipe->bath_pressure_level > 100U) ||
      (recipe->clean_pressure_level > 100U) ||
      (recipe->dry_fan_percent > 100U) ||
      (recipe->self_clean_liquid_pressure_x10 > 100U) ||
      (recipe->self_clean_rinse_pressure_x10 > 100U))
  {
    return 0U;
  }

#define RECIPE_TIME_TOO_LARGE(field) \
  ((recipe->field) > APP_RECIPE_MAX_TIME_MS)
  if (RECIPE_TIME_TOO_LARGE(spray_time_ms) ||
      RECIPE_TIME_TOO_LARGE(spray_on_time_ms) ||
      RECIPE_TIME_TOO_LARGE(spray_pause_time_ms) ||
      RECIPE_TIME_TOO_LARGE(bath_on_time_ms) ||
      RECIPE_TIME_TOO_LARGE(bath_pause_time_ms) ||
      RECIPE_TIME_TOO_LARGE(clean_time_ms) ||
      (recipe->bath_liquid_time_ms > APP_RECIPE_MAX_BATH_LIQUID_MS) ||
      RECIPE_TIME_TOO_LARGE(dry_time_ms) ||
      RECIPE_TIME_TOO_LARGE(self_clean_top_time_ms) ||
      RECIPE_TIME_TOO_LARGE(self_clean_left_upper_time_ms) ||
      RECIPE_TIME_TOO_LARGE(self_clean_left_middle_time_ms) ||
      RECIPE_TIME_TOO_LARGE(self_clean_rear_time_ms) ||
      RECIPE_TIME_TOO_LARGE(self_clean_dry_time_ms) ||
      RECIPE_TIME_TOO_LARGE(self_clean_liquid_spray_time_ms) ||
      RECIPE_TIME_TOO_LARGE(self_clean_disinfect_spray_time_ms) ||
      RECIPE_TIME_TOO_LARGE(self_clean_soak_time_ms))
  {
    return 0U;
  }
#undef RECIPE_TIME_TOO_LARGE
  if ((recipe->bath_cycle_count > APP_RECIPE_MAX_BATH_CYCLES) ||
      ((recipe->bath_cycle_count != 0U) &&
       ((recipe->bath_on_time_ms == 0U) ||
        ((uint64_t)recipe->bath_cycle_count * (recipe->bath_liquid_time_ms + recipe->bath_on_time_ms) +
         (uint64_t)(recipe->bath_cycle_count - 1U) * recipe->bath_pause_time_ms +
         BATH_STAGE_FIXED_MS > UINT32_MAX))))
  {
    return 0U;
  }
  return 1U;
}

static uint8_t validate_legacy_recipe(const AppRecipeLegacyV6_t *recipe)
{
  AppRecipe_t current;

  if ((recipe == NULL) || (recipe->bath_time_ms > APP_RECIPE_MAX_TIME_MS) ||
      (recipe->liquid_soak_time_ms > APP_RECIPE_MAX_TIME_MS))
  {
    return 0U;
  }
  copy_legacy_recipe(&current, recipe);
  return AppParam_ValidateRecipe(&current);
}

/*
 * 装载完整默认参数和四套默认动物配方。
 * 仅在Flash无有效参数或参数结构版本变化时使用，不会在每次开机覆盖用户参数。
 */
uint8_t AppParam_ValidateSelfTestConfig(const AppSelfTestConfig_t *config)
{
  if ((config == NULL) || (config->fast_fan_percent > 100U) ||
      (config->deep_heater_percent > 100U) || (config->deep_fan_percent > 100U) ||
      (config->valve_pressure_x10 > 100U) ||
      (config->pressure_min_x10 > config->pressure_max_x10) ||
      (config->pressure_max_x10 >= config->pressure_over_x10) ||
      (config->pressure_over_x10 > 100U) ||
      (config->proportional_valve_angle > 90U) ||
      (config->hot_trend_samples == 0U) || (config->hot_trend_samples > 20U) ||
      (config->heater_min_rise_x10 > 200U) ||
      (config->heater_max_temp_x10 < 100) || (config->heater_max_temp_x10 > 800) ||
      (config->communication_timeout_ms < 100U) ||
      (config->heater_observe_ms < 1000U) ||
      (config->pressure_stable_ms < 100U) ||
      (config->pressure_timeout_ms < config->pressure_stable_ms) ||
      (config->fill_timeout_ms < 1000U) || (config->hot_observe_ms < 1000U))
  {
    return 0U;
  }
  return 1U;
}

static void load_defaults(void)
{
  uint8_t index;
  AppRecipe_t default_recipe;

  memset(&app_param, 0, sizeof(app_param));
  app_param.magic = APP_PARAM_MAGIC;
  app_param.version = APP_PARAM_VERSION;
  app_param.length = (uint16_t)sizeof(AppParam_t);
  app_param.default_percent = 0U;
  app_param.thyristor_station = RS485_STATION_DEFAULT_THYRISTOR;
  app_param.pwm_frequency = 5U;
  app_param.auto_period_ms = 1000U;
  app_param.clean_period_ms = 60000U;
  app_param.active_recipe = (uint8_t)APP_RECIPE_SHORT_SMALL;
  app_param.self_test.fast_fan_percent = 50U;
  app_param.self_test.deep_heater_percent = 80U;
  app_param.self_test.deep_fan_percent = 30U;
  app_param.self_test.valve_pressure_x10 = 30U;
  app_param.self_test.pressure_min_x10 = 28U;
  app_param.self_test.pressure_max_x10 = 32U;
  app_param.self_test.pressure_over_x10 = 40U;
  app_param.self_test.proportional_valve_angle = 90U;
  app_param.self_test.hot_trend_samples = 3U;
  app_param.self_test.heater_min_rise_x10 = 5U;
  app_param.self_test.heater_max_temp_x10 = 450;
  app_param.self_test.communication_timeout_ms = 3000U;
  app_param.self_test.heater_observe_ms = 45000U;
  app_param.self_test.pressure_stable_ms = 2000U;
  app_param.self_test.pressure_timeout_ms = 10000U;
  app_param.self_test.fill_timeout_ms = 180000U;
  app_param.self_test.hot_observe_ms = 60000U;
  write_u32_le(&app_param.reserved[0], 0U);

  /*
   * 这些值只用于软件联调，现场测试前应由上位机写入正式工艺参数。
   * 四个配方先复制同一组安全、保守的默认值，后续可分别覆盖。
   */
  memset(&default_recipe, 0, sizeof(default_recipe));
  default_recipe.tank_target_temp_x10 = 400;//水箱目标温度，单位 0.1℃
  default_recipe.tank_temp_tolerance_x10 = 20U;//水箱水温容差，单位 0.1℃
  default_recipe.pipe_temp_tolerance_x10 = 20U;//管路水温容差，单位 0.1℃
  default_recipe.spray_pressure_level = 50U;//冲洗水压/增压泵频率百分比
  default_recipe.bath_pressure_level = 50U;//沐浴水压/增压泵频率百分比
  default_recipe.clean_pressure_level = 50U;//清洗水压/增压泵频率百分比
  default_recipe.dry_fan_percent = 100U;//烘干风机功率
  default_recipe.dry_target_cabin_temp_x10 = 335;//烘干目标舱内温度，单位 0.1℃
  default_recipe.spray_time_ms = 60000U;//冲洗总时间
  default_recipe.spray_on_time_ms = 20000U;//冲洗出水时间
  default_recipe.spray_pause_time_ms = 5000U;//冲洗暂停时间
  default_recipe.bath_cycle_count = 5U;//沐浴循环次数
  default_recipe.bath_on_time_ms = 20000U;//沐浴出水时间
  default_recipe.bath_pause_time_ms = 5000U;//沐浴暂停时间
  default_recipe.bath_liquid_time_ms = APP_RECIPE_DEFAULT_BATH_LIQUID_MS;//每轮沐浴加液时间
  default_recipe.clean_time_ms = 60000U;//清洗总时间
  default_recipe.dry_time_ms = 300000U;//烘干时间
  default_recipe.self_clean_top_time_ms = 15000U;//自清洁顶部清洁时间
  default_recipe.self_clean_left_upper_time_ms = 15000U;//自清洁左侧上层清洁时间
  default_recipe.self_clean_left_middle_time_ms = 15000U;//自清洁左侧夹层清洁时间
  default_recipe.self_clean_rear_time_ms = 15000U;//自清洁后端清洁时间
  default_recipe.self_clean_dry_time_ms = 300000U;//自清洁烘干时间
  fill_self_clean_recipe_defaults(&default_recipe);
  for (index = 0U; index < (uint8_t)APP_RECIPE_COUNT; ++index)
  {
    app_param.recipes[index] = default_recipe;
  }
  app_param.crc32 = param_crc32(&app_param);
}

/* 同时检查魔术值、版本、结构长度和CRC；全部正确才返回1。 */
static uint8_t flash_param_is_valid(const AppParam_t *param)
{
  uint8_t index;

  if ((param->magic != APP_PARAM_MAGIC) ||
      (param->version != APP_PARAM_VERSION) ||
      (param->length != sizeof(AppParam_t)) ||
      (param->default_percent > 100U) ||
      (param->thyristor_station == 0U) ||
      (param->thyristor_station > 247U) ||
      (param->active_recipe >= (uint8_t)APP_RECIPE_COUNT) ||
      (AppParam_ValidateSelfTestConfig(&param->self_test) == 0U))
  {
    return 0U;
  }

  if (param->crc32 != param_crc32(param))
  {
    return 0U;
  }
  for (index = 0U; index < (uint8_t)APP_RECIPE_COUNT; ++index)
  {
    if (AppParam_ValidateRecipe(&param->recipes[index]) == 0U)
    {
      return 0U;
    }
  }
  return 1U;
}

/* V7 used total milliseconds; V8 used cycles but still had fixed 3s dosing. */
static uint8_t migrate_legacy_v7_v8(const AppParam_t *legacy)
{
  if ((legacy->magic != APP_PARAM_MAGIC) ||
      ((legacy->version != 7U) && (legacy->version != 8U)) ||
      (legacy->length != sizeof(*legacy)) || (legacy->crc32 != param_crc32(legacy)))
  {
    return 0U;
  }
  memcpy(&param_write_image, legacy, sizeof(param_write_image));
  for (uint8_t index = 0U; index < APP_RECIPE_COUNT; ++index)
  {
    AppRecipe_t *recipe = &param_write_image.recipes[index];
    if (recipe->bath_liquid_time_ms > APP_RECIPE_MAX_TIME_MS) return 0U;
    if (legacy->version == 7U)
    {
      if (recipe->bath_cycle_count > APP_RECIPE_MAX_TIME_MS) return 0U;
      recipe->bath_cycle_count = legacy_bath_cycles(recipe->bath_cycle_count,
                                                  recipe->bath_on_time_ms,
                                                  recipe->bath_pause_time_ms);
    }
    recipe->bath_liquid_time_ms = APP_RECIPE_DEFAULT_BATH_LIQUID_MS;
  }
  param_write_image.version = APP_PARAM_VERSION;
  param_write_image.crc32 = param_crc32(&param_write_image);
  if (flash_param_is_valid(&param_write_image) == 0U) return 0U;
  memcpy(&app_param, &param_write_image, sizeof(app_param));
  return 1U;
}

static uint8_t migrate_legacy_v4(const AppParamLegacyV4_t *legacy)
{
  uint8_t index;

  if ((legacy->magic != APP_PARAM_MAGIC) ||
      (legacy->version != APP_PARAM_LEGACY_V4) ||
      (legacy->length != sizeof(AppParamLegacyV4_t)) ||
      (legacy->crc32 != legacy_v4_crc32(legacy)) ||
      (legacy->default_percent > 100U) ||
      (legacy->thyristor_station == 0U) ||
      (legacy->thyristor_station > 247U) ||
      (legacy->active_recipe >= APP_PARAM_LEGACY_RECIPE_COUNT))
  {
    return 0U;
  }

  for (index = 0U; index < APP_PARAM_LEGACY_RECIPE_COUNT; ++index)
  {
    if (validate_legacy_recipe(&legacy->recipes[index]) == 0U)
    {
      return 0U;
    }
  }

  load_defaults();
  app_param.default_percent = legacy->default_percent;
  app_param.thyristor_station = legacy->thyristor_station;
  app_param.pwm_frequency = legacy->pwm_frequency;
  app_param.auto_period_ms = legacy->auto_period_ms;
  app_param.clean_period_ms = legacy->clean_period_ms;
  app_param.active_recipe = legacy->active_recipe;
  for (index = 0U; index < APP_PARAM_LEGACY_RECIPE_COUNT; ++index)
  {
    copy_legacy_recipe(&app_param.recipes[index], &legacy->recipes[index]);
  }
  memcpy(app_param.reserved, legacy->reserved, sizeof(legacy->reserved));
  app_param.crc32 = param_crc32(&app_param);
  return 1U;
}

static uint8_t migrate_legacy_v5(const AppParamLegacyV5_t *legacy)
{
  uint8_t index;

  if ((legacy->magic != APP_PARAM_MAGIC) || (legacy->version != APP_PARAM_LEGACY_V5) ||
      (legacy->length != sizeof(AppParamLegacyV5_t)) ||
      (legacy->crc32 != legacy_v5_crc32(legacy)) ||
      (legacy->default_percent > 100U) || (legacy->thyristor_station == 0U) ||
      (legacy->thyristor_station > 247U) ||
      (legacy->active_recipe >= (uint8_t)APP_RECIPE_COUNT))
  {
    return 0U;
  }
  for (index = 0U; index < (uint8_t)APP_RECIPE_COUNT; ++index)
  {
    if (validate_legacy_recipe(&legacy->recipes[index]) == 0U)
    {
      return 0U;
    }
  }
  load_defaults();
  app_param.default_percent = legacy->default_percent;
  app_param.thyristor_station = legacy->thyristor_station;
  app_param.pwm_frequency = legacy->pwm_frequency;
  app_param.auto_period_ms = legacy->auto_period_ms;
  app_param.clean_period_ms = legacy->clean_period_ms;
  app_param.active_recipe = legacy->active_recipe;
  for (index = 0U; index < (uint8_t)APP_RECIPE_COUNT; ++index)
  {
    copy_legacy_recipe(&app_param.recipes[index], &legacy->recipes[index]);
  }
  memcpy(app_param.reserved, legacy->reserved, sizeof(legacy->reserved));
  app_param.crc32 = param_crc32(&app_param);
  return 1U;
}

static uint8_t migrate_legacy_v6(const AppParamLegacyV6_t *legacy)
{
  uint8_t index;

  if ((legacy->magic != APP_PARAM_MAGIC) ||
      (legacy->version != APP_PARAM_LEGACY_V6) ||
      (legacy->length != sizeof(AppParamLegacyV6_t)) ||
      (legacy->crc32 != legacy_v6_crc32(legacy)) ||
      (legacy->default_percent > 100U) ||
      (legacy->thyristor_station == 0U) ||
      (legacy->thyristor_station > 247U) ||
      (legacy->active_recipe >= (uint8_t)APP_RECIPE_COUNT) ||
      (AppParam_ValidateSelfTestConfig(&legacy->self_test) == 0U))
  {
    return 0U;
  }
  for (index = 0U; index < (uint8_t)APP_RECIPE_COUNT; ++index)
  {
    if (validate_legacy_recipe(&legacy->recipes[index]) == 0U)
    {
      return 0U;
    }
  }
  load_defaults();
  app_param.default_percent = legacy->default_percent;
  app_param.thyristor_station = legacy->thyristor_station;
  app_param.pwm_frequency = legacy->pwm_frequency;
  app_param.auto_period_ms = legacy->auto_period_ms;
  app_param.clean_period_ms = legacy->clean_period_ms;
  app_param.active_recipe = legacy->active_recipe;
  for (index = 0U; index < (uint8_t)APP_RECIPE_COUNT; ++index)
  {
    copy_legacy_recipe(&app_param.recipes[index], &legacy->recipes[index]);
  }
  app_param.self_test = legacy->self_test;
  memcpy(app_param.reserved, legacy->reserved, sizeof(legacy->reserved));
  app_param.crc32 = param_crc32(&app_param);
  return 1U;
}

/* 初始化参数模块：优先加载Flash，校验失败则恢复默认值并立即保存。 */
void AppParam_Init(void)
{
  const AppParam_t *flash_param = (const AppParam_t *)PARAM_FLASH_ADDRESS;

  save_requested = 0U;
  save_in_progress = 0U;
  param_change_counter = 0U;

  if (flash_param_is_valid(flash_param) != 0U)
  {
    memcpy(&app_param, flash_param, sizeof(app_param));
  }
  else if (migrate_legacy_v7_v8(flash_param) != 0U)
  {
    AppParam_RequestSave();
  }
  else if (migrate_legacy_v6((const AppParamLegacyV6_t *)flash_param) != 0U)
  {
    AppParam_RequestSave();
  }
  else if (migrate_legacy_v5((const AppParamLegacyV5_t *)flash_param) != 0U)
  {
    AppParam_RequestSave();
  }
  else if (migrate_legacy_v4((const AppParamLegacyV4_t *)flash_param) != 0U)
  {
    AppParam_RequestSave();
  }
  else
  {
    load_defaults();
    if (flash_param->magic != APP_PARAM_MAGIC)
    {
      AppParam_RequestSave();
    }
  }
}

/* 返回整个只读参数结构地址，调用者不得绕过设置接口直接修改。 */
const AppParam_t *AppParam_Get(void)
{
  return &app_param;
}

uint32_t AppParam_GetRevision(void)
{
  return read_u32_le(&app_param.reserved[0]);
}

void AppParam_SetRevision(uint32_t revision)
{
  write_u32_le(&app_param.reserved[0], revision);
}

/*
 * 立即擦除Sector 10并逐字节写入完整参数。
 * 返回HAL状态；频繁调用会消耗Flash寿命，普通修改应使用延时保存请求。
 */
HAL_StatusTypeDef AppParam_SaveNow(void)
{
  FLASH_EraseInitTypeDef erase = {0};
  uint32_t sector_error = 0U;
  uint32_t address;
  const uint32_t *data;
  HAL_StatusTypeDef status;
  uint32_t change_counter;
  uint32_t primask;

  if (AppParam_CanSaveNow() == 0U)
  {
    return HAL_BUSY;
  }

  primask = __get_PRIMASK();
  __disable_irq();
  if (save_in_progress != 0U)
  {
    if (primask == 0U)
    {
      __enable_irq();
    }
    return HAL_BUSY;
  }
  save_in_progress = 1U;

  app_param.magic = APP_PARAM_MAGIC;
  app_param.version = APP_PARAM_VERSION;
  app_param.length = (uint16_t)sizeof(AppParam_t);
  app_param.crc32 = param_crc32(&app_param);
  memcpy(&param_write_image, &app_param, sizeof(param_write_image));
  change_counter = param_change_counter;
  if (primask == 0U)
  {
    __enable_irq();
  }
  data = (const uint32_t *)&param_write_image;

  erase.TypeErase = FLASH_TYPEERASE_SECTORS;
  erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
  erase.Sector = FLASH_SECTOR_10;
  erase.NbSectors = 1U;

  HAL_FLASH_Unlock();
  status = HAL_FLASHEx_Erase(&erase, &sector_error);
  if (status == HAL_OK)
  {
    for (address = 0U; address < sizeof(AppParam_t); address += sizeof(uint32_t))
    {
      status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                                 PARAM_FLASH_ADDRESS + address,
                                 data[address / sizeof(uint32_t)]);
      if (status != HAL_OK)
      {
        break;
      }
    }
  }
  HAL_FLASH_Lock();

  primask = __get_PRIMASK();
  __disable_irq();
  if ((status == HAL_OK) && (change_counter == param_change_counter))
  {
    save_requested = 0U;
  }
  else if (status != HAL_OK)
  {
    save_request_tick = HAL_GetTick();
  }
  save_in_progress = 0U;
  if (primask == 0U)
  {
    __enable_irq();
  }
  return status;
}

uint8_t AppParam_CanSaveNow(void)
{
#if (APP_DRY_RUN_ENABLE != 0U)
  /*
   * 空跑模式只模拟传感器和执行器，实际 GPIO/485 不会动作。
   * 此时输出缓存可能长期保持“热水器常开”等模拟状态，不能用它阻止参数写 Flash。
   */
  if ((AutoProcess_IsRunning() != 0U) ||
      (TaskClean_IsRunning() != 0U) ||
      (TaskSelfTest_IsRunning() != 0U) ||
      (AutoDevice_IsPaused() != 0U))
  {
    return 0U;
  }
  return 1U;
#else
  static const uint8_t thyristor_stations[] = {
    RS485_STATION_LEFT_FAN, RS485_STATION_RIGHT_FAN,
    RS485_STATION_BOTTOM_FAN, RS485_STATION_TOP_EXHAUST_FAN,
    RS485_STATION_LEFT_HEATER, RS485_STATION_RIGHT_HEATER,
    RS485_STATION_BOTTOM_HEATER
  };
  uint8_t index;

  if ((AutoProcess_IsRunning() != 0U) ||
      (TaskClean_IsRunning() != 0U) ||
      (TaskSelfTest_IsRunning() != 0U) ||
      (AutoDevice_IsPaused() != 0U) ||
      (TaskOutput_HasDangerousOutputActive() != 0U) ||
      (TaskComm485_GetLastBoosterPumpRunning() != 0U) ||
      (TaskComm485_GetLastProportionalValveAngle() != 0U))
  {
    return 0U;
  }
  for (index = 0U; index < (uint8_t)(sizeof(thyristor_stations) /
                                      sizeof(thyristor_stations[0])); ++index)
  {
    if (TaskComm485_GetLastPercent(thyristor_stations[index]) != 0U)
    {
      return 0U;
    }
  }
  return 1U;
#endif
}

/* 提交延时保存请求，把短时间内连续修改合并成一次Flash擦写。 */
void AppParam_RequestSave(void)
{
  uint32_t primask;

  /*
   * 这里只记录请求，不立刻擦写 Flash。
   * 真正写入动作由周期任务处理，可以把短时间内多次修改合并成一次写入。
   */
  primask = __get_PRIMASK();
  __disable_irq();
  save_requested = 1U;
  param_change_counter++;
  save_request_tick = HAL_GetTick();
  if (primask == 0U)
  {
    __enable_irq();
  }
}

/* 周期处理保存请求；请求保持1秒后才真正写入Flash。 */
void AppParam_Task(void)
{
  if ((save_requested != 0U) &&
      ((HAL_GetTick() - save_request_tick) >= APP_PARAM_SAVE_DELAY_MS))
  {
    (void)AppParam_SaveNow();
  }
}

/* 设置默认可控硅百分比，自动限制到0～100并请求保存。 */
void AppParam_SetDefaultPercent(uint8_t percent)
{
  if (percent > 100U)
  {
    percent = 100U;
  }
  if (app_param.default_percent != percent)
  {
    app_param.default_percent = percent;
    AppParam_RequestSave();
  }
}

/* 设置默认可控硅Modbus站号，仅接受1～247。 */
void AppParam_SetThyristorStation(uint8_t station)
{
  if ((station == 0U) || (station > 247U))
  {
    return;
  }
  if (app_param.thyristor_station != station)
  {
    app_param.thyristor_station = station;
    AppParam_RequestSave();
  }
}

void AppParam_SetGlobalConfig(uint8_t default_percent,
                              uint8_t thyristor_station,
                              uint16_t pwm_frequency,
                              uint32_t auto_period_ms,
                              uint32_t clean_period_ms)
{
  if (default_percent > 100U)
  {
    default_percent = 100U;
  }
  if ((thyristor_station == 0U) || (thyristor_station > 247U))
  {
    thyristor_station = RS485_STATION_DEFAULT_THYRISTOR;
  }

  app_param.default_percent = default_percent;
  app_param.thyristor_station = thyristor_station;
  app_param.pwm_frequency = pwm_frequency;
  app_param.auto_period_ms = auto_period_ms;
  app_param.clean_period_ms = clean_period_ms;
}

/*
 * 返回当前自动流程配方。
 * 如果Flash中的选择编号异常，则安全回退到“短毛小型”配方。
 */
const AppRecipe_t *AppParam_GetActiveRecipe(void)
{
  uint8_t recipe = app_param.active_recipe;
  if (recipe >= (uint8_t)APP_RECIPE_COUNT)
  {
    recipe = (uint8_t)APP_RECIPE_SHORT_SMALL;
  }
  return &app_param.recipes[recipe];
}

/* 按编号读取指定配方；编号越界返回NULL。 */
const AppRecipe_t *AppParam_GetRecipe(AppRecipeId recipe_id)
{
  if (recipe_id >= APP_RECIPE_COUNT)
  {
    return NULL;
  }
  return &app_param.recipes[recipe_id];
}

/* 选择本次自动流程使用的动物配方，并提交Flash延时保存请求。 */
void AppParam_SetActiveRecipe(AppRecipeId recipe_id)
{
  if ((recipe_id < APP_RECIPE_COUNT) &&
      (app_param.active_recipe != (uint8_t)recipe_id))
  {
    app_param.active_recipe = (uint8_t)recipe_id;
    AppParam_RequestSave();
  }
}

void AppParam_SelectActiveRecipe(AppRecipeId recipe_id)
{
  if (recipe_id < APP_RECIPE_COUNT)
  {
    app_param.active_recipe = (uint8_t)recipe_id;
  }
}

/*
 * 用上位机下发的数据完整更新一套配方。
 * recipe_id为目标槽位，recipe为新参数；校验通过返回HAL_OK并请求保存。
 */
HAL_StatusTypeDef AppParam_SetRecipe(AppRecipeId recipe_id,
                                    const AppRecipe_t *recipe)
{
  if ((recipe_id >= APP_RECIPE_COUNT) ||
      (AppParam_ValidateRecipe(recipe) == 0U))
  {
    return HAL_ERROR;
  }
  app_param.recipes[recipe_id] = *recipe;
  AppParam_RequestSave();
  return HAL_OK;
}

HAL_StatusTypeDef AppParam_SetRecipeDirect(AppRecipeId recipe_id,
                                          const AppRecipe_t *recipe)
{
  if ((recipe_id >= APP_RECIPE_COUNT) ||
      (AppParam_ValidateRecipe(recipe) == 0U))
  {
    return HAL_ERROR;
  }
  app_param.recipes[recipe_id] = *recipe;
  return HAL_OK;
}

const AppSelfTestConfig_t *AppParam_GetSelfTestConfig(void)
{
  return &app_param.self_test;
}

HAL_StatusTypeDef AppParam_SetSelfTestConfigDirect(const AppSelfTestConfig_t *config)
{
  if (AppParam_ValidateSelfTestConfig(config) == 0U)
  {
    return HAL_ERROR;
  }
  app_param.self_test = *config;
  return HAL_OK;
}

void AppParam_RestoreDefaults(void)
{
  uint32_t revision = AppParam_GetRevision();

  load_defaults();
  AppParam_SetRevision(revision + 1U);
  AppParam_RequestSave();
}
