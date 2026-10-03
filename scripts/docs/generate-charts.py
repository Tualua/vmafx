#!/usr/bin/env python3
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Render the documentation charts from their Vega-Lite specs and repository data.

A chart is a hand-written Vega-Lite spec, ``docs/charts/<slug>/spec.vl.json``,
whose rows this script builds from repository sources (ADR-1508). For every
chart it writes:

- ``docs/charts/<slug>/data.json``: the rows, built from the chart's sources;
- ``docs/assets/charts/<slug>.light.svg`` and ``.dark.svg``: static renders by
  vl-convert-python, the version pinned in ``docs/requirements-lock.txt``;
- the block between ``<!-- >>> CHART <slug>`` and ``<!-- <<< CHART <slug> -->``
  on each page that shows the chart: the two images, a caption and the data
  table.

It also writes ``docs/assets/charts/theme.json`` (the light and dark Vega
configs ``docs/javascripts/charts.js`` uses for the interactive chart),
``docs/javascripts/vendor/vega/vega-bundle.js`` (vl-convert's bundle of Vega,
Vega-Lite and vega-embed; its hash goes into that directory's
``vendor.json``) and ``docs/assets/charts/manifest.json`` (the hashes of every
render and of the inputs it was rendered from).

Flags:
    --write            rewrite every output.
    --check            exit 1 when an output differs. With vl-convert-python
                       installed, the SVGs and the bundle are rendered again
                       and compared byte for byte. Without it, the run says so
                       and compares them with the manifest's hashes, and the
                       manifest's input hash with the current spec, data and
                       theme, so an edited spec without a new render fails.
    --require-render   make a missing vl-convert-python an error (CI).

