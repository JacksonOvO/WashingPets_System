#ifndef AUTO_DEVICE_H
#define AUTO_DEVICE_H

#include "stm32f4xx_hal.h"

/*
 * 上位机命令等待旁路开关：
 * 1U：开发调试阶段，不等待上位机，所有合法 AutoCommand 均视为已经收到。
 * 0U：上位机联调阶段，必须由协议解析层调用 AutoDevice_SetCommand() 后，
 *     流程中的 AutoDevice_ConsumeCommand() 才会返回成功。
 */
#define AUTO_DEVICE_HOST_COMMAND_BYPASS  0U

/*
 * 上位机空跑联调开关：
 * 0U：真实硬件模式，读取真实传感器并驱动真实GPIO/485执行器。
 * 1U：空跑模式，只保留232协议交互；传感器、输入和执行器全部使用软件模拟。
 */
#define APP_DRY_RUN_ENABLE  0U

/*
 * 下门门磁临时旁路开关：
 * 1U：下门门磁未安装时，自动流程和互锁把下门关闭信号视为已到位；
 *     进/出舱流程中的“等待下门打开”步骤也直接放行。
 * 0U：读取真实下门门磁。
 */
#define AUTO_LOWER_DOOR_SENSOR_BYPASS  0U

/*
 * RS485流量阀临时旁路开关：
 * 0U：未安装流量阀，不发送控制命令，自动流程直接跳过相关等待。
 * 1U：安装流量阀后启用真实控制和流程等待。
 */
#define AUTO_FLOW_VALVE_ENABLE  0U

/*
 * 自动工艺公共设备接口。
 *
 * 本模块把流程代码与具体GPIO、传感器驱动和上位机协议隔离开：
 * 1. 流程文件只使用“设备名称”，不直接散落GPIO端口和引脚；
 * 2. 所有开关量均按用户提供的硬件表定义为低电平有效；
 * 3. 尚未完成的温度和RS485流量阀驱动通过更新接口接入；
 * 4. 上位机协议完成后，只需调用命令和运行状态接口，无需修改流程表。
 */

typedef enum
{
  AUTO_INPUT_BATH_LIQUID_LOW = 0,          /* PA3: IN1: 沐浴液1低液位 */
  AUTO_INPUT_FRAGRANCE_LOW = 1,            /* PA4: IN2: 沐浴液2低液位 */
  AUTO_INPUT_SOFTENER_LOW = 2,             /* PA5: IN3: 沐浴液3低液位 */
  AUTO_INPUT_DEODORIZER_LOW = 3,           /* PA6: IN4: 沐浴液4低液位 */
  AUTO_INPUT_CLEAN_LIQUID_LOW = 4,         /* PA7: IN5: 清洁液低液位 */
  AUTO_INPUT_DISINFECTANT_LOW = 5,         /* PC4: IN6: 消毒液低液位 */
  AUTO_INPUT_TANK_LOW = 6,                 /* PC5: IN7: 水箱低液位 */
  AUTO_INPUT_TANK_MIDDLE = 7,              /* PB0: IN8: 水箱中液位 */
  AUTO_INPUT_TANK_HIGH = 8,                /* PB1: IN9: 水箱高液位 */
  AUTO_INPUT_EMERGENCY_STOP = 9,           /* PB2: IN10: 急停开关 */
  AUTO_INPUT_UPPER_DOOR_OPEN = 10,         /* PF11: IN11: 上门开门到位 */
  AUTO_INPUT_SPARE_1 = 11,                 /* PF12: IN12: 备用1 */
  AUTO_INPUT_SPARE_2 = 12,                 /* PF13: IN13: 备用2 */
  AUTO_INPUT_SPARE_3 = 13,                 /* PF14: IN14: 备用3 */
  AUTO_INPUT_SPARE_4 = 14,                 /* PE10: IN15: 备用4 */
  AUTO_INPUT_SPARE_5 = 15,                 /* PE11: IN16: 备用5 */
  AUTO_INPUT_SPARE_6 = 16,                 /* PE12: IN17: 备用6 */
  AUTO_INPUT_SPARE_7 = 17,                 /* PE13: IN18: 备用7 */
  AUTO_INPUT_SPARE_8 = 18,                 /* PE14: IN19: 备用8 */
  AUTO_INPUT_UPPER_DOOR_CLOSED = 19,       /* PF15: 独立上门关门到位 (bit19) */
  AUTO_INPUT_LOWER_DOOR_CLOSED = 20,       /* PG0: 独立下门关门到位 (bit20) */
  AUTO_INPUT_COUNT = 21
} AutoInputPoint;

