#!/usr/bin/env python3
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Every vmaf_v1.0.16* model runs wholly on a GPU backend and returns the CPU's bits.

Usage::

    test_gpu_v1_models_no_fallback.py --self-test
    test_gpu_v1_models_no_fallback.py --backend cuda|sycl|hip

With ``--backend``, every built-in ``vmaf_v1.0.16*`` model (one per JSON file
under ``model/vmaf_v1.0.16*/``) and the CLI's default (a run without
``--model``) score each fixture twice with the ``vmaf`` CLI of this build, at
``--precision max``: once with ``--backend cpu`` and once with the device
backend. The fixtures are the 576x324 `src01_hrc00` / `src01_hrc01` pair at 8, 10
and 12 bits 4:2:0 and 10 bits 4:2:2, and the first 16 frames of the 3840x2160
pair in `testdata/bbb`. A run
fails when the device run lists an extractor on any other backend in
``feature_backends`` (a CPU fallback), when its ``backend_used`` is not the
device, and on any difference in a per-frame, pooled or aggregate metric,
including a key that only one side has.

Every device run also scores one known fallback: ``--feature
float_adm=adm_csf_mode=1``, an option value every ``float_adm`` twin
implements only at its default (``VMAF_OPT_FLAG_DEFAULT_ONLY``, ADR-1316). The
check must name ``float_adm`` as a fallback there, so the test fails when the
check stops seeing fallbacks.

``--self-test`` runs the comparison on synthetic receipts without a device.

