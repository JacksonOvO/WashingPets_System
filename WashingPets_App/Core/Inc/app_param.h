#ifndef APP_PARAM_H
#define APP_PARAM_H

#include "stm32f4xx_hal.h"

/*
 * 参数保存区固定在 STM32F407ZG 内部 Flash Sector 10。
 *
 * 地址范围：
 *   起始：0x080C0000
 *   大小：0x20000，即 128KB
 *
 * 说明：
 * - APP 链接脚本已经把程序区域限制在 0x08004000 ~ 0x080BFFFF。
 * - Bootloader 在线升级 APP 时不要擦除 Sector 10，参数即可跨升级保留。
 * - 如果用调试器/下载器执行整片擦除，内部 Flash 全部会被擦掉，参数也会丢失。
 */
#define PARAM_FLASH_ADDRESS  0x080C0000U
#define PARAM_FLASH_SIZE     0x20000U

#define APP_PARAM_MAGIC      0x4150524DU  /* "APRM" */
#define APP_PARAM_VERSION    0x000BU
#define APP_RECIPE_PROTOCOL_VERSION 3U
#define APP_RECIPE_DEFAULT_BATH_LIQUID_MS 3000U
#define APP_RECIPE_MAX_BATH_LIQUID_MS 60000U
#define APP_RECIPE_MAX_BATH_CYCLES 100U

/* 四种基础动物工艺配方，由上位机按毛长和体型选择。 */
typedef enum
{
  APP_RECIPE_SHORT_SMALL = 0, /* 短毛、小型 */
  APP_RECIPE_SHORT_LARGE,     /* 短毛、大型 */
  APP_RECIPE_LONG_SMALL,      /* 长毛、小型 */
  APP_RECIPE_LONG_LARGE,      /* 长毛、大型 */
  APP_RECIPE_COUNT = 26
} AppRecipeId;

/*
 * 单套自动工艺参数。
 * 温度统一使用0.1℃，时间统一使用毫秒，功率和阀门开度使用0～100%。
 * 上位机收到不同动物类型后，修改对应配方并调用AppParam_SetActiveRecipe()。
 */
typedef struct
{
  int16_t tank_target_temp_x10;          /* 水箱目标温度，单位 0.1℃ */
  uint16_t tank_temp_tolerance_x10;      /* 水箱水温容差，单位 0.1℃ */
  uint16_t pipe_temp_tolerance_x10;      /* 管路水温容差，单位 0.1℃ */
  uint8_t spray_pressure_level;          /* 冲洗水压，单位0.1kg，15表示1.5kg */
  uint8_t bath_pressure_level;           /* 沐浴水压，单位0.1kg，15表示1.5kg */
  uint8_t clean_pressure_level;          /* 清洗水压，单位0.1kg，15表示1.5kg */
  uint8_t dry_fan_percent;               /* 烘干风机功率，0~100% */
  int16_t dry_target_cabin_temp_x10;     /* 烘干目标舱内温度，单位 0.1℃ */
  uint32_t spray_time_ms;                /* 冲洗总时间 */
  uint32_t spray_on_time_ms;             /* 冲洗出水时间 */
  uint32_t spray_pause_time_ms;          /* 冲洗暂停时间 */
  uint32_t bath_cycle_count;             /* 沐浴循环次数，0跳过，最大100 */
  uint32_t bath_on_time_ms;              /* 沐浴出水时间 */
  uint32_t bath_pause_time_ms;           /* 沐浴暂停时间 */
  uint32_t bath_liquid_time_ms;          /* 每轮沐浴加液时间，0跳过投液，默认3000ms */
  uint32_t clean_time_ms;                /* 清洗总时间 */
  uint32_t dry_time_ms;                  /* 烘干时间 */
  uint32_t self_clean_top_time_ms;         /* 自清洁顶部清洁时间 */
  uint32_t self_clean_left_upper_time_ms;  /* 自清洁左侧上层清洁时间 */
  uint32_t self_clean_left_middle_time_ms; /* 自清洁左侧夹层清洁时间 */
  uint32_t self_clean_rear_time_ms;        /* 自清洁后端清洁时间 */
  uint32_t self_clean_dry_time_ms;         /* 自清洁烘干时间 */
  uint8_t self_clean_liquid_pressure_x10;  /* 自清洁药液喷洒压力，单位0.1kg */
  uint8_t self_clean_rinse_pressure_x10;   /* 自清洁清水冲洗压力，单位0.1kg */
  uint8_t self_clean_reserved[2];          /* 对齐保留 */
  uint32_t self_clean_liquid_spray_time_ms;      /* 自清洁清洁液喷洒时间 */
  uint32_t self_clean_disinfect_spray_time_ms;   /* 自清洁消毒液喷洒时间 */
  uint32_t self_clean_soak_time_ms;              /* 自清洁药液静置时间 */
} AppRecipe_t;

