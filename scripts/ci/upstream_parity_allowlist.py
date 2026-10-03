#!/usr/bin/env python3
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Allowlist of the upstream parity guard (ADR-1487).

The guard (``scripts/dev/upstream_parity.py``) compares this tree's CPU
extractors with Netflix/vmaf at the recorded parity head, value by value at
``%.17g``. Code inherited from Netflix evaluates as Netflix's source does; a
difference is allowed only when a fragment in ``scripts/ci/upstream_parity.d/``
covers it. This module loads the fragments and attributes differences to them.
It needs no build and no upstream checkout.

One fragment per difference, named ``<extractor>.<topic>`` (``model.<topic>``
for a predicted score). ``key: value`` lines, the grammar of ``exact_twins.d``
(``cross_backend_calibration.parse_fragment_fields``):

``kind``      ``value`` (a number both trees emit differs), ``error`` (the run
              ends differently), ``name`` (a metric only one tree emits),
              ``pending-port`` (upstream has it, the port is not on master) or
              ``pending-revert`` (an unintended difference whose revert is not
              on master). A pending fragment names its ``branch``.
``runs``      globs over the run name (``F.<extractor>.<options>``,
              ``M.<model>``, ``C.<collection>``, ``B.<built-in>``). Default:
              ``F.<extractor>.*`` from the file name.
``metrics``   globs over the metric name; for ``value`` and ``name``.
``fixtures``  globs over the fixture name. Default ``*``.
``frames``    globs over where the value sits: a frame number, ``agg`` (an
              aggregate), ``pool-mean`` or ``pool-harmonic``. Default ``*``.
``dispatch``  ``scalar``, ``avx2``, ``default`` (comma list). Default: all.
``bound``     largest absolute difference covered, or ``inf`` (a NaN or an
              infinity on one side). For ``value``.
``status``    ``<upstream>/<fork>`` pairs of ``ok``, ``error``, ``crash``; for
              ``error``, and for a ``value`` whose upstream side is undefined
              behaviour that sometimes ends the run instead.
``side``      ``fork`` or ``upstream``: the tree that emits the name.
``adr``       the ADR(s) that record the deviation. Required unless pending.
``upstream``  the Netflix/vmaf pull request or issue, where one exists.
``branch``    the branch that ends a pending difference.
``evidence``  one line: what was measured.

