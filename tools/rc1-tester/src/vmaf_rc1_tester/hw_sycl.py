# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""The SYCL backend of the GPU section (the Intel GPU tester image).

How the image reaches an Intel GPU, recorded as facts and as one `path`:

- `drm`: a Linux render node (`/dev/dri/renderD*`) of PCI vendor 0x8086 that this
  process can open; the kernel driver (`i915` or `xe`) is read from sysfs;
- `wsl`: WSL2's paravirtualised GPU, `/dev/dxg`, with the host driver's
  `/usr/lib/wsl/lib/libdxcore.so` mounted (the compute runtime opens that path);
- `windows`: the Windows SYCL zip (ADR-1566), whose Level Zero loader
  (`tests/ze_loader.dll`, built from LEVEL_ZERO_VERSION) finds the Intel graphics
  driver's Level Zero GPU driver;
- `none`: neither, with the reason and the `docker run` option that is missing
  (on Windows: the zip's loader is missing).

The devices are the Level Zero GPUs (hw_l0probe.py, its own bounded process), in
the order `ONEAPI_DEVICE_SELECTOR=level_zero:<n>` selects; every run of a device
is pinned with that variable. The audit is test_sycl_kernel_scratch's: kernels
that use private memory or spill registers (ADR-1395).
"""

from __future__ import annotations

import json
import os
import re
import stat
import sys
from collections.abc import Mapping
from pathlib import Path
from typing import Any

from .hw_equiv import Runner
from .hw_gpu import GpuBackend

INTEL_VENDOR = "0x8086"
WSL_LIBDXCORE = Path("/usr/lib/wsl/lib/libdxcore.so")
DXG = Path("/dev/dxg")
DRI = Path("/dev/dri")
SYSFS_DRM = Path("/sys/class/drm")
DPKG_STATUS = Path("/var/lib/dpkg/status")
WINDOWS_LOADER = "tests/ze_loader.dll"  # relative to the zip root; its programs load it too
SYSTEM32 = Path(os.environ.get("SYSTEMROOT", r"C:\Windows")) / "System32"
DRIVER_HINT = ("install or update the Intel graphics driver for Arc, Iris Xe or UHD Graphics, "
               "which provides the Level Zero GPU driver")  # fmt: skip
PROBE_TIMEOUT_SECONDS = 120.0
# The packages whose versions say which runtime ran: SYCL runtime, Level Zero
# loader, compute runtime, IGC, gmmlib.
RUNTIME_PACKAGES = (
    "intel-oneapi-runtime-dpcpp-sycl-core",
    "intel-oneapi-umf-1.1",
    "libze1",
    "libze-intel-gpu1",
    "intel-opencl-icd",
    "intel-igc-core-2",
    "libigdgmm12",
)
AUDITED = re.compile(
    r"audited (\d+) kernels: (\d+) use scratch memory, (\d+) listed in the ratchet"
)
FAILED_KERNEL = re.compile(r"FAIL: (.+?) uses scratch memory \(private (\d+) B, spill (\d+) B\)")
PROBES = re.compile(
    r"probes: private array (\d+) B, (\d+)/(\d+) wrong; spill (\(not run\) )?(\d+) B, (\d+)/(\d+) wrong"
)
MAX_LISTED = 40


def node_facts(node: Path, sysfs: Path = SYSFS_DRM) -> dict[str, Any]:
    """One render node: PCI vendor and device, kernel driver, group, and whether
    this process can open it for reading and writing."""
    facts: dict[str, Any] = {"node": node.name}
    device = sysfs / node.name / "device"
    for key in ("vendor", "device"):
        try:
            facts[key] = (device / key).read_text(encoding="ascii").strip()
        except OSError:
            facts[key] = "unknown"
    try:
        facts["driver"] = (device / "driver").resolve(strict=True).name
    except OSError:
        facts["driver"] = "unknown"
    try:
        info = node.stat()
    except OSError:
        return {**facts, "gid": None, "accessible": False}
    facts["gid"] = info.st_gid
    facts["mode"] = oct(stat.S_IMODE(info.st_mode))
    facts["accessible"] = os.access(node, os.R_OK | os.W_OK)
    return facts


def access_facts(
    dri: Path = DRI, dxg: Path = DXG, libdxcore: Path = WSL_LIBDXCORE, sysfs: Path = SYSFS_DRM
) -> dict[str, Any]:
    """How the container can reach a GPU, and the path a run takes."""
    nodes = [node_facts(node, sysfs) for node in sorted(dri.glob("renderD*"))]
    intel = [node for node in nodes if node["vendor"] == INTEL_VENDOR]
    facts: dict[str, Any] = {
        "render_nodes": nodes,
        "dxg_present": dxg.exists(),
        "dxg_accessible": dxg.exists() and os.access(dxg, os.R_OK | os.W_OK),
        "wsl_libdxcore_present": libdxcore.is_file(),
        "groups": sorted(os.getgroups()),
    }
    if facts["dxg_accessible"] and facts["wsl_libdxcore_present"]:
        facts["path"] = "wsl"
    elif any(node["accessible"] for node in intel):
        facts["path"] = "drm"
    else:
        facts["path"] = "none"
    return facts


def missing_access_reason(facts: Mapping[str, Any]) -> str:
    """Why no Intel GPU is reachable, with the `docker run` option that fixes it."""
    intel = [n for n in facts["render_nodes"] if n["vendor"] == INTEL_VENDOR]
    if intel and not any(n["accessible"] for n in intel):
        gids = sorted({str(n["gid"]) for n in intel if n["gid"] is not None})
        return ("the Intel render node is not readable by this container's user: add "
                + " ".join(f"--group-add {gid}" for gid in gids))  # fmt: skip
    if facts["dxg_present"] and not facts["wsl_libdxcore_present"]:
        return "/dev/dxg is present but /usr/lib/wsl is not mounted: add -v /usr/lib/wsl:/usr/lib/wsl:ro"
    if facts["dxg_present"]:
        return "/dev/dxg is not readable and writable by this container's user"
    if facts["render_nodes"]:
        return "no render node of an Intel GPU (PCI vendor 0x8086) is visible"
    return ("no GPU device node is visible: add --device /dev/dri (Linux) or "
            "--device /dev/dxg -v /usr/lib/wsl:/usr/lib/wsl:ro (WSL2)")  # fmt: skip


def windows_access_facts(root: Path, system32: Path = SYSTEM32) -> dict[str, Any]:
    """The Windows zip reaches an Intel GPU through its own Level Zero loader, which
    finds the graphics driver's Level Zero driver; System32's loader (installed by the
    driver) is recorded, not used."""
    present = (root / WINDOWS_LOADER).is_file()
    return {"loader": WINDOWS_LOADER, "loader_present": present,
            "system32_ze_loader_present": (system32 / "ze_loader.dll").is_file(),
            "path": "windows" if present else "none"}  # fmt: skip


def windows_reason(facts: Mapping[str, Any], probe: Mapping[str, Any]) -> str:
    """Why the Windows zip reaches no Intel GPU."""
    if not facts["loader_present"]:
        return f"the zip's Level Zero loader {WINDOWS_LOADER} is missing: unpack the whole zip"
    status, error = probe.get("status"), probe.get("error", "")
    if status in ("no_loader", "error"):
        return f"the zip's Level Zero loader did not run ({status}: {error})"
    return f"Level Zero finds no Intel GPU ({status}{': ' + error if error else ''}): {DRIVER_HINT}"


def recorded_runtime(root: Path) -> dict[str, str]:
    """image/gpu-runtime.json of the Windows zip: the oneAPI and Level Zero loader
    versions its build used (no package database on Windows)."""
    try:
        document = json.loads((root / "image" / "gpu-runtime.json").read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {}
    return (
        {str(k): str(v) for k, v in sorted(document.items())} if isinstance(document, dict) else {}
    )


def runtime_versions(status_file: Path = DPKG_STATUS) -> dict[str, str]:
    """Installed versions of the runtime packages, from the dpkg database."""
    try:
        text = status_file.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return {}
    found: dict[str, str] = {}
    for block in text.split("\n\n"):
        fields = dict(line.split(": ", 1) for line in block.splitlines() if ": " in line)
        name = fields.get("Package")
        if name in RUNTIME_PACKAGES and "installed" in fields.get("Status", ""):
            found[name] = fields.get("Version", "unknown")
    return dict(sorted(found.items()))


def probe_devices(runner: Runner, loader: Path | None = None) -> dict[str, Any]:
    """The Level Zero probe in its own process; `error` when it does not finish.
    `loader` is the Level Zero loader to open (the Windows zip's own), else the
    system's."""
    src = str(Path(__file__).resolve().parents[1])
    env = {**os.environ, "PYTHONPATH": src}
    if loader is not None:
        env["VMAFX_ZE_LOADER"] = str(loader)
    try:
        result = runner(
            [sys.executable, "-B", "-m", "vmaf_rc1_tester.hw_l0probe"], environment=env,
            timeout_seconds=PROBE_TIMEOUT_SECONDS, max_output_bytes=262_144,
        )  # fmt: skip
        document = json.loads(result.stdout)
    except (TimeoutError, RuntimeError, ValueError, OSError) as error:
        return {"status": "error", "error": str(error)[:200], "devices": []}
    return document if isinstance(document, dict) else {"status": "error", "devices": []}


def discover_windows(root: Path, runner: Runner) -> dict[str, Any]:
    """The Windows zip: its own loader, its recorded runtime versions."""
    facts = windows_access_facts(root)
    probe = probe_devices(runner, root / WINDOWS_LOADER) if facts["loader_present"] else {}
    facts["level_zero"] = {k: v for k, v in probe.items() if k != "devices"}
    devices = [{"index": d["index"], "facts": d} for d in probe.get("devices", [])]
    found: dict[str, Any] = {"access": facts, "runtime": recorded_runtime(root), "devices": devices}
    if not devices:
        found["reason"] = windows_reason(facts, probe)
    return found


def windows_host() -> bool:
    return os.name == "nt"


def discover(root: Path, runner: Runner) -> dict[str, Any]:
    """Access facts, runtime versions and the Level Zero GPUs of this host."""
    if windows_host():
        return discover_windows(root, runner)
    facts = access_facts()
    probe = probe_devices(runner)
    facts["level_zero"] = {k: v for k, v in probe.items() if k != "devices"}
    devices = [{"index": d["index"], "facts": d} for d in probe.get("devices", [])]
    found: dict[str, Any] = {"access": facts, "runtime": runtime_versions(), "devices": devices}
    if not devices:
        reason = missing_access_reason(facts)
        if facts["path"] != "none":
            reason = f"Level Zero reports no GPU ({probe.get('status')}): {probe.get('error', '')}"
        found["reason"] = reason.strip().rstrip(":")
    return found


def device_env(device: Mapping[str, Any]) -> dict[str, str]:
    """Pins every run to one Level Zero GPU."""
    return {"ONEAPI_DEVICE_SELECTOR": f"level_zero:{int(device['index'])}"}


def parse_scratch_audit(output: str) -> dict[str, Any]:
    """test_sycl_kernel_scratch's findings. `status` is the test's own (a kernel in
    the ratchet list may use scratch memory); `row_result` is the state row's
    stricter condition: no kernel in scratch memory at all."""
    if "[SKIP] no SYCL GPU to audit" in output:
        return {"status": "skip", "row_result": "skip"}
    audited = AUDITED.search(output)
    if audited is None:
        return {"status": "error", "row_result": "not_run", "reason": "no audit line in the output"}
    kernels, in_scratch, listed = (int(group) for group in audited.groups())
    failing = [{"kernel": m.group(1)[:200], "private_bytes": int(m.group(2)),
                "spill_bytes": int(m.group(3))} for m in FAILED_KERNEL.finditer(output)]  # fmt: skip
    result: dict[str, Any] = {
        "status": "fail" if failing else "pass",
        "row_result": "pass" if kernels > 0 and in_scratch == 0 else "fail",
        "kernels_audited": kernels,
        "kernels_in_scratch": in_scratch,
        "ratchet_entries": listed,
        "failing_kernels": failing[:MAX_LISTED],
    }
    probe = PROBES.search(output)
    if probe is not None:
        result["scratch_probe"] = {
            "private_bytes": int(probe.group(1)),
            "private_wrong": f"{probe.group(2)}/{probe.group(3)}",
            "spill_ran": probe.group(4) is None,
            "spill_bytes": int(probe.group(5)),
            "spill_wrong": f"{probe.group(6)}/{probe.group(7)}",
        }
    return result


SYCL = GpuBackend(
    name="sycl",
    discover=discover,
    device_env=device_env,
    audits={"test_sycl_kernel_scratch": parse_scratch_audit},
    row_map="sycl-rows.json",
)
