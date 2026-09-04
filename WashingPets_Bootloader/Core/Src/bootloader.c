#include "bootloader.h"
#include "iap_protocol.h"

#include <stddef.h>
#include <string.h>

/* Bootloader 串口协议和运行参数。
 * BL_START_TIMEOUT_MS 只在上电后短暂等待主机查询使用；超时且 APP 有效时
 * 直接跳转 APP，避免设备每次上电都长时间停在 Bootloader。
 */
#define BL_PACKET_TIMEOUT_MS       3000U
#define BL_START_TIMEOUT_MS        2000U
#define BL_ERROR_TIMEOUT           0xFFU

/* APP 元数据常量。
 * 元数据放在 Sector 11，用于记录 APP 大小、CRC、版本和升级状态。
 * Sector 10 预留给掉电参数保存，升级擦除 APP 时不能擦该扇区。
 * 状态值故意选择非 0/非全 1 的模式，便于区分擦除态和写入态。
 */
#define BL_METADATA_MAGIC          0x424C4D44U
#define BL_STATUS_IN_PROGRESS      0x7FFFFFFFU
#define BL_STATUS_VALID            0x3FFFFFFFU
#define BL_STATUS_FAILED           0x5FFFFFFFU

/* 当前硬件版本和运行指示灯配置。
 * PC11 为低电平点亮，所以 on/off 函数里电平和直觉相反。
 */
#define BL_HARDWARE_VERSION        0x0001U
#define BL_LED_BLINK_MS            120U
#define BL_LED_GPIO_PORT           GPIOC
#define BL_LED_GPIO_PIN            GPIO_PIN_11

/* APP 复位入口函数类型。跳转 APP 时会把向量表里的 reset_handler
 * 强制转换成该函数指针并调用。
 */
typedef void (*AppEntry)(void);

/* 写入 Flash 的 APP 元数据。
 * record_crc 只覆盖 record_crc 之前的字段，用来判断元数据本身是否完整。
 * image_crc 则覆盖整个 APP bin，用于升级结束后的固件完整性校验。
 */
typedef struct
{
  uint32_t magic;
  uint32_t target;
  uint32_t version;
  uint32_t size;
  uint32_t image_crc;
  uint32_t record_crc;
  uint32_t status;
} AppMetadata;

/* 一帧 IAP 命令解析后的内存表示。
 * 串口线上的帧格式为：
 *   55 AA | command | sequence | length_le | payload | crc16_le
 * CRC16 只覆盖 command/sequence/length/payload，不包含 55 AA 帧头。
 */
typedef struct
{
  uint8_t command;
  uint8_t sequence;
  uint16_t length;
  uint8_t payload[IAP_MAX_PAYLOAD_SIZE];
} IapPacket;

/* 升级过程的运行状态。
 * expected_sequence 用来保证主机按顺序发送 START/DATA/END。
 * written 表示已经成功写入 APP 区的字节数，同时也是断点/错误响应中的 next_offset。
 */
typedef struct
{
  uint8_t active;
  uint8_t expected_sequence;
  uint16_t chunk_size;
  uint32_t image_size;
  uint32_t image_crc;
  uint32_t image_version;
  uint32_t written;
} UpgradeContext;

/* Bootloader 全局运行上下文。
 * last_* 用于处理主机重发上一帧的情况：如果上一帧已经 ACK，重复收到时
 * 直接重发 ACK，避免主机因串口丢包而把设备状态推进两次。
 */
static UART_HandleTypeDef *bl_uart;
static UpgradeContext upgrade;
static uint8_t last_valid;
static uint8_t last_command;
static uint8_t last_sequence;
static uint8_t last_error;
static uint32_t last_next_offset;
static uint32_t led_tick;

/* 初始化 Bootloader 指示灯。 */
static void bootloader_led_init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOC_CLK_ENABLE();
  GPIO_InitStruct.Pin = BL_LED_GPIO_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(BL_LED_GPIO_PORT, &GPIO_InitStruct);

  /* PC11 LED is active-low. Keep it on while Bootloader is running. */
  HAL_GPIO_WritePin(BL_LED_GPIO_PORT, BL_LED_GPIO_PIN, GPIO_PIN_RESET);
}

