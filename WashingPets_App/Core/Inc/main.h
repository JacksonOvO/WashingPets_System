/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */
HAL_StatusTypeDef UART2_Send(const uint8_t *data, uint16_t length);
uint16_t UART2_Read(uint8_t *data, uint16_t max_length);

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define APP_ERROR_LED_Pin GPIO_PIN_11
#define APP_ERROR_LED_GPIO_Port GPIOC
#define APP_RUN_LED_Pin GPIO_PIN_12
#define APP_RUN_LED_GPIO_Port GPIOC

/* IN1: 沐浴液1低液位 - PA3 */
#define IN1_BATH_LIQUID_LOW_Pin GPIO_PIN_3
#define IN1_BATH_LIQUID_LOW_GPIO_Port GPIOA
/* IN2: 沐浴液2低液位 - PA4 */
#define IN2_FRAGRANCE_LOW_Pin GPIO_PIN_4
#define IN2_FRAGRANCE_LOW_GPIO_Port GPIOA
/* IN3: 沐浴液3低液位 - PA5 */
#define IN3_SOFTENER_LOW_Pin GPIO_PIN_5
#define IN3_SOFTENER_LOW_GPIO_Port GPIOA
/* IN4: 沐浴液4低液位 - PA6 */
#define IN4_DEODORIZER_LOW_Pin GPIO_PIN_6
#define IN4_DEODORIZER_LOW_GPIO_Port GPIOA
/* IN5: 清洁液低液位 - PA7 */
#define IN5_CLEAN_LIQUID_LOW_Pin GPIO_PIN_7
#define IN5_CLEAN_LIQUID_LOW_GPIO_Port GPIOA
/* IN6: 消毒液低液位 - PC4 */
#define IN6_DISINFECTANT_LOW_Pin GPIO_PIN_4
#define IN6_DISINFECTANT_LOW_GPIO_Port GPIOC
/* IN7: 水箱低液位 - PC5 */
#define IN7_TANK_LOW_Pin GPIO_PIN_5
#define IN7_TANK_LOW_GPIO_Port GPIOC
/* IN8: 水箱中液位 - PB0 */
#define IN8_TANK_MIDDLE_Pin GPIO_PIN_0
#define IN8_TANK_MIDDLE_GPIO_Port GPIOB
/* IN9: 水箱高液位 - PB1 */
#define IN9_TANK_HIGH_Pin GPIO_PIN_1
#define IN9_TANK_HIGH_GPIO_Port GPIOB
/* IN10: 急停开关 - PB2 */
#define IN10_EMERGENCY_STOP_Pin GPIO_PIN_2
#define IN10_EMERGENCY_STOP_GPIO_Port GPIOB
/* IN11: 上门开门到位 - PF11 */
#define IN11_UPPER_DOOR_OPEN_Pin GPIO_PIN_11
#define IN11_UPPER_DOOR_OPEN_GPIO_Port GPIOF
/* IN12: 备用1 - PF12 */
#define IN12_SPARE_1_Pin GPIO_PIN_12
#define IN12_SPARE_1_GPIO_Port GPIOF
/* IN13: 备用2 - PF13 */
#define IN13_SPARE_2_Pin GPIO_PIN_13
#define IN13_SPARE_2_GPIO_Port GPIOF
/* IN14: 备用3 - PF14 */
#define IN14_SPARE_3_Pin GPIO_PIN_14
#define IN14_SPARE_3_GPIO_Port GPIOF
/* IN15: 备用4 - PE10 */
#define IN15_SPARE_4_Pin GPIO_PIN_10
#define IN15_SPARE_4_GPIO_Port GPIOE
/* IN16: 备用5 - PE11 */
#define IN16_SPARE_5_Pin GPIO_PIN_11
#define IN16_SPARE_5_GPIO_Port GPIOE
/* IN17: 备用6 - PE12 */
#define IN17_SPARE_6_Pin GPIO_PIN_12
#define IN17_SPARE_6_GPIO_Port GPIOE
/* IN18: 备用7 - PE13 */
#define IN18_SPARE_7_Pin GPIO_PIN_13
#define IN18_SPARE_7_GPIO_Port GPIOE
/* IN19: 备用8 - PE14 */
#define IN19_SPARE_8_Pin GPIO_PIN_14
#define IN19_SPARE_8_GPIO_Port GPIOE
/* 独立上门关门到位 (bit19) - PF15 */
#define UPPER_DOOR_CLOSED_Pin GPIO_PIN_15
#define UPPER_DOOR_CLOSED_GPIO_Port GPIOF
/* 独立下门关门到位 (bit20) - PG0 */
#define LOWER_DOOR_CLOSED_Pin GPIO_PIN_0
#define LOWER_DOOR_CLOSED_GPIO_Port GPIOG

