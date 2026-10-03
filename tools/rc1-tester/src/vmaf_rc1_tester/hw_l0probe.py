# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Intel GPUs as the Level Zero loader reports them (the Intel GPU tester image).

Runs as its own process, `python3 -m vmaf_rc1_tester.hw_l0probe`, under the
caller's time limit: a broken driver can hang `zeInit`. Prints one JSON document
with, per GPU, its name, PCI vendor and device ID, GPU IP version and family,
execution-unit layout, sub-group sizes and driver version. It reads no UUID,
serial number or PCI address: the UUID fields of the structures are never copied.

The structure layouts are those of `level_zero/ze_api.h` (Level Zero 1.x). The
IP version is `(major << 22) | (minor << 14) | revision`, the form `ocloc ids`
prints as `major.minor.revision`; the families below follow `ocloc ids` of
compute-runtime 26.35 for the targets of the default SYCL AOT list.
"""

from __future__ import annotations

import ctypes
import json
import sys
from typing import Any

LOADER = "libze_loader.so.1"
ZE_INIT_FLAG_GPU_ONLY = 1
STYPE_DRIVER_PROPERTIES = 0x1
STYPE_DEVICE_PROPERTIES = 0x3
STYPE_DEVICE_COMPUTE_PROPERTIES = 0x4
STYPE_DEVICE_IP_VERSION_EXT = 0x1000F
FLAG_INTEGRATED = 1
DEVICE_TYPE_GPU = 1
MAX_DRIVERS = 8
MAX_DEVICES = 16
# (major, first minor, last minor) -> family. tgllp 12.0, rkl 12.1, adl-s/rpl-s 12.2,
# adl-p/rpl-p 12.3, adl-n 12.4, dg1 12.10; dg2/acm 12.55-12.57; pvc 12.60;
# mtl-u/arl-s 12.70, mtl-h 12.71, arl-h 12.74; bmg 20.1/20.2, lnl 20.4; ptl 30.
FAMILIES = (
    (12, 0, 4, "xe-lp"),
    (12, 10, 10, "xe-lp"),
    (12, 55, 57, "xe-hpg"),
    (12, 60, 60, "xe-hpc"),
    (12, 70, 74, "xe-lpg"),
    (20, 0, 63, "xe2"),
    (30, 0, 63, "xe3"),
)


class DriverProperties(ctypes.Structure):
    _fields_ = [
        ("stype", ctypes.c_uint32),
        ("pNext", ctypes.c_void_p),
        ("uuid", ctypes.c_uint8 * 16),
        ("driverVersion", ctypes.c_uint32),
    ]


class IpVersion(ctypes.Structure):
    _fields_ = [
        ("stype", ctypes.c_uint32),
        ("pNext", ctypes.c_void_p),
        ("ipVersion", ctypes.c_uint32),
    ]


class DeviceProperties(ctypes.Structure):
    _fields_ = [
        ("stype", ctypes.c_uint32),
        ("pNext", ctypes.c_void_p),
        ("type", ctypes.c_uint32),
        ("vendorId", ctypes.c_uint32),
        ("deviceId", ctypes.c_uint32),
        ("flags", ctypes.c_uint32),
        ("subdeviceId", ctypes.c_uint32),
        ("coreClockRate", ctypes.c_uint32),
        ("maxMemAllocSize", ctypes.c_uint64),
        ("maxHardwareContexts", ctypes.c_uint32),
        ("maxCommandQueuePriority", ctypes.c_uint32),
        ("numThreadsPerEU", ctypes.c_uint32),
        ("physicalEUSimdWidth", ctypes.c_uint32),
        ("numEUsPerSubslice", ctypes.c_uint32),
        ("numSubslicesPerSlice", ctypes.c_uint32),
        ("numSlices", ctypes.c_uint32),
        ("timerResolution", ctypes.c_uint64),
        ("timestampValidBits", ctypes.c_uint32),
        ("kernelTimestampValidBits", ctypes.c_uint32),
        ("uuid", ctypes.c_uint8 * 16),
        ("name", ctypes.c_char * 256),
    ]


class ComputeProperties(ctypes.Structure):
    _fields_ = [
        ("stype", ctypes.c_uint32),
        ("pNext", ctypes.c_void_p),
        ("maxTotalGroupSize", ctypes.c_uint32),
        ("maxGroupSizeX", ctypes.c_uint32),
        ("maxGroupSizeY", ctypes.c_uint32),
        ("maxGroupSizeZ", ctypes.c_uint32),
        ("maxGroupCountX", ctypes.c_uint32),
        ("maxGroupCountY", ctypes.c_uint32),
        ("maxGroupCountZ", ctypes.c_uint32),
        ("maxSharedLocalMemory", ctypes.c_uint32),
        ("numSubGroupSizes", ctypes.c_uint32),
        ("subGroupSizes", ctypes.c_uint32 * 8),
    ]


def ip_string(ip_version: int) -> str:
    """`major.minor.revision` of a Level Zero IP version, as `ocloc ids` prints it."""
    return f"{ip_version >> 22}.{(ip_version >> 14) & 0xFF}.{ip_version & 0x3FFF}"


def family_of(ip_version: int) -> str:
    """The GPU family of an IP version, or `unknown`."""
    major, minor = ip_version >> 22, (ip_version >> 14) & 0xFF
    for fam_major, first, last, name in FAMILIES:
        if major == fam_major and first <= minor <= last:
            return name
    return "unknown"


def describe_device(props: DeviceProperties, compute: ComputeProperties, ip: int) -> dict:
    """The report's facts about one device; no UUID."""
    sizes = [int(compute.subGroupSizes[i]) for i in range(min(compute.numSubGroupSizes, 8))]
    eus = props.numSlices * props.numSubslicesPerSlice * props.numEUsPerSubslice
    return {
        "name": props.name.decode("utf-8", errors="replace").strip(),
        "vendor_id": f"0x{props.vendorId:04x}",
        "device_id": f"0x{props.deviceId:04x}",
        "integrated": bool(props.flags & FLAG_INTEGRATED),
        "ip_version": ip_string(ip) if ip else "unknown",
        "family": family_of(ip) if ip else "unknown",
        "eu_count": int(eus),
        "threads_per_eu": int(props.numThreadsPerEU),
        "eu_simd_width": int(props.physicalEUSimdWidth),
        "core_clock_mhz": int(props.coreClockRate),
        "sub_group_sizes": sizes,
        "max_mem_alloc_bytes": int(props.maxMemAllocSize),
    }


