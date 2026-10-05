# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Tests for the Metal section and the macOS host facts."""

from __future__ import annotations

import json
import sys
from pathlib import Path

_HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(_HERE.parent / "src"))

from vmaf_rc1_tester import hw_facts, hw_metal
from vmaf_rc1_tester.safe_process import CommandResult

FIXTURE = {"id": "f1", "ref": "r", "dis": "d", "width": 16, "height": 16,
           "pixel_format": "420", "bitdepth": 8}  # fmt: skip


def doc(values: list[float], backends: list[tuple[str, str]]) -> str:
    frames = [{"frameNum": i, "metrics": {"psnr": v}} for i, v in enumerate(values)]
    used = [{"extractor": e, "backend": b} for e, b in backends]
    return json.dumps({"frames": frames, "backend_used": "metal", "feature_backends": used})


def runner(text: str | None, code: int = 0):
    def run(argv, **_kw):
        assert argv[argv.index("--backend") + 1] == "metal"
        if text is not None:
            Path(argv[argv.index("--output") + 1]).write_text(text)
        return CommandResult(code, "", "no device" if code else "")

    return run


def run(text, code=0, cpu=None):
    return hw_metal.run_metal_equivalence(
        "vmaf", [FIXTURE], {"f1": cpu or {"psnr": [1.0, 2.0]}},
        timeout_seconds=1, runner=runner(text, code),
    )  # fmt: skip


def test_identical_and_features_split() -> None:
    result = run(doc([1.0, 2.0], [("psnr", "metal"), ("cambi", "cpu")]))
    cell = result["fixtures"][0]
    assert result["status"] == "identical"
    assert cell["extractors_on_metal"] == ["psnr"] and cell["extractors_on_cpu"] == ["cambi"]


def test_difference_is_reported_with_both_values_and_max_diff() -> None:
    result = run(doc([1.0, 2.5], [("psnr", "metal")]))
    detail = result["fixtures"][0]["details"][0]
    assert result["status"] == "differing"
    assert (detail["first_frame"], detail["left"], detail["right"]) == (1, "2", "2.5")
    assert detail["max_abs_diff"] == "0.5"


def test_exit_100_is_no_device_not_error() -> None:
    result = run(None, code=100)
    assert result["status"] == "no_device"
    assert "no_device" not in result["fixtures"][0]


def test_other_failure_and_silent_cpu_fallback_fail_closed() -> None:
    assert run(None, code=1)["status"] == "error"
    fallback = run(doc([1.0, 2.0], [("psnr", "cpu")]))
    assert (
        fallback["status"] == "error" and "silent CPU fallback" in fallback["fixtures"][0]["error"]
    )


def test_failed_metal_run_keeps_the_line_that_names_the_cause() -> None:
    """Report #2118: both 1080p Metal runs ended with a close-time SpEED warning as
    their last stderr line; the cell kept that line and lost the error before it."""
    stderr = (
        'libvmaf WARNING feature "x" cannot be overwritten at index 1\n'
        "problem reading pictures\n"
        "libvmaf WARNING est_params: covariance matrix was singular on 4 of 4 solves\n"
    )

    def failing(argv, **_kw):
        return CommandResult(234, "", stderr)

    result = hw_metal.run_metal_equivalence(
        "vmaf", [FIXTURE], {"f1": {"psnr": [1.0]}}, timeout_seconds=1, runner=failing
    )
    error = result["fixtures"][0]["error"]
    assert result["status"] == "error"
    assert error.startswith("vmaf exited 234: libvmaf WARNING est_params")
    assert "problem reading pictures" in error and "cannot be overwritten" in error


def test_no_cells_is_error_and_mixed_no_device_is_error() -> None:
    assert hw_metal.status_of_cells([]) == "error"
    assert hw_metal.status_of_cells([{"no_device": True, "error": "x"}, {"error": "y"}]) == "error"


def test_darwin_parsers() -> None:
    text = "hw.optional.arm.FEAT_AES: 1\nhw.optional.arm.FEAT_SVE2: 0\nhw.optional.neon: 1\nx: 1"
    assert hw_facts.parse_hw_optional(text) == ["arm.FEAT_AES", "neon"]
    gpu = {"SPDisplaysDataType": [{"sppci_model": "Apple M4", "spdisplays_mtlgpufamilysupport": "m",
                                   "spdisplays_vendor": "Apple", "serial_number": "SECRET"}]}  # fmt: skip
    found = hw_facts.parse_metal_device(json.dumps(gpu))
    assert found == {"sppci_model": "Apple M4", "spdisplays_mtlgpufamilysupport": "m",
                     "spdisplays_vendor": "Apple"}  # fmt: skip
    assert hw_facts.parse_metal_device("not json") is None
    assert hw_facts.parse_metal_device('{"SPDisplaysDataType": []}') is None


def test_collect_darwin_facts_keeps_allow_listed_values_only() -> None:
    def fake(argv, **_kw):
        outputs = {
            "machdep.cpu.brand_string": "Apple M4",
            "hw.model": "Mac16,1",
            "kern.osproductversion": "15.1",
            "kern.osversion": "24B83",
        }
        if argv[0].endswith("system_profiler"):
            return CommandResult(0, '{"SPDisplaysDataType": [{"sppci_model": "Apple M4"}]}', "")
        if argv[1] == "hw.optional":
            return CommandResult(0, "hw.optional.neon: 1\n", "")
        return CommandResult(0, outputs[argv[2]] + "\n", "")

    facts = hw_facts.collect_darwin_facts(fake)
    assert facts["platform"] == "darwin" and facts["cpu_model"] == "Apple M4"
    assert facts["cpuinfo"] == {"machdep.cpu.brand_string": "Apple M4", "hw.model": "Mac16,1"}
    assert facts["os_version"] == "15.1" and facts["cpu_features"] == ["neon"]
    assert facts["metal_device"] == {"sppci_model": "Apple M4"}
    assert set(facts["cpuinfo"]) <= hw_facts.ALLOWED_CPUINFO_KEYS


def test_collect_darwin_facts_survives_missing_tools() -> None:
    def broken(argv, **_kw):
        raise OSError("no such tool")

    facts = hw_facts.collect_darwin_facts(broken)
    assert facts["cpu_model"] == "unknown" and facts["metal_device"] is None
