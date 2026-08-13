#include "oxygen_sensor.h"

/*
 * 氧气/温湿度三合一传感器通信协议说明：
 * - 物理接口：RS485，半双工通信；
 * - 上层协议：Modbus RTU；
 * - 功能码：0x03，读取保持寄存器；
 * - 起始寄存器 0x0000：湿度，数值扩大了 10 倍；
 * - 寄存器 0x0001：温度，有符号 16 位数，数值扩大了 10 倍；
 * - 寄存器 0x0002：氧气浓度，数值扩大了 10 倍；
 * - 一次读取 3 个寄存器，因此正常响应中包含 6 字节有效数据。
 *
 * 例如传感器站号为 1 时，请求帧为：
 *   01 03 00 00 00 03 05 CB
 * 正常响应帧格式为：
 *   站号 03 06 湿度高 湿度低 温度高 温度低 氧气高 氧气低 CRC低 CRC高
 */
#define OXYGEN_SENSOR_FUNC_READ              0x03U
#define OXYGEN_SENSOR_REG_HUMIDITY           0x0000U
#define OXYGEN_SENSOR_REG_OZONE               0x0002U
#define OXYGEN_SENSOR_REGISTER_COUNT         3U
#define OXYGEN_SENSOR_RESPONSE_DATA_LENGTH   6U
#define OXYGEN_SENSOR_REQUEST_LENGTH         8U
#define OXYGEN_SENSOR_RESPONSE_LENGTH        11U
#define OXYGEN_SENSOR_EXCEPTION_LENGTH       5U
#define OXYGEN_SENSOR_RESPONSE_TIMEOUT_MS    500U

/* 保存本驱动所使用的 UART。当前工程中由 task_comm485.c 传入 USART1。 */
static UART_HandleTypeDef *oxygen_sensor_uart;

/*
 * 计算 Modbus RTU 使用的 CRC16 校验值。
 * 初始值为 0xFFFF，多项式为 0xA001。
 * Modbus 帧发送 CRC 时采用低字节在前、高字节在后的顺序。
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

/* 计算帧前 data_length 个字节的 CRC，并追加到帧末尾。 */
static void append_crc(uint8_t *frame, uint16_t data_length)
{
  uint16_t crc = modbus_crc16(frame, data_length);

  frame[data_length] = (uint8_t)crc;
  frame[data_length + 1U] = (uint8_t)(crc >> 8U);
}

/* 检查接收到的完整 Modbus RTU 帧的 CRC 是否正确。 */
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
 * 完成一次底层 RS485 请求和响应交换。
 *
 * 处理步骤：
 * 1. 清除 UART 上一次通信可能留下的溢出标志和接收数据；
 * 2. 将 PC0 切换为 RS485 发送模式并发送 8 字节请求帧；
 * 3. 等待最后一个停止位发送完成，再切换为接收模式；
 * 4. 在接收数据中寻找正确的“设备站号 + 功能码”帧头；
 * 5. 根据正常响应或异常响应接收剩余字节；
 * 6. 检查 CRC 和 Modbus 异常标志。
 *
 * response 数组必须至少能存放 11 字节的正常响应帧。
 */