/* 熄灭指示灯，通常在即将跳转 APP 或升级完成时调用。 */
static void bootloader_led_off(void)
{
  HAL_GPIO_WritePin(BL_LED_GPIO_PORT, BL_LED_GPIO_PIN, GPIO_PIN_SET);
}

/* 点亮指示灯，通常表示当前仍停留在 Bootloader。 */
static void bootloader_led_on(void)
{
  HAL_GPIO_WritePin(BL_LED_GPIO_PORT, BL_LED_GPIO_PIN, GPIO_PIN_RESET);
}

/* 低成本心跳闪烁：在擦写 Flash 或计算 CRC 的长耗时路径里调用，
 * 既能提示设备还活着，也避免阻塞期间用户误判为死机。
 */
static void bootloader_led_blink(void)
{
  uint32_t now = HAL_GetTick();

  if ((now - led_tick) >= BL_LED_BLINK_MS)
  {
    led_tick = now;
    HAL_GPIO_TogglePin(BL_LED_GPIO_PORT, BL_LED_GPIO_PIN);
  }
}

/* 以下 read/write 小工具统一处理协议里的小端序字段。
 * STM32F4 本身是小端，但显式读写可以避免结构体对齐和编译器填充问题。
 */
static uint16_t read_u16_le(const uint8_t *data)
{
  return (uint16_t)data[0] | ((uint16_t)data[1] << 8U);
}

static uint32_t read_u32_le(const uint8_t *data)
{
  return ((uint32_t)data[0]) | ((uint32_t)data[1] << 8U) |
         ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
}

static void write_u16_le(uint8_t *data, uint16_t value)
{
  data[0] = (uint8_t)value;
  data[1] = (uint8_t)(value >> 8U);
}

static void write_u32_le(uint8_t *data, uint32_t value)
{
  data[0] = (uint8_t)value;
  data[1] = (uint8_t)(value >> 8U);
  data[2] = (uint8_t)(value >> 16U);
  data[3] = (uint8_t)(value >> 24U);
}

/* CRC16-CCITT-FALSE，用于保护每一帧串口协议数据。 */
static uint16_t crc16_update(uint16_t crc, const uint8_t *data, uint16_t length)
{
  uint8_t bit;

  while (length-- > 0U)
  {
    crc ^= (uint16_t)(*data++) << 8U;
    for (bit = 0U; bit < 8U; ++bit)
    {
      crc = ((crc & 0x8000U) != 0U) ?
            (uint16_t)((crc << 1U) ^ 0x1021U) :
            (uint16_t)(crc << 1U);
    }
  }
  return crc;
}

