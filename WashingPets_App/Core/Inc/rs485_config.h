#ifndef RS485_CONFIG_H
#define RS485_CONFIG_H

/* RS485 Modbus slave addresses. Change field addresses here only. */
#define RS485_STATION_BOOSTER_PUMP      1U  /* 保留旧485增压泵站号；当前增压泵改由OUT29 GPIO控制。 */
#define RS485_STATION_PROPORTIONAL_VALVE 2U /* 冷热水比例调节阀 */
#define RS485_STATION_LEFT_FAN          10U /* 左风机 */
#define RS485_STATION_RIGHT_FAN         11U /* 右风机 */
#define RS485_STATION_BOTTOM_FAN        12U /* 底部风机 */
#define RS485_STATION_TOP_EXHAUST_FAN   13U /* 顶部排风扇 */
#define RS485_TOP_EXHAUST_PRESENT       1U  /* 顶部排风扇在位开关：0=未接（屏蔽巡检与上电清零），1=已接 */
#define RS485_STATION_LEFT_HEATER       14U /* 左加热棒 */
#define RS485_STATION_RIGHT_HEATER      15U /* 右加热棒 */
#define RS485_STATION_BOTTOM_HEATER     16U /* 底部加热棒 */
#define RS485_STATION_OXYGEN_SENSOR     20U /* 舱外温湿度 / 舱内氧气 */
#define RS485_STATION_TEMPERATURE       21U /* 四路水温采集模块 */
#define RS485_TEMPERATURE_MODULE_PRESENT 1U  /* 0=暂不接入21号四路采集模块，使用下方固定温度；1=真实读取 */
/* Bit0~Bit3对应四路水温CH1~CH4；当前CH3管路出水口未接，屏蔽并使用固定管路温度。 */
#define RS485_TEMPERATURE_CHANNEL_ENABLE_MASK 0x0BU
#define RS485_FIXED_INLET_TEMP_X10       300 /* 进水口固定温度，单位0.1℃ */
#define RS485_FIXED_TANK_TEMP_X10        360 /* 水箱固定温度，单位0.1℃ */
#define RS485_FIXED_PIPE_TEMP_X10        360 /* 管路固定温度，单位0.1℃ */
#define RS485_FIXED_SPARE_TEMP_X10       360 /* 备用/混合水固定温度，单位0.1℃ */
#define RS485_STATION_INCABTRH_SENSOR   22U /* 舱内温湿度 */
#define RS485_STATION_OZONE_SENSOR      23U /* 舱内臭氧 */

#define RS485_STATION_DEFAULT_THYRISTOR RS485_STATION_LEFT_FAN

#endif
