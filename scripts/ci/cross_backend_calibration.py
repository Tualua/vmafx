#!/usr/bin/env python3
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Per-GPU-generation ULP calibration loader (ADR-0234).

Companion to ``cross_backend_vif_diff.py`` and
``cross_backend_parity_gate.py``. Loads the YAML calibration table
in ``scripts/ci/gpu_ulp_calibration.yaml`` and resolves a
``(feature, gpu_id)`` lookup to an absolute tolerance.

Why this lives in its own module:

* The two gate scripts share the lookup logic verbatim; one source
  of truth avoids drift.
* The lookup is unit-tested (``scripts/ci/test_calibration.py``)
  without spinning up the gates' subprocess scaffolding.
* The module is import-safe even on hosts without ``pyyaml``
  installed — falling back to the gate's per-feature default and
  emitting a one-line stderr advisory rather than crashing CI.

The data shape is documented in ``gpu_ulp_calibration.yaml`` itself.
This module is the loader, not the schema definition.
"""

from __future__ import annotations

import dataclasses
import math
import re
import sys
from collections.abc import Collection, Mapping
from pathlib import Path
from typing import Any

# pyyaml is universally available on the project's CI images and
# locally (see ``ai/src/vmaf_train/data/datasets.py`` etc.). We import
# it lazily so a misconfigured host produces a clear advisory rather
# than an opaque ImportError on script startup. The gate scripts pass
# a ``None`` table when calibration cannot load — the lookup falls
# back to the existing per-feature default in that case.
try:
    import yaml  # type: ignore[import-untyped]
except ImportError:  # pragma: no cover - exercised only when pyyaml absent
    yaml = None


DEFAULT_CALIBRATION_PATH = Path(__file__).parent / "gpu_ulp_calibration.yaml"

# ---------------------------------------------------------------------------
# ADR-1361: area-scaled tolerance for features whose CPU reference adds
# every per-coefficient term of a plane into one running float.
#
# ``calc_psnrhvs()`` (third_party/xiph/psnr_hvs.c) sums N = 64 * blocks_x *
# blocks_y terms per plane into one ``float``. Its relative rounding error
# grows like u * sqrt(N) (probabilistic model, u = 2**-24), and the dB score
# carries it as (10 / ln 10) * relative error, so the achievable CPU/GPU
# agreement loosens with the frame area. The tolerance a table supplies is
# the contract at the reference geometry (576x324, where ADR-0191 measured
# it); above that it grows with sqrt(N / N_ref), below it never tightens or
# loosens. The same scaling is equivalent to the bound
#   T(N) = max(T_ref, (10 / ln 10) * lam * u * sqrt(N)),
#   lam  = T_ref / ((10 / ln 10) * u * sqrt(N_ref))   (3.93 for T_ref = 5e-4).
# ---------------------------------------------------------------------------

PSNR_HVS_BLOCK = 8
PSNR_HVS_STEP = 7
AREA_SCALED_REFERENCE_GEOMETRY = (576, 324)

# Only the luma plane's terms are counted: it has the most, and the combined
# score's relative error is bounded by the worst plane's.
AREA_SCALED_FEATURES = ("psnr_hvs",)


# ---------------------------------------------------------------------------
# ADR-1397: twins that reproduce the CPU extractor bit for bit.
#
# ``psnr_hvs_cuda`` (ADR-1397), ``psnr_hvs_sycl`` and ``psnr_hvs_hip``
# (ADR-1401) store the terms ``calc_psnrhvs()`` sums, computed in the CPU's
# arithmetic, and the host adds them into one running float in the CPU's
# order. Their scores are therefore the CPU's, at every frame size, and a cell
# whose two sides are the CPU extractor or such a twin is compared exactly:
# tolerance 0, no area scaling, no calibration row, and ``--precision max`` so
# that a last-bit difference reaches the comparison instead of being rounded
# away by the default ``%.6f``. Both sides of a cell come from one ``vmaf``
# binary, so they share the host ``log10`` behind the dB value. A twin joins
# this table only with a measurement that shows bit-identity and an ADR that
# records it; a twin that sums per block (``psnr_hvs_metal``, outside the
# gates' backend list) keeps the ADR-1361 tolerance.
# ADR-1428 (fragment pattern, ADR-0221): the listing is not a literal. Each
# (feature, backend) pair is one file ``exact_twins.d/<feature>.<backend>``
# naming the ADR that establishes exactness and one line of evidence, so
# declaring a twin exact adds one file and edits nothing shared. The ADR in
# each fragment is the ADR of that twin's arithmetic; ``EXACT_TWIN_SOURCE``
# names ADR-1397 for every listed twin because it is the ADR of the exact cell
# itself.
# ---------------------------------------------------------------------------

EXACT_TWINS_DIR = Path(__file__).parent / "exact_twins.d"
ADR_DIR = Path(__file__).resolve().parents[2] / "docs" / "adr"
FRAGMENT_KEYS = ("adr", "evidence")
_FRAGMENT_NAME = re.compile(r"^(?P<feature>[a-z0-9_]+)\.(?P<backend>[a-z0-9_]+)$")
_ADR_LIST = re.compile(r"^ADR-\d{4}(, ADR-\d{4})*$")


class ExactTwinError(ValueError):
    """An ``exact_twins.d`` fragment or the directory itself is malformed."""


@dataclasses.dataclass(frozen=True)
class ExactTwin:
    """One listed twin: ``backend`` returns the CPU extractor's bits for ``feature``."""

    feature: str
    backend: str
    adrs: tuple[str, ...]
    evidence: str


