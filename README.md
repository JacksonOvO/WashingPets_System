# WashingPets System

本仓库包含洗宠机 STM32 APP、Bootloader、上位机通信协议文档和 Linux 工作台工具。

## 本次更新内容

以下内容为当前工作区相对 Git 版本的主要变更，提交前可按本节核对。

### STM32 APP 固件

- 固件切换为真实硬件编译模式：`APP_DRY_RUN_ENABLE=0`。
- 上电默认工作模式改为 `MANUAL` 手动模式；完整自动流程需要上位机先切 `AUTO`，再发送流程启动命令。
- `OUT5` 保持 `PD8 排风紫外线灯`；`OUT29-PF1` 改为增压泵 GPIO 启停，不再使用正门加热功能。
- `OUT26-PC14 舱内照明` 改为低电平有效、上电默认开启，工作台可手动控制；急停/看门狗紧急关断时保持开启。
- 洗前准备流程增加“请求上位机选择本次运行配方”，配方回复数据增加沐浴液选择字段。
- 喷泡沫阶段按沐浴液选择控制 `OUT13~OUT16`，不再写死只开消毒液阀。
- 自清洁流程重做：清洁液喷洒、静置、分路冲洗、消毒液喷洒、冲洗、紫外线消杀与烘干并行。
- 新增快速/深度自检任务 `task_self_test.c/.h`，支持风机、加热、增压泵、阀门、比例阀、热水链路等项目检测，并通过 `0xA7 SELF_TEST_RESULT` 上报结果位图。
- 配方和参数结构升级，新增自检参数组并保留历史参数迁移。
- `0xA4 FAULT_EVENT` 删除 `fault_bitmap`，仅保留 `primary_fault_code`。
- `0xA5 TELEMETRY` 增加 485 执行器状态、流程剩余时间等数据；增压泵改为GPIO后，原压力反馈字段保留为0。
- `0xA6 IO_STATUS` 改为输入/输出位图上报，支持 IO 变化主动上报。
- 增压泵从 485 压力控制改为 `OUT29` GPIO 启停，固件不再轮询站号1、不再上报 `0x0808` 增压泵485通讯失败。
- 比例阀通信健康判断改为按角度写入成功/失败判定，不再使用后台 `0x03` 读反馈寄存器，避免可控制但误报 `0x0809`。
- 21 号四路水温采集模块恢复真实读取；CH3 管路出水口温度因未接入而屏蔽，使用固定管路温度并按有效数据上报。
- 顶部排风扇 13 号从站当前通过 `RS485_TOP_EXHAUST_PRESENT=0` 屏蔽巡检。

### Bootloader / IAP

- APP 与 Bootloader 的设备 ID 不再使用固定值，统一改为运行时读取 STM32 96 位 UID 并用 CRC32 压缩成 32 位 target ID。
- Bootloader 写 metadata、校验 App、上报设备信息、处理升级开始命令时使用同一套 UID-CRC32 target ID。
- IAP 协议文档同步 target ID 规则和升级交互说明。

### Linux 工作台

- 工作台增加/修正 A4 故障中心详细显示，按 `primary_fault_code` 展示具体故障。
- 修复故障复位后故障显示残留问题。
- 修复 IO 输入/输出状态显示和 A6 位图解析。
- IO 页面同步输出点名称：`OUT5 排风紫外线灯`、`OUT26 舱内照明`。
- `OUT26 舱内照明` 开关恢复可控。
- 修正配方参数读取、编辑焦点切换时参数回退等问题。
- 修正增压泵压力设置单位，工作台输入“公斤”后按 0.1kg 单位下发。
- 升级工具日志滚动和升级流程做过修正，并重新打包 Linux `.deb`。

### 文档

- 同步更新 `WashingPets_App/STM32_上位机通信协议说明书.md`。
- 新增/更新自动流程、自检逻辑、开发文档等说明文件。

## 构建和生成物

- 当前 APP 版本化固件：
  `WashingPets_App/build/Debug/WashingPets_APP_V2.3.3.bin`
- 工作台升级用固件已同步到：
  `WashingPets_Bootloader/tools/APP_IAP/APP.bin`
- Linux 工作台安装包：
  `WashingPets_Bootloader/tools/APP_IAP/dist/pawclean-workbench_1.1.0_amd64.deb`

构建时 APP C 编译和链接通过；当前命令行环境最后一步 `cube-cmake` 不在 PATH，需要用系统 `cmake -P WashingPets_App/cmake/copy_versioned_app_bin.cmake` 补生成版本化 bin。

## 提交前注意

建议提交源码、协议文档、工作台源码、必要的固件/安装包生成物。

不建议提交以下临时文件：

- `WashingPets_Bootloader/tools/.qoder/`
- `WashingPets_Bootloader/tools/APP_IAP/linux_workbench/__pycache__/`
- `WashingPets_Bootloader/tools/APP_IAP/linux_workbench/gtk_app.py.bak_20260819`
- 临时或旧版固件，例如确认不用的 `APP_DRYRUN_V2.4.0.bin`
