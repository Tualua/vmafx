# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Help texts and ADR references of vmaf-tune (bug brief items 13-19, 21).

- Item 13: ``ladder --crf-sweep`` help and the ``build_ladder`` docstring
  named the old sweep ``18,23,28,33,38``.
- Item 15: the ``auto`` help said "seven short-circuits"; the tree has ten.
- Item 16: ``fast --encoder`` help said the encoder "must be in
  ENCODER_VOCAB_V2"; any adapter is accepted and the proxy's ``unknown``
  slot takes the others, without saying so.
- Item 17: ``--two-pass`` help said "libx264 / libx265 today"; five adapters
  run a 2-pass encode.
- Item 18: ``--workdir`` help cited ADR-0546 (an audit bundle) instead of
  ADR-0598.
- Item 19: ADR numbers in the help, the usage pages and the AGENTS.d pages
  resolved to unrelated records (renumbered ADRs).
- Item 22: the split modules (auto, bisect, executor, per_shot, prefilter,
  score) cited renumbered ADRs in comments and docstrings, and ``auto.py``
  still counted "seven" short-circuits.
- Item 21: the ladder docstring said the sampler picks the row closest to
  the target; ``pick_target_vmaf`` takes the lowest-bitrate row that clears it.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

import pytest

_HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(_HERE.parent / "src"))

from _vmaf_cli import REPO_ROOT

from vmaftune import cli
from vmaftune.auto import ShortCircuit
from vmaftune.codec_adapters import get_adapter, known_codecs
from vmaftune.ladder import DEFAULT_SAMPLER_CRF_SWEEP, build_ladder
from vmaftune.proxy import ENCODER_VOCAB_V2

# ADRs that vmaf-tune pages cite for a cross-cutting reason; every other cited
# record must be about vmaf-tune (its text names the tool).
CROSS_CUTTING_ADRS = {
    "0212": "HIP backend the --score-backend hip name selects",
    "0214": "cross-backend parity gate behind the score backends",
    "0222": "vmaf-perShot binary that tune-per-shot calls",
    "0223": "TransNet V2 shot detector behind vmaf-perShot",
    "0247": "vmaf-roi signal that saliency.py mirrors",
    "0286": "saliency_student_v1 model recommend-saliency loads",
    "0726": "removal of the Vulkan score backend",
}
_ADR_REF = re.compile(r"(?<![Pp]elorus )ADR[- ](\d{4})")
_ADR_LINK = re.compile(r"\]\(([^)#]*?adr/\d{4}-[^)#]*\.md)")
_ABOUT_VMAF_TUNE = re.compile(r"vmaf-tune|vmaftune|vmafx-tune", re.IGNORECASE)


def _subparser(name: str) -> argparse.ArgumentParser:
    parser = cli._build_parser()
    for action in parser._actions:
        if isinstance(action, argparse._SubParsersAction):
            return action.choices[name]
    raise AssertionError("vmaf-tune has no subcommands")


def _option_help(command: str, option: str) -> str:
    for action in _subparser(command)._actions:
        if option in action.option_strings:
            return " ".join(str(action.help).split())
    raise AssertionError(f"{command} has no {option}")


def _subcommand_help(command: str) -> str:
    parser = cli._build_parser()
    for action in parser._actions:
        if isinstance(action, argparse._SubParsersAction):
            for choice in action._choices_actions:
                if choice.dest == command:
                    return " ".join(str(choice.help).split())
    raise AssertionError(f"no subcommand {command}")


def _help_texts() -> list[tuple[str, str]]:
    """Every help string of every subcommand and option, labelled."""
    parser = cli._build_parser()
    texts = [("vmaf-tune (description)", str(parser.description))]
    for action in parser._actions:
        if not isinstance(action, argparse._SubParsersAction):
            continue
        texts += [(f"{c.dest} (subcommand)", str(c.help)) for c in action._choices_actions]
        for name, sub in action.choices.items():
            texts += [(f"{name} {'/'.join(a.option_strings)}", str(a.help)) for a in sub._actions]
    return texts


def _doc_pages() -> list[Path]:
    if not (REPO_ROOT / "docs" / "adr").is_dir():
        pytest.skip("not inside the VMAFx repository: docs/adr is absent")
    pages = sorted((REPO_ROOT / "docs" / "usage").glob("vmaf-tune*.md"))
    pages += sorted((REPO_ROOT / "tools" / "vmaf-tune" / "AGENTS.d").glob("*.md"))
    assert pages, "no vmaf-tune pages found"
    return pages


