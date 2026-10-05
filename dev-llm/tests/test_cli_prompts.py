# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2

"""Smoke-test that packaged prompt templates are discoverable and template-
substitutable. Does NOT exercise Ollama."""

from __future__ import annotations

import subprocess
from pathlib import Path

from typer.testing import CliRunner

from vmaf_dev_llm import cli
from vmaf_dev_llm.cli import _guess_prompt_for_file, _load_prompt
from vmaf_dev_llm.config import Config


def test_all_prompts_present() -> None:
    cfg = Config()
    for name in (
        "review_c.md",
        "review_cuda.md",
        "review_sycl.md",
        "commit_msg.md",
        "doc_section.md",
    ):
        text = _load_prompt(cfg, name)
        assert text.strip(), f"empty prompt: {name}"


def test_guess_prompt() -> None:
    assert _guess_prompt_for_file(Path("foo.c")) == "review_c.md"
    assert _guess_prompt_for_file(Path("foo.cpp")) == "review_c.md"
    assert _guess_prompt_for_file(Path("foo.cu")) == "review_cuda.md"
    assert _guess_prompt_for_file(Path("foo.cuh")) == "review_cuda.md"
    assert _guess_prompt_for_file(Path("sycl/foo.cpp")) == "review_sycl.md"
    assert _guess_prompt_for_file(Path("foo.rs")) == "review_c.md"  # fallback


def test_review_c_has_required_placeholders() -> None:
    cfg = Config()
    text = _load_prompt(cfg, "review_c.md")
    assert "{{FILE_PATH}}" in text
    assert "{{SOURCE}}" in text


def test_commit_msg_has_diff_placeholder() -> None:
    cfg = Config()
    text = _load_prompt(cfg, "commit_msg.md")
    assert "{{DIFF}}" in text


def test_doc_section_has_symbol_placeholder() -> None:
    cfg = Config()
    text = _load_prompt(cfg, "doc_section.md")
    assert "{{SYMBOL}}" in text
    assert "{{FILE_PATH}}" in text
    assert "{{SOURCE}}" in text


def test_commitmsg_reports_git_stderr(monkeypatch) -> None:
    def fail_git(*_args: object, **_kwargs: object) -> str:
        raise subprocess.CalledProcessError(2, ["git"], stderr="fatal: fixture failure")

    monkeypatch.setattr(cli, "load_config", Config)
    monkeypatch.setattr(cli, "checked_output", fail_git)

    result = CliRunner().invoke(cli.app, ["commitmsg"])

    assert result.exit_code == 1
    assert "git diff --staged failed: fatal: fixture failure" in result.output


def _unreachable(_cfg: Config, **_kwargs: object) -> str:
    raise cli.OllamaError("fixture: ollama unreachable")


def test_commitmsg_without_staged_changes_exits_two(monkeypatch) -> None:
    monkeypatch.setattr(cli, "load_config", Config)
    monkeypatch.setattr(cli, "checked_output", lambda *_a, **_k: "  \n")

    result = CliRunner().invoke(cli.app, ["commitmsg"])

    assert result.exit_code == 2
    assert "nothing to draft" in result.output


def test_commitmsg_ollama_failure_exits_one(monkeypatch) -> None:
    monkeypatch.setattr(cli, "load_config", Config)
    monkeypatch.setattr(cli, "checked_output", lambda *_a, **_k: "diff --git a b\n")
    monkeypatch.setattr(cli, "_run_ollama", _unreachable)

    result = CliRunner().invoke(cli.app, ["commitmsg"])

    assert result.exit_code == 1
    assert "ollama unreachable" in result.output


def test_review_and_docgen_ollama_failure_exit_one(monkeypatch, tmp_path: Path) -> None:
    source = tmp_path / "sample.c"
    source.write_text("int main(void) { return 0; }\n")
    monkeypatch.setattr(cli, "load_config", Config)
    monkeypatch.setattr(cli, "_run_ollama", _unreachable)

    review = CliRunner().invoke(cli.app, ["review", "--file", str(source)])
    docgen = CliRunner().invoke(cli.app, ["docgen", "--file", str(source), "--symbol", "main"])

    assert review.exit_code == 1
    assert docgen.exit_code == 1


class _FakeClient:
    def __init__(self, reachable: bool) -> None:
        self._reachable = reachable

    def available(self) -> bool:
        return self._reachable


def test_check_exit_status_follows_ollama_reachability(monkeypatch) -> None:
    monkeypatch.setattr(cli, "load_config", Config)
    for reachable, status in ((True, 0), (False, 1)):
        monkeypatch.setattr(cli, "OllamaClient", lambda r=reachable, **_k: _FakeClient(r))
        result = CliRunner().invoke(cli.app, ["check"])
        assert result.exit_code == status