typedef struct
{
  uint8_t fast_fan_percent;
  uint8_t deep_heater_percent;
  uint8_t deep_fan_percent;
  uint8_t valve_pressure_x10;
  uint8_t pressure_min_x10;
  uint8_t pressure_max_x10;
  uint8_t pressure_over_x10;
  uint8_t proportional_valve_angle;
  uint8_t hot_trend_samples;
  uint8_t reserved0[3];
  uint16_t heater_min_rise_x10;
  int16_t heater_max_temp_x10;
  uint32_t communication_timeout_ms;
  uint32_t heater_observe_ms;
  uint32_t pressure_stable_ms;
  uint32_t pressure_timeout_ms;
  uint32_t fill_timeout_ms;
  uint32_t hot_observe_ms;
} AppSelfTestConfig_t;

_Static_assert(sizeof(AppSelfTestConfig_t) == 40U, "self-test protocol size");

typedef struct
{
  uint32_t magic;              /* 固定魔术值，用于判断参数区是否已经初始化 */
  uint16_t version;            /* 参数结构版本，结构变化时递增 */
  uint16_t length;             /* 整个 AppParam_t 的长度，用于结构兼容判断 */
  uint32_t crc32;              /* 从 default_percent 到结构末尾的 CRC32 */

  uint8_t default_percent;     /* 上电默认可控硅电压百分比，范围 0~100 */
  uint8_t thyristor_station;   /* 可控硅 Modbus 站号，默认 1 */
  uint16_t pwm_frequency;      /* 可控硅 PWM 频率配置，当前预留 */
  uint32_t auto_period_ms;     /* 自动流程周期参数，当前预留 */
  uint32_t clean_period_ms;    /* 自清洁周期参数，当前预留 */
  uint8_t active_recipe;       /* 当前自动流程使用的AppRecipeId。 */
  uint8_t reserved0[3];
  AppRecipe_t recipes[APP_RECIPE_COUNT];
  AppSelfTestConfig_t self_test;
  uint8_t reserved[16];
} AppParam_t;

/* Stored in reserved[4..7]; reserved[0..3] remains the shared revision. */
typedef struct
{
  uint8_t fan_pwm;
  uint8_t temperature_mask;
  uint8_t lower_door_bypass;
  uint8_t host_command_bypass;
} AppHardwareConfig_t;
_Static_assert(sizeof(AppHardwareConfig_t) == 4U, "hardware protocol size");

uint8_t AppParam_BoosterPumpUsesRs485(void);
void AppParam_CommitBoosterPumpMode(uint8_t mode);
const AppHardwareConfig_t *AppParam_GetHardwareConfig(void);
uint8_t AppParam_ValidateHardwareConfig(const AppHardwareConfig_t *config);
uint8_t AppParam_HardwareConfigAllowed(void);
/* Only the comm485 configuration transaction may commit a live change. */
void AppParam_CommitHardwareConfig(const AppHardwareConfig_t *config);
uint8_t AppParam_IsSavePending(void);

void AppParam_Init(void);
const AppParam_t *AppParam_Get(void);
uint32_t AppParam_GetRevision(void);
void AppParam_SetRevision(uint32_t revision);
HAL_StatusTypeDef AppParam_SaveNow(void);
uint8_t AppParam_CanSaveNow(void);
void AppParam_RequestSave(void);
void AppParam_Task(void);

void AppParam_SetDefaultPercent(uint8_t percent);
void AppParam_SetThyristorStation(uint8_t station);
void AppParam_SetGlobalConfig(uint8_t default_percent,
                              uint8_t thyristor_station,
                              uint16_t pwm_frequency,
                              uint32_t auto_period_ms,
                              uint32_t clean_period_ms);
const AppRecipe_t *AppParam_GetActiveRecipe(void);
const AppRecipe_t *AppParam_GetRecipe(AppRecipeId recipe_id);
void AppParam_SetActiveRecipe(AppRecipeId recipe_id);
void AppParam_SelectActiveRecipe(AppRecipeId recipe_id);
HAL_StatusTypeDef AppParam_SetRecipe(AppRecipeId recipe_id,
                                    const AppRecipe_t *recipe);
HAL_StatusTypeDef AppParam_SetRecipeDirect(AppRecipeId recipe_id,
                                          const AppRecipe_t *recipe);
uint8_t AppParam_ValidateRecipe(const AppRecipe_t *recipe);
const AppSelfTestConfig_t *AppParam_GetSelfTestConfig(void);
uint8_t AppParam_ValidateSelfTestConfig(const AppSelfTestConfig_t *config);
HAL_StatusTypeDef AppParam_SetSelfTestConfigDirect(const AppSelfTestConfig_t *config);
void AppParam_RestoreDefaults(void);

#endif