def _parse_fragment_lines(path: Path) -> dict[str, str]:
    """Return the ``key: value`` pairs of one fragment; reject anything else."""

    lines = path.read_text(encoding="utf-8").splitlines()
    if not any(line.strip() for line in lines):
        raise ExactTwinError(f"{path.name}: empty fragment")
    fields: dict[str, str] = {}
    for number, line in enumerate(lines, start=1):
        key, sep, value = line.partition(":")
        key, value = key.strip(), value.strip()
        if not sep or not value:
            raise ExactTwinError(f"{path.name}:{number}: expected 'key: value', got {line!r}")
        if key not in FRAGMENT_KEYS:
            raise ExactTwinError(f"{path.name}:{number}: unknown key {key!r}")
        if key in fields:
            raise ExactTwinError(f"{path.name}:{number}: duplicate key {key!r}")
        fields[key] = value
    return fields


def _check_adrs(path: Path, value: str, adr_dir: Path) -> tuple[str, ...]:
    if not _ADR_LIST.match(value):
        raise ExactTwinError(f"{path.name}: adr must be 'ADR-NNNN[, ADR-NNNN...]', got {value!r}")
    adrs = tuple(item.strip() for item in value.split(","))
    for adr in adrs:
        if not list(adr_dir.glob(f"{adr[4:]}-*.md")):
            raise ExactTwinError(f"{path.name}: {adr} has no file under {adr_dir}")
    return adrs


def parse_exact_twin_fragment(path: Path, adr_dir: Path = ADR_DIR) -> ExactTwin:
    """Parse and validate one ``<feature>.<backend>`` fragment file."""

    match = _FRAGMENT_NAME.match(path.name)
    if not path.is_file() or match is None or match["backend"] == "cpu":
        raise ExactTwinError(f"{path.name}: name must be <feature>.<backend> (backend not cpu)")
    fields = _parse_fragment_lines(path)
    for key in FRAGMENT_KEYS:
        if key not in fields:
            raise ExactTwinError(f"{path.name}: missing key {key!r}")
    return ExactTwin(
        feature=match["feature"],
        backend=match["backend"],
        adrs=_check_adrs(path, fields["adr"], adr_dir),
        evidence=fields["evidence"],
    )


def load_exact_twin_fragments(
    directory: Path = EXACT_TWINS_DIR, adr_dir: Path = ADR_DIR
) -> tuple[ExactTwin, ...]:
    """Load every fragment in ``directory``, sorted by (feature, backend).

    One file per pair makes a duplicate pair impossible; a duplicate key
    inside a file is rejected by the parser.

    A missing or empty directory is an error: it would otherwise silently
    turn every exact cell back into a tolerance cell.
    """

    if not directory.is_dir():
        raise ExactTwinError(f"{directory}: exact-twin fragment directory is missing")
    paths = sorted(directory.iterdir())
    if not paths:
        raise ExactTwinError(f"{directory}: no exact-twin fragments")
    twins = tuple(parse_exact_twin_fragment(path, adr_dir) for path in paths)
    return tuple(sorted(twins, key=lambda twin: (twin.feature, twin.backend)))


def build_exact_twins(twins: tuple[ExactTwin, ...]) -> dict[str, frozenset[str]]:
    """Group listed twins into ``feature -> backends``."""

    grouped: dict[str, set[str]] = {}
    for twin in twins:
        grouped.setdefault(twin.feature, set()).add(twin.backend)
    return {feature: frozenset(grouped[feature]) for feature in sorted(grouped)}


