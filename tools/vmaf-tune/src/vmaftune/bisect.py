# Copyright 2026 Lusoris and Claude (Anthropic)
# SPDX-License-Identifier: BSD-3-Clause-Plus-Patent
"""Phase B — target-VMAF bisect.

Given a (source, codec, target VMAF) triple, find the *largest* CRF
whose actual measured VMAF still meets the target. "Largest" because
higher CRF = lower bitrate at acceptable quality — that's the cost-
optimal point on the CRF axis.

The algorithm is the obvious one (matches the analytical-curve binary
search in :func:`vmaftune.predictor.pick_crf` but operates on real
encodes via the existing :mod:`vmaftune.encode` / :mod:`vmaftune.score`
seams):

1. Encode at the midpoint CRF of the current ``[lo, hi]`` window and
   score with libvmaf.
2. If measured VMAF >= target, the window narrows upward
   (try a higher CRF — we can compress harder).
3. Else the window narrows downward (we need higher quality).
4. Stop when the window collapses to a single CRF or after
   ``max_iterations``.

The midpoint rounds toward the **lower-quality** end of the window so
we never accept a CRF whose VMAF we have not actually measured: a
clean off-by-one safety net for the "best so far" record.

The bisect assumes monotone-decreasing VMAF in CRF for the (codec,
content) under test. Adjacent samples that violate this contract are
flagged via ``error`` rather than silently accepted; we never
fall back to a different search strategy because the AGENTS-pinned
invariant is "bisect requires monotonicity, hard error otherwise"
(see ``tools/vmaf-tune/AGENTS.md`` Phase B section). Real-world content
is monotone in CRF for every modern codec; pathological cases are
ours-to-fix in the encoder, not ours-to-paper-over here.

Subprocess boundary is the test seam: ``encode_runner`` and
``score_runner`` mirror the pattern from ``encode.run_encode`` /
``score.run_score`` so unit tests inject deterministic stubs.

Phase B is the production wiring the existing ``compare`` /
``recommend-saliency`` / ``predict`` / ``tune-per-shot`` / ``ladder``
subcommands have been stubbing out via the
``NotImplementedError("Phase B pending")`` placeholder predicate.
"""

from __future__ import annotations

import contextlib
import dataclasses
import math
import tempfile
from pathlib import Path
from typing import TYPE_CHECKING

from .codec_adapters import get_adapter
from .encode import EncodeRequest, bitrate_kbps, run_encode
from .score import VMAF_RAW_SUFFIXES, ScoreRequest, maybe_decode_distorted, run_score

if TYPE_CHECKING:
    from .compare import PredicateFn, RecommendResult


# Sentinel: a measured VMAF below this floor against a non-degenerate
# encode signals a sample failure, not a real low-quality result. We
# refuse to draw a monotonicity conclusion from such samples.
_VMAF_VALID_FLOOR: float = 0.0
_VMAF_VALID_CEIL: float = 100.0


@dataclasses.dataclass(frozen=True)
class BisectResult:
    """One bisect's best (CRF, VMAF, bitrate) tuple at a given target.

    Mirrors the shape of :class:`vmaftune.compare.RecommendResult` so
    a one-line adapter (:func:`make_bisect_predicate`) satisfies the
    ``compare.PredicateFn`` signature.

    ``ok=False`` carries a human-readable ``error`` string and leaves
    the numeric fields at sentinel values; downstream consumers
    (``compare`` ranking, ``ladder`` knee selection) skip such rows.
    """

    codec: str
    best_crf: int
    measured_vmaf: float
    bitrate_kbps: float
    encode_time_ms: float
    n_iterations: int
    encoder_version: str = ""
    ok: bool = True
    error: str = ""

    def to_recommend_result(self) -> "RecommendResult":
        """Project onto the ``compare.RecommendResult`` shape.

        Lazy import keeps the bisect module standalone — ``compare``
        imports ``bisect`` for production wiring; the reverse import
        only happens when callers explicitly ask for the projection.
        """
        from .compare import RecommendResult

        return RecommendResult(
            codec=self.codec,
            best_crf=self.best_crf,
            bitrate_kbps=self.bitrate_kbps,
            encode_time_ms=self.encode_time_ms,
            vmaf_score=self.measured_vmaf,
            encoder_version=self.encoder_version,
            ok=self.ok,
            error=self.error,
        )