Wired into ``make docs-fragments-check`` / ``make docs-fragments-write``.
Exit status: 0 clean, 1 drift or a source error, 2 usage error.
"""

from __future__ import annotations

import argparse
import dataclasses
import hashlib
import importlib
import json
import os
import re
import sys
from collections import Counter
from collections.abc import Callable
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from scripts.ci.cross_backend_calibration import EXACT_TWINS, LIBM_TWINS  # noqa: E402
from scripts.ci.upstream_parity_allowlist import load_fragments  # noqa: E402

SOURCE_ROOT = ROOT
DOCS = ROOT / "docs"
CHARTS_DIR = DOCS / "charts"
ASSETS_DIR = DOCS / "assets" / "charts"
BUNDLE_DIR = DOCS / "javascripts" / "vendor" / "vega"
BUNDLE = BUNDLE_DIR / "vega-bundle.js"
MANIFEST = ASSETS_DIR / "manifest.json"
THEME = ASSETS_DIR / "theme.json"
COLOUR_REF = "@vx/"
Row = dict[str, Any]

# Static renders measure their text with Liberation Sans, which vl-convert
# carries, so the layout is the same on every machine; Arial and Helvetica
# have the same metrics where a browser shows the image. The interactive
# chart uses the site's font.
STATIC_FONT = "Liberation Sans, Arial, Helvetica, sans-serif"
LIVE_FONT = "Inter, system-ui, sans-serif"

# Colours: the site palette (docs/stylesheets/vmafx.css) and categorical and
# ordinal steps checked with the chart palette validator for both surfaces.
TOKENS = {
    "light": {
        "surface": "#ffffff",
        "ink": "#0f172a",
        "ink-2": "#475569",
        "ink-3": "#64748b",
        "grid": "#e2e8f0",
        "domain": "#cbd5e1",
        "strong": "#1d4ed8",
        "medium": "#6da7ec",
        "empty": "#eef2f8",
        "on-strong": "#ffffff",
        "on-medium": "#0f172a",
        "on-empty": "#475569",
        "cat-1": "#2a78d6",
        "cat-2": "#eb6834",
        "cat-3": "#1baf7a",
    },
    "dark": {
        "surface": "#0e1424",
        "ink": "#e2e8f0",
        "ink-2": "#a9b4c7",
        "ink-3": "#8391a8",
        "grid": "#1f2a44",
        "domain": "#2d3a5a",
        "strong": "#60a5fa",
        "medium": "#2f5aa8",
        "empty": "#19223b",
        "on-strong": "#0b1020",
        "on-medium": "#f8fafc",
        "on-empty": "#a9b4c7",
        "cat-1": "#3987e5",
        "cat-2": "#d95926",
        "cat-3": "#199e70",
    },
}


class ChartError(Exception):
    """A chart source, spec or page cannot be read."""


@dataclasses.dataclass(frozen=True)
class Chart:
    slug: str
    pages: tuple[str, ...]
    rows: Callable[[], list[Row]]
    table: Callable[[list[Row]], str]
    alt: Callable[[list[Row]], str]
    caption: str


# --------------------------------------------------------------- sources


BACKENDS = (("cuda", "CUDA"), ("sycl", "SYCL"), ("hip", "HIP"), ("metal", "Metal"))
STATUS_TEXT = {"exact": "exact", "libm bound": "libm bound", "not declared": "not declared"}


def _twin_backends() -> list[tuple[str, str]]:
    known = {key for key, _ in BACKENDS}
    seen = {b for backends in EXACT_TWINS.values() for b in backends}
    seen |= {b for backends in LIBM_TWINS.values() for b in backends}
    return list(BACKENDS) + [(b, b) for b in sorted(seen - known)]


def _twin_cell(feature: str, backend: str) -> tuple[str, str, str]:
    if backend in EXACT_TWINS.get(feature, ()):
        return "exact", "=", "tolerance 0 (scripts/ci/exact_twins.d/)"
    bound = LIBM_TWINS.get(feature, {}).get(backend)
    if bound is not None:
        return "libm bound", f"≤ {bound:g}", f"bound {bound:g}, LIBM_TWINS"
    return "not declared", "\u2013", "no fragment in scripts/ci/exact_twins.d/"


def twin_rows() -> list[Row]:
    features = sorted(set(EXACT_TWINS) | set(LIBM_TWINS))
    rows = []
    for f_index, feature in enumerate(features):
        for b_index, (backend, label) in enumerate(_twin_backends()):
            status, text, detail = _twin_cell(feature, backend)
            rows.append(
                {
                    "feature": feature,
                    "feature_order": f_index,
                    "backend": label,
                    "backend_order": b_index,
                    "status": status,
                    "label": text,
                    "detail": detail,
                }
            )
    return rows


def twin_table(rows: list[Row]) -> str:
    labels = sorted({(r["backend_order"], r["backend"]) for r in rows})
    head = "| Feature | " + " | ".join(label for _, label in labels) + " |\n"
    head += "| --- |" + " --- |" * len(labels) + "\n"
    cells: dict[str, dict[str, str]] = {}
    for row in rows:
        text = row["status"] if row["status"] != "libm bound" else f"libm bound ({row['label']})"
        cells.setdefault(row["feature"], {})[row["backend"]] = text
    body = "".join(
        f"| `{feature}` | " + " | ".join(cells[feature][label] for _, label in labels) + " |\n"
        for feature in sorted(cells)
    )
    return head + body


def twin_alt(rows: list[Row]) -> str:
    counts = Counter(row["status"] for row in rows)
    features = len({row["feature"] for row in rows})
    labels = [label for _, label in sorted({(r["backend_order"], r["backend"]) for r in rows})]
    backends = ", ".join(labels[:-1]) + " and " + labels[-1] if len(labels) > 1 else labels[0]
    return (
        f"Status of {features} features on {backends}: {counts['exact']} twins exact, "
        f"{counts['libm bound']} within a libm bound, {counts['not declared']} not declared."
    )


def parity_rows() -> list[Row]:
    fragments = load_fragments()
    per_feature = Counter(fragment.name.split(".")[0] for fragment in fragments)
    order = {
        name: i
        for i, (name, _) in enumerate(sorted(per_feature.items(), key=lambda kv: (-kv[1], kv[0])))
    }
    kinds = Counter(fragment.kind for fragment in fragments)
    kind_order = {kind: i for i, (kind, _) in enumerate(kinds.most_common())}
    rows = [
        {
            "feature": fragment.name.split(".")[0],
            "feature_order": order[fragment.name.split(".")[0]],
            "fragment": fragment.name,
            "kind": fragment.kind,
            "kind_order": kind_order[fragment.kind],
        }
        for fragment in fragments
    ]
    return sorted(rows, key=lambda r: (r["feature_order"], r["kind_order"], r["fragment"]))


def parity_table(rows: list[Row]) -> str:
    kinds = sorted({(r["kind_order"], r["kind"]) for r in rows})
    head = "| Extractor | " + " | ".join(f"`{k}`" for _, k in kinds) + " | Total |\n"
    head += "| --- |" + " ---: |" * (len(kinds) + 1) + "\n"
    counts = Counter((r["feature"], r["kind"]) for r in rows)
    features = sorted({(r["feature_order"], r["feature"]) for r in rows})
    body = ""
    for _, feature in features:
        cells = [str(counts[(feature, k)]) for _, k in kinds]
        total = sum(counts[(feature, k)] for _, k in kinds)
        body += f"| `{feature}` | " + " | ".join(cells) + f" | {total} |\n"
    return head + body


def parity_alt(rows: list[Row]) -> str:
    per_feature = Counter(r["feature"] for r in rows)
    top = ", ".join(f"{name} {count}" for name, count in per_feature.most_common(3))
    kinds = ", ".join(
        f"{count} {kind}" for kind, count in Counter(r["kind"] for r in rows).most_common()
    )
    return (
        f"{len(rows)} allowlist entries over {len(per_feature)} extractors ({kinds}); "
        f"the most are for {top}."
    )


SNAPSHOTS = (
    ("cpu", "testdata/scores_cpu_576.json"),
    ("sycl_a380", "testdata/scores_sycl_a380_576.json"),
    ("sycl_b580", "testdata/scores_sycl_b580_576.json"),
    ("sycl_uhd770", "testdata/scores_sycl_uhd770_576.json"),
)


def _snapshot_vmaf(relative: str) -> list[float]:
    try:
        frames = json.loads((SOURCE_ROOT / relative).read_text(encoding="utf-8"))["frames"]
        return [float(frame["metrics"]["vmaf"]) for frame in frames]
    except (OSError, KeyError, TypeError, ValueError) as err:
        raise ChartError(f"{relative}: {err}") from err


def frame_rows() -> list[Row]:
    series = {key: _snapshot_vmaf(path) for key, path in SNAPSHOTS}
    count = len(series["cpu"])
    if any(len(values) != count for values in series.values()):
        raise ChartError("the 576x324 snapshots hold different frame counts")
    return [{"frame": i, **{key: series[key][i] for key, _ in SNAPSHOTS}} for i in range(count)]


def frame_table(rows: list[Row]) -> str:
    head = "| Frame | CPU | SYCL, Arc A380 | SYCL, Arc B580 | SYCL, UHD Graphics 770 |\n"
    head += "| ---: | ---: | ---: | ---: | ---: |\n"
    body = "".join(
        f"| {r['frame']} | {r['cpu']:.6f} | {r['sycl_a380']:.6f} | {r['sycl_b580']:.6f} | {r['sycl_uhd770']:.6f} |\n"
        for r in rows
    )
    return head + body


def frame_alt(rows: list[Row]) -> str:
    cpu = [r["cpu"] for r in rows]
    diff = max(abs(r[key] - r["cpu"]) for r in rows for key, _ in SNAPSHOTS[1:])
    return (
        f"Per-frame VMAF of the Netflix 576x324 pair over {len(rows)} frames, from "
        f"{min(cpu):.3f} to {max(cpu):.3f}; the three SYCL snapshots differ from the "
        f"CPU snapshot by at most {diff:.6f}."
    )


CHARTS = (
    Chart(
        slug="twin-exactness",
        pages=("index.md", "backends/index.md"),
        rows=twin_rows,
        table=twin_table,
        alt=twin_alt,
        caption=(
            "Source: the fragments in `scripts/ci/exact_twins.d/` and `LIBM_TWINS` in "
            "`scripts/ci/cross_backend_calibration.py`. An exact twin returns the CPU "
            "extractor's bits (=) and is compared with tolerance 0; a libm-bound twin differs "
            "only through the math library, within the stated bound (≤); a dash means no twin "
            "of that backend is declared either way."
        ),
    ),
    Chart(
        slug="upstream-parity-allowlist",
        pages=("development/upstream-parity.md",),
        rows=parity_rows,
        table=parity_table,
        alt=parity_alt,
        caption="Source: the fragments in `scripts/ci/upstream_parity.d/`, one per recorded difference.",
    ),
    Chart(
        slug="per-frame-vmaf",
        pages=("development/netflix-benchmark-baselines.md",),
        rows=frame_rows,
        table=frame_table,
        alt=frame_alt,
        caption=(
            "Source: `testdata/scores_cpu_576.json` and the three "
            "`testdata/scores_sycl_*_576.json` snapshots. Hover a frame for every snapshot's value."
        ),
    ),
)


# --------------------------------------------------------------- rendering

BUNDLE_BANNER = (
    "/*! Vega, Vega-Lite and vega-embed with their dependencies, bundled by "
    "vl-convert-python (scripts/docs/generate-charts.py). BSD-3-Clause, ISC, MIT, "
    "Unlicense and 0BSD; the licence texts are in THIRD-PARTY-LICENSES.txt beside this file. */\n"
)
VEGA_LITE_VERSION = "6.4"


def vega_config(scheme: str, font: str) -> dict[str, Any]:
    """The Vega-Lite config of one colour scheme: text, axes, legend, gaps."""
    tokens = TOKENS[scheme]
    text = {
        "labelColor": tokens["ink-2"],
        "titleColor": tokens["ink-2"],
        "labelFontSize": 12,
        "titleFontSize": 12,
    }
    return {
        "background": "transparent",
        "font": font,
        "padding": 8,
        "view": {"stroke": None},
        "title": {"color": tokens["ink"], "fontSize": 14, "fontWeight": 600, "offset": 12},
        "axis": {
            **text,
            "titleFontWeight": 600,
            "gridColor": tokens["grid"],
            "domainColor": tokens["domain"],
            "tickColor": tokens["domain"],
        },
        "legend": {**text, "titleFontWeight": 600, "symbolStrokeWidth": 0},
        "text": {"color": tokens["ink"]},
    }


def substitute(spec: Any, tokens: dict[str, str]) -> Any:
    """Replace every ``@vx/<name>`` string in a spec with the scheme's colour."""
    root = json.loads(json.dumps(spec))
    stack: list[Any] = [root]
    while stack:
        node = stack.pop()
        pairs = list(node.items()) if isinstance(node, dict) else list(enumerate(node))
        for key, value in pairs:
            if isinstance(value, str) and value.startswith(COLOUR_REF):
                name = value[len(COLOUR_REF) :]
                if name not in tokens:
                    raise ChartError(f"unknown colour token {value!r}")
                node[key] = tokens[name]
            elif isinstance(value, (dict, list)):
                stack.append(value)
    return root


