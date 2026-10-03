#!/usr/bin/env python3
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Positive, negative and boundary cases for scripts/docs/generate-charts.py.

The checks run on a copy of the chart files in a temporary tree, so a planted
defect never touches the repository. The render cases need vl-convert-python
(installed from docs/requirements-lock.txt) and are skipped, with that reason,
where it is missing; the hash path is tested everywhere.
"""

import contextlib
import importlib.util
import io
import json
import shutil
import sys
import tempfile
import unittest
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location(
    "generate_charts", ROOT / "scripts/docs/generate-charts.py"
)
assert SPEC is not None and SPEC.loader is not None
charts: Any = importlib.util.module_from_spec(SPEC)
sys.modules["generate_charts"] = charts
SPEC.loader.exec_module(charts)

PATHS = ("DOCS", "CHARTS_DIR", "ASSETS_DIR", "BUNDLE_DIR", "BUNDLE", "MANIFEST", "THEME", "ROOT")
RENDERER = charts.load_renderer()


class ChartTreeTests(unittest.TestCase):
    """Check and write on a temporary copy of every file the generator owns."""

    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="vmafx-charts-test-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        docs = self.root / "docs"
        for relative in ("charts", "assets/charts", "javascripts/vendor/vega"):
            shutil.copytree(ROOT / "docs" / relative, docs / relative)
        pages = {page for chart in charts.CHARTS for page in chart.pages}
        for page in pages:
            (docs / page).parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / "docs" / page, docs / page)
        saved = {name: getattr(charts, name) for name in PATHS}

        def restore() -> None:
            for name, value in saved.items():
                setattr(charts, name, value)

        self.addCleanup(restore)
        charts.ROOT = self.root
        charts.DOCS = docs
        charts.CHARTS_DIR = docs / "charts"
        charts.ASSETS_DIR = docs / "assets" / "charts"
        charts.BUNDLE_DIR = docs / "javascripts" / "vendor" / "vega"
        charts.BUNDLE = charts.BUNDLE_DIR / "vega-bundle.js"
        charts.MANIFEST = charts.ASSETS_DIR / "manifest.json"
        charts.THEME = charts.ASSETS_DIR / "theme.json"

    def drift(self, renderer: Any = None) -> list[str]:
        return list(charts.check(charts.build_outputs(renderer)))

    def test_committed_tree_matches_without_renderer(self) -> None:
        self.assertEqual(self.drift(), [])

    def test_edited_svg_is_found_without_renderer(self) -> None:
        svg = charts.ASSETS_DIR / "twin-exactness.dark.svg"
        svg.write_text(svg.read_text().replace("<svg ", '<svg data-edit="1" ', 1))
        self.assertTrue(any("manifest.json" in line for line in self.drift()))

    def test_spec_edited_without_render_is_found(self) -> None:
        spec = charts.CHARTS_DIR / "per-frame-vmaf" / "spec.vl.json"
        data = json.loads(spec.read_text())
        data["height"] = data["height"] + 1
        spec.write_text(json.dumps(data))
        self.assertTrue(any("manifest.json" in line for line in self.drift()))

    def test_edited_data_is_found(self) -> None:
        data = charts.CHARTS_DIR / "upstream-parity-allowlist" / "data.json"
        rows = json.loads(data.read_text())
        rows[0]["kind"] = "edited"
        data.write_text(json.dumps(rows, indent=1) + "\n")
        self.assertTrue(any("data.json" in line for line in self.drift()))

    def test_edited_page_block_is_found(self) -> None:
        page = charts.DOCS / "development" / "upstream-parity.md"
        page.write_text(
            page.read_text().replace("<summary>Data table</summary>", "<summary>Table</summary>")
        )
        self.assertTrue(any("upstream-parity.md" in line for line in self.drift()))

    def test_page_without_sentinels_is_an_error(self) -> None:
        page = charts.DOCS / "backends" / "index.md"
        page.write_text(page.read_text().replace("<!-- <<< CHART twin-exactness -->", ""))
        with self.assertRaises(charts.ChartError):
            charts.build_outputs(None)

    def test_spec_must_leave_the_data_to_the_generator(self) -> None:
        spec = charts.CHARTS_DIR / "twin-exactness" / "spec.vl.json"
        data = json.loads(spec.read_text())
        data["data"] = {"values": []}
        spec.write_text(json.dumps(data))
        with self.assertRaises(charts.ChartError):
            charts.build_outputs(None)

    def test_main_check_exit_status(self) -> None:
        saved = charts.load_renderer
        self.addCleanup(lambda: setattr(charts, "load_renderer", saved))
        charts.load_renderer = lambda: None
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(charts.main(["--check"]), 0)
            self.assertEqual(charts.main(["--check", "--require-render"]), 2)
            self.assertEqual(charts.main(["--write"]), 2)
            (charts.CHARTS_DIR / "per-frame-vmaf" / "data.json").write_text("[]\n")
            self.assertEqual(charts.main(["--check"]), 1)

    @unittest.skipIf(
        RENDERER is None, "vl-convert-python is not installed (docs/requirements-lock.txt)"
    )
    def test_fresh_render_matches_and_finds_an_edit(self) -> None:
        self.assertEqual(self.drift(RENDERER), [])
        svg = charts.ASSETS_DIR / "upstream-parity-allowlist.light.svg"
        svg.write_text(svg.read_text() + "<!-- edit -->\n")
        found = self.drift(RENDERER)
        self.assertTrue(any("upstream-parity-allowlist.light.svg" in line for line in found))


class TokenTests(unittest.TestCase):
    def test_tokens_are_replaced_in_nested_specs(self) -> None:
        spec: dict[str, Any] = {
            "layer": [
                {"mark": {"color": "@vx/ink"}},
                {"encoding": {"range": ["@vx/cat-1", "plain"]}},
            ]
        }
        out = charts.substitute(spec, charts.TOKENS["dark"])
        self.assertEqual(out["layer"][0]["mark"]["color"], charts.TOKENS["dark"]["ink"])
        self.assertEqual(
            out["layer"][1]["encoding"]["range"], [charts.TOKENS["dark"]["cat-1"], "plain"]
        )
        self.assertEqual(spec["layer"][0]["mark"]["color"], "@vx/ink")

    def test_unknown_token_is_an_error(self) -> None:
        with self.assertRaises(charts.ChartError):
            charts.substitute({"mark": {"color": "@vx/no-such-colour"}}, charts.TOKENS["light"])

    def test_both_schemes_define_the_same_tokens(self) -> None:
        self.assertEqual(set(charts.TOKENS["light"]), set(charts.TOKENS["dark"]))


class SourceTests(unittest.TestCase):
    def test_twin_rows_cover_every_feature_and_backend(self) -> None:
        rows = charts.twin_rows()
        features = {row["feature"] for row in rows}
        backends = {row["backend"] for row in rows}
        self.assertEqual(len(rows), len(features) * len(backends))
        self.assertTrue({"CUDA", "SYCL", "HIP", "Metal"} <= backends)
        statuses = {row["status"] for row in rows}
        self.assertTrue(statuses <= {"exact", "libm bound", "not declared"})

    def test_frame_rows_are_the_snapshots(self) -> None:
        rows = charts.frame_rows()
        snapshot = json.loads((ROOT / "testdata/scores_cpu_576.json").read_text())["frames"]
        self.assertEqual(
            [row["cpu"] for row in rows], [frame["metrics"]["vmaf"] for frame in snapshot]
        )


if __name__ == "__main__":
    unittest.main()
