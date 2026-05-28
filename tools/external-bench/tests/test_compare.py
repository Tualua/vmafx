# SPDX-License-Identifier: BSD-3-Clause-Plus-Patent OR MIT
# Copyright 2026 Lusoris
"""Unit tests for the external-competitor benchmark harness.

Every test here stubs ``subprocess.run`` so the test suite never
depends on the user having ``x264-pVMAF`` or ``dover-mobile``
installed. Per ADR-0332 the harness ships only wrappers; tests
exercise the schema-merge + aggregation + rendering paths.
"""

from __future__ import annotations

import json
import os
import pathlib
import subprocess
import sys

import pytest

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))

import compare  # noqa: E402

# --- test fixture constants -------------------------------------------------

EXPECTED_FRAMES_DEFAULT = 2
EXPECTED_PLCC_CANNED = 0.95
EXPECTED_RUNTIME_TOTAL_SUM = 300
EXPECTED_FAILED_RC = 3
EXPECTED_MISSING_CORPUS_RC = 4
FIXTURE_WIDTH = 576
FIXTURE_HEIGHT = 324
FIXTURE_DIS_COUNT = 2

# --- helpers ---------------------------------------------------------------


def _canned_output(competitor: str, n_frames: int = 3) -> dict:
    return {
        "frames": [
            {
                "frame_idx": i,
                "predicted_vmaf_or_mos": 80.0 + i,
                "runtime_ms": 1.5,
            }
            for i in range(n_frames)
        ],
        "summary": {
            "competitor": competitor,
            "plcc": 0.95,
            "srocc": 0.93,
            "rmse": 2.5,
            "runtime_total_ms": 1.5 * n_frames,
            "params": 12345,
            "gflops": 0.5,
        },
    }


def _make_stub_runner(by_competitor: dict[str, dict]):
    """Build a stub `subprocess.run` that writes canned JSON to --out."""

    def stub_run(cmd, *_args, **_kwargs):
        # Find --out, write canned JSON for the competitor inferred
        # from the wrapper path.
        out_idx = cmd.index("--out")
        out_path = pathlib.Path(cmd[out_idx + 1])
        wrapper = cmd[1]  # bash <wrapper>
        for name, _ in compare.WRAPPERS.items():
            if name in wrapper:
                competitor = name
                break
        else:
            raise AssertionError(f"unknown wrapper in cmd: {cmd}")
        out_path.write_text(json.dumps(by_competitor.get(competitor, _canned_output(competitor))))
        return subprocess.CompletedProcess(args=cmd, returncode=0, stdout="", stderr="")

    return stub_run


def _write_fake_vmaf_tune(tmp_path: pathlib.Path) -> pathlib.Path:
    """Create a tiny `vmaf-tune` stand-in for shell-wrapper smoke tests."""
    fake = tmp_path / "vmaf-tune"
    fake.write_text(
        """#!/usr/bin/env python3
import json
import pathlib
import sys

out = pathlib.Path(sys.argv[sys.argv.index("--json") + 1])
out.write_text(json.dumps({
    "frames": [{
        "frame": 0,
        "predicted_mos": 4.25,
        "predicted_vmaf": 91.5,
        "runtime_ms": 3.0,
    }],
    "plcc": 0.75,
    "srocc": 0.70,
    "rmse": 1.25,
    "runtime_total_ms": 3.0,
    "params": 123,
    "gflops": 0.01,
}))
""",
        encoding="utf-8",
    )
    fake.chmod(0o755)
    return fake


def _run_fork_shell_wrapper(tmp_path: pathlib.Path, competitor: str) -> dict:
    fake_vmaf_tune = _write_fake_vmaf_tune(tmp_path)
    model_dir = tmp_path / "models"
    model_dir.mkdir()
    (model_dir / "nr_metric_v1.json").write_text("{}", encoding="utf-8")
    (model_dir / "fr_regressor_v2_ensemble_v1.json").write_text("{}", encoding="utf-8")

    ref = tmp_path / "ref.yuv"
    dis = tmp_path / "dis.yuv"
    ref.write_bytes(b"\x00" * 16)
    dis.write_bytes(b"\x00" * 16)
    out = tmp_path / f"{competitor}.json"

    cmd = [
        "bash",
        str(compare.WRAPPERS[competitor]),
        "--dis",
        str(dis),
        "--width",
        "16",
        "--height",
        "16",
        "--pixfmt",
        "yuv420p",
        "--fps",
        "24",
        "--out",
        str(out),
    ]
    if competitor == "fork-fr-regressor":
        cmd.extend(["--ref", str(ref)])

    env = os.environ.copy()
    env["EXTERNAL_BENCH_VMAF_TUNE"] = str(fake_vmaf_tune)
    env["EXTERNAL_BENCH_MODEL_DIR"] = str(model_dir)
    proc = subprocess.run(  # noqa: S603 - fixed argv runs repo wrapper with temp fixtures
        cmd,
        check=False,
        capture_output=True,
        text=True,
        env=env,
    )
    assert proc.returncode == 0, proc.stderr
    return json.loads(out.read_text(encoding="utf-8"))