def _adr_file(number: str) -> Path | None:
    found = sorted((REPO_ROOT / "docs" / "adr").glob(f"{number}-*.md"))
    return found[0] if found else None


def _misattributed(label: str, text: str) -> list[str]:
    """ADR references in ``text`` that are missing or not about vmaf-tune."""
    bad = []
    for number in sorted(set(_ADR_REF.findall(text))):
        record = _adr_file(number)
        if record is None:
            bad.append(f"{label}: ADR-{number} does not exist")
        elif number not in CROSS_CUTTING_ADRS and not _ABOUT_VMAF_TUNE.search(
            record.read_text(encoding="utf-8")
        ):
            bad.append(f"{label}: ADR-{number} is {record.name}")
    return bad


# ---------------------------------------------------------------- item 13


def test_description_does_not_limit_corpus_to_libx264() -> None:
    description = " ".join(str(cli._build_parser().description).split())
    assert "libx264 + libvmaf" not in description
    assert "any registered codec adapter" in description


def test_ladder_crf_sweep_help_names_the_sampler_sweep() -> None:
    text = _option_help("ladder", "--crf-sweep")
    assert ",".join(str(c) for c in DEFAULT_SAMPLER_CRF_SWEEP) in text
    assert "18,23,28,33,38" not in text


def test_build_ladder_docstring_names_the_sweep_and_the_pick() -> None:
    doc = " ".join(str(build_ladder.__doc__).split())
    assert "DEFAULT_SAMPLER_CRF_SWEEP" in doc
    assert "(18, 23, 28, 33, 38)" not in doc
    # Item 21: the sampler takes the lowest-bitrate row that clears the target.
    assert "lowest bitrate whose VMAF" in doc
    assert "closest to" not in doc


# ---------------------------------------------------------------- item 15


def test_auto_help_counts_every_short_circuit() -> None:
    text = _subcommand_help("auto")
    assert f"{len(ShortCircuit)} short-circuits" in text
    assert "seven" not in text
    assert "ADR-0397" in text


def test_short_circuit_count_is_ten() -> None:
    # Boundary: the help reads the enum, so this pins what users see today.
    assert len(ShortCircuit) == 10


# ---------------------------------------------------------------- item 16


def test_fast_encoder_help_describes_the_unknown_slot() -> None:
    text = _option_help("fast", "--encoder")
    assert "must be in" not in text
    assert "'unknown' slot" in text
    assert "proxy_encoder_slot" in text


@pytest.mark.parametrize("encoder", ["libaom-av1", "h264_amf", "hevc_videotoolbox"])
def test_out_of_vocabulary_encoder_is_named(
    encoder: str, capsys: pytest.CaptureFixture[str]
) -> None:
    assert encoder not in ENCODER_VOCAB_V2
    args = argparse.Namespace(smoke=False, encoder=encoder)
    assert cli._fast_proxy_encoder_slot(args) == "unknown"
    err = capsys.readouterr().err
    assert encoder in err and "'unknown' slot" in err


@pytest.mark.parametrize(
    ("encoder", "smoke"), [("libx264", False), ("av1_qsv", False), ("libaom-av1", True)]
)
def test_vocabulary_encoder_or_smoke_names_nothing(
    encoder: str, smoke: bool, capsys: pytest.CaptureFixture[str]
) -> None:
    args = argparse.Namespace(smoke=smoke, encoder=encoder)
    assert cli._fast_proxy_encoder_slot(args) is None
    assert capsys.readouterr().err == ""