#define AUTO_INPUT_FIRST AUTO_INPUT_BATH_LIQUID_LOW
#define AUTO_INPUT_GPIO_COUNT 19U

typedef enum
{
  AUTO_OUTPUT_OXYGEN_MACHINE = 1,       /* OUT1-PB12 */
  AUTO_OUTPUT_EXHAUST_UV_LAMP = 2,       /* OUT2-PB13 */
  AUTO_OUTPUT_UV_LAMP = 3,              /* OUT3-PB14 */
  AUTO_OUTPUT_BLOWER = 4,               /* OUT4-PB15: air and heat together */
  AUTO_OUTPUT_SWING_MOTOR = 5,          /* OUT5-PD8 */
  AUTO_OUTPUT_FLOOR_LIFT = 6,           /* OUT6-PD9 */
  AUTO_OUTPUT_MAIN_POWER = 7,           /* OUT7-PD10 */
  AUTO_OUTPUT_AIR_FRESHENER = 8,         /* OUT8-PD11 */
  AUTO_OUTPUT_BATH_PUMP = 9,            /* OUT9-PD12 */
  AUTO_OUTPUT_SELF_CLEAN_PUMP = 10,     /* OUT10-PD13 */
  AUTO_OUTPUT_LIGHTING = 11,           /* OUT11-PB4 */
  AUTO_OUTPUT_FRONT_DOOR_LIGHT = 12,    /* OUT12-PB5 */
  AUTO_OUTPUT_DOOR_OPEN = 13,           /* OUT13-PB6 */
  AUTO_OUTPUT_DOOR_CLOSE = 14,          /* OUT14-PB7 */
  AUTO_OUTPUT_DOOR_STOP = 15,           /* OUT15-PB8 */
  AUTO_OUTPUT_MANUAL_CLEAN_VALVE = 16,  /* OUT16-PB9 */
  AUTO_OUTPUT_TAP_WATER_VALVE = 17,     /* OUT17-PE0 */
  AUTO_OUTPUT_STANDBY2_VALVE = 18,      /* OUT18-PE1 */
  AUTO_OUTPUT_BYPASS_VALVE = 19,        /* OUT19-PB3 */
  AUTO_OUTPUT_SHOWER_MIDDLE = 20,       /* OUT20-PE2 */
  AUTO_OUTPUT_SHOWER_UPPER = 21,        /* OUT21-PE3 */
  AUTO_OUTPUT_SELF_CLEAN_TOP_VALVE = 22,/* OUT22-PE4 */
  AUTO_OUTPUT_SELF_CLEAN_LOWER_VALVE = 23, /* OUT23-PE5 */
  AUTO_OUTPUT_SELF_CLEAN_TOILET_VALVE = 24,/* OUT24-PE6 */
  AUTO_OUTPUT_BATH_LIQUID_VALVE = 25,   /* OUT25-PC13: selection 0 */
  AUTO_OUTPUT_FRAGRANCE_VALVE = 26,     /* OUT26-PC14: selection 1 */
  AUTO_OUTPUT_SOFTENER_VALVE = 27,      /* OUT27-PC15: selection 2 */
  AUTO_OUTPUT_DEODORIZER_VALVE = 28,    /* OUT28-PF0: selection 3 */
  AUTO_OUTPUT_CONDITIONER_VALVE = 29,   /* OUT29-PF1: cleaning liquid */
  AUTO_OUTPUT_DISINFECTANT_VALVE = 30,  /* OUT30-PF2 */
  AUTO_OUTPUT_BOOSTER_PUMP_RUN = 31,    /* OUT31-PF3 */
  AUTO_OUTPUT_SPARE_1 = 32,             /* OUT32-PF10 */
  AUTO_OUTPUT_SPARE_2 = 33,             /* OUT33-PC0 */
  AUTO_OUTPUT_SPARE_3 = 34,             /* OUT34-PC1 */
  AUTO_OUTPUT_SPARE_4 = 35,             /* OUT35-PC2 */
  AUTO_OUTPUT_SPARE_5 = 36,             /* OUT36-PC3 */
  AUTO_OUTPUT_SPARE_6 = 37,             /* OUT37-PA0 */
  AUTO_OUTPUT_SPARE_7 = 38,             /* OUT38-PA1 */
  AUTO_OUTPUT_SPARE_8 = 39,             /* OUT39-PA2 */
  AUTO_OUTPUT_UPPER_LOCK = 40,          /* MAGLOCK1 EN-PF4 */
  AUTO_OUTPUT_LOWER_LOCK = 41,          /* MAGLOCK2 EN-PF5 */
  AUTO_OUTPUT_LEFT_LOCK = 42,           /* MAGLOCK3 EN-PF6 */
  AUTO_OUTPUT_RIGHT_LOCK = 43,          /* MAGLOCK4 EN-PF7 */
  AUTO_OUTPUT_REAR_LOCK_1 = 44,         /* MAGLOCK5 EN-PF8 */
  AUTO_OUTPUT_REAR_LOCK_2 = 45,         /* MAGLOCK6 EN-PF9 */
  AUTO_OUTPUT_COUNT = 46
} AutoOutputPoint;

