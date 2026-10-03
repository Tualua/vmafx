# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""NVIDIA GPUs as the CUDA driver reports them (the NVIDIA GPU tester image).

Runs as its own process, `python3 -m vmaf_rc1_tester.hw_cudaprobe`, under the
caller's time limit: a broken driver can hang `cuInit`. It loads the driver
library the way libvmaf does (`libcuda.so.1`, which the NVIDIA Container Toolkit
mounts from the host, or WSL2's `/usr/lib/wsl/lib/libcuda.so.1`) and prints one
JSON document with, per GPU, its name, compute capability and family,
multiprocessor count, total memory and whether it is integrated, plus the
driver's CUDA version. It reads no UUID, serial number or PCI bus ID.

The order is the driver's under the caller's `CUDA_DEVICE_ORDER` (the backend
sets `PCI_BUS_ID` for the probe and for every run, so `CUDA_VISIBLE_DEVICES=<n>`
pins a run to device n of this list). The attribute numbers are those of
`cuda.h` (CUDA 13).
"""

from __future__ import annotations

import ctypes
import json
import sys
from typing import Any

DRIVER = "libcuda.so.1"
ATTRIBUTE_CLOCK_RATE = 13
ATTRIBUTE_MULTIPROCESSOR_COUNT = 16
ATTRIBUTE_INTEGRATED = 18
ATTRIBUTE_CC_MAJOR = 75
ATTRIBUTE_CC_MINOR = 76
MAX_DEVICES = 16
NAME_BYTES = 256
# Compute capability major -> family (NVIDIA's architecture names). Below 8.0 is
# outside the build's floor (ADR-1223); 10.x, 11.x and 12.x are Blackwell
# (data centre, Jetson Thor and GeForce / DGX Spark).
FAMILIES = {8: "ampere", 9: "hopper", 10: "blackwell", 11: "blackwell", 12: "blackwell"}
# The families a supported device can have (8.9 is Ada, the rest of 8.x Ampere).
FAMILY_NAMES = ("ampere", "ada", "hopper", "blackwell")
CC_FLOOR = (8, 0)


def family_of(major: int, minor: int) -> str:
    """The architecture family of a compute capability, or `unknown`."""
    if (major, minor) == (8, 9):
        return "ada"
    if (major, minor) < CC_FLOOR:
        return {7: "volta-turing", 6: "pascal"}.get(major, "unknown")
    return FAMILIES.get(major, "unknown")


def error_name(lib: Any, code: int) -> str:
    """`CUDA_ERROR_...` of a driver result, from the driver itself."""
    text = ctypes.c_char_p()
    if lib.cuGetErrorName(code, ctypes.byref(text)) != 0 or not text.value:
        return f"error {code}"
    return text.value.decode("ascii", errors="replace")


def attribute(lib: Any, device: int, which: int) -> int:
    value = ctypes.c_int(0)
    if lib.cuDeviceGetAttribute(ctypes.byref(value), which, device) != 0:
        return -1
    return int(value.value)


def describe_device(lib: Any, ordinal: int) -> dict[str, Any] | None:
    """The report's facts about one device; no UUID, no PCI bus ID."""
    device = ctypes.c_int(0)
    if lib.cuDeviceGet(ctypes.byref(device), ordinal) != 0:
        return None
    name = ctypes.create_string_buffer(NAME_BYTES)
    if lib.cuDeviceGetName(name, NAME_BYTES, device) != 0:
        return None
    memory = ctypes.c_size_t(0)
    if lib.cuDeviceTotalMem_v2(ctypes.byref(memory), device) != 0:
        memory.value = 0
    major = attribute(lib, device, ATTRIBUTE_CC_MAJOR)
    minor = attribute(lib, device, ATTRIBUTE_CC_MINOR)
    return {
        "name": name.value.decode("utf-8", errors="replace").strip(),
        "compute_capability": f"{major}.{minor}",
        "family": family_of(major, minor),
        "supported": (major, minor) >= CC_FLOOR,
        "multiprocessors": attribute(lib, device, ATTRIBUTE_MULTIPROCESSOR_COUNT),
        "clock_mhz": attribute(lib, device, ATTRIBUTE_CLOCK_RATE) // 1000,
        "integrated": attribute(lib, device, ATTRIBUTE_INTEGRATED) == 1,
        "memory_bytes": int(memory.value),
    }


def driver_version(lib: Any) -> str:
    """The CUDA version the driver supports, as `13.2`, or `unknown`."""
    version = ctypes.c_int(0)
    if lib.cuDriverGetVersion(ctypes.byref(version)) != 0:
        return "unknown"
    return f"{version.value // 1000}.{(version.value % 1000) // 10}"


def probe(driver: str = DRIVER) -> dict[str, Any]:
    """Every CUDA device in the driver's order: the order `CUDA_VISIBLE_DEVICES` uses."""
    try:
        lib = ctypes.CDLL(driver)
    except OSError as error:
        return {"status": "no_driver_library", "error": str(error)[:200], "devices": []}
    result = lib.cuInit(0)
    if result != 0:
        return {"status": "init_failed", "error": f"cuInit returned {error_name(lib, result)}",
                "driver_cuda_version": driver_version(lib), "devices": []}  # fmt: skip
    count = ctypes.c_int(0)
    if lib.cuDeviceGetCount(ctypes.byref(count)) != 0:
        count.value = 0
    devices = []
    for ordinal in range(min(count.value, MAX_DEVICES)):
        facts = describe_device(lib, ordinal)
        if facts is not None:
            facts["index"] = ordinal
            devices.append(facts)
    return {"status": "ok" if devices else "no_device", "driver_cuda_version": driver_version(lib),
            "devices": devices}  # fmt: skip


def main() -> int:
    print(json.dumps(probe(), sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