# --- tests -----------------------------------------------------------------


def test_run_wrapper_parses_canned_output(tmp_path: pathlib.Path) -> None:
    item = compare.CorpusItem(
        name="t/0",
        ref=tmp_path / "ref.yuv",
        dis=tmp_path / "dis.yuv",
        width=576,
        height=324,
    )
    out_path = tmp_path / "out.json"
    runner = _make_stub_runner({"x264-pvmaf": _canned_output("x264-pvmaf", n_frames=2)})

    result = compare.run_wrapper("x264-pvmaf", item, out_path, runner=runner)

    assert result["summary"]["competitor"] == "x264-pvmaf"
    assert len(result["frames"]) == EXPECTED_FRAMES_DEFAULT
    assert result["summary"]["plcc"] == EXPECTED_PLCC_CANNED


def test_fork_nr_shell_wrapper_emits_registry_competitor_key(tmp_path: pathlib.Path) -> None:
    payload = _run_fork_shell_wrapper(tmp_path, "fork-nr-metric")

    assert payload["summary"]["competitor"] == "fork-nr-metric"
    assert compare.validate_wrapper_output("fork-nr-metric", payload) is payload


def test_fork_fr_shell_wrapper_emits_registry_competitor_key(tmp_path: pathlib.Path) -> None:
    payload = _run_fork_shell_wrapper(tmp_path, "fork-fr-regressor")

    assert payload["summary"]["competitor"] == "fork-fr-regressor"
    assert compare.validate_wrapper_output("fork-fr-regressor", payload) is payload


def test_run_wrapper_propagates_failure(tmp_path: pathlib.Path) -> None:
    item = compare.CorpusItem(
        name="t/0",
        ref=None,
        dis=tmp_path / "dis.yuv",
        width=10,
        height=10,
    )

    def stub_run(cmd, *_a, **_kw):
        return subprocess.CompletedProcess(
            args=cmd,
            returncode=EXPECTED_FAILED_RC,
            stdout="",
            stderr="boom",
        )

    with pytest.raises(RuntimeError, match=f"rc={EXPECTED_FAILED_RC}"):
        compare.run_wrapper(
            "dover-mobile",
            item,
            tmp_path / "out.json",
            runner=stub_run,
        )


def test_validate_wrapper_output_rejects_missing_summary() -> None:
    payload = {"frames": []}
    with pytest.raises(ValueError, match="summary"):
        compare.validate_wrapper_output("x264-pvmaf", payload)


def test_validate_wrapper_output_rejects_wrong_competitor() -> None:
    payload = _canned_output("dover-mobile")
    with pytest.raises(ValueError, match=r"summary\.competitor"):
        compare.validate_wrapper_output("x264-pvmaf", payload)


def test_validate_wrapper_output_rejects_bad_frame_value() -> None:
    payload = _canned_output("x264-pvmaf")
    payload["frames"][0]["runtime_ms"] = "slow"
    with pytest.raises(ValueError, match=r"frames\[0\]\.runtime_ms"):
        compare.validate_wrapper_output("x264-pvmaf", payload)


def test_run_wrapper_rejects_invalid_json(tmp_path: pathlib.Path) -> None:
    item = compare.CorpusItem(
        name="t/0",
        ref=None,
        dis=tmp_path / "dis.yuv",
        width=10,
        height=10,
    )

    def stub_run(cmd, *_a, **_kw):
        out_idx = cmd.index("--out")
        pathlib.Path(cmd[out_idx + 1]).write_text("{not json", encoding="utf-8")
        return subprocess.CompletedProcess(args=cmd, returncode=0, stdout="", stderr="")

    with pytest.raises(RuntimeError, match="invalid JSON"):
        compare.run_wrapper("dover-mobile", item, tmp_path / "out.json", runner=stub_run)


def test_run_wrapper_rejects_invalid_schema(tmp_path: pathlib.Path) -> None:
    item = compare.CorpusItem(
        name="t/0",
        ref=None,
        dis=tmp_path / "dis.yuv",
        width=10,
        height=10,
    )

    def stub_run(cmd, *_a, **_kw):
        out_idx = cmd.index("--out")
        pathlib.Path(cmd[out_idx + 1]).write_text(
            json.dumps({"frames": [], "summary": {"competitor": "dover-mobile"}}),
            encoding="utf-8",
        )
        return subprocess.CompletedProcess(args=cmd, returncode=0, stdout="", stderr="")

    with pytest.raises(RuntimeError, match="invalid schema"):
        compare.run_wrapper("dover-mobile", item, tmp_path / "out.json", runner=stub_run)


