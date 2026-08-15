#ifndef APP_IAP_H
#define APP_IAP_H

#include "stm32f4xx_hal.h"

#define APP_IAP_APP_ADDRESS          0x08004000U //APP 程序在 Flash 里的起始地址
#define APP_IAP_TARGET_ID            0x407A0C06U //设备目标 ID，用来识别当前设备型号/工程
#define APP_IAP_HARDWARE_VERSION     0x0001U  //硬件版本号
#define APP_IAP_VERSION              0x00020301U //APP 软件版本号: 2.3.1
#define APP_IAP_REQUEST_MAGIC        0xA55A5AA5U //IAP 请求魔术字,升级请求

/* 1=开启实时上报遥测数据, 0=关闭遥测数据上报. */
#define APP_IAP_TELEMETRY_ENABLE     1U

void AppIap_Init(UART_HandleTypeDef *huart);
void AppIap_ProcessByte(uint8_t byte);
void AppIap_Service(void);
void AppIap_OnBusinessByte(uint8_t byte);
void AppIap_TxLock(void);
void AppIap_TxUnlock(void);

#endif