static OxygenSensorStatus exchange(const uint8_t request[8],
                                   uint8_t response[11],
                                   uint16_t *response_length,
                                   uint8_t expected_data_length)
{
  uint8_t byte;
  uint8_t header_index = 0U;
  uint16_t remaining;
  uint32_t start_tick;

  if ((oxygen_sensor_uart == NULL) || (response_length == NULL))
  {
    return OXYGEN_SENSOR_ERROR_PARAM;
  }

  /* 丢弃上一次通信残留的数据，避免旧数据被误认为本次响应。 */
  __HAL_UART_CLEAR_OREFLAG(oxygen_sensor_uart);
  __HAL_UART_FLUSH_DRREGISTER(oxygen_sensor_uart);

  /* 先通过 USART2 打印即将发送的帧，再使能 RS485 发送方向。 */
  OxygenSensor_OnTransmit(request, OXYGEN_SENSOR_REQUEST_LENGTH);
  OxygenSensor_SetTransmitMode();
  HAL_Delay(1U);

  /* 请求帧固定为 8 字节：站号、功能码、地址、数量和 CRC。 */
  if (HAL_UART_Transmit(oxygen_sensor_uart, (uint8_t *)request,
                        OXYGEN_SENSOR_REQUEST_LENGTH,
                        OXYGEN_SENSOR_RESPONSE_TIMEOUT_MS) != HAL_OK)
  {
    OxygenSensor_SetReceiveMode();
    return OXYGEN_SENSOR_ERROR_TIMEOUT;
  }

  /*
   * 等待 TC（Transmission Complete）置位。
   * TC 表示移位寄存器中的最后一个停止位也已经发送完毕，此时才能关闭
   * RS485 发送驱动；否则过早切换方向可能截断 CRC 的最后一个字节。
   */
  start_tick = HAL_GetTick();
  while (__HAL_UART_GET_FLAG(oxygen_sensor_uart, UART_FLAG_TC) == RESET)
  {
    if ((HAL_GetTick() - start_tick) >= OXYGEN_SENSOR_RESPONSE_TIMEOUT_MS)
    {
      OxygenSensor_SetReceiveMode();
      return OXYGEN_SENSOR_ERROR_TIMEOUT;
    }
  }
  OxygenSensor_SetReceiveMode();

  /*
   * RS485 收发方向切换时可能出现干扰字节，所以逐字节寻找正确帧头。
   * 正常功能码为 0x03；传感器报告 Modbus 异常时功能码为 0x83。
   */
  start_tick = HAL_GetTick();
  while ((HAL_GetTick() - start_tick) < OXYGEN_SENSOR_RESPONSE_TIMEOUT_MS)
  {
    if (HAL_UART_Receive(oxygen_sensor_uart, &byte, 1U, 10U) != HAL_OK)
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
      if ((byte == OXYGEN_SENSOR_FUNC_READ) ||
          (byte == (uint8_t)(OXYGEN_SENSOR_FUNC_READ | 0x80U)))
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
    return OXYGEN_SENSOR_ERROR_TIMEOUT;
  }

  /* 帧头后的第一个字节是有效数据长度，异常响应中则是异常码。 */
  if (HAL_UART_Receive(oxygen_sensor_uart, &response[2], 1U,
                       OXYGEN_SENSOR_RESPONSE_TIMEOUT_MS) != HAL_OK)
  {
    return OXYGEN_SENSOR_ERROR_TIMEOUT;
  }

  if ((response[1] & 0x80U) != 0U)
  {
    /* 异常响应格式：站号、异常功能码、异常码、CRC低、CRC高。 */
    remaining = 2U;
    *response_length = OXYGEN_SENSOR_EXCEPTION_LENGTH;
  }
  else
  {
    /* 三个寄存器应当返回 6 字节数据，长度不符说明响应不是本命令的。 */
    if (response[2] != expected_data_length)
    {
      OxygenSensor_OnReceive(response, 3U);
      return OXYGEN_SENSOR_ERROR_RESPONSE;
    }
    remaining = (uint16_t)expected_data_length + 2U;
    *response_length = (uint16_t)expected_data_length + 5U;
  }

  /* 接收数据区和最后两个 CRC 字节。 */
  if (HAL_UART_Receive(oxygen_sensor_uart, &response[3], remaining,
                       OXYGEN_SENSOR_RESPONSE_TIMEOUT_MS) != HAL_OK)
  {
    return OXYGEN_SENSOR_ERROR_TIMEOUT;
  }
  OxygenSensor_OnReceive(response, *response_length);

  /* 必须先验证 CRC，再使用响应中的任何测量数据。 */
  if (crc_is_valid(response, *response_length) == 0U)
  {
    return OXYGEN_SENSOR_ERROR_CRC;
  }
  if ((response[1] & 0x80U) != 0U)
  {
    return OXYGEN_SENSOR_ERROR_EXCEPTION;
  }
  return OXYGEN_SENSOR_OK;
}

/* 初始化驱动并让 RS485 芯片默认处于接收状态。 */
void OxygenSensor_Init(UART_HandleTypeDef *huart)
{
  oxygen_sensor_uart = huart;
  OxygenSensor_SetReceiveMode();
}

/*
 * 一次读取湿度、温度和氧气浓度。
 *
 * 读取范围：从 0x0000 开始连续读取 3 个保持寄存器。
 * 输出数据仍然保留传感器的 10 倍定点格式，避免在单片机中引入浮点运算：
 * - humidity_x10 = 359，代表 35.9 %RH；
 * - temperature_x10 = -75，代表 -7.5 摄氏度；
 * - oxygen_x10 = 100，代表 10.0 %VOL。
 */