def validate_exact_twins(
    twins: tuple[ExactTwin, ...], features: Collection[str], backends: Collection[str]
) -> None:
    """Reject a fragment naming a feature or backend the gate does not know."""

    for twin in twins:
        if twin.feature not in features:
            raise ExactTwinError(f"{twin.feature}.{twin.backend}: unknown feature")
        if twin.backend not in backends:
            raise ExactTwinError(f"{twin.feature}.{twin.backend}: unknown backend")


EXACT_TWIN_FRAGMENTS: tuple[ExactTwin, ...] = load_exact_twin_fragments()
EXACT_TWINS: dict[str, frozenset[str]] = build_exact_twins(EXACT_TWIN_FRAGMENTS)
EXACT_TWIN_TOLERANCE = 0.0
EXACT_TWIN_PRECISION = "max"
EXACT_TWIN_SOURCE = "exact:ADR-1397"


def is_exact_pair(feature: str, backend_a: str, backend_b: str) -> bool:
    """True when both sides of a cell return the CPU extractor's bits for ``feature``.

    A side qualifies when it is ``cpu`` or a backend listed for the feature
    in ``EXACT_TWINS``; a feature without an entry never qualifies.
    """

    exact = EXACT_TWINS.get(feature)
    if exact is None:
        return False
    return all(backend == "cpu" or backend in exact for backend in (backend_a, backend_b))


# ---------------------------------------------------------------------------
# ADR-1426: twins that run the CPU extractor's arithmetic but call another
# math library.
#
# ``ciede_cuda`` evaluates ``ciede.c``'s expressions in its types and the host
# adds the per-pixel values in the CPU's raster order, so the only difference
# left is the math library behind ``pow``, ``atan2``, ``sin``, ``cos``,
# ``exp`` and ``powf``: glibc on the CPU, CUDA's on the device. A few pixels
# in a million round to the neighbouring float (38 of 8.3 million on a 4K
# frame, all of them from glibc's ``powf``, which is not correctly rounded).
# One such pixel moves ``45 - 20 * log10(mean)`` by at most
# ``8.7 * 2^-23 * (value / mean) / pixels``. Measured on an RTX 4090: 1.4e-11
# at 3840x2160, 6.9e-13 at 576x324. The tolerance below is for frames of
# 576x324 and larger; it leaves two orders of magnitude over the measurement
# and four below the fp32 twins' ``FEATURE_TOLERANCE``. Such a cell runs at
# ``--precision max`` like an exact one.
#
# ADR-1430: ``speed_chroma_cuda`` is the same case with one function. The
# device rounds ``log2`` correctly (ADR-1380); ``speed.c`` calls the C
# library's ``log2f``, and glibc's returns the neighbouring float for 0.015 %
# to 0.97 % of the arguments of a binade. Run against a CPU whose ``log2f`` is
# correctly rounded the twin returns the CPU's bits on every output (789 of
# 789 on the fixtures); against glibc 13 of those 789 differ, by one to five
# steps of the fp32 score (4.8e-7 to 1.4e-6 for scores of 3 to 11). The
# bound is that count in the coarsest steps the fixtures have: the scores are
# fp32 values below 16, where a step is at most 2^-20, and five steps are
# 5 * 2^-20 = 4.77e-6, written as 5e-6. A fixture whose scores are larger
# needs the same count in its own step (a synthetic frame scoring 22.5 is
# 3.8e-6 away, two steps of 2^-19). It is not a statement about the twin's
# arithmetic, which has no known difference left.
#
# ADR-1436: ``ciede_sycl`` runs the same statements on a device without an
# fp64 type, with every fp64 value as an fp32 pair (about 48 bits) and every
# math-library call as a pair function (about 2^-44). Its pixels can differ
# from the CPU's where a pair does not decide a float rounding (about one
# pixel in 1.6 million against the fp64 evaluation on synthetic colour pairs,
# none of 24.9 million on three 4K frames) and do differ where the host's
# ``powf`` is not correctly rounded, as on CUDA (18 to 64 pixels per 4K
# frame). Measured on an Arc A380: 1.4e-11 at 3840x2160, 6.9e-13 at 576x324,
# the CUDA twin's figures.
# ---------------------------------------------------------------------------

LIBM_TWINS: dict[str, dict[str, float]] = {
    "ciede": {"cuda": 1e-9, "sycl": 1e-9},
    "speed_chroma": {"cuda": 5e-6},
}
LIBM_TWIN_SOURCE = "libm:ADR-1426"


