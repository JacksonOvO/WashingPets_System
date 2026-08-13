# 喜宠岛 STM32 智能洗护设备项目记忆文档

更新时间：2026-07-27  
用途：切换新 GPT 账号后，把本文档发给新 GPT，用于快速恢复项目上下文。

## 1. 项目是什么

这是一个宠物智能洗护设备项目，核心控制由 STM32F407 APP 固件完成，上位机/调试台负责参数配置、流程触发、状态显示、曲线记录和 OTA 升级。

设备包含：

- 舱门机构：上门电磁锁、下门电磁锁、上门闭门器；现在没有电动推杆。
- 水路系统：水箱、自来水阀、热水器阀、旁通/内循环、冷热水比例调节阀、增压泵、淋浴/冲洗/自清洁水路阀。
- 环境系统：舱外温湿度、舱内温湿度、氧气、臭氧、四路水温采集。
- 烘干系统：左风机、右风机、底部风机、顶部排风扇、左加热棒、右加热棒、底部加热棒。
- 自清洁系统：顶部、左侧上层、左侧夹层、后端等清洁动作。
- 调试工具：WPF 调试工作台，可连接 COM、控制 IO、控制 485、下发配方、触发流程、记录测试曲线、执行 OTA。

## 2. 主要工程路径

STM32 APP 工程：

```text
C:\Users\Administrator\Desktop\DEMO\App
```

调试工作台：

```text
C:\Users\Administrator\Desktop\DEMO\Bootloader\tools\APP_IAP\STM32智能调试工作台_WPF.exe
C:\Users\Administrator\Desktop\DEMO\Bootloader\tools\APP_IAP\WpfWorkbench
```

协议说明书：

```text
C:\Users\Administrator\Desktop\DEMO\Agreement\STM32_上位机通信协议说明书.md
```

测试曲线输出目录：

```text
C:\Users\Administrator\Desktop\测试曲线
```

项目记录文档：

```text
C:\Users\Administrator\Desktop\DEMO\比例调节阀\单片机硬件_优化版.docx
```

## 3. 当前 Git/源码状态

最近仓库 HEAD：

```text
df4db1b 配方结构和流程参数更新、烘干控制按配方目标温度动态调加热棒功率、加入底部风机/底部加热棒/顶部排风扇相关控制、增压泵/排风扇支持 485 和 GPIO 两种控制思路、A5 实时遥测增加上位机 ACK 应答处理，修正了自清洁“手动结束后又自动重跑”的事件逻辑
```

当前仍有未提交修改，涉及：

```text
Core/Inc/app_iap.h
Core/Src/app_iap.c
Core/Src/auto_stage_bath.c
Core/Src/auto_stage_clean.c
Core/Src/auto_stage_dry.c
Core/Src/auto_stage_exit_prepare.c
Core/Src/auto_stage_wash_ready.c
Core/Src/clean_process.c
Core/Src/task_comm485.c
Core/Src/task_water_tank.c
```

注意：部分 C 文件中文注释在终端里显示为乱码，这是编码显示问题，不要因为乱码随意重写大段文件。

## 4. 重要编译命令

STM32 APP 编译常用命令：

```powershell
& .\cmake\cube-cmake.cmd --build .\build\Debug
```

在之前环境中，这个命令前缀已被批准：

```text
C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe -Command & .\cmake\cube-cmake.cmd --build .\build\Debug
```

调试工作台 WPF 编译一般使用 .NET Framework csc：

