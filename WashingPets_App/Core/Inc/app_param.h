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
#define APP_PARAM_VERSION    0x0005U

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
  uint32_t bath_time_ms;                 /* 沐浴总时间 */
  uint32_t bath_on_time_ms;              /* 沐浴出水时间 */
  uint32_t bath_pause_time_ms;           /* 沐浴暂停时间 */
  uint32_t liquid_soak_time_ms;          /* 洗剂浸润时间 */
  uint32_t clean_time_ms;                /* 清洗总时间 */
  uint32_t dry_time_ms;                  /* 烘干时间 */
  uint32_t self_clean_top_time_ms;         /* 自清洁顶部清洁时间 */
  uint32_t self_clean_left_upper_time_ms;  /* 自清洁左侧上层清洁时间 */
  uint32_t self_clean_left_middle_time_ms; /* 自清洁左侧夹层清洁时间 */
  uint32_t self_clean_rear_time_ms;        /* 自清洁后端清洁时间 */
  uint32_t self_clean_dry_time_ms;         /* 自清洁烘干时间 */
} AppRecipe_t;

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
  uint8_t reserved[16];
} AppParam_t;

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
void AppParam_RestoreDefaults(void);

#endif