def libm_pair_tolerance(feature: str, backend_a: str, backend_b: str) -> float | None:
    """Tolerance of a cell whose sides differ only in their math library.

    A side qualifies when it is ``cpu`` or a backend listed for the feature
    in ``LIBM_TWINS``; the cell's tolerance is the largest listed one. Returns
    ``None`` for every other cell.
    """

    twins = LIBM_TWINS.get(feature)
    if twins is None:
        return None
    sides = (backend_a, backend_b)
    if not all(side == "cpu" or side in twins for side in sides):
        return None
    listed = [twins[side] for side in sides if side in twins]
    return max(listed) if listed else None


def psnr_hvs_term_count(width: int, height: int) -> int:
    """Terms ``calc_psnrhvs()`` adds into one float for a ``width x height`` luma plane.

    Blocks start every ``PSNR_HVS_STEP`` pixels while a whole 8x8 block fits
    (``for (y = 0; y < h - 7; y += 7)``), and each block contributes 64 terms.
    A plane smaller than one block has no terms.
    """

    if width < PSNR_HVS_BLOCK or height < PSNR_HVS_BLOCK:
        return 0
    blocks_x = (width - PSNR_HVS_BLOCK) // PSNR_HVS_STEP + 1
    blocks_y = (height - PSNR_HVS_BLOCK) // PSNR_HVS_STEP + 1
    return PSNR_HVS_BLOCK * PSNR_HVS_BLOCK * blocks_x * blocks_y


def metric_delta(value_a: float | None, value_b: float | None) -> float:
    """Absolute difference of one metric between two runs, as both gates compare it.

    vmaf writes a non-finite score as JSON ``null``: identical pictures give
    ``psnr_hvs_cb`` = inf, for example (four of the first 50 BBB 4K frames).
    Both sides ``null`` is agreement (0.0); one side ``null`` is a mismatch
    (inf). Before this, the gates raised TypeError on such a fixture.
    """

    if value_a is None or value_b is None:
        return 0.0 if value_a is None and value_b is None else math.inf
    return abs(value_a - value_b)


def area_tolerance_factor(feature: str, width: int | None, height: int | None) -> float:
    """Multiplier applied to ``feature``'s reference-geometry tolerance (ADR-1361).

    1.0 for every feature outside ``AREA_SCALED_FEATURES``, for an unknown
    geometry, and for any frame at or below the reference term count;
    ``sqrt(N / N_ref)`` above it.
    """

    if feature not in AREA_SCALED_FEATURES or width is None or height is None:
        return 1.0
    terms = psnr_hvs_term_count(width, height)
    reference_terms = psnr_hvs_term_count(*AREA_SCALED_REFERENCE_GEOMETRY)
    if terms <= reference_terms:
        return 1.0
    return math.sqrt(terms / reference_terms)


@dataclasses.dataclass(frozen=True)
class CalibrationEntry:
    """One ``gpus:`` row from the calibration YAML."""

    gpu_id_pattern: str
    label: str
    status: str  # "calibrated" | "placeholder"
    features: Mapping[str, float]
    notes: str = ""

    def matches(self, gpu_id: str) -> bool:
        """Glob-match ``gpu_id`` against this entry's pattern.

        Only trailing ``*`` is supported — the patterns in the YAML
        are simple prefix-match expressions ("vulkan:0x1002:0x73*").
        ``*`` anywhere else is treated literally; we deliberately
        avoid ``fnmatch.fnmatch`` here to keep the matching surface
        small and the precedence rules trivial.
        """

        if self.gpu_id_pattern.endswith("*"):
            prefix = self.gpu_id_pattern[:-1]
            return gpu_id.startswith(prefix)
        return self.gpu_id_pattern == gpu_id

    def specificity(self) -> int:
        """Length of the non-wildcard prefix. Higher = more specific."""

        if self.gpu_id_pattern.endswith("*"):
            return len(self.gpu_id_pattern) - 1
        return len(self.gpu_id_pattern) + 1  # exact match beats any glob