/* 标准以太网 CRC32 算法，用于校验完整 APP 镜像和元数据记录。 */
static uint32_t crc32_update(uint32_t crc, const uint8_t *data, uint32_t length)
{
  uint32_t bit;

  while (length-- > 0U)
  {
    bootloader_led_blink();
    crc ^= *data++;
    for (bit = 0U; bit < 8U; ++bit)
    {
      crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
  }
  return crc;
}

static uint32_t crc32_data(const uint8_t *data, uint32_t length)
{
  return crc32_update(0xFFFFFFFFU, data, length) ^ 0xFFFFFFFFU;
}

static uint32_t device_id(void)
{
  uint32_t uid[3];

  uid[0] = *(volatile uint32_t *)UID_BASE;
  uid[1] = *(volatile uint32_t *)(UID_BASE + 4U);
  uid[2] = *(volatile uint32_t *)(UID_BASE + 8U);
  return crc32_data((const uint8_t *)uid, sizeof(uid));
}

/* 从串口接收并校验一帧 IAP 命令。
 * first_byte_timeout 控制等待帧头第一个字节的时间：
 * - 上电初期使用短超时，用于决定是否进入 APP；
 * - 进入升级循环后使用 HAL_MAX_DELAY，一直等待主机命令。
 */
static uint8_t receive_packet(IapPacket *packet, uint32_t first_byte_timeout)
{
  uint8_t byte;
  uint8_t header[4];
  uint8_t crc_bytes[2];
  uint32_t start_tick = HAL_GetTick();
  uint32_t receive_timeout;
  uint16_t crc;

  for (;;)
  {
    if (first_byte_timeout == HAL_MAX_DELAY)
    {
      receive_timeout = HAL_MAX_DELAY;
    }
    else
    {
      uint32_t elapsed = HAL_GetTick() - start_tick;
      if (elapsed >= first_byte_timeout)
      {
        return BL_ERROR_TIMEOUT;
      }
      receive_timeout = first_byte_timeout - elapsed;
    }

    if (HAL_UART_Receive(bl_uart, &byte, 1U, receive_timeout) != HAL_OK)
    {
      return (first_byte_timeout == HAL_MAX_DELAY) ? IAP_ERR_BAD_CRC16 : BL_ERROR_TIMEOUT;
    }
    if (byte != IAP_HEAD_0)
    {
      continue;
    }
    if ((HAL_UART_Receive(bl_uart, &byte, 1U, 100U) == HAL_OK) &&
        (byte == IAP_HEAD_1))
    {
      break;
    }
  }

  if (HAL_UART_Receive(bl_uart, header, sizeof(header), BL_PACKET_TIMEOUT_MS) != HAL_OK)
  {
    return IAP_ERR_BAD_CRC16;
  }
  packet->command = header[0];
  packet->sequence = header[1];
  packet->length = read_u16_le(&header[2]);
  if (packet->length > IAP_MAX_PAYLOAD_SIZE)
  {
    return IAP_ERR_BAD_LENGTH;
  }
  if ((packet->length > 0U) &&
      (HAL_UART_Receive(bl_uart, packet->payload, packet->length,
                        BL_PACKET_TIMEOUT_MS) != HAL_OK))
  {
    return IAP_ERR_BAD_CRC16;
  }
  if (HAL_UART_Receive(bl_uart, crc_bytes, sizeof(crc_bytes),
                       BL_PACKET_TIMEOUT_MS) != HAL_OK)
  {
    return IAP_ERR_BAD_CRC16;
  }

  crc = crc16_update(0xFFFFU, header, sizeof(header));
  crc = crc16_update(crc, packet->payload, packet->length);

  return (crc == read_u16_le(crc_bytes)) ? IAP_STATUS_OK : IAP_ERR_BAD_CRC16;
}

/* 发送一帧 IAP 响应。响应帧和命令帧使用相同封包格式。 */
static void send_frame(uint8_t command, uint8_t sequence,
                       const uint8_t *payload, uint16_t length)
{
  uint8_t header[6];
  uint8_t crc_bytes[2];
  uint16_t crc;

  header[0] = IAP_HEAD_0;
  header[1] = IAP_HEAD_1;
  header[2] = command;
  header[3] = sequence;
  write_u16_le(&header[4], length);

  crc = crc16_update(0xFFFFU, &header[2], 4U);
  crc = crc16_update(crc, payload, length);
  write_u16_le(crc_bytes, crc);

  HAL_UART_Transmit(bl_uart, header, sizeof(header), 1000U);
  if (length > 0U)
  {
    HAL_UART_Transmit(bl_uart, payload, length, 1000U);
  }
  HAL_UART_Transmit(bl_uart, crc_bytes, sizeof(crc_bytes), 1000U);
}

/* 发送 ACK，并记录上一条成功响应，便于处理主机重发。 */
static void send_ack(uint8_t request, uint8_t sequence, uint32_t next_offset)
{
  uint8_t payload[6];

  payload[0] = request;
  payload[1] = IAP_STATUS_OK;
  write_u32_le(&payload[2], next_offset);
  send_frame(IAP_RSP_ACK, sequence, payload, sizeof(payload));

  last_valid = 1U;
  last_command = request;
  last_sequence = sequence;
  last_error = IAP_STATUS_OK;
  last_next_offset = next_offset;
}

/* 发送 NACK。NACK payload 中包含 expected_sequence 和 next_offset，
 * 主机可以据此判断是序号错误、偏移错误，还是需要重新同步。
 */
static void send_nack(uint8_t request, uint8_t sequence, uint8_t error,
                      uint32_t next_offset)
{
  uint8_t payload[8];

  payload[0] = request;
  payload[1] = error;
  payload[2] = upgrade.expected_sequence;
  payload[3] = 0U;
  write_u32_le(&payload[4], next_offset);
  send_frame(IAP_RSP_NACK, sequence, payload, sizeof(payload));

  last_valid = 0U;
}

/* 擦除连续 Flash 扇区。
 * first_sector 和 count 使用 HAL 的扇区编号，不是 Flash 地址。
 */
static uint8_t flash_erase(uint32_t first_sector, uint32_t count)
{
  FLASH_EraseInitTypeDef erase = {0};
  uint32_t sector_error;
  HAL_StatusTypeDef status;

  erase.TypeErase = FLASH_TYPEERASE_SECTORS;
  erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
  erase.Sector = first_sector;
  erase.NbSectors = count;

  HAL_FLASH_Unlock();
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                         FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);
  bootloader_led_blink();
  status = HAL_FLASHEx_Erase(&erase, &sector_error);
  bootloader_led_blink();
  HAL_FLASH_Lock();
  return (status == HAL_OK) ? 1U : 0U;
}