def test_aggregate_computes_means() -> None:
    results = [
        {
            "summary": {
                "plcc": 0.9,
                "srocc": 0.8,
                "rmse": 1.0,
                "runtime_total_ms": 100,
                "params": 1,
                "gflops": 0.1,
            }
        },
        {
            "summary": {
                "plcc": 1.0,
                "srocc": 0.9,
                "rmse": 2.0,
                "runtime_total_ms": 200,
                "params": 1,
                "gflops": 0.1,
            }
        },
    ]
    agg = compare.aggregate("x", results)
    assert agg.n_clips == EXPECTED_FRAMES_DEFAULT
    assert agg.plcc_mean == pytest.approx(0.95)
    assert agg.srocc_mean == pytest.approx(0.85)
    assert agg.rmse_mean == pytest.approx(1.5)
    assert agg.runtime_total_ms == EXPECTED_RUNTIME_TOTAL_SUM


def test_render_table_includes_all_competitors() -> None:
    aggs = [
        compare.CompetitorAggregate(
            competitor="fork-fr-regressor",
            n_clips=3,
            plcc_mean=0.99,
            srocc_mean=0.98,
            rmse_mean=1.2,
            runtime_total_ms=12.5,
            params=1000,
            gflops=0.05,
        ),
        compare.CompetitorAggregate(
            competitor="x264-pvmaf",
            n_clips=3,
            plcc_mean=0.91,
            srocc_mean=0.89,
            rmse_mean=3.4,
            runtime_total_ms=200.0,
            params=5000,
            gflops=2.0,
        ),
    ]
    table = compare.render_table(aggs)
    assert "fork-fr-regressor" in table
    assert "x264-pvmaf" in table
    assert "PLCC" in table
    assert "0.9900" in table
    assert "0.9100" in table


def test_main_emits_table_with_stubbed_wrappers(
    tmp_path: pathlib.Path,
    monkeypatch: pytest.MonkeyPatch,
    capsys: pytest.CaptureFixture[str],
) -> None:
    # Build a fake corpus (two distorted variants of one source).
    src = tmp_path / "netflix" / "src01_576x324_24fps" / "ref"
    src.mkdir(parents=True)
    (src / "ref.yuv").write_bytes(b"\x00" * 16)
    dis = tmp_path / "netflix" / "src01_576x324_24fps" / "dis"
    dis.mkdir(parents=True)
    (dis / "dis_a.yuv").write_bytes(b"\x00" * 16)
    (dis / "dis_b.yuv").write_bytes(b"\x00" * 16)

    runner = _make_stub_runner({})
    # Inject the runner into compare.run_wrapper via monkeypatch on
    # subprocess.run, since main() calls run_wrapper without a custom
    # runner kwarg.
    monkeypatch.setattr(compare.subprocess, "run", runner)

    rc = compare.main(
        [
            "--bvi-dvc-root",
            str(tmp_path / "does-not-exist"),
            "--netflix-public-root",
            str(tmp_path / "netflix"),
            "--out-json",
            str(tmp_path / "agg.json"),
        ]
    )
    assert rc == 0

    captured = capsys.readouterr()
    # All four competitors should appear with n_clips=2.
    for c in ("fork-fr-regressor", "fork-nr-metric", "x264-pvmaf", "dover-mobile"):
        assert c in captured.out

    agg_data = json.loads((tmp_path / "agg.json").read_text())
    assert {a["competitor"] for a in agg_data} == set(compare.WRAPPERS.keys())
    for a in agg_data:
        assert a["n_clips"] == FIXTURE_DIS_COUNT


def test_main_errors_clearly_when_corpus_missing(
    tmp_path: pathlib.Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    rc = compare.main(
        [
            "--bvi-dvc-root",
            str(tmp_path / "absent-bvi"),
            "--netflix-public-root",
            str(tmp_path / "absent-nf"),
        ]
    )
    assert rc == EXPECTED_MISSING_CORPUS_RC
    err = capsys.readouterr().err
    assert "no corpus found" in err
    assert "BVI-DVC" in err
    assert "Netflix Public" in err


def test_corpus_discovery_pairs_netflix_drop(tmp_path: pathlib.Path) -> None:
    root = tmp_path / "netflix"
    src = root / "src01_576x324_24fps"
    (src / "ref").mkdir(parents=True)
    (src / "ref" / "ref.yuv").write_bytes(b"\x00")
    (src / "dis").mkdir(parents=True)
    (src / "dis" / "d1.yuv").write_bytes(b"\x00")
    (src / "dis" / "d2.yuv").write_bytes(b"\x00")

    items = compare.discover_corpus(
        bvi_dvc_root=tmp_path / "no-bvi",
        netflix_root=root,
    )
    assert len(items) == FIXTURE_DIS_COUNT
    assert all(i.width == FIXTURE_WIDTH and i.height == FIXTURE_HEIGHT for i in items)
    assert all(i.ref is not None and i.ref.name == "ref.yuv" for i in items)
