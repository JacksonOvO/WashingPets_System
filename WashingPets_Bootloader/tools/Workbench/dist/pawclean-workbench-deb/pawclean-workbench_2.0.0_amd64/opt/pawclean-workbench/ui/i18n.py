#!/usr/bin/env python3
"""Internationalization — Chinese/English language switching."""

_current_lang = "en"


def get_lang():
    return _current_lang


def set_lang(lang):
    global _current_lang
    _current_lang = lang if lang in ("en", "zh") else "en"


def tr(key, default=""):
    """Translate a key to the current language."""
    entry = _STRINGS.get(key)
    if entry is None:
        return default or key
    return entry.get(_current_lang, entry.get("en", default or key))


# ─── All translatable strings ────────────────────────────────────────────────

_STRINGS = {
    # ── Sidebar / Brand ───────────────────────────────────────────────────
    "brand.title":           {"en": "PawClean",          "zh": "喜宠岛"},
    "brand.subtitle":        {"en": "Pet Washing Workbench", "zh": "洗宠机工作台"},
    "nav.section.monitor":   {"en": "MONITORING",        "zh": "监控"},
    "nav.section.control":   {"en": "CONTROL",           "zh": "控制"},
    "nav.section.engineer":  {"en": "ENGINEERING",       "zh": "工程"},
    "nav.dashboard":         {"en": "Workbench",         "zh": "工作台"},
    "nav.telemetry":         {"en": "Telemetry",         "zh": "实时数据"},
    "nav.faults":            {"en": "Fault Center",      "zh": "故障中心"},
    "nav.control":           {"en": "Workflow",          "zh": "工作控制"},
    "nav.io":                {"en": "IO Control",        "zh": "IO 控制"},
    "nav.params":            {"en": "Parameters",        "zh": "参数配置"},
    "nav.test":              {"en": "Test",              "zh": "测试"},
    "nav.debug":             {"en": "Debug",             "zh": "调试工具"},
    "nav.upgrade":           {"en": "Firmware",          "zh": "固件升级"},

    # ── Header ────────────────────────────────────────────────────────────
    "header.disconnected":   {"en": "Disconnected",      "zh": "未连接"},
    "header.connected":      {"en": "Connected",         "zh": "已连接"},
    "header.refresh":        {"en": "Refresh",           "zh": "刷新"},
    "header.connect":        {"en": "Connect",           "zh": "连接"},
    "header.disconnect":     {"en": "Disconnect",        "zh": "断开"},
    "header.poll":           {"en": "Compat Poll",       "zh": "兼容轮询"},
    "header.lang":           {"en": "中文",              "zh": "EN"},

    # ── Status Bar ────────────────────────────────────────────────────────
    "statusbar.no_faults":   {"en": "No Faults",         "zh": "无故障"},
    "statusbar.offline":     {"en": "Offline",           "zh": "离线"},
    "statusbar.active_fault":{"en": "1 Active Fault",    "zh": "1 个活动故障"},
    "statusbar.fault_count": {"en": "{active} Active / {total} Total", "zh": "{active} 活动 / {total} 总计"},
    "statusbar.recipe":      {"en": "Recipe {n}",        "zh": "配方{n}"},
    "statusbar.flow_time":   {"en": "Total {time}",      "zh": "总剩余 {time}"},
    "statusbar.stage_time":  {"en": "Stage {time}",      "zh": "流程 {time}"},
    "statusbar.step":        {"en": "Step {n}",          "zh": "步骤 {n}"},

    # ── Mode / State names ────────────────────────────────────────────────
    "mode.idle":             {"en": "Idle",              "zh": "空闲"},
    "mode.manual":           {"en": "Manual",            "zh": "手动"},
    "mode.auto":             {"en": "Auto",              "zh": "自动"},
    "mode.test":             {"en": "Test",              "zh": "测试"},
    "mode.fault":            {"en": "Fault",             "zh": "故障"},
    "state.idle":            {"en": "Idle",              "zh": "空闲"},
    "state.start":           {"en": "Starting",          "zh": "启动中"},
    "state.running":         {"en": "Running",           "zh": "运行中"},
    "state.waiting":         {"en": "Waiting",           "zh": "等待中"},
    "state.paused":          {"en": "Paused",            "zh": "已暂停"},
    "state.stopping":        {"en": "Stopping",          "zh": "停止中"},
    "state.done":            {"en": "Complete",          "zh": "已完成"},
    "state.fault":           {"en": "Fault",             "zh": "故障"},

    # ── Process Type names ────────────────────────────────────────────────
    "process.none":          {"en": "No Process",         "zh": "无流程"},
    "process.auto":          {"en": "Auto Process",       "zh": "自动流程"},
    "process.single_test":   {"en": "Single Test",        "zh": "单流程测试"},
    "process.self_clean":    {"en": "Self Clean",         "zh": "自清洗流程"},
    "process.manual_op":     {"en": "Manual",             "zh": "手动操作"},

    # ── Dashboard ─────────────────────────────────────────────────────────
    "dash.offline":          {"en": "Device Offline",    "zh": "设备离线"},
    "dash.offline_hint":     {"en": "Connect the device to start", "zh": "连接设备以开始"},
    "dash.total_remaining":  {"en": "TOTAL REMAINING",   "zh": "总剩余时间"},
    "dash.stage_remaining":  {"en": "STAGE REMAINING",   "zh": "流程剩余时间"},
    "dash.current_step":     {"en": "CURRENT STEP",      "zh": "当前步骤号"},
    "dash.active_faults":    {"en": "ACTIVE FAULTS",     "zh": "活动故障"},
    "dash.total_faults":     {"en": "TOTAL EVENTS",      "zh": "故障总计"},
    "dash.sensor_data":      {"en": "Sensor Data",       "zh": "传感器数据"},
    "dash.actuator_status":  {"en": "Actuator Status",   "zh": "执行器状态"},
    "dash.interaction_req":  {"en": "Interaction Request","zh": "交互请求"},
    "dash.recent_faults":    {"en": "Recent Faults",     "zh": "最近故障"},
    "dash.no_recent_faults": {"en": "No recent faults",  "zh": "无最近故障"},
    "dash.recipe":           {"en": "Recipe",            "zh": "配方"},

    # ── Sensor names ──────────────────────────────────────────────────────
    "sensor.cabin_ext_temp":  {"en": "Cabin Out",        "zh": "舱外温度"},
    "sensor.cabin_ext_humi":  {"en": "Out Humidity",     "zh": "舱外湿度"},
    "sensor.cabin_oxygen":    {"en": "Oxygen",           "zh": "氧气"},
    "sensor.inlet_water_temp":{"en": "Inlet Water",      "zh": "进水温度"},
    "sensor.tank_water_temp": {"en": "Tank Water",       "zh": "水箱温度"},
    "sensor.outlet_water_temp":{"en": "Outlet Water",    "zh": "出水温度"},
    "sensor.reserve_temp":    {"en": "Mixed Water",      "zh": "混水温度"},
    "sensor.cabin_temp":      {"en": "Cabin Temp",       "zh": "舱内温度"},
    "sensor.cabin_humidity":  {"en": "Cabin Humidity",   "zh": "舱内湿度"},
    "sensor.cabin_ozone":     {"en": "Ozone",            "zh": "臭氧"},

    # ── Actuator names ────────────────────────────────────────────────────
    "act.left_fan":           {"en": "Left Fan",         "zh": "左风机"},
    "act.right_fan":          {"en": "Right Fan",        "zh": "右风机"},
    "act.bottom_fan":         {"en": "Bottom Fan",       "zh": "底部风机"},
    "act.left_heater":        {"en": "Left Heater",      "zh": "左加热"},
    "act.right_heater":       {"en": "Right Heater",     "zh": "右加热"},
    "act.bottom_heater":      {"en": "Bottom Heater",    "zh": "底部加热"},
    "act.booster_pump":       {"en": "Booster Pump",     "zh": "增压泵"},
    "act.prop_valve":         {"en": "Prop Valve",       "zh": "比例阀"},

    # ── Control Page ──────────────────────────────────────────────────────
    "ctrl.mode_recipe":       {"en": "Mode & Recipe",    "zh": "模式与配方"},
    "ctrl.work_mode":         {"en": "Work Mode",        "zh": "工作模式"},
    "ctrl.manual":            {"en": "Manual",           "zh": "手动"},
    "ctrl.auto":              {"en": "Auto",             "zh": "自动"},
    "ctrl.query":             {"en": "Query",            "zh": "查询"},
    "ctrl.recipe":            {"en": "Recipe",           "zh": "配方"},
    "ctrl.recipe_n":          {"en": "Recipe {n}",       "zh": "配方 {n}"},
    "ctrl.select":            {"en": "Select",           "zh": "选择"},
    "ctrl.process_actions":   {"en": "Process Actions",  "zh": "流程控制"},
    "ctrl.flow_actions":      {"en": "Flow Actions",     "zh": "流程操作"},
    "ctrl.start":             {"en": "Start",            "zh": "开始"},
    "ctrl.pause":             {"en": "Pause",            "zh": "暂停"},
    "ctrl.continue":          {"en": "Continue",         "zh": "继续"},
    "ctrl.end":               {"en": "End",              "zh": "结束"},
    "ctrl.stage_maint":       {"en": "Stage & Maintenance","zh": "段测与维护"},
    "ctrl.stage_test":        {"en": "Stage Test",       "zh": "段测"},
    "ctrl.self_clean":        {"en": "Self Clean",       "zh": "自清洁"},
    "ctrl.fault_reset":       {"en": "Fault Reset",      "zh": "故障复位"},
    "ctrl.door_control":      {"en": "Door Control",     "zh": "舱门控制"},
    "ctrl.open_top":          {"en": "Open Top",         "zh": "开上舱门"},
    "ctrl.open_bottom":       {"en": "Open Bottom",      "zh": "开下舱门"},
    "ctrl.close_bottom":      {"en": "Close Bottom",     "zh": "关下舱门"},
    "ctrl.close_top":         {"en": "Close Top",        "zh": "关上舱门"},
    "ctrl.auto_close":        {"en": "Auto Close",       "zh": "自动关门"},
    "ctrl.request":           {"en": "Request",          "zh": "请求"},
    "ctrl.info":              {"en": "Status Info",      "zh": "状态信息"},
    "ctrl.current_mode":      {"en": "Current Mode",     "zh": "当前模式"},
    "ctrl.current_recipe":    {"en": "Current Recipe",   "zh": "当前配方"},
    "ctrl.current_stage":     {"en": "Current Stage",    "zh": "当前段"},
    "ctrl.current_step":      {"en": "Current Step",     "zh": "当前步骤号"},
    "ctrl.run_state":         {"en": "Run State",        "zh": "运行状态"},

    # ── Telemetry Page ────────────────────────────────────────────────────
    "tel.sensor_data":        {"en": "Sensor Data",      "zh": "传感器数据"},
    "tel.status_flags":       {"en": "Status Flags",     "zh": "状态标志"},
    "tel.process_time":       {"en": "Process Time",     "zh": "流程时间"},
    "tel.total":              {"en": "Total",            "zh": "总计"},
    "tel.stage":              {"en": "Stage",            "zh": "当前段"},
    "tel.realtime_curves":    {"en": "Real-time Curves", "zh": "实时曲线"},
    "tel.curves_hint":        {"en": "Sensor curves · 30s/point", "zh": "传感器曲线 · 30秒/点"},

    # ── Test Page ─────────────────────────────────────────────────────────
    "test.card_record":       {"en": "Single Flow Record", "zh": "单流程记录"},
    "test.conditions":        {"en": "Test Conditions",  "zh": "测试条件"},
    "test.interval":          {"en": "Sample Interval (s/point)", "zh": "采样间隔(秒/点)"},
    "test.stage":             {"en": "Flow",             "zh": "流程"},
    "test.cond_hint":         {"en": "Runs the selected single flow and records sensor data",
                               "zh": "开始后执行所选单流程，并记录传感器数据"},
    "test.series":            {"en": "Record Items",     "zh": "记录项目"},
    "test.series.temp":       {"en": "Temperature",      "zh": "温度"},
    "test.series.humidity":   {"en": "Humidity",         "zh": "湿度"},
    "test.series.oxygen":     {"en": "Oxygen",           "zh": "氧气"},
    "test.series.ozone":      {"en": "Ozone",            "zh": "臭氧"},
    "test.series.fan":        {"en": "Fan Power",        "zh": "风机功率"},
    "test.series.heater":     {"en": "Heater Power",     "zh": "加热棒功率"},
    "test.exec":              {"en": "Execute",          "zh": "执行"},
    "test.start":             {"en": "Start Test",       "zh": "开始测试"},
    "test.stop_save":         {"en": "Stop & Save Curve", "zh": "结束并保存曲线"},
    "test.status_idle":       {"en": "Not Started",      "zh": "未开始"},
    "test.status_running":    {"en": "Testing",          "zh": "测试中"},
    "test.status":            {"en": "{state}, {n} points recorded",
                               "zh": "{state}，已记录 {n} 个点"},
    "test.saved":             {"en": "Saved to {dir}: {csv}, {png}",
                               "zh": "已保存到 {dir}：{csv}、{png}"},
    "test.device":            {"en": "Device: no status received",
                               "zh": "设备：未收到状态"},
    "test.device_prefix":     {"en": "Device: ",         "zh": "设备："},
    "test.cmd_sent":          {"en": "Command sent, waiting for device to enter stage test…",
                               "zh": "指令已发送，等待设备进入单阶段测试…"},
    "test.device_entered":    {"en": "Device entered stage test, recording curves",
                               "zh": "设备已进入单阶段测试，开始记录曲线"},
    "test.no_response":       {"en": "Command sent but device did not enter stage test. Check device is idle and in manual mode, see log for NACK",
                               "zh": "指令已发送但设备未进入单阶段测试，请确认设备空闲且为手动模式，通信日志可查看 NACK 原因"},
    "test.nack":              {"en": "Device rejected the command: {text}",
                               "zh": "设备拒绝了指令：{text}"},
    "test.card_curve":        {"en": "Test Curves",      "zh": "测试曲线"},
    "test.curve_summary":     {"en": "{series} curve · {interval}s/point · single flow {stage}",
                               "zh": "{series}曲线 · {interval}秒/点 · 单流程 {stage}"},
    "test.curve_empty":       {"en": "Start a test and the telemetry curves will show here",
                               "zh": "点击开始测试后，对应遥测曲线会显示在这里"},
    "test.card_selftest":     {"en": "Quick / Deep Self-test", "zh": "快速/深度自检"},
    "test.selftest_quick":    {"en": "Quick Self-test",  "zh": "快速自检"},
    "test.selftest_deep":     {"en": "Deep Self-test",   "zh": "深度自检"},
    "test.selftest_stop":     {"en": "Safely Stop Self-test", "zh": "安全停止自检"},
    "test.selftest_hint":     {"en": "Self-test requires auto mode idle, no e-stop, door closed and light curtains clear; the workbench switches to auto mode first",
                               "zh": "自检需在自动模式空闲、无急停、门关闭、双光幕未触发时启动，工作台会自动先切自动模式"},
    "test.selftest_no_result":{"en": "No self-test result received", "zh": "未收到自检结果"},
    "test.selftest_passed":   {"en": "Passed",           "zh": "通过"},
    "test.selftest_failed":   {"en": "Failed",           "zh": "失败"},

    # ── Faults Page ───────────────────────────────────────────────────────
    "fault.no_fault":         {"en": "No Fault",         "zh": "无故障"},
    "fault.normal":           {"en": "Normal",           "zh": "正常"},
    "fault.reset":            {"en": "Fault Reset",      "zh": "故障复位"},
    "fault.history":          {"en": "Fault History",    "zh": "故障历史"},
    "fault.no_history":       {"en": "No fault history", "zh": "无故障历史"},
    "fault.unknown":          {"en": "Unknown",          "zh": "未知"},
    "fault.sev_info":         {"en": "Info",             "zh": "提示"},
    "fault.sev_warning":      {"en": "Warning",          "zh": "警告"},
    "fault.sev_shutdown":     {"en": "Shutdown",         "zh": "停机"},
    "fault.sev_estop":        {"en": "E-Stop",           "zh": "急停"},
    "fault.severity":         {"en": "Severity",         "zh": "严重等级"},
    "fault.event_id":         {"en": "Event ID",         "zh": "事件ID"},
    "fault.total_events":     {"en": "Total Events",     "zh": "事件总计"},
    "fault.active_label":     {"en": "Active",           "zh": "活动"},
    "fault.resolved_label":   {"en": "Resolved",         "zh": "已解除"},

    # ── Params Page ───────────────────────────────────────────────────────
    "param.recipe_config":    {"en": "Recipe Configuration", "zh": "配方配置"},
    "param.read":             {"en": "Read",             "zh": "读取"},
    "param.write_ram":        {"en": "Write RAM",        "zh": "写入RAM"},
    "param.save_flash":       {"en": "Save Flash",       "zh": "保存Flash"},
    "param.restore_default":  {"en": "Restore Default",  "zh": "恢复默认"},
    "param.version":          {"en": "Version",          "zh": "版本"},
    "param.parameters":       {"en": "Parameters",       "zh": "参数"},

    # ── IO Control Page ───────────────────────────────────────────────────
    "io.rs485_manual":        {"en": "RS485 Manual Control", "zh": "RS485 手动控制"},
    "io.station":             {"en": "Station",          "zh": "站号"},
    "io.value":               {"en": "Value",            "zh": "数值"},
    "io.mode":                {"en": "Mode",             "zh": "模式"},
    "io.gpio_actuator":       {"en": "GPIO Actuator",    "zh": "GPIO 执行器"},
    "io.pump":                {"en": "Pump",             "zh": "泵"},
    "io.start_pump":          {"en": "Start Pump",       "zh": "启动泵"},
    "io.stop_pump":           {"en": "Stop Pump",        "zh": "停止泵"},
    "io.set_rs485":           {"en": "Set RS485",        "zh": "设置RS485"},
    "io.rs485_hint":          {"en": "Pump: enter kg (0.0~10.0)  |  Fan/Heater: 0~100%  |  Valve: 0~90°",
                                "zh": "泵: 输入公斤数(0.0~10.0)  |  风机/加热: 0~100%  |  阀: 0~90°"},
    "io.rs485_invalid":       {"en": "Invalid value for the selected RS485 station",
                                "zh": "当前RS485站号的输入值无效"},
    "io.gpio_outputs":        {"en": "GPIO Outputs",     "zh": "GPIO 输出"},
    "io.toggle_hint":         {"en": "Toggle to send manual output command", "zh": "切换以发送手动输出命令"},
    "io.no_status":           {"en": "No IO status",     "zh": "无IO状态"},
    "io.gpio_inputs":         {"en": "GPIO Inputs",      "zh": "GPIO 输入"},
    "io.on":                  {"en": "ON",               "zh": "ON"},
    "io.off":                 {"en": "OFF",              "zh": "OFF"},
    "io.in":                  {"en": "in",               "zh": "入"},
    "io.out":                 {"en": "out",              "zh": "出"},

    # ── Debug Page ────────────────────────────────────────────────────────
    "debug.comm_log":         {"en": "Communication Log (TX/RX)", "zh": "通信日志 (TX/RX)"},
    "debug.clear":            {"en": "Clear",            "zh": "清除"},
    "debug.entries":          {"en": "{n} entries",      "zh": "{n} 条"},
    "debug.self_test":        {"en": "Self Test",        "zh": "自检"},
    "debug.self_test_hint":   {"en": "Requires Auto mode and idle state", "zh": "需要自动模式且空闲状态"},
    "debug.quick_test":       {"en": "Quick Test",       "zh": "快速自检"},
    "debug.deep_test":        {"en": "Deep Test",        "zh": "深度自检"},
    "debug.safe_stop":        {"en": "Safe Stop",        "zh": "安全停止"},
    "debug.no_result":        {"en": "No self-test result", "zh": "无自检结果"},
    "debug.deep":             {"en": "Deep",             "zh": "深度"},
    "debug.quick":            {"en": "Quick",            "zh": "快速"},
    "debug.aborted":          {"en": "Aborted",          "zh": "中止"},
    "debug.done":             {"en": "Done",             "zh": "完成"},
    "debug.test_colon":       {"en": "test: ",           "zh": "自检: "},

    # ── Upgrade Page ──────────────────────────────────────────────────────
    "fw.title":               {"en": "Firmware Upgrade", "zh": "固件升级"},
    "fw.version":             {"en": "Version",          "zh": "版本"},
    "fw.firmware":            {"en": "Firmware",         "zh": "固件"},
    "fw.browse":              {"en": "Browse",           "zh": "浏览"},
    "fw.query_device":        {"en": "Query Device",     "zh": "查询设备"},
    "fw.start_upgrade":       {"en": "Start Upgrade",    "zh": "开始升级"},
    "fw.ready":               {"en": "Ready",            "zh": "就绪"},
    "fw.hardware":            {"en": "Hardware",         "zh": "硬件版本"},
    "fw.bootloader":          {"en": "Bootloader",       "zh": "引导版本"},
    "fw.app_version":         {"en": "APP Version",      "zh": "APP版本"},
    "fw.target_id":           {"en": "Target ID",        "zh": "目标ID"},
    "fw.app_start":           {"en": "APP Start",        "zh": "APP起始"},
    "fw.capacity":            {"en": "Capacity",         "zh": "容量"},
    "fw.max_chunk":           {"en": "Max Chunk",        "zh": "最大块"},
    "fw.run_mode":            {"en": "Run Mode",         "zh": "运行模式"},
    "fw.upgrade_log":         {"en": "Upgrade Log",      "zh": "升级日志"},
    "fw.upgrading":           {"en": "Upgrading...",     "zh": "升级中..."},
    "fw.select_firmware":     {"en": "Select Firmware",  "zh": "选择固件"},
    "fw.cancel":              {"en": "Cancel",           "zh": "取消"},
    "fw.open":                {"en": "Open",             "zh": "打开"},
    "fw.binary_files":        {"en": "Binary files",     "zh": "二进制文件"},
    "fw.no_port":             {"en": "No port selected", "zh": "未选择端口"},
    "fw.query_failed":        {"en": "Query failed",     "zh": "查询失败"},
    "fw.device_queried":      {"en": "Device queried",   "zh": "设备查询完成"},
    "fw.upgrade_complete":    {"en": "Upgrade complete", "zh": "升级完成"},
    "fw.failed":              {"en": "Failed",           "zh": "失败"},

    # ── Common / Errors ───────────────────────────────────────────────────
    "err.no_serial":          {"en": "No serial port available", "zh": "无可用串口"},
    "err.stage_test_mode":    {"en": "Stage test requires manual mode. Switch to manual first.",
                                "zh": "段测需要手动模式，请先切换到手动模式。"},
}


