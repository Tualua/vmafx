#!/usr/bin/env python3
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Bit-depth x chroma-layout matrix of every exact GPU twin, `==` against the CPU.

Every (feature, backend) pair declared exact in ``scripts/ci/exact_twins.d/``
is run on generated fixtures at 8, 10, 12 and 16 bits and in 4:2:0, 4:2:2 and
4:4:4, once with ``--backend cpu`` and once on the device, both at
``--precision max``. A cell passes when both runs have the same frames, the
same output keys and the same bits in every value; there is no tolerance.

The fixtures are written by this script from integer arithmetic only, so they
are the same bytes on every host. They are 357x353 (odd, not a multiple of 16
or 64, so every plane row ends in a partial tile and a device pitch differs
from the width) and four frames long. Each plane holds a moving ramp, blocks
inverted by plane, hashed noise, and runs clipped to 0 and to the depth's
maximum; the distorted frames add their own noise and a shifted window.

A depth the CPU extractor refuses is ``n/a`` (only 16 bits may be refused);
at 8, 10 and 12 bits a CPU failure is an error. Usage::

    exact_twin_matrix.py --vmaf-binary build/tools/vmaf --backends cuda \\
        [--depths 8 10 12 16] [--layouts 420 422 444] [--features ...] \\
        [--json-out m.json] [--md-out m.md] \\
        [--record docs/development/exact-twin-matrix.md --recorded-on TEXT]

``--record`` replaces each run backend's table in the page (between its
``exact-twin-matrix:<backend>`` markers) and needs a full run: every exact twin
of the backend at every depth and layout. ``recorded_problems()`` is the check
the device-free contract test runs on that page: every exact twin of a device
backend must have a recorded row with every cell equal, or ``n/a`` at 16 bits.

Device runs take ``<lock-dir>/<backend lock>`` (default ``~/.cache/vmafx-locks``)
with a 300 s limit inside the lock when the directory exists. Exit 0 when every
cell passes or is ``n/a``, 1 otherwise, 2 on a usage error, 77 when the CLI
refuses a backend (no device).
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
from array import array
from collections.abc import Iterable, Sequence
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from scripts.ci.cross_backend_calibration import EXACT_TWINS, metric_delta
from scripts.ci.cross_backend_parity_gate import build_command
from scripts.lib.safe_subprocess import run as run_command

WIDTH = 357
HEIGHT = 353
FRAMES = 4
DEPTHS = (8, 10, 12, 16)
LAYOUTS = ("420", "422", "444")
# A CPU extractor may refuse this depth; at every other depth a refusal is an error.
OPTIONAL_DEPTH = 16
SKIP = 77
EXIT_BACKEND_REFUSED = 100
DEVICE_RUN_LIMIT_S = 300
WAIT_LIMIT_S = 3600
LOCK_FILE = {"cuda": "cuda-4090.lock", "sycl": "sycl-a380.lock", "hip": "hip-gfx1036.lock"}
DEVICE_BACKENDS = tuple(sorted(LOCK_FILE))
MASK32 = 0xFFFFFFFF
BYTE_DEPTH = 8
# The window the distorted picture moves: x0, x1, y0, y1.
MOVED_WINDOW = (61, 102, 37, 66)
NOTE_PROBLEMS = 4
MIN_ROW_PARTS = 3
STATUS_PASS = "="  # noqa: S105 - a table mark, not a credential
STATUS_FAIL = "FAIL"
STATUS_ERROR = "ERROR"
STATUS_NA = "n/a"


# ---------------------------------------------------------------------------
# Fixtures: integer arithmetic only, little-endian 16-bit words above 8 bits.
# ---------------------------------------------------------------------------


def plane_size(width: int, height: int, layout: str, plane: int) -> tuple[int, int]:
    """Plane `plane` of a `layout` picture, chroma ceil-subsampled as vmaf_picture_alloc()."""
    if plane == 0 or layout == "444":
        return width, height
    if layout == "422":
        return (width + 1) >> 1, height
    return (width + 1) >> 1, (height + 1) >> 1


def mix(value: int) -> int:
    """A 32-bit integer hash (the finaliser of MurmurHash3)."""
    value &= MASK32
    value ^= value >> 16
    value = (value * 0x85EBCA6B) & MASK32
    value ^= value >> 13
    value = (value * 0xC2B2AE35) & MASK32
    return value ^ (value >> 16)


