# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Tests for the report's GPU section: the generic part (hw_gpu.py), the SYCL
backend (hw_sycl.py, hw_l0probe.py), the GPU state rows (hw_rows.py) and the
device test runner (hw_suites.py). Device-free: every runner is a fake."""

from __future__ import annotations

import json
import os
import sys
from pathlib import Path

import pytest

_HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(_HERE.parent / "src"))

from vmaf_rc1_tester import hw_gpu, hw_l0probe, hw_rows, hw_suites, hw_sycl
from vmaf_rc1_tester.safe_process import CommandResult

FIXTURE = {"id": "f1", "ref": "r.yuv", "dis": "d.yuv", "width": 16, "height": 16,
           "pixel_format": "420", "bitdepth": 8}  # fmt: skip
EXACT = {"metrics": ["psnr_y"], "extractor": "psnr_sycl", "options": "", "bound": "0",
         "source": "exact:ADR-1397"}  # fmt: skip
LIBM = {"metrics": ["ciede2000"], "extractor": "ciede_sycl", "options": "",
        "bound": "1.0000000000000001e-09", "source": "libm:ADR-1426"}  # fmt: skip
OPTIONED = {"metrics": ["float_ssim_l"], "extractor": "float_ssim_sycl",
            "options": "enable_lcs=true", "bound": "0", "source": "exact:ADR-1397"}  # fmt: skip
BOUNDS = {"backend": "sycl", "features": {"psnr": EXACT, "ciede": LIBM, "float_ssim_lcs": OPTIONED}}


# ---- hw_l0probe ---------------------------------------------------------------------------


def ip(major: int, minor: int, revision: int) -> int:
    return (major << 22) | (minor << 14) | revision


@pytest.mark.parametrize(
    ("version", "family"),
    [
        ((12, 0, 0), "xe-lp"),  # tgllp
        ((12, 2, 0), "xe-lp"),  # adl-s / rpl-s: the UHD 770
        ((12, 4, 0), "xe-lp"),  # adl-n, last of the range
        ((12, 10, 0), "xe-lp"),  # dg1
        ((12, 56, 5), "xe-hpg"),  # dg2-g11: the A380
        ((12, 60, 7), "xe-hpc"),  # pvc
        ((12, 70, 4), "xe-lpg"),  # mtl-u / arl-s
        ((12, 74, 4), "xe-lpg"),  # arl-h
        ((20, 1, 0), "xe2"),  # bmg-g21: the B580 and the B60
        ((20, 4, 4), "xe2"),  # lnl-m
        ((30, 0, 4), "xe3"),  # ptl-h
        ((12, 5, 0), "unknown"),  # boundary: between Xe-LP and DG1
        ((12, 11, 0), "unknown"),
        ((12, 75, 0), "unknown"),
        ((9, 0, 0), "unknown"),
    ],
)
def test_ip_version_families_follow_ocloc_ids(version, family) -> None:
    value = ip(*version)
    assert hw_l0probe.ip_string(value) == ".".join(str(part) for part in version)
    assert hw_l0probe.family_of(value) == family


def test_device_facts_never_copy_the_uuid() -> None:
    props = hw_l0probe.DeviceProperties()
    props.name, props.vendorId, props.deviceId, props.flags = b"Intel(R) UHD 770", 0x8086, 0x4680, 1
    props.numSlices, props.numSubslicesPerSlice, props.numEUsPerSubslice = 1, 4, 8
    props.uuid[:] = list(range(16))
    compute = hw_l0probe.ComputeProperties()
    compute.numSubGroupSizes, compute.subGroupSizes[:3] = 3, [8, 16, 32]
    facts = hw_l0probe.describe_device(props, compute, ip(12, 2, 0))
    assert facts["family"] == "xe-lp" and facts["eu_count"] == 32 and facts["integrated"]
    assert facts["sub_group_sizes"] == [8, 16, 32] and facts["device_id"] == "0x4680"
    assert not any("uuid" in key for key in facts)
    assert hw_l0probe.describe_device(props, compute, 0)["family"] == "unknown"


def test_probe_without_a_loader_reports_it() -> None:
    result = hw_l0probe.probe("libze_loader_absent.so.9")
    assert result["status"] == "no_loader" and result["devices"] == []


# ---- hw_sycl ------------------------------------------------------------------------------


def fake_node(dri: Path, sysfs: Path, name: str, vendor: str, mode: int = 0o666) -> None:
    node = dri / name
    node.write_text("")
    node.chmod(mode)
    device = sysfs / name / "device"
    device.mkdir(parents=True)
    (device / "vendor").write_text(vendor + "\n")
    (device / "device").write_text("0x4680\n")
    (device.parent / "xe").mkdir()
    (device / "driver").symlink_to(device.parent / "xe")


def test_access_takes_the_intel_render_node(tmp_path: Path) -> None:
    dri, sysfs = tmp_path / "dri", tmp_path / "sys"
    dri.mkdir()
    fake_node(dri, sysfs, "renderD128", "0x10de")
    fake_node(dri, sysfs, "renderD129", "0x8086")
    facts = hw_sycl.access_facts(dri, tmp_path / "dxg", tmp_path / "libdxcore.so", sysfs)
    assert facts["path"] == "drm" and not facts["dxg_present"]
    intel = [n for n in facts["render_nodes"] if n["vendor"] == "0x8086"]
    assert intel[0]["driver"] == "xe" and intel[0]["accessible"]


def test_access_without_intel_node_or_with_only_dxg(tmp_path: Path) -> None:
    dri, sysfs = tmp_path / "dri", tmp_path / "sys"
    dri.mkdir()
    fake_node(dri, sysfs, "renderD128", "0x1002")
    facts = hw_sycl.access_facts(dri, tmp_path / "dxg", tmp_path / "libdxcore.so", sysfs)
    assert facts["path"] == "none"
    assert "no render node of an Intel GPU" in hw_sycl.missing_access_reason(facts)
    (tmp_path / "dxg").write_text("")
    facts = hw_sycl.access_facts(dri, tmp_path / "dxg", tmp_path / "libdxcore.so", sysfs)
    assert facts["path"] == "none" and "-v /usr/lib/wsl" in hw_sycl.missing_access_reason(facts)
    (tmp_path / "libdxcore.so").write_text("")
    facts = hw_sycl.access_facts(dri, tmp_path / "dxg", tmp_path / "libdxcore.so", sysfs)
    assert facts["path"] == "wsl"


def test_reason_names_the_group_of_an_unreadable_node() -> None:
    facts = {"render_nodes": [{"vendor": "0x8086", "accessible": False, "gid": 110}],
             "dxg_present": False, "wsl_libdxcore_present": False}  # fmt: skip
    assert hw_sycl.missing_access_reason(facts).endswith("--group-add 110")
    empty = {"render_nodes": [], "dxg_present": False, "wsl_libdxcore_present": False}
    assert "--device /dev/dri" in hw_sycl.missing_access_reason(empty)


def test_runtime_versions_read_the_listed_installed_packages(tmp_path: Path) -> None:
    status = tmp_path / "status"
    status.write_text(
        "Package: libze1\nStatus: install ok installed\nVersion: 1.34.0\n\n"
        "Package: bash\nStatus: install ok installed\nVersion: 5.2\n\n"
        "Package: intel-igc-core-2\nStatus: deinstall ok config-files\nVersion: 2.0\n"
    )
    assert hw_sycl.runtime_versions(status) == {"libze1": "1.34.0"}
    assert hw_sycl.runtime_versions(tmp_path / "absent") == {}


AUDIT_PASS = (
    "test_kernels_outside_ratchet_use_no_scratch:   audited 127 kernels: 0 use scratch memory,"
    " 0 listed in the ratchet\npass\n"
    "  probes: private array 32768 B, 0/256 wrong; spill 0 B, 0/256 wrong\n"
)


def test_scratch_audit_pass_fail_skip_and_garbage() -> None:
    clean = hw_sycl.parse_scratch_audit(AUDIT_PASS)
    assert clean["status"] == "pass" and clean["row_result"] == "pass"
    assert clean["kernels_audited"] == 127 and clean["scratch_probe"]["private_wrong"] == "0/256"
    spilled = hw_sycl.parse_scratch_audit(
        "  FAIL: Ss2SlotKernel uses scratch memory (private 0 B, spill 192 B) and is not in x\n"
        "  audited 125 kernels: 1 use scratch memory, 0 listed in the ratchet\n"
    )
    assert spilled["status"] == "fail" and spilled["row_result"] == "fail"
    assert spilled["failing_kernels"][0] == {"kernel": "Ss2SlotKernel", "private_bytes": 0,
                                            "spill_bytes": 192}  # fmt: skip
    # A kernel the ratchet lists passes the test but not the row's condition.
    listed = hw_sycl.parse_scratch_audit(
        "audited 9 kernels: 1 use scratch memory, 1 listed in the ratchet"
    )
    assert listed["status"] == "pass" and listed["row_result"] == "fail"
    assert hw_sycl.parse_scratch_audit("  [SKIP] no SYCL GPU to audit (-19)")["status"] == "skip"
    assert hw_sycl.parse_scratch_audit("segfault")["status"] == "error"
    assert hw_sycl.parse_scratch_audit("audited 0 kernels: 0 use scratch memory, 0 listed in the ratchet")["row_result"] == "fail"  # fmt: skip


def test_device_env_pins_level_zero_ordinal() -> None:
    assert hw_sycl.device_env({"index": 1}) == {"ONEAPI_DEVICE_SELECTOR": "level_zero:1"}


# ---- hw_gpu: twins --------------------------------------------------------------------------


def cell(scores: dict, on_device: list[str], on_cpu: list[str] = ()) -> dict:
    return {"fixture": "f1", "scores": scores, "extractors_on_device": on_device,
            "extractors_on_cpu": list(on_cpu), "details": []}  # fmt: skip


def test_feature_verdicts() -> None:
    cpu = {"f1": {"psnr_y": [1.0, 2.0], "ciede2000": [3.0, 4.0]}}
    same = cell({"psnr_y": [1.0, 2.0], "ciede2000": [3.0, 4.0]}, ["psnr_sycl", "ciede_sycl"])
    assert hw_gpu.feature_verdict("psnr", EXACT, [same], cpu)["status"] == "identical"
    libm = cell({"psnr_y": [1.0, 2.0], "ciede2000": [3.0, 4.0 + 5e-10]}, ["ciede_sycl"])
    item = hw_gpu.feature_verdict("ciede", LIBM, [libm], cpu)
    assert item["status"] == "within_bound" and item["differing_values"] == 1
    assert item["first"]["frame"] == 1 and item["first"]["metric"] == "ciede2000"
    wrong = cell({"psnr_y": [1.0, 2.0000000000000004]}, ["psnr_sycl"])  # one ulp is a difference
    assert hw_gpu.feature_verdict("psnr", EXACT, [wrong], cpu)["status"] == "differing"
    far = cell({"ciede2000": [3.0, 4.0 + 2e-9]}, ["ciede_sycl"])
    assert hw_gpu.feature_verdict("ciede", LIBM, [far], cpu)["status"] == "differing"
    absent = cell({"ciede2000": [3.0]}, ["ciede_sycl"])
    assert hw_gpu.feature_verdict("ciede", LIBM, [absent], cpu)["first"]["device"] == "absent"


def test_feature_on_the_cpu_or_with_options_or_absent() -> None:
    cpu = {"f1": {"psnr_y": [1.0]}}
    fell_back = cell({"psnr_y": [1.0]}, ["ciede_sycl"], ["psnr"])
    assert hw_gpu.feature_verdict("psnr", {**EXACT, "extractor": "psnr"}, [fell_back], cpu)[
        "status"] == "not_on_device"  # fmt: skip
    assert (
        hw_gpu.feature_verdict("float_ssim_lcs", OPTIONED, [fell_back], cpu)["status"]
        == "gate_only"
    )
    assert hw_gpu.feature_verdict("psnr", EXACT, [], cpu)["status"] == "not_in_run"


def test_twins_status_counts_unmapped_metrics_and_errors() -> None:
    features = [{"feature": "psnr", "status": "identical", "metrics": ["psnr_y"]}]
    clean = [{"fixture": "f1", "details": []}]
    assert hw_gpu.twins_status(clean, features) == "identical"
    vmaf = [{"fixture": "f1", "details": [{"metric": "vmaf"}]}]
    assert hw_gpu.twins_status(vmaf, features) == "differing"
    assert hw_gpu.twins_status([{"fixture": "f1", "error": "x"}], features) == "error"
    assert hw_gpu.twins_status([], features) == "error"


# ---- hw_gpu: the whole section with a fake backend ----------------------------------------


def vmaf_document(metrics: dict, backend: str) -> str:
    frames = [{"frameNum": 0, "metrics": metrics}]
    feature_backends = [{"extractor": "psnr_sycl" if backend == "sycl" else "psnr",
                         "backend": backend}]  # fmt: skip
    return json.dumps({"frames": frames, "backend_used": backend,
                       "feature_backends": feature_backends})  # fmt: skip


def fake_image(root: Path) -> None:
    (root / "image").mkdir(parents=True)
    (root / "image" / "gpu-twins.json").write_text(
        json.dumps({"backend": "sycl", "features": {"psnr": EXACT}})
    )
    (root / "image" / "rows.json").write_text(json.dumps({"rows": [
        {"id": "T-X-2026-10-03", "part": "Xe-LP", "families": ["xe-lp"],
         "tests": ["test_a"], "audits": ["test_a"], "gate": []},
        {"id": "T-X-2026-10-03", "part": "Xe2", "families": ["xe2"], "tests": ["test_a"]},
    ]}))  # fmt: skip
    (root / "image" / "gpu-tests.json").write_text(json.dumps({
        "tests": [{"name": "test_a", "cmd": "/t/test_a"}],
        "left_out": [{"name": "test_py", "reason": "Python test"}]}))  # fmt: skip


def fake_backend(devices: list[dict]) -> hw_gpu.GpuBackend:
    def discover(_root, _runner):
        found = {"access": {"path": "drm"}, "runtime": {"libze1": "1.34.0"}, "devices": devices}
        if not devices:
            found["reason"] = "no GPU device node is visible: add --device /dev/dri"
        return found

    return hw_gpu.GpuBackend(
        name="sycl", discover=discover, device_env=hw_sycl.device_env,
        audits={"test_a": hw_sycl.parse_scratch_audit}, row_map="rows.json",
    )  # fmt: skip


def fake_runner(device_value: float, seen: list):
    def run(argv, **kwargs):
        seen.append((argv, kwargs.get("environment", {})))
        if "--output" in argv:
            Path(argv[argv.index("--output") + 1]).write_text(
                vmaf_document({"psnr_y": device_value}, "sycl"))  # fmt: skip
            return CommandResult(0, "", "libvmaf INFO SYCL: using device: Fake GPU\n")
        if argv[0] == "/t/test_a":
            return CommandResult(0, "", AUDIT_PASS)
        return CommandResult(1, "", "")  # the gate script: absent in the fake image

    return run


BUDGET = hw_gpu.Budget(fixture=10, gate=10, tests=10)
XE_LP = {"index": 0, "facts": {"name": "Fake UHD", "family": "xe-lp", "ip_version": "12.2.0"}}


def test_section_passes_and_pins_every_run_to_the_device(tmp_path: Path) -> None:
    fake_image(tmp_path)
    seen: list = []
    section = hw_gpu.run_gpu_section(
        tmp_path, fake_backend([XE_LP]), [FIXTURE], {"f1": {"psnr_y": [40.0]}}, BUDGET,
        runner=fake_runner(40.0, seen),
    )  # fmt: skip
    device = section["devices"][0]
    assert device["twins"]["status"] == "identical"
    assert device["device_lines"] == ["libvmaf INFO SYCL: using device: Fake GPU"]
    assert device["audits"]["test_a"]["row_result"] == "pass"
    assert device["device_tests"]["results"] == {"test_a": "pass"}
    assert all(env.get("ONEAPI_DEVICE_SELECTOR") == "level_zero:0" for _, env in seen if env)
    # The fake image has no gate: the device fails on it, and with it the section.
    assert device["gate"]["status"] == "error" and section["status"] == "fail"
    rows = {row["part"]: row["verdict"] for row in device["rows"]["rows"]}
    assert rows == {"Xe-LP": "pass", "Xe2": "not_applicable"}


def test_planted_wrong_cpu_value_fails_closed(tmp_path: Path) -> None:
    fake_image(tmp_path)
    section = hw_gpu.run_gpu_section(
        tmp_path, fake_backend([XE_LP]), [FIXTURE], {"f1": {"psnr_y": [40.000000000000007]}},
        BUDGET, runner=fake_runner(40.0, []),
    )  # fmt: skip
    twins = section["devices"][0]["twins"]
    assert twins["status"] == "differing" and twins["features"][0]["status"] == "differing"
    assert section["devices"][0]["status"] == "fail" and section["status"] == "fail"


def test_no_device_is_no_device_with_the_reason(tmp_path: Path) -> None:
    fake_image(tmp_path)
    section = hw_gpu.run_gpu_section(tmp_path, fake_backend([]), [FIXTURE], {}, BUDGET,
                                     runner=fake_runner(0.0, []))  # fmt: skip
    assert section["status"] == "no_device" and "--device /dev/dri" in section["reason"]
    items = hw_gpu.gpu_not_exercised(section)
    assert items == [("SYCL twins", section["reason"])]


def test_malformed_twin_bounds_are_an_error_not_a_crash(tmp_path: Path) -> None:
    fake_image(tmp_path)
    (tmp_path / "image" / "gpu-twins.json").write_text('{"features": {"psnr": {"bound": "0"}}}')
    section = hw_gpu.run_gpu_section(tmp_path, fake_backend([XE_LP]), [FIXTURE], {}, BUDGET,
                                     runner=fake_runner(0.0, []))  # fmt: skip
    assert section["status"] == "error" and "malformed" in section["reason"]


def test_device_status_and_section_status() -> None:
    good = {"twins": {"status": "within_bound"}, "gate": {"status": "pass"},
            "device_tests": {"status": "pass"}, "audits": {"a": {"status": "pass"}}}  # fmt: skip
    assert hw_gpu.device_status(good) == "pass"
    assert hw_gpu.device_status({**good, "audits": {"a": {"status": "fail"}}}) == "fail"
    assert hw_gpu.device_status({**good, "device_tests": {"status": "fail"}}) == "fail"
    assert hw_gpu.section_status([]) == "no_device"
    assert hw_gpu.section_status([{"status": "pass"}, {"status": "fail"}]) == "fail"


# ---- hw_rows: GPU rows ----------------------------------------------------------------------


def device(family: str, results: dict, audit: str = "pass") -> dict:
    return {"facts": {"family": family}, "device_tests": {"results": results},
            "audits": {"test_a": {"row_result": audit}}, "gate": {"fixtures": []}}  # fmt: skip


ROW = {"id": "T-X-2026-10-03", "part": "p", "families": ["xe-lp"], "tests": ["test_a"],
       "audits": ["test_a"]}  # fmt: skip


def test_gpu_rows_follow_family_tests_and_audits() -> None:
    row_map = {"rows": [ROW]}
    assert (
        hw_rows.evaluate_device_rows(row_map, device("xe-lp", {"test_a": "pass"}))["status"]
        == "pass"
    )
    assert (
        hw_rows.evaluate_device_rows(row_map, device("xe-lp", {"test_a": "fail"}))["status"]
        == "fail"
    )
    skipped = hw_rows.evaluate_device_rows(row_map, device("xe-lp", {"test_a": "skip"}))
    assert skipped["status"] == "not_measured"
    spill = hw_rows.evaluate_device_rows(row_map, device("xe-lp", {"test_a": "pass"}, "fail"))
    assert spill["status"] == "fail"
    other = hw_rows.evaluate_device_rows(row_map, device("xe2", {"test_a": "fail"}))
    assert other["status"] == "not_applicable" and other["counts"]["not_applicable"] == 1
    assert hw_rows.evaluate_device_rows(None, device("xe-lp", {}))["status"] == "not_applicable"


# ---- hw_suites: device tests ----------------------------------------------------------------


def test_device_tests_resolve_placeholders_prepend_paths_and_get_a_scratch_dir(
    tmp_path: Path, monkeypatch
) -> None:
    monkeypatch.setenv("LD_LIBRARY_PATH", "/opt/intel/lib")
    root = tmp_path / "root"
    (root / "image").mkdir(parents=True)
    (root / "build" / "tools").mkdir(parents=True)
    manifest = root / "image" / "gpu-tests.json"
    manifest.write_text(json.dumps({"tests": [
        {"name": "test_sh", "cmd": "/t/x.sh", "args": ["sycl"], "scratch": True, "timeout": 5,
         "env": {"MESON_SOURCE_ROOT": "{root}", "LD_LIBRARY_PATH": "{root}/build/src",
                 "MESON_BUILD_ROOT": "{work}"}},
        {"name": "test_plain", "cmd": "/t/plain"}]}))  # fmt: skip
    calls: list = []

    def run(argv, **kwargs):
        cwd = Path(kwargs["cwd"])
        calls.append((argv, kwargs, (cwd / "tools").is_symlink(), cwd.is_dir()))
        return CommandResult(0 if argv[0] == "/t/x.sh" else 77, "", "")

    result = hw_suites.run_unit_tests(manifest, timeout_seconds=100, runner=run,
                                      environment={"ONEAPI_DEVICE_SELECTOR": "level_zero:0"})  # fmt: skip
    (argv, kwargs, linked, is_dir), (_, plain, plain_linked, plain_dir) = calls
    env = kwargs["environment"]
    assert argv == ["/t/x.sh", "sycl"] and linked and is_dir and kwargs["timeout_seconds"] == 20
    assert env["MESON_SOURCE_ROOT"] == str(root.resolve())
    assert env["LD_LIBRARY_PATH"] == f"{root.resolve()}/build/src{os.pathsep}/opt/intel/lib"
    assert (
        env["MESON_BUILD_ROOT"] == kwargs["cwd"] and env["ONEAPI_DEVICE_SELECTOR"] == "level_zero:0"
    )
    assert plain_dir and not plain_linked and plain["timeout_seconds"] == 100
    assert result["results"] == {"test_sh": "pass", "test_plain": "skip"}
    assert result["skipped_tests"] == ["test_plain"] and result["status"] == "pass"