Exit 77 (skip) when this build was configured without the backend, the CLI
refuses the backend (exit 100: no device), or a fixture is absent; the reason
is printed. Device runs take the device's lock under ``VMAFX_LOCK_DIR``
(default ``~/.cache/vmafx-locks``) when that directory exists, with a 300 s
limit inside the lock.
"""

from __future__ import annotations

import argparse
import dataclasses
import json
import math
import os
import shutil
import sys
import tempfile
from collections.abc import Sequence
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from scripts.lib.safe_subprocess import run as run_command  # noqa: E402

YUV = ROOT / "python" / "test" / "resource" / "yuv"
UHD = ROOT / "testdata" / "bbb"
MODEL_GLOB = "vmaf_v1.0.16*/vmaf_v1.0.16*.json"
DEFAULT_LABEL = "default"
SKIP = 77
EXIT_BACKEND_REFUSED = 100
DEVICE_RUN_LIMIT_S = 300
WAIT_LIMIT_S = 3600
BACKEND_OPTION = {"cuda": "enable_cuda", "sycl": "enable_sycl", "hip": "enable_hipcc"}
LOCK_FILE = {"cuda": "cuda-4090.lock", "sycl": "sycl-a380.lock", "hip": "hip-gfx1036.lock"}
KNOWN_FALLBACK_FEATURE = "float_adm=adm_csf_mode=1"
KNOWN_FALLBACK_EXTRACTOR = "float_adm"
# The four SDR and the four HFR models of the upstream v1.0.16 release.
V1_MODEL_COUNT = 8


@dataclasses.dataclass(frozen=True)
class Fixture:
    """One reference / distorted pair and the geometry the CLI needs."""

    name: str
    ref: Path
    dis: Path
    width: int
    height: int
    pix_fmt: str
    bitdepth: int
    frame_cnt: int | None = None


def src01_fixture(name: str, suffix: str, pix_fmt: str, bitdepth: int) -> Fixture:
    """The 576x324 src01 pair stored as `src01_hrc0N_576x324<suffix>`."""
    return Fixture(
        name=name,
        ref=YUV / f"src01_hrc00_576x324{suffix}",
        dis=YUV / f"src01_hrc01_576x324{suffix}",
        width=576,
        height=324,
        pix_fmt=pix_fmt,
        bitdepth=bitdepth,
    )


FIXTURES: tuple[Fixture, ...] = (
    src01_fixture("src01-420-8", ".yuv", "420", 8),
    src01_fixture("src01-420-10", ".yuv420p10le.yuv", "420", 10),
    src01_fixture("src01-420-12", ".yuv420p12le.yuv", "420", 12),
    src01_fixture("src01-422-10", ".yuv422p10le.yuv", "422", 10),
    Fixture(
        name="uhd-3840x2160-420-8",
        ref=UHD / "ref_3840x2160_200f.yuv",
        dis=UHD / "dis_3840x2160_200f.yuv",
        width=3840,
        height=2160,
        pix_fmt="420",
        bitdepth=8,
        frame_cnt=16,
    ),
)


# ---------------------------------------------------------------------------
# The comparison: pure functions over the CLI's JSON receipts.
# ---------------------------------------------------------------------------


def fallback_problems(receipt: dict[str, Any], backend: str) -> list[str]:
    """Why `receipt` is not a run done wholly on `backend`; empty when it is."""
    problems: list[str] = []
    used = receipt.get("backend_used")
    if used != backend:
        problems.append(f"backend_used is {used!r}, not {backend!r}")
    entries = receipt.get("feature_backends")
    if not isinstance(entries, list) or not entries:
        return [*problems, "feature_backends is missing or empty"]
    for entry in entries:
        on = entry.get("backend") if isinstance(entry, dict) else None
        if on != backend:
            name = entry.get("extractor") if isinstance(entry, dict) else entry
            problems.append(f"extractor {name!r} ran on {on!r}")
    return problems


def same_value(a: Any, b: Any) -> bool:
    """Equal values; two NaNs are the same value."""
    if isinstance(a, float) and isinstance(b, float) and math.isnan(a) and math.isnan(b):
        return True
    return type(a) is type(b) and a == b


def flat_values(where: str, value: Any) -> dict[str, Any]:
    """`value` as dotted keys, two mapping levels deep (pooled: metric -> statistic)."""
    if not isinstance(value, dict):
        return {where: value}
    flat: dict[str, Any] = {}
    for key, inner in value.items():
        if isinstance(inner, dict):
            flat |= {f"{where}.{key}.{stat}": leaf for stat, leaf in inner.items()}
        else:
            flat[f"{where}.{key}"] = inner
    return flat


def mapping_problems(where: str, cpu: Any, dev: Any) -> list[str]:
    """Differences between two metric mappings: keys on one side only, unequal values."""
    cpu_flat, dev_flat = flat_values(where, cpu), flat_values(where, dev)
    problems = [
        f"{key}: only the {'cpu' if key in cpu_flat else 'device'} run has it"
        for key in sorted(set(cpu_flat) ^ set(dev_flat))
    ]
    for key in sorted(set(cpu_flat) & set(dev_flat)):
        if not same_value(cpu_flat[key], dev_flat[key]):
            problems.append(f"{key}: cpu {cpu_flat[key]!r} != device {dev_flat[key]!r}")
    return problems


def score_problems(cpu: dict[str, Any], dev: dict[str, Any]) -> list[str]:
    """Every per-frame, pooled or aggregate value the two runs do not share."""
    cpu_frames = cpu.get("frames") or []
    dev_frames = dev.get("frames") or []
    if not cpu_frames or len(cpu_frames) != len(dev_frames):
        return [f"frame count: cpu {len(cpu_frames)}, device {len(dev_frames)}"]
    problems: list[str] = []
    for index, (a, b) in enumerate(zip(cpu_frames, dev_frames, strict=True)):
        problems += mapping_problems(f"frames[{index}]", a.get("metrics"), b.get("metrics"))
    for section in ("pooled_metrics", "aggregate_metrics"):
        problems += mapping_problems(section, cpu.get(section), dev.get(section))
    return problems


def run_problems(cpu: dict[str, Any], dev: dict[str, Any], backend: str) -> list[str]:
    """Fallbacks of the device run, then its score differences from the CPU run."""
    return fallback_problems(dev, backend) + score_problems(cpu, dev)


# ---------------------------------------------------------------------------
# Running the CLI.
# ---------------------------------------------------------------------------


@dataclasses.dataclass(frozen=True)
class Runner:
    """The CLI of this build, and the lock wrapper of the device under test."""

    vmaf: Path
    backend: str
    workdir: Path
    prefix: tuple[str, ...]

    def argv(self, fixture: Fixture, model: str, backend: str, out: Path) -> list[str]:
        args = [str(self.vmaf), "-r", str(fixture.ref), "-d", str(fixture.dis)]
        args += ["-w", str(fixture.width), "-h", str(fixture.height)]
        args += ["-p", fixture.pix_fmt, "-b", str(fixture.bitdepth)]
        if fixture.frame_cnt is not None:
            args += ["--frame_cnt", str(fixture.frame_cnt)]
        if model != DEFAULT_LABEL:
            args += ["--model", f"version={model}"]
        args += ["--precision", "max", "--json", "-o", str(out), "--backend", backend]
        return args

    def run(self, argv: list[str], *, on_device: bool) -> tuple[int, str]:
        full = [*self.prefix, *argv] if on_device else argv
        proc = run_command(
            full,
            allowed_executables=(full[0],),
            capture_output=True,
            text=True,
            check=False,
            timeout_seconds=WAIT_LIMIT_S if on_device else DEVICE_RUN_LIMIT_S,
            max_output_bytes=4 * 1_048_576,
        )
        return proc.returncode, (proc.stderr or proc.stdout)


class Refused(Exception):
    """The CLI refused the device backend: nothing on this host to compare."""


def score(runner: Runner, fixture: Fixture, model: str, extra: Sequence[str] = ()) -> list[str]:
    """Problems of one model on one fixture, CPU run against device run."""
    receipts: dict[str, dict[str, Any]] = {}
    for backend in ("cpu", runner.backend):
        out = runner.workdir / f"{fixture.name}_{model}_{backend}.json"
        argv = [*runner.argv(fixture, model, backend, out), *extra]
        code, err = runner.run(argv, on_device=backend != "cpu")
        if code == EXIT_BACKEND_REFUSED and backend != "cpu":
            raise Refused(err.strip())
        if code != 0:
            return [f"{backend} run exited {code}: {err.strip()[-400:]}"]
        receipts[backend] = json.loads(out.read_text(encoding="utf-8"))
    return run_problems(receipts["cpu"], receipts[runner.backend], runner.backend)


def known_fallback_seen(runner: Runner) -> str | None:
    """None when the check names the planted fallback, else why it does not."""
    fixture = FIXTURES[0]
    problems = score(runner, fixture, DEFAULT_LABEL, ("--feature", KNOWN_FALLBACK_FEATURE))
    named = [p for p in problems if f"extractor {KNOWN_FALLBACK_EXTRACTOR!r} ran on 'cpu'" in p]
    if named:
        return None
    return f"--feature {KNOWN_FALLBACK_FEATURE} was not reported as a fallback: {problems}"


def model_versions() -> list[str]:
    """The built-in vmaf_v1.0.16* versions, one per model JSON in the tree."""
    return sorted(path.stem for path in (ROOT / "model").glob(MODEL_GLOB))


def lock_prefix(backend: str) -> tuple[str, ...]:
    """`flock <lock> timeout 300` when the lock directory exists, else `timeout 300`."""
    lock_dir = Path(os.environ.get("VMAFX_LOCK_DIR", Path.home() / ".cache" / "vmafx-locks"))
    flock = shutil.which("flock")
    timeout = shutil.which("timeout")
    limit = (timeout, str(DEVICE_RUN_LIMIT_S)) if timeout else ()
    if flock and lock_dir.is_dir():
        return (flock, str(lock_dir / LOCK_FILE[backend]), *limit)
    return limit


# ---------------------------------------------------------------------------
# Skip decisions, made before any device is touched.
# ---------------------------------------------------------------------------


def find_cli() -> Path | None:
    """The CLI Meson passes in VMAF_CLI, or `build/tools/vmaf`."""
    configured = os.environ.get("VMAF_CLI")
    binary = Path(configured) if configured else ROOT / "build" / "tools" / "vmaf"
    return binary if binary.is_file() else None


def build_option(build_dir: Path, name: str) -> bool | None:
    """A boolean Meson option of `build_dir`, or None when Meson does not say."""
    options = build_dir / "meson-info" / "intro-buildoptions.json"
    try:
        entries = json.loads(options.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None
    for entry in entries if isinstance(entries, list) else []:
        if isinstance(entry, dict) and entry.get("name") == name:
            value = entry.get("value")
            return value if isinstance(value, bool) else None
    return None


def skip_reason(cli: Path | None, backend: str) -> str | None:
    """Why there is nothing to compare on this build and host, or None."""
    if cli is None:
        return "vmaf CLI not found (VMAF_CLI)"
    if build_option(cli.parent.parent, BACKEND_OPTION[backend]) is False:
        return f"this build was configured without {BACKEND_OPTION[backend]}"
    missing = [str(p) for f in FIXTURES for p in (f.ref, f.dis) if not p.is_file()]
    if missing:
        return f"fixtures not found: {', '.join(missing)}"
    return None


def device_main(backend: str) -> int:
    """Score every model on every fixture on `backend`; 0 when all are the CPU's."""
    cli = find_cli()
    reason = skip_reason(cli, backend)
    if reason is not None or cli is None:
        sys.stderr.write(f"{reason}; skipping ({SKIP})\n")
        return SKIP
    versions = model_versions()
    if not versions:
        sys.stderr.write(f"no model matches model/{MODEL_GLOB}\n")
        return 1
    models = [*versions, DEFAULT_LABEL]
    with tempfile.TemporaryDirectory(prefix=f"v1-models-{backend}-") as tmp:
        runner = Runner(cli, backend, Path(tmp), lock_prefix(backend))
        try:
            return report(runner, models)
        except Refused as refused:
            sys.stderr.write(f"--backend {backend} refused: {refused}; skipping ({SKIP})\n")
            return SKIP


