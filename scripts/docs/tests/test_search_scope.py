#!/usr/bin/env python3
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Positive, negative and boundary cases for the search scope of ADR-1512.

The build cases run MkDocs with Material on a fixture that copies the
repository's real ``.meta.yml`` files and index front matter, then plant a new
ADR, a new research digest and a new user page. They need the docs toolchain
(``docs/requirements-lock.txt``) and are skipped, with that reason, where
MkDocs is missing; the Docs CI job installs it and runs them. Each build case
also builds once without the ``meta`` plugin and requires the planted ADR to
be indexed there, so the check is seen failing before it is seen passing.
"""

import contextlib
import importlib.util
import io
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3]))

from scripts.docs import check_search_scope as scope

TITLES_SPEC = importlib.util.spec_from_file_location(
    "generate_record_titles", Path(__file__).resolve().parents[1] / "generate-record-titles.py"
)
assert TITLES_SPEC is not None and TITLES_SPEC.loader is not None
titles = importlib.util.module_from_spec(TITLES_SPEC)
TITLES_SPEC.loader.exec_module(titles)

ROOT = Path(__file__).resolve().parents[3]
HAVE_MKDOCS = (
    importlib.util.find_spec("mkdocs") is not None
    and importlib.util.find_spec("material") is not None
)

META_FILES = (
    "adr/.meta.yml",
    "adr/by-tag/.meta.yml",
    "research/.meta.yml",
    "changelog-archive/.meta.yml",
)
FRONT_MATTER_PAGES = ("adr/README.md", "research/README.md", "rebase-notes.md", "state.md")


def front_matter(path: Path) -> str:
    """The front matter block of a page, with its closing line and a blank line."""
    text = path.read_text(encoding="utf-8")
    if not text.startswith("---\n"):
        return ""
    end = text.index("\n---\n", 4) + len("\n---\n")
    return text[:end] + "\n"


class LocationTests(unittest.TestCase):
    def test_record_pages_are_excluded(self) -> None:
        for page in (
            "adr/0403-mkdocs-strict-gate-validation-policy/",
            "adr/9999-new/",
            "research/2137-docs-site-toolchain-charts-diagrams/",
            "rebase-notes/",
            "state/",
            "changelog-archive/1.0.0-rc.1/",
        ):
            with self.subTest(page=page):
                self.assertTrue(scope.is_excluded(page))

    def test_index_and_user_pages_stay(self) -> None:
        for page in (
            "adr/",
            "adr/titles/",
            "adr/by-tag/",
            "adr/by-tag/cuda/",
            "research/",
            "research/titles/",
            "",
            "usage/cli/",
            "development/state-tools/",
        ):
            with self.subTest(page=page):
                self.assertFalse(scope.is_excluded(page))

    def test_anchor_is_dropped(self) -> None:
        self.assertEqual(scope.page_of("adr/0403-x/#context"), "adr/0403-x/")

    def test_findings_report_both_directions(self) -> None:
        complete = set(scope.REQUIRED) | {"adr/by-tag/cuda/"}
        self.assertEqual(scope.findings(complete), [])
        self.assertTrue(scope.findings(complete | {"adr/0001-x/"}))
        self.assertTrue(scope.findings(complete - {"adr/"}))
        self.assertTrue(scope.findings(complete - {"adr/by-tag/cuda/"}))


@unittest.skipUnless(
    HAVE_MKDOCS, "MkDocs with Material is not installed (docs/requirements-lock.txt)"
)
class FixtureBuildTests(unittest.TestCase):
    """A MkDocs build with the repository's search scope and planted pages."""

    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="vmafx-search-scope-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        docs = self.root / "docs"
        pages = {
            "index.md": "# Home\n\nThe landing page.\n",
            "getting-started.md": "# Getting started\n\nInstall it.\n",
            "usage/cli.md": "# CLI\n\nEvery flag.\n",
            "usage/planted-user-page.md": "# Planted user page\n\nplantedusertoken\n",
            "adr/0001-existing.md": "# ADR-0001: Existing\n\nexistingadrtoken\n",
            "adr/9999-planted-new-adr.md": "# Planted decision\n\nplantedadrtoken\n",
            "adr/by-tag/index.md": "# ADRs by tag\n\n[cuda](cuda.md)\n",
            "adr/by-tag/cuda.md": "# cuda\n\nPlanted decision\n",
            "research/9999-planted-digest.md": "# Research-9999: Planted\n\nplanteddigesttoken\n",
            "changelog-archive/1.0.0-rc.1.md": "# 1.0.0-rc.1\n\narchivetoken\n",
        }
        for relative, body in pages.items():
            target = docs / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(body, encoding="utf-8")
        for relative in FRONT_MATTER_PAGES:
            title = Path(relative).stem.replace("-", " ").title()
            (docs / relative).write_text(
                front_matter(ROOT / "docs" / relative) + f"# {title}\n\nThe index of records.\n",
                encoding="utf-8",
            )
        for relative in META_FILES:
            shutil.copyfile(ROOT / "docs" / relative, docs / relative)

    def build(self, *, meta: bool) -> set[str]:
        from mkdocs.commands.build import build  # noqa: PLC0415 - optional toolchain
        from mkdocs.config import load_config  # noqa: PLC0415

        plugins = ["search", "material/meta"] if meta else ["search"]
        config_file = self.root / "mkdocs.yml"
        config_file.write_text(
            "site_name: fixture\nuse_directory_urls: true\ntheme:\n  name: material\n"
            "plugins:\n" + "".join(f"  - {name}\n" for name in plugins),
            encoding="utf-8",
        )
        site = self.root / ("site" if meta else "site-without-meta")
        config = load_config(str(config_file), site_dir=str(site))
        build(config)
        return scope.load_pages(site)

    def test_planted_adr_needs_the_meta_files_to_leave_the_index(self) -> None:
        without = self.build(meta=False)
        self.assertIn("adr/9999-planted-new-adr/", without)
        self.assertTrue(scope.findings(without))

    def test_planted_record_is_excluded_and_planted_user_page_indexed(self) -> None:
        pages = self.build(meta=True)
        self.assertNotIn("adr/9999-planted-new-adr/", pages)
        self.assertNotIn("adr/0001-existing/", pages)
        self.assertNotIn("research/9999-planted-digest/", pages)
        self.assertNotIn("changelog-archive/1.0.0-rc.1/", pages)
        self.assertNotIn("rebase-notes/", pages)
        self.assertNotIn("state/", pages)
        self.assertIn("usage/planted-user-page/", pages)
        self.assertIn("adr/", pages)
        self.assertIn("adr/by-tag/cuda/", pages)
        self.assertIn("research/", pages)