def handles(getter: Any, owner: Any, limit: int) -> list[ctypes.c_void_p]:
    """Handles from a Level Zero `Get(owner, &count, array)` pair of calls."""
    count = ctypes.c_uint32(0)
    args = (owner,) if owner is not None else ()
    if getter(*args, ctypes.byref(count), None) != 0 or count.value == 0:
        return []
    count.value = min(count.value, limit)
    array = (ctypes.c_void_p * count.value)()
    if getter(*args, ctypes.byref(count), array) != 0:
        return []
    return [ctypes.c_void_p(array[i]) for i in range(count.value)]


def device_facts(lib: Any, device: ctypes.c_void_p) -> dict | None:
    """Facts of one Level Zero device, or None when it is not a GPU."""
    ip = IpVersion(STYPE_DEVICE_IP_VERSION_EXT, None, 0)
    props = DeviceProperties()
    props.stype = STYPE_DEVICE_PROPERTIES
    props.pNext = ctypes.cast(ctypes.pointer(ip), ctypes.c_void_p)
    if lib.zeDeviceGetProperties(device, ctypes.byref(props)) != 0:
        return None
    if props.type != DEVICE_TYPE_GPU:
        return None
    compute = ComputeProperties()
    compute.stype = STYPE_DEVICE_COMPUTE_PROPERTIES
    if lib.zeDeviceGetComputeProperties(device, ctypes.byref(compute)) != 0:
        compute.numSubGroupSizes = 0
    return describe_device(props, compute, int(ip.ipVersion))


def driver_version(lib: Any, driver: ctypes.c_void_p) -> int | None:
    props = DriverProperties()
    props.stype = STYPE_DRIVER_PROPERTIES
    if lib.zeDriverGetProperties(driver, ctypes.byref(props)) != 0:
        return None
    return int(props.driverVersion)


def probe(loader: str = LOADER) -> dict[str, Any]:
    """Every Level Zero GPU in loader order: the order `level_zero:<n>` selects."""
    try:
        lib = ctypes.CDLL(loader)
    except OSError as error:
        return {"status": "no_loader", "error": str(error)[:200], "devices": []}
    lib.zeInit.restype = ctypes.c_uint32
    result = lib.zeInit(ZE_INIT_FLAG_GPU_ONLY)
    if result != 0:
        return {"status": "init_failed", "error": f"zeInit returned 0x{result:08x}", "devices": []}
    devices: list[dict[str, Any]] = []
    for driver in handles(lib.zeDriverGet, None, MAX_DRIVERS):
        version = driver_version(lib, driver)
        for device in handles(lib.zeDeviceGet, driver, MAX_DEVICES):
            facts = device_facts(lib, device)
            if facts is not None:
                facts["driver_version"] = version
                devices.append(facts)
    for index, facts in enumerate(devices):
        facts["index"] = index
    return {"status": "ok" if devices else "no_device", "devices": devices}


def main() -> int:
    print(json.dumps(probe(), sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