/* 按字节写入 Flash，并在写完后用 memcmp 做一次回读校验。
 * 这里按 byte 写虽然慢，但主机 chunk 较小，实现简单且对对齐要求最低。
 */
static uint8_t flash_write(uint32_t address, const uint8_t *data, uint32_t length)
{
  uint32_t index;

  HAL_FLASH_Unlock();
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                         FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);
  for (index = 0U; index < length; ++index)
  {
    bootloader_led_blink();
    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_BYTE, address + index, data[index]) != HAL_OK)
    {
      HAL_FLASH_Lock();
      return 0U;
    }
  }
  HAL_FLASH_Lock();
  return (memcmp((const void *)address, data, length) == 0) ? 1U : 0U;
}

/* 只更新元数据里的 status 字段。
 * 注意 Flash 只能把 bit 从 1 写成 0，不能直接从 0 写回 1；
 * 所以状态值设计需要满足升级过程中的单向写入约束。
 */
static uint8_t metadata_write_status(uint32_t status)
{
  return flash_write(BOOTLOADER_METADATA_ADDRESS + offsetof(AppMetadata, status),
                     (const uint8_t *)&status, sizeof(status));
}

/* 开始一次升级：擦除元数据区，写入 IN_PROGRESS 状态的元数据记录。
 * 这样即使升级中途断电，下一次启动也能识别到 APP 不是 VALID 状态。
 */
static uint8_t metadata_begin(uint32_t size, uint32_t crc, uint32_t version)
{
  AppMetadata metadata;

  metadata.magic = BL_METADATA_MAGIC;
  metadata.target = device_id();
  metadata.version = version;
  metadata.size = size;
  metadata.image_crc = crc;
  metadata.record_crc = crc32_data((const uint8_t *)&metadata,
                                   offsetof(AppMetadata, record_crc));
  metadata.status = BL_STATUS_IN_PROGRESS;

  if (flash_erase(FLASH_SECTOR_11, 1U) == 0U)
  {
    return 0U;
  }
  return flash_write(BOOTLOADER_METADATA_ADDRESS, (const uint8_t *)&metadata,
                     sizeof(metadata));
}

/* 检查 APP 向量表是否合法。
 * 第一个 word 是 MSP 初值，必须落在 SRAM 范围且 8 字节对齐；
 * 第二个 word 是 Reset_Handler，必须落在 APP Flash 范围且 Thumb bit 为 1。
 */
