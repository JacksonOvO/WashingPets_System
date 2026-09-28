#include <assert.h>
#include <stdio.h>
#include "../../Core/Src/app_param.c"

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
  puts("Parameters: V4-V8 migration, V9 preservation, dosing bounds and CRC PASS");
}