```powershell
$csc='C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe'
$gac='C:\Windows\Microsoft.NET\assembly'
$dir='C:\Users\Administrator\Desktop\DEMO\Bootloader\tools\APP_IAP\WpfWorkbench'
$out='C:\Users\Administrator\Desktop\DEMO\Bootloader\tools\APP_IAP\STM32智能调试工作台_WPF.exe'
& $csc /nologo /target:winexe /optimize+ /platform:anycpu /out:$out /win32manifest:"$dir\app.manifest" /reference:System.dll /reference:System.Core.dll /reference:"$gac\GAC_MSIL\WindowsBase\v4.0_4.0.0.0__31bf3856ad364e35\WindowsBase.dll" /reference:"$gac\GAC_32\PresentationCore\v4.0_4.0.0.0__31bf3856ad364e35\PresentationCore.dll" /reference:"$gac\GAC_MSIL\PresentationFramework\v4.0_4.0.0.0__31bf3856ad364e35\PresentationFramework.dll" /reference:"$gac\GAC_MSIL\System.Xaml\v4.0_4.0.0.0__b77a5c561934e089\System.Xaml.dll" /resource:"$dir\Workbench.xaml",Workbench.xaml /resource:"$dir\BrandLogo.jpg",BrandLogo.jpg /resource:"$dir\AppIcon.jpg",AppIcon.jpg "$dir\Program.cs" "$dir\Protocol.cs"
```

## 5. OTA / IAP 记忆

当前源码关键定义在 `Core/Inc/app_iap.h`：

```c
#define APP_IAP_APP_ADDRESS          0x08004000U
#define APP_IAP_TARGET_ID            0x407A0C06U
#define APP_IAP_HARDWARE_VERSION     0x0001U
#define APP_IAP_VERSION              0x00020101U
```

版本编码规则：

```text
major << 16 | minor << 8 | patch
```

曾经上位机工程师反馈问题：

- 固件能写入并重启，但 `QUERY_INFO` 返回 `app_version = 0x02000000`。
- DMP 期望 `1.0.1 = 0x00010001`，实际版本不一致，报 `VERSION_VERIFY_FAILED`。

已做过的修复方向：

- `QUERY_INFO (0x10)` 返回 `RSP_DEVICE_INFO (0x90)` 时，在 payload 偏移 14 写入 `app_version`，小端序。
- APP 初始化完成后主动发送 `RSP_APP_READY (0x93)`。
- `RSP_APP_READY` payload 共 22 字节，包含：
  - `target_id = 0x407A0C06`
  - `hardware_version = 1`
  - `app_version`
  - 实际 `app_size`
  - 实际 `app_crc32`
- `app_size` 和 `app_crc32` 应读取 Bootloader 升级完成后保存的固件元数据，不要写死。
- Bootloader 和 APP 必须使用相同 Target ID、版本编码、小端规则。

提醒：当前源码里 `APP_IAP_VERSION` 是 `0x00020101U`，后续如果上位机仍期望 1.0.1，需要同步确认版本号，不要盲目改回。

## 6. 485 从站站号

当前 `Core/Inc/rs485_config.h` 站号：

```text
1  = 增压泵变频器
2  = 冷热水比例调节阀
10 = 左风机
11 = 右风机
12 = 底部风机
13 = 顶部排风扇
14 = 左加热棒
15 = 右加热棒
16 = 底部加热棒
20 = 舱外温湿度 / 舱内氧气
21 = 四路水温采集模块
22 = 舱内温湿度
23 = 舱内臭氧
```

新增增压泵 Modbus 示例报文：

```text
10% 调频：01 06 10 00 03 E8 8D B4
启动：    01 06 20 00 00 01 43 CA
停止：    01 06 20 00 00 06 02 08
```

配方里的水压档位现在实际理解为增压泵频率百分比。

## 7. 配方结构

当前配方数量：

```c
APP_RECIPE_COUNT = 26
```

当前 `AppRecipe_t` 大小：

```c
sizeof(AppRecipe_t) == 68U
```

当前配方字段：