def report(runner: Runner, models: list[str]) -> int:
    """Run every (model, fixture) pair and the known fallback; print the table."""
    failed = 0
    for model in models:
        for fixture in FIXTURES:
            problems = score(runner, fixture, model)
            verdict = "ok" if not problems else f"FAIL ({len(problems)})"
            print(f"{runner.backend:<5} {model:<28} {fixture.name:<22} {verdict}")
            for problem in problems[:20]:
                print(f"    {problem}")
            failed += bool(problems)
    missed = known_fallback_seen(runner)
    print(f"{runner.backend:<5} known fallback {KNOWN_FALLBACK_FEATURE}: {missed or 'reported'}")
    total = len(models) * len(FIXTURES)
    print(f"{runner.backend}: {total - failed} of {total} runs wholly on the device and equal")
    return 1 if failed or missed else 0


# ---------------------------------------------------------------------------
# Device-free self-test of the comparison.
# ---------------------------------------------------------------------------


def receipt(backend: str, entries: list[tuple[str, str]], value: float) -> dict[str, Any]:
    """A synthetic CLI receipt: one frame, one pooled metric."""
    return {
        "backend_used": backend,
        "feature_backends": [{"extractor": e, "backend": b} for e, b in entries],
        "frames": [{"frameNum": 0, "metrics": {"vmaf": value, "adm2": 0.5}}],
        "pooled_metrics": {"vmaf": {"mean": value}},
        "aggregate_metrics": {},
    }