def load_spec(chart: Chart) -> dict[str, Any]:
    path = CHARTS_DIR / chart.slug / "spec.vl.json"
    try:
        spec = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as err:
        raise ChartError(f"{path}: {err}") from err
    if spec.get("data") != {"name": "table"}:
        raise ChartError(
            f'{path}: data must be {{"name": "table"}}; the generator supplies the rows'
        )
    return dict(spec)


def full_spec(chart: Chart, rows: list[Row], scheme: str) -> dict[str, Any]:
    spec = substitute(load_spec(chart), TOKENS[scheme])
    spec["data"] = {"values": rows}
    spec["description"] = chart.alt(rows)
    return dict(spec)


def load_renderer() -> Any | None:
    try:
        # Optional: only the docs lock installs it, so it is looked up at run time.
        return importlib.import_module("vl_convert")
    except ImportError:
        return None


def renderer_version(vlc: Any) -> str:
    return (
        f"vl-convert {vlc.__version__}; Vega {vlc.get_vega_version()}; Vega-Lite "
        f"{VEGA_LITE_VERSION}; vega-embed {vlc.get_vega_embed_version()}"
    )


def render_svg(vlc: Any, chart: Chart, rows: list[Row], scheme: str) -> str:
    spec = full_spec(chart, rows, scheme)
    config = vega_config(scheme, STATIC_FONT)
    svg = str(vlc.vegalite_to_svg(spec, vl_version=VEGA_LITE_VERSION, config=config))
    return svg if svg.endswith("\n") else svg + "\n"


