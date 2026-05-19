#!/usr/bin/env python3
# Copyright 2026 Lusoris and Claude (Anthropic)
# SPDX-License-Identifier: BSD-3-Clause-Plus-Patent
"""Calibrate the δ_fast threshold for NR early-elimination (ADR-0615 / ADR-0624).

Walks the Netflix corpus (`.corpus/netflix/`) or a fallback subset of
BBB / KoNViD / BVI-DVC clips, runs both FR VMAF and NR scoring at a grid
of CRF values, fits a linear calibration curve `vmaf_fr ≈ f(vmaf_nr)`,
and writes `calibration_threshold = 2σ(residuals)` into
`model/tiny/nr_metric_v1.json`.

Usage
-----
::

    # With Netflix corpus (preferred):
    python ai/scripts/calibrate_nr_threshold.py \\
        --corpus .corpus/netflix/ \\
        --output model/tiny/nr_metric_v1.json

    # Dry-run (no JSON write):
    python ai/scripts/calibrate_nr_threshold.py --dry-run

    # Force a specific δ_fast value (skip regression):
    python ai/scripts/calibrate_nr_threshold.py --delta-fast 8.0

    # Custom encoder / CRF grid:
    python ai/scripts/calibrate_nr_threshold.py --crfs 20,25,30,35 --codec libx264

Output
------
Writes a calibration report to
``docs/ai/models/nr_metric_v1-calibration-<YYYY-MM-DD>.md`` and updates
the ``calibration_threshold`` field of ``model/tiny/nr_metric_v1.json``.
"""

from __future__ import annotations

import argparse
import json
import logging
import math
import subprocess
import sys
import tempfile
from datetime import date
from pathlib import Path
from typing import NamedTuple

_log = logging.getLogger(__name__)

# ---------------------------------------------------------------------------
# Defaults
# ---------------------------------------------------------------------------

_REPO_ROOT = Path(__file__).resolve().parents[2]
_DEFAULT_MODEL_JSON = _REPO_ROOT / "model" / "tiny" / "nr_metric_v1.json"
_DEFAULT_MODEL_ONNX = _REPO_ROOT / "model" / "tiny" / "nr_metric_v1.onnx"
_DEFAULT_CORPUS = _REPO_ROOT / ".corpus" / "netflix"
_DEFAULT_CRFS: tuple[int, ...] = (18, 23, 28, 33, 38)
_DEFAULT_CODEC = "libx264"
_DEFAULT_PRESET = "medium"

# BBB YUVs under python/test/resource/yuv/ as minimal fallback fixtures.
_FALLBACK_YUV_GLOB = "src01*.yuv"
_FALLBACK_YUV_DIR = _REPO_ROOT / "python" / "test" / "resource" / "yuv"


# ---------------------------------------------------------------------------
# Data types
# ---------------------------------------------------------------------------


class CalibrationSample(NamedTuple):
    """One (CRF, NR score, FR score) measurement."""

    yuv_name: str
    crf: int
    nr_score: float
    fr_score: float

    @property
    def residual(self) -> float:
        """Residual = FR - NR (before regression; used for naive 1:1 estimate)."""
        return self.fr_score - self.nr_score


# ---------------------------------------------------------------------------
# Core helpers
# ---------------------------------------------------------------------------


def _find_yuv_files(corpus: Path) -> list[Path]:
    """Return all .yuv files under *corpus* (recursive)."""
    return sorted(corpus.rglob("*.yuv"))


def _run_fr_vmaf(
    ref_yuv: Path,
    dist_yuv: Path,
    *,
    width: int,
    height: int,
    pix_fmt: str = "yuv420p",
    vmaf_bin: str = "vmaf",
) -> float | None:
    """Run the vmaf CLI and return the pooled VMAF score, or None on error."""
    cmd = [
        vmaf_bin,
        "--reference",
        str(ref_yuv),
        "--distorted",
        str(dist_yuv),
        "--width",
        str(width),
        "--height",
        str(height),
        "--pixel_format",
        pix_fmt,
        "--output",
        "/dev/stdout",
        "--json",
        "--no_progress",
    ]
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, check=False, timeout=120)
    except (OSError, subprocess.SubprocessError) as exc:
        _log.warning("FR VMAF failed for %s: %s", dist_yuv.name, exc)
        return None
    if proc.returncode != 0:
        _log.warning("FR VMAF non-zero exit (%d) for %s", proc.returncode, dist_yuv.name)
        return None
    try:
        data = json.loads(proc.stdout)
        return float(data["pooled_metrics"]["vmaf"]["mean"])
    except (json.JSONDecodeError, KeyError, TypeError, ValueError) as exc:
        _log.warning("FR VMAF parse error for %s: %s", dist_yuv.name, exc)
        return None


