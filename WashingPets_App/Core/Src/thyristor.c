#include "thyristor.h"

#include <string.h>

/*
 * 可控硅控制器通信驱动
 *
 * 物理接口：RS485 半双工通信。
 * 上层协议：Modbus RTU。
 * 支持的功能码：
 * - 0x03：读取保持寄存器；
 * - 0x06：写单个保持寄存器。
 *
 * 本文件只负责可控硅设备的协议收发和数据检查，不负责创建线程。
 * USART1/RS485 总线由 task_comm485.c 统一管理，避免多个线程同时访问总线。
 */
#define THYRISTOR_FUNC_READ            0x03U
#define THYRISTOR_FUNC_WRITE           0x06U
#define THYRISTOR_RESPONSE_TIMEOUT_MS  200U
#define THYRISTOR_TX_COMPLETE_TIMEOUT_MS 20U

/* 保存可控硅驱动使用的 UART，当前工程由 task_comm485.c 传入 USART1。 */
static UART_HandleTypeDef *thyristor_uart;

/*
 * 计算 Modbus RTU 的 CRC16 校验值。
 *
 * 算法参数：
 * - CRC 初始值：0xFFFF；
 * - 生成多项式：0xA001；
 * - 按字节从帧头计算到数据区末尾；
 * - Modbus 帧发送 CRC 时低字节在前，高字节在后。
 */
static uint16_t modbus_crc16(const uint8_t *data, uint16_t length)
{
  uint16_t crc = 0xFFFFU;
  uint16_t index;
  uint8_t bit;

  for (index = 0U; index < length; ++index)
  {
    crc ^= data[index];
    for (bit = 0U; bit < 8U; ++bit)
    {
      if ((crc & 1U) != 0U)
      {
        crc = (uint16_t)((crc >> 1U) ^ 0xA001U);
      }
      else
      {
        crc >>= 1U;
      }
    }
  }
  return crc;
}

/* 计算帧前 data_length 个字节的 CRC，并追加到帧末尾两个字节。 */
static void append_crc(uint8_t *frame, uint16_t data_length)
{
  uint16_t crc = modbus_crc16(frame, data_length);

  frame[data_length] = (uint8_t)crc;
  frame[data_length + 1U] = (uint8_t)(crc >> 8U);
}

/*
 * 检查接收帧的 CRC 是否正确。
 * 接收帧最后两个字节依次为 CRC 低字节和 CRC 高字节。
 */
static uint8_t crc_is_valid(const uint8_t *frame, uint16_t length)
{
  uint16_t expected;

  if (length < 3U)
  {
    return 0U;
  }
  expected = (uint16_t)frame[length - 2U] |
             ((uint16_t)frame[length - 1U] << 8U);
  return (modbus_crc16(frame, (uint16_t)(length - 2U)) == expected) ? 1U : 0U;
}

/*
 * 完成一次 Modbus RTU 请求与响应交换。
 *
 * request 固定为 8 字节，格式为：
 *   站号 功能码 地址高 地址低 数据高 数据低 CRC低 CRC高
 *
 * 对于 0x03 读取命令，“数据高、数据低”表示读取寄存器数量；
 * 对于 0x06 写命令，“数据高、数据低”表示写入寄存器的数值。
 *
 * 函数处理流程：
 * 1. 清除 UART 中上一次通信残留的溢出标志和接收数据；
 * 2. 将 RS485 芯片切换到发送模式；
 * 3. 发送完整的 8 字节请求帧；
 * 4. 等待最后一个停止位发送完成，然后切换到接收模式；
 * 5. 寻找正确的设备站号和功能码；
 * 6. 根据功能码接收剩余响应数据；
 * 7. 检查 CRC 和 Modbus 异常响应。
 */