```text
tank_target_temp_x10             水箱目标温度，0.1℃
tank_temp_tolerance_x10          水箱水温容差，0.1℃
pipe_temp_tolerance_x10          管路水温容差，0.1℃
spray_pressure_level             冲洗档位/增压泵频率百分比
bath_pressure_level              沐浴档位/增压泵频率百分比
clean_pressure_level             清洗档位/增压泵频率百分比
dry_fan_percent                  烘干目标风机功率，0~100%
dry_target_cabin_temp_x10        烘干目标舱内温度，0.1℃
spray_time_ms                    冲洗总时间
spray_on_time_ms                 冲洗出水时间
spray_pause_time_ms              冲洗暂停时间
bath_time_ms                     沐浴总时间
bath_on_time_ms                  沐浴出水时间
bath_pause_time_ms               沐浴暂停时间
liquid_soak_time_ms              洗剂浸润时间
clean_time_ms                    清洗总时间
dry_time_ms                      烘干时间
self_clean_top_time_ms           自清洁顶部清洁时间
self_clean_left_upper_time_ms    自清洁左侧上层清洁时间
self_clean_left_middle_time_ms   自清洁左侧夹层清洁时间
self_clean_rear_time_ms          自清洁后端清洁时间
self_clean_dry_time_ms           自清洁烘干时间
```

设计目的：针对不同品种、不同体型、不同毛发长短的狗，使用不同洗护参数。

## 8. 自动流程总体

主流程拆分为多个子流程：

```text
环境调节 -> 入舱 -> 洗前准备 -> 冲洗 -> 沐浴 -> 清洗 -> 烘干 -> 出舱准备 -> 出舱 -> 自清洁
```

重要策略：

- 上电默认自动模式。
- 上电后上下舱门电磁锁应输出上锁。
- 自动模式刚切入时，水箱先按默认参数恒温。
- 上位机发送配方后，后续流程按配方参数执行。
- 进入自动流程后，会持续上报设备状态、IO 状态、遥测数据。

## 9. 舱门机构逻辑

现在没有电动推杆，只有上门闭门器。

机构关系：

- 上门打开时会带动下门打开。
- 下门关闭时会带动上门关闭。

入舱流程和出舱流程都按类似逻辑：

1. 流程开始时向上位机发送“请求打开上舱门”。
2. 等待上位机按钮确认。
3. 打开上舱门电磁锁。
4. 等待上下电磁锁到位输入解除，确认上下门已打开。
5. 等待关门相关信号。
6. 自动关闭上门闭门器。
7. 到位后上下门电磁锁上锁。
8. 结束流程。

调试台交互按钮目前概念上只有两个核心按钮：

- 入舱开门
- 出舱开门

两者都走上位机交互 `CMD=0x33`，但请求类型/阶段要区分，避免出舱流程点开门无效。

## 10. 终止服务逻辑

协议 `CMD=0x32` 增加了终止服务按钮/动作。

用户需求：

- 如果全自动流程还在环境调整阶段，终止服务后直接结束，不跳出舱，不自清洁。
- 如果已经进入入舱流程，舱门可能已打开，终止服务后需要跳到出舱流程。
- 已进入入舱后的终止服务，后续仍要触发自清洁。
- 如果用户在烘干前点结束服务，仍然要给水箱发送自清洁补水信号，因为后面还需要自清洁。

曾修复问题：

- 自清洁“手动结束后又自动重跑”。
- 自清洁结束按钮结束不了或结束不及时。

## 11. 水箱逻辑

水箱温度关键数据：

- 水箱温度在四路水温采集模块 CH2。
- 四路水温采集模块站号 21。

用户曾遇到：

- 手动/自动切换后水箱温度显示不真实。
- 实际水箱 42℃，程序还在旁通加热。
- 手动打开增压泵后四路温度异常，最后判断是电气干扰问题，不是代码问题，之前温度采集防异常修改已要求恢复。

当前水箱策略要点：

- 自动模式下水箱恒温按默认参数。
- 配方下发后，按配方里的水箱目标温度和容差升温/恒温。
- 洗澡和自清洁补水目标统一为水箱高限位，不再用中限位作为停止条件。
- 开始烘干后会给水箱自清洁补水准备信号，目标是后续自清洁时水箱补到高限位。
- 曾修复自来水阀打开但比例阀不开的问题，补冷水时比例阀要打开到指定角度。

## 12. 烘干流程逻辑

当前烘干目标：

- 快速升温。
- 舱内恒温在配方目标温度附近。
- 不建议到温度后风机降到 0%，风机功率较大且对热风输出有明显影响。

