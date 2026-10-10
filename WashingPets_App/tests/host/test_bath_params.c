#include <assert.h>
#include <stdio.h>
#include "../../Core/Src/app_param.c"

static uint8_t automatic, emergency, running, paused, dangerous, percent;
uint32_t HAL_GetTick(void) { return 100U; }
uint32_t __get_PRIMASK(void) { return 0U; }
void __disable_irq(void) { }
void __enable_irq(void) { }
uint8_t TaskOutput_IsAutoMode(void) { return automatic; }
uint8_t AutoDevice_IsEmergencyStopped(void) { return emergency; }
uint8_t AutoProcess_IsRunning(void) { return running; }
uint8_t TaskClean_IsRunning(void) { return running; }
uint8_t TaskSelfTest_IsRunning(void) { return running; }
uint8_t AutoDevice_IsPaused(void) { return paused; }
uint8_t TaskOutput_HasDangerousOutputActive(void) { return dangerous; }
uint8_t TaskComm485_GetLastBoosterPumpRunning(void) { return 0U; }
uint8_t TaskComm485_GetLastProportionalValveAngle(void) { return 0U; }
uint8_t TaskComm485_GetLastPercent(uint8_t station) { (void)station; return percent; }

int main(void)
{
  load_defaults();
  assert(sizeof(AppRecipe_t) == 84);
  assert(offsetof(AppRecipe_t, bath_cycle_count) == 24);
  assert(offsetof(AppRecipe_t, bath_liquid_time_ms) == 36);
  assert(app_param.recipes[0].bath_liquid_time_ms == 3000);
  assert(AppParam_ValidateRecipe(&app_param.recipes[0]));
  AppParam_t old = app_param;
  old.version = 7;
  old.reserved[0] = 123;
  old.active_recipe = 7;
  for (unsigned i = 0; i < APP_RECIPE_COUNT; ++i)
  {
    old.recipes[i].bath_cycle_count = 120000 + i * 25000;
    old.recipes[i].dry_time_ms += i;
    old.recipes[i].bath_liquid_time_ms = i * 1000;
  }
  old.crc32 = param_crc32(&old);
  assert(migrate_legacy_v7_v8(&old));
  assert(flash_param_is_valid(&app_param));
  assert(app_param.reserved[0] == 123 && app_param.active_recipe == 7);
  assert(!memcmp(&old.self_test, &app_param.self_test, sizeof(old.self_test)));
  for (unsigned i = 0; i < APP_RECIPE_COUNT; ++i)
  {
    AppRecipe_t expected = old.recipes[i];
    expected.bath_cycle_count = 5 + i;
    expected.bath_liquid_time_ms = 3000;
    assert(!memcmp(&expected, &app_param.recipes[i], sizeof(expected)));
  }
  old.crc32 ^= 1;
  assert(!migrate_legacy_v7_v8(&old));
  AppParam_t old8 = app_param;
  old8.version = 8;
  for (unsigned i = 0; i < APP_RECIPE_COUNT; ++i)
    old8.recipes[i].bath_liquid_time_ms = i * 1000;
  old8.crc32 = param_crc32(&old8);
  assert(migrate_legacy_v7_v8(&old8));
  for (unsigned i = 0; i < APP_RECIPE_COUNT; ++i)
  {
    AppRecipe_t expected = old8.recipes[i];
    expected.bath_liquid_time_ms = 3000;
    assert(!memcmp(&expected, &app_param.recipes[i], sizeof(expected)));
  }
  app_param.recipes[0].bath_liquid_time_ms = 7500;
  app_param.crc32 = param_crc32(&app_param);
  assert(flash_param_is_valid(&app_param));
  assert(!migrate_legacy_v7_v8(&app_param));
  assert(legacy_bath_cycles(0, 20000, 5000) == 0);
  assert(legacy_bath_cycles(120000, 0, 5000) == 0);
  assert(legacy_bath_cycles(120000, 20000, 5000) == 5);
  assert(legacy_bath_cycles(86400000, 1, 0) == 100);
  AppParamLegacyV6_t old6 = {0};
  AppParamLegacyV5_t old5 = {0};
  AppParamLegacyV4_t old4 = {0};
  memcpy(&old6, &old, offsetof(AppParam_t, recipes));
  old6.version = 6;
  old6.length = sizeof(old6);
  old6.self_test = old.self_test;
  memcpy(old6.reserved, old.reserved, sizeof(old.reserved));
  for (unsigned i = 0; i < APP_RECIPE_COUNT; ++i)
    memcpy(&old6.recipes[i], &old.recipes[i], sizeof(old6.recipes[i]));
  old6.crc32 = legacy_v6_crc32(&old6);
  assert(migrate_legacy_v6(&old6));
  assert(app_param.recipes[25].bath_cycle_count == 30);
  assert(app_param.reserved[0] == 123 && flash_param_is_valid(&app_param));
  memcpy(&old5, &old6, offsetof(AppParamLegacyV5_t, reserved));
  old5.version = 5;
  old5.length = sizeof(old5);
  old5.crc32 = legacy_v5_crc32(&old5);
  assert(migrate_legacy_v5(&old5));
  assert(app_param.recipes[25].bath_cycle_count == 30 && flash_param_is_valid(&app_param));
  memcpy(&old4, &old5, offsetof(AppParamLegacyV4_t, reserved));
  old4.version = 4;
  old4.length = sizeof(old4);
  old4.active_recipe = 2;
  old4.crc32 = legacy_v4_crc32(&old4);
  assert(migrate_legacy_v4(&old4));
  assert(app_param.recipes[3].bath_cycle_count == 8 && flash_param_is_valid(&app_param));
  AppRecipeLegacyV6_t v6;
  memcpy(&v6, &old.recipes[0], sizeof(v6));
  v6.bath_time_ms = 120000;
  AppRecipe_t converted;
  copy_legacy_recipe(&converted, &v6);
  assert(converted.bath_cycle_count == 5 && AppParam_ValidateRecipe(&converted));
  converted.bath_cycle_count = 101;
  assert(!AppParam_ValidateRecipe(&converted));
  converted.bath_cycle_count = 1;
  converted.bath_on_time_ms = 0;
  assert(!AppParam_ValidateRecipe(&converted));
  converted.bath_cycle_count = 0;
  assert(AppParam_ValidateRecipe(&converted));
  assert(converted.bath_liquid_time_ms == 3000);
  converted.bath_liquid_time_ms = 60001;
  assert(!AppParam_ValidateRecipe(&converted));
  converted.bath_liquid_time_ms = 60000;
  assert(AppParam_ValidateRecipe(&converted));
  converted.bath_liquid_time_ms = 0;
  assert(AppParam_ValidateRecipe(&converted));
  AppParam_t old9 = app_param;
  old9.version = 9U;
  memset(&old9.reserved[4], 0xA5, 4U);
  old9.recipes[0].bath_liquid_time_ms = 7800U;
  old9.crc32 = param_crc32(&old9);
  assert(migrate_legacy_v7_v8(&old9));
  assert(flash_param_is_valid(&app_param));
  assert(!memcmp(old9.recipes, app_param.recipes, sizeof(old9.recipes)));
  assert(!memcmp(old9.reserved, app_param.reserved, 4U));
  AppHardwareConfig_t defaults = {0U, 11U, 0U, 0U};
  assert(sizeof(defaults) == 4U);
  assert(!memcmp(AppParam_GetHardwareConfig(), &defaults, 4U));
  AppHardwareConfig_t config = {1U, 15U, 1U, 1U};
  assert(AppParam_ValidateHardwareConfig(&config));
  assert(!AppParam_ValidateHardwareConfig(NULL));
  for (unsigned field = 0U; field < 4U; ++field)
  {
    AppHardwareConfig_t invalid = config;
    ((uint8_t *)&invalid)[field] = field == 1U ? 16U : 2U;
    assert(!AppParam_ValidateHardwareConfig(&invalid));
  }
  assert(AppParam_HardwareConfigAllowed());
  uint8_t *flags[] = {&automatic, &emergency, &running, &paused, &dangerous, &percent};
  for (unsigned i = 0U; i < sizeof(flags)/sizeof(flags[0]); ++i)
  {
    *flags[i] = 1U;
    assert(!AppParam_HardwareConfigAllowed());
    *flags[i] = 0U;
  }
  uint32_t revision = AppParam_GetRevision();
  AppParam_CommitHardwareConfig(&config);
  assert(AppParam_GetRevision() == revision + 1U && AppParam_IsSavePending());
  assert(!memcmp(AppParam_GetHardwareConfig(), &config, 4U));
  app_param.crc32 = param_crc32(&app_param);
  assert(flash_param_is_valid(&app_param));
  assert(!migrate_legacy_v7_v8(&app_param));
  AppParam_CommitBoosterPumpMode(1U);
  assert(AppParam_BoosterPumpUsesRs485());
  AppParam_t old10 = app_param;
  old10.version = 10U;
  old10.crc32 = param_crc32(&old10);
  assert(migrate_legacy_v7_v8(&old10));
  assert(app_param.version == 11U && !AppParam_BoosterPumpUsesRs485());
  assert(!memcmp(old10.reserved, app_param.reserved, 8U));
  assert(!memcmp(old10.recipes, app_param.recipes, sizeof(old10.recipes)));
  AppParam_CommitBoosterPumpMode(1U);
  AppParam_RestoreDefaults();
  assert(AppParam_BoosterPumpUsesRs485());
  assert(!memcmp(AppParam_GetHardwareConfig(), &config, 4U));
  puts("Parameters: V4-V10 migration, V11 hardware/pump, idle guards, recipe preservation and CRC PASS");
}
