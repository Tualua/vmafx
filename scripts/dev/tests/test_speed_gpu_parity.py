# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2

"""Positive, negative, and boundary controls for the SpEED GPU parity script."""

from __future__ import annotations

import contextlib
import importlib.util
import io
import json
import os
import sys
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory
from unittest import mock

ROOT = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location(
    "speed_gpu_parity", ROOT / "scripts/dev/speed_gpu_parity.py"
)
assert SPEC is not None and SPEC.loader is not None
SGP = importlib.util.module_from_spec(SPEC)
# dataclasses resolve their module through sys.modules.
sys.modules[SPEC.name] = SGP
SPEC.loader.exec_module(SGP)

FIXTURE = SGP.Fixture("t", Path("r.yuv"), Path("d.yuv"), 576, 324, 2)


def _frames(*values: float) -> list[dict[str, float]]:
    return [{"speed_temporal": value} for value in values]


class CompareTests(unittest.TestCase):
    def test_identical_frames_are_all_exact(self) -> None:
        result = SGP.compare(_frames(0.0, 24.5), _frames(0.0, 24.5))
        self.assertEqual(result["speed_temporal"], SGP.Difference(2, 2, 0.0))

    def test_one_ulp_frame_is_counted_and_measured(self) -> None:
        result = SGP.compare(_frames(0.0, 24.5), _frames(0.0, 24.500001))
        diff = result["speed_temporal"]
        self.assertEqual((diff.exact, diff.total), (1, 2))
        self.assertAlmostEqual(diff.max_abs, 1e-6, places=9)

    def test_frame_count_mismatch_is_an_error(self) -> None:
        with self.assertRaises(ValueError):
            SGP.compare(_frames(0.0, 1.0), _frames(0.0))

    def test_empty_run_is_an_error(self) -> None:
        with self.assertRaises(ValueError):
            SGP.compare([], [])

    def test_output_set_mismatch_is_an_error(self) -> None:
        with self.assertRaises(ValueError):
            SGP.compare(_frames(1.0), [{"speed_chroma_u": 1.0}])

    def test_nan_delta_is_never_within_a_bound(self) -> None:
        # max() alone would return 1.0 here and hide the NaN frame.
        result = SGP.compare(_frames(1.0, 2.0), _frames(2.0, float("nan")))
        self.assertEqual(result["speed_temporal"].max_abs, float("inf"))


class CommandTests(unittest.TestCase):
    def test_cpu_command_uses_the_cpu_extractor_and_threads(self) -> None:
        command = SGP.vmaf_command(Path("vmaf"), FIXTURE, "speed_chroma", None, 2, Path("o"), 16)
        self.assertEqual(
            command[-6:], ["--backend", "cpu", "--threads", "16", "--feature", "speed_chroma"]
        )
        self.assertIn("max", command)

    def test_gpu_command_names_the_twin(self) -> None:
        command = SGP.vmaf_command(Path("vmaf"), FIXTURE, "speed_chroma", "cuda", 2, Path("o"), 16)
        self.assertEqual(command[-4:], ["--backend", "cuda", "--feature", "speed_chroma_cuda"])

    def test_ms_per_frame_removes_startup(self) -> None:
        self.assertAlmostEqual(SGP.ms_per_frame(1.0, 1.2), 10.0)

    def test_zero_reps_is_rejected(self) -> None:
        with self.assertRaises(SystemExit):
            SGP.parse(["--backend", "sycl", "--reps", "0"])

    def test_features_default_to_speed_and_accept_other_twins(self) -> None:
        self.assertEqual(SGP.parse(["--backend", "sycl"]).feature, SGP.FEATURES)
        args = SGP.parse(["--backend", "sycl", "--feature", "ssimulacra2", "--feature", "x"])
        self.assertEqual(args.feature, ("ssimulacra2", "x"))

    def test_negative_or_nan_bound_is_rejected(self) -> None:
        for bound in ("-1e-9", "nan"):
            with self.assertRaises(SystemExit):
                SGP.parse(["--backend", "sycl", "--max-abs-diff", bound])


def _touch_fixtures(root: Path, *, bbb: bool = True) -> tuple[Path, Path]:
    """Empty stand-ins for the fixture files; the fake CLI never reads them."""
    netflix, bbb_dir = root / "netflix", root / "bbb"
    netflix.mkdir()
    bbb_dir.mkdir()
    for name in ("src01_hrc00_576x324.yuv", "src01_hrc01_576x324.yuv"):
        (netflix / name).touch()
    if bbb:
        for name in ("ref_3840x2160_200f.yuv", "dis_3840x2160_200f.yuv"):
            (bbb_dir / name).touch()
    return netflix, bbb_dir