static uint8_t app_vector_is_valid(void)
{
  uint32_t stack_pointer = *(volatile uint32_t *)BOOTLOADER_APP_ADDRESS;
  uint32_t reset_handler = *(volatile uint32_t *)(BOOTLOADER_APP_ADDRESS + 4U);

  return ((stack_pointer >= 0x20000000U) &&
          (stack_pointer <= 0x20020000U) &&
          ((stack_pointer & 0x7U) == 0U) &&
          (reset_handler >= BOOTLOADER_APP_ADDRESS) &&
          (reset_handler < BOOTLOADER_PARAM_ADDRESS) &&
          ((reset_handler & 1U) != 0U)) ? 1U : 0U;
}

/* 判断元数据区是否仍是擦除态。
 * 允许元数据全 0xFFFFFFFF 时仍按旧 APP 兼容路径启动，只要向量表有效。
 */
static uint8_t metadata_is_erased(const AppMetadata *metadata)
{
  const uint32_t *words = (const uint32_t *)metadata;
  uint32_t index;

  for (index = 0U; index < (sizeof(AppMetadata) / sizeof(uint32_t)); ++index)
  {
    if (words[index] != 0xFFFFFFFFU)
    {
      return 0U;
    }
  }
  return 1U;
}

/* 判断 APP 是否可启动。
 * 新升级的 APP 必须满足向量表、元数据 magic/target/status/record_crc、
 * 镜像长度和整包 CRC32 校验；旧格式 APP 在元数据擦除态时只校验向量表。
 */
static uint8_t app_is_valid(void)
{
  const AppMetadata *metadata = (const AppMetadata *)BOOTLOADER_METADATA_ADDRESS;

  if (app_vector_is_valid() == 0U)
  {
    return 0U;
  }
  if (metadata_is_erased(metadata) != 0U)
  {
    return 1U;
  }
  if ((metadata->magic != BL_METADATA_MAGIC) ||
      (metadata->target != device_id()) ||
      (metadata->status != BL_STATUS_VALID) ||
      (metadata->size < 8U) || (metadata->size > BOOTLOADER_APP_CAPACITY) ||
      (metadata->record_crc != crc32_data((const uint8_t *)metadata,
                                          offsetof(AppMetadata, record_crc))))
  {
    return 0U;
  }
  return (crc32_data((const uint8_t *)BOOTLOADER_APP_ADDRESS, metadata->size) ==
          metadata->image_crc) ? 1U : 0U;
}

/* 消费 APP 发来的进入升级请求。
 * APP 收到主机 EnterUpgrade 后会把 BOOTLOADER_REQUEST_MAGIC 写入 RTC 备份寄存器
 * 并软复位。Bootloader 上电后读取到该标志就停在升级模式，然后立即清除标志。
 */
static uint8_t consume_upgrade_request(void)
{
  uint8_t requested;

  __HAL_RCC_PWR_CLK_ENABLE();
  HAL_PWR_EnableBkUpAccess();
  requested = (RTC->BKP0R == BOOTLOADER_REQUEST_MAGIC) ? 1U : 0U;
  if (requested != 0U)
  {
    RTC->BKP0R = 0U;
  }
  HAL_PWR_DisableBkUpAccess();
  return requested;
}

/* 跳转到 APP。
 * 跳转前需要关闭 Bootloader 使用过的外设、清理 SysTick 和 NVIC，
 * 设置向量表偏移到 APP 起始地址，并切换 MSP 到 APP 向量表里的栈顶。
 */
static void jump_to_app(void)
{
  uint32_t app_stack;
  uint32_t app_reset;
  uint32_t index;
  AppEntry entry;

  if (app_is_valid() == 0U)
  {
    return;
  }
  app_stack = *(volatile uint32_t *)BOOTLOADER_APP_ADDRESS;
  app_reset = *(volatile uint32_t *)(BOOTLOADER_APP_ADDRESS + 4U);
  entry = (AppEntry)app_reset;

  bootloader_led_off();
  HAL_UART_DeInit(bl_uart);
  HAL_RCC_DeInit();
  HAL_DeInit();
  SysTick->CTRL = 0U;
  SysTick->LOAD = 0U;
  SysTick->VAL = 0U;
  __disable_irq();
  for (index = 0U; index < 8U; ++index)
  {
    NVIC->ICER[index] = 0xFFFFFFFFU;
    NVIC->ICPR[index] = 0xFFFFFFFFU;
  }
  SCB->VTOR = BOOTLOADER_APP_ADDRESS;
  __DSB();
  __ISB();
  __set_CONTROL(0U);
  __set_MSP(app_stack);
  __enable_irq();
  entry();
}