A difference is attributed to one fragment: a deliberate one before a pending
one, then the smallest bound. A fragment no difference is attributed to is
stale and fails the guard, which is what removes a pending fragment once its
revert or port is on master.
"""

from __future__ import annotations

import dataclasses
import fnmatch
import math
import re
import sys
from collections.abc import Iterable, Sequence
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO))

from scripts.ci.cross_backend_calibration import (  # noqa: E402
    check_fragment_adrs,
    parse_fragment_fields,
)

ADR_DIR = REPO / "docs" / "adr"
FRAGMENTS_DIR = Path(__file__).parent / "upstream_parity.d"
DISPATCHES = ("scalar", "avx2", "default")
STATUSES = ("ok", "error", "crash")
PENDING_KINDS = ("pending-port", "pending-revert")
KINDS = ("value", "error", "name", *PENDING_KINDS)
KEYS = (
    "kind",
    "runs",
    "metrics",
    "fixtures",
    "frames",
    "dispatch",
    "bound",
    "status",
    "side",
    "adr",
    "upstream",
    "branch",
    "evidence",
)
_NAME = re.compile(r"^(?P<extractor>[a-z0-9_]+)\.(?P<topic>[a-z0-9-]+)$")
_UPSTREAM = re.compile(r"^Netflix/vmaf#\d+(, Netflix/vmaf#\d+)*$")
_BRANCH = re.compile(r"^[a-z0-9][a-z0-9._/-]*$")


class AllowlistError(ValueError):
    """An ``upstream_parity.d`` fragment or the directory itself is malformed."""


@dataclasses.dataclass(frozen=True)
class Fragment:
    """One allowed difference between this tree and upstream."""

    name: str
    kind: str
    runs: tuple[str, ...]
    metrics: tuple[str, ...]
    fixtures: tuple[str, ...]
    frames: tuple[str, ...]
    dispatch: tuple[str, ...]
    bound: float | None
    status: tuple[tuple[str, str], ...]
    side: str | None
    adrs: tuple[str, ...]
    upstream: tuple[str, ...]
    branch: str | None
    evidence: str

    @property
    def pending(self) -> bool:
        return self.kind in PENDING_KINDS


@dataclasses.dataclass(frozen=True)
class Difference:
    """One place where the two trees disagree.

    ``what`` is ``value`` (``size`` is the absolute difference, ``inf`` when a
    side is not finite), ``status`` (the run ended ``upstream`` / ``fork``) or
    ``name`` (``upstream`` or ``fork`` is empty: the tree without the metric).
    """

    dispatch: str
    fixture: str
    run: str
    what: str
    metric: str
    frame: str
    upstream: str
    fork: str
    size: float

    def where(self) -> str:
        place = f"{self.dispatch}/{self.fixture}/{self.run}"
        if self.what == "status":
            return place
        return f"{place} {self.metric}#{self.frame}"


@dataclasses.dataclass(frozen=True)
class Classification:
    """Differences sorted by what the allowlist says about them."""

    covered: dict[str, tuple[Difference, ...]]
    uncovered: tuple[Difference, ...]
    exceeded: tuple[tuple[Difference, Fragment], ...]
    stale: tuple[Fragment, ...]
    unexercised: tuple[Fragment, ...]

    @property
    def passed(self) -> bool:
        return not (self.uncovered or self.exceeded or self.stale)


def _split(value: str) -> tuple[str, ...]:
    items = tuple(item.strip() for item in value.split(","))
    if not all(items):
        raise ValueError("empty item")
    return items


def _parse_bound(name: str, value: str) -> float:
    try:
        bound = float(value)
    except ValueError as error:
        raise AllowlistError(f"{name}: bound must be a number or 'inf', got {value!r}") from error
    if math.isnan(bound) or bound < 0:
        raise AllowlistError(f"{name}: bound must be zero, positive or 'inf', got {value!r}")
    return bound


def _parse_status(name: str, value: str) -> tuple[tuple[str, str], ...]:
    pairs: list[tuple[str, str]] = []
    for item in _split(value):
        upstream, sep, fork = item.partition("/")
        if not sep or upstream not in STATUSES or fork not in STATUSES or upstream == fork:
            raise AllowlistError(
                f"{name}: status must be '<upstream>/<fork>' of two different values of "
                f"{', '.join(STATUSES)}, got {item!r}"
            )
        pairs.append((upstream, fork))
    return tuple(pairs)


def _parse_dispatch(name: str, value: str) -> tuple[str, ...]:
    items = _split(value)
    for item in items:
        if item not in DISPATCHES:
            raise AllowlistError(f"{name}: dispatch {item!r} is not one of {', '.join(DISPATCHES)}")
    return items


def _require(name: str, fields: dict[str, str], kind: str, needed: Sequence[str]) -> None:
    for key in needed:
        if key not in fields:
            raise AllowlistError(f"{name}: kind {kind} needs {key!r}")


def _forbid(name: str, fields: dict[str, str], kind: str, banned: Sequence[str]) -> None:
    for key in banned:
        if key in fields:
            raise AllowlistError(f"{name}: kind {kind} does not take {key!r}")


def _check_shape(name: str, fields: dict[str, str]) -> None:
    """The keys each kind needs and the keys it must not carry."""

    kind = fields["kind"]
    if kind == "value":
        _require(name, fields, kind, ("metrics", "bound", "adr"))
        _forbid(name, fields, kind, ("side", "branch"))
    elif kind == "error":
        _require(name, fields, kind, ("status", "adr"))
        _forbid(name, fields, kind, ("metrics", "bound", "side", "branch"))
    elif kind == "name":
        _require(name, fields, kind, ("metrics", "side", "adr"))
        _forbid(name, fields, kind, ("status", "bound", "branch"))
    else:
        _require(name, fields, kind, ("branch",))
        if "bound" in fields or "side" in fields:
            _require(name, fields, kind, ("metrics",))
        if not any(key in fields for key in ("bound", "status", "side")):
            raise AllowlistError(f"{name}: kind {kind} needs 'bound', 'status' or 'side'")


def _optional_fields(
    name: str, fields: dict[str, str]
) -> tuple[str | None, tuple[str, ...], str | None]:
    side = fields.get("side")
    if side is not None and side not in ("fork", "upstream"):
        raise AllowlistError(f"{name}: side must be 'fork' or 'upstream', got {side!r}")
    upstream = fields.get("upstream", "")
    if upstream and not _UPSTREAM.match(upstream):
        raise AllowlistError(
            f"{name}: upstream must be 'Netflix/vmaf#<n>[, Netflix/vmaf#<n>...]', got {upstream!r}"
        )
    branch = fields.get("branch")
    if branch is not None and not _BRANCH.match(branch):
        raise AllowlistError(f"{name}: branch {branch!r} is not a branch name")
    return side, _split(upstream) if upstream else (), branch


def parse_fragment(path: Path, adr_dir: Path = ADR_DIR) -> Fragment:
    """Parse and validate one ``<extractor>.<topic>`` fragment file."""

    match = _NAME.match(path.name)
    if not path.is_file() or match is None:
        raise AllowlistError(f"{path.name}: name must be <extractor>.<topic>")
    fields = parse_fragment_fields(path, KEYS, AllowlistError)
    for key in ("kind", "evidence"):
        if key not in fields:
            raise AllowlistError(f"{path.name}: missing key {key!r}")
    if fields["kind"] not in KINDS:
        raise AllowlistError(f"{path.name}: kind must be one of {', '.join(KINDS)}")
    _check_shape(path.name, fields)
    if match["extractor"] == "model" and "runs" not in fields:
        raise AllowlistError(f"{path.name}: a model fragment names its 'runs'")
    side, upstream, branch = _optional_fields(path.name, fields)
    try:
        runs = _split(fields.get("runs", f"F.{match['extractor']}.*"))
        metrics = _split(fields["metrics"]) if "metrics" in fields else ()
        fixtures = _split(fields.get("fixtures", "*"))
        frames = _split(fields.get("frames", "*"))
    except ValueError as error:
        raise AllowlistError(f"{path.name}: empty item in a comma list") from error
    return Fragment(
        name=path.name,
        kind=fields["kind"],
        runs=runs,
        metrics=metrics,
        fixtures=fixtures,
        frames=frames,
        dispatch=_parse_dispatch(path.name, fields["dispatch"]) if "dispatch" in fields else (),
        bound=_parse_bound(path.name, fields["bound"]) if "bound" in fields else None,
        status=_parse_status(path.name, fields["status"]) if "status" in fields else (),
        side=side,
        adrs=(
            check_fragment_adrs(path, fields["adr"], adr_dir, AllowlistError)
            if "adr" in fields
            else ()
        ),
        upstream=upstream,
        branch=branch,
        evidence=fields["evidence"],
    )


def load_fragments(
    directory: Path = FRAGMENTS_DIR, adr_dir: Path = ADR_DIR
) -> tuple[Fragment, ...]:
    """Load every fragment of *directory*, sorted by name.

    A missing directory is an error: it would turn every difference into an
    uncovered one for a reason that has nothing to do with the code. An empty
    directory is the goal, not an error.
    """

    if not directory.is_dir():
        raise AllowlistError(f"{directory}: allowlist fragment directory is missing")
    return tuple(parse_fragment(path, adr_dir) for path in sorted(directory.iterdir()))


def _any_glob(patterns: Iterable[str], text: str) -> bool:
    return any(fnmatch.fnmatchcase(text, pattern) for pattern in patterns)


def in_scope(fragment: Fragment, dispatch: str, fixture: str, run: str) -> bool:
    """True when the run lies in the fragment's dispatch, fixture and run scope."""

    if fragment.dispatch and dispatch not in fragment.dispatch:
        return False
    return _any_glob(fragment.fixtures, fixture) and _any_glob(fragment.runs, run)