#define OUT1_OXYGEN_MACHINE_Pin GPIO_PIN_12
#define OUT1_OXYGEN_MACHINE_GPIO_Port GPIOB
#define OUT2_EXHAUST_UV_LAMP_Pin GPIO_PIN_13
#define OUT2_EXHAUST_UV_LAMP_GPIO_Port GPIOB
#define OUT3_UV_LAMP_Pin GPIO_PIN_14
#define OUT3_UV_LAMP_GPIO_Port GPIOB
#define OUT4_BLOWER_Pin GPIO_PIN_15
#define OUT4_BLOWER_GPIO_Port GPIOB
#define OUT5_SWING_MOTOR_Pin GPIO_PIN_8
#define OUT5_SWING_MOTOR_GPIO_Port GPIOD
#define OUT6_FLOOR_LIFT_Pin GPIO_PIN_9
#define OUT6_FLOOR_LIFT_GPIO_Port GPIOD
#define OUT7_MAIN_POWER_Pin GPIO_PIN_10
#define OUT7_MAIN_POWER_GPIO_Port GPIOD
#define OUT8_AIR_FRESHENER_Pin GPIO_PIN_11
#define OUT8_AIR_FRESHENER_GPIO_Port GPIOD
#define OUT9_BATH_PUMP_Pin GPIO_PIN_12
#define OUT9_BATH_PUMP_GPIO_Port GPIOD
#define OUT10_SELF_CLEAN_PUMP_Pin GPIO_PIN_13
#define OUT10_SELF_CLEAN_PUMP_GPIO_Port GPIOD
#define OUT11_LIGHTING_Pin GPIO_PIN_4
#define OUT11_LIGHTING_GPIO_Port GPIOB
#define OUT12_FRONT_DOOR_LIGHT_Pin GPIO_PIN_5
#define OUT12_FRONT_DOOR_LIGHT_GPIO_Port GPIOB
#define OUT13_DOOR_OPEN_Pin GPIO_PIN_6
#define OUT13_DOOR_OPEN_GPIO_Port GPIOB
#define OUT14_DOOR_CLOSE_Pin GPIO_PIN_7
#define OUT14_DOOR_CLOSE_GPIO_Port GPIOB
#define OUT15_DOOR_STOP_Pin GPIO_PIN_8
#define OUT15_DOOR_STOP_GPIO_Port GPIOB
#define OUT16_MANUAL_CLEAN_VALVE_Pin GPIO_PIN_9
#define OUT16_MANUAL_CLEAN_VALVE_GPIO_Port GPIOB
#define OUT17_TAP_WATER_VALVE_Pin GPIO_PIN_0
#define OUT17_TAP_WATER_VALVE_GPIO_Port GPIOE
#define OUT18_STANDBY2_VALVE_Pin GPIO_PIN_1
#define OUT18_STANDBY2_VALVE_GPIO_Port GPIOE
#define OUT19_BYPASS_VALVE_Pin GPIO_PIN_3
#define OUT19_BYPASS_VALVE_GPIO_Port GPIOB
#define OUT20_SHOWER_MIDDLE_Pin GPIO_PIN_2
#define OUT20_SHOWER_MIDDLE_GPIO_Port GPIOE
#define OUT21_SHOWER_UPPER_Pin GPIO_PIN_3
#define OUT21_SHOWER_UPPER_GPIO_Port GPIOE
#define OUT22_SELF_CLEAN_TOP_VALVE_Pin GPIO_PIN_4
#define OUT22_SELF_CLEAN_TOP_VALVE_GPIO_Port GPIOE
#define OUT23_SELF_CLEAN_LOWER_VALVE_Pin GPIO_PIN_5
#define OUT23_SELF_CLEAN_LOWER_VALVE_GPIO_Port GPIOE
#define OUT24_SELF_CLEAN_TOILET_VALVE_Pin GPIO_PIN_6
#define OUT24_SELF_CLEAN_TOILET_VALVE_GPIO_Port GPIOE
#define OUT25_BATH_LIQUID_VALVE_Pin GPIO_PIN_13
#define OUT25_BATH_LIQUID_VALVE_GPIO_Port GPIOC
#define OUT26_FRAGRANCE_VALVE_Pin GPIO_PIN_14
#define OUT26_FRAGRANCE_VALVE_GPIO_Port GPIOC
#define OUT27_SOFTENER_VALVE_Pin GPIO_PIN_15
#define OUT27_SOFTENER_VALVE_GPIO_Port GPIOC
#define OUT28_DEODORIZER_VALVE_Pin GPIO_PIN_0
#define OUT28_DEODORIZER_VALVE_GPIO_Port GPIOF
#define OUT29_CONDITIONER_VALVE_Pin GPIO_PIN_1
#define OUT29_CONDITIONER_VALVE_GPIO_Port GPIOF
#define OUT30_DISINFECTANT_VALVE_Pin GPIO_PIN_2
#define OUT30_DISINFECTANT_VALVE_GPIO_Port GPIOF
#define OUT31_BOOSTER_PUMP_RUN_Pin GPIO_PIN_3
#define OUT31_BOOSTER_PUMP_RUN_GPIO_Port GPIOF
#define OUT32_SPARE_1_Pin GPIO_PIN_10
#define OUT32_SPARE_1_GPIO_Port GPIOF
#define OUT33_SPARE_2_Pin GPIO_PIN_0
#define OUT33_SPARE_2_GPIO_Port GPIOC
#define OUT34_SPARE_3_Pin GPIO_PIN_1
#define OUT34_SPARE_3_GPIO_Port GPIOC
#define OUT35_SPARE_4_Pin GPIO_PIN_2
#define OUT35_SPARE_4_GPIO_Port GPIOC
#define OUT36_SPARE_5_Pin GPIO_PIN_3
#define OUT36_SPARE_5_GPIO_Port GPIOC
#define OUT37_SPARE_6_Pin GPIO_PIN_0
#define OUT37_SPARE_6_GPIO_Port GPIOA
#define OUT38_SPARE_7_Pin GPIO_PIN_1
#define OUT38_SPARE_7_GPIO_Port GPIOA
#define OUT39_SPARE_8_Pin GPIO_PIN_2
#define OUT39_SPARE_8_GPIO_Port GPIOA
#define MAGLOCK1_EN_Pin GPIO_PIN_4
#define MAGLOCK1_EN_GPIO_Port GPIOF
#define MAGLOCK2_EN_Pin GPIO_PIN_5
#define MAGLOCK2_EN_GPIO_Port GPIOF
#define MAGLOCK3_EN_Pin GPIO_PIN_6
#define MAGLOCK3_EN_GPIO_Port GPIOF
#define MAGLOCK4_EN_Pin GPIO_PIN_7
#define MAGLOCK4_EN_GPIO_Port GPIOF
#define MAGLOCK5_EN_Pin GPIO_PIN_8
#define MAGLOCK5_EN_GPIO_Port GPIOF
#define MAGLOCK6_EN_Pin GPIO_PIN_9
#define MAGLOCK6_EN_GPIO_Port GPIOF

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