已做策略：

- 冲洗/清洗快结束前提前预热：
  - 所有加热棒 100%。
  - 所有风机 50%。
- 进入烘干流程后：
  - 风机从 50% 逐步升到 100%。
  - 100% 吹一段时间。
  - 再降到配方目标风机功率。
  - 后续根据舱内目标温度动态调整加热棒功率。
- 烘干流程包含：
  - 左风机、右风机、底部风机、顶部排风扇。
  - 左加热棒、右加热棒、底部加热棒。

当前代码中：

```c
DRY_FAN_RAMP_TIME_MS = 10000U
```

这表示风机 10 秒内从 50% 升到 100%。

## 13. 出舱准备 CheckTempHumidity

文件：

```text
Core/Src/auto_stage_exit_prepare.c
```

当前判断不是固定目标温度，而是判断舱内与舱外温差：

```text
温差允许范围：±1℃
湿度高阈值：80.0%
```

逻辑：

```text
如果舱内湿度 > 80%：
    如果舱内温度比舱外低超过 1℃：
        升温除湿
    否则：
        通风除湿

否则湿度达标：
    如果舱内温度比舱外高超过 1℃：
        通风降温
    如果舱内温度比舱外低超过 1℃：
        升温
    否则：
        温湿度达标，关闭温湿度控制
```

## 14. 冲洗/沐浴/清洗/自清洁

已做过的关键改动：

- 沐浴流程中屏蔽蠕动泵操作。
- 自清洁中屏蔽自清洁上层动作。
- 自清洁第一步 `CheckTankHigh` 改为一直等待水箱高液位，不跳报错。
- 洗澡和自清洁补水统一到高限位。
- 沐浴流程已启用：
  - `bath_on_time_ms`
  - `bath_pause_time_ms`
- 增压泵和蠕动泵按“喷一段时间、停一段时间”循环，不再一直连续喷。

曾经排查过的问题：

- 单流程冲洗到步骤 120 报错，设备状态 7、步骤状态 5。
- 水管出水口温度约 30℃时仍报错。
- 单流程冲洗时水箱中限位会影响流程。
- 单流程冲洗时水箱会打开增压泵和旁通，原因和后台水箱调温/补水逻辑有关。

## 15. 485 通信与传感器数据

用户曾遇到自动运行时所有传感器值都没了，日志中 A5 遥测大量 `FF FF`。

已做优化：

- `Core/Src/task_comm485.c`
- 增加 `COMM485_MAX_COMMANDS_PER_LOOP = 4U`
- 相同站号相同功率不重复下发。
- 先限制执行器命令队列处理数量，再轮询传感器，避免风机/加热棒频繁调功率把传感器轮询挤掉。

注意：

- 增压泵开启导致温度采集异常时，最终判断可能是电气干扰，不要只从代码角度处理。
- 485 总线上的强干扰、电源地、屏蔽、终端电阻、模块供电都要考虑。

## 16. 调试工作台功能

调试工作台 exe 路径：

```text
C:\Users\Administrator\Desktop\DEMO\Bootloader\tools\APP_IAP\STM32智能调试工作台_WPF.exe
```

重要功能：

- 连接 APP 串口。
- 查询状态。
- 手动/自动模式切换。
- 流程开始、暂停、继续、结束、终止服务。
- 单流程选择。
- 配方读取/保存/选择。
- IO 实时状态显示。
- GPIO 输出控制。
- 485 手动控制。
- 风机/加热棒实时功率显示。
- 比例阀角度显示。
- 增压泵频率显示/控制。
- 测试界面单流程曲线记录。
- OTA 升级。

最近新增：

- 测试界面加了“流程监控”，默认打开。
- 调试台触发自动流程开始后，自动记录：
  - 舱外温湿度
  - 舱内温湿度
  - 氧气
  - 臭氧
  - 风机功率
  - 加热棒功率
- 自动流程结束/故障/中止后自动保存，不需要再点结束按钮。
- 保存到桌面 `测试曲线` 文件夹。
- 文件名区分：
  - `流程监控_完成_时间.csv/png`
  - `流程监控_故障_时间.csv/png`
  - `流程监控_中止_时间.csv/png`