def _kind_matches(fragment: Fragment, difference: Difference) -> bool:
    if difference.what == "status":
        return (difference.upstream, difference.fork) in fragment.status
    if not _any_glob(fragment.metrics, difference.metric):
        return False
    if difference.what == "name":
        emitter = "fork" if difference.fork else "upstream"
        return fragment.side == emitter
    return fragment.bound is not None and _any_glob(fragment.frames, difference.frame)


def covers(fragment: Fragment, difference: Difference) -> bool:
    """True when *fragment* allows *difference*, its bound included."""

    if not in_scope(fragment, difference.dispatch, difference.fixture, difference.run):
        return False
    if not _kind_matches(fragment, difference):
        return False
    return difference.what != "value" or difference.size <= (fragment.bound or 0.0)


def _nearest_miss(fragments: Sequence[Fragment], difference: Difference) -> Fragment | None:
    """The fragment that would cover a value difference but for its bound."""

    if difference.what != "value":
        return None
    candidates = [
        fragment
        for fragment in fragments
        if in_scope(fragment, difference.dispatch, difference.fixture, difference.run)
        and _kind_matches(fragment, difference)
    ]
    return max(candidates, key=lambda fragment: fragment.bound or 0.0, default=None)


def _preference(fragment: Fragment) -> tuple[bool, float, str]:
    return (fragment.pending, fragment.bound if fragment.bound is not None else 0.0, fragment.name)


def classify(
    differences: Iterable[Difference],
    fragments: Sequence[Fragment],
    executed: Iterable[tuple[str, str, str]],
) -> Classification:
    """Attribute every difference to a fragment, or report it.

    *executed* lists the ``(dispatch, fixture, run)`` triples that were
    compared. A fragment with no difference attributed to it is stale when at
    least one executed run lies in its scope, and unexercised when none does
    (a reduced run set, or a fixture that is not installed).
    """

    covered: dict[str, list[Difference]] = {fragment.name: [] for fragment in fragments}
    uncovered: list[Difference] = []
    exceeded: list[tuple[Difference, Fragment]] = []
    for difference in differences:
        matching = [fragment for fragment in fragments if covers(fragment, difference)]
        if matching:
            covered[min(matching, key=_preference).name].append(difference)
            continue
        miss = _nearest_miss(fragments, difference)
        if miss is None:
            uncovered.append(difference)
        else:
            exceeded.append((difference, miss))
    runs = tuple(executed)
    stale: list[Fragment] = []
    unexercised: list[Fragment] = []
    for fragment in fragments:
        if covered[fragment.name]:
            continue
        if any(in_scope(fragment, *run) for run in runs):
            stale.append(fragment)
        else:
            unexercised.append(fragment)
    return Classification(
        covered={name: tuple(items) for name, items in covered.items()},
        uncovered=tuple(uncovered),
        exceeded=tuple(exceeded),
        stale=tuple(stale),
        unexercised=tuple(unexercised),
    )
