# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""``vmaf-tune auto --smoke`` plans without a source.

The smoke planner probes nothing (synthetic 1080p SDR metadata), but the
subcommand marked ``--src`` required, so ``auto --smoke`` exited 2 before the
planner ran. ``--src`` is now needed unless ``--smoke`` is given, and always
with ``--execute``. The Go ``vmafx-tune auto`` has the same contract
(``TestAutoSmokeNeedsNoSrc``) and records an empty ``src``.

Failing before the change (argparse exited 2 for a missing ``--src``, and
``run_auto`` took no ``None`` source): every test here except
``test_smoke_with_src_still_records_it``.
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
from vmaftune.auto import run_auto


def _plan(capsys: pytest.CaptureFixture[str], *argv: str) -> tuple[int, dict, str]:
    """Run ``auto``; returns the exit code, the parsed JSON plan and stderr."""
    rc = cli.main(["auto", *argv])
    captured = capsys.readouterr()
    return rc, (json.loads(captured.out) if captured.out.strip() else {}), captured.err


def test_smoke_plans_without_src(capsys: pytest.CaptureFixture[str]) -> None:
    rc, plan, _ = _plan(capsys, "--smoke")
    assert rc == 0
    meta = plan["metadata"]
    assert meta["smoke"] is True
    assert meta["src"] == ""
    assert plan["cells"], "smoke plan carries cells"
    assert meta["winner"]["status"] == "budget_and_quality_met"


def test_smoke_with_src_still_records_it(capsys: pytest.CaptureFixture[str]) -> None:
    rc, plan, _ = _plan(capsys, "--smoke", "--src", "ref.yuv")
    assert rc == 0
    assert plan["metadata"]["src"] == "ref.yuv"


@pytest.mark.parametrize("argv", [[], ["--allow-codecs", "libx264"]])
def test_non_smoke_without_src_exits_2(argv: list[str], capsys: pytest.CaptureFixture[str]) -> None:
    rc, plan, err = _plan(capsys, *argv)
    assert rc == 2
    assert plan == {}
    assert "--src is required unless --smoke" in err


def test_execute_needs_src_even_with_smoke(capsys: pytest.CaptureFixture[str]) -> None:
    rc, plan, err = _plan(capsys, "--smoke", "--execute")
    assert rc == 2
    assert plan == {}
    assert "--src is required with --execute" in err


def test_run_auto_smoke_accepts_no_src() -> None:
    plan = run_auto(
        src=None, target_vmaf=93.0, max_budget_kbps=8000.0, allow_codecs=("libx264",), smoke=True
    )
    assert plan.metadata["src"] == ""


def test_run_auto_non_smoke_refuses_no_src() -> None:
    with pytest.raises(ValueError, match="src is required"):
        run_auto(src=None, target_vmaf=93.0, max_budget_kbps=8000.0, allow_codecs=("libx264",))


def test_auto_help_says_src_is_optional_with_smoke() -> None:
    parser = cli._build_parser()
    sub = next(a for a in parser._actions if isinstance(a, argparse._SubParsersAction))
    action = next(a for a in sub.choices["auto"]._actions if "--src" in a.option_strings)
    assert not action.required
    text = " ".join(str(action.help).split())
    assert "unless --smoke" in text
    assert "--execute" in text
