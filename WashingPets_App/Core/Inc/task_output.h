#ifndef TASK_OUTPUT_H
#define TASK_OUTPUT_H

#include "auto_device.h"

typedef enum
{
  APP_OUTPUT_SOURCE_AUTO = 0,
  APP_OUTPUT_SOURCE_MANUAL
} AppOutputSource;

/* 在RTOS启动前配置38路低电平有效输出，并全部置为关闭。 */
void TaskOutput_InitHardware(void);
void TaskOutput_Create(void);

/* 0=手动模式，1=自动模式；上电默认手动。 */
void TaskOutput_SetAutoMode(uint8_t auto_mode);
uint8_t TaskOutput_IsAutoMode(void);

/* enabled使用逻辑状态：1=设备开启，0=设备关闭。 */
HAL_StatusTypeDef TaskOutput_Request(AppOutputSource source,
                                     AutoOutputPoint point,
                                     uint8_t enabled);

/* 读取经过模式仲裁和安全互锁后的实际逻辑输出。 */
uint8_t TaskOutput_GetActual(AutoOutputPoint point);
uint8_t TaskOutput_HasDangerousOutputActive(void);

/* 清除指定来源或全部来源的请求，模式切换和故障复位时使用。 */
void TaskOutput_ClearSource(AppOutputSource source);
HAL_StatusTypeDef TaskOutput_ClearSourceSync(AppOutputSource source,
                                             uint32_t timeout_ms);
void TaskOutput_AllOff(void);
void TaskOutput_DangerousOff(void);
HAL_StatusTypeDef TaskOutput_Sync(uint32_t timeout_ms);

#endif
