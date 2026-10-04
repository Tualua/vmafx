# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Crashes and dead flags of the vmaf-tune CLI (bug brief items 6-9, 20).

- Item 6: the coarse-to-fine grid was 10..50 for every encoder; libx265,
  libsvtav1, libvvenc, AMF and ProRes stopped with a ValueError traceback.
- Item 7: ``ladder --workdir`` / ``--max-concurrent-decodes`` were accepted
  and ignored.
- Item 8: ``auto --execute`` passed no geometry to ``run_plan`` (1920x1080 at
  25 fps for every source).
- Item 9: ``recommend --with-uncertainty`` on corpus rows (which carry no
  ``vmaf_interval``) fell back to the point pick without saying so.
- Item 20: the result line's ``visited=2/15`` read as encodes saved.
"""

from __future__ import annotations

import argparse
import json
import sys
import threading
from pathlib import Path

import pytest

_HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(_HERE.parent / "src"))

from vmaftune import cli, corpus
from vmaftune.cli import main as cli_main
from vmaftune.codec_adapters import known_codecs
from vmaftune.corpus import (
    CorpusJob,
    CorpusOptions,
    _pick_best_crf,
    _should_skip_refinement,
    coarse_search_window,
    coarse_to_fine_search,
)
from vmaftune.ladder import SamplerResources, _SamplerSettings

# ---------------------------------------------------------------- item 6


@pytest.mark.parametrize(
    ("encoder", "window"),
    [
        ("libx264", (10, 50)),
        ("libx265", (15, 40)),
        ("libsvtav1", (20, 50)),
        ("libvvenc", (17, 50)),
        ("h264_amf", (15, 40)),
        ("hevc_nvenc", (15, 40)),
        ("h264_qsv", (10, 50)),
        ("prores_videotoolbox", (0, 5)),
    ],
)
def test_coarse_window_follows_the_adapter(encoder, window):
    assert coarse_search_window(encoder) == window


def test_every_adapter_accepts_its_own_window():
    """The default window never contains a CRF the adapter refuses."""
    from vmaftune.codec_adapters import get_adapter

    for name in known_codecs():
        if name == "av1_videotoolbox":
            continue  # placeholder adapter refuses every cell (ADR-0339)
        adapter = get_adapter(name)
        lo, hi = coarse_search_window(name)
        preset = "medium" if "medium" in adapter.presets else adapter.presets[0]
        for crf in (lo, hi):
            adapter.validate(preset, crf)


def _job(tmp_path: Path) -> CorpusJob:
    src = tmp_path / "ref.yuv"
    src.write_bytes(b"\x80" * 1024)
    return CorpusJob(
        source=src,
        width=64,
        height=64,
        pix_fmt="yuv420p",
        framerate=24.0,
        duration_s=1.0,
        cells=(("medium", 0),),
    )


def _fake_iter_rows(seen: list[int], score_of):
    def _iter(job, opts, **_kw):
        for _preset, crf in job.cells:
            seen.append(crf)
            yield {"crf": crf, "vmaf_score": score_of(crf), "exit_status": 0}

    return _iter


def test_libx265_search_stays_inside_its_range(tmp_path, monkeypatch):
    seen: list[int] = []
    monkeypatch.setattr(corpus, "iter_rows", _fake_iter_rows(seen, lambda c: 100 - c))
    opts = CorpusOptions(encoder="libx265", encode_dir=tmp_path / "e")
    list(coarse_to_fine_search(_job(tmp_path), opts, target_vmaf=70.0))
    assert seen[:3] == [15, 25, 35]
    assert min(seen) >= 15 and max(seen) <= 40


def test_refused_window_raises_before_any_encode(tmp_path, monkeypatch):
    seen: list[int] = []
    monkeypatch.setattr(corpus, "iter_rows", _fake_iter_rows(seen, lambda c: 90.0))
    opts = CorpusOptions(encoder="libx265", encode_dir=tmp_path / "e")
    with pytest.raises(ValueError, match="libx265 refuses the coarse-to-fine window"):
        coarse_to_fine_search(_job(tmp_path), opts, crf_min=10, crf_max=50)
    assert seen == []


def test_higher_is_better_adapters_refine_downwards():
    """VideoToolbox -q:v rises with quality: the lowest passing value is the centre."""
    rows = [{"crf": c, "vmaf_score": s} for c, s in ((10, 80.0), (30, 92.0), (50, 97.0))]
    assert _pick_best_crf(rows, target_vmaf=90.0) == 50
    assert _pick_best_crf(rows, target_vmaf=90.0, higher_is_better=True) == 30
    skip = {"coarse_grid": (10, 30, 50), "target_vmaf": 90.0, "best_score": 95.0, "crf_max": 50}
    assert _should_skip_refinement(best_crf=10, higher_is_better=True, crf_min=10, **skip)
    assert not _should_skip_refinement(best_crf=30, higher_is_better=True, crf_min=10, **skip)


def _corpus_argv(tmp_path: Path, *extra: str) -> list[str]:
    src = tmp_path / "ref.yuv"
    src.write_bytes(b"\x80" * 1024)
    return [
        "corpus",
        "--source",
        str(src),
        "--width",
        "64",
        "--height",
        "64",
        "--preset",
        "medium",
        "--output",
        str(tmp_path / "c.jsonl"),
        "--score-backend",
        "cpu",
        *extra,
    ]


@pytest.fixture
def _cpu(monkeypatch):
    monkeypatch.setattr(cli, "select_backend", lambda prefer, vmaf_bin: "cpu")


def test_cli_refused_crf_is_one_line_and_exit_2(tmp_path, capsys, _cpu):
    rc = cli_main(_corpus_argv(tmp_path, "--encoder", "libx265", "--crf", "10"))
    err = capsys.readouterr().err
    assert rc == 2
    assert "libx265 refuses preset 'medium' at 10" in err
    assert "Traceback" not in err
    assert not (tmp_path / "c.jsonl").exists()


def test_cli_coarse_to_fine_libx265_runs(tmp_path, monkeypatch, _cpu):
    seen: list[int] = []
    monkeypatch.setattr(corpus, "iter_rows", _fake_iter_rows(seen, lambda c: 100 - c))
    rc = cli_main(
        _corpus_argv(tmp_path, "--encoder", "libx265", "--coarse-to-fine", "--target-vmaf", "70")
    )
    assert rc == 0
    assert seen and min(seen) >= 15 and max(seen) <= 40
    assert len((tmp_path / "c.jsonl").read_text().splitlines()) == len(seen)


# ---------------------------------------------------------------- item 7


def test_sampler_scratch_goes_to_workdir_and_decode_holds_the_cap(tmp_path, monkeypatch):
    seen: dict = {}

    def fake_iter_rows(job, opts, **_kw):
        seen["encode_dir"] = opts.encode_dir
        seen["semaphore"] = opts.decode_semaphore
        yield {"crf": 25, "vmaf_score": 95.0, "bitrate_kbps": 100.0, "exit_status": 0}

    monkeypatch.setattr(corpus, "iter_rows", fake_iter_rows)
    sem = threading.Semaphore(3)
    settings = _SamplerSettings(
        pix_fmt="yuv420p",
        framerate=24.0,
        duration_s=1.0,
        crf_sweep=(25,),
        src_width=None,
        src_height=None,
        cloud_sink=None,
        score_backend=None,
        vmaf_model=None,
        resources=SamplerResources(workdir=tmp_path / "work", decode_semaphore=sem),
    )
    settings.sample(tmp_path / "ref.yuv", "libx264", 64, 64, 90.0)
    assert seen["encode_dir"].parent.parent == tmp_path / "work"
    assert seen["semaphore"] is sem


def test_sampler_without_workdir_uses_vmaftune_workdir(tmp_path, monkeypatch):
    monkeypatch.setenv("VMAFTUNE_WORKDIR", str(tmp_path / "env-work"))
    assert SamplerResources().scratch_parent() == tmp_path / "env-work"
    assert SamplerResources(workdir=tmp_path / "flag").scratch_parent() == tmp_path / "flag"


def test_reference_decode_holds_the_semaphore(tmp_path, monkeypatch):
    sem = threading.Semaphore(1)
    held: list[bool] = []

    def fake_decode(source, **_kw):
        held.append(not sem.acquire(blocking=False))
        return source, 0

    monkeypatch.setattr(corpus, "_maybe_decode_reference", fake_decode)
    opts = CorpusOptions(encode_dir=tmp_path / "e", decode_semaphore=sem)
    corpus._decode_job_reference(_job(tmp_path), opts)
    assert held == [True]


def test_ladder_cli_passes_workdir_and_cap(tmp_path, monkeypatch):
    captured: dict = {}

    def fake_sampler(**kw):
        captured.update(kw)
        return lambda *a: None

    monkeypatch.setattr("vmaftune.ladder.make_default_sampler", fake_sampler)
    monkeypatch.setattr("vmaftune.ladder.build_and_emit", lambda **kw: "#EXTM3U\n")
    monkeypatch.setattr(cli, "select_backend", lambda prefer, vmaf_bin: "cpu")
    args = cli._build_parser().parse_args(
        [
            "ladder",
            "--src",
            str(tmp_path / "ref.yuv"),
            "--resolutions",
            "64x64",
            "--target-vmafs",
            "90",
            "--format",
            "json",
            "--workdir",
            str(tmp_path / "w"),
            "--max-concurrent-decodes",
            "2",
        ]
    )
    cli._build_ladder_manifest(args, [(64, 64)], [90.0], None)
    resources = captured["resources"]
    assert resources.workdir == tmp_path / "w"
    assert resources.decode_semaphore._value == 2


# ---------------------------------------------------------------- item 8


def _auto_args(src: Path, *extra: str) -> argparse.Namespace:
    return cli._build_parser().parse_args(["auto", "--src", str(src), "--execute", *extra])


def test_auto_execute_raw_yuv_without_geometry_is_refused(tmp_path):
    with pytest.raises(ValueError, match="raw YUV needs --width, --height, --framerate"):
        cli._auto_execute_geometry(_auto_args(tmp_path / "ref.yuv"))
    with pytest.raises(ValueError, match="needs --framerate"):
        cli._auto_execute_geometry(
            _auto_args(tmp_path / "ref.yuv", "--width", "640", "--height", "360")
        )


def test_auto_execute_raw_yuv_with_geometry(tmp_path):
    geometry = cli._auto_execute_geometry(
        _auto_args(
            tmp_path / "ref.yuv",
            "--width",
            "640",
            "--height",
            "360",
            "--framerate",
            "29.97",
            "--pix-fmt",
            "yuv420p10le",
        )
    )
    assert geometry == {
        "width": 640,
        "height": 360,
        "framerate": 29.97,
        "pix_fmt": "yuv420p10le",
        "source_is_container": False,
    }


def test_auto_execute_container_probes_what_is_missing(tmp_path, monkeypatch):
    monkeypatch.setattr(
        "vmaftune.predictor_features._probe_video_geometry", lambda src, cfg, run: (1280, 720, 50.0)
    )
    geometry = cli._auto_execute_geometry(_auto_args(tmp_path / "src.mp4", "--framerate", "25"))
    assert (geometry["width"], geometry["height"], geometry["framerate"]) == (1280, 720, 25.0)
    assert geometry["source_is_container"] is True
    monkeypatch.setattr(
        "vmaftune.predictor_features._probe_video_geometry", lambda src, cfg, run: (0, 0, 0.0)
    )
    with pytest.raises(ValueError, match="ffprobe could not read"):
        cli._auto_execute_geometry(_auto_args(tmp_path / "src.mp4"))


def test_auto_execute_hands_the_geometry_to_run_plan(tmp_path, monkeypatch):
    calls: list[dict] = []

    def fake_run_plan(plan, src, runs_dir, **kw):
        calls.append(kw)
        return []

    monkeypatch.setattr("vmaftune.executor.run_plan", fake_run_plan)
    rc = cli_main(
        [
            "auto",
            "--src",
            str(tmp_path / "ref.yuv"),
            "--smoke",
            "--output",
            str(tmp_path / "plan.json"),
            "--execute",
            "--runs-dir",
            str(tmp_path / "runs"),
            "--width",
            "320",
            "--height",
            "240",
            "--framerate",
            "24",
        ]
    )
    assert rc == 0
    assert calls[0]["width"] == 320 and calls[0]["framerate"] == 24.0
    assert calls[0]["source_is_container"] is False
    rc = cli_main(
        [
            "auto",
            "--src",
            str(tmp_path / "ref.yuv"),
            "--smoke",
            "--execute",
            "--output",
            str(tmp_path / "p2.json"),
        ]
    )
    assert rc == 2 and len(calls) == 1


# ------------------------------------------------------------ items 9, 20


def _write_rows(path: Path, rows: list[dict]) -> Path:
    path.write_text("".join(json.dumps(r) + "\n" for r in rows), encoding="utf-8")
    return path


def _row(crf: int, vmaf: float, interval: tuple[float, float] | None = None) -> dict:
    row = {
        "encoder": "libx264",
        "preset": "medium",
        "crf": crf,
        "vmaf_score": vmaf,
        "bitrate_kbps": 1000.0 + crf,
        "exit_status": 0,
    }
    if interval:
        row["vmaf_interval"] = {"low": interval[0], "high": interval[1], "alpha": 0.05}
    return row


def test_uncertainty_on_corpus_rows_says_it_is_unavailable(tmp_path, capsys):
    corpus_path = _write_rows(tmp_path / "c.jsonl", [_row(20, 95.0), _row(30, 91.0)])
    rc = cli_main(
        [
            "recommend",
            "--from-corpus",
            str(corpus_path),
            "--target-vmaf",
            "90",
            "--with-uncertainty",
        ]
    )
    out = capsys.readouterr()
    assert rc == 0
    assert "no row carries a vmaf_interval" in out.err
    assert "uncertainty=unavailable" in out.out
    assert "rows_examined=" in out.out and "visited=" not in out.out


def test_uncertainty_with_intervals_names_nothing_unavailable(tmp_path, capsys):
    corpus_path = _write_rows(
        tmp_path / "c.jsonl", [_row(20, 95.0, (94.5, 95.5)), _row(30, 91.0, (90.5, 91.5))]
    )
    rc = cli_main(
        [
            "recommend",
            "--from-corpus",
            str(corpus_path),
            "--target-vmaf",
            "90",
            "--with-uncertainty",
        ]
    )
    out = capsys.readouterr()
    assert rc == 0
    assert "vmaf_interval" not in out.err
    assert "uncertainty=unavailable" not in out.out


def test_live_uncertainty_line_says_every_row_was_encoded(tmp_path, capsys):
    args = argparse.Namespace(target_vmaf=90.0, uncertainty_sidecar=None)
    rows = [dict(_row(c, 100.0 - c), src="ref.yuv") for c in (10, 20, 30)]
    assert cli._emit_live_uncertainty_pick(args, rows, tmp_path / "c.jsonl") == 0
    out = capsys.readouterr()
    assert "rows_examined=" in out.out and "(all 3 encoded)" in out.out
    assert "uncertainty=unavailable" in out.out