def self_test_cases() -> list[tuple[str, bool]]:
    """(case, passed) pairs: positive, negative and boundary receipts."""
    cpu = receipt("cpu", [("adm", "cpu")], 90.0)
    on_dev = receipt("cuda", [("adm_cuda", "cuda"), ("motion_cuda", "cuda")], 90.0)
    fell_back = receipt("cuda", [("adm_cuda", "cuda"), ("float_adm", "cpu")], 90.0)
    one_ulp = receipt("cuda", [("adm_cuda", "cuda")], math.nextafter(90.0, 100.0))
    extra_key = receipt("cuda", [("adm_cuda", "cuda")], 90.0)
    extra_key["frames"][0]["metrics"]["motion2"] = 0.0
    no_entries = receipt("cuda", [], 90.0)
    nan_cpu = receipt("cpu", [], math.nan)
    nan_dev = receipt("cuda", [("adm_cuda", "cuda")], math.nan)
    short = receipt("cuda", [("adm_cuda", "cuda")], 90.0) | {"frames": []}
    return [
        ("identical device run passes", run_problems(cpu, on_dev, "cuda") == []),
        (
            "cpu entry is named",
            run_problems(cpu, fell_back, "cuda") == ["extractor 'float_adm' ran on 'cpu'"],
        ),
        (
            "one ulp is a difference",
            [p.split(":")[0] for p in run_problems(cpu, one_ulp, "cuda")]
            == ["frames[0].vmaf", "pooled_metrics.vmaf.mean"],
        ),
        (
            "key on one side only",
            run_problems(cpu, extra_key, "cuda")
            == ["frames[0].motion2: only the device run has it"],
        ),
        (
            "empty feature_backends fails",
            run_problems(cpu, no_entries, "cuda") == ["feature_backends is missing or empty"],
        ),
        (
            "wrong backend_used fails",
            fallback_problems(on_dev, "hip")[:1] == ["backend_used is 'cuda', not 'hip'"],
        ),
        ("NaN equals NaN", run_problems(nan_cpu, nan_dev, "cuda") == []),
        (
            "frame count differs",
            run_problems(cpu, short, "cuda") == ["frame count: cpu 1, device 0"],
        ),
        ("int is not float", not same_value(1, 1.0)),
    ]


def self_test() -> int:
    """0 when every synthetic case gives the expected verdict."""
    failed = [name for name, passed in self_test_cases() if not passed]
    for name in failed:
        print(f"self-test FAIL: {name}")
    if len(model_versions()) != V1_MODEL_COUNT:
        failed.append(f"model/{MODEL_GLOB} names {len(model_versions())} models")
        print(f"self-test FAIL: {failed[-1]}")
    print(f"self-test: {len(self_test_cases()) + 1 - len(failed)} passed, {len(failed)} failed")
    return 1 if failed else 0


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--backend", choices=sorted(BACKEND_OPTION))
    mode.add_argument("--self-test", action="store_true")
    args = parser.parse_args(argv)
    return self_test() if args.self_test else device_main(args.backend)


if __name__ == "__main__":
    sys.exit(main())
