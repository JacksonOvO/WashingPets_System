#ifndef BOOTLOADER_H
#define BOOTLOADER_H

#include "stm32f4xx_hal.h"

/* Bootloader Flash 布局说明（STM32F407ZG，1 MB Flash）：
 *
 * 0x08000000 - 0x08003FFF：Sector 0，Bootloader 自身代码。
 * 0x08004000 - 0x080BFFFF：APP 固件存放区。
 * 0x080C0000 - 0x080DFFFF：参数保存区，Bootloader 升级时不会擦除。
 * 0x080E0000 - 0x080FFFFF：Sector 11，APP 元数据区。
 *
 * APP 必须链接到 BOOTLOADER_APP_ADDRESS，否则中断向量表和复位入口
 * 都会不匹配，Bootloader 校验通过后也无法正确跳转。
 */
#define BOOTLOADER_APP_ADDRESS       0x08004000U
#define BOOTLOADER_PARAM_ADDRESS     0x080C0000U
#define BOOTLOADER_METADATA_ADDRESS  0x080E0000U
#define BOOTLOADER_FLASH_END         0x08100000U

/* APP 最大容量：从 APP 起始地址到参数区开始地址。
 * 升级开始时会用该值判断 bin 文件是否越界，避免擦写到参数区和元数据区。
 */
#define BOOTLOADER_APP_CAPACITY      (BOOTLOADER_PARAM_ADDRESS - BOOTLOADER_APP_ADDRESS)

/* 设备身份和版本信息。
 * TARGET_ID 用于防止主机把不属于本硬件的固件刷入设备。
 * VERSION 编码格式为 major.minor.patch：0x00MMmmpp，这里是 0.1.1。
 * REQUEST_MAGIC 由 APP 写入 RTC 备份寄存器，复位后 Bootloader 读取到它
 * 就会停留在升级模式，而不是直接跳转到 APP。
 */
#define BOOTLOADER_TARGET_ID         0x407A0C06U
#define BOOTLOADER_VERSION           0x00010100U
#define BOOTLOADER_REQUEST_MAGIC     0xA55A5AA5U

/* Bootloader 主循环入口。
 * huart 指向用于 IAP 升级协议的串口句柄，本工程里通常是 USART1。
 */
void Bootloader_Run(UART_HandleTypeDef *huart);

#endif
