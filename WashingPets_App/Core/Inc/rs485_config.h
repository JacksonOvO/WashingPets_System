#ifndef RS485_CONFIG_H
#define RS485_CONFIG_H

/* RS485 Modbus slave addresses. Change field addresses here only. */
#define RS485_STATION_BOOSTER_PUMP      1U  /* 增压泵变频器 */
#define RS485_STATION_PROPORTIONAL_VALVE 2U /* 冷热水比例调节阀 */
#define RS485_STATION_LEFT_FAN          10U /* 左风机 */
#define RS485_STATION_RIGHT_FAN         11U /* 右风机 */
#define RS485_STATION_BOTTOM_FAN        12U /* 底部风机 */
#define RS485_STATION_TOP_EXHAUST_FAN   13U /* 顶部排风扇 */
#define RS485_STATION_LEFT_HEATER       14U /* 左加热棒 */
#define RS485_STATION_RIGHT_HEATER      15U /* 右加热棒 */
#define RS485_STATION_BOTTOM_HEATER     16U /* 底部加热棒 */
#define RS485_STATION_OXYGEN_SENSOR     20U /* 舱外温湿度 / 舱内氧气 */
#define RS485_STATION_TEMPERATURE       21U /* 四路水温采集模块 */
#define RS485_STATION_INCABTRH_SENSOR   22U /* 舱内温湿度 */
#define RS485_STATION_OZONE_SENSOR      23U /* 舱内臭氧 */

#define RS485_STATION_DEFAULT_THYRISTOR RS485_STATION_LEFT_FAN

#endif