def test_fast_json_carries_the_proxy_slot(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    monkeypatch.setattr(cli, "_prepare_fast_runtime", lambda args: (None, None, "cpu"))
    monkeypatch.setattr(
        cli,
        "fast_recommend",
        lambda **kwargs: {"encoder": kwargs["encoder"], "recommended_crf": 30},
    )
    rc = cli.main(
        ["fast", "--src", "ref.yuv", "--width", "64", "--height", "64"]
        + ["--target-vmaf", "92", "--encoder", "libaom-av1"]
    )
    captured = capsys.readouterr()
    assert rc == 0
    assert json.loads(captured.out)["proxy_encoder_slot"] == "unknown"
    assert "'unknown' slot" in captured.err


# ---------------------------------------------------------------- item 17


def test_two_pass_help_lists_every_two_pass_adapter() -> None:
    text = _option_help("corpus", "--two-pass")
    two_pass = [c for c in known_codecs() if getattr(get_adapter(c), "supports_two_pass", False)]
    assert len(two_pass) == 5
    for codec in two_pass:
        assert codec in text
    assert "today" not in text
    assert "libx264 / libx265" not in text


# ---------------------------------------------------------------- item 18


@pytest.mark.parametrize("command", ["compare", "tune-per-shot", "ladder"])
def test_workdir_help_cites_the_workdir_adr(command: str) -> None:
    text = _option_help(command, "--workdir")
    assert "ADR-0598" in text
    assert "ADR-0546" not in text


# ---------------------------------------------------------------- item 19


def test_help_texts_cite_vmaf_tune_records() -> None:
    _doc_pages()
    bad = [b for label, text in _help_texts() for b in _misattributed(label, text)]
    assert not bad, "\n".join(bad)


def test_pages_cite_vmaf_tune_records() -> None:
    bad = []
    for page in _doc_pages():
        bad += _misattributed(page.name, page.read_text(encoding="utf-8"))
    assert not bad, "\n".join(bad)


def test_page_links_to_adrs_resolve() -> None:
    broken = []
    for page in _doc_pages():
        for target in _ADR_LINK.findall(page.read_text(encoding="utf-8")):
            if not (page.parent / target).resolve().is_file():
                broken.append(f"{page.name}: {target}")
    assert not broken, "\n".join(broken)


def test_unrelated_record_is_reported() -> None:
    # Negative case: a citation of a record about another subsystem fails.
    _doc_pages()
    assert _misattributed("fixture", "see ADR-0454") == [
        "fixture: ADR-0454 is 0454-vif-cuda-smem-staging.md"
    ]
    assert _misattributed("fixture", "see ADR-9999") == ["fixture: ADR-9999 does not exist"]
    assert _misattributed("fixture", "frozen Pelorus ADR-0110 contract, ADR-0726") == []


# ---------------------------------------------------------------- item 22

# The modules whose HISS-04 baseline was retired together with their stale ADR
# numbers. recommend.py keeps its own row (another change owns its pick rule).
SPLIT_MODULES = ("auto", "bisect", "executor", "per_shot", "prefilter", "score")


def _module_misattributions(paths: list[Path]) -> list[str]:
    """ADR references in the source text (comments, docstrings, strings) of ``paths``."""
    bad = []
    for path in paths:
        bad += _misattributed(path.name, path.read_text(encoding="utf-8"))
    return bad


def _split_module_paths() -> list[Path]:
    _doc_pages()
    return [_HERE.parent / "src" / "vmaftune" / f"{name}.py" for name in SPLIT_MODULES]


def test_split_modules_cite_vmaf_tune_records() -> None:
    bad = _module_misattributions(_split_module_paths())
    assert not bad, "\n".join(bad)


def test_module_scan_reports_unrelated_and_unqualified_pelorus_records(tmp_path: Path) -> None:
    # Negative cases: a renumbered record and a Pelorus ADR cited without
    # saying whose it is (ADR-0110 here is a coverage-gate record).
    _doc_pages()
    fixture = tmp_path / "fixture.py"
    fixture.write_text(
        '"""See ADR-0468 and ADR-0110; Pelorus ADR-0110 is fine, ADR-0301 too."""\n',
        encoding="utf-8",
    )
    assert _module_misattributions([fixture]) == [
        "fixture.py: ADR-0110 is 0110-coverage-gate-fprofile-update-atomic.md",
        "fixture.py: ADR-0468 is 0468-hip-float-adm-real-kernel.md",
    ]


def test_auto_module_counts_every_short_circuit() -> None:
    text = (_HERE.parent / "src" / "vmaftune" / "auto.py").read_text(encoding="utf-8")
    assert len(ShortCircuit) == 10
    assert "the seven short-circuit" not in text.lower()
    assert "ten short-circuit" in text


def test_prefilter_notes_name_the_pelorus_records() -> None:
    pytest.importorskip("optuna")
    from vmaftune.prefilter import recommend_prefilter

    notes = recommend_prefilter(src=None, target_vmaf=90.0, smoke=True, n_trials=3)["notes"]
    assert "Pelorus ADR-0106" in notes
    assert not _misattributed("prefilter notes", notes)