class ExecutablePathTests(unittest.TestCase):
    """``--vmaf build/tools/vmaf``, the default and the form every documented
    command uses, was refused by ``safe_subprocess`` ("allowlisted executable
    must be bare or absolute"), so the script exited 2 before it ran anything."""

    def test_relative_path_with_a_directory_is_anchored_at_the_working_directory(self) -> None:
        args = SGP.parse(["--backend", "cuda", "--vmaf", "build-cuda/tools/vmaf"])
        self.assertEqual(args.vmaf, Path.cwd() / "build-cuda/tools/vmaf")
        self.assertTrue(SGP.parse(["--backend", "cuda"]).vmaf.is_absolute())

    def test_absolute_path_and_bare_name_are_kept(self) -> None:
        absolute = Path.cwd() / "elsewhere" / "vmaf"
        self.assertEqual(SGP.parse(["--backend", "cuda", "--vmaf", str(absolute)]).vmaf, absolute)
        # A bare name stays bare: safe_subprocess resolves it on PATH.
        self.assertEqual(SGP.parse(["--backend", "cuda", "--vmaf", "vmaf"]).vmaf, Path("vmaf"))

    @unittest.skipIf(os.name == "nt", "the stand-in executable is a POSIX script")
    def test_relative_vmaf_runs_through_the_real_command_validation(self) -> None:
        # No mock of run_command here: the validation that refused the path is
        # the thing under test.
        stand_in = (
            f"#!{sys.executable}\n"
            "import json, sys\n"
            "out = sys.argv[sys.argv.index('-o') + 1]\n"
            "frames = [{'metrics': {'speed_temporal': 7.0}}]\n"
            "json.dump({'frames': frames}, open(out, 'w', encoding='utf-8'))\n"
        )
        with TemporaryDirectory() as tmp:
            tool = Path(tmp) / "tools" / "vmaf"
            tool.parent.mkdir()
            tool.write_text(stand_in, encoding="utf-8")
            tool.chmod(0o755)
            netflix, bbb_dir = _touch_fixtures(Path(tmp))
            previous = Path.cwd()
            os.chdir(tmp)
            try:
                relative = ["--vmaf", "tools/vmaf", "--feature", "speed_temporal"]
                relative += ["--netflix-dir", str(netflix), "--bbb-dir", str(bbb_dir)]
                status = SGP.main(["--backend", "sycl", "--no-timing", *relative])
            finally:
                os.chdir(previous)
        self.assertEqual(status, 0)


class MainTests(unittest.TestCase):
    def _run(self, gpu_value: float, *extra: str, bbb: bool = True) -> int:
        def fake_run(command: list[str], **_: object) -> None:
            out = Path(command[command.index("-o") + 1])
            value = gpu_value if "cpu" not in command else 7.0
            frames = [{"metrics": {"speed_temporal": value}}]
            if "speed_chroma" in command[-1]:
                frames = [{"metrics": {"speed_chroma_u": value}}]
            if "ssimulacra2" in command[-1]:
                frames = [{"metrics": {"ssimulacra2": value}}]
            out.write_text(json.dumps({"frames": frames}), encoding="utf-8")

        # SGP is loaded through importlib, so mypy sees its attributes as Any.
        with TemporaryDirectory() as tmp, mock.patch.object(SGP, "run_command", fake_run):
            netflix, bbb_dir = _touch_fixtures(Path(tmp), bbb=bbb)
            status = SGP.main(
                [
                    "--backend",
                    "sycl",
                    "--no-timing",
                    "--vmaf",
                    str(Path(tmp) / "v"),
                    "--netflix-dir",
                    str(netflix),
                    "--bbb-dir",
                    str(bbb_dir),
                    *extra,
                ]
            )
        return int(status)

    def test_missing_fixture_is_a_usage_error_naming_the_option(self) -> None:
        stderr = io.StringIO()
        with contextlib.redirect_stderr(stderr):
            self.assertEqual(self._run(7.0, bbb=False), 2)
        self.assertIn("ref_3840x2160_200f.yuv", stderr.getvalue())
        self.assertIn("--skip-fixture 3840x2160", stderr.getvalue())

    def test_skipped_fixture_is_reported_with_its_reason(self) -> None:
        stdout = io.StringIO()
        skip = ("--skip-fixture", "3840x2160", "--skip-reason", "BBB 4K not on this host")
        with contextlib.redirect_stdout(stdout):
            self.assertEqual(self._run(7.0, *skip, bbb=False), 0)
        self.assertIn("SKIPPED fixture 3840x2160: BBB 4K not on this host", stdout.getvalue())
        self.assertNotIn("3840x2160 speed", stdout.getvalue())

    def test_skip_without_reason_and_reason_without_skip_are_refused(self) -> None:
        for extra in (("--skip-fixture", "3840x2160"), ("--skip-reason", "why")):
            with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as raised:
                self._run(7.0, *extra)
            self.assertEqual(raised.exception.code, 2)

    def test_skipping_every_fixture_is_an_error(self) -> None:
        both = ("--skip-fixture", "576x324", "--skip-fixture", "3840x2160", "--skip-reason", "x")
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(self._run(7.0, *both), 2)

    def test_identical_twin_exits_zero(self) -> None:
        self.assertEqual(self._run(7.0), 0)

    def test_differing_twin_exits_one(self) -> None:
        self.assertEqual(self._run(7.5), 1)

    def test_difference_within_the_bound_exits_zero(self) -> None:
        bound = ("--feature", "ssimulacra2", "--max-abs-diff", "1e-9")
        self.assertEqual(self._run(7.0 + 1e-12, *bound), 0)

    def test_difference_at_the_bound_exits_zero_and_past_it_one(self) -> None:
        self.assertEqual(self._run(7.5, "--feature", "ssimulacra2", "--max-abs-diff", "0.5"), 0)
        self.assertEqual(self._run(7.5, "--feature", "ssimulacra2", "--max-abs-diff", "0.49"), 1)


if __name__ == "__main__":
    unittest.main()