@dataclasses.dataclass
class CalibrationTable:
    """Loaded calibration table (the YAML file's deserialised form)."""

    version: int
    default_fp32_tolerance: float
    default_fp16_tolerance: float
    entries: list[CalibrationEntry]

    def lookup(self, gpu_id: str) -> CalibrationEntry | None:
        """Most-specific match for ``gpu_id``, or ``None``.

        Specificity = length of the non-wildcard prefix. Ties are
        broken by file order (first match wins) — predictable and
        easy to audit when two patterns shadow each other.
        """

        best: CalibrationEntry | None = None
        for entry in self.entries:
            if not entry.matches(gpu_id):
                continue
            if best is None or entry.specificity() > best.specificity():
                best = entry
        return best

    def tolerance_for(
        self,
        feature: str,
        gpu_id: str | None,
        feature_default: float,
    ) -> float:
        """Resolve the tolerance for ``(feature, gpu_id)``.

        Resolution order:

        1. If ``gpu_id`` is None or no entry matches, return the
           caller-supplied per-feature default (existing
           ``FEATURE_TOLERANCE.get(feature, DEFAULT_FP32_TOLERANCE)``
           contract preserved).
        2. If the matched entry has a ``features:`` override for
           ``feature``, return that.
        3. Otherwise return the caller-supplied per-feature default
           (the matched arch is registered but lacks calibration
           data for this feature — typical of placeholder entries).
        """

        if gpu_id is None:
            return feature_default
        entry = self.lookup(gpu_id)
        if entry is None:
            return feature_default
        return float(entry.features.get(feature, feature_default))


def _coerce_features(raw: Any) -> Mapping[str, float]:
    """Normalise the YAML ``features:`` block into ``{name: float}``.

    ``raw`` is whatever ``yaml.safe_load`` returned for that field —
    expected to be a mapping ``{feature_name: tolerance}`` or
    ``None`` / missing for placeholder entries.
    """

    if raw is None:
        return {}
    if not isinstance(raw, Mapping):
        msg = f"calibration entry 'features' must be a mapping, got {type(raw).__name__}"
        raise ValueError(msg)
    out: dict[str, float] = {}
    for name, value in raw.items():
        if not isinstance(name, str):
            msg = f"calibration feature name must be str, got {type(name).__name__}"
            raise ValueError(msg)
        out[name] = float(value)
    return out


def parse_table(payload: Mapping[str, Any]) -> CalibrationTable:
    """Build a ``CalibrationTable`` from an already-deserialised mapping.

    Split out from ``load_calibration_table`` so the unit test can
    exercise the parser without touching the filesystem.
    """

    if not isinstance(payload, Mapping):
        msg = f"calibration table root must be a mapping, got {type(payload).__name__}"
        raise ValueError(msg)
    version = int(payload.get("version", 1))
    default_fp32 = float(payload.get("default_fp32_tolerance", 5.0e-5))
    default_fp16 = float(payload.get("default_fp16_tolerance", 1.0e-2))
    raw_gpus = payload.get("gpus", [])
    if not isinstance(raw_gpus, list):
        msg = f"calibration 'gpus' must be a list, got {type(raw_gpus).__name__}"
        raise ValueError(msg)
    entries: list[CalibrationEntry] = []
    for row in raw_gpus:
        if not isinstance(row, Mapping):
            msg = f"calibration gpu row must be a mapping, got {type(row).__name__}"
            raise ValueError(msg)
        pattern = row.get("id")
        if not isinstance(pattern, str) or not pattern:
            msg = "calibration gpu row missing required 'id' field"
            raise ValueError(msg)
        entries.append(
            CalibrationEntry(
                gpu_id_pattern=pattern,
                label=str(row.get("label", pattern)),
                status=str(row.get("status", "placeholder")),
                features=_coerce_features(row.get("features")),
                notes=str(row.get("notes", "")),
            )
        )
    return CalibrationTable(
        version=version,
        default_fp32_tolerance=default_fp32,
        default_fp16_tolerance=default_fp16,
        entries=entries,
    )


def load_calibration_table(path: Path | None = None) -> CalibrationTable | None:
    """Load and parse the YAML calibration table.

    Returns ``None`` (with a stderr advisory) when ``pyyaml`` is
    unavailable or the file is missing — the gate scripts treat
    ``None`` as "no calibration; use built-in defaults", preserving
    backward compatibility with hosts that haven't installed the
    optional dependency.

    Raises ``ValueError`` on a malformed table — a corrupt
    calibration file is a CI-visible bug, not a silent fallback.
    """

    if path is None:
        path = DEFAULT_CALIBRATION_PATH
    if yaml is None:
        sys.stderr.write(
            "calibration: pyyaml not installed; falling back to per-feature defaults\n"
        )
        return None
    if not path.exists():
        sys.stderr.write(
            f"calibration: table not found at {path}; falling back to per-feature defaults\n"
        )
        return None
    with path.open() as fh:
        payload = yaml.safe_load(fh)
    return parse_table(payload)