/* 回复设备信息。
 * payload[0] 为 run_mode：1 表示当前在 Bootloader；APP 侧通常返回 0。
 * 主机工具通过它决定是直接升级，还是先发 EnterUpgrade 让 APP 复位进 Bootloader。
 */
static void send_info(uint8_t sequence)
{
  const AppMetadata *metadata = (const AppMetadata *)BOOTLOADER_METADATA_ADDRESS;
  uint8_t payload[28];

  payload[0] = 1U; /* run_mode: Bootloader */
  payload[1] = IAP_PROTOCOL_MAJOR;
  payload[2] = IAP_PROTOCOL_MINOR;
  payload[3] = app_is_valid();
  write_u32_le(&payload[4], device_id());
  write_u16_le(&payload[8], BL_HARDWARE_VERSION);
  write_u32_le(&payload[10], BOOTLOADER_VERSION);
  write_u32_le(&payload[14], metadata->version);
  write_u32_le(&payload[18], BOOTLOADER_APP_ADDRESS);
  write_u32_le(&payload[22], BOOTLOADER_APP_CAPACITY);
  write_u16_le(&payload[26], IAP_MAX_DATA_SIZE);
  send_frame(IAP_RSP_DEVICE_INFO, sequence, payload, sizeof(payload));
}

/* 处理升级开始命令。
 * START payload 长度固定 22 字节，包含 target、硬件版本范围、APP 版本、
 * 镜像大小、镜像 CRC32 和主机期望的 chunk 大小。
 */
static void handle_start(const IapPacket *packet)
{
  uint32_t target;
  uint32_t version;
  uint32_t size;
  uint32_t crc;
  uint16_t hw_min;
  uint16_t hw_max;
  uint16_t chunk_size;

  if (packet->length != 22U)
  {
    send_nack(packet->command, packet->sequence, IAP_ERR_BAD_LENGTH, upgrade.written);
    return;
  }

  target = read_u32_le(&packet->payload[0]);
  hw_min = read_u16_le(&packet->payload[4]);
  hw_max = read_u16_le(&packet->payload[6]);
  version = read_u32_le(&packet->payload[8]);
  size = read_u32_le(&packet->payload[12]);
  crc = read_u32_le(&packet->payload[16]);
  chunk_size = read_u16_le(&packet->payload[20]);

  if ((target != device_id()) ||
      (BL_HARDWARE_VERSION < hw_min) || (BL_HARDWARE_VERSION > hw_max))
  {
    send_nack(packet->command, packet->sequence, IAP_ERR_TARGET_MISMATCH, 0U);
    return;
  }
  if ((size < 8U) || (size > BOOTLOADER_APP_CAPACITY) ||
      (chunk_size == 0U) || (chunk_size > IAP_MAX_DATA_SIZE))
  {
    send_nack(packet->command, packet->sequence, IAP_ERR_SIZE, 0U);
    return;
  }
  if ((metadata_begin(size, crc, version) == 0U) ||
      (flash_erase(FLASH_SECTOR_1, 9U) == 0U))
  {
    metadata_write_status(BL_STATUS_FAILED);
    send_nack(packet->command, packet->sequence, IAP_ERR_FLASH, 0U);
    return;
  }

  upgrade.active = 1U;
  bootloader_led_blink();
  upgrade.chunk_size = chunk_size;
  upgrade.image_size = size;
  upgrade.image_crc = crc;
  upgrade.image_version = version;
  upgrade.written = 0U;
  send_ack(packet->command, packet->sequence, 0U);
  upgrade.expected_sequence++;
}