static ThyristorStatus exchange(const uint8_t request[8], uint8_t *response,
                                uint16_t *response_length)
{
  uint8_t byte;
  uint8_t header_index = 0U;
  uint16_t remaining;
  uint32_t start_tick;

  if (thyristor_uart == NULL)
  {
    return THYRISTOR_ERROR_PARAM;
  }

  /* 丢弃上次通信可能残留的数据，防止旧数据干扰本次响应判断。 */
  __HAL_UART_CLEAR_OREFLAG(thyristor_uart);
  __HAL_UART_FLUSH_DRREGISTER(thyristor_uart);

  /* 先输出调试帧，再通过 PC0 将 RS485 芯片切换到发送状态。 */
  Thyristor_OnTransmit(request, 8U);
  Thyristor_SetTransmitMode();
  HAL_Delay(1U);

  /* 发送固定长度的 Modbus RTU 请求帧。 */
  if (HAL_UART_Transmit(thyristor_uart, (uint8_t *)request, 8U,
                        THYRISTOR_RESPONSE_TIMEOUT_MS) != HAL_OK)
  {
    Thyristor_SetReceiveMode();
    return THYRISTOR_ERROR_TIMEOUT;
  }

  /*
   * 等待 TC（Transmission Complete）标志置位。
   * TC 表示最后一个字节的停止位也已经发送完成，此时才能关闭发送驱动。
   * 如果过早切换到接收模式，可能导致请求帧最后的 CRC 字节被截断。
   */
  start_tick = HAL_GetTick();
  while (__HAL_UART_GET_FLAG(thyristor_uart, UART_FLAG_TC) == RESET)
  {
    if ((HAL_GetTick() - start_tick) >= THYRISTOR_TX_COMPLETE_TIMEOUT_MS)
    {
      Thyristor_SetReceiveMode();
      return THYRISTOR_ERROR_TIMEOUT;
    }
  }
  Thyristor_SetReceiveMode();

  /*
   * RS485 收发方向切换时可能出现干扰字节，因此逐字节寻找正确帧头。
   * 正常响应功能码与请求相同；异常响应功能码等于原功能码加 0x80。
   */
  start_tick = HAL_GetTick();
  while ((HAL_GetTick() - start_tick) < THYRISTOR_RESPONSE_TIMEOUT_MS)
  {
    if (HAL_UART_Receive(thyristor_uart, &byte, 1U, 10U) != HAL_OK)
    {
      continue;
    }

    if (header_index == 0U)
    {
      if (byte == request[0])
      {
        response[0] = byte;
        header_index = 1U;
      }
    }
    else
    {
      if ((byte == request[1]) || (byte == (uint8_t)(request[1] | 0x80U)))
      {
        response[1] = byte;
        header_index = 2U;
        break;
      }
      if (byte == request[0])
      {
        response[0] = byte;
      }
      else
      {
        header_index = 0U;
      }
    }
  }
  if (header_index != 2U)
  {
    return THYRISTOR_ERROR_TIMEOUT;
  }

  /*
   * 读取响应的第 3 个字节：
   * - 0x03 正常响应中表示后续有效数据字节数；
   * - 异常响应中表示 Modbus 异常码；
   * - 0x06 正常响应中是被写寄存器地址的高字节。
   */
  if (HAL_UART_Receive(thyristor_uart, &response[2], 1U,
                       THYRISTOR_RESPONSE_TIMEOUT_MS) != HAL_OK)
  {
    return THYRISTOR_ERROR_TIMEOUT;
  }

  if ((response[1] & 0x80U) != 0U)
  {
    /* 异常响应共 5 字节，已经收到 3 字节，还需接收两个 CRC 字节。 */
    remaining = 2U;
  }
  else if (response[1] == THYRISTOR_FUNC_WRITE)
  {
    /*
     * 0x06 正常响应会原样返回请求帧。
     * 已收到站号、功能码和地址高字节，剩余 5 字节需要继续接收。
     */
    remaining = 5U;
  }
    else if (response[1] == THYRISTOR_FUNC_READ)
    {
      /*
       * 当前可控硅读接口一次只读取一个 16 位寄存器，
       * 所以有效数据最多只能是 2 字节。
       */
      if (response[2] > 2U)
      {
        Thyristor_OnReceive(response, 3U);
        return THYRISTOR_ERROR_RESPONSE;
      }
    remaining = (uint16_t)response[2] + 2U;
  }
  else
  {
    Thyristor_OnReceive(response, 3U);
    return THYRISTOR_ERROR_RESPONSE;
  }

  /* 接收数据区以及帧末尾的 CRC 低、高字节。 */
  if (HAL_UART_Receive(thyristor_uart, &response[3], remaining,
                       THYRISTOR_RESPONSE_TIMEOUT_MS) != HAL_OK)
  {
    return THYRISTOR_ERROR_TIMEOUT;
  }
  *response_length = (uint16_t)(3U + remaining);
  Thyristor_OnReceive(response, *response_length);

  /* CRC 错误时禁止继续使用响应中的寄存器数据。 */
  if (crc_is_valid(response, *response_length) == 0U)
  {
    return THYRISTOR_ERROR_CRC;
  }

  /* 功能码最高位置 1 表示从机返回了 Modbus 异常。 */
  if ((response[1] & 0x80U) != 0U)
  {
    return THYRISTOR_ERROR_EXCEPTION;
  }
  return THYRISTOR_OK;
}

/* 初始化可控硅驱动，并让 RS485 芯片默认处于接收状态。 */
void Thyristor_Init(UART_HandleTypeDef *huart)
{
  thyristor_uart = huart;
  Thyristor_SetReceiveMode();
}

/*
 * 使用 Modbus 0x06 功能码写入一个保持寄存器。
 *
 * station：设备站号，有效范围为 1～247；
 * reg：要写入的 16 位寄存器地址；
 * value：要写入的 16 位数值。
 *
 * 0x06 的正常响应应当与请求帧完全相同，因此接收后使用 memcmp
 * 对全部 8 个字节进行比较，防止写入了错误的地址或错误的数值。
 */