def _failure(
    codec: str,
    error: str,
    *,
    n_iterations: int = 0,
    best_crf: int = -1,
    measured_vmaf: float = float("nan"),
    bitrate_kbps: float = float("nan"),
    encode_time_ms: float = float("nan"),
    encoder_version: str = "",
) -> BisectResult:
    return BisectResult(
        codec=codec,
        best_crf=best_crf,
        measured_vmaf=measured_vmaf,
        bitrate_kbps=bitrate_kbps,
        encode_time_ms=encode_time_ms,
        n_iterations=n_iterations,
        encoder_version=encoder_version,
        ok=False,
        error=error,
    )


def _midpoint_lower_quality(lo: int, hi: int) -> int:
    """Round toward the lower-quality (higher-CRF) end of the window.

    Higher CRF = lower quality. ``ceil((lo + hi) / 2)`` always picks
    the higher-CRF mid when the window is even-sized — that way the
    "best so far" we accept on a pass is the CRF we actually measured,
    never one we extrapolated to from an adjacent sample.
    """
    return (lo + hi + 1) // 2


def bisect_target_vmaf(
    src: Path,
    codec: str,
    target_vmaf: float,
    *,
    width: int,
    height: int,
    pix_fmt: str = "yuv420p",
    framerate: float = 24.0,
    duration_s: float = 0.0,
    sample_clip_seconds: float = 0.0,
    preset: str | None = None,
    crf_range: tuple[int, int] | None = None,
    max_iterations: int = 8,
    vmaf_model: str = "vmaf_v0.6.1",
    score_backend: str | None = None,
    encode_runner: object | None = None,
    score_runner: object | None = None,
    decode_runner: object | None = None,
    ffmpeg_bin: str = "ffmpeg",
    vmaf_bin: str = "vmaf",
    workdir: Path | None = None,
) -> BisectResult:
    """Find the largest CRF whose measured VMAF still meets ``target_vmaf``.

    Parameters
    ----------
    src
        Reference YUV. Geometry / pix_fmt / framerate / duration are
        passed via kwargs because the file does not self-describe.
    codec
        Codec adapter name (must exist in
        :mod:`vmaftune.codec_adapters`).
    target_vmaf
        Quality floor; the bisect returns the highest-CRF cell whose
        measured VMAF clears this.
    sample_clip_seconds
        Optional ADR-0301 centre-window sample clip. When positive and
        shorter than ``duration_s``, each iteration encodes only that
        window and scores against the matching reference frame window.
    crf_range
        ``(lo, hi)`` inclusive bound on the search domain. ``None``
        defaults to the codec adapter's ``quality_range`` (per
        ADR-0296 the adapter's range is the search-space boundary,
        not a user-input gate).
    max_iterations
        Hard cap on encode+score round-trips. The window halves each
        iteration so the asymptote is ``ceil(log2(hi - lo + 1))``;
        ``max_iterations`` short-circuits before that for paranoia.
    preset
        Preset name forwarded verbatim to the adapter. ``None`` picks
        the adapter's mid-range default (``"medium"`` for x264 /
        x265 / svtav1 today).
    encode_runner / score_runner
        Subprocess-runner stubs. Default to
        :func:`subprocess.run` via the underlying ``run_encode`` /
        ``run_score`` calls. Tests inject fakes; production callers
        leave them ``None``.
    workdir
        Where the per-iteration encoded outputs live. ``None`` uses a
        :class:`tempfile.TemporaryDirectory` cleaned at exit.

    Returns
    -------
    BisectResult
        The best-so-far (CRF, VMAF, bitrate) tuple. ``ok=False`` when
        the target is unreachable in the given window or the
        monotonicity assumption fails.
    """
    try:
        adapter = get_adapter(codec)
    except KeyError as exc:
        return _failure(codec, f"unknown codec: {exc}")

    lo, hi = crf_range if crf_range is not None else adapter.quality_range
    lo = int(lo)
    hi = int(hi)
    if lo > hi:
        return _failure(codec, f"invalid crf_range: lo={lo} > hi={hi}")

    if max_iterations <= 0:
        return _failure(codec, f"max_iterations must be >= 1, got {max_iterations}")

    chosen_preset = preset if preset is not None else _default_preset(adapter)

    if workdir is None:
        workdir_ctx = tempfile.TemporaryDirectory()
        workdir_path = Path(workdir_ctx.name)
    else:
        workdir_ctx = None
        workdir_path = Path(workdir)
        workdir_path.mkdir(parents=True, exist_ok=True)

    # State across iterations:
    best: BisectResult | None = None
    last_vmaf_at_crf: dict[int, float] = {}
    n_iterations = 0
    cur_lo, cur_hi = lo, hi

    try:
        while cur_lo <= cur_hi and n_iterations < max_iterations:
            mid = _midpoint_lower_quality(cur_lo, cur_hi)
            n_iterations += 1

            sample = _encode_and_score(
                src=src,
                codec=codec,
                adapter=adapter,
                preset=chosen_preset,
                crf=mid,
                width=width,
                height=height,
                pix_fmt=pix_fmt,
                framerate=framerate,
                duration_s=duration_s,
                sample_clip_seconds=sample_clip_seconds,
                vmaf_model=vmaf_model,
                score_backend=score_backend,
                encode_runner=encode_runner,
                score_runner=score_runner,
                decode_runner=decode_runner,
                ffmpeg_bin=ffmpeg_bin,
                vmaf_bin=vmaf_bin,
                workdir=workdir_path,
            )
            if not sample.ok:
                return dataclasses.replace(sample, n_iterations=n_iterations)

            mono_err = _detect_monotonicity_violation(last_vmaf_at_crf, mid, sample.measured_vmaf)
            last_vmaf_at_crf[mid] = sample.measured_vmaf
            if mono_err is not None:
                return _failure(
                    codec,
                    mono_err,
                    n_iterations=n_iterations,
                    best_crf=best.best_crf if best is not None else -1,
                    measured_vmaf=best.measured_vmaf if best is not None else float("nan"),
                    bitrate_kbps=best.bitrate_kbps if best is not None else float("nan"),
                    encode_time_ms=sample.encode_time_ms,
                    encoder_version=sample.encoder_version,
                )

            if sample.measured_vmaf >= target_vmaf:
                # We met quality at this CRF — record it as best-so-far
                # and try harder compression next.
                best = dataclasses.replace(sample, n_iterations=n_iterations)
                cur_lo = mid + 1
            else:
                # Quality miss — narrow toward higher quality.
                cur_hi = mid - 1

        if best is None:
            # Target unreachable in the searched window.
            return _failure(
                codec,
                (
                    f"target VMAF {target_vmaf:g} unreachable in CRF window "
                    f"[{lo}, {hi}] after {n_iterations} iterations "
                    f"(best sample: {_describe_best_miss(last_vmaf_at_crf)})"
                ),
                n_iterations=n_iterations,
            )

        return best
    finally:
        if workdir_ctx is not None:
            workdir_ctx.cleanup()