OxygenSensorStatus OxygenSensor_ReadAll(uint8_t station,
                                        OxygenSensorData *data)
{
  uint8_t request[OXYGEN_SENSOR_REQUEST_LENGTH];
  uint8_t response[OXYGEN_SENSOR_RESPONSE_LENGTH];
  uint16_t response_length;
  uint16_t raw_temperature;
  OxygenSensorStatus status;

  if ((station == 0U) || (station > 247U) || (data == NULL))
  {
    return OXYGEN_SENSOR_ERROR_PARAM;
  }

  /* 组装“读取 0x0000 起始的 3 个保持寄存器”请求帧。 */
  request[0] = station;
  request[1] = OXYGEN_SENSOR_FUNC_READ;
  request[2] = (uint8_t)(OXYGEN_SENSOR_REG_HUMIDITY >> 8U);
  request[3] = (uint8_t)OXYGEN_SENSOR_REG_HUMIDITY;
  request[4] = 0U;
  request[5] = OXYGEN_SENSOR_REGISTER_COUNT;
  append_crc(request, 6U);

  /* 发送请求并接收完整响应，底层已经完成超时、异常和 CRC 检查。 */
  status = exchange(request, response, &response_length,
                    OXYGEN_SENSOR_RESPONSE_DATA_LENGTH);
  if (status != OXYGEN_SENSOR_OK)
  {
    return status;
  }
  if ((response_length != OXYGEN_SENSOR_RESPONSE_LENGTH) ||
      (response[0] != station) ||
      (response[1] != OXYGEN_SENSOR_FUNC_READ) ||
      (response[2] != OXYGEN_SENSOR_RESPONSE_DATA_LENGTH))
  {
    return OXYGEN_SENSOR_ERROR_RESPONSE;
  }

  /*
   * Modbus 每个寄存器均为高字节在前、低字节在后。
   * 温度寄存器可能为负数，因此先合成为 uint16_t，再转换为 int16_t，
   * 这样 0xFFB5 会按照二进制补码正确转换为 -75。
   */
  data->humidity_x10 = ((uint16_t)response[3] << 8U) | response[4];
  raw_temperature = ((uint16_t)response[5] << 8U) | response[6];
  data->temperature_x10 = (int16_t)raw_temperature;
  data->oxygen_x10 = ((uint16_t)response[7] << 8U) | response[8];
  return OXYGEN_SENSOR_OK;
}

OxygenSensorStatus OxygenSensor_ReadOzone(uint8_t station,
                                          uint16_t *ozone_x100)
{
  uint8_t request[OXYGEN_SENSOR_REQUEST_LENGTH];
  uint8_t response[OXYGEN_SENSOR_RESPONSE_LENGTH];
  uint16_t response_length;
  OxygenSensorStatus status;

  if ((station == 0U) || (station > 247U) || (ozone_x100 == NULL))
  {
    return OXYGEN_SENSOR_ERROR_PARAM;
  }

  request[0] = station;
  request[1] = OXYGEN_SENSOR_FUNC_READ;
  request[2] = (uint8_t)(OXYGEN_SENSOR_REG_OZONE >> 8U);
  request[3] = (uint8_t)OXYGEN_SENSOR_REG_OZONE;
  request[4] = 0U;
  request[5] = 1U;
  append_crc(request, 6U);

  status = exchange(request, response, &response_length, 2U);
  if (status != OXYGEN_SENSOR_OK)
  {
    return status;
  }
  if ((response_length != 7U) ||
      (response[0] != station) ||
      (response[1] != OXYGEN_SENSOR_FUNC_READ) ||
      (response[2] != 2U))
  {
    return OXYGEN_SENSOR_ERROR_RESPONSE;
  }

  *ozone_x100 = ((uint16_t)response[3] << 8U) | response[4];
  return OXYGEN_SENSOR_OK;
}

/*
 * 以下为可覆盖的弱函数。task_comm485.c 提供了本工程的实际实现：
 * - SetTransmitMode/SetReceiveMode：通过 PC0 控制 RS485 DE/RE；
 * - OnTransmit/OnReceive：通过 USART2 输出十六进制调试信息。
 */
// __weak void OxygenSensor_SetTransmitMode(void)
// {
// }

// __weak void OxygenSensor_SetReceiveMode(void)
// {
// }

// __weak void OxygenSensor_OnTransmit(const uint8_t *data, uint16_t length)
// {
//   (void)data;
//   (void)length;
// }

// __weak void OxygenSensor_OnReceive(const uint8_t *data, uint16_t length)
// {
//   (void)data;
//   (void)length;
// }
