# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""The CUDA backend of the GPU section (the NVIDIA GPU tester image).

The image holds no NVIDIA library: libvmaf loads the driver's `libcuda.so.1` at
run time. How the image reaches an NVIDIA GPU, recorded as facts and as one `path`:

- `nvidia`: the NVIDIA device nodes (`/dev/nvidiactl`, `/dev/nvidia<N>`), which
  the NVIDIA Container Toolkit adds with the host driver's libraries for
  `--gpus all` or the CDI device `nvidia.com/gpu=all`;
- `wsl`: WSL2's paravirtualised GPU, `/dev/dxg`, with the host driver's
  `/usr/lib/wsl/lib/libcuda.so.1` mounted (Docker Desktop does both for
  `--gpus all`);
- `none`: neither, with the reason and the `docker run` option that is missing.

The devices are the CUDA devices of the driver (hw_cudaprobe.py, its own bounded
process) in PCI bus order; every run of a device is pinned with
`CUDA_VISIBLE_DEVICES=<n>` under the same order. A device below the build's
compute capability floor (8.0, ADR-1223), or one no kernel of the build can run
on, is listed with the reason and not run. Per device the facts name the code
path the driver takes for its kernels: a cubin of the build, or the PTX the
driver compiles at load time (image/cuda-targets.json, written from the build's
gencode list). CUDA has no audit test.
"""

from __future__ import annotations

import json
import os
import sys
from collections.abc import Mapping, Sequence
from pathlib import Path
from typing import Any

from .hw_equiv import Runner
from .hw_gpu import GpuBackend

DEV = Path("/dev")
DXG = Path("/dev/dxg")
WSL_LIBCUDA = Path("/usr/lib/wsl/lib/libcuda.so.1")
PROBE_TIMEOUT_SECONDS = 120.0
DEVICE_ORDER = {"CUDA_DEVICE_ORDER": "PCI_BUS_ID"}
TOOLKIT_HINT = ("add --gpus all (NVIDIA Container Toolkit; with CDI: "
                "--device nvidia.com/gpu=all)")  # fmt: skip


def node_facts(dev: Path = DEV) -> list[dict[str, Any]]:
    """The NVIDIA device nodes visible in the container and whether this process
    can open them for reading and writing."""
    nodes = []
    for node in sorted(dev.glob("nvidia*")):
        if node.is_dir():  # /dev/nvidia-caps
            continue
        nodes.append({"node": node.name, "accessible": os.access(node, os.R_OK | os.W_OK)})
    return nodes


def access_facts(dev: Path = DEV, dxg: Path = DXG, libcuda: Path = WSL_LIBCUDA) -> dict[str, Any]:
    """How the container can reach an NVIDIA GPU, and the path a run takes."""
    nodes = node_facts(dev)
    names = {node["node"] for node in nodes if node["accessible"]}
    facts: dict[str, Any] = {
        "device_nodes": nodes,
        "dxg_present": dxg.exists(),
        "dxg_accessible": dxg.exists() and os.access(dxg, os.R_OK | os.W_OK),
        "wsl_libcuda_present": libcuda.is_file(),
        "groups": sorted(os.getgroups()),
    }
    if facts["dxg_accessible"] and facts["wsl_libcuda_present"]:
        facts["path"] = "wsl"
    elif "nvidiactl" in names and any(name[6:].isdigit() for name in names):
        facts["path"] = "nvidia"
    else:
        facts["path"] = "none"
    return facts


def missing_access_reason(facts: Mapping[str, Any]) -> str:
    """Why no NVIDIA GPU is reachable, with the `docker run` option that fixes it."""
    if facts["dxg_present"] and not facts["wsl_libcuda_present"]:
        return ("/dev/dxg is present but the WSL2 driver library /usr/lib/wsl/lib/libcuda.so.1 is"
                " not: run with --gpus all under Docker Desktop's WSL2 backend")  # fmt: skip
    if facts["dxg_present"]:
        return "/dev/dxg is not readable and writable by this container's user"
    if facts["device_nodes"] and not any(node["accessible"] for node in facts["device_nodes"]):
        return "the NVIDIA device nodes are not readable and writable by this container's user"
    if facts["device_nodes"]:
        return "the NVIDIA device nodes are incomplete (no /dev/nvidiactl or /dev/nvidia<N>)"
    return f"no NVIDIA GPU device node is visible: {TOOLKIT_HINT}"


def probe_reason(probe: Mapping[str, Any], facts: Mapping[str, Any]) -> str:
    """Why the driver reports no usable device although a device node is visible."""
    status = probe.get("status")
    if status == "no_driver_library":
        return ("the NVIDIA device is visible but the driver library libcuda.so.1 is not: the "
                f"NVIDIA Container Toolkit did not mount it; {TOOLKIT_HINT}, not --device "
                "/dev/nvidia0")  # fmt: skip
    if status == "init_failed":
        return (f"the CUDA driver did not start ({probe.get('error', '')}; the driver supports "
                f"CUDA {probe.get('driver_cuda_version', 'unknown')}) on path {facts['path']}")  # fmt: skip
    return f"the CUDA driver reports no device ({status}): {probe.get('error', '')}".rstrip(": ")


def load_targets(path: Path) -> dict[str, Any]:
    """image/cuda-targets.json: the build's cubin and PTX targets, {} when absent."""
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {}
    return document if isinstance(document, dict) else {}


