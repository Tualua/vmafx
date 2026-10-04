# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""The pick rule: every command returns the lowest-bitrate passing encode.

The rule lives in ``vmaftune.recommend.lowest_passing_row``. ``recommend``,
its interval-aware variant, the live-mode picker of the CLI, the ladder's
default sampler and the ``fast`` objective all reach it; each has a test here
on rows where the lowest bitrate is NOT the highest CRF-order pick the older
"smallest CRF" rule returned, so the test fails on the old behaviour.
"""

from __future__ import annotations

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "src"))

from vmaftune import cli as cli_module
from vmaftune.fast import UNMET_OBJECTIVE_BASE, objective_value
from vmaftune.recommend import (
    RecommendRequest,
    UncertaintyAwareRequest,
    lowest_passing_row,
    pick_target_vmaf,
    pick_target_vmaf_with_uncertainty,
    recommend,
    row_bitrate_kbps,
)
from vmaftune.uncertainty import ConfidenceDecision


def _row(crf: int, vmaf: float, kbps: float, **extra) -> dict:
    row = {
        "encoder": "libx264",
        "preset": "medium",
        "src": "a.yuv",
        "crf": crf,
        "vmaf_score": vmaf,
        "bitrate_kbps": kbps,
        "exit_status": 0,
    }
    row.update(extra)
    return row


# A non-monotone sweep: CRF 20 passes cheaply (a lucky encode), CRF 28 passes
# at a higher bitrate. The old rule returned CRF 20 only by CRF order; the
# new one returns the cheaper of the two by bitrate, which is CRF 20 too, so
# the rows below put the cheap one at the HIGHER CRF to separate the rules.
ROWS = [
    _row(20, 97.0, 9000.0),
    _row(24, 94.0, 4000.0),
    _row(28, 93.0, 5200.0),  # passes, but costs more than CRF 24
    _row(32, 88.0, 2000.0),  # misses
]


def test_lowest_passing_row_takes_the_lowest_bitrate_not_the_lowest_crf() -> None:
    winner = lowest_passing_row(ROWS, 92.0)
    assert winner is not None and winner["crf"] == 24


def test_lowest_passing_row_none_when_nothing_clears() -> None:
    assert lowest_passing_row(ROWS, 99.0) is None


@pytest.mark.parametrize(
    ("rows", "crf"),
    [
        # equal bitrate: the higher VMAF wins
        ([_row(30, 93.0, 3000.0), _row(26, 95.0, 3000.0)], 26),
        # equal bitrate and VMAF: the lower CRF wins
        ([_row(30, 95.0, 3000.0), _row(26, 95.0, 3000.0)], 26),
    ],
)
def test_lowest_passing_row_tie_breaks(rows: list[dict], crf: int) -> None:
    winner = lowest_passing_row(rows, 92.0)
    assert winner is not None and winner["crf"] == crf


def test_pick_target_vmaf_returns_the_lowest_bitrate_passing_row() -> None:
    result = pick_target_vmaf(ROWS, 92.0)
    assert result.row["crf"] == 24
    assert result.margin == pytest.approx(94.0 - 92.0)
    assert "UNMET" not in result.predicate


def test_pick_target_vmaf_boundary_exactly_at_target_passes() -> None:
    assert pick_target_vmaf([_row(30, 92.0, 1000.0), _row(20, 99.0, 9000.0)], 92.0).row["crf"] == 30


def test_pick_target_vmaf_unmet_is_still_the_closest_miss() -> None:
    result = pick_target_vmaf(ROWS, 99.5)
    assert result.row["crf"] == 20 and "UNMET" in result.predicate


@pytest.mark.parametrize("bad", [None, float("nan"), "n/a"])
def test_missing_or_unusable_bitrate_is_an_error_not_a_skip(bad: object) -> None:
    rows = [_row(24, 94.0, 4000.0), {**_row(28, 93.0, 1.0), "bitrate_kbps": bad}]
    with pytest.raises(ValueError, match="bitrate_kbps"):
        pick_target_vmaf(rows, 92.0)
    with pytest.raises(ValueError, match="bitrate_kbps"):
        row_bitrate_kbps(rows[1])


def test_row_without_the_bitrate_key_is_an_error() -> None:
    row = _row(24, 94.0, 4000.0)
    del row["bitrate_kbps"]
    with pytest.raises(ValueError, match="bitrate_kbps"):
        pick_target_vmaf([row], 92.0)


def test_recommend_dispatcher_uses_the_rule() -> None:
    assert recommend(ROWS, RecommendRequest(target_vmaf=92.0)).row["crf"] == 24


def _interval_row(crf: int, vmaf: float, kbps: float, low: float, high: float) -> dict:
    return _row(crf, vmaf, kbps, vmaf_interval={"low": low, "high": high, "alpha": 0.05})


def test_uncertainty_tight_short_circuit_returns_the_lowest_bitrate_clearing_row() -> None:
    # Both rows are tight and clear 90; CRF 28 is cheaper. The old walk
    # (input order) stopped at CRF 18, the highest-bitrate clearing row.
    rows = [
        _interval_row(18, 96.0, 8000.0, 95.5, 96.5),
        _interval_row(28, 92.0, 3000.0, 91.5, 92.5),
    ]
    result = pick_target_vmaf_with_uncertainty(rows, UncertaintyAwareRequest(target_vmaf=90.0))
    assert result.decision is ConfidenceDecision.TIGHT
    assert result.row["crf"] == 28
    assert result.visited == 1, "ascending bitrate: the cheapest row is examined first"


def test_uncertainty_point_fallback_returns_the_lowest_bitrate_passing_row() -> None:
    rows = [  # middle-band widths: no short-circuit, point estimates decide
        _interval_row(18, 96.0, 8000.0, 94.0, 97.5),
        _interval_row(28, 92.0, 3000.0, 90.0, 93.5),
    ]
    result = pick_target_vmaf_with_uncertainty(rows, UncertaintyAwareRequest(target_vmaf=90.0))
    assert result.decision is ConfidenceDecision.MIDDLE
    assert result.row["crf"] == 28


def test_uncertainty_rows_without_bitrate_are_an_error() -> None:
    row = _interval_row(18, 96.0, 8000.0, 95.5, 96.5)
    del row["bitrate_kbps"]
    with pytest.raises(ValueError, match="bitrate_kbps"):
        pick_target_vmaf_with_uncertainty([row], UncertaintyAwareRequest(target_vmaf=90.0))


def test_live_recommend_picker_returns_the_lowest_bitrate_passing_encode() -> None:
    pick = cli_module._lowest_bitrate_passing(ROWS, 92.0)
    assert pick == ("a.yuv", "medium", 24, 94.0)


def test_live_recommend_picker_groups_per_source_in_row_order() -> None:
    rows = [
        _row(20, 80.0, 1000.0, src="a.yuv"),  # a.yuv never clears
        _row(24, 95.0, 5000.0, src="b.yuv"),
        _row(28, 93.0, 3000.0, src="b.yuv"),
    ]
    assert cli_module._lowest_bitrate_passing(rows, 92.0) == ("b.yuv", "medium", 28, 93.0)


def test_live_recommend_picker_none_when_nothing_clears() -> None:
    assert cli_module._lowest_bitrate_passing(ROWS, 99.5) is None


def test_ladder_default_sampler_picks_the_lowest_bitrate_row(monkeypatch) -> None:
    from vmaftune import corpus as corpus_module
    from vmaftune import ladder as ladder_module

    def fake_iter_rows(job, opts, **_kwargs):
        for preset, crf in job.cells:
            # CRF 25 and 30 both clear 92; CRF 30 costs less, CRF 20 most.
            vmaf = {20: 97.0, 25: 94.0, 30: 93.0}.get(crf, 80.0)
            kbps = {20: 9000.0, 25: 4000.0, 30: 2500.0}.get(crf, 1000.0)
            yield _row(crf, vmaf, kbps, preset=preset)

    monkeypatch.setattr(corpus_module, "iter_rows", fake_iter_rows)
    point = ladder_module._default_sampler(Path("dummy.yuv"), "libx264", 640, 360, 92.0)
    assert point.crf == 30
    assert point.bitrate_kbps == pytest.approx(2500.0)


@pytest.mark.parametrize(
    ("vmaf", "kbps", "target", "want"),
    [
        (94.0, 4000.0, 92.0, 4000.0),  # meets: its bitrate
        (92.0, 1234.5, 92.0, 1234.5),  # exactly at the target meets it
        (91.0, 4000.0, 92.0, UNMET_OBJECTIVE_BASE + 1.0),  # misses by 1
        (80.0, 100.0, 92.0, UNMET_OBJECTIVE_BASE + 12.0),
    ],
)
def test_fast_objective_values(vmaf: float, kbps: float, target: float, want: float) -> None:
    # The same table is pinned in the Go port (pkg/fast TestObjectiveValue).
    assert objective_value(vmaf, kbps, target) == pytest.approx(want)


def test_fast_objective_ranks_every_pass_before_every_miss() -> None:
    passing = objective_value(92.0, 250000.0, 92.0)
    miss = objective_value(91.999, 1.0, 92.0)
    assert passing < miss
    assert objective_value(91.0, 1.0, 92.0) < objective_value(80.0, 1.0, 92.0)


def test_fast_smoke_search_returns_the_cheapest_passing_crf() -> None:
    pytest.importorskip("optuna")
    from vmaftune.fast import TrialSample, fast_recommend

    def predictor(crf: int) -> TrialSample:
        # VMAF 100 - 0.6 * crf meets 90 up to CRF 16 (90.4); CRF 17 (89.8)
        # misses by 0.2. The old objective, |gap| + 1e-4 * kbps, preferred
        # CRF 17 (a smaller gap and a lower bitrate) and returned a miss.
        return TrialSample(
            crf=crf, predicted_vmaf=100.0 - 0.6 * crf, predicted_kbps=10000.0 - 100.0 * crf
        )

    result = fast_recommend(
        None, 90.0, smoke=True, predictor=predictor, crf_range=(10, 30), n_trials=60
    )
    assert result["recommended_crf"] == 16
    assert result["predicted_vmaf"] >= 90.0
