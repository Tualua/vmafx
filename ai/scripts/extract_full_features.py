#!/usr/bin/env python3
# Copyright 2026 Lusoris and Claude (Anthropic)
# SPDX-License-Identifier: BSD-3-Clause-Plus-Patent
"""Extract the FULL_FEATURES (Research-0026) over the Netflix corpus.

Phase 1 (PR #185) added ``FULL_FEATURES`` to
``ai/data/feature_extractor.py``. This script is the Phase 2
re-extraction driver that produces the parquet feeding the
correlation / mutual-information / feature-importance analysis.

The output schema is one row per (pair, frame):

  source           : str   (e.g. "BigBuckBunny")
  dis_basename     : str   (e.g. "BigBuckBunny_30_384_550.yuv")
  frame_index      : int   (0-based per-pair frame number)
  codec            : str   (encoder family; ``"unknown"`` for this corpus)
  vmaf             : float (vmaf_v0.6.1 teacher score)
  <26 feature columns from FULL_FEATURES>

(ADR-0559 extended FULL_FEATURES from 22 to 26 columns by appending
speed_temporal and speed_chroma_u/v/uv.  The speed_* extractors are CPU-only;
GPU twins are tracked in ADR-0557 and ADR-0558.)

The Netflix Public corpus ships pre-encoded distorted YUVs with no
in-band codec metadata, so the ``codec`` column defaults to ``"unknown"``
— this is the documented limitation cited in
[ADR-0235](../../docs/adr/0235-codec-aware-fr-regressor.md). Override
via ``--codec`` when re-extracting against a manifest that does carry
encoder labels.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import time
from pathlib import Path

import numpy as np

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent))

from ai.data.feature_extractor import FULL_FEATURES, extract_features
from ai.data.netflix_loader import iter_pairs
from ai.data.scores import teacher_scores


def _per_clip_cache_path(root: Path, source: str, dis_stem: str) -> Path:
    return root / source / f"{dis_stem}.json"


def _load_or_compute(
    pair,
    cache_dir: Path,
    vmaf_binary: Path,
) -> dict:
    src = pair.source
    stem = pair.dis_path.stem
    cache_path = _per_clip_cache_path(cache_dir, src, stem)
    if cache_path.is_file():
        return json.loads(cache_path.read_text())

    feats = extract_features(
        pair.ref_path,
        pair.dis_path,
        pair.width,
        pair.height,
        features=FULL_FEATURES,
        vmaf_binary=vmaf_binary,
    )
    teacher = teacher_scores(
        pair.ref_path,
        pair.dis_path,
        pair.width,
        pair.height,
        vmaf_binary=vmaf_binary,
    )
    payload = {
        "feature_names": list(feats.feature_names),
        "per_frame": feats.per_frame.tolist(),
        "teacher_per_frame": teacher.per_frame.tolist(),
    }
    cache_path.parent.mkdir(parents=True, exist_ok=True)
    cache_path.write_text(json.dumps(payload))
    return payload


def main() -> int:
    ap = argparse.ArgumentParser(prog="extract_full_features.py")
    ap.add_argument(
        "--data-root",
        type=Path,
        default=Path(
            os.environ.get(
                "VMAF_NETFLIX_CORPUS_DIR",
                str(Path(__file__).resolve().parents[2] / ".corpus" / "netflix"),
            )
        ),
        help=(
            "Netflix corpus root. Override with the ``VMAF_NETFLIX_CORPUS_DIR`` "
            "env var when running inside the dev-mcp container or other layouts."
        ),
    )
    ap.add_argument(
        "--cache-dir",
        type=Path,
        default=Path.home() / ".cache" / "vmaf-tiny-ai-full",
    )
    ap.add_argument(
        "--vmaf-bin",
        type=Path,
        default=Path("build-cpu/tools/vmaf"),
    )
    ap.add_argument(
        "--out",
        type=Path,
        default=Path("runs/full_features_netflix.parquet"),
    )
    ap.add_argument("--max-pairs", type=int, default=None)
    ap.add_argument(
        "--codec",
        type=str,
        default="unknown",
        help="Codec label baked into the parquet's `codec` column. The "
        "Netflix Public corpus ships pre-encoded distortions without "
        "in-band codec metadata, so the safe default is 'unknown' "
        "(bucketed via ai/src/vmaf_train/codec.py). Override when "
        "re-extracting against a manifest that does carry labels.",
    )
    args = ap.parse_args()

    if not args.vmaf_bin.is_file():
        print(f"error: vmaf binary not found at {args.vmaf_bin}", file=sys.stderr)
        return 2

    rows: list[dict] = []
    pairs = list(iter_pairs(args.data_root, max_pairs=args.max_pairs))
    print(f"[extract] {len(pairs)} pairs; FULL_FEATURES = {len(FULL_FEATURES)} features")
    t0 = time.time()
    for i, pair in enumerate(pairs):
        wt = time.time() - t0
        print(
            f"[extract] {i + 1}/{len(pairs)} {pair.source}/{pair.dis_path.name} (elapsed {wt:.0f}s)"
        )
        payload = _load_or_compute(pair, args.cache_dir, args.vmaf_bin)
        per_frame = np.asarray(payload["per_frame"], dtype=np.float32)
        teacher_per_frame = np.asarray(payload["teacher_per_frame"], dtype=np.float32)
        n = min(per_frame.shape[0], teacher_per_frame.shape[0])
        for fi in range(n):
            row = {
                "source": pair.source,
                "dis_basename": pair.dis_path.name,
                "frame_index": fi,
                "codec": args.codec,
                "vmaf": float(teacher_per_frame[fi]),
            }
            for col, val in zip(FULL_FEATURES, per_frame[fi], strict=False):
                row[col] = float(val)
            rows.append(row)

    import pandas as pd  # local import — pandas optional for non-Phase-2 paths

    df = pd.DataFrame(rows)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    df.to_parquet(args.out)
    print(
        f"[extract] wrote {args.out}: {len(df)} rows × {len(df.columns)} cols "
        f"in {time.time() - t0:.0f}s wall"
    )
    return 0


if __name__ == "__main__":  # pragma: no cover
    raise SystemExit(main())
