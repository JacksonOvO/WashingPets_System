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
#define OZONE_Pin GPIO_PIN_13
#define OZONE_GPIO_Port GPIOC
#define THYRISTOR_ADD_Pin GPIO_PIN_0
#define THYRISTOR_ADD_GPIO_Port GPIOA
#define APP_ERROR_LED_Pin GPIO_PIN_11
#define APP_ERROR_LED_GPIO_Port GPIOC
#define APP_RUN_LED_Pin GPIO_PIN_12
#define APP_RUN_LED_GPIO_Port GPIOC
#define EXHAUST_FAN_Pin GPIO_PIN_12
#define EXHAUST_FAN_GPIO_Port GPIOB
#define OXYGEN_MACHINE_Pin GPIO_PIN_13
#define OXYGEN_MACHINE_GPIO_Port GPIOB
#define AIR_FRESHENER_Pin GPIO_PIN_14
#define AIR_FRESHENER_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