def _default_preset(adapter: object) -> str:
    """Return the adapter's mid-range preset.

    The codec-adapter contract names ``"medium"`` for the canonical
    cross-codec sweep axis (see AGENTS.md "Adapter preset vocabulary"),
    so we prefer that when the adapter advertises it; otherwise we
    pick the middle of the ``presets`` tuple.
    """
    presets = getattr(adapter, "presets", None)
    if not presets:
        return "medium"
    if "medium" in presets:
        return "medium"
    return presets[len(presets) // 2]


def _detect_monotonicity_violation(
    history: dict[int, float],
    new_crf: int,
    new_vmaf: float,
) -> str | None:
    """Detect a 2-sample violation of monotone-decreasing VMAF in CRF.

    Returns ``None`` when consistent; a human-readable error string
    when at least one prior sample directly contradicts the new one
    by more than a small float-noise tolerance.
    """
    tol = 0.5  # VMAF units — looser than measurement noise on a single shot
    for crf, vmaf in history.items():
        if crf < new_crf and new_vmaf > vmaf + tol:
            return (
                f"monotonicity violation: VMAF rose from {vmaf:.2f} at CRF {crf} "
                f"to {new_vmaf:.2f} at CRF {new_crf} (expected non-increasing)"
            )
        if crf > new_crf and new_vmaf < vmaf - tol:
            return (
                f"monotonicity violation: VMAF fell from {vmaf:.2f} at CRF {crf} "
                f"to {new_vmaf:.2f} at CRF {new_crf} (expected non-decreasing for lower CRF)"
            )
    return None


def _describe_best_miss(history: dict[int, float]) -> str:
    if not history:
        return "no samples recorded"
    crf, vmaf = max(history.items(), key=lambda kv: kv[1])
    return f"closest miss VMAF={vmaf:.2f} at CRF {crf}"


def _sample_clip_window(
    *,
    duration_s: float,
    sample_clip_seconds: float,
    framerate: float,
) -> tuple[float, float, int, int]:
    """Return encode/score alignment knobs for ADR-0301 sample clips."""
    sample_s = float(sample_clip_seconds)
    duration = float(duration_s)
    fps = float(framerate)
    if sample_s <= 0.0 or duration <= 0.0 or sample_s >= duration or fps <= 0.0:
        return 0.0, 0.0, 0, 0
    clip_s = sample_s
    start_s = max(0.0, (duration - clip_s) / 2.0)
    frame_skip_ref = max(0, int(round(start_s * fps)))
    frame_cnt = max(1, int(round(clip_s * fps)))
    return start_s, clip_s, frame_skip_ref, frame_cnt


def _encode_and_score(
    *,
    src: Path,
    codec: str,
    adapter: object,
    preset: str,
    crf: int,
    width: int,
    height: int,
    pix_fmt: str,
    framerate: float,
    duration_s: float,
    sample_clip_seconds: float,
    vmaf_model: str,
    score_backend: str | None,
    encode_runner: object | None,
    score_runner: object | None,
    ffmpeg_bin: str,
    vmaf_bin: str,
    workdir: Path,
    decode_runner: object | None = None,
) -> BisectResult:
    """One encode+score round-trip — returns a sample-shaped BisectResult.

    The ``n_iterations`` field on the returned struct is always ``0``;
    the caller stamps it with the cumulative count.
    """
    try:
        adapter.validate(preset, crf)  # type: ignore[attr-defined]
    except (ValueError, AttributeError) as exc:
        return _failure(codec, f"adapter rejected (preset={preset!r}, crf={crf}): {exc}")

    out_path = workdir / f"bisect_{codec}_{preset}_{crf}.mkv"
    encoder_name = getattr(adapter, "encoder", codec)
    sample_start_s, sample_duration_s, frame_skip_ref, frame_cnt = _sample_clip_window(
        duration_s=duration_s,
        sample_clip_seconds=sample_clip_seconds,
        framerate=framerate,
    )
    # Bug #1: When the reference source is a container (mp4/mkv/…) the
    # encoder ffmpeg invocation must NOT prepend ``-f rawvideo`` —
    # otherwise ffmpeg tries to parse the demuxed container as raw YUV
    # and produces "Output file is empty". Autodetect via the same
    # suffix table that the post-encode decode step uses.
    src_is_container = Path(src).suffix.lower() not in VMAF_RAW_SUFFIXES
    enc_req = EncodeRequest(
        source=Path(src),
        width=int(width),
        height=int(height),
        pix_fmt=pix_fmt,
        framerate=float(framerate),
        encoder=encoder_name,
        preset=preset,
        crf=int(crf),
        output=out_path,
        sample_clip_seconds=sample_duration_s,
        sample_clip_start_s=sample_start_s,
        source_is_container=src_is_container,
    )
    enc_res = run_encode(enc_req, ffmpeg_bin=ffmpeg_bin, runner=encode_runner)
    if enc_res.exit_status != 0:
        return _failure(
            codec,
            f"encode failed at CRF {crf} (exit={enc_res.exit_status}): "
            f"{enc_res.stderr_tail.strip().splitlines()[-1] if enc_res.stderr_tail else 'no stderr'}",
            encode_time_ms=enc_res.encode_time_ms,
            encoder_version=enc_res.encoder_version,
        )

    # Bug #3: The libvmaf CLI only accepts raw .yuv / .y4m. The
    # encoded artefact is a Matroska container; without this decode
    # step the vmaf binary mis-parses it as raw YUV and aborts with
    # "file too small for declared geometry". We also need a raw YUV
    # reference for the same reason — if ``src`` is a container, the
    # caller cannot have decoded it (bisect is responsible for the
    # full round trip), so decode it once into the workdir.
    # ``decode_runner`` defaults to the encode runner: both are
    # ffmpeg invocations, so production callers (which leave both
    # ``None``) get the real ``subprocess.run`` either way, while
    # tests can keep injecting a single stub.
    effective_decode_runner = decode_runner if decode_runner is not None else encode_runner

    ref_for_score = Path(src)
    decoded_ref: Path | None = None
    if src_is_container:
        from .score import _decode_to_raw_yuv  # noqa: PLC0415 — module-local helper

        decoded_ref = workdir / (Path(src).stem + ".ref.decoded.yuv")
        # Re-use across iterations within the same bisect — workdir
        # persists for the bisect's lifetime so a single decode is
        # enough (every iteration scores the same reference).
        rc = 0
        if not decoded_ref.exists():
            rc = _decode_to_raw_yuv(
                Path(src),
                decoded_ref,
                pix_fmt=pix_fmt,
                ffmpeg_bin=ffmpeg_bin,
                runner=effective_decode_runner,
            )
        if rc != 0 or not decoded_ref.exists():
            return _failure(
                codec,
                f"reference decode to raw YUV failed (rc={rc}) for {src}",
                encode_time_ms=enc_res.encode_time_ms,
                encoder_version=enc_res.encoder_version,
            )
        ref_for_score = decoded_ref

    score_req = ScoreRequest(
        reference=ref_for_score,
        distorted=out_path,
        width=int(width),
        height=int(height),
        pix_fmt=pix_fmt,
        model=vmaf_model,
        frame_skip_ref=frame_skip_ref,
        frame_cnt=frame_cnt,
    )
    # Decode the encoded container to raw YUV — libvmaf will not accept
    # the .mkv otherwise. ``maybe_decode_distorted`` is a no-op for raw
    # outputs, so callers that wire a custom encoder that emits .yuv
    # directly are unaffected.
    score_req, decode_rc = maybe_decode_distorted(
        score_req,
        workdir=workdir,
        ffmpeg_bin=ffmpeg_bin,
        runner=effective_decode_runner,
    )
    if decode_rc != 0:
        with contextlib.suppress(OSError):
            if out_path.exists():
                out_path.unlink()
        return _failure(
            codec,
            f"distorted decode to raw YUV failed (rc={decode_rc}) at CRF {crf}",
            encode_time_ms=enc_res.encode_time_ms,
            encoder_version=enc_res.encoder_version,
        )

    score_res = run_score(
        score_req,
        vmaf_bin=vmaf_bin,
        runner=score_runner,
        backend=score_backend,
    )

    # Best-effort cleanup: the encoded artefact + per-iteration decoded
    # sidecar are throwaway; we keep the workdir alive across
    # iterations so a caller-supplied workdir can still inspect it
    # later (the temp-dir path cleans on context exit instead).
    with contextlib.suppress(OSError):
        if out_path.exists():
            out_path.unlink()
        if score_req.distorted != out_path and score_req.distorted.exists():
            score_req.distorted.unlink()

    if score_res.exit_status != 0:
        return _failure(
            codec,
            f"score failed at CRF {crf} (exit={score_res.exit_status})",
            encode_time_ms=enc_res.encode_time_ms,
            encoder_version=enc_res.encoder_version,
        )

    measured = float(score_res.vmaf_score)
    if math.isnan(measured) or measured < _VMAF_VALID_FLOOR or measured > _VMAF_VALID_CEIL:
        return _failure(
            codec,
            f"score returned out-of-range VMAF {measured!r} at CRF {crf}",
            encode_time_ms=enc_res.encode_time_ms,
            encoder_version=enc_res.encoder_version,
        )

    bitrate_duration_s = sample_duration_s if sample_duration_s > 0.0 else duration_s
    br_kbps = bitrate_kbps(enc_res.encode_size_bytes, bitrate_duration_s)

    return BisectResult(
        codec=codec,
        best_crf=int(crf),
        measured_vmaf=measured,
        bitrate_kbps=br_kbps,
        encode_time_ms=enc_res.encode_time_ms,
        n_iterations=0,
        encoder_version=enc_res.encoder_version,
        ok=True,
        error="",
    )


def make_bisect_predicate(
    target_vmaf: float,
    *,
    width: int,
    height: int,
    pix_fmt: str = "yuv420p",
    framerate: float = 24.0,
    duration_s: float = 0.0,
    sample_clip_seconds: float = 0.0,
    preset: str | None = None,
    crf_range: tuple[int, int] | None = None,
    max_iterations: int = 8,
    vmaf_model: str = "vmaf_v0.6.1",
    score_backend: str | None = None,
    encode_runner: object | None = None,
    score_runner: object | None = None,
    decode_runner: object | None = None,
    ffmpeg_bin: str = "ffmpeg",
    vmaf_bin: str = "vmaf",
    workdir: Path | None = None,
) -> "PredicateFn":
    """Return a :data:`compare.PredicateFn` that closes over bisect knobs.

    The returned callable matches ``compare.compare_codecs``'s
    predicate signature ``(codec, src, target_vmaf) -> RecommendResult``.
    The ``target_vmaf`` argument the predicate receives at call time
    is forwarded through verbatim; the closure-time ``target_vmaf``
    here serves as the default for callers that pin one floor across
    many comparisons.

    Note ``target_vmaf`` appears at both layers because the predicate
    signature exposes a target argument (so the same predicate may be
    re-used with shifting targets) but encode geometry / runners must
    be fixed before the predicate is built.
    """

    def _predicate(codec: str, src: Path, runtime_target_vmaf: float) -> "RecommendResult":
        # Runtime target argument wins; closure-time default is unused
        # whenever ``compare_codecs`` calls us (it always supplies the
        # current target). We keep the closure default for callers that
        # bind the predicate directly without ``compare_codecs``.
        target = (
            runtime_target_vmaf
            if not (runtime_target_vmaf is None or math.isnan(runtime_target_vmaf))
            else target_vmaf
        )
        result = bisect_target_vmaf(
            src,
            codec,
            float(target),
            width=width,
            height=height,
            pix_fmt=pix_fmt,
            framerate=framerate,
            duration_s=duration_s,
            sample_clip_seconds=sample_clip_seconds,
            preset=preset,
            crf_range=crf_range,
            max_iterations=max_iterations,
            vmaf_model=vmaf_model,
            score_backend=score_backend,
            encode_runner=encode_runner,
            score_runner=score_runner,
            decode_runner=decode_runner,
            ffmpeg_bin=ffmpeg_bin,
            vmaf_bin=vmaf_bin,
            workdir=workdir,
        )
        return result.to_recommend_result()

    return _predicate


__all__ = [
    "BisectResult",
    "bisect_target_vmaf",
    "make_bisect_predicate",
]