## 17. 压力传感器软件

之前基于压力传感器 Modbus-RTU 说明书做过 Windows 可打开的软件。

用户需求：

- COM 口可选。
- 实时显示重量。
- 显示曲线。
- 说明书里的参数都可配置。
- 清零、去皮。
- 三段线性补偿。
- 最终显示补偿后的重量。

已解释过：

- 补偿：用用户填写的三组不同重量区间实际值，做分段线性校正。
- 滤波：对波动值做平滑，让 39.5、39.6、39.7 这类小抖动显示更稳定。
- 最后误差目标约 0.3 kg。

## 18. 重要的历史改动摘要

7/20：

- 上电默认自动模式。
- 上电后上下舱门电磁锁上锁。
- 新增终止服务。
- 烘干/终止服务后给水箱发自清洁补水准备信号。
- 自清洁等待高液位，不再报错。
- 屏蔽蠕动泵。
- 修复自清洁结束不及时。

7/21：

- 入舱、出舱流程改为新舱门机构逻辑。
- 调整自动模式、上电模式、上下门锁输出。
- 修正水箱恒温、补水、自清洁前补水、旁通、热水器阀、比例阀逻辑。
- 修复入舱准备增压泵不输出、照明异常、单流程异常、清输出覆盖等问题。

7/22：

- 统一 485 从站编号。
- 新增 485 增压泵控制。
- 新增顶部排风扇 485 控制。
- 增加 GPIO/485 执行器切换。
- 调试工作台更新功率显示、比例阀角度、增压泵控制。
- 协议说明书增加 0x37、0x38，A5 遥测扩展，配方结构更新。

7/23：

- 配方结构和流程参数更新。
- 烘干按配方目标温度动态调加热棒功率。
- 加入底部风机、底部加热棒、顶部排风扇。
- A5 实时遥测增加上位机 ACK。
- 配方数量改为 26。

7/24：

- 修正自清洁手动结束后自动重跑。
- 烘干流程优化：提前预热、风机爬坡、目标功率恒温。
- OTA 版本上报修复。
- 调试工作台适配新协议和 OTA 版本解析。

7/25：

- 洗澡和自清洁补水目标统一到高限位。
- 沐浴流程启用出水/暂停时间。
- 485 通信防堵塞优化。
- 调试台新增流程监控，自动保存完整曲线数据。

## 19. 常见问题和判断

如果传感器突然全是 `FF`：

- 先看 485 是否被执行器命令挤占。
- 再看是否有增压泵/加热棒/风机干扰。
- 不要只改代码，硬件干扰概率很高。

如果右风机 11 控制不了：

- 确认实际站号是不是 11。
- 确认调试台没有旧站号 1~6 的清零逻辑。
- 确认 IO 控制页面和 485 控制页面的控制路径是否一致。

如果自清洁结束后又重跑：

- 查自动流程事件标志是否未清。
- 查手动结束是否被主流程下一拍重新判定为需要自清洁。

如果出舱流程中“开上舱门”按钮无效：

- 入舱开门和出舱开门应是两个交互请求。
- 调试台按钮发送的 request/stage/type 不能混用。

如果水箱明明达到温度还加热：

- 确认 CH2 水箱温度采集是否真实。
- 确认当前是否已收到配方。
- 自动模式未进入流程前可能使用默认参数。
- 也要排查温度采集模块是否被增压泵等干扰。

## 20. 给新 GPT 的建议首条消息

可以把下面这段和本文档一起发给新 GPT：

```text
这是我的 STM32 智能洗护设备项目记忆文档。请先完整阅读它，再帮我继续改代码。工程路径是 C:\Users\Administrator\Desktop\DEMO\App。调试台在 C:\Users\Administrator\Desktop\DEMO\Bootloader\tools\APP_IAP。请注意不要随意还原我现有修改，先看 git status 和相关源码，再按我的新需求小范围修改并编译验证。
```