def render_bundle(vlc: Any) -> str:
    text = BUNDLE_BANNER + str(vlc.javascript_bundle(vl_version=VEGA_LITE_VERSION))
    return text if text.endswith("\n") else text + "\n"


def sha256(text: str | bytes) -> str:
    data = text.encode("utf-8") if isinstance(text, str) else text
    return hashlib.sha256(data).hexdigest()


def inputs_hash(chart: Chart, rows: list[Row], renderer: str) -> str:
    payload = {
        "spec": load_spec(chart),
        "rows": rows,
        "description": chart.alt(rows),
        "config": {scheme: vega_config(scheme, STATIC_FONT) for scheme in TOKENS},
        "tokens": TOKENS,
        "renderer": renderer,
    }
    return sha256(json.dumps(payload, sort_keys=True, ensure_ascii=False))


def svg_size(svg: str) -> tuple[str, str]:
    match = re.search(r'<svg[^>]*\swidth="(\d+(?:\.\d+)?)"[^>]*\sheight="(\d+(?:\.\d+)?)"', svg)
    if match is None:
        raise ChartError("rendered SVG has no width and height")
    return match.group(1), match.group(2)


# ------------------------------------------------------------------ pages


def _rel(target: Path, page: str) -> str:
    return Path(os.path.relpath(target, (DOCS / page).parent)).as_posix()


