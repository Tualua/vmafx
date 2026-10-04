# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Tests for the CUDA backend of the GPU section (hw_cuda.py, hw_cudaprobe.py) and for
the build helper that records the build's CUDA targets. Device-free: every runner is
a fake, every device node a file in a temporary directory."""

from __future__ import annotations

import importlib.util
import json
import sys
from pathlib import Path

import pytest

_HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(_HERE.parent / "src"))

from vmaf_rc1_tester import hw_cuda, hw_cudaprobe, hw_gpu
from vmaf_rc1_tester.safe_process import CommandResult

_spec = importlib.util.spec_from_file_location(
    "prepare_build", _HERE.parent / "image" / "prepare_build.py"
)
prepare_build = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(prepare_build)

# The gencode list of core/src/meson.build with nvcc 13.4, as the Meson log prints it.
MESON_LOG = (
    "Message: Found CUDA version = 13.4\n"
    "Message: CUDA gencode = ['--fatbin', '-gencode=arch=compute_80,code=sm_80', "
    "'-gencode=arch=compute_80,code=compute_80', '-gencode=arch=compute_86,code=sm_86', "
    "'-gencode=arch=compute_89,code=sm_89', '-gencode=arch=compute_90,code=sm_90', "
    "'-gencode=arch=compute_100,code=sm_100', '-gencode=arch=compute_120,code=sm_120', "
    "'-gencode=arch=compute_120,code=compute_120']\n"
)
TARGETS = prepare_build.cuda_targets(MESON_LOG)
FIXTURE = {"id": "f1", "ref": "r.yuv", "dis": "d.yuv", "width": 16, "height": 16,
           "pixel_format": "420", "bitdepth": 8}  # fmt: skip
EXACT = {"metrics": ["psnr_y"], "extractor": "psnr_cuda", "options": "", "bound": "0",
         "source": "exact:ADR-1457"}  # fmt: skip
BUDGET = hw_gpu.Budget(fixture=10, gate=10, tests=10)
RTX = {"index": 0, "name": "NVIDIA GeForce RTX 3060", "compute_capability": "8.6",
       "family": "ampere", "supported": True}  # fmt: skip


# ---- build targets -------------------------------------------------------------------------


def test_cuda_targets_come_from_the_meson_log(tmp_path: Path) -> None:
    assert TARGETS == {
        "cuda_version": "13.4",
        "cubins": ["sm_80", "sm_86", "sm_89", "sm_90", "sm_100", "sm_120"],
        "ptx": ["compute_80", "compute_120"],
    }
    with pytest.raises(prepare_build.BuildError, match="no CUDA version or gencode"):
        prepare_build.cuda_targets("Message: CUDA gencode = []\n")
    with pytest.raises(prepare_build.BuildError, match="names no cubin"):
        prepare_build.cuda_targets(
            "Message: Found CUDA version = 13.4\nMessage: CUDA gencode = ['--cuda-gpu-arch=sm_80']\n"
        )
    build = tmp_path / "build"
    (build / "meson-logs").mkdir(parents=True)
    (build / "meson-logs" / "meson-log.txt").write_text(MESON_LOG)
    assert prepare_build.main(["prepare_build.py", "cuda-targets", str(build), str(tmp_path)]) == 0
    assert json.loads((tmp_path / "image" / "cuda-targets.json").read_text()) == TARGETS


# ---- hw_cudaprobe --------------------------------------------------------------------------


@pytest.mark.parametrize(
    ("capability", "family"),
    [
        ((8, 0), "ampere"),  # A100
        ((8, 6), "ampere"),  # RTX 30xx
        ((8, 7), "ampere"),  # Orin
        ((8, 9), "ada"),  # RTX 40xx: the RTX 4090
        ((9, 0), "hopper"),
        ((10, 0), "blackwell"),
        ((10, 3), "blackwell"),
        ((11, 0), "blackwell"),  # Thor
        ((12, 0), "blackwell"),  # RTX 50xx
        ((12, 1), "blackwell"),  # GB10
        ((7, 5), "volta-turing"),  # boundary: below the 8.0 floor
        ((6, 1), "pascal"),
        ((13, 0), "unknown"),
    ],
)
def test_compute_capability_families(capability, family) -> None:
    assert hw_cudaprobe.family_of(*capability) == family


def test_probe_without_the_driver_library_reports_it() -> None:
    result = hw_cudaprobe.probe("libcuda_absent.so.9")
    assert result["status"] == "no_driver_library" and result["devices"] == []


# ---- code path -----------------------------------------------------------------------------


@pytest.mark.parametrize(
    ("capability", "path"),
    [
        ("8.0", "cubin sm_80"),
        ("8.6", "cubin sm_86"),
        ("8.7", "cubin sm_86"),  # a cubin runs on a newer minor of its major
        ("8.9", "cubin sm_89"),
        ("9.0", "cubin sm_90"),
        ("10.3", "cubin sm_100"),
        ("12.1", "cubin sm_120"),
        ("11.0", "ptx compute_80 (compiled by the driver)"),  # no 11.x cubin
        ("13.0", "ptx compute_120 (compiled by the driver)"),
        ("7.5", "none"),  # boundary: below every target
        ("garbage", "none"),
    ],
)
def test_code_path_picks_the_cubin_or_the_ptx_the_driver_loads(capability, path) -> None:
    assert hw_cuda.code_path(capability, TARGETS) == path


def test_code_path_without_recorded_targets_is_none() -> None:
    assert hw_cuda.code_path("8.9", {}) == "none"


# ---- access --------------------------------------------------------------------------------


def nodes(dev: Path, *names: str) -> None:
    dev.mkdir(exist_ok=True)
    for name in names:
        (dev / name).write_text("")
        (dev / name).chmod(0o666)


def test_access_takes_the_toolkit_device_nodes(tmp_path: Path) -> None:
    dev = tmp_path / "dev"
    nodes(dev, "nvidiactl", "nvidia-uvm", "nvidia0")
    (dev / "nvidia-caps").mkdir()
    facts = hw_cuda.access_facts(dev, tmp_path / "dxg", tmp_path / "libcuda.so.1")
    assert facts["path"] == "nvidia"
    assert [n["node"] for n in facts["device_nodes"]] == ["nvidia-uvm", "nvidia0", "nvidiactl"]


def test_access_without_nodes_or_with_only_dxg(tmp_path: Path) -> None:
    dev = tmp_path / "dev"
    nodes(dev)
    facts = hw_cuda.access_facts(dev, tmp_path / "dxg", tmp_path / "libcuda.so.1")
    assert facts["path"] == "none" and "--gpus all" in hw_cuda.missing_access_reason(facts)
    nodes(dev, "nvidia0")  # no nvidiactl: what a bare --device /dev/nvidia0 gives
    facts = hw_cuda.access_facts(dev, tmp_path / "dxg", tmp_path / "libcuda.so.1")
    assert facts["path"] == "none" and "incomplete" in hw_cuda.missing_access_reason(facts)
    (tmp_path / "dxg").write_text("")
    facts = hw_cuda.access_facts(dev, tmp_path / "dxg", tmp_path / "libcuda.so.1")
    assert facts["path"] == "none" and "WSL2" in hw_cuda.missing_access_reason(facts)
    (tmp_path / "libcuda.so.1").write_text("")
    assert hw_cuda.access_facts(dev, tmp_path / "dxg", tmp_path / "libcuda.so.1")["path"] == "wsl"


def test_probe_reasons_name_the_missing_option() -> None:
    facts = {"path": "nvidia"}
    missing = hw_cuda.probe_reason({"status": "no_driver_library"}, facts)
    assert "libcuda.so.1" in missing and "--gpus all" in missing
    failed = hw_cuda.probe_reason({"status": "init_failed", "error": "cuInit returned "
                                   "CUDA_ERROR_INSUFFICIENT_DRIVER",
                                   "driver_cuda_version": "12.8"}, facts)  # fmt: skip
    assert "CUDA_ERROR_INSUFFICIENT_DRIVER" in failed and "12.8" in failed


# ---- discovery and pinning -----------------------------------------------------------------


def probe_runner(document: dict, seen: list):
    def run(argv, **kwargs):
        seen.append((argv, kwargs["environment"]))
        return CommandResult(0, json.dumps(document), "")

    return run


def fake_root(tmp_path: Path) -> Path:
    (tmp_path / "image").mkdir(parents=True, exist_ok=True)
    (tmp_path / "image" / "cuda-targets.json").write_text(json.dumps(TARGETS))
    return tmp_path


def test_discover_runs_supported_devices_in_bus_order(tmp_path: Path, monkeypatch) -> None:
    monkeypatch.setattr(hw_cuda, "access_facts", lambda: {"path": "nvidia", "device_nodes": []})
    turing = {"index": 1, "name": "Tesla T4", "compute_capability": "7.5", "supported": False}
    seen: list = []
    document = {"status": "ok", "driver_cuda_version": "13.4", "devices": [RTX, turing]}
    found = hw_cuda.discover(fake_root(tmp_path), probe_runner(document, seen))
    assert [d["index"] for d in found["devices"]] == [0]
    assert found["devices"][0]["facts"]["code_path"] == "cubin sm_86"
    assert found["runtime"] == {"driver_cuda_version": "13.4", "build_cuda_version": "13.4"}
    assert "below compute capability 8.0" in found["access"]["devices_left_out"][0]["reason"]
    assert seen[0][1]["CUDA_DEVICE_ORDER"] == "PCI_BUS_ID"


def test_discover_with_only_an_old_device_says_why(tmp_path: Path, monkeypatch) -> None:
    monkeypatch.setattr(hw_cuda, "access_facts", lambda: {"path": "nvidia", "device_nodes": []})
    turing = {"index": 0, "name": "Tesla T4", "compute_capability": "7.5", "supported": False}
    document = {"status": "ok", "devices": [turing]}
    found = hw_cuda.discover(fake_root(tmp_path), probe_runner(document, []))
    assert found["devices"] == [] and "Tesla T4 (7.5)" in found["reason"]


def test_device_env_pins_one_device_in_the_probe_order() -> None:
    assert hw_cuda.device_env({"index": 2}) == {"CUDA_DEVICE_ORDER": "PCI_BUS_ID",
                                                "CUDA_VISIBLE_DEVICES": "2"}  # fmt: skip


# ---- the whole section with the CUDA backend -------------------------------------------------


def section_image(root: Path) -> None:
    fake_root(root)
    (root / "image" / "gpu-twins.json").write_text(
        json.dumps({"backend": "cuda", "features": {"psnr": EXACT}})
    )
    (root / "image" / "cuda-rows.json").write_text(json.dumps({"rows": [
        {"id": "T-X-2026-10-03", "part": "Ampere", "families": ["ampere"], "tests": ["test_a"]},
        {"id": "T-X-2026-10-03", "part": "Hopper", "families": ["hopper"], "tests": ["test_a"]},
    ]}))  # fmt: skip
    (root / "image" / "gpu-tests.json").write_text(
        json.dumps({"tests": [{"name": "test_a", "cmd": "/t/test_a"}], "left_out": []})
    )


def section_runner(device_value: float, seen: list):
    def run(argv, **kwargs):
        seen.append((argv, kwargs.get("environment", {})))
        if "hw_cudaprobe" in " ".join(argv):
            return CommandResult(0, json.dumps({"status": "ok", "devices": [RTX]}), "")
        if "--output" in argv:
            document = {"frames": [{"frameNum": 0, "metrics": {"psnr_y": device_value}}],
                        "feature_backends": [{"extractor": "psnr_cuda", "backend": "cuda"}]}  # fmt: skip
            Path(argv[argv.index("--output") + 1]).write_text(json.dumps(document))
            return CommandResult(0, "", "")
        return CommandResult(0 if argv[0] == "/t/test_a" else 1, "", "")

    return run


def test_section_pins_every_run_and_applies_the_family_rows(tmp_path: Path, monkeypatch) -> None:
    monkeypatch.setattr(hw_cuda, "access_facts", lambda: {"path": "nvidia", "device_nodes": []})
    section_image(tmp_path)
    seen: list = []
    section = hw_gpu.run_gpu_section(tmp_path, hw_cuda.CUDA, [FIXTURE], {"f1": {"psnr_y": [40.0]}},
                                     BUDGET, runner=section_runner(40.0, seen))  # fmt: skip
    device = section["devices"][0]
    assert device["twins"]["status"] == "identical" and device["audits"] == {}
    assert device["device_tests"]["results"] == {"test_a": "pass"}
    runs = [env for argv, env in seen if "hw_cudaprobe" not in " ".join(argv)]
    assert runs and all(env.get("CUDA_VISIBLE_DEVICES") == "0" for env in runs)
    rows = {row["part"]: row["verdict"] for row in device["rows"]["rows"]}
    assert rows == {"Ampere": "pass", "Hopper": "not_applicable"}
    summary = hw_gpu.summary_lines(section)[1]
    assert "(ampere 8.6)" in summary


def test_planted_wrong_cpu_value_fails_closed_and_names_the_frame(
    tmp_path: Path, monkeypatch
) -> None:
    monkeypatch.setattr(hw_cuda, "access_facts", lambda: {"path": "nvidia", "device_nodes": []})
    section_image(tmp_path)
    section = hw_gpu.run_gpu_section(
        tmp_path, hw_cuda.CUDA, [FIXTURE], {"f1": {"psnr_y": [40.000000000000007]}}, BUDGET,
        runner=section_runner(40.0, []),
    )  # fmt: skip
    feature = section["devices"][0]["twins"]["features"][0]
    assert feature["status"] == "differing" and section["status"] == "fail"
    assert feature["first"] == {"fixture": "f1", "metric": "psnr_y", "frame": 0,
                                "cpu": "40.000000000000007", "device": "40"}  # fmt: skip


def test_no_device_is_no_device_with_the_missing_option(tmp_path: Path, monkeypatch) -> None:
    empty = {"path": "none", "device_nodes": [], "dxg_present": False,
             "wsl_libcuda_present": False}  # fmt: skip
    monkeypatch.setattr(hw_cuda, "access_facts", lambda: dict(empty))
    section_image(tmp_path)
    no_driver = {"status": "no_driver_library", "error": "libcuda.so.1: cannot open", "devices": []}
    section = hw_gpu.run_gpu_section(tmp_path, hw_cuda.CUDA, [FIXTURE], {}, BUDGET,
                                     runner=probe_runner(no_driver, []))  # fmt: skip
    assert section["status"] == "no_device" and section["access"]["path"] == "none"
    assert "--gpus all" in section["reason"]
    assert hw_gpu.gpu_not_exercised(section) == [("CUDA twins", section["reason"])]


def test_windows_reaches_the_gpu_through_the_display_driver(tmp_path: Path) -> None:
    facts = hw_cuda.windows_access_facts(tmp_path)
    assert facts == {"driver_library": "nvcuda.dll", "driver_library_present": False,
                     "path": "none"}  # fmt: skip
    reason = hw_cuda.missing_access_reason(facts)
    assert "nvcuda.dll is not in System32" in reason and "NVIDIA display driver" in reason
    (tmp_path / "nvcuda.dll").write_bytes(b"MZ")
    facts = hw_cuda.windows_access_facts(tmp_path)
    assert facts["path"] == "windows"
    broken = hw_cuda.probe_reason({"status": "no_driver_library", "error": "error 126"}, facts)
    assert "does not load (error 126)" in broken and "--gpus" not in broken


def test_host_access_follows_the_platform(monkeypatch) -> None:
    monkeypatch.setattr(hw_cuda, "windows_access_facts", lambda: {"path": "windows"})
    monkeypatch.setattr(hw_cuda, "access_facts", lambda: {"path": "nvidia"})
    monkeypatch.setattr(hw_cuda.os, "name", "nt")
    assert hw_cuda.host_access()["path"] == "windows"
    monkeypatch.setattr(hw_cuda.os, "name", "posix")
    assert hw_cuda.host_access()["path"] == "nvidia"