class TitleListTests(unittest.TestCase):
    """generate-record-titles.py: a new record reaches its title list."""

    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="vmafx-record-titles-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        for relative, body in {
            "docs/adr/README.md": "# ADR index\n",
            "docs/adr/0000-template.md": "# ADR-0000: Template\n",
            "docs/adr/0001-first.md": "<!-- markdownlint-disable MD013 -->\n# ADR-0001: First `code_span` decision\n",
            "docs/research/README.md": "# Research digests\n",
            "docs/research/0001-digest.md": "# Research-0001: A digest\n",
        }.items():
            path = self.root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(body, encoding="utf-8")

    def run_titles(self, mode: str) -> int:
        with contextlib.redirect_stderr(io.StringIO()):
            return int(titles.main([mode, "--root", str(self.root)]))

    def test_lists_hold_every_record_and_stay_indexed(self) -> None:
        self.assertEqual(self.run_titles("--write"), 0)
        adr = (self.root / "docs/adr/titles.md").read_text()
        self.assertIn("## ADR-0001: First `code_span` decision", adr)
        self.assertIn("[0001-first](0001-first.md)", adr)
        self.assertNotIn("Template", adr)
        self.assertIn("search:\n  exclude: false", adr)
        research = (self.root / "docs/research/titles.md").read_text()
        self.assertIn("## Research-0001: A digest", research)
        self.assertNotIn("Research digests", research.split("# Research digest titles", 1)[1])
        self.assertEqual(self.run_titles("--check"), 0)

    def test_planted_record_makes_the_list_stale(self) -> None:
        self.assertEqual(self.run_titles("--write"), 0)
        (self.root / "docs/adr/0002-planted.md").write_text(
            "# ADR-0002: Planted\n", encoding="utf-8"
        )
        self.assertEqual(self.run_titles("--check"), 1)
        self.assertEqual(self.run_titles("--write"), 0)
        self.assertIn("## ADR-0002: Planted", (self.root / "docs/adr/titles.md").read_text())

    def test_record_without_title_is_an_error(self) -> None:
        (self.root / "docs/research/0002-untitled.md").write_text("no heading\n", encoding="utf-8")
        self.assertEqual(self.run_titles("--write"), 1)


if __name__ == "__main__":
    unittest.main()