def page_block(chart: Chart, rows: list[Row], page: str, size: tuple[str, str]) -> str:
    """The generated Markdown between a chart's sentinels on one page."""
    alt = chart.alt(rows)
    base = _rel(ASSETS_DIR, page)
    width, height = size
    images = "".join(
        f'![{alt}]({base}/{chart.slug}.{scheme}.svg#only-{scheme}){{ .vx-chart__static width="{width}" '
        f'height="{height}" }}\n'
        for scheme in TOKENS
    )
    return (
        "<!-- markdownlint-capture -->\n<!-- markdownlint-disable MD013 MD033 -->\n"
        '<figure class="vx-chart" markdown>\n\n'
        f"{images}\n"
        f"<figcaption markdown>{chart.caption}</figcaption>\n\n"
        "</figure>\n\n"
        '<details class="vx-chart__table" markdown>\n'
        "<summary>Data table</summary>\n\n"
        f"{chart.table(rows)}\n"
        "</details>\n"
        "<!-- markdownlint-restore -->\n"
    )


def splice(text: str, slug: str, block: str, page: str) -> str:
    begin = f"<!-- >>> CHART {slug}: generated, do not edit -->\n"
    end = f"<!-- <<< CHART {slug} -->"
    start = text.find(begin)
    stop = text.find(end, start + 1)
    if start < 0 or stop < 0:
        raise ChartError(
            f"docs/{page}: no sentinels for chart {slug!r} ({begin.strip()} ... {end})"
        )
    return text[: start + len(begin)] + block + text[stop:]


# ---------------------------------------------------------------- outputs


@dataclasses.dataclass
class Build:
    """Every output file and its expected content."""

    files: dict[Path, str] = dataclasses.field(default_factory=dict)
    notes: list[str] = dataclasses.field(default_factory=list)


def _svgs(vlc: Any | None, chart: Chart, rows: list[Row]) -> dict[str, str]:
    if vlc is not None:
        return {scheme: render_svg(vlc, chart, rows, scheme) for scheme in TOKENS}
    out = {}
    for scheme in TOKENS:
        path = ASSETS_DIR / f"{chart.slug}.{scheme}.svg"
        try:
            out[scheme] = path.read_text(encoding="utf-8")
        except OSError as err:
            raise ChartError(
                f"{path}: {err}; run --write with vl-convert-python installed"
            ) from err
    return out


