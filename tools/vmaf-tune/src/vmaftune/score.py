# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""vmaf binary driver — Phase A.

Spawns the libvmaf CLI (`vmaf`) against a (reference YUV, distorted
encode) pair and parses the pooled VMAF score from the JSON output.

Subprocess boundary is the integration seam — tests mock subprocess.
"""

from __future__ import annotations

import dataclasses
import json
import re
import subprocess
import tempfile
import time
from pathlib import Path
from typing import Any

from . import CANONICAL6_FEATURES
from .defaultmodel import DEFAULT_MODEL

# Modern libvmaf wraps the integer-pipeline feature results under
# ``pooled_metrics`` keys prefixed with ``integer_``.  The canonical-6
# bare names used in CANONICAL6_FEATURES map to these prefixed keys:
#
#   adm2       → integer_adm2
#   vif_scale0 → integer_vif_scale0
#   vif_scale1 → integer_vif_scale1
#   vif_scale2 → integer_vif_scale2
#   vif_scale3 → integer_vif_scale3
#   motion2    → integer_motion2
#
# The mapping is consulted by ``parse_feature_aggregates``.  Any
# canonical name absent from this dict is looked up by the bare name
# (covers non-integer features such as ``cambi`` or future additions).
_CANONICAL_TO_POOLED_KEY: dict[str, str] = {
    "adm2": "integer_adm2",
    "vif_scale0": "integer_vif_scale0",
    "vif_scale1": "integer_vif_scale1",
    "vif_scale2": "integer_vif_scale2",
    "vif_scale3": "integer_vif_scale3",
    "motion2": "integer_motion2",
}


@dataclasses.dataclass(frozen=True)
class ScoreRequest:
    """Pair to score: reference YUV vs distorted encode.

    ``frame_skip_ref`` / ``frame_cnt`` mirror the libvmaf CLI flags
    (``--frame_skip_ref`` / ``--frame_cnt``). Sample-clip mode (ADR-0301)
    sets these so VMAF compares the same time window of the reference
    that was fed to the encoder, instead of slicing the reference YUV
    on disk. Both ``0`` (default) keeps the legacy full-source scoring.

    ``duration_s`` (added 2026-05-18, ADR-0498) gates the container -> raw
    YUV decode step that :func:`maybe_decode_distorted` performs before
    handing a path to the vmaf CLI. When set to a positive value, the
    decode is bounded with ffmpeg's ``-t`` so a 10-second probe against
    a 634-second source produces ~896 MB of raw YUV instead of ~58 GB
    (BBB e2e v2 Bug #v2-A). ``0.0`` (default) preserves the legacy
    full-source decode for callers that have not been updated yet.
    """

    reference: Path
    distorted: Path
    width: int
    height: int
    pix_fmt: str
    model: str = DEFAULT_MODEL
    frame_skip_ref: int = 0
    frame_cnt: int = 0
    duration_s: float = 0.0


@dataclasses.dataclass(frozen=True)
class ScoreResult:
    """Outcome of one scoring call.

    ``feature_means`` / ``feature_stds`` carry the canonical-6 libvmaf
    per-feature pooled aggregates parsed out of
    ``pooled_metrics.<feature>``: ``adm2``, ``vif_scale0..3``,
    ``motion2`` (see ``vmaftune.CANONICAL6_FEATURES``). Each feature key
    that libvmaf does not emit for the run (e.g. when a cambi-only
    model is selected) is absent from the dict — the corpus row writer
    fills the missing column with ``NaN`` rather than inventing a zero
    (ADR-0366).
    """

    request: ScoreRequest
    vmaf_score: float
    score_time_ms: float
    vmaf_binary_version: str
    exit_status: int
    stderr_tail: str
    feature_means: dict[str, float] = dataclasses.field(default_factory=dict)
    feature_stds: dict[str, float] = dataclasses.field(default_factory=dict)


_VMAF_VERSION_RE = re.compile(r"VMAF version[: ]+(\S+)")


def build_vmaf_command(
    req: ScoreRequest,
    json_output: Path,
    *,
    vmaf_bin: str = "vmaf",
    backend: str | None = None,
) -> list[str]:
    """Compose the libvmaf CLI argv. Pure function for test pinning.

    ``backend`` (when set) is forwarded as the libvmaf CLI's
    ``--backend NAME`` selector — values ``cpu`` / ``cuda`` / ``sycl``
    / ``hip`` per ADR-0299 (the Vulkan value was removed, ADR-0726). When
    ``None`` the flag is omitted so the libvmaf binary picks its own
    default (CPU on a stock build).
    """
    cmd = [
        vmaf_bin,
        "--reference",
        str(req.reference),
        "--distorted",
        str(req.distorted),
        "--width",
        str(req.width),
        "--height",
        str(req.height),
        "--pixel_format",
        _pixfmt_to_vmaf(req.pix_fmt),
        "--bitdepth",
        str(_bitdepth_for(req.pix_fmt)),
        "--model",
        _model_arg(req.model),
        "--json",
        "--output",
        str(json_output),
    ]
    if not _model_requests_vif(req.model):
        cmd.extend(["--feature", "vif"])
    if backend:
        cmd.extend(["--backend", backend])
    # Sample-clip mode (ADR-0301): align reference window with the
    # encoded slice so VMAF compares matching frames. The distorted is
    # already a clip-length encode, so no --frame_skip_dist is needed.
    if req.frame_skip_ref > 0:
        cmd.extend(["--frame_skip_ref", str(req.frame_skip_ref)])
    if req.frame_cnt > 0:
        cmd.extend(["--frame_cnt", str(req.frame_cnt)])
    return cmd


def _model_requests_vif(model: str) -> bool:
    """Return True if the model already requests VIF features natively.

    VMAF models in the v0.6 generation (e.g. vmaf_v0.6.1, vmaf_b_v0.6.3,
    vmaf_4k_v0.6.1, vmaf_v0.6.1neg) natively include vif_scale0..3.
    The default model since ADR-1168 (vmaf_v1.0.16_3d0h) and other v1
    models do not include VIF, so --feature vif must be passed
    explicitly to ensure canonical-6 features are populated.

    For custom JSON model files, if the file exists on disk and is
    readable, we inspect whether any feature name contains "vif".
    """
    if not model:
        return False
    val = model.split("=", 1)[1] if "=" in model else model
    if "v0.6" in val:
        return True
    target_path = Path(val)
    if target_path.is_file():
        try:
            with open(target_path, "r", encoding="utf-8") as f:
                data = json.load(f)
            feature_names = data.get("model_dict", {}).get("feature_names", [])
            return any("vif" in f.lower() for f in feature_names)
        except Exception:
            pass
    return False


model_requests_vif = _model_requests_vif


def _model_arg(model: str) -> str:
    """Format the ``--model`` argument for the libvmaf CLI.

    Accepts either a bare version identifier (``"vmaf_v0.6.1"``) or a
    pre-formatted ``key=value`` string (``"path=/abs/model.json"``,
    ``"version=vmaf_v0.6.1"``). Bare identifiers are wrapped as
    ``version=...``; pre-formatted strings pass through. Used by
    ``corpus.py`` to inject HDR-model paths (see ADR-0300).
    """
    if "=" in model:
        return model
    return f"version={model}"


def _pixfmt_to_vmaf(pix_fmt: str) -> str:
    """Map ffmpeg pix_fmt to libvmaf's --pixel_format vocabulary.

    Only the subset Phase A actually drives. Falls back to ``420``.
    """
    if pix_fmt.startswith("yuv422"):
        return "422"
    if pix_fmt.startswith("yuv444"):
        return "444"
    return "420"


def _bitdepth_for(pix_fmt: str) -> int:
    if "10le" in pix_fmt or "p10" in pix_fmt:
        return 10
    if "12le" in pix_fmt or "p12" in pix_fmt:
        return 12
    return 8


def parse_vmaf_json(payload: dict[str, Any]) -> float:
    """Pull the pooled VMAF score from libvmaf's JSON output.

    Tries the modern ``pooled_metrics.vmaf.mean`` shape first, falls
    back to the older top-level ``VMAF score``. Raises ``ValueError``
    if neither is present.
    """
    pooled = payload.get("pooled_metrics") or {}
    vmaf = pooled.get("vmaf") or {}
    if "mean" in vmaf:
        return float(vmaf["mean"])
    if "VMAF score" in payload:
        return float(payload["VMAF score"])
    raise ValueError("vmaf JSON missing pooled_metrics.vmaf.mean")


def _find_pooled_block(pooled: dict[str, Any], name: str, pooled_key: str) -> dict[str, Any] | None:
    """Return the pooled block for ``name``: integer_* key, bare name, then prefix."""
    block = pooled.get(pooled_key)
    if not isinstance(block, dict):
        # Synthetic payloads may use bare keys without integer_* prefixes.
        block = pooled.get(name)
    if isinstance(block, dict):
        return block
    # Options-suffixed keys (integer_adm2_csf_2_dlmw_0.7_..., integer_motion2_mmxv_18).
    prefixes = (f"{pooled_key}_", f"{name}_")
    for key, value in pooled.items():
        if isinstance(value, dict) and key.startswith(prefixes):
            return value
    return None


def _float_or_none(value: Any) -> float | None:
    """Return ``float(value)`` or ``None`` when it does not convert."""
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def _per_frame_mean(frames: list[Any], name: str, pooled_key: str) -> float | None:
    """Mean of a feature over per-frame metrics, ``None`` when no frame has it."""
    vals: list[float] = []
    for fr in frames:
        metrics = fr.get("metrics") or {}
        key = pooled_key if pooled_key in metrics else name
        if key in metrics:
            val = _float_or_none(metrics[key])
            if val is not None:
                vals.append(val)
    return sum(vals) / len(vals) if vals else None


def _store_block(
    name: str, block: dict[str, Any], means: dict[str, float], stds: dict[str, float]
) -> None:
    """Copy a pooled block's ``mean`` / ``stddev`` into the result dicts."""
    for key, dest in (("mean", means), ("stddev", stds)):
        val = _float_or_none(block[key]) if key in block else None
        if val is not None:
            dest[name] = val


def parse_feature_aggregates(
    payload: dict[str, Any], feature_names: tuple[str, ...]
) -> tuple[dict[str, float], dict[str, float]]:
    """Pull per-feature ``mean`` / ``stddev`` aggregates from libvmaf JSON.

    Each canonical bare name (``adm2``, ``vif_scale0``, ...) is resolved to
    its ``integer_*`` pooled key through ``_CANONICAL_TO_POOLED_KEY``, then
    the bare name, then an options-suffixed prefix match. ``stddev`` is
    guarded because real integer-pipeline blocks carry ``harmonic_mean``
    instead. Features absent from ``pooled_metrics`` fall back to the
    per-frame mean; features present nowhere are omitted (the corpus row
    writer turns absence into ``NaN``). The legacy top-level ``VMAF score``
    shape yields an empty aggregate set.
    """
    pooled = payload.get("pooled_metrics") or {}
    frames = payload.get("frames") or []
    means: dict[str, float] = {}
    stds: dict[str, float] = {}
    for name in feature_names:
        pooled_key = _CANONICAL_TO_POOLED_KEY.get(name, name)
        block = _find_pooled_block(pooled, name, pooled_key)
        if block is not None:
            _store_block(name, block, means, stds)
        elif frames:
            fmean = _per_frame_mean(frames, name, pooled_key)
            if fmean is not None:
                means[name] = fmean
    return means, stds


def _decode_to_raw_yuv(
    src: Path,
    dst: Path,
    *,
    pix_fmt: str,
    ffmpeg_bin: str = "ffmpeg",
    runner: object | None = None,
    duration_s: float | None = None,
) -> int:
    """Decode a container (mp4/mkv/…) to a raw planar YUV file for the vmaf CLI.

    The vmaf CLI only accepts ``.yuv`` / ``.y4m`` inputs. When the distorted
    encode is a container file (e.g. ``.mp4``) the caller must decode it first.
    Returns the ffmpeg exit code — non-zero signals a decode failure.

    ``duration_s``: optional ffmpeg ``-t`` clamp on the decoded output.
    When ``None`` or ``<= 0`` the full source is decoded (legacy
    behaviour). When set to a positive value the decoded YUV is bounded
    to ``duration_s`` seconds, preventing a 10-second probe against a
    634-second source from materialising tens of gigabytes of raw YUV
    that the score step never reads (BBB e2e v2 Bug #v2-A, ADR-0498).
    """
    runner_fn = runner or subprocess.run
    cmd = [
        ffmpeg_bin,
        "-y",
        "-hide_banner",
        "-loglevel",
        "error",
        "-i",
        str(src),
        "-f",
        "rawvideo",
        "-pix_fmt",
        pix_fmt,
    ]
    if duration_s is not None and duration_s > 0.0:
        # ``-t`` after ``-i`` clamps the output to the first N seconds.
        cmd.extend(["-t", f"{float(duration_s)}"])
    cmd.append(str(dst))
    completed = runner_fn(cmd, capture_output=True, text=True, check=False)  # type: ignore[operator]
    return int(getattr(completed, "returncode", 1))


# Suffixes the vmaf CLI accepts as raw YUV without a prior ffmpeg
# decode step. ADR-0499 / BBB e2e v3 Bug #V3-B: ``.y4m`` was previously
# listed here on the assumption the CLI auto-detects Y4M containers
# from the extension. It does not — vmaf-tune always passes
# ``--width`` / ``--height`` / ``--pixel_format`` / ``--bitdepth``
# (see :func:`build_vmaf_command`) which flips the libvmaf CLI's
# ``use_yuv`` flag (core/tools/cli_parse.c) and routes both inputs
# through ``raw_input_open``. Y4M files then trip the file-size
# mismatch guard inside ``raw_input_open``. The empty-suffix entry is
# kept for fixture trees that name raw YUV without a ``.yuv``
# extension — geometry is already pinned by the ``--width`` / etc.
# flags, so those inputs round-trip correctly.
VMAF_RAW_SUFFIXES: frozenset[str] = frozenset({".yuv", ""})


def maybe_decode_distorted(
    req: ScoreRequest,
    *,
    workdir: Path,
    ffmpeg_bin: str = "ffmpeg",
    runner: object | None = None,
) -> tuple[ScoreRequest, int]:
    """Decode ``req.distorted`` to raw YUV when it is a container file.

    Lifted shared helper — both ``corpus.iter_rows`` and ``bisect``
    funnel container-shaped encoder outputs (``.mkv`` / ``.mp4``)
    through here before invoking the vmaf CLI. The libvmaf binary only
    accepts raw ``.yuv`` / ``.y4m``; without this decode step it
    interprets the container bytes as raw planar samples and aborts
    with "file too small for declared geometry" (Bug #3 in the BBB
    end-to-end run on 2026-05-17).

    Returns ``(updated_request, returncode)``.

    - ``returncode == 0`` and the returned request points at a freshly-
      written raw YUV when the input was a container and the decode
      succeeded.
    - ``returncode == 0`` with the original request returned when the
      input was already raw (no work to do).
    - A non-zero ``returncode`` with the original request signals a
      decode failure; callers should treat the score step as failed
      rather than invoking the vmaf binary on an undecodable file.
    """
    if req.distorted.suffix.lower() in VMAF_RAW_SUFFIXES:
        return req, 0
    workdir.mkdir(parents=True, exist_ok=True)
    decoded = workdir / (req.distorted.stem + ".decoded.yuv")
    # Honour ``req.duration_s`` when set so we don't materialise a 58 GB
    # raw YUV when the caller only intends to score a 10-second window
    # (BBB e2e v2 Bug #v2-A).
    decode_duration = req.duration_s if req.duration_s > 0.0 else None
    rc = _decode_to_raw_yuv(
        req.distorted,
        decoded,
        pix_fmt=req.pix_fmt,
        ffmpeg_bin=ffmpeg_bin,
        runner=runner,
        duration_s=decode_duration,
    )
    if rc != 0 or not decoded.exists():
        return req, rc if rc != 0 else 1
    return dataclasses.replace(req, distorted=decoded), 0


def _read_score_payload(
    json_path: Path, rc: int
) -> tuple[int, float, dict[str, float], dict[str, float]]:
    """Parse the vmaf JSON at ``json_path``; returns ``(rc, score, means, stds)``."""
    score = float("nan")
    if rc != 0 or not json_path.exists():
        return rc, score, {}, {}
    try:
        with json_path.open("r", encoding="utf-8") as fh:
            payload = json.load(fh)
    except json.JSONDecodeError:
        # vmaf exited 0 but wrote corrupt/partial JSON (e.g. killed
        # mid-write): NaN score and a non-zero status, not a crash.
        return 65, score, {}, {}
    try:
        score = parse_vmaf_json(payload)
    except ValueError:
        rc = rc or 65
    # Per-feature aggregates are best-effort: a cambi-only model exposes no
    # ``adm2``; the corpus row writer fills missing entries with NaN.
    means, stds = parse_feature_aggregates(payload, CANONICAL6_FEATURES)
    return rc, score, means, stds


def _run_vmaf_once(
    req: ScoreRequest,
    json_path: Path,
    runner_fn: Any,
    vmaf_bin: str,
    backend: str | None,
) -> ScoreResult:
    """Run the vmaf command once and fold its output into a ScoreResult."""
    cmd = build_vmaf_command(req, json_path, vmaf_bin=vmaf_bin, backend=backend)
    started = time.monotonic()
    completed = runner_fn(cmd, capture_output=True, text=True, check=False)
    elapsed_ms = (time.monotonic() - started) * 1000.0
    stderr = getattr(completed, "stderr", "") or ""
    rc, score, means, stds = _read_score_payload(
        json_path, int(getattr(completed, "returncode", 1))
    )
    match = _VMAF_VERSION_RE.search(stderr)
    return ScoreResult(
        request=req,
        vmaf_score=score,
        score_time_ms=elapsed_ms,
        vmaf_binary_version=match.group(1) if match else "unknown",
        exit_status=rc,
        stderr_tail=stderr[-2048:],
        feature_means=means,
        feature_stds=stds,
    )


def run_score(
    req: ScoreRequest,
    *,
    vmaf_bin: str = "vmaf",
    runner: object | None = None,
    workdir: Path | None = None,
    backend: str | None = None,
) -> ScoreResult:
    """Drive the vmaf CLI for a single (ref, dist) pair.

    ``backend`` is forwarded to :func:`build_vmaf_command`; when ``None``
    no ``--backend`` flag is emitted (libvmaf picks its own default).

    The vmaf CLI only accepts raw ``.yuv`` / ``.y4m`` inputs. Callers that
    pass a container path (``mp4``, ``mkv``, etc.) as ``req.distorted`` must
    decode it to a raw YUV file first — see :func:`decode_distorted_container`
    in corpus.py for the corpus pipeline's decode step.
    """
    runner_fn = runner or subprocess.run
    if workdir is None:
        with tempfile.TemporaryDirectory() as tmp:
            return _run_vmaf_once(req, Path(tmp) / "vmaf.json", runner_fn, vmaf_bin, backend)
    workdir.mkdir(parents=True, exist_ok=True)
    return _run_vmaf_once(req, workdir / "vmaf.json", runner_fn, vmaf_bin, backend)