#define AUTO_OUTPUT_FIRST AUTO_OUTPUT_OXYGEN_MACHINE
#define AUTO_OUTPUT_GPIO_COUNT 39U

typedef enum
{
  AUTO_COMMAND_START = 0,
  AUTO_COMMAND_OPEN_UPPER_DOOR,
  AUTO_COMMAND_OPEN_LOWER_DOOR,
  AUTO_COMMAND_CLOSE_UPPER_DOOR,
  AUTO_COMMAND_CLOSE_LOWER_DOOR,
  AUTO_COMMAND_AUTO_CLOSE_DOOR,
  AUTO_COMMAND_CONTINUE,
  AUTO_COMMAND_COUNT
} AutoCommand;

typedef enum
{
  AUTO_REPORT_UPPER_DOOR_OPEN = 0,
  AUTO_REPORT_LOWER_DOOR_OPEN,
  AUTO_REPORT_LOWER_DOOR_NOT_CLOSED,
  AUTO_REPORT_LOWER_DOOR_LOCKED,
  AUTO_REPORT_UPPER_DOOR_NOT_CLOSED,
  AUTO_REPORT_UPPER_DOOR_LOCKED,
  AUTO_REPORT_ENTRY_DONE,
  AUTO_REPORT_WASH_READY_DONE,
  AUTO_REPORT_SPRAY_DONE,
  AUTO_REPORT_BATH_DONE,
  AUTO_REPORT_CLEAN_DONE,
  AUTO_REPORT_DRY_DONE,
  AUTO_REPORT_EXIT_PREPARE_DONE,
  AUTO_REPORT_EXIT_DONE,
  AUTO_REPORT_SELF_CLEAN_DONE,
  AUTO_REPORT_SENSOR_FAULT,
  AUTO_REPORT_TIMEOUT_FAULT,
  AUTO_REPORT_FAULT_CLEARED,
  AUTO_REPORT_COUNT
} AutoReport;