def _committed(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8")
    except OSError:
        return ""


def _bundle(vlc: Any | None, build: Build) -> str:
    if vlc is not None:
        text = render_bundle(vlc)
        build.files[BUNDLE] = text
        return sha256(text)
    return sha256(BUNDLE.read_bytes()) if BUNDLE.is_file() else ""


def _vendor_json(bundle_hash: str) -> str:
    path = BUNDLE_DIR / "vendor.json"
    try:
        vendor = json.loads(path.read_text(encoding="utf-8"))
        vendor["files"]["vega-bundle.js"]["sha256"] = bundle_hash
    except (OSError, json.JSONDecodeError, KeyError, TypeError) as err:
        raise ChartError(f"{path}: {err}") from err
    return json.dumps(vendor, indent=2) + "\n"


def _theme() -> str:
    theme = {
        scheme: {"tokens": TOKENS[scheme], "config": vega_config(scheme, LIVE_FONT)}
        for scheme in TOKENS
    }
    return json.dumps(theme, indent=2, sort_keys=True) + "\n"


def _committed_renderer() -> str:
    try:
        return str(json.loads(MANIFEST.read_text(encoding="utf-8"))["renderer"])
    except (OSError, json.JSONDecodeError, KeyError, TypeError):
        return ""


def build_outputs(vlc: Any | None) -> Build:
    build = Build()
    renderer = renderer_version(vlc) if vlc is not None else _committed_renderer()
    manifest: dict[str, Any] = {"renderer": renderer, "charts": {}}
    pages: dict[str, str] = {}
    for chart in CHARTS:
        rows = chart.rows()
        build.files[CHARTS_DIR / chart.slug / "data.json"] = json.dumps(rows, indent=1) + "\n"
        svgs = _svgs(vlc, chart, rows)
        entry = {"inputs_sha256": inputs_hash(chart, rows, renderer)}
        for scheme, svg in svgs.items():
            build.files[ASSETS_DIR / f"{chart.slug}.{scheme}.svg"] = svg
            entry[f"{scheme}_svg_sha256"] = sha256(svg)
        manifest["charts"][chart.slug] = entry
        for page in chart.pages:
            text = pages.get(page) or _committed(DOCS / page)
            pages[page] = splice(
                text, chart.slug, page_block(chart, rows, page, svg_size(svgs["light"])), page
            )
    for page, text in pages.items():
        build.files[DOCS / page] = text
    manifest["bundle_sha256"] = _bundle(vlc, build)
    build.files[BUNDLE_DIR / "vendor.json"] = _vendor_json(manifest["bundle_sha256"])
    build.files[THEME] = _theme()
    build.files[MANIFEST] = json.dumps(manifest, indent=2, sort_keys=True) + "\n"
    return build


def check(build: Build) -> list[str]:
    drift = []
    for path, expected in sorted(build.files.items()):
        if _committed(path) != expected:
            drift.append(f"{path.relative_to(ROOT)} differs from a fresh build")
    return drift


def write(build: Build) -> None:
    for path, content in build.files.items():
        path.parent.mkdir(parents=True, exist_ok=True)
        if _committed(path) != content:
            path.write_text(content, encoding="utf-8")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true")
    mode.add_argument("--write", action="store_true")
    parser.add_argument("--require-render", action="store_true")
    args = parser.parse_args(argv)
    vlc = load_renderer()
    if vlc is None and (args.write or args.require_render):
        print(
            "generate-charts: vl-convert-python is missing; install docs/requirements-lock.txt",
            file=sys.stderr,
        )
        return 2
    try:
        build = build_outputs(vlc)
    except ChartError as err:
        print(f"generate-charts: {err}", file=sys.stderr)
        return 1
    if args.write:
        write(build)
        print(f"generate-charts: wrote {len(CHARTS)} charts")
        return 0
    if vlc is None:
        print(
            "generate-charts: vl-convert-python is not installed, so the SVGs and the bundle were not "
            "rendered again; they were checked against docs/assets/charts/manifest.json instead"
        )
    drift = check(build)
    for line in drift:
        print(f"generate-charts: {line}; run `make docs-fragments-write`", file=sys.stderr)
    if drift:
        return 1
    print(f"generate-charts: {len(CHARTS)} charts match their specs and data")
    return 0


if __name__ == "__main__":
    sys.exit(main())