/* 处理升级数据命令。
 * DATA payload = offset(4) + data_length(2) + data。
 * offset 必须等于当前 written，保证主机和设备写入进度严格一致。
 */
static void handle_data(const IapPacket *packet)
{
  uint32_t offset;
  uint16_t data_length;

  if ((upgrade.active == 0U) || (packet->length < 6U))
  {
    send_nack(packet->command, packet->sequence,
              (upgrade.active == 0U) ? IAP_ERR_BAD_STATE : IAP_ERR_BAD_LENGTH,
              upgrade.written);
    return;
  }

  offset = read_u32_le(&packet->payload[0]);
  data_length = read_u16_le(&packet->payload[4]);
  if ((packet->length != (uint16_t)(data_length + 6U)) ||
      (offset != upgrade.written) ||
      (data_length == 0U) ||
      (data_length > upgrade.chunk_size) ||
      (data_length > (upgrade.image_size - upgrade.written)))
  {
    send_nack(packet->command, packet->sequence, IAP_ERR_OFFSET, upgrade.written);
    return;
  }

  if (flash_write(BOOTLOADER_APP_ADDRESS + offset, &packet->payload[6],
                  data_length) == 0U)
  {
    metadata_write_status(BL_STATUS_FAILED);
    upgrade.active = 0U;
    send_nack(packet->command, packet->sequence, IAP_ERR_FLASH, upgrade.written);
    return;
  }

  upgrade.written += data_length;
  send_ack(packet->command, packet->sequence, upgrade.written);
  upgrade.expected_sequence++;
}

/* 处理升级结束命令。
 * END 时会重新计算整个 APP 区 CRC32，并检查 APP 向量表是否合法。
 * 全部通过后把元数据状态写成 VALID，然后短暂延时并跳转新 APP。
 */
static void handle_end(const IapPacket *packet)
{
  uint32_t size;
  uint32_t crc;
  uint32_t actual_crc;

  if ((upgrade.active == 0U) || (packet->length != 8U))
  {
    send_nack(packet->command, packet->sequence,
              (upgrade.active == 0U) ? IAP_ERR_BAD_STATE : IAP_ERR_BAD_LENGTH,
              upgrade.written);
    return;
  }

  size = read_u32_le(&packet->payload[0]);
  crc = read_u32_le(&packet->payload[4]);
  bootloader_led_blink();
  actual_crc = crc32_data((const uint8_t *)BOOTLOADER_APP_ADDRESS, upgrade.image_size);
  bootloader_led_blink();
  if ((size != upgrade.image_size) || (crc != upgrade.image_crc) ||
      (upgrade.written != upgrade.image_size) ||
      (actual_crc != upgrade.image_crc) ||
      (app_vector_is_valid() == 0U))
  {
    metadata_write_status(BL_STATUS_FAILED);
    upgrade.active = 0U;
    send_nack(packet->command, packet->sequence, IAP_ERR_FW_CRC32, upgrade.written);
    return;
  }

  if (metadata_write_status(BL_STATUS_VALID) == 0U)
  {
    upgrade.active = 0U;
    send_nack(packet->command, packet->sequence, IAP_ERR_FLASH, upgrade.written);
    return;
  }

  upgrade.active = 0U;
  bootloader_led_off();
  send_ack(packet->command, packet->sequence, upgrade.written);
  upgrade.expected_sequence++;
  HAL_Delay(50U);
  jump_to_app();
}

/* 根据命令号分发协议帧。
 * 先处理查询重发、ACK 重发和序号校验，再进入具体命令处理。
 */