typedef enum
{
  AUTO_FAULT_NONE = 0x0000U,
  AUTO_FAULT_EMERGENCY_STOP = 0x0001U,
  AUTO_FAULT_FLOW_JUMP_FAILED = 0x0101U,
  AUTO_FAULT_OUTPUT_COMMAND_FAILED = 0x0102U,
  AUTO_FAULT_PROCESS_ABORTED = 0x0103U,
  AUTO_FAULT_UPPER_LOCK_OUTPUT_FAILED = 0x0201U,
  AUTO_FAULT_LOWER_LOCK_OUTPUT_FAILED = 0x0202U,
  AUTO_FAULT_UPPER_DOOR_LOCK_CHECK_FAILED = 0x0203U,
  AUTO_FAULT_LOWER_DOOR_LOCK_CHECK_FAILED = 0x0204U,
  AUTO_FAULT_DOOR_NOT_CLOSED = 0x0205U,
  AUTO_FAULT_UPPER_DOOR_OPEN_TIMEOUT = 0x0206U,
  AUTO_FAULT_TANK_TEMP_SENSOR_INVALID = 0x0301U,
  AUTO_FAULT_PIPE_TEMP_SENSOR_INVALID = 0x0302U,
  AUTO_FAULT_PIPE_TEMP_NOT_READY = 0x0303U,
  AUTO_FAULT_TANK_TEMP_NOT_READY = 0x0304U,
  AUTO_FAULT_TANK_HIGH_WAIT_TOO_LONG = 0x0305U,
  AUTO_FAULT_TANK_LOW_PROTECT = 0x0306U,
  AUTO_FAULT_INLET_TEMP_SENSOR_INVALID = 0x0308U,
  AUTO_FAULT_SPARE_TEMP_SENSOR_INVALID = 0x0309U,
  AUTO_FAULT_CABIN_TEMP_SENSOR_INVALID = 0x0401U,
  AUTO_FAULT_CABIN_HUMIDITY_SENSOR_INVALID = 0x0402U,
  AUTO_FAULT_OUTDOOR_TEMP_SENSOR_INVALID = 0x0403U,
  AUTO_FAULT_OXYGEN_SENSOR_INVALID = 0x0404U,
  AUTO_FAULT_OZONE_SENSOR_INVALID = 0x0405U,
  AUTO_FAULT_BATH_LIQUID_LOW = 0x0501U,
  AUTO_FAULT_DISINFECTANT_LOW = 0x0502U,
  AUTO_FAULT_BOOSTER_PUMP_START_FAILED = 0x0503U,
  AUTO_FAULT_BOOSTER_PUMP_PRESSURE_SET_FAILED = 0x0504U,
  AUTO_FAULT_SHOWER_VALVE_OUTPUT_FAILED = 0x0505U,
  AUTO_FAULT_FLOW_VALVE_OUTPUT_FAILED = 0x0506U,
  AUTO_FAULT_CLEAN_LIQUID_LOW = 0x0507U,
  AUTO_FAULT_FAN_OUTPUT_FAILED = 0x0601U,
  AUTO_FAULT_HEATER_OUTPUT_FAILED = 0x0602U,
  AUTO_FAULT_DRY_CABIN_TEMP_SENSOR_INVALID = 0x0603U,
  AUTO_FAULT_DRY_TEMP_CONTROL_FAILED = 0x0604U,
  AUTO_FAULT_SELF_CLEAN_NOT_ALLOWED = 0x0701U,
  AUTO_FAULT_SELF_CLEAN_FLOW_ABNORMAL = 0x0702U,
  AUTO_FAULT_SELF_CLEAN_DISINFECTION_NOT_ALLOWED = 0x0703U,
  AUTO_FAULT_485_LEFT_FAN_COMM = 0x0801U,
  AUTO_FAULT_485_RIGHT_FAN_COMM = 0x0802U,
  AUTO_FAULT_485_BOTTOM_FAN_COMM = 0x0803U,
  AUTO_FAULT_485_TOP_EXHAUST_COMM = 0x0804U,
  AUTO_FAULT_485_LEFT_HEATER_COMM = 0x0805U,
  AUTO_FAULT_485_RIGHT_HEATER_COMM = 0x0806U,
  AUTO_FAULT_485_BOTTOM_HEATER_COMM = 0x0807U,
  AUTO_FAULT_485_BOOSTER_PUMP_COMM = 0x0808U,
  AUTO_FAULT_485_PROPORTIONAL_VALVE_COMM = 0x0809U,
  AUTO_FAULT_485_FAN_PWM_COMM = 0x080AU, /* QX-DO24 communication/configuration mismatch. */
  AUTO_FAULT_SELF_TEST_NOT_ALLOWED = 0x0901U,
  AUTO_FAULT_SELF_TEST_OVERPRESSURE = 0x0902U,
  AUTO_FAULT_SELF_TEST_OVER_TEMPERATURE = 0x0903U,
  AUTO_FAULT_SELF_TEST_LEVEL_INVALID = 0x0904U
} AutoFaultCode;

