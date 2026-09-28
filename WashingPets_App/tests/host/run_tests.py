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
        for name in ("test_bath", "test_bath_params", "test_water_tank", "test_tank_high", "test_clean_preheat"):
            binary = Path(build) / name
            subprocess.run([
                "gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                "-I", str(HERE), "-I", str(APP / "Core/Inc"),
                str(HERE / f"{name}.c"), "-o", str(binary),
            ], check=True)
            subprocess.run([str(binary)], check=True)
    for tool in ("APP_IAP/linux_workbench", "Workbench"):
        test_workbench(ROOT / "WashingPets_Bootloader/tools" / tool / "server.py")
