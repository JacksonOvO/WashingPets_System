"""Host-only tests. No serial connection or GPIO/Flash hardware access."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
APP = HERE.parents[1]
ROOT = APP.parent


def test_workbench(path):
    spec = importlib.util.spec_from_file_location("workbench_test", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    state = module.WorkbenchState()
    assert len(module.OUTPUT_NAMES) == 45 and module.OUTPUT_NAMES[39].startswith("MAGLOCK1")
    assert len(module.INPUT_NAMES) == 21 and module.INPUT_GPIO_COUNT == 19
    assert module.FAULT_CODES[0x0206] == "上门开到位超时"
    assert module.FAULT_CODES[0x0507] == "清洁液低液位"
    for code in (0x0206, 0x0507, 0x0502):
        state.update_fault(module.struct.pack("<IBBH", code, 1, 3, code))
        assert state.status["fault"]["primary_fault_code"] == code
        assert state.status["fault"]["fault_name"] == module.FAULT_CODES[code]
    state.update_io(bytes([21, 45]) + ((1 << 0) | (1 << 10) | (1 << 19) | (1 << 20)).to_bytes(3, "little") + ((1 << 38) | (1 << 39) | (1 << 44)).to_bytes(6, "little"))
    assert len(state.status["io"]["inputs"]) == 21
    assert all(state.status["io"]["inputs"][i]["state"] for i in (0, 10, 19, 20))
    assert "PF15" in state.status["io"]["inputs"][19]["name"]
    assert "PG0" in state.status["io"]["inputs"][20]["name"]
    assert not state.status["io"]["inputs"][18]["state"]
    assert state.status["io"]["outputs"][38]["state"]
    assert state.status["io"]["outputs"][39]["state"]
    assert state.status["io"]["outputs"][44]["state"]
    hw = {"fan_pwm": 1, "temperature_mask": 15, "lower_door_bypass": 0,
          "host_command_bypass": 0, "booster_mode": 1}
    block = state.build_hardware_payload(hw, 42)
    assert len(block) == 14 and block[10:] == bytes([1, 15, 0, 0])
    state.update_hardware(module.struct.pack("<BBHIH", 3, 0, 1, 42, 8) + bytes([1, 15, 0, 0, 0, 1, 0, 0]))
    state.update_hardware(module.struct.pack("<BBHIHB", 3, 1, 1, 42, 1, 1))
    assert state.hardware["booster_mode"] == 1 and state.hardware["fan_pwm"] == 1
    state.update_hardware(module.struct.pack("<BBHIH", 3, 0, 1, 43, 8) + bytes([0, 15, 0, 0, 0, 1, 0, 0]))
    assert state.hardware["booster_mode"] == 1
    pressure = bytearray(41)
    pressure[32] = 31
    state.update_telemetry(pressure)
    assert state.status["power"]["增压泵压力"][0] is None
    module.put_u16(pressure, 22, 8)
    state.update_telemetry(pressure)
    assert state.status["power"]["增压泵压力"][0] == 3.1
    simulated = dict(hw, booster_mode=0, revision=50, busy=False, save_pending=False)
    commands = []
    def exchange(command, payload, timeout=15.0):
        commands.append((command, payload))
        if command == module.CMD_GET_PARAM:
            if payload[1] == 0:
                raw = bytes(simulated[k] for k in ("fan_pwm", "temperature_mask", "lower_door_bypass", "host_command_bypass"))
                raw += bytes([0, simulated["save_pending"], 0, 0])
            else:
                raw = bytes([simulated["booster_mode"]])
            block = module.struct.pack("<BBHIH", 3, payload[1], 1, simulated["revision"], len(raw)) + raw
            state.update_hardware(block)
            return block
        if command == module.CMD_SET_PARAM:
            assert module.u32(payload, 4) == simulated["revision"]
            if payload[1] == 0:
                for k, value in zip(("fan_pwm", "temperature_mask", "lower_door_bypass", "host_command_bypass"), payload[10:]):
                    simulated[k] = value
            else:
                assert len(payload) == 11 and module.u16(payload, 8) == 1
                simulated["booster_mode"] = payload[10]
            simulated["revision"] += 1
            simulated["save_pending"] = True
        if command == module.CMD_SAVE_PARAM:
            assert module.u32(payload, 0) == simulated["revision"]
            simulated["save_pending"] = False
        return module.struct.pack("<BBI", command, 0, simulated["revision"])
    state._hardware_exchange = exchange
    saved = state.apply_hardware(hw)
    assert saved["booster_mode"] == 1 and saved["revision"] == 52 and not saved["save_pending"]
    assert [p[1] for cmd, p in commands if cmd == module.CMD_SET_PARAM] == [0, 1]
    values = {f["key"]: f["min"] for f in module.RECIPE_FIELDS}
    values.update(bathCycles=5, bathOnSec=20, bathPauseSec=5, bathLiquidSec=7.5)
    payload = state.build_param_payload(3, 42, values)
    assert len(payload) == 94 and module.u16(payload, 2) == 3
    assert [module.u32(payload, p) for p in (34, 38, 42, 46)] == [5, 20000, 5000, 7500]
    state.update_parameters(payload)
    assert state.parameters["bathCycles"] == 5
    assert state.parameters["bathOnSec"] == 20
    assert state.parameters["bathLiquidSec"] == 7.5
    assert state.status["paramRevision"] == 42
    old = bytearray(payload)
    module.put_u16(old, 2, 1)
    module.put_u32(old, 34, 120000)
    state.update_parameters(old)
    assert state.parameters["bathCycles"] == 5
    module.put_u16(old, 2, 2)
    state.update_parameters(old)
    assert state.parameters["bathLiquidSec"] == 7.5
    for dose in (0, 3, 7.5, 60):
        p = state.build_param_payload(3, 42, dict(values, bathLiquidSec=dose))
        assert module.u32(p, 46) == round(dose * 1000)
    for dose in (-1, 60.1, float("nan"), float("inf")):
        try:
            state.build_param_payload(3, 42, dict(values, bathLiquidSec=dose))
        except ValueError:
            pass
        else:
            raise AssertionError(f"Accepted invalid dosing time: {dose}")
    for invalid in (-1, 101, 2.5, float("nan")):
        try:
            state.build_param_payload(3, 42, dict(values, bathCycles=invalid))
        except ValueError:
            pass
        else:
            raise AssertionError(f"Accepted invalid cycle count: {invalid}")
    try:
        state.build_param_payload(3, 42, dict(values, bathOnSec=0))
    except ValueError:
        pass
    else:
        raise AssertionError("Accepted dosing without water")
    print(f"{path.relative_to(ROOT)}: recipe protocol PASS", flush=True)


if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="bath-tests-") as build:
        for name in ("test_inputs_remap", "test_dry_remap", "test_self_clean_remap", "test_bath", "test_bath_params", "test_water_tank", "test_tank_high", "test_clean_preheat", "test_pwm_fan", "test_hardware_config", "test_temperature_mask"):
            binary = Path(build) / name
            subprocess.run([
                "gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                "-I", str(HERE), "-I", str(APP / "Core/Inc"),
                str(HERE / f"{name}.c"), "-o", str(binary),
            ], check=True)
            subprocess.run([str(binary)], check=True)
        for mode in (0, 1):
            binary = Path(build) / f"door-open-{mode}"
            subprocess.run([
                "gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                f"-DDOOR_TEST_EXIT={mode}", "-I", str(HERE), "-I", str(APP / "Core/Inc"),
                str(HERE / "test_door_open.c"), "-o", str(binary),
            ], check=True)
            subprocess.run([str(binary)], check=True)
        for mode in (0, 1):
            binary = Path(build) / f"fan-routing-{mode}"
            subprocess.run([
                "gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                f"-DFAN_USE_PWM_MODULE={mode}",
                "-I", str(HERE), "-I", str(APP / "Core/Inc"),
                str(HERE / "test_fan_routing.c"), "-o", str(binary),
            ], check=True)
            subprocess.run([str(binary)], check=True)
    for tool in ("APP_IAP/linux_workbench", "Workbench"):
        test_workbench(ROOT / "WashingPets_Bootloader/tools" / tool / "server.py")