def sample(x: int, y: int, key: tuple[int, int, int, int], maximum: int) -> int:
    """One sample: ramp moving 3 px per frame, inverted blocks, noise, clipped runs.

    `key` is (plane, frame, distorted, seed): the distorted picture adds its own
    noise and moves a 41x29 window by 5 px.
    """
    plane, frame, distorted, seed = key
    x0, x1, y0, y1 = MOVED_WINDOW
    shift = 3 * frame + (5 if distorted and x0 <= x < x1 and y0 <= y < y1 else 0)
    ramp = ((x + shift) * 7 + y * 5) * maximum // (7 * (WIDTH + 16) + 5 * HEIGHT)
    if ((x + shift) // 11 + y // 9 + plane) % 3 == 0:
        ramp = maximum - ramp
    noise = mix(x * 0x9E3779B1 ^ y * 0x85EBCA77 ^ (seed << 8) ^ (plane << 4) ^ frame)
    amplitude = max(maximum >> (5 if distorted else 6), 1)
    value = ramp + noise % (2 * amplitude + 1) - amplitude
    if (y // 17 + x // 23 + frame) % 7 == 0:
        value += maximum // 3 if (x + y) % 2 else -maximum // 3
    return min(max(value, 0), maximum)


def plane_bytes(width: int, height: int, depth: int, key: tuple[int, int, int, int]) -> bytes:
    """One plane, row after row; 16-bit little-endian words above 8 bits."""
    maximum = (1 << depth) - 1
    values = array("B" if depth == BYTE_DEPTH else "H")
    for y in range(height):
        values.extend(sample(x, y, key, maximum) for x in range(width))
    if values.itemsize > 1 and sys.byteorder != "little":
        values.byteswap()
    return values.tobytes()


def write_fixture(path: Path, depth: int, layout: str, distorted: bool) -> None:
    """Write FRAMES frames of the WIDTH x HEIGHT fixture of `depth` and `layout`."""
    with path.open("wb") as out:
        for frame in range(FRAMES):
            for plane in range(3):
                width, height = plane_size(WIDTH, HEIGHT, layout, plane)
                key = (plane, frame, int(distorted), 0x5EED + depth)
                out.write(plane_bytes(width, height, depth, key))


@dataclasses.dataclass(frozen=True)
class Fixture:
    """The reference and distorted files of one (depth, layout)."""

    ref: Path
    dis: Path
    depth: int
    layout: str


def make_fixture(workdir: Path, depth: int, layout: str) -> Fixture:
    """Generate (once) the pair of one (depth, layout) under `workdir`."""
    stem = workdir / f"matrix_{WIDTH}x{HEIGHT}_{layout}p{depth}"
    fixture = Fixture(Path(f"{stem}_ref.yuv"), Path(f"{stem}_dis.yuv"), depth, layout)
    for path, distorted in ((fixture.ref, False), (fixture.dis, True)):
        if not path.is_file():
            write_fixture(path, depth, layout, distorted)
    return fixture


# ---------------------------------------------------------------------------
# Comparison of two CLI receipts.
# ---------------------------------------------------------------------------


def same_value(a: Any, b: Any) -> bool:
    """The same bits: equal numbers, both null (a non-finite score), or two NaNs."""
    if isinstance(a, float) and isinstance(b, float) and math.isnan(a) and math.isnan(b):
        return True
    return type(a) is type(b) and a == b


def frame_differences(cpu: list[dict[str, Any]], dev: list[dict[str, Any]]) -> list[str]:
    """Every output of every frame that the two runs do not share, as text."""
    if len(cpu) != len(dev) or not cpu:
        return [f"frame count: cpu {len(cpu)}, device {len(dev)}"]
    problems: list[str] = []
    for index, (a, b) in enumerate(zip(cpu, dev, strict=True)):
        ma, mb = a.get("metrics", {}), b.get("metrics", {})
        for key in sorted(set(ma) ^ set(mb)):
            problems.append(f"frame {index} {key}: only the {'cpu' if key in ma else 'device'} run")
        for key in sorted(set(ma) & set(mb)):
            if not same_value(ma[key], mb[key]):
                problems.append(f"frame {index} {key}: cpu {ma[key]!r} device {mb[key]!r}")
    return problems


def max_difference(cpu: list[dict[str, Any]], dev: list[dict[str, Any]]) -> float:
    """Largest absolute difference over the outputs both runs have."""
    worst = 0.0
    for a, b in zip(cpu, dev, strict=False):
        ma, mb = a.get("metrics", {}), b.get("metrics", {})
        for key in set(ma) & set(mb):
            worst = max(worst, metric_delta(ma[key], mb[key]))
    return worst


# ---------------------------------------------------------------------------
# Running the CLI.
# ---------------------------------------------------------------------------


def lock_prefix(backend: str, lock_dir: Path) -> tuple[str, ...]:
    """`flock <lock> timeout 300` when the lock directory exists, else `timeout 300`."""
    flock, timeout = shutil.which("flock"), shutil.which("timeout")
    limit = (timeout, str(DEVICE_RUN_LIMIT_S)) if timeout else ()
    if backend in LOCK_FILE and flock and lock_dir.is_dir():
        return (flock, str(lock_dir / LOCK_FILE[backend]), *limit)
    return limit


@dataclasses.dataclass(frozen=True)
class Cli:
    """The vmaf binary, the working directory and the lock directory."""

    binary: Path
    workdir: Path
    lock_dir: Path

    def frames(
        self, fixture: Fixture, feature: str, backend: str
    ) -> tuple[list[dict[str, Any]] | None, int, str]:
        """(frames, exit status, message) of one run; frames is None on a failure."""
        out = self.workdir / f"{feature}_{fixture.layout}p{fixture.depth}_{backend}.json"
        geometry = (WIDTH, HEIGHT, fixture.layout, fixture.depth)
        argv = build_command(
            self.binary, fixture.ref, fixture.dis, *geometry, feature, backend, None, out, "max"
        )
        device = backend != "cpu"
        full = [*lock_prefix(backend, self.lock_dir), *argv] if device else argv
        proc = run_command(
            full,
            allowed_executables=(full[0],),
            capture_output=True,
            text=True,
            check=False,
            timeout_seconds=WAIT_LIMIT_S if device else DEVICE_RUN_LIMIT_S,
            max_output_bytes=4 * 1_048_576,
        )
        if proc.returncode != 0:
            return None, proc.returncode, (proc.stderr or proc.stdout).strip()[-300:]
        payload = json.loads(out.read_text(encoding="utf-8"))
        return list(payload.get("frames") or []), 0, ""


@dataclasses.dataclass
class CellResult:
    """One (feature, backend, depth, layout) cell."""

    feature: str
    backend: str
    depth: int
    layout: str
    status: str
    frames: int = 0
    values: int = 0
    max_abs_diff: float = 0.0
    note: str = ""


class BackendRefused(Exception):
    """The CLI refused a device backend (exit 100): no device on this host."""


def cpu_cell_status(fixture: Fixture, code: int, message: str) -> tuple[str, str]:
    """Status and note of a cell whose CPU run failed."""
    if fixture.depth == OPTIONAL_DEPTH:
        return STATUS_NA, f"cpu refuses {fixture.depth}-bit: {message}"
    return STATUS_ERROR, f"cpu exited {code}: {message}"


def compare_cell(
    base: CellResult, cpu: list[dict[str, Any]], dev: list[dict[str, Any]]
) -> CellResult:
    """Fill `base` from the two runs' frames."""
    problems = frame_differences(cpu, dev)
    values = sum(len(frame.get("metrics", {})) for frame in cpu)
    return dataclasses.replace(
        base,
        status=STATUS_FAIL if problems else STATUS_PASS,
        frames=len(cpu),
        values=values,
        max_abs_diff=max_difference(cpu, dev),
        note="; ".join(problems[:NOTE_PROBLEMS])
        + (f"; {len(problems)} in all" if len(problems) > NOTE_PROBLEMS else ""),
    )


def run_cell(
    cli: Cli, fixture: Fixture, feature: str, backend: str, cpu: tuple[Any, int, str]
) -> CellResult:
    """One cell, given the CPU run of its (feature, fixture)."""
    base = CellResult(feature, backend, fixture.depth, fixture.layout, STATUS_ERROR)
    cpu_frames, cpu_code, cpu_message = cpu
    if cpu_frames is None:
        status, note = cpu_cell_status(fixture, cpu_code, cpu_message)
        return dataclasses.replace(base, status=status, note=note)
    dev_frames, code, message = cli.frames(fixture, feature, backend)
    if code == EXIT_BACKEND_REFUSED:
        raise BackendRefused(f"--backend {backend}: {message}")
    if dev_frames is None:
        return dataclasses.replace(base, note=f"{backend} exited {code}: {message}")
    return compare_cell(base, cpu_frames, dev_frames)


def exact_features(backends: Iterable[str], only: Sequence[str]) -> dict[str, list[str]]:
    """feature -> the selected backends that declare it exact (exact_twins.d)."""
    chosen = set(backends)
    plan: dict[str, list[str]] = {}
    for feature in sorted(EXACT_TWINS):
        listed = sorted(b for b in EXACT_TWINS[feature] if b in chosen)
        if listed and (not only or feature in only):
            plan[feature] = listed
    return plan


def run_matrix(
    cli: Cli, plan: dict[str, list[str]], depths: Sequence[int], layouts: Sequence[str]
) -> list[CellResult]:
    """Every cell of `plan` over `depths` x `layouts`, printed as it completes."""
    results: list[CellResult] = []
    for depth in depths:
        for layout in layouts:
            fixture = make_fixture(cli.workdir, depth, layout)
            for feature, backends in plan.items():
                cpu = cli.frames(fixture, feature, "cpu")
                for backend in backends:
                    cell = run_cell(cli, fixture, feature, backend, cpu)
                    print_cell(cell)
                    results.append(cell)
    return results


# ---------------------------------------------------------------------------
# Output.
# ---------------------------------------------------------------------------


def print_cell(cell: CellResult) -> None:
    """One line per cell on stdout."""
    print(
        f"{cell.backend:<5} {cell.feature:<18} {cell.depth:>2}-bit {cell.layout}  "
        f"{cell.status:<5} frames={cell.frames} values={cell.values} "
        f"max_abs_diff={cell.max_abs_diff:.3e}" + (f"  {cell.note}" if cell.note else ""),
        flush=True,
    )


def column_labels(depths: Sequence[int], layouts: Sequence[str]) -> list[str]:
    """`8/420`, `8/422`, ... in depth-major order."""
    return [f"{depth}/{layout}" for depth in depths for layout in layouts]


def markdown_table(results: list[CellResult], depths: Sequence[int], layouts: Sequence[str]) -> str:
    """One row per (backend, feature), one column per depth/layout cell."""
    columns = column_labels(depths, layouts)
    cells = {(r.backend, r.feature, f"{r.depth}/{r.layout}"): r.status for r in results}
    rows = sorted({(r.backend, r.feature) for r in results})
    lines = [
        "| backend | feature | " + " | ".join(columns) + " |",
        "|---|---|" + "---|" * len(columns),
    ]
    for backend, feature in rows:
        marks = [cells.get((backend, feature, column), "-") for column in columns]
        lines.append(f"| {backend} | `{feature}` | " + " | ".join(marks) + " |")
    return "\n".join(lines) + "\n"


# ---------------------------------------------------------------------------
# The recorded page: one table per device backend between markers.
# ---------------------------------------------------------------------------

RECORD_BEGIN = "<!-- exact-twin-matrix:{backend}:begin -->"
RECORD_END = "<!-- exact-twin-matrix:{backend}:end -->"


def record_block(backend: str, results: list[CellResult], recorded_on: str) -> str:
    """The marked block of `backend`: the provenance line, then its table."""
    rows = [r for r in results if r.backend == backend]
    equal = sum(r.status == STATUS_PASS for r in rows)
    absent = sum(r.status == STATUS_NA for r in rows)
    counts = f"{equal} of {len(rows)} cells equal, {absent} `n/a`."
    table = markdown_table(rows, DEPTHS, LAYOUTS)
    begin, end = RECORD_BEGIN.format(backend=backend), RECORD_END.format(backend=backend)
    return f"{begin}\n\n{recorded_on}: {counts}\n\n{table}\n{end}"


def record(page: Path, results: list[CellResult], recorded_on: str) -> None:
    """Replace (or append) the block of every backend `results` covers."""
    text = page.read_text(encoding="utf-8")
    for backend in sorted({r.backend for r in results}):
        begin, end = RECORD_BEGIN.format(backend=backend), RECORD_END.format(backend=backend)
        block = record_block(backend, results, recorded_on)
        if begin in text and end in text:
            head, rest = text.split(begin, 1)
            text = head + block + rest.split(end, 1)[1]
        else:
            text = text.rstrip("\n") + "\n\n" + block + "\n"
    page.write_text(text, encoding="utf-8")


def recorded_rows(text: str) -> dict[tuple[str, str], list[str]]:
    """(backend, feature) -> the cell marks of its row, from every recorded table."""
    rows: dict[tuple[str, str], list[str]] = {}
    for line in text.splitlines():
        parts = [part.strip() for part in line.strip().strip("|").split("|")]
        if (
            len(parts) < MIN_ROW_PARTS
            or parts[0] not in DEVICE_BACKENDS
            or not parts[1].startswith("`")
        ):
            continue
        rows[(parts[0], parts[1].strip("`"))] = parts[2:]
    return rows


def row_problems(marks: list[str], columns: list[str]) -> list[str]:
    """Why a recorded row is not a full, passing row."""
    if len(marks) != len(columns):
        return [f"{len(marks)} cells, not {len(columns)}"]
    bad = []
    for column, mark in zip(columns, marks, strict=True):
        optional = column.startswith(f"{OPTIONAL_DEPTH}/") and mark == STATUS_NA
        if mark != STATUS_PASS and not optional:
            bad.append(f"{column} is {mark!r}")
    return bad


def recorded_problems(text: str, twins: dict[str, frozenset[str]] = EXACT_TWINS) -> list[str]:
    """Every exact twin of a device backend that the page does not record as equal."""
    rows = recorded_rows(text)
    columns = column_labels(DEPTHS, LAYOUTS)
    problems: list[str] = []
    for feature in sorted(twins):
        for backend in sorted(set(twins[feature]) & set(DEVICE_BACKENDS)):
            marks = rows.get((backend, feature))
            if marks is None:
                problems.append(f"{feature}.{backend}: declared exact, no recorded matrix row")
                continue
            problems += [f"{feature}.{backend}: {p}" for p in row_problems(marks, columns)]
    return problems


def write_outputs(args: argparse.Namespace, results: list[CellResult]) -> None:
    """The JSON and Markdown files requested on the command line."""
    if args.json_out is not None:
        payload = {
            "schema_version": 1,
            "geometry": [WIDTH, HEIGHT, FRAMES],
            "cells": [dataclasses.asdict(r) for r in results],
        }
        args.json_out.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n")
    if args.md_out is not None:
        args.md_out.write_text(markdown_table(results, args.depths, args.layouts))
    if args.record is not None:
        record(args.record, results, args.recorded_on)


def summary(results: list[CellResult]) -> int:
    """Print the counts; 0 when no cell failed."""
    counts = {s: sum(r.status == s for r in results) for s in (STATUS_PASS, STATUS_NA)}
    failed = [r for r in results if r.status not in (STATUS_PASS, STATUS_NA)]
    print(
        f"exact-twin matrix: {len(results)} cells, {counts[STATUS_PASS]} equal, "
        f"{counts[STATUS_NA]} n/a (cpu refuses the depth), {len(failed)} failed"
    )
    for cell in failed:
        print(f"  FAILED {cell.backend} {cell.feature} {cell.depth}/{cell.layout}: {cell.note}")
    return 1 if failed else 0


def parse_args(argv: Sequence[str] | None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--vmaf-binary", type=Path, required=True)
    parser.add_argument("--backends", nargs="+", choices=DEVICE_BACKENDS, required=True)
    parser.add_argument("--depths", nargs="+", type=int, choices=DEPTHS, default=list(DEPTHS))
    parser.add_argument("--layouts", nargs="+", choices=LAYOUTS, default=list(LAYOUTS))
    parser.add_argument("--features", nargs="*", default=[])
    parser.add_argument("--workdir", type=Path, default=None)
    parser.add_argument("--lock-dir", type=Path, default=None)
    parser.add_argument("--json-out", type=Path, default=None)
    parser.add_argument("--md-out", type=Path, default=None)
    parser.add_argument("--record", type=Path, default=None)
    parser.add_argument("--recorded-on", default="")
    return parser.parse_args(argv)


def usage_error(args: argparse.Namespace) -> str | None:
    """Why the arguments cannot run, or None."""
    if not args.vmaf_binary.is_file():
        return f"vmaf binary not found: {args.vmaf_binary}"
    if args.record is None:
        return None
    full = not args.features and set(args.depths) == set(DEPTHS)
    if not full or set(args.layouts) != set(LAYOUTS) or not args.recorded_on:
        return "--record needs every depth, layout and feature, and --recorded-on"
    return None


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    error = usage_error(args)
    if error is not None:
        sys.stderr.write(f"{error}\n")
        return 2
    plan = exact_features(args.backends, args.features)
    if not plan:
        sys.stderr.write("no exact twin matches --backends / --features\n")
        return 2
    default_locks = Path(os.environ.get("VMAFX_LOCK_DIR", Path.home() / ".cache" / "vmafx-locks"))
    with tempfile.TemporaryDirectory(prefix="exact-matrix-") as tmp:
        workdir = args.workdir or Path(tmp)
        workdir.mkdir(parents=True, exist_ok=True)
        cli = Cli(args.vmaf_binary.resolve(), workdir, args.lock_dir or default_locks)
        try:
            results = run_matrix(cli, plan, args.depths, args.layouts)
        except BackendRefused as refused:
            sys.stderr.write(f"{refused}; skipping ({SKIP})\n")
            return SKIP
    write_outputs(args, results)
    return summary(results)


if __name__ == "__main__":
    sys.exit(main())