static void process_packet(const IapPacket *packet)
{
  if ((upgrade.expected_sequence != 0U) &&
      (packet->command == IAP_CMD_QUERY_INFO) &&
      (packet->sequence == (uint8_t)(upgrade.expected_sequence - 1U)))
  {
    send_info(packet->sequence);
    return;
  }
  if ((last_valid != 0U) && (packet->command == last_command) &&
      (packet->sequence == last_sequence))
  {
    if (last_error == IAP_STATUS_OK)
    {
      send_ack(last_command, last_sequence, last_next_offset);
    }
    return;
  }
  if (packet->sequence != upgrade.expected_sequence)
  {
    send_nack(packet->command, packet->sequence, IAP_ERR_SEQ, upgrade.written);
    return;
  }

  switch (packet->command)
  {
    case IAP_CMD_QUERY_INFO:
      if (packet->length == 0U)
      {
        send_info(packet->sequence);
        upgrade.expected_sequence++;
      }
      else
      {
        send_nack(packet->command, packet->sequence, IAP_ERR_BAD_LENGTH, upgrade.written);
      }
      break;

    case IAP_CMD_UPGRADE_START:
      handle_start(packet);
      break;

    case IAP_CMD_UPGRADE_DATA:
      handle_data(packet);
      break;

    case IAP_CMD_UPGRADE_END:
      handle_end(packet);
      break;

    case IAP_CMD_UPGRADE_ABORT:
      if (upgrade.active != 0U)
      {
        metadata_write_status(BL_STATUS_FAILED);
        upgrade.active = 0U;
        bootloader_led_on();
      }
      send_ack(packet->command, packet->sequence, upgrade.written);
      upgrade.expected_sequence++;
      break;

    case IAP_CMD_RUN_APP:
      if ((packet->length == 0U) && (app_is_valid() != 0U))
      {
        send_ack(packet->command, packet->sequence, upgrade.written);
        HAL_Delay(20U);
        jump_to_app();
      }
      else
      {
        send_nack(packet->command, packet->sequence, IAP_ERR_APP_INVALID,
                  upgrade.written);
      }
      break;

    default:
      send_nack(packet->command, packet->sequence, IAP_ERR_BAD_CMD, upgrade.written);
      break;
  }
}

/* Bootloader 主入口。
 * 典型流程：
 * 1. 初始化串口上下文和指示灯。
 * 2. 如果 APP 没有请求升级且 APP 有效，则短暂等待主机查询；
 *    窗口内只认合法的 IAP 设备信息查询（0x10）作为进入升级模式的信号，
 *    其它帧（主机业务轮询、串口干扰、CRC 错误帧）一律忽略并继续等待，
 *    超时后直接跳转 APP，避免主机轮询帧把设备永久截留在 Bootloader。
 * 3. 一旦进入 Bootloader 模式，就永久等待主机 IAP 命令。
 */
void Bootloader_Run(UART_HandleTypeDef *huart)
{
  IapPacket packet;
  uint8_t error;

  bl_uart = huart;
  bootloader_led_init();
  led_tick = HAL_GetTick();
  __HAL_UART_FLUSH_DRREGISTER(bl_uart);
  memset(&upgrade, 0, sizeof(upgrade));

  if ((consume_upgrade_request() == 0U) && (app_is_valid() != 0U))
  {
    uint32_t window_start = HAL_GetTick();

    for (;;)
    {
      uint32_t elapsed = HAL_GetTick() - window_start;
      uint32_t remaining;

      if (elapsed >= BL_START_TIMEOUT_MS)
      {
        jump_to_app();
      }
      remaining = BL_START_TIMEOUT_MS - elapsed;
      error = receive_packet(&packet, remaining);
      if (error == BL_ERROR_TIMEOUT)
      {
        jump_to_app();
      }
      /* 只有合法的 IAP 查询帧才说明主机真的要升级；
       * 业务轮询帧或坏帧都忽略，继续等到超时跳 APP。 */
      if ((error == IAP_STATUS_OK) && (packet.command == IAP_CMD_QUERY_INFO))
      {
        process_packet(&packet);
        break;
      }
    }
  }

  for (;;)
  {
    error = receive_packet(&packet, HAL_MAX_DELAY);
    if (error == IAP_STATUS_OK)
    {
      process_packet(&packet);
    }
    else if (error == IAP_ERR_BAD_LENGTH)
    {
      send_nack(packet.command, packet.sequence, error, upgrade.written);
    }
  }
}
