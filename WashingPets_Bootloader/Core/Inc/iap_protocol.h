#ifndef IAP_PROTOCOL_H
#define IAP_PROTOCOL_H

#define IAP_HEAD_0                 0x55U
#define IAP_HEAD_1                 0xAAU
#define IAP_MAX_DATA_SIZE          256U
#define IAP_MAX_PAYLOAD_SIZE       (IAP_MAX_DATA_SIZE + 6U)

#define IAP_PROTOCOL_MAJOR         1U
#define IAP_PROTOCOL_MINOR         1U

#define IAP_CMD_QUERY_INFO         0x10U
#define IAP_CMD_ENTER_UPGRADE      0x11U
#define IAP_CMD_UPGRADE_START      0x20U
#define IAP_CMD_UPGRADE_DATA       0x21U
#define IAP_CMD_UPGRADE_END        0x22U
#define IAP_CMD_UPGRADE_ABORT      0x23U
#define IAP_CMD_RUN_APP            0x25U

#define IAP_RSP_DEVICE_INFO        0x90U
#define IAP_RSP_ACK                0xF0U
#define IAP_RSP_NACK               0xF1U

#define IAP_STATUS_OK              0x00U
#define IAP_ERR_BAD_CMD            0x01U
#define IAP_ERR_BAD_LENGTH         0x02U
#define IAP_ERR_BAD_CRC16          0x03U
#define IAP_ERR_BAD_STATE          0x04U
#define IAP_ERR_BUSY               0x05U
#define IAP_ERR_SEQ                0x06U
#define IAP_ERR_TARGET_MISMATCH    0x08U
#define IAP_ERR_SIZE               0x0AU
#define IAP_ERR_OFFSET             0x0BU
#define IAP_ERR_FLASH              0x0CU
#define IAP_ERR_FW_CRC32           0x0DU
#define IAP_ERR_APP_INVALID        0x0EU

#endif
