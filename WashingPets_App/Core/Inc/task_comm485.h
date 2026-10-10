#ifndef TASK_COMM485_H
#define TASK_COMM485_H

#include "stm32f4xx_hal.h"
#include "app_param.h"

typedef struct
{
  uint16_t humidity_x10;      /* 359 表示 35.9 %RH */
  int16_t temperature_x10;    /* -75 表示 -7.5 摄氏度 */
  uint16_t oxygen_x10;        /* 209 表示 20.9 %VOL */
  uint32_t update_tick;       /* 最近一次成功读取时的 HAL 毫秒计数 */
  uint8_t valid;              /* 1：数据有效；0：尚未读取或数据已过期 */
  int16_t incab_temperature_x10; /* Station 9: cabin temperature, unit 0.1 degC. */
  uint16_t incab_humidity_x10;   /* Station 9: cabin humidity, unit 0.1 %RH. */
  uint32_t incab_update_tick;
  uint8_t incab_valid;

  uint16_t ozone_x100;           /* Station 10: cabin ozone concentration, unit 0.01 ppm. */
  uint32_t ozone_update_tick;
  uint8_t ozone_valid;
} TaskComm485EnvironmentData;

typedef enum
{
  COMM485_INHIBIT_STARTUP = (1UL << 0),
  COMM485_INHIBIT_SAFE_STOP = (1UL << 1),
  COMM485_INHIBIT_PAUSE = (1UL << 2),
  COMM485_INHIBIT_MODE_CHANGE = (1UL << 3),
  COMM485_INHIBIT_FAULT = (1UL << 4),
  COMM485_INHIBIT_CONFIG = (1UL << 5)
} TaskComm485InhibitReason;

void TaskComm485_Create(void);
HAL_StatusTypeDef TaskComm485_RequestHardwareConfig(const AppHardwareConfig_t *config);
HAL_StatusTypeDef TaskComm485_RequestBoosterPumpMode(uint8_t mode);
uint8_t TaskComm485_HardwareConfigBusy(void);
uint8_t TaskComm485_TakeHardwareConfigResult(HAL_StatusTypeDef *result);

/*
 * 将指定站号可控硅的输出百分比命令放入 RS485 队列。
 * station 有效范围为 1～247，percent 有效范围为 0～100。
 * PWM模式下10/11/12/13为逻辑风机编号，内部映射站号30的CH1/2/3/4。
 * 不可直接向物理站号30发送此百分比接口命令。
 */
HAL_StatusTypeDef TaskComm485_RequestSetPercent(uint8_t station,
                                                uint8_t percent);
HAL_StatusTypeDef TaskComm485_ForceSetPercent(uint8_t station, uint8_t percent);
HAL_StatusTypeDef TaskComm485_RequestSetProportionalValveAngle(uint8_t angle);
HAL_StatusTypeDef TaskComm485_RequestSetBoosterPumpPercent(uint8_t percent);
HAL_StatusTypeDef TaskComm485_RequestSetBoosterPumpRunning(uint8_t enabled);
HAL_StatusTypeDef TaskComm485_ForceSetBoosterPumpPercent(uint8_t percent);
HAL_StatusTypeDef TaskComm485_ForceSetBoosterPumpRunning(uint8_t enabled);
uint8_t TaskComm485_GetLastPercent(uint8_t station);
uint8_t TaskComm485_GetLastProportionalValveAngle(void);
uint8_t TaskComm485_GetLastBoosterPumpPercent(void);
uint8_t TaskComm485_GetBoosterPumpFeedbackPressure(void);
uint8_t TaskComm485_GetBoosterPumpFeedback(uint8_t *pressure_x10);
uint8_t TaskComm485_GetLastBoosterPumpRunning(void);
uint32_t TaskComm485_GetStationSuccessCount(uint8_t station);
void TaskComm485_SetSelfTestPressurePriority(uint8_t enabled);
void TaskComm485_SetRs485ActuatorMode(uint8_t enabled);
uint8_t TaskComm485_IsRs485ActuatorMode(void);
void TaskComm485_RequestSafeStop(void);
void TaskComm485_InhibitNonzero(uint32_t reasons);
void TaskComm485_ReleaseNonzero(uint32_t reasons);
uint32_t TaskComm485_GetInhibitReasons(void);

/*
 * 健康巡检接口：bitmap 按协议 13.1 故障类别位，随传感器与
 * 485 从站在线状态实时置位/清除；空载仿真下恒为健康。
 */
uint32_t TaskComm485_GetHealthBitmap(void);
uint8_t TaskComm485_IsAllHealthy(void);
uint16_t TaskComm485_GetFirstHealthFault(void);
void TaskComm485_ResetHealthFaults(void);

/*
 * 获取 RS485 线程缓存的最新环境数据，不会直接访问 USART1。
 * 返回 1 表示数据仍在有效期内；返回 0 表示无有效数据。
 */
uint8_t TaskComm485_GetEnvironmentData(TaskComm485EnvironmentData *data);

#endif