# ─── Lookup helpers for dynamic strings ──────────────────────────────────────

MODE_NAMES_KEYS = {
    0: "mode.idle",
    1: "mode.manual",
    2: "mode.auto",
    3: "mode.test",
    4: "mode.fault",
}

STATE_NAMES_KEYS = {
    0: "state.idle",
    1: "state.start",
    2: "state.running",
    3: "state.waiting",
    4: "state.paused",
    5: "state.stopping",
    6: "state.done",
    7: "state.fault",
}

PROCESS_TYPE_KEYS = {
    0: "process.none",
    1: "process.auto",
    2: "process.single_test",
    3: "process.self_clean",
    4: "process.manual_op",
}

SENSOR_KEYS = {
    "cabin_ext_temp":   "sensor.cabin_ext_temp",
    "cabin_ext_humi":   "sensor.cabin_ext_humi",
    "cabin_oxygen":     "sensor.cabin_oxygen",
    "inlet_water_temp": "sensor.inlet_water_temp",
    "tank_water_temp":  "sensor.tank_water_temp",
    "outlet_water_temp":"sensor.outlet_water_temp",
    "reserve_temp":     "sensor.reserve_temp",
    "cabin_temp":       "sensor.cabin_temp",
    "cabin_humidity":   "sensor.cabin_humidity",
    "cabin_ozone":      "sensor.cabin_ozone",
}

