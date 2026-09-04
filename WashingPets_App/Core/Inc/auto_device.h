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
  AUTO_INPUT_DISINFECTANT_LOW = 0, /* PA3：消毒液低液位。 */
  AUTO_INPUT_BATH_LIQUID_LOW,     /* PA4：沐浴液低液位。 */
  AUTO_INPUT_FRAGRANCE_LOW,       /* PA5：芬香液低液位。 */
  AUTO_INPUT_SOFTENER_LOW,        /* PA6：柔顺液低液位。 */
  AUTO_INPUT_DEODORIZER_LOW,      /* PA7：除螨液低液位。 */
  AUTO_INPUT_CONDITIONER_LOW,     /* PC4：护毛素低液位。 */
  AUTO_INPUT_TANK_LOW,            /* PC5：蓄水箱下液位。 */
  AUTO_INPUT_TANK_MIDDLE,         /* PB0：蓄水箱中液位。 */
  AUTO_INPUT_TANK_HIGH,           /* PB1：蓄水箱上液位。 */
  AUTO_INPUT_EMERGENCY_STOP,      /* PB2：急停开关。 */
  AUTO_INPUT_DRAIN_PIPE_LEVEL,    /* PF12：排水管液位。 */
  AUTO_INPUT_UPPER_LIGHT_CURTAIN, /* PF13：上门光幕。 */
  AUTO_INPUT_LOWER_LIGHT_CURTAIN, /* PF14：下门光幕。 */
  AUTO_INPUT_UPPER_DOOR_CLOSED,   /* PF15：上门关闭到位。 */
  AUTO_INPUT_LOWER_DOOR_CLOSED,   /* PG0：下门关闭到位。 */
  AUTO_INPUT_LEFT_LOCK_CLOSED,    /* PG1：左电插锁到位。 */
  AUTO_INPUT_RIGHT_LOCK_CLOSED,   /* PE7：右电插锁到位。 */
  AUTO_INPUT_REAR_LOCK_1_CLOSED,  /* PE8：后电插锁1到位。 */
  AUTO_INPUT_REAR_LOCK_2_CLOSED,  /* PE9：后电插锁2到位。 */
  AUTO_INPUT_COUNT
} AutoInputPoint;

typedef enum
{
  AUTO_OUTPUT_EXHAUST_FAN= 1,               /* OUT1-PB12：排气扇。 */
  AUTO_OUTPUT_OXYGEN_MACHINE,               /* OUT2-PB13：制氧机。 */
  AUTO_OUTPUT_AIR_FRESHENER,                /* OUT3-PB14：空气清新剂。 */
  AUTO_OUTPUT_MANUAL_CLEAN_VALVE,           /* OUT4-PB15：手动清洁电磁阀。 */
  AUTO_OUTPUT_EXHAUST_UV_LAMP,               /* OUT5-PD8：排风紫外线灯。 */
  AUTO_OUTPUT_WATER_HEATER,                 /* OUT6-PD9：热水器。 */
  AUTO_OUTPUT_TAP_WATER_VALVE,              /* OUT7-PD10：自来水阀。 */
  AUTO_OUTPUT_SCREW_PUMP,                   /* OUT8-PD11：螺动泵。 */
  AUTO_OUTPUT_SHOWER_UPPER,                 /* OUT9-PD12：淋浴阀上。 */
  AUTO_OUTPUT_SHOWER_MIDDLE,                /* OUT10-PD13：淋浴阀中。 */
  AUTO_OUTPUT_SHOWER_LOWER,                 /* OUT11-PB4：淋浴阀下。 */
  AUTO_OUTPUT_SHOWER_REAR,                  /* OUT12-PB5：淋浴阀后。 */
  AUTO_OUTPUT_BATH_LIQUID_VALVE,            /* OUT13-PB6：沐浴液阀。 */
  AUTO_OUTPUT_FRAGRANCE_VALVE,              /* OUT14-PB7：芬香液阀。 */
  AUTO_OUTPUT_SOFTENER_VALVE,               /* OUT15-PB8：柔顺液阀。 */
  AUTO_OUTPUT_DEODORIZER_VALVE,             /* OUT16-PB9：除螨液阀。 */
  AUTO_OUTPUT_CONDITIONER_VALVE,            /* OUT17-PE0：清洁液阀。 */
  AUTO_OUTPUT_DISINFECTANT_VALVE,           /* OUT18-PE1：消毒液阀。 */
  AUTO_OUTPUT_UV_LAMP,                       /* OUT19-PB3：紫外线灯。 */
  AUTO_OUTPUT_SELF_CLEAN_TOP_VALVE,         /* OUT20-PE2：自清洁顶部阀。 */
  AUTO_OUTPUT_SELF_CLEAN_FRONT_UPPER_VALVE, /* OUT21-PE3：自清洁前侧上阀。 */
  AUTO_OUTPUT_SELF_CLEAN_FRONT_LOWER_VALVE, /* OUT22-PE4：自清洁前侧下阀。 */
  AUTO_OUTPUT_SELF_CLEAN_REAR_VALVE,        /* OUT23-PE5：自清洁后端阀。 */
  AUTO_OUTPUT_BYPASS_VALVE,                 /* OUT24-PE6：旁通阀。 */
  AUTO_OUTPUT_OZONE,                        /* OUT25-PC13：臭氧。 */
  AUTO_OUTPUT_LIGHTING,                     /* OUT26-PC14：舱内照明，低电平有效，默认开启。 */
  AUTO_OUTPUT_FRONT_DOOR_LIGHT,             /* OUT27-PC15：正门灯带。 */
  AUTO_OUTPUT_LOGO_LIGHT,                   /* OUT28-PF0：LOGO发光板。 */
  AUTO_OUTPUT_FRONT_DOOR_HEATER,            /* OUT29-PF1：正门加热。 */
  AUTO_OUTPUT_DOOR_CLOSE,                  /* OUT30-PF2：自动关门输出。 */
  AUTO_OUTPUT_DOOR_STOP,                   /* OUT31-PF3：关门停止输出。 */
  AUTO_OUTPUT_UPPER_LOCK,                   /* OUT32-PF4：上电磁锁。 */
  AUTO_OUTPUT_LOWER_LOCK,                   /* OUT33-PF5：下电磁锁。 */
  AUTO_OUTPUT_LEFT_LOCK,                    /* OUT34-PF6：左电插锁。 */
  AUTO_OUTPUT_RIGHT_LOCK,                   /* OUT35-PF7：右电插锁。 */
  AUTO_OUTPUT_REAR_LOCK_1,                  /* OUT36-PF8：后电插锁1。 */
  AUTO_OUTPUT_REAR_LOCK_2,                  /* OUT37-PF9：后电插锁2。 */
  AUTO_OUTPUT_LOWER_DOOR_LIGHT,             /* OUT38-PF10：下门灯板。 */
  AUTO_OUTPUT_STANDBY2_VALVE,               /* OUT39-PC0：热水器阀。 */
  AUTO_OUTPUT_MAIN_POWER,                   /* OUT40-PC1：主板电源控制。 */
  AUTO_OUTPUT_COUNT
} AutoOutputPoint;

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