def arch_tuple(target: str) -> tuple[int, int]:
    """`sm_86` or `compute_120` -> (8, 6) or (12, 0)."""
    digits = target.rsplit("_", 1)[-1]
    return int(digits[:-1]), int(digits[-1])


def capability(text: str) -> tuple[int, int]:
    major, _, minor = str(text).partition(".")
    try:
        return int(major), int(minor)
    except ValueError:
        return (0, 0)


def code_path(compute_capability: str, targets: Mapping[str, Any]) -> str:
    """Which kernel code the driver loads on a device of this compute capability: the
    newest cubin of the same major no newer than the device, else the newest PTX no
    newer than the device (compiled by the driver at load time), else `none`."""
    device = capability(compute_capability)
    cubins = sorted(arch_tuple(t) for t in targets.get("cubins", []))
    native = [c for c in cubins if c[0] == device[0] and c[1] <= device[1]]
    if native:
        return "cubin sm_{}{}".format(*native[-1])
    ptx = [p for p in sorted(arch_tuple(t) for t in targets.get("ptx", [])) if p <= device]
    if ptx:
        return "ptx compute_{}{} (compiled by the driver)".format(*ptx[-1])
    return "none"


def probe_devices(runner: Runner) -> dict[str, Any]:
    """The CUDA probe in its own process; `error` when it does not finish."""
    src = str(Path(__file__).resolve().parents[1])
    env = {**os.environ, "PYTHONPATH": src, **DEVICE_ORDER}
    try:
        result = runner(
            [sys.executable, "-B", "-m", "vmaf_rc1_tester.hw_cudaprobe"], environment=env,
            timeout_seconds=PROBE_TIMEOUT_SECONDS, max_output_bytes=262_144,
        )  # fmt: skip
        document = json.loads(result.stdout)
    except (TimeoutError, RuntimeError, ValueError, OSError) as error:
        return {"status": "error", "error": str(error)[:200], "devices": []}
    return document if isinstance(document, dict) else {"status": "error", "devices": []}


def split_devices(
    probed: Sequence[Mapping[str, Any]], targets: Mapping[str, Any]
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    """(devices to run, devices left out with the reason)."""
    usable: list[dict[str, Any]] = []
    left_out: list[dict[str, Any]] = []
    for device in probed:
        facts = {**device, "code_path": code_path(str(device.get("compute_capability")), targets)}
        if not facts.get("supported"):
            left_out.append({**facts, "reason": "below compute capability 8.0, the build's "
                             "floor (ADR-1223)"})  # fmt: skip
        elif facts["code_path"] == "none":
            left_out.append({**facts, "reason": "no cubin or PTX of the build runs on it"})
        else:
            usable.append({"index": int(device["index"]), "facts": facts})
    return usable, left_out


def discover(root: Path, runner: Runner) -> dict[str, Any]:
    """Access facts, driver and build versions, and the usable CUDA devices of this host."""
    facts = access_facts()
    probe = probe_devices(runner)
    targets = load_targets(root / "image" / "cuda-targets.json")
    facts["cuda_driver"] = {k: v for k, v in probe.items() if k != "devices"}
    devices, left_out = split_devices(probe.get("devices", []), targets)
    if left_out:
        facts["devices_left_out"] = left_out
    runtime = {"driver_cuda_version": str(probe.get("driver_cuda_version", "unknown")),
               "build_cuda_version": str(targets.get("cuda_version", "unknown"))}  # fmt: skip
    found: dict[str, Any] = {"access": facts, "runtime": runtime, "devices": devices}
    if not devices:
        if left_out:
            names = ", ".join(f"{d.get('name')} ({d.get('compute_capability')})" for d in left_out)
            found["reason"] = f"no NVIDIA GPU this build runs on: {names}: {left_out[0]['reason']}"
        elif facts["path"] == "none":
            found["reason"] = missing_access_reason(facts)
        else:
            found["reason"] = probe_reason(probe, facts)
    return found


def device_env(device: Mapping[str, Any]) -> dict[str, str]:
    """Pins every run to one CUDA device, in the probe's order."""
    return {**DEVICE_ORDER, "CUDA_VISIBLE_DEVICES": str(int(device["index"]))}


CUDA = GpuBackend(
    name="cuda",
    discover=discover,
    device_env=device_env,
    audits={},
    row_map="cuda-rows.json",
)