ACTUATOR_KEYS = {
    "left_fan_pct":     "act.left_fan",
    "right_fan_pct":    "act.right_fan",
    "bottom_fan_pct":   "act.bottom_fan",
    "left_heater_pct":  "act.left_heater",
    "right_heater_pct": "act.right_heater",
    "bottom_heater_pct":"act.bottom_heater",
    "booster_pressure": "act.booster_pump",
    "valve_angle":      "act.prop_valve",
}

SEVERITY_KEYS = {
    1: "fault.sev_info",
    2: "fault.sev_warning",
    3: "fault.sev_shutdown",
    4: "fault.sev_estop",
}

SEVERITY_BADGE_STYLE = {
    1: "info",
    2: "warning",
    3: "error",
    4: "error",
}


def mode_name(code):
    return tr(MODE_NAMES_KEYS.get(code, "mode.idle"))


def state_name(code):
    return tr(STATE_NAMES_KEYS.get(code, "state.idle"))


def process_name(code):
    return tr(PROCESS_TYPE_KEYS.get(code, "process.none"))


def sensor_name(key):
    return tr(SENSOR_KEYS.get(key, key), key)


def actuator_name(key):
    return tr(ACTUATOR_KEYS.get(key, key), key)


def severity_name(sev):
    return tr(SEVERITY_KEYS.get(sev, "fault.unknown"))


def severity_badge_style(sev):
    return SEVERITY_BADGE_STYLE.get(sev, "neutral")


def recipe_name(index):
    """Generate recipe name with current language."""
    template = tr("ctrl.recipe_n", "Recipe {n}")
    return template.format(n=index + 1)
