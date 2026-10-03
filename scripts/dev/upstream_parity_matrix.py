#!/usr/bin/env python3
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""What the upstream parity guard runs: fixtures, extractors, options, models.

Data and the functions that enumerate it; ``scripts/dev/upstream_parity.py``
executes it. Two sizes:

``probe``  every shared extractor at its default options on the small and
           medium fixtures, the option variants on one fixture, a few models.
           Seconds to a minute; ``make upstream-parity``.
``full``   every fixture, every option variant on five fixtures, every model
           file upstream ships. About half an hour at eight workers;
           ``make upstream-parity-full``.

Fixtures come from three places. The three Netflix golden pairs
(``python/test/resource/yuv``, fetched by ``scripts/test/fetch-test-yuvs.sh``)
are required. Clips derived from the 576x324 pair by
``scripts/dev/upstream_parity.py`` need nothing else: crops down to 8x8, odd
sizes, 4:2:2, 4:4:4 and 4:0:0 forms, 10- and 16-bit forms, full-range noise.
The other Netflix clips and the 4K pair under ``testdata/bbb`` are optional:
a fixture whose file is absent is skipped with a printed reason, never
counted as compared.
"""

from __future__ import annotations

import dataclasses

NETFLIX = "netflix"
BBB = "bbb"
DERIVED = "derived"

SRC01 = "src01_hrc00_576x324"
DIS01 = "src01_hrc01_576x324"
AKIYO = "p_vmaf_hacking_investigation_0_0_akiyo_cif_notyuv_0to0_identity_vs_akiyo_cif_notyuv_0to0_multiply_q_"
Q160 = "_test_0_1_src01_hrc00_576x324_576x324_vs_src01_hrc01_576x324_576x324_q_160x90.yuv"


@dataclasses.dataclass(frozen=True)
class Fixture:
    """A reference / distorted pair of raw planar files.

    ``recipe`` is empty for a file that exists as it is; for a derived fixture
    it names the generator and its arguments (``upstream_parity.derive()``).
    """

    name: str
    source: str
    ref: str
    dis: str
    w: int
    h: int
    pix: str
    bpc: int
    frames: int
    recipe: tuple[str, ...] = ()
    probe: bool = False
    required: bool = False


SD = (576, 324)
HD = (1920, 1080)


def _netflix(
    name: str,
    files: tuple[str, str],
    size: tuple[int, int],
    layout: tuple[str, int, int],
    probe: bool = False,
    required: bool = False,
) -> Fixture:
    """A Netflix clip pair: *layout* is (pixel format, bits, frames)."""

    pix, bpc, frames = layout
    return Fixture(name, NETFLIX, *files, *size, pix, bpc, frames, (), probe, required)


def _derived(
    name: str,
    size: tuple[int, int],
    layout: tuple[str, int, int],
    recipe: tuple[str, ...],
    probe: bool = False,
) -> Fixture:
    """A pair written by ``upstream_parity.derive()`` from *recipe*."""

    pix, bpc, frames = layout
    files = (f"{name}_ref.yuv", f"{name}_dis.yuv")
    return Fixture(name, DERIVED, *files, *size, pix, bpc, frames, recipe, probe)


def _src01(suffix: str) -> tuple[str, str]:
    return f"{SRC01}{suffix}", f"{DIS01}{suffix}"


CHECKER = "checkerboard_1920_1080_10_3_"
SPARKS = ("sparks_ref_480x270.yuv42010le.yuv", "sparks_dis_480x270.yuv42010le.yuv")
BBB_FILES = ("ref_3840x2160_200f.yuv", "dis_3840x2160_200f.yuv")
CROP = ("crop420", "nflx8")

FIXTURES: tuple[Fixture, ...] = (
    # The three Netflix golden pairs: required.
    _netflix("nflx8", _src01(".yuv"), SD, ("420", 8, 48), True, True),
    _netflix("cb1", (f"{CHECKER}0_0.yuv", f"{CHECKER}1_0.yuv"), HD, ("420", 8, 3), True, True),
    _netflix("cb10", (f"{CHECKER}0_0.yuv", f"{CHECKER}10_0.yuv"), HD, ("420", 8, 3), True, True),
    # Other Netflix clips and the 4K pair: compared when present.
    _netflix("nflx10", _src01(".yuv420p10le.yuv"), SD, ("420", 10, 3), True),
    _netflix("nflx12", _src01(".yuv420p12le.yuv"), SD, ("420", 12, 3)),
    _netflix("nflx16", _src01(".yuv420p16le.yuv"), SD, ("420", 16, 3)),
    _netflix("nflx422p10", _src01(".yuv422p10le.yuv"), SD, ("422", 10, 48), True),
    _netflix("sparks10", SPARKS, (480, 270), ("420", 10, 5)),
    _netflix("q160x90", (f"ref{Q160}", f"dis{Q160}"), (160, 90), ("420", 8, 48), True),
    _netflix("akiyo352", (f"ref{AKIYO}352x288", f"dis{AKIYO}352x288"), (352, 288), ("420", 8, 1)),
    _netflix("akiyo18x22", (f"ref{AKIYO}18x22", f"dis{AKIYO}18x22"), (18, 22), ("420", 8, 1), True),
    Fixture("bbb4k", BBB, *BBB_FILES, 3840, 2160, "420", 8, 6),
    # Derived from the 576x324 pair (or generated): always available.
    _derived("noise8", SD, ("420", 8, 3), ("noise",), True),
    _derived("noise10", SD, ("420", 10, 3), ("noise",)),
    _derived("noise12", SD, ("420", 12, 3), ("noise",)),
    _derived("noise16", SD, ("420", 16, 3), ("noise",)),
    _derived("nflx422", SD, ("422", 8, 8), ("to422", "nflx8"), True),
    _derived("nflx444", SD, ("444", 8, 8), ("to444", "nflx8")),
    _derived("nflx444p12", SD, ("444", 12, 3), ("to444", "nflx12")),
    _derived("nflx400", SD, ("400", 8, 8), ("to400", "nflx8"), True),
    _derived("nflx10w", SD, ("420", 10, 3), ("widen", "nflx8")),
    _derived("nflx16w", SD, ("420", 16, 3), ("widen", "nflx8"), True),
    _derived("odd444", (573, 163), ("444", 8, 8), ("crop444", "nflx444")),
    _derived("s256x144", (256, 144), ("420", 8, 3), CROP),
    _derived("s24x24", (24, 24), ("420", 8, 3), CROP, True),
    _derived("s19x19", (19, 19), ("420", 8, 3), CROP, True),
    _derived("s18x22", (18, 22), ("420", 8, 2), CROP, True),
    _derived("s17x17", (17, 17), ("420", 8, 2), CROP, True),
    _derived("s16x16", (16, 16), ("420", 8, 2), CROP, True),
    _derived("s12x9", (12, 9), ("420", 8, 2), CROP, True),
    _derived("s8x8", (8, 8), ("420", 8, 2), CROP, True),
)

# Fixtures the option variants and the models run on in the full matrix.
OPTION_FIXTURES = ("nflx8", "nflx10", "noise8", "q160x90", "cb1")
MODEL_FIXTURES = ("nflx8", "nflx10", "nflx422p10", "cb1", "cb10", "noise8", "q160x90", "bbb4k")
# ... and in the probe set.
PROBE_OPTION_FIXTURES = ("nflx8", "cb1", "noise8")
PROBE_MODEL_FIXTURES = ("nflx8", "cb1", "noise8")

# Extractors both trees register, with the default request and, where
# `debug` only adds metrics, the debug request. `float_ansnr` (upstream only)
# and the extractors only this tree has are not compared.
DEFAULT_SPECS: dict[str, tuple[str, ...]] = {
    "float_psnr": ("",),
    "float_adm": ("", "debug=true"),
    "float_vif": ("", "debug=true"),
    "float_motion": ("", "debug=false"),
    "float_moment": ("",),
    "speed_chroma": ("",),
    "speed_temporal": ("",),
    "float_ms_ssim": ("",),
    "float_ssim": ("",),
    "ciede": ("",),
    "psnr": ("",),
    "psnr_hvs": ("",),
    "adm": ("", "debug=true"),
    "motion": ("", "debug=true"),
    "vif": ("", "debug=true"),
    "cambi": ("",),
}


def _debug(options: tuple[str, ...]) -> tuple[str, ...]:
    return tuple(f"debug=true:{option}" for option in options)


_ADM_COMBO = "adm_dlm_weight=0.7:adm_enhn_gain_limit=1.0:adm_noise_weight=0.02"
_ADM_COMBO_TAIL = "adm_ref_display_height=1080:adm_min_val=0.5:adm_csf_mode=2"

_FLOAT_ADM = _debug(
    (
        "adm_enhn_gain_limit=1.0",
        "adm_enhn_gain_limit=1.2",
        "adm_norm_view_dist=1.5",
        "adm_norm_view_dist=5.0:adm_ref_display_height=2160",
        "adm_ref_display_height=2160",
        "adm_noise_weight=0.02",
        "adm_noise_weight=0.5",
        "adm_csf_scale=2.0",
        "adm_csf_diag_scale=0.5",
        *(f"adm_csf_mode={mode}" for mode in range(1, 10)),
        "adm_bypass_cm=1",
        "adm_adm3_apply_hm=true",
        "adm_p_norm=2.0",
        "adm_p_norm=4.5",
        "adm_dlm_weight=0.7",
        "adm_min_val=0.5",
        *(f"adm_f{band}s{scale}=0.5" for band in (1, 2) for scale in range(4)),
        "adm_skip_aim_scale=0",
        "adm_skip_aim_scale=2",
        "adm_skip_scale0=true",
        f"{_ADM_COMBO}:adm_norm_view_dist=3.0:{_ADM_COMBO_TAIL}",
    )
)

_FLOAT_VIF = _debug(
    (
        "vif_enhn_gain_limit=1.0",
        "vif_enhn_gain_limit=1.5",
        "vif_kernelscale=0.5",
        "vif_kernelscale=1.5",
        "vif_kernelscale=2.0",
        "vif_kernelscale=0.6666667",
        "vif_kernelscale=2.4",
        "vif_prescale=0.5",
        "vif_prescale=0.5:vif_prescale_method=bilinear",
        "vif_prescale=0.5:vif_prescale_method=bicubic",
        "vif_prescale=0.3333:vif_prescale_method=lanczos4",
        "vif_prescale=2.0:vif_prescale_method=bilinear",
        "vif_scale1_min_val=0.9",
        "vif_scale2_min_val=0.99",
        "vif_scale3_min_val=0.99",
        "vif_sigma_nsq=1.0",
        "vif_sigma_nsq=3.5",
        "vif_sigma_nsq=0.0",
        "vif_skip_scale0=true",
    )
)

_FLOAT_MOTION = (
    "motion_force_zero=true",
    "motion_fps_weight=0.5",
    "motion_blend_factor=0.5",
    "motion_blend_factor=0.5:motion_blend_offset=3.0",
    "motion_add_scale1=true",
    "motion_filter_size=1",
    "motion_filter_size=3",
    "motion_add_uv=true",
    "motion_max_val=2.0",
    "motion_add_scale1=true:motion_add_uv=true:motion_filter_size=3",
)

_SPEED_COMBO = "speed_nn_floor=0.1:speed_sigma_nn=0.19:speed_weight_var_mode=5"

_SPEED_CHROMA = (
    "speed_kernelscale=0.5",
    "speed_kernelscale=1.5",
    "speed_kernelscale=2.0",
    "speed_prescale=0.5",
    "speed_prescale=0.5:speed_prescale_method=bilinear",
    "speed_prescale=0.6:speed_prescale_method=bilinear",
    "speed_prescale=0.5:speed_prescale_method=bicubic",
    "speed_prescale=2.0:speed_prescale_method=lanczos4",
    "speed_sigma_nn=0.19",
    "speed_sigma_nn=1.0",
    "speed_nn_floor=0.1",
    "speed_max_val=3.0",
    *(f"speed_weight_var_mode={mode}" for mode in range(1, 7)),
    f"{_SPEED_COMBO}:speed_max_val=45.0",
    f"{_SPEED_COMBO}:speed_prescale=0.5:speed_prescale_method=bilinear:speed_max_val=45.0",
)

_SPEED_TEMPORAL = (
    *(option for option in _SPEED_CHROMA if "weight_var" not in option),
    "speed_use_ref_diff=true",
)

_ADM = _debug(
    (
        "adm_csf_scale=2.0",
        "adm_csf_diag_scale=0.5",
        "adm_dlm_weight=0.7",
        "adm_enhn_gain_limit=1.0",
        "adm_enhn_gain_limit=1.2",
        "adm_norm_view_dist=1.5",
        "adm_norm_view_dist=5.0",
        "adm_ref_display_height=2160",
        "adm_csf_mode=1",
        "adm_csf_mode=2",
        "adm_csf_mode=3",
        "adm_noise_weight=0.02",
        "adm_noise_weight=0.5",
        "adm_skip_aim=true",
        "adm_skip_scale0=true",
        "adm_min_val=0.5",
        f"{_ADM_COMBO}:adm_norm_view_dist=1.5:adm_ref_display_height=2160:adm_min_val=0.5:adm_csf_mode=2",
        f"{_ADM_COMBO}:adm_norm_view_dist=3.0:{_ADM_COMBO_TAIL}",
        f"{_ADM_COMBO}:adm_norm_view_dist=5.0:{_ADM_COMBO_TAIL}",
        "adm_csf_mode=1:adm_norm_view_dist=1.5:adm_ref_display_height=2160",
    )
)

_MOTION = _debug(
    (
        "motion_force_zero=true",
        "motion_blend_factor=0.5",
        "motion_blend_factor=0.5:motion_blend_offset=3.0",
        "motion_fps_weight=0.5",
        "motion_max_val=2.0",
        "motion_max_val=18.0",
        "motion_five_frame_window=true",
        "motion_moving_average=true",
        "motion_max_val=18.0:motion_five_frame_window=true:motion_moving_average=true",
    )
)

_CAMBI = (
    "cambi_max_val=0.5",
    "enc_width=384:enc_height=216",
    "enc_bitdepth=8",
    "enc_bitdepth=10",
    "window_size=31",
    "window_size=127",
    "topk=0.3",
    "cambi_topk=0.3",
    "tvi_threshold=0.05",
    "cambi_vis_lum_threshold=0.06",
    "cambi_vis_lum_threshold=5.0",
    "max_log_contrast=0",
    "max_log_contrast=3",
    "max_log_contrast=5",
    "full_ref=true",
    "full_ref=true:src_width=576:src_height=324",
    "eotf=pq",
    "cambi_eotf=pq",
    "cambi_high_res_speedup=1080",
    "cambi_high_res_speedup=2160",
    "cambi_high_res_speedup=1080:cambi_vis_lum_threshold=0.06:cambi_max_val=17.0",
)

OPTION_SPECS: dict[str, tuple[str, ...]] = {
    "float_adm": _FLOAT_ADM,
    "float_vif": _FLOAT_VIF,
    "float_motion": _FLOAT_MOTION,
    "speed_chroma": _SPEED_CHROMA,
    "speed_temporal": _SPEED_TEMPORAL,
    "float_ms_ssim": (
        "enable_lcs=true",
        "enable_db=true",
        "enable_db=true:clip_db=true",
        "clip_db=true",
    ),
    "float_ssim": (
        "enable_lcs=true",
        "enable_db=true",
        "enable_db=true:clip_db=true",
        "scale=1",
        "scale=2",
        "scale=4",
        "scale=2:enable_lcs=true",
    ),
    "psnr": (
        "enable_chroma=false",
        "enable_mse=true",
        "enable_apsnr=true",
        "enable_apsnr=true:enable_chroma=false",
        "reduced_hbd_peak=true",
        "min_sse=0.5",
        "enable_mse=true:enable_apsnr=true:reduced_hbd_peak=true:min_sse=0.5",
    ),
    "adm": _ADM,
    "motion": _MOTION,
    "vif": _debug(("vif_enhn_gain_limit=1.0", "vif_enhn_gain_limit=1.5", "vif_skip_scale0=true")),
    "cambi": _CAMBI,
}

# Model files upstream ships under model/ (the same bytes in both trees), as
# (run name, harness kind, path under model/, flags).
_MODEL_FILES: tuple[tuple[str, str], ...] = (
    ("M", "other_models/nflxtrain_norm_type_none.json"),
    ("M", "other_models/nflx_v1.json"),
    ("M", "other_models/vmaf_v0.6.0.json"),
    ("M", "other_models/vmaf_v0.6.1mfz.json"),
    ("C", "vmaf_4k_rb_v0.6.2/vmaf_4k_rb_v0.6.2.json"),
    ("M", "vmaf_4k_v0.6.1.json"),
    ("M", "vmaf_4k_v0.6.1neg.json"),
    ("C", "vmaf_b_v0.6.3.json"),
    ("M", "vmaf_float_4k_v0.6.1.json"),
    ("C", "vmaf_float_b_v0.6.3.json"),
    ("M", "vmaf_float_v0.6.1.json"),
    ("M", "vmaf_float_v0.6.1neg.json"),
    ("C", "vmaf_rb_v0.6.2/vmaf_rb_v0.6.2.json"),
    ("C", "vmaf_rb_v0.6.3/vmaf_rb_v0.6.3.json"),
    ("M", "vmaf_v0.6.1.json"),
    ("M", "vmaf_v0.6.1neg.json"),
    ("M", "vmaf_v1.0.16_hfr/vmaf_v1.0.16_hfr_1d5h_2160.json"),
    ("M", "vmaf_v1.0.16_hfr/vmaf_v1.0.16_hfr_3d0h_2160.json"),
    ("M", "vmaf_v1.0.16_hfr/vmaf_v1.0.16_hfr_3d0h.json"),
    ("M", "vmaf_v1.0.16_hfr/vmaf_v1.0.16_hfr_5d0h.json"),
    ("M", "vmaf_v1.0.16/vmaf_v1.0.16_1d5h_2160.json"),
    ("M", "vmaf_v1.0.16/vmaf_v1.0.16_3d0h_2160.json"),
    ("M", "vmaf_v1.0.16/vmaf_v1.0.16_3d0h.json"),
    ("M", "vmaf_v1.0.16/vmaf_v1.0.16_5d0h.json"),
)
_FLAGGED_MODELS = ("vmaf_v0.6.1", "vmaf_float_v0.6.1")
_BUILT_IN = ("vmaf_v0.6.1", "vmaf_v0.6.1neg", "vmaf_4k_v0.6.1", "vmaf_float_v0.6.1")
PROBE_MODELS = (
    "M.vmaf_v0.6.1",
    "M.vmaf_float_v0.6.1",
    "M.vmaf_v1.0.16_3d0h",
    "M.vmaf_v1.0.16_hfr_3d0h",
    "C.vmaf_b_v0.6.3",
    "C.vmaf_float_b_v0.6.3",
    "B.vmaf_v0.6.1",
)


@dataclasses.dataclass(frozen=True)
class Run:
    """One harness invocation: a fixture and a request.

    ``spec`` is the harness argument with ``{model}`` standing for the model
    directory of the tree that runs it.
    """

    fixture: str
    name: str
    spec: str


def spec_id(options: str) -> str:
    """The run-name form of an option string (``a=1:b=2`` -> ``a-1+b-2``)."""

    return options.replace(":", "+").replace("/", "_").replace("=", "-") or "default"


def _feature_run(fixture: str, extractor: str, options: str) -> Run:
    spec = f"F:{extractor}" + (f":{options}" if options else "")
    return Run(fixture, f"F.{extractor}.{spec_id(options)}", spec)


def model_specs() -> tuple[tuple[str, str], ...]:
    """Every model request as (run name, harness spec)."""

    specs: list[tuple[str, str]] = []
    for kind, relative in _MODEL_FILES:
        stem = relative.rsplit("/", 1)[-1].removesuffix(".json")
        specs.append((f"{kind}.{stem}", f"{kind}:{{model}}/{relative}"))
        if stem in _FLAGGED_MODELS:
            specs.extend(
                (f"M.{stem}.{flag}", f"M:{{model}}/{relative}:{flag}")
                for flag in ("transform", "noclip")
            )
    specs.extend((f"B.{version}", f"B:{version}") for version in _BUILT_IN)
    return tuple(specs)


def matrix(mode: str) -> tuple[Run, ...]:
    """The runs of *mode* (``probe`` or ``full``), in a stable order."""

    if mode not in ("probe", "full"):
        raise ValueError(f"unknown mode {mode!r}")
    probe = mode == "probe"
    runs: list[Run] = []
    for fixture in FIXTURES:
        if probe and not fixture.probe:
            continue
        for extractor, defaults in DEFAULT_SPECS.items():
            runs.extend(_feature_run(fixture.name, extractor, options) for options in defaults)
    for fixture_name in PROBE_OPTION_FIXTURES if probe else OPTION_FIXTURES:
        for extractor, variants in OPTION_SPECS.items():
            runs.extend(_feature_run(fixture_name, extractor, options) for options in variants)
    for fixture_name in PROBE_MODEL_FIXTURES if probe else MODEL_FIXTURES:
        for name, spec in model_specs():
            if not probe or name in PROBE_MODELS:
                runs.append(Run(fixture_name, name, spec))
    return tuple(runs)


def fixture_by_name(name: str) -> Fixture:
    """The fixture called *name*."""

    for fixture in FIXTURES:
        if fixture.name == name:
            return fixture
    raise KeyError(name)