def _encode_yuv(
    ref_yuv: Path,
    *,
    width: int,
    height: int,
    pix_fmt: str = "yuv420p",
    codec: str = "libx264",
    preset: str = "medium",
    crf: int = 23,
    output: Path,
    ffmpeg_bin: str = "ffmpeg",
) -> bool:
    """Encode ref_yuv to output using the given CRF; return True on success."""
    cmd = [
        ffmpeg_bin,
        "-y",
        "-f",
        "rawvideo",
        "-pixel_format",
        pix_fmt,
        "-video_size",
        f"{width}x{height}",
        "-i",
        str(ref_yuv),
        "-c:v",
        codec,
        "-preset",
        preset,
        "-crf",
        str(crf),
        str(output),
    ]
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, check=False, timeout=300)
        return proc.returncode == 0
    except (OSError, subprocess.SubprocessError) as exc:
        _log.warning("Encode failed for %s CRF=%d: %s", ref_yuv.name, crf, exc)
        return False


def _decode_to_yuv(
    encoded: Path,
    output: Path,
    *,
    pix_fmt: str = "yuv420p",
    ffmpeg_bin: str = "ffmpeg",
) -> bool:
    """Decode encoded container to raw YUV; return True on success."""
    cmd = [
        ffmpeg_bin,
        "-y",
        "-i",
        str(encoded),
        "-f",
        "rawvideo",
        "-pix_fmt",
        pix_fmt,
        str(output),
    ]
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, check=False, timeout=300)
        return proc.returncode == 0
    except (OSError, subprocess.SubprocessError) as exc:
        _log.warning("Decode failed for %s: %s", encoded.name, exc)
        return False


def _detect_yuv_geometry(yuv: Path) -> tuple[int, int] | None:
    """Attempt to infer width×height from the filename (e.g. _1920x1080_).

    Returns (width, height) or None when the filename does not embed geometry.
    """
    import re

    m = re.search(r"[_x](\d{2,4})[x_](\d{2,4})", yuv.stem)
    if m:
        w, h = int(m.group(1)), int(m.group(2))
        if 64 <= w <= 7680 and 64 <= h <= 4320:
            return w, h
    # Fallback patterns: 576x324, 1920x1080 in the stem
    for m2 in re.finditer(r"(\d{3,4})x(\d{3,4})", yuv.stem):
        w, h = int(m2.group(1)), int(m2.group(2))
        if 64 <= w <= 7680 and 64 <= h <= 4320:
            return w, h
    return None


def _run_nr_score(
    dist_yuv: Path,
    *,
    width: int,
    height: int,
    pix_fmt: str = "yuv420p",
    nr_model_path: Path,
) -> float | None:
    """Return the NR VMAF proxy score for dist_yuv, or None on error."""
    try:
        sys.path.insert(0, str(_REPO_ROOT / "tools" / "vmaf-tune" / "src"))
        from vmaftune.score_backend import NRProxyBackend

        backend = NRProxyBackend(model_path=nr_model_path, use_gpu_ep=True)
        result = backend.score_nr(dist_yuv, width=width, height=height, pix_fmt=pix_fmt)
        return result.nr_score
    except Exception as exc:
        _log.warning("NR score failed for %s: %s", dist_yuv.name, exc)
        return None


# ---------------------------------------------------------------------------
# Regression / statistics
# ---------------------------------------------------------------------------


def _linear_regression(x: list[float], y: list[float]) -> tuple[float, float, list[float]]:
    """Fit y = a*x + b (least-squares) and return (a, b, residuals)."""
    n = len(x)
    if n < 2:
        raise ValueError(f"Need at least 2 samples for regression; got {n}")
    sx = sum(x)
    sy = sum(y)
    sxx = sum(xi * xi for xi in x)
    sxy = sum(xi * yi for xi, yi in zip(x, y, strict=True))
    denom = n * sxx - sx * sx
    if abs(denom) < 1e-12:
        # Degenerate (all NR scores identical) — fall back to mean bias
        a = 0.0
        b = sy / n - a * sx / n
    else:
        a = (n * sxy - sx * sy) / denom
        b = (sy - a * sx) / n
    residuals = [yi - (a * xi + b) for xi, yi in zip(x, y, strict=True)]
    return a, b, residuals


