# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""``corpus --vmaf-model`` / ``--neg`` and the resolution-aware selector.

The CLI never turned the selector off, so ``--vmaf-model`` and ``--neg``
changed nothing: every row was scored with the height-rule model. An
explicit ``--vmaf-model`` now scores every row with that model, ``--neg``
takes the NEG variant of whichever model applies, and the CLI names the
model on stderr.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import pytest

_HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(_HERE.parent / "src"))

from vmaftune import cli
from vmaftune.corpus import CorpusJob, CorpusOptions, iter_rows
from vmaftune.defaultmodel import DEFAULT_MODEL, DEFAULT_MODEL_NEG
from vmaftune.ladder import _sweep_corpus_options, make_default_sampler
from vmaftune.resolution import MODEL_4K, MODEL_4K_NEG


class _Done:
    def __init__(self, returncode=0, stdout="", stderr=""):
        self.returncode = returncode
        self.stdout = stdout
        self.stderr = stderr


def _rows(tmp_path: Path, height: int, **opts_kw) -> tuple[list[dict], list[list[str]]]:
    """Run one scored cell; return the rows and every vmaf argv."""
    src = tmp_path / "ref.yuv"
    src.write_bytes(b"\x80" * 4096)
    score_calls: list[list[str]] = []

    def fake_encode(cmd, capture_output, text, check):
        Path(cmd[-1]).write_bytes(b"\x00" * 2048)
        return _Done(stderr="ffmpeg version 7.1\nx264 - core 164\n")

    def fake_score(cmd, capture_output, text, check):
        score_calls.append(list(cmd))
        out = Path(cmd[cmd.index("--output") + 1])
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps({"pooled_metrics": {"vmaf": {"mean": 90.0}}}))
        return _Done(stderr="VMAF version: 3.0.0\n")

    job = CorpusJob(
        source=src,
        width=height * 16 // 9,
        height=height,
        pix_fmt="yuv420p",
        framerate=24.0,
        duration_s=1.0,
        cells=(("medium", 23),),
    )
    opts = CorpusOptions(
        output=tmp_path / "c.jsonl", encode_dir=tmp_path / "enc", src_sha256=False, **opts_kw
    )
    rows = list(iter_rows(job, opts, encode_runner=fake_encode, score_runner=fake_score))
    return rows, score_calls


def _model_arg(argv: list[str]) -> str:
    return argv[argv.index("--model") + 1]


@pytest.mark.parametrize(
    ("height", "opts_kw", "want"),
    [
        # Resolution-aware default: the height rule, boundary at 2160 lines.
        (2159, {}, DEFAULT_MODEL),
        (2160, {}, MODEL_4K),
        # NEG of the height-rule model (was dropped: the selector replaced it).
        (1080, {"neg": True}, DEFAULT_MODEL_NEG),
        (2160, {"neg": True}, MODEL_4K_NEG),
        # Explicit model with the selector off wins at any height.
        (2160, {"resolution_aware": False, "vmaf_model": "vmaf_v0.6.1"}, "vmaf_v0.6.1"),
        (
            2160,
            {"resolution_aware": False, "vmaf_model": "vmaf_v0.6.1", "neg": True},
            "vmaf_v0.6.1neg",
        ),
    ],
)
def test_iter_rows_scores_with_the_selected_model(tmp_path, height, opts_kw, want):
    rows, calls = _rows(tmp_path, height, **opts_kw)
    assert rows[0]["vmaf_model"] == want
    assert _model_arg(calls[0]) == f"version={want}"


def _corpus_args(*extra: str) -> argparse.Namespace:
    parser = cli._build_parser()
    args = parser.parse_args(
        [
            "corpus",
            "--source",
            "ref.yuv",
            "--width",
            "3840",
            "--height",
            "2160",
            "--preset",
            "medium",
            "--crf",
            "23",
            "--score-backend",
            "cpu",
            *extra,
        ]
    )
    return args


@pytest.fixture
def _cpu_backend(monkeypatch):
    monkeypatch.setattr(cli, "select_backend", lambda prefer, vmaf_bin: "cpu")


def test_cli_explicit_vmaf_model_turns_the_selector_off(_cpu_backend, capsys):
    opts = cli._build_opts(_corpus_args("--vmaf-model", "vmaf_v0.6.1"))
    assert opts.resolution_aware is False
    assert opts.vmaf_model == "vmaf_v0.6.1"
    assert "VMAF model = vmaf_v0.6.1 for every row" in capsys.readouterr().err


def test_cli_without_vmaf_model_names_the_height_rule(_cpu_backend, capsys):
    opts = cli._build_opts(_corpus_args())
    assert opts.resolution_aware is True
    assert opts.neg is False
    err = capsys.readouterr().err
    assert "picked per encode height" in err
    assert MODEL_4K in err


def test_cli_neg_reaches_the_options(_cpu_backend, capsys):
    opts = cli._build_opts(_corpus_args("--neg"))
    assert opts.neg is True
    assert opts.resolution_aware is True
    assert MODEL_4K_NEG in capsys.readouterr().err


def test_ladder_sampler_neg_and_pinned_model(tmp_path):
    """ladder had the same override: its corpus options never carried --neg."""
    from vmaftune.ladder import _SamplerSettings

    def settings(**kw):
        base = {
            "pix_fmt": "yuv420p",
            "framerate": 24.0,
            "duration_s": 1.0,
            "crf_sweep": (23,),
            "src_width": None,
            "src_height": None,
            "cloud_sink": None,
            "score_backend": None,
            "vmaf_model": None,
        }
        base.update(kw)
        return _SamplerSettings(**base)

    default = _sweep_corpus_options("libx264", tmp_path, settings())
    assert default.resolution_aware is True and default.neg is False
    neg = _sweep_corpus_options("libx264", tmp_path, settings(neg=True))
    assert neg.neg is True and neg.resolution_aware is True
    pinned = _sweep_corpus_options("libx264", tmp_path, settings(vmaf_model="vmaf_v0.6.1"))
    assert pinned.resolution_aware is False and pinned.vmaf_model == "vmaf_v0.6.1"
    assert callable(make_default_sampler(neg=True))