ThyristorStatus Thyristor_WriteRegister(uint8_t station, uint16_t reg,
                                        uint16_t value)
{
  uint8_t request[8];
  uint8_t response[8];
  uint16_t response_length;
  ThyristorStatus status;

  if ((station == 0U) || (station > 247U))
  {
    return THYRISTOR_ERROR_PARAM;
  }

  /* 按 Modbus 高字节在前的格式组装写单寄存器请求。 */
  request[0] = station;
  request[1] = THYRISTOR_FUNC_WRITE;
  request[2] = (uint8_t)(reg >> 8U);
  request[3] = (uint8_t)reg;
  request[4] = (uint8_t)(value >> 8U);
  request[5] = (uint8_t)value;
  append_crc(request, 6U);

  /* 发送请求并等待设备回传写入确认帧。 */
  status = exchange(request, response, &response_length);
  if (status != THYRISTOR_OK)
  {
    return status;
  }

  /* 0x06 正常响应必须与请求帧的 8 个字节完全一致。 */
  if ((response_length != sizeof(response)) ||
      (memcmp(request, response, sizeof(request)) != 0))
  {
    return THYRISTOR_ERROR_RESPONSE;
  }
  return THYRISTOR_OK;
}

/*
 * 使用 Modbus 0x03 功能码读取一个保持寄存器。
 *
 * 本函数固定读取 1 个寄存器，因此请求中的寄存器数量为 0x0001，
 * 正常响应长度为 7 字节：
 *   站号 03 02 数据高 数据低 CRC低 CRC高
 */
ThyristorStatus Thyristor_ReadRegister(uint8_t station, uint16_t reg,
                                       uint16_t *value)
{
  uint8_t request[8];
  uint8_t response[8];
  uint16_t response_length;
  ThyristorStatus status;

  if ((station == 0U) || (station > 247U) || (value == NULL))
  {
    return THYRISTOR_ERROR_PARAM;
  }

  /* 组装“读取一个保持寄存器”的 Modbus RTU 请求帧。 */
  request[0] = station;
  request[1] = THYRISTOR_FUNC_READ;
  request[2] = (uint8_t)(reg >> 8U);
  request[3] = (uint8_t)reg;
  request[4] = 0U;
  request[5] = 1U;
  append_crc(request, 6U);

  /* 发送请求，并完成超时、CRC和异常响应检查。 */
  status = exchange(request, response, &response_length);
  if (status != THYRISTOR_OK)
  {
    return status;
  }
  if ((response_length != 7U) || (response[0] != station) ||
      (response[1] != THYRISTOR_FUNC_READ) || (response[2] != 2U))
  {
    return THYRISTOR_ERROR_RESPONSE;
  }

  /* Modbus 寄存器数据为高字节在前，将两个字节合成为 uint16_t。 */
  *value = ((uint16_t)response[3] << 8U) | response[4];
  return THYRISTOR_OK;
}

/*
 * 设置可控硅的电压输出百分比。
 * 上层使用0～100%，发送前换算为0～240V并写入寄存器0x0000。
 * 设备寄存器只接受整数伏，换算时四舍五入到最接近的1V。
 */
ThyristorStatus Thyristor_SetVoltagePercent(uint8_t station, uint8_t percent)
{
  uint16_t voltage;

  if (percent > 100U)
  {
    return THYRISTOR_ERROR_PARAM;
  }
  voltage = (uint16_t)((((uint32_t)percent *
                         THYRISTOR_MAX_OUTPUT_VOLTAGE) + 50U) / 100U);
  return Thyristor_WriteRegister(station, THYRISTOR_REG_OUTPUT_VOLTAGE,
                                 voltage);
}

/*
 * 读取可控硅当前电压输出百分比。
 * 设备返回0～240V，本函数检查范围后换算回0～100%。
 */
ThyristorStatus Thyristor_GetVoltagePercent(uint8_t station, uint8_t *percent)
{
  uint16_t voltage;
  ThyristorStatus status;

  if (percent == NULL)
  {
    return THYRISTOR_ERROR_PARAM;
  }
  status = Thyristor_ReadRegister(station, THYRISTOR_REG_OUTPUT_VOLTAGE,
                                  &voltage);
  if (status != THYRISTOR_OK)
  {
    return status;
  }
  if (voltage > THYRISTOR_MAX_OUTPUT_VOLTAGE)
  {
    return THYRISTOR_ERROR_RESPONSE;
  }

  *percent = (uint8_t)((((uint32_t)voltage * 100U) +
                        (THYRISTOR_MAX_OUTPUT_VOLTAGE / 2U)) /
                       THYRISTOR_MAX_OUTPUT_VOLTAGE);
  return THYRISTOR_OK;
}

/*
 * 以下为可覆盖的弱函数。
 * task_comm485.c 中提供了本工程使用的实际实现：
 * - SetTransmitMode：PC0 置 1，使 RS485 芯片进入发送模式；
 * - SetReceiveMode：PC0 置 0，使 RS485 芯片进入接收模式；
 * - OnTransmit/OnReceive：通过 USART2 打印十六进制调试帧。
 *
 * 保留弱实现可以让该驱动脱离当前硬件后继续复用。
 */
__weak void Thyristor_SetTransmitMode(void)
{
}

__weak void Thyristor_SetReceiveMode(void)
{
}

__weak void Thyristor_OnTransmit(const uint8_t *data, uint16_t length)
{
  (void)data;
  (void)length;
}

__weak void Thyristor_OnReceive(const uint8_t *data, uint16_t length)
{
  (void)data;
  (void)length;
}