def _compute_delta_fast(residuals: list[float]) -> float:
    """Compute δ_fast = 2σ(residuals) (covers >95% of in-domain content)."""
    n = len(residuals)
    if n < 2:
        return 8.0  # fall back to ADR-0615 design default
    mean_r = sum(residuals) / n
    var_r = sum((r - mean_r) ** 2 for r in residuals) / (n - 1)
    sigma = math.sqrt(var_r)
    return 2.0 * sigma


def _pearson_r(x: list[float], y: list[float]) -> float:
    """Return Pearson correlation coefficient."""
    n = len(x)
    if n < 2:
        return float("nan")
    mx = sum(x) / n
    my = sum(y) / n
    num = sum((xi - mx) * (yi - my) for xi, yi in zip(x, y, strict=True))
    denom = math.sqrt(sum((xi - mx) ** 2 for xi in x) * sum((yi - my) ** 2 for yi in y))
    if denom < 1e-12:
        return float("nan")
    return num / denom


# ---------------------------------------------------------------------------
# Report generation
# ---------------------------------------------------------------------------


def _write_calibration_report(
    samples: list[CalibrationSample],
    *,
    a: float,
    b: float,
    sigma: float,
    delta_fast: float,
    plcc: float,
    n_clips: int,
    n_crfs: int,
    corpus_path: str,
    output_dir: Path,
) -> Path:
    """Write a Markdown calibration report and return its path."""
    today = date.today().isoformat()
    report_name = f"nr_metric_v1-calibration-{today}.md"
    report_path = output_dir / report_name

    lines = [
        f"# NR metric v1 — Calibration Report ({today})",
        "",
        "## Summary",
        "",
        f"- **Corpus**: `{corpus_path}`",
        f"- **Clips**: {n_clips}",
        f"- **CRF values**: {n_crfs} ({_DEFAULT_CRFS})",
        f"- **Total samples**: {len(samples)}",
        f"- **Regression**: `vmaf_fr ≈ {a:.4f} × vmaf_nr + {b:.4f}`",
        f"- **PLCC**: {plcc:.4f}",
        f"- **σ(residuals)**: {sigma:.4f} VMAF",
        f"- **δ_fast (2σ)**: **{delta_fast:.2f} VMAF**",
        "",
        "## Interpretation",
        "",
        (
            f"During bisect, when |NR_score − target| > {delta_fast:.2f} VMAF, "
            "the full-reference VMAF call is skipped and the bisect window advances "
            "in the NR-implied direction. This threshold is calibrated to cover "
            ">95% of in-domain content residuals correctly (2σ coverage)."
        ),
        "",
        "## Per-sample data (first 50)",
        "",
        "| Clip | CRF | NR score | FR score | Residual (FR−NR) |",
        "|------|-----|----------|----------|-----------------|",
    ]
    for s in samples[:50]:
        lines.append(
            f"| {s.yuv_name} | {s.crf} | {s.nr_score:.2f} | {s.fr_score:.2f} | {s.residual:.2f} |"
        )
    if len(samples) > 50:
        lines.append(f"| … | … | … | … | ({len(samples) - 50} more rows omitted) |")

    lines += [
        "",
        "## References",
        "",
        "- ADR-0615: Fast NR pre-scoring for CRF bisect acceleration.",
        "- ADR-0624: Implementation record.",
        "- `model/tiny/nr_metric_v1.json` — updated `calibration_threshold` field.",
        "- `ai/scripts/calibrate_nr_threshold.py` — this script.",
    ]

    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return report_path


# ---------------------------------------------------------------------------
# Main calibration logic
# ---------------------------------------------------------------------------


