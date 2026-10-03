#!/usr/bin/env python3
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""The allowlist of the upstream parity guard: fragment grammar and attribution (ADR-1487)."""

from __future__ import annotations

import math
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))

from scripts.ci import upstream_parity_allowlist as allowlist  # noqa: E402
from scripts.dev import upstream_parity, upstream_parity_matrix  # noqa: E402

# An ADR id with no file, spelled so that the ADR citation scan does not read it
# as a citation of this source file.
MISSING_ADR = "ADR-" + "9" * 4
VALUE = "kind: value\nmetrics: ciede2000\nbound: 0.2\nadr: ADR-0001\nevidence: measured\n"


class FragmentDirectory(unittest.TestCase):
    """A scratch ``upstream_parity.d`` and an ADR directory with ADR-0001."""

    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        root = Path(self._tmp.name)
        self.fragments = root / "upstream_parity.d"
        self.adrs = root / "adr"
        self.fragments.mkdir()
        self.adrs.mkdir()
        (self.adrs / "0001-a-decision.md").write_text("# ADR-0001\n", encoding="utf-8")

    def write(self, name: str, text: str) -> Path:
        path = self.fragments / name
        path.write_text(text, encoding="utf-8")
        return path

    def parse(self, name: str, text: str) -> allowlist.Fragment:
        return allowlist.parse_fragment(self.write(name, text), self.adrs)


class Grammar(FragmentDirectory):
    def test_a_value_fragment_takes_its_run_scope_from_its_name(self) -> None:
        fragment = self.parse("ciede.chroma-422", VALUE)
        self.assertEqual(fragment.kind, "value")
        self.assertEqual(fragment.runs, ("F.ciede.*",))
        self.assertEqual(fragment.fixtures, ("*",))
        self.assertEqual(fragment.dispatch, ())
        self.assertEqual(fragment.bound, 0.2)
        self.assertEqual(fragment.adrs, ("ADR-0001",))
        self.assertFalse(fragment.pending)

    def test_scope_keys_are_comma_lists(self) -> None:
        text = (
            VALUE + "runs: F.ciede.*, M.*\nfixtures: nflx422p10, s1*\ndispatch: scalar, default\n"
        )
        fragment = self.parse("ciede.chroma-422", text)
        self.assertEqual(fragment.runs, ("F.ciede.*", "M.*"))
        self.assertEqual(fragment.fixtures, ("nflx422p10", "s1*"))
        self.assertEqual(fragment.dispatch, ("scalar", "default"))

    def test_the_upstream_reference_is_optional_and_checked(self) -> None:
        self.assertEqual(self.parse("ciede.a", VALUE).upstream, ())
        fragment = self.parse("ciede.b", VALUE + "upstream: Netflix/vmaf#1611, Netflix/vmaf#1563\n")
        self.assertEqual(fragment.upstream, ("Netflix/vmaf#1611", "Netflix/vmaf#1563"))
        with self.assertRaisesRegex(allowlist.AllowlistError, "upstream must be"):
            self.parse("ciede.c", VALUE + "upstream: #1611\n")

    def test_an_error_fragment_names_status_pairs(self) -> None:
        text = "kind: error\nstatus: ok/error, crash/error\nadr: ADR-0001\nevidence: measured\n"
        fragment = self.parse("cambi.small-frames", text)
        self.assertEqual(fragment.status, (("ok", "error"), ("crash", "error")))
        for bad in ("ok", "ok/ok", "ok/gone", "fine/error"):
            with (
                self.subTest(status=bad),
                self.assertRaisesRegex(allowlist.AllowlistError, "status"),
            ):
                self.parse("cambi.bad", text.replace("ok/error, crash/error", bad))

    def test_a_value_fragment_may_also_cover_a_run_that_ends_differently(self) -> None:
        fragment = self.parse("ciede.undefined", VALUE.replace("0.2", "inf") + "status: crash/ok\n")
        run = ("scalar", "nflx8", "F.ciede.default")
        crash = allowlist.Difference(
            "scalar", "nflx8", "F.ciede.default", "status", "", "", "crash", "ok", 0.0
        )
        self.assertTrue(allowlist.covers(fragment, crash))
        self.assertTrue(allowlist.covers(fragment, value(math.inf)))
        self.assertTrue(allowlist.classify([crash], [fragment], [run]).passed)

    def test_a_name_fragment_names_the_emitting_side(self) -> None:
        text = "kind: name\nmetrics: psnr_hvs*\nside: fork\nadr: ADR-0001\nevidence: measured\n"
        self.assertEqual(self.parse("psnr_hvs.luma-on-400", text).side, "fork")
        with self.assertRaisesRegex(allowlist.AllowlistError, "side must be"):
            self.parse("psnr_hvs.bad", text.replace("side: fork", "side: both"))

    def test_a_pending_fragment_needs_a_branch_and_no_adr(self) -> None:
        text = "kind: pending-revert\nmetrics: ciede2000\nbound: 1e-8\nbranch: fix/ciede-upstream-expression\nevidence: measured\n"
        fragment = self.parse("ciede.cast", text)
        self.assertTrue(fragment.pending)
        self.assertEqual(fragment.adrs, ())
        self.assertEqual(fragment.branch, "fix/ciede-upstream-expression")
        with self.assertRaisesRegex(allowlist.AllowlistError, "needs 'branch'"):
            self.parse("ciede.cast2", text.replace("branch: fix/ciede-upstream-expression\n", ""))
        with self.assertRaisesRegex(allowlist.AllowlistError, "needs 'bound', 'status' or 'side'"):
            self.parse("ciede.cast3", "kind: pending-port\nbranch: port/x\nevidence: e\n")

    def test_each_kind_rejects_keys_that_are_not_its_own(self) -> None:
        cases = {
            "kind: value\nmetrics: m\nadr: ADR-0001\nevidence: e\n": "needs 'bound'",
            "kind: value\nbound: 1\nadr: ADR-0001\nevidence: e\n": "needs 'metrics'",
            "kind: value\nmetrics: m\nbound: 1\nevidence: e\n": "needs 'adr'",
            VALUE + "branch: fix/x\n": "does not take 'branch'",
            "kind: error\nstatus: ok/error\nmetrics: m\nadr: ADR-0001\nevidence: e\n": "does not take 'metrics'",
            "kind: name\nmetrics: m\nadr: ADR-0001\nevidence: e\n": "needs 'side'",
        }
        for index, (text, message) in enumerate(cases.items()):
            with (
                self.subTest(message=message),
                self.assertRaisesRegex(allowlist.AllowlistError, message),
            ):
                self.parse(f"ciede.case{index}", text)

    def test_malformed_fragments_are_rejected(self) -> None:
        cases = {
            ("ciede.a", "metrics: m\nbound: 1\nadr: ADR-0001\nevidence: e\n"): "missing key 'kind'",
            (
                "ciede.b",
                "kind: value\nmetrics: m\nbound: 1\nadr: ADR-0001\n",
            ): "missing key 'evidence'",
            ("ciede.c", VALUE.replace("kind: value", "kind: tolerance")): "kind must be one of",
            ("ciede.d", VALUE + "tolerance: 1\n"): "unknown key 'tolerance'",
            ("ciede.e", VALUE + "bound: 2\n"): "duplicate key 'bound'",
            ("ciede.f", VALUE.replace("ADR-0001", MISSING_ADR)): f"{MISSING_ADR} has no file",
            ("ciede.g", VALUE + "dispatch: sse2\n"): "dispatch 'sse2'",
            ("ciede.h", VALUE + "fixtures: a,,b\n"): "empty item",
            ("Ciede.X", VALUE): "name must be <extractor>.<topic>",
            ("ciede", VALUE): "name must be <extractor>.<topic>",
            ("ciede.i", ""): "empty fragment",
            ("model.score", VALUE): "a model fragment names its 'runs'",
        }
        for (name, text), message in cases.items():
            with self.subTest(name=name), self.assertRaisesRegex(allowlist.AllowlistError, message):
                self.parse(name, text)

    def test_bound_limits(self) -> None:
        self.assertEqual(self.parse("ciede.zero", VALUE.replace("0.2", "0")).bound, 0.0)
        self.assertEqual(self.parse("ciede.inf", VALUE.replace("0.2", "inf")).bound, math.inf)
        for bad in ("-1e-9", "nan", "small"):
            with (
                self.subTest(bound=bad),
                self.assertRaisesRegex(allowlist.AllowlistError, "bound must be"),
            ):
                self.parse("ciede.bad", VALUE.replace("0.2", bad))

    def test_the_directory_loads_sorted_and_may_be_empty(self) -> None:
        self.assertEqual(allowlist.load_fragments(self.fragments, self.adrs), ())
        self.write("psnr.b", VALUE.replace("ciede2000", "psnr_cb"))
        self.write("ciede.a", VALUE)
        names = [fragment.name for fragment in allowlist.load_fragments(self.fragments, self.adrs)]
        self.assertEqual(names, ["ciede.a", "psnr.b"])

    def test_a_missing_directory_is_an_error(self) -> None:
        with self.assertRaisesRegex(allowlist.AllowlistError, "is missing"):
            allowlist.load_fragments(self.fragments / "absent", self.adrs)


def value(
    size: float, *, fixture: str = "nflx8", metric: str = "ciede2000", dispatch: str = "scalar"
) -> allowlist.Difference:
    return allowlist.Difference(
        dispatch, fixture, "F.ciede.default", "value", metric, "0", "1", "2", size
    )


RUN = ("scalar", "nflx8", "F.ciede.default")


class GeneratedPage(FragmentDirectory):
    """The page `scripts/docs/generate-upstream-parity-allowlist.py` renders."""

    PENDING_HEADING = "## Pending: differences that are to disappear"
    PENDING = (
        "kind: pending-revert\nmetrics: ciede2000\nbound: 0.2\n"
        "branch: fix/example\nevidence: measured\n"
    )

    def render(self) -> str:
        import importlib.util  # noqa: PLC0415

        path = ROOT / "scripts" / "docs" / "generate-upstream-parity-allowlist.py"
        spec = importlib.util.spec_from_file_location("allowlist_page", path)
        assert spec is not None and spec.loader is not None
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        page = module.render(self.fragments, self.adrs)
        assert isinstance(page, str)
        return page

    def test_the_pending_section_stays_when_nothing_is_pending(self) -> None:
        # docs/state.md and the guide link to the section's anchor.
        self.write("ciede.example", VALUE)
        page = self.render()
        self.assertIn(self.PENDING_HEADING, page)
        self.assertIn("None: every fragment above is a deliberate deviation", page)

    def test_a_pending_fragment_is_listed_under_the_section(self) -> None:
        self.write("ciede.example", VALUE)
        self.write("ciede.pending", self.PENDING)
        page = self.render()
        self.assertIn(self.PENDING_HEADING, page)
        self.assertNotIn("None: every fragment above", page)
        self.assertIn("`ciede.pending`", page.split(self.PENDING_HEADING, 1)[1])


class Attribution(FragmentDirectory):
    def test_a_difference_within_the_bound_is_covered(self) -> None:
        fragment = self.parse("ciede.a", VALUE)
        result = allowlist.classify([value(0.1)], [fragment], [RUN])
        self.assertTrue(result.passed)
        self.assertEqual(len(result.covered["ciede.a"]), 1)

    def test_a_difference_no_fragment_covers_fails(self) -> None:
        fragment = self.parse("ciede.a", VALUE)
        planted = value(1e-9, metric="psnr_y")
        result = allowlist.classify([value(0.1), planted], [fragment], [RUN])
        self.assertFalse(result.passed)
        self.assertEqual(result.uncovered, (planted,))

    def test_the_bound_is_inclusive_and_anything_above_it_fails(self) -> None:
        fragment = self.parse("ciede.a", VALUE)
        self.assertTrue(allowlist.classify([value(0.2)], [fragment], [RUN]).passed)
        above = value(math.nextafter(0.2, 1.0))
        result = allowlist.classify([above], [fragment], [RUN])
        self.assertFalse(result.passed)
        self.assertEqual(result.exceeded, ((above, fragment),))
        self.assertEqual(result.uncovered, ())

    def test_a_non_finite_side_needs_an_infinite_bound(self) -> None:
        finite = self.parse("ciede.a", VALUE)
        infinite = self.parse("ciede.b", VALUE.replace("0.2", "inf"))
        self.assertFalse(allowlist.classify([value(math.inf)], [finite], [RUN]).passed)
        self.assertTrue(allowlist.classify([value(math.inf)], [infinite], [RUN]).passed)

    def test_a_signed_zero_difference_needs_a_fragment_in_scope(self) -> None:
        self.assertFalse(allowlist.classify([value(0.0)], [], [RUN]).passed)
        self.assertTrue(
            allowlist.classify([value(0.0)], [self.parse("ciede.a", VALUE)], [RUN]).passed
        )

    def test_scope_limits_fixture_dispatch_and_run(self) -> None:
        fragment = self.parse("ciede.a", VALUE + "fixtures: nflx422p10\ndispatch: default\n")
        self.assertFalse(allowlist.covers(fragment, value(0.1)))
        self.assertFalse(allowlist.covers(fragment, value(0.1, fixture="nflx422p10")))
        self.assertTrue(
            allowlist.covers(fragment, value(0.1, fixture="nflx422p10", dispatch="default"))
        )
        other_run = allowlist.Difference(
            "default", "nflx422p10", "F.psnr.default", "value", "ciede2000", "0", "1", "2", 0.1
        )
        self.assertFalse(allowlist.covers(fragment, other_run))

    def test_a_fragment_without_differences_is_stale(self) -> None:
        fragment = self.parse("ciede.a", VALUE)
        result = allowlist.classify([], [fragment], [RUN])
        self.assertFalse(result.passed)
        self.assertEqual(result.stale, (fragment,))

    def test_a_fragment_with_no_run_in_scope_is_not_judged(self) -> None:
        fragment = self.parse("ciede.a", VALUE + "fixtures: bbb4k\n")
        result = allowlist.classify([], [fragment], [RUN])
        self.assertTrue(result.passed)
        self.assertEqual(result.unexercised, (fragment,))

    def test_a_deliberate_fragment_takes_a_difference_before_a_pending_one(self) -> None:
        deliberate = self.parse("ciede.products", VALUE.replace("0.2", "2e-11"))
        pending = self.parse(
            "ciede.cast",
            "kind: pending-revert\nmetrics: ciede2000\nbound: 1e-8\nbranch: fix/ciede\nevidence: e\n",
        )
        both = allowlist.classify([value(1e-11), value(5e-9)], [pending, deliberate], [RUN])
        self.assertTrue(both.passed)
        self.assertEqual(len(both.covered["ciede.products"]), 1)
        self.assertEqual(len(both.covered["ciede.cast"]), 1)
        # Once the revert is on master only the small differences remain: the
        # pending fragment covers them too, but they are not attributed to it.
        after = allowlist.classify([value(1e-11)], [pending, deliberate], [RUN])
        self.assertEqual(after.stale, (pending,))

    def test_among_deliberate_fragments_the_smallest_bound_wins(self) -> None:
        tight = self.parse("ciede.tight", VALUE.replace("0.2", "0.01"))
        wide = self.parse("ciede.wide", VALUE)
        result = allowlist.classify([value(0.005), value(0.1)], [wide, tight], [RUN])
        self.assertEqual(len(result.covered["ciede.tight"]), 1)
        self.assertEqual(len(result.covered["ciede.wide"]), 1)

    def test_status_and_name_differences(self) -> None:
        error = self.parse(
            "ciede.gray",
            "kind: error\nstatus: ok/error\nfixtures: nflx400\nadr: ADR-0001\nevidence: e\n",
        )
        name = self.parse(
            "ciede.names",
            "kind: name\nmetrics: ciede_extra\nside: fork\nadr: ADR-0001\nevidence: e\n",
        )
        status = allowlist.Difference(
            "scalar", "nflx400", "F.ciede.default", "status", "", "", "ok", "error", 0.0
        )
        fork_only = allowlist.Difference(
            "scalar", "nflx8", "F.ciede.default", "name", "ciede_extra", "*", "", "emitted", 0.0
        )
        runs = [RUN, ("scalar", "nflx400", "F.ciede.default")]
        self.assertTrue(allowlist.classify([status, fork_only], [error, name], runs).passed)
        reverse = dataclass_replace(status, upstream="error", fork="ok")
        upstream_only = dataclass_replace(fork_only, upstream="emitted", fork="")
        result = allowlist.classify([reverse, upstream_only], [error, name], runs)
        self.assertEqual(set(result.uncovered), {reverse, upstream_only})


def dataclass_replace(difference: allowlist.Difference, **changes: str) -> allowlist.Difference:
    fields = {**difference.__dict__, **changes}
    return allowlist.Difference(**fields)


class RepositoryAllowlist(unittest.TestCase):
    """The fragments the repository ships."""

    def test_every_fragment_parses_and_cites_an_existing_adr(self) -> None:
        fragments = allowlist.load_fragments()
        for fragment in fragments:
            with self.subTest(fragment=fragment.name):
                self.assertTrue(fragment.pending or fragment.adrs)
                self.assertTrue(fragment.evidence)

    def test_every_fragment_has_a_run_of_the_full_matrix_in_scope(self) -> None:
        runs = [
            (dispatch, run.fixture, run.name)
            for dispatch in upstream_parity.MODE_DISPATCH["full"]
            for run in upstream_parity_matrix.matrix("full")
        ]
        for fragment in allowlist.load_fragments():
            with self.subTest(fragment=fragment.name):
                self.assertTrue(any(allowlist.in_scope(fragment, *run) for run in runs))


if __name__ == "__main__":
    unittest.main()
