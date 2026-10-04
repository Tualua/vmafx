# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Host facts of a Windows machine for the tester report (the Windows zip).

Windows has no `/proc/cpuinfo` and no `AT_HWCAP`. The processor's name, vendor and
family come from three values of the registry key Windows fills at boot
(`HKLM\\HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0`); the instruction-set
features from `IsProcessorFeaturePresent` (kernel32). Nothing here reads a host
name, user name, serial number, MAC address or network address.
"""

from __future__ import annotations

import ctypes
import os
import platform
import re
from collections.abc import Callable
from typing import Any

CPU_REGISTRY_KEY = r"HARDWARE\DESCRIPTION\System\CentralProcessor\0"
# The registry values read, and the allow-listed cpuinfo key each one fills.
REGISTRY_VALUES = (("ProcessorNameString", "model name"), ("VendorIdentifier", "vendor_id"))
# "Intel64 Family 6 Model 151 Stepping 2", "ARMv8 (64-bit) Family 8 Model D4B Revision 0".
IDENTIFIER = re.compile(r"Family (\S+) Model (\S+) (?:Stepping|Revision) (\S+)")
X86_IDENTIFIER_KEYS = ("cpu family", "model", "stepping")
ARM_IDENTIFIER_KEYS = ("cpu architecture", "cpu part", "cpu revision")
# PF_* values of IsProcessorFeaturePresent (processthreadsapi.h, Microsoft Learn, read
# 2026-10-04). SSSE3 to AVX512F need Windows 10 2004; BMI2 and the SVE family 24H2.
X86_FEATURES = {
    "sse": 6, "sse2": 10, "sse3": 13, "xsave": 17, "ssse3": 36, "sse4_1": 37,
    "sse4_2": 38, "avx": 39, "avx2": 40, "avx512f": 41, "bmi2": 60,
}  # fmt: skip
ARM_FEATURES = {
    "crc32": 31, "atomics": 34, "asimddp": 43, "jscvt": 44, "lrcpc": 45, "sve": 46,
    "sve2": 47, "i8mm": 66, "fp16": 67, "bf16": 68, "sme": 70, "sme2": 71,
}  # fmt: skip
# core/src/x86/cpu.c, in its order: each level needs the features listed. Windows
# reports AVX512F alone (no CD, BW, DQ or VL bit), so AVX-512 is read from it.
X86_DISPATCH = (
    ("sse2", ("sse2",)),
    ("ssse3", ("sse2", "sse3", "ssse3")),
    ("sse4.1", ("sse2", "sse3", "ssse3", "sse4_1")),
    ("avx2", ("avx", "avx2")),
    ("avx512", ("avx", "avx2", "avx512f")),
)
X86_SOURCE = (
    "mirror of core/src/x86/cpu.c from IsProcessorFeaturePresent; AVX-512 from "
    "PF_AVX512F alone (Windows reports no CD, BW, DQ or VL bit)"
)
ARM_SOURCE = "mirror of core/src/arm/cpu.c: NEON on aarch64; the MSVC build has no SVE2 path"
MACHINES = {"amd64": "x86_64", "x86_64": "x86_64", "arm64": "aarch64", "aarch64": "aarch64"}

Registry = Callable[[str], "str | None"]
FeatureProbe = Callable[[int], bool]


def windows_machine(raw: str) -> str:
    """The Linux name of a Windows architecture (`AMD64` -> `x86_64`, `ARM64` -> `aarch64`),
    so a Windows report names its references as the Linux image does."""
    return MACHINES.get(raw.lower(), raw.lower())


def registry_value(name: str) -> str | None:
    """One value of the processor's registry key, or None."""
    try:
        import winreg  # Windows only

        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, CPU_REGISTRY_KEY) as key:
            value, _kind = winreg.QueryValueEx(key, name)
    except (ImportError, OSError):
        return None
    return str(value)


def processor_feature(number: int) -> bool:
    """IsProcessorFeaturePresent(number); False where kernel32 is not there."""
    try:
        probe = ctypes.WinDLL("kernel32").IsProcessorFeaturePresent
    except (AttributeError, OSError):
        return False
    probe.argtypes = [ctypes.c_uint32]
    probe.restype = ctypes.c_int
    return bool(probe(number))


def read_cpu(machine: str, registry: Registry = registry_value) -> dict[str, str]:
    """Allow-listed cpuinfo keys from the processor's registry values."""
    found: dict[str, str] = {}
    for value_name, key in REGISTRY_VALUES:
        value = (registry(value_name) or "").strip()
        if value:
            found[key] = value
    match = IDENTIFIER.search(registry("Identifier") or "")
    if match:
        keys = ARM_IDENTIFIER_KEYS if machine == "aarch64" else X86_IDENTIFIER_KEYS
        found.update(zip(keys, match.groups(), strict=True))
    return found


def features(machine: str, present: FeatureProbe = processor_feature) -> list[str]:
    """Names of the instruction-set features Windows reports for this processor."""
    table = ARM_FEATURES if machine == "aarch64" else X86_FEATURES
    return sorted(name for name, number in table.items() if present(number))


def dispatch_flags(machine: str, found: list[str]) -> list[str]:
    """The flags the fork's runtime selects: NEON on aarch64, the x86 levels whose
    features are all present."""
    if machine == "aarch64":
        return ["neon"]
    if machine != "x86_64":
        return []
    have = set(found)
    return [flag for flag, needed in X86_DISPATCH if all(name in have for name in needed)]


def collect_windows_facts(
    registry: Registry = registry_value, present: FeatureProbe = processor_feature
) -> dict[str, Any]:
    """Host facts block of a Windows report, in the key order of the Linux one."""
    machine = windows_machine(platform.machine())
    cpu = read_cpu(machine, registry)
    found = features(machine, present)
    release, version = platform.release(), platform.version()
    return {
        "platform": "windows",
        "machine": machine,
        "kernel_release": version,
        "in_container": False,
        "cpu_model": cpu.get("model name", "unknown"),
        "cpuinfo": {key: cpu[key] for key in sorted(cpu)},
        "cpu_features": found,
        "hwcap": None,
        "hwcap2": None,
        "sve_in_hwcap": False,
        "dispatch_flags": dispatch_flags(machine, found),
        "dispatch_flags_source": ARM_SOURCE if machine == "aarch64" else X86_SOURCE,
        "logical_cores": os.cpu_count(),
        "os_version": f"Windows {release} ({version})",
    }  # fmt: skip