def calibrate(
    *,
    corpus: Path,
    model_json: Path,
    model_onnx: Path,
    crfs: tuple[int, ...],
    codec: str,
    preset: str,
    dry_run: bool,
    vmaf_bin: str,
    ffmpeg_bin: str,
    pix_fmt: str,
    width: int | None,
    height: int | None,
    max_clips: int | None,
    report_dir: Path,
    delta_fast_override: float | None,
) -> int:
    """Run the calibration sweep and write output. Returns 0 on success."""
    # Collect YUV files.
    if corpus.is_dir():
        yuv_files = _find_yuv_files(corpus)
        corpus_label = str(corpus)
    else:
        _log.info("Corpus dir %s not found; using fallback YUVs from %s", corpus, _FALLBACK_YUV_DIR)
        yuv_files = sorted(_FALLBACK_YUV_DIR.glob(_FALLBACK_YUV_GLOB))
        corpus_label = str(_FALLBACK_YUV_DIR)

    if not yuv_files:
        _log.error("No .yuv files found under %s or fallback dir", corpus)
        return 1

    if max_clips is not None:
        yuv_files = yuv_files[:max_clips]

    _log.info("Found %d YUV files; CRF grid: %s", len(yuv_files), crfs)

    samples: list[CalibrationSample] = []

    with tempfile.TemporaryDirectory(prefix="calibrate_nr_") as tmp:
        tmpdir = Path(tmp)

        for yuv in yuv_files:
            # Detect geometry from filename or use caller-supplied values.
            geom = _detect_yuv_geometry(yuv) if (width is None or height is None) else None
            w = width if width is not None else (geom[0] if geom else None)
            h = height if height is not None else (geom[1] if geom else None)
            if w is None or h is None:
                _log.warning(
                    "Cannot determine geometry for %s; skip. Pass --width/--height.", yuv.name
                )
                continue

            for crf in crfs:
                _log.info("Processing %s @ CRF=%d (%dx%d)", yuv.name, crf, w, h)

                # Encode reference → encoded MKV.
                encoded = tmpdir / f"{yuv.stem}_crf{crf}.mkv"
                if not _encode_yuv(
                    yuv,
                    width=w,
                    height=h,
                    pix_fmt=pix_fmt,
                    codec=codec,
                    preset=preset,
                    crf=crf,
                    output=encoded,
                    ffmpeg_bin=ffmpeg_bin,
                ):
                    continue

                # Decode encoded → distorted YUV.
                dist_yuv = tmpdir / f"{yuv.stem}_crf{crf}_dist.yuv"
                if not _decode_to_yuv(encoded, dist_yuv, pix_fmt=pix_fmt, ffmpeg_bin=ffmpeg_bin):
                    continue

                # FR VMAF.
                fr = _run_fr_vmaf(
                    yuv,
                    dist_yuv,
                    width=w,
                    height=h,
                    pix_fmt=pix_fmt,
                    vmaf_bin=vmaf_bin,
                )
                if fr is None:
                    continue

                # NR VMAF proxy.
                nr = _run_nr_score(
                    dist_yuv,
                    width=w,
                    height=h,
                    pix_fmt=pix_fmt,
                    nr_model_path=model_onnx,
                )
                if nr is None:
                    continue

                samples.append(
                    CalibrationSample(yuv_name=yuv.name, crf=crf, nr_score=nr, fr_score=fr)
                )

    if not samples:
        _log.error("No calibration samples collected; check corpus and tool paths.")
        return 1

    # Fit linear regression.
    nr_vals = [s.nr_score for s in samples]
    fr_vals = [s.fr_score for s in samples]
    a, b, residuals = _linear_regression(nr_vals, fr_vals)
    sigma = math.sqrt(sum(r * r for r in residuals) / max(1, len(residuals) - 1))
    delta_fast = (
        delta_fast_override if delta_fast_override is not None else _compute_delta_fast(residuals)
    )
    plcc = _pearson_r(nr_vals, fr_vals)

    _log.info("Regression: vmaf_fr ≈ %.4f × vmaf_nr + %.4f", a, b)
    _log.info("PLCC=%.4f  σ=%.4f  δ_fast (2σ)=%.2f", plcc, sigma, delta_fast)

    # Write calibration report.
    report_path = _write_calibration_report(
        samples,
        a=a,
        b=b,
        sigma=sigma,
        delta_fast=delta_fast,
        plcc=plcc,
        n_clips=len({s.yuv_name for s in samples}),
        n_crfs=len(crfs),
        corpus_path=corpus_label,
        output_dir=report_dir,
    )
    print(f"Calibration report written to {report_path}")

    # Update model/tiny/nr_metric_v1.json.
    if not dry_run:
        try:
            existing = json.loads(model_json.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as exc:
            _log.error("Failed to read %s: %s", model_json, exc)
            return 1

        existing["calibration_threshold"] = round(delta_fast, 4)
        existing["calibration_plcc"] = round(plcc, 4)
        existing["calibration_sigma"] = round(sigma, 4)
        existing["calibration_date"] = date.today().isoformat()
        existing["calibration_n_samples"] = len(samples)

        model_json.write_text(
            json.dumps(existing, indent=2, ensure_ascii=False) + "\n",
            encoding="utf-8",
        )
        print(f"Updated calibration_threshold={delta_fast:.4f} in {model_json}")
    else:
        print(f"[dry-run] would set calibration_threshold={delta_fast:.4f} in {model_json}")

    return 0


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------


def _build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        description=(
            "Calibrate the δ_fast threshold for NR early-elimination "
            "(ADR-0615 / ADR-0624). "
            "Walks a YUV corpus, runs FR+NR scoring at a CRF grid, fits "
            "vmaf_fr ≈ f(vmaf_nr) linear regression, and writes "
            "calibration_threshold = 2σ to model/tiny/nr_metric_v1.json."
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p.add_argument(
        "--corpus",
        type=Path,
        default=_DEFAULT_CORPUS,
        help=(
            "directory of reference YUV files to sweep "
            f"(default: {_DEFAULT_CORPUS}). "
            "Falls back to python/test/resource/yuv/ when this dir is absent."
        ),
    )
    p.add_argument(
        "--output",
        type=Path,
        default=_DEFAULT_MODEL_JSON,
        metavar="JSON",
        help=(f"path to nr_metric_v1.json to update (default: {_DEFAULT_MODEL_JSON})"),
    )
    p.add_argument(
        "--onnx",
        type=Path,
        default=_DEFAULT_MODEL_ONNX,
        metavar="ONNX",
        help=f"path to nr_metric_v1.onnx (default: {_DEFAULT_MODEL_ONNX})",
    )
    p.add_argument(
        "--crfs",
        default=",".join(str(c) for c in _DEFAULT_CRFS),
        help=(
            f"comma-separated CRF values to sweep (default: {','.join(str(c) for c in _DEFAULT_CRFS)})"
        ),
    )
    p.add_argument(
        "--codec",
        default=_DEFAULT_CODEC,
        help=f"FFmpeg codec name (default: {_DEFAULT_CODEC})",
    )
    p.add_argument(
        "--preset",
        default=_DEFAULT_PRESET,
        help=f"encoder preset (default: {_DEFAULT_PRESET})",
    )
    p.add_argument(
        "--width",
        type=int,
        default=None,
        help="override frame width (default: auto-detect from filename)",
    )
    p.add_argument(
        "--height",
        type=int,
        default=None,
        help="override frame height (default: auto-detect from filename)",
    )
    p.add_argument(
        "--pix-fmt",
        default="yuv420p",
        dest="pix_fmt",
        help="pixel format (default: yuv420p)",
    )
    p.add_argument(
        "--max-clips",
        type=int,
        default=None,
        metavar="N",
        help="limit to the first N YUV files (useful for quick smoke runs)",
    )
    p.add_argument(
        "--vmaf-bin",
        default="vmaf",
        help="path to the vmaf binary (default: vmaf)",
    )
    p.add_argument(
        "--ffmpeg-bin",
        default="ffmpeg",
        help="path to ffmpeg (default: ffmpeg)",
    )
    p.add_argument(
        "--report-dir",
        type=Path,
        default=_REPO_ROOT / "docs" / "ai" / "models",
        metavar="DIR",
        help="directory for the calibration Markdown report (default: docs/ai/models/)",
    )
    p.add_argument(
        "--delta-fast",
        type=float,
        default=None,
        metavar="VMAF",
        dest="delta_fast_override",
        help=(
            "force a specific δ_fast value instead of fitting from residuals. "
            "The JSON is still updated; the regression is skipped."
        ),
    )
    p.add_argument(
        "--dry-run",
        action="store_true",
        default=False,
        help="compute δ_fast and write the report but do NOT update the JSON",
    )
    p.add_argument(
        "-v",
        "--verbose",
        action="store_true",
        default=False,
        help="verbose logging",
    )
    return p


def main(argv: list[str] | None = None) -> int:
    p = _build_parser()
    args = p.parse_args(argv)

    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(levelname)s %(message)s",
    )

    try:
        crfs: tuple[int, ...] = tuple(int(c.strip()) for c in args.crfs.split(",") if c.strip())
    except ValueError as exc:
        print(f"error: --crfs: {exc}", file=sys.stderr)
        return 2

    if not crfs:
        print("error: --crfs must not be empty", file=sys.stderr)
        return 2

    return calibrate(
        corpus=args.corpus,
        model_json=args.output,
        model_onnx=args.onnx,
        crfs=crfs,
        codec=args.codec,
        preset=args.preset,
        dry_run=args.dry_run,
        vmaf_bin=args.vmaf_bin,
        ffmpeg_bin=args.ffmpeg_bin,
        pix_fmt=args.pix_fmt,
        width=args.width,
        height=args.height,
        max_clips=args.max_clips,
        report_dir=args.report_dir,
        delta_fast_override=args.delta_fast_override,
    )


if __name__ == "__main__":
    sys.exit(main())
