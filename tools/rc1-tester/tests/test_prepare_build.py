# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Tests for the build-time helper of the tester image (image/prepare_build.py)."""

from __future__ import annotations

import importlib.util
import json
import subprocess
from pathlib import Path

import pytest

_PATH = Path(__file__).resolve().parents[1] / "image" / "prepare_build.py"
_spec = importlib.util.spec_from_file_location("prepare_build", _PATH)
pb = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(pb)


def test_listed_names_skip_comments_and_blanks(tmp_path: Path) -> None:
    path = tmp_path / "list.txt"
    path.write_text("# comment\n\ntest_a\n  test_b  \n")
    assert pb.listed_names(path) == ["test_a", "test_b"]


def fake_introspect(monkeypatch, build: Path, names: list[str], python: tuple[str, ...] = ()):
    tests = [{"name": n, "cmd": [str(build / "test" / n)]} for n in names]
    tests += [{"name": n, "cmd": ["/usr/bin/python3", "x.py"]} for n in python]

    def run(*_args, **_kwargs):
        return subprocess.CompletedProcess([], 0, stdout=json.dumps(tests), stderr="")

    monkeypatch.setattr(pb.subprocess, "run", run)


def test_native_tests_keep_listed_executables_only(tmp_path: Path, monkeypatch) -> None:
    names = [f"test_{i}" for i in range(pb.MIN_TESTS)]
    fake_introspect(monkeypatch, tmp_path, names + ["test_unlisted"], python=["test_py"])
    found = pb.native_tests(tmp_path, [*names, "test_py", "test_absent"])
    assert [item["name"] for item in found] == sorted(names)  # no python test, no absent one


def test_too_few_tests_fail_the_build(tmp_path: Path, monkeypatch) -> None:
    fake_introspect(monkeypatch, tmp_path, ["test_only_one"])
    with pytest.raises(pb.BuildError):
        pb.native_tests(tmp_path, ["test_only_one"])


def test_exactly_the_minimum_is_accepted(tmp_path: Path, monkeypatch) -> None:
    names = [f"t{i}" for i in range(pb.MIN_TESTS)]
    fake_introspect(monkeypatch, tmp_path, names)
    assert len(pb.native_tests(tmp_path, names)) == pb.MIN_TESTS


def test_report_fixture_lines(tmp_path: Path) -> None:
    manifest = tmp_path / "m.sha256"
    manifest.write_text("a1  yuv/one.yuv\nb2  yuv/two.yuv\nc3  yuv/unused.yuv\n")
    fixtures = tmp_path / "f.json"
    entry = {"ref": "python/test/resource/yuv/one.yuv", "dis": "python/test/resource/yuv/two.yuv"}
    fixtures.write_text(json.dumps({"fixtures": [entry]}))
    assert pb.report_fixture_lines(manifest, fixtures) == ["a1  yuv/one.yuv", "b2  yuv/two.yuv"]
    entry["dis"] = "python/test/resource/yuv/missing.yuv"
    fixtures.write_text(json.dumps({"fixtures": [entry]}))
    with pytest.raises(pb.BuildError):
        pb.report_fixture_lines(manifest, fixtures)


def test_shipped_lists_and_manifests_agree() -> None:
    image = _PATH.parent
    names = pb.listed_names(image / "unit-tests.txt")
    macos = pb.listed_names(image / "unit-tests-macos.txt")
    assert set(names) <= set(macos) and len(names) == len(set(names))
    shipped = pb.report_fixture_lines(image / "fixtures.sha256", image / "fixtures.json")
    assert len(shipped) == 7


def test_main_reports_a_build_error_with_status_1(tmp_path: Path, capsys) -> None:
    manifest = tmp_path / "m.sha256"
    manifest.write_text("a1  yuv/one.yuv\n")
    fixtures = tmp_path / "f.json"
    entry = {"ref": "python/test/resource/yuv/x.yuv", "dis": "python/test/resource/yuv/x.yuv"}
    fixtures.write_text(json.dumps({"fixtures": [entry]}))
    assert pb.main(["prepare_build.py", "fixtures", str(manifest), str(fixtures)]) == 1
    assert "missing from the SHA-256 manifest" in capsys.readouterr().err
    assert pb.main(["prepare_build.py", "bogus"]) == 64