typedef struct
{
  int16_t inlet_temperature_x10; /* 进水口温度，单位0.1℃。 */
  int16_t tank_temperature_x10;  /* 水箱温度，单位0.1℃。 */
  int16_t pipe_temperature_x10;  /* 管路出水温度，单位0.1℃。 */
  int16_t spare_temperature_x10; /* 混合水温/CH4，单位0.1℃。 */
  uint32_t inlet_update_tick;
  uint32_t tank_update_tick;
  uint32_t pipe_update_tick;
  uint32_t spare_update_tick;
  uint8_t inlet_temperature_valid;
  uint8_t tank_temperature_valid;
  uint8_t pipe_temperature_valid;
  uint8_t spare_temperature_valid;
} AutoSensorData;

void AutoDevice_Init(void);
uint8_t AutoDevice_ReadInput(AutoInputPoint point);
uint8_t AutoDevice_ReadInputRaw(AutoInputPoint point);
void AutoDevice_ProcessInputs(void);
HAL_StatusTypeDef AutoDevice_SetOutput(AutoOutputPoint point, uint8_t enabled);
HAL_StatusTypeDef AutoDevice_SetTopFanRunning(uint8_t enabled);
HAL_StatusTypeDef AutoDevice_SetBlower(uint8_t enabled);
uint8_t AutoDevice_IsBoosterPumpRunning(void);
void AutoDevice_AllDangerousOutputsOff(void);
void AutoDevice_SetAllShowerValves(uint8_t enabled);
HAL_StatusTypeDef AutoDevice_SetBoosterPumpPercent(uint8_t percent);
HAL_StatusTypeDef AutoDevice_SetBoosterPumpRunning(uint8_t enabled);

void AutoDevice_UpdateTankTemperature(int16_t temperature_x10, uint8_t valid);
void AutoDevice_UpdatePipeTemperature(int16_t temperature_x10, uint8_t valid);
void AutoDevice_UpdateTemperatureChannels(const int16_t temperature_x10[4],
                                          const uint8_t valid[4]);
void AutoDevice_InvalidateTemperatureChannels(void);
uint8_t AutoDevice_GetSensorData(AutoSensorData *data);

/* RS485流量控制阀驱动完成后，可重写该弱函数发送真实控制帧。 */
HAL_StatusTypeDef AutoDevice_FlowValveWritePercent(uint8_t percent);
HAL_StatusTypeDef AutoDevice_SetFlowPercent(uint8_t percent);
uint8_t AutoDevice_GetFlowTargetPercent(void);

void AutoDevice_SetCommand(AutoCommand command);
void AutoDevice_ClearHostCommands(void);
uint8_t AutoDevice_ConsumeCommand(AutoCommand command);
void AutoDevice_SetRecipeSelectedFlag(void);
uint8_t AutoDevice_ConsumeRecipeSelectedFlag(void);
void AutoDevice_SetBathLiquidSelection(uint8_t selection);
uint8_t AutoDevice_GetBathLiquidSelection(void);
void AutoDevice_ClearProcessSignals(void);
void AutoDevice_SetPaused(uint8_t paused);
void AutoDevice_SetStopRequested(uint8_t requested);
uint8_t AutoDevice_IsPaused(void);
uint8_t AutoDevice_IsStopRequested(void);
uint8_t AutoDevice_IsEmergencyStopped(void);
uint32_t AutoDevice_GetProcessTick(void);

/* 上位机状态上报完成后，可重写该弱函数封装并发送真实协议帧。 */
void AutoDevice_OnReport(AutoReport report);
void AutoDevice_Report(AutoReport report);
void AutoDevice_SetFault(AutoFaultCode code);
AutoFaultCode AutoDevice_GetFault(void);
void AutoDevice_ClearFault(void);
void AutoDevice_ReportFault(AutoFaultCode code, AutoReport report);
void AutoDevice_RequestHostInteraction(uint8_t interaction_type);

#endif
