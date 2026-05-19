#!/usr/bin/env python3
# Copyright 2026 Lusoris and Claude (Anthropic)
# SPDX-License-Identifier: BSD-3-Clause-Plus-Patent
"""Train the KonViD MOS head v1 — Phase 3 of ADR-0325.

Phases 1 + 2 of ADR-0325 land the KonViD-1k / KonViD-150k corpora as
JSONL drops under ``.workingdir2/konvid-{1k,150k}/`` (PRs #440 / #447).
Phase 3 — this script — trains a small MLP that maps the canonical-6
libvmaf features + saliency mean/var + 3 TransNet shot-metadata
columns + a UGC-mixed encoder one-hot to a scalar MOS prediction in
``[1.0, 5.0]`` (subjective MOS units).

Why a separate MOS head, when the fork already ships
``fr_regressor_v2_ensemble`` (VMAF prediction)? Because the
ChatGPT-vision audit (Research-0086) flagged that the fork's
predictors all target *VMAF*, not raw subjective MOS — so we cannot
honestly claim the fork "predicts human MOS" without a head trained
against subjective ratings. KonViD ships ≥5 crowdworker MOS ratings
per clip, which is exactly the subjective ground truth the head
needs.

ONNX I/O contract::

    Inputs:
      features: float32 [N, 11]   canonical-6 + saliency_mean +
                                  saliency_var + shot_count_norm +
                                  shot_mean_len_norm + shot_cut_density
      encoder_onehot: float32 [N, 1]  ENCODER_VOCAB v4 single slot
                                      (always [1.0]) — placeholder for
                                      future multi-slot expansion.
    Output:
      mos: float32 [N]            predicted MOS in [1.0, 5.0]

Reproducer (smoke — no real corpus on disk; deterministic seed)::

    python ai/scripts/train_konvid_mos_head.py --smoke

Production (real KonViD JSONL drops or CHUG/K150K feature parquet)::

    python ai/scripts/train_konvid_mos_head.py \
        --konvid-1k .workingdir2/konvid-1k/konvid_1k.jsonl \
        --konvid-150k .workingdir2/konvid-150k/konvid_150k.jsonl

    python ai/scripts/train_konvid_mos_head.py \
        --feature-parquet .workingdir2/chug/training/full_features_chug.parquet

Production-flip gate (mirrors ADR-0303 / fr_regressor_v2_ensemble):

* Mean PLCC ≥ 0.85 across 5 folds
* SROCC ≥ 0.82
* RMSE ≤ 0.45 MOS units
* Max-min spread across folds ≤ 0.005

Per the user direction (memory ``feedback_no_test_weakening``) and
ADR-0325 §Production-flip gate: a real-corpus miss does **not** lower
the threshold — the head ships ``Status: Proposed`` instead with the
gate verdict recorded in the model card.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import sys
import time
from collections.abc import Sequence
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import numpy as np

from aiutils.file_utils import sha256

REPO_ROOT = Path(__file__).resolve().parents[2]

# ---------------------------------------------------------------------
# Constants — schema-pinned, must stay in sync with the model card and
# the predictor in ``tools/vmaf-tune/src/vmaftune/predictor.py``.
# ---------------------------------------------------------------------

CANONICAL_6: tuple[str, ...] = (
    "adm2",
    "vif_scale0",
    "vif_scale1",
    "vif_scale2",
    "vif_scale3",
    "motion2",
)

# Saliency + TransNet shot-metadata columns. The 3 shot-metadata fields
# are the ones #477's TransNet ingestion bolts onto the corpus rows;
# when the corpus row does not carry them (Phase 1/2 KonViD JSONL was
# spec'd before #477), we default-fill with content-independent zeros.
EXTRA_FEATURES: tuple[str, ...] = (
    "saliency_mean",
    "saliency_var",
    "shot_count_norm",  # log10(1 + shot_count) / 3.0  -> ~[0, 1]
    "shot_mean_len_norm",  # mean shot length in seconds / 30.0 -> ~[0, 1]
    "shot_cut_density",  # cuts per frame -> ~[0, 0.1]
)

FEATURE_COLUMNS: tuple[str, ...] = CANONICAL_6 + EXTRA_FEATURES
N_FEATURES = len(FEATURE_COLUMNS)

# ENCODER_VOCAB v4 — KonViD UGC content collapses to a single
# ``ugc-mixed`` slot per ADR-0325 §Phase 2 §Decision. The MOS-head
# input carries the slot as a length-1 one-hot so the schema is
# forward-compatible with future multi-slot expansion (e.g. when the
# fork starts ingesting LSVQ + YouTube-UGC).
ENCODER_VOCAB_V4: tuple[str, ...] = ("ugc-mixed",)
ENCODER_VOCAB_V4_VERSION = 4
N_ENCODERS = len(ENCODER_VOCAB_V4)

# MOS scale — KonViD uses the standard ITU-T 5-point MOS scale.
MOS_MIN = 1.0
MOS_MAX = 5.0

# Production-flip gate per ADR-0325 Phase 3 (mirrors ADR-0303 shape).
# The values are recorded here as constants so the trainer's emitted
# JSON carries the gate it was trained against. They are *not* lowered
# on real-corpus failures (memory ``feedback_no_test_weakening``).
GATE_MEAN_PLCC: float = 0.85
GATE_SROCC: float = 0.82
GATE_RMSE_MAX: float = 0.45
GATE_SPREAD_MAX: float = 0.005

# Synthetic-corpus gate — placeholder per the task brief; the real
# threshold comes from real-corpus runs once #447 lands.
SYNTHETIC_GATE_PLCC: float = 0.75


@dataclass(frozen=True)
class CorpusArrays:
    """Projected MOS-corpus arrays plus optional split labels."""

    features: np.ndarray
    encoder: np.ndarray
    mos: np.ndarray
    splits: np.ndarray


# ---------------------------------------------------------------------
# Helpers — seeding, sha256, corpus loading.
# ---------------------------------------------------------------------


def _set_seed(seed: int) -> None:
    """Deterministic seed across numpy + torch (when available)."""
    import contextlib

    np.random.seed(seed)
    try:
        import torch  # type: ignore[import-not-found]

        torch.manual_seed(seed)
        if hasattr(torch, "use_deterministic_algorithms"):
            # Some PyTorch builds reject a hard True under CUDA; warn-only is best-effort.
            with contextlib.suppress(RuntimeError):
                torch.use_deterministic_algorithms(True, warn_only=True)
    except ImportError:
        pass


def _load_jsonl(path: Path) -> list[dict[str, Any]]:
    """Load a JSONL corpus drop into a list of dicts."""
    out: list[dict[str, Any]] = []
    with path.open("r", encoding="utf-8") as fh:
        for raw in fh:
            line = raw.strip()
            if not line:
                continue
            try:
                out.append(json.loads(line))
            except json.JSONDecodeError:
                # Skip mis-formatted lines rather than aborting the
                # whole training run; a well-tested ingester should not
                # produce them but defence in depth is cheap.
                continue
    return out


def _load_parquet(path: Path) -> list[dict[str, Any]]:
    """Load a FULL_FEATURES parquet corpus drop into a list of dicts."""
    try:
        import pandas as pd  # type: ignore[import-not-found]
    except ImportError as exc:
        raise RuntimeError("pandas is required for --feature-parquet corpus loading") from exc
    frame = pd.read_parquet(path)
    return frame.to_dict(orient="records")


def _load_corpus_rows(path: Path) -> list[dict[str, Any]]:
    if path.suffix.lower() in {".parquet", ".pq"}:
        return _load_parquet(path)
    return _load_jsonl(path)


def _normalise_split(raw: Any) -> str:
    split = str(raw or "").strip().lower()
    return split if split in {"train", "val", "test"} else ""


def _row_to_features(row: dict[str, Any]) -> tuple[np.ndarray, float] | None:
    """Project one corpus row to ``(features, mos)`` or ``None`` to skip.

    Phase 1/2 KonViD JSONL rows carry per-clip aggregates only — they
    do *not* yet carry the canonical-6 libvmaf features, the saliency
    extractor's output, or TransNet shot-metadata. The trainer
    accepts whichever subset of those columns the row carries and
    fills the rest with content-independent defaults; that lets this
    script run today against the in-flight Phase 1/2 JSONL while the
    canonical-6 / saliency / shot-metadata columns get bolted on in
    follow-up PRs.
    """
    import contextlib

    mos = row.get("mos")
    try:
        mos_f = float(mos)
    except (TypeError, ValueError):
        mos_f = math.nan
    if not math.isfinite(mos_f):
        raw_mos = row.get("mos_raw_0_100")
        try:
            mos_f = MOS_MIN + (MOS_MAX - MOS_MIN) * (float(raw_mos) / 100.0)
        except (TypeError, ValueError):
            mos_f = math.nan
    if not (math.isfinite(mos_f) and MOS_MIN <= mos_f <= MOS_MAX):
        # KonViD's published MOS values live in [1, 5]; out-of-range
        # rows indicate a schema mismatch and are dropped rather than
        # silently clamped.
        return None
    feats = np.zeros(N_FEATURES, dtype=np.float32)
    for idx, name in enumerate(FEATURE_COLUMNS):
        value = row.get(name)
        # Parquet corpora produced by the CHUG materialiser store per-clip
        # temporal averages under the ``<feature>_mean`` column name (e.g.
        # ``adm2_mean``).  When a parquet file has mixed columns, pandas
        # fills absent slots with NaN rather than omitting the key, so we
        # must treat NaN as "missing" and also fall back.  Accept either
        # form so that full-features parquet and bare-feature JSONL rows
        # can coexist in the same corpus load.  (Regression introduced by
        # PR #908 which dropped this fallback during the aiutils refactor.)
        try:
            primary_f = float(value)
        except (TypeError, ValueError):
            primary_f = math.nan
        if not math.isfinite(primary_f):
            value = row.get(f"{name}_mean")
        if value is not None:
            with contextlib.suppress(TypeError, ValueError):
                feats[idx] = float(value)
    return feats, mos_f


def _load_corpus_arrays(
    paths: Sequence[Path],
    parquet_paths: Sequence[Path] = (),
) -> CorpusArrays:
    """Load + project one or more JSONL or parquet corpus paths.

    Returns features, encoder one-hot, MOS, and optional split labels.
    The encoder one-hot is always ``[1.0]`` because ENCODER_VOCAB v4 has
    a single slot.
    """
    rows: list[dict[str, Any]] = []
    for p in (*paths, *parquet_paths):
        if p is not None and p.is_file():
            rows.extend(_load_corpus_rows(p))
    pairs: list[tuple[np.ndarray, float]] = []
    splits: list[str] = []
    for row in rows:
        proj = _row_to_features(row)
        if proj is not None:
            pairs.append(proj)
            splits.append(_normalise_split(row.get("split")))
    if not pairs:
        return CorpusArrays(
            features=np.empty((0, N_FEATURES), dtype=np.float32),
            encoder=np.empty((0, N_ENCODERS), dtype=np.float32),
            mos=np.empty((0,), dtype=np.float32),
            splits=np.empty((0,), dtype="<U5"),
        )
    feats = np.stack([p[0] for p in pairs]).astype(np.float32)
    mos = np.asarray([p[1] for p in pairs], dtype=np.float32)
    encoder = np.ones((feats.shape[0], N_ENCODERS), dtype=np.float32)
    return CorpusArrays(
        features=feats,
        encoder=encoder,
        mos=mos,
        splits=np.asarray(splits, dtype="<U5"),
    )


def _load_corpus(paths: Sequence[Path]) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Load + project one or more JSONL or parquet corpus paths."""
    arrays = _load_corpus_arrays(paths)
    return arrays.features, arrays.encoder, arrays.mos


def _heldout_split_indices(splits: np.ndarray) -> tuple[np.ndarray, np.ndarray] | None:
    """Return train/eval indices when corpus rows carry explicit splits."""
    if splits.size == 0:
        return None
    train = np.flatnonzero(splits == "train")
    val = np.flatnonzero(splits == "val")
    test = np.flatnonzero(splits == "test")
    eval_idx = val if len(val) > 0 else test
    if len(train) == 0 or len(eval_idx) == 0:
        return None
    return train, eval_idx


# ---------------------------------------------------------------------
# Synthetic-corpus generator — used when no real corpus is on disk.
# ---------------------------------------------------------------------


def _synthesize_corpus(
    n_rows: int = 600,
    seed: int = 0,
) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Synthesise a deterministic-seeded synthetic MOS corpus.

    The synthetic generator picks plausible canonical-6 ranges that
    mirror the fr_regressor_v2 trainer's generator, then derives a
    learnable MOS target as a smooth nonlinear function of
    ``(adm2, motion2, saliency_mean)`` plus 1-rater Gaussian noise.
    The generator is purely deterministic at the per-seed level so a
    fresh checkout reproduces the gate verdict bit-for-bit.
    """
    rng = np.random.default_rng(seed)
    feats = np.column_stack(
        [
            rng.uniform(0.4, 1.0, size=n_rows),  # adm2
            rng.uniform(0.2, 0.95, size=n_rows),  # vif_scale0
            rng.uniform(0.3, 0.95, size=n_rows),  # vif_scale1
            rng.uniform(0.4, 0.97, size=n_rows),  # vif_scale2
            rng.uniform(0.5, 0.98, size=n_rows),  # vif_scale3
            rng.uniform(0.0, 30.0, size=n_rows),  # motion2
            rng.uniform(0.0, 0.6, size=n_rows),  # saliency_mean
            rng.uniform(0.0, 0.05, size=n_rows),  # saliency_var
            rng.uniform(0.0, 1.0, size=n_rows),  # shot_count_norm
            rng.uniform(0.0, 1.0, size=n_rows),  # shot_mean_len_norm
            rng.uniform(0.0, 0.1, size=n_rows),  # shot_cut_density
        ]
    ).astype(np.float32)
    # Smooth nonlinear MOS target — anchored so well-encoded
    # high-saliency content lives near 4.5 and grainy fast-motion
    # content near 1.5. The form is a sum of three monotone terms
    # plus a small noise floor, which keeps a small MLP capable of
    # recovering ≥0.75 PLCC on the synthetic split.
    base = (
        2.5
        + 1.6 * feats[:, 0]  # adm2 lift
        - 0.04 * feats[:, 5]  # motion2 penalty
        + 0.6 * feats[:, 6]  # saliency lift
        + 0.3 * feats[:, 4]  # vif_scale3 lift
    )
    noise = rng.normal(0.0, 0.10, size=n_rows)  # ~rater noise
    target = np.clip(base + noise, MOS_MIN, MOS_MAX).astype(np.float32)
    encoder = np.ones((n_rows, N_ENCODERS), dtype=np.float32)
    return feats, encoder, target


# ---------------------------------------------------------------------
# Model — small MLP. ~30K-100K params, ONNX-allowlist conformant
# (Gemm + ReLU + dropout-as-Identity at eval time + LayerNorm).
# ---------------------------------------------------------------------


def _build_model(
    in_features: int = N_FEATURES,
    n_encoders: int = N_ENCODERS,
    hidden: int = 64,
    depth: int = 2,
    dropout: float = 0.1,
):  # type: ignore[no-untyped-def]
    """Build a small ``nn.Module`` MLP. Imported torch lazily."""
    import torch
    from torch import nn

    class MOSHead(nn.Module):
        def __init__(self) -> None:
            super().__init__()
            in_dim = in_features + n_encoders
            layers: list[nn.Module] = [nn.LayerNorm(in_dim)]
            prev = in_dim
            for _ in range(depth):
                layers.append(nn.Linear(prev, hidden))
                layers.append(nn.ReLU())
                layers.append(nn.Dropout(dropout))
                prev = hidden
            layers.append(nn.Linear(prev, 1))
            self.body = nn.Sequential(*layers)

        def forward(
            self,
            features: "torch.Tensor",
            encoder_onehot: "torch.Tensor",
        ) -> "torch.Tensor":
            x = torch.cat([features, encoder_onehot], dim=-1)
            raw = self.body(x).squeeze(-1)
            # Sigmoid + affine clamp into [MOS_MIN, MOS_MAX] — keeps
            # the head's output in the documented MOS range without
            # relying on a runtime Clip op.
            return MOS_MIN + (MOS_MAX - MOS_MIN) * torch.sigmoid(raw)

    return MOSHead()


def _count_parameters(model) -> int:  # type: ignore[no-untyped-def]
    return int(sum(p.numel() for p in model.parameters()))


# ---------------------------------------------------------------------
# Cross-validation training loop.
# ---------------------------------------------------------------------


def _kfold_indices(n: int, k: int, seed: int) -> list[tuple[np.ndarray, np.ndarray]]:
    """Split ``n`` row indices into ``k`` (train, val) folds, deterministic."""
    rng = np.random.default_rng(seed)
    perm = rng.permutation(n)
    folds = np.array_split(perm, k)
    out: list[tuple[np.ndarray, np.ndarray]] = []
    for i in range(k):
        val = folds[i]
        train = np.concatenate([folds[j] for j in range(k) if j != i])
        out.append((train, val))
    return out


def _plcc(a: np.ndarray, b: np.ndarray) -> float:
    if len(a) < 2 or len(b) < 2:
        return float("nan")
    if a.std() == 0.0 or b.std() == 0.0:
        return float("nan")
    return float(np.corrcoef(a, b)[0, 1])


def _srocc(a: np.ndarray, b: np.ndarray) -> float:
    if len(a) < 2 or len(b) < 2:
        return float("nan")
    ra = np.argsort(np.argsort(a))
    rb = np.argsort(np.argsort(b))
    return _plcc(ra.astype(np.float64), rb.astype(np.float64))


def _rmse(a: np.ndarray, b: np.ndarray) -> float:
    if len(a) == 0:
        return float("nan")
    return float(np.sqrt(np.mean((a - b) ** 2)))


def _train_one_fold(
    *,
    features_train: np.ndarray,
    encoder_train: np.ndarray,
    mos_train: np.ndarray,
    features_val: np.ndarray,
    encoder_val: np.ndarray,
    mos_val: np.ndarray,
    epochs: int,
    batch_size: int,
    lr: float,
    weight_decay: float,
    seed: int,
) -> tuple[np.ndarray, dict[str, float]]:
    """Train one fold; return ``(val_pred, fold_metrics)``."""
    import torch
    from torch.utils.data import DataLoader, TensorDataset

    _set_seed(seed)
    model = _build_model()
    ds = TensorDataset(
        torch.from_numpy(features_train.astype(np.float32)),
        torch.from_numpy(encoder_train.astype(np.float32)),
        torch.from_numpy(mos_train.astype(np.float32)),
    )
    loader = DataLoader(ds, batch_size=batch_size, shuffle=True, drop_last=False)
    opt = torch.optim.AdamW(model.parameters(), lr=lr, weight_decay=weight_decay)
    loss_fn = torch.nn.MSELoss()
    model.train()
    for _ep in range(epochs):
        for fb, eb, yb in loader:
            opt.zero_grad()
            pred = model(fb, eb)
            loss = loss_fn(pred, yb)
            loss.backward()
            opt.step()
    model.eval()
    with torch.no_grad():
        val_pred = (
            model(
                torch.from_numpy(features_val.astype(np.float32)),
                torch.from_numpy(encoder_val.astype(np.float32)),
            )
            .cpu()
            .numpy()
        )
    val_pred = np.asarray(val_pred, dtype=np.float32)
    metrics = {
        "n_train": len(mos_train),
        "n_val": len(mos_val),
        "plcc": _plcc(val_pred, mos_val),
        "srocc": _srocc(val_pred, mos_val),
        "rmse": _rmse(val_pred, mos_val),
    }
    return val_pred, metrics


def _train_full(
    *,
    features: np.ndarray,
    encoder: np.ndarray,
    mos: np.ndarray,
    epochs: int,
    batch_size: int,
    lr: float,
    weight_decay: float,
    seed: int,
):  # type: ignore[no-untyped-def]
    """Train one model on the full corpus — the ship checkpoint."""
    import torch
    from torch.utils.data import DataLoader, TensorDataset

    _set_seed(seed)
    model = _build_model()
    ds = TensorDataset(
        torch.from_numpy(features.astype(np.float32)),
        torch.from_numpy(encoder.astype(np.float32)),
        torch.from_numpy(mos.astype(np.float32)),
    )
    loader = DataLoader(ds, batch_size=batch_size, shuffle=True, drop_last=False)
    opt = torch.optim.AdamW(model.parameters(), lr=lr, weight_decay=weight_decay)
    loss_fn = torch.nn.MSELoss()
    model.train()
    for _ep in range(epochs):
        for fb, eb, yb in loader:
            opt.zero_grad()
            pred = model(fb, eb)
            loss = loss_fn(pred, yb)
            loss.backward()
            opt.step()
    model.eval()
    return model


def _export_onnx(model, onnx_path: Path) -> str:  # type: ignore[no-untyped-def]
    """Export the trained model as opset-17 ONNX. Returns sha256."""
    import torch

    onnx_path.parent.mkdir(parents=True, exist_ok=True)
    dummy_features = torch.zeros(1, N_FEATURES, dtype=torch.float32)
    dummy_encoder = torch.ones(1, N_ENCODERS, dtype=torch.float32)
    torch.onnx.export(
        model,
        (dummy_features, dummy_encoder),
        str(onnx_path),
        input_names=["features", "encoder_onehot"],
        output_names=["mos"],
        dynamic_axes={
            "features": {0: "batch"},
            "encoder_onehot": {0: "batch"},
            "mos": {0: "batch"},
        },
        opset_version=17,
    )
    return sha256(onnx_path)


# ---------------------------------------------------------------------
# Gate evaluation — mirrors ADR-0303 shape.
# ---------------------------------------------------------------------


def _evaluate_gate(folds: list[dict[str, Any]], *, synthetic: bool) -> dict[str, Any]:
    """Apply the production-flip gate to per-fold metrics. No threshold lowering."""
    plcc_vals = [f["plcc"] for f in folds if not math.isnan(f["plcc"])]
    srocc_vals = [f["srocc"] for f in folds if not math.isnan(f["srocc"])]
    rmse_vals = [f["rmse"] for f in folds if not math.isnan(f["rmse"])]
    mean_plcc = float(np.mean(plcc_vals)) if plcc_vals else float("nan")
    mean_srocc = float(np.mean(srocc_vals)) if srocc_vals else float("nan")
    mean_rmse = float(np.mean(rmse_vals)) if rmse_vals else float("nan")
    spread = float(max(plcc_vals) - min(plcc_vals)) if plcc_vals else float("nan")
    if synthetic:
        # Synthetic gate is a single PLCC threshold per the task brief
        # — the real PLCC/SROCC/RMSE/spread gate comes online when a
        # real corpus is on disk.
        passed = (not math.isnan(mean_plcc)) and mean_plcc >= SYNTHETIC_GATE_PLCC
        gate_used: dict[str, Any] = {
            "kind": "synthetic",
            "plcc_min": SYNTHETIC_GATE_PLCC,
        }
    else:
        passed = (
            (not math.isnan(mean_plcc))
            and mean_plcc >= GATE_MEAN_PLCC
            and (not math.isnan(mean_srocc))
            and mean_srocc >= GATE_SROCC
            and (not math.isnan(mean_rmse))
            and mean_rmse <= GATE_RMSE_MAX
            and (not math.isnan(spread))
            and spread <= GATE_SPREAD_MAX
        )
        gate_used = {
            "kind": "real",
            "plcc_min": GATE_MEAN_PLCC,
            "srocc_min": GATE_SROCC,
            "rmse_max": GATE_RMSE_MAX,
            "spread_max": GATE_SPREAD_MAX,
        }
    return {
        "passed": bool(passed),
        "mean_plcc": mean_plcc,
        "mean_srocc": mean_srocc,
        "mean_rmse": mean_rmse,
        "plcc_spread": spread,
        "gate": gate_used,
    }


# ---------------------------------------------------------------------
# Manifest writer.
# ---------------------------------------------------------------------


def _build_manifest(
    *,
    onnx_path: Path,
    sha256: str,
    folds: list[dict[str, Any]],
    gate: dict[str, Any],
    feature_mean: list[float],
    feature_std: list[float],
    n_params: int,
    smoke: bool,
    n_rows: int,
    epochs: int,
    seed: int,
) -> dict[str, Any]:
    return {
        "id": "konvid_mos_head_v1",
        "kind": "mos",
        "onnx": onnx_path.name,
        "sha256": sha256,
        "opset": 17,
        "feature_order": list(FEATURE_COLUMNS),
        "feature_mean": feature_mean,
        "feature_std": feature_std,
        "encoder_vocab": list(ENCODER_VOCAB_V4),
        "encoder_vocab_version": ENCODER_VOCAB_V4_VERSION,
        "mos_range": [MOS_MIN, MOS_MAX],
        "n_parameters": n_params,
        "training_recipe": {
            "epochs": epochs,
            "seed": seed,
            "n_rows": n_rows,
            "smoke": smoke,
            "k_folds": len(folds),
        },
        "folds": folds,
        "gate": gate,
        "adr": "0336",
        "phase": "ADR-0325 Phase 3",
    }


# ---------------------------------------------------------------------
# Entry point.
# ---------------------------------------------------------------------


def main(argv: Sequence[str] | None = None) -> int:
    ap = argparse.ArgumentParser(prog="train_konvid_mos_head.py")
    _konvid_1k_dir = Path(
        os.environ.get(
            "VMAF_KONVID_1K_DIR",
            str(Path(__file__).resolve().parents[2] / ".corpus" / "konvid-1k"),
        )
    )
    _konvid_150k_dir = Path(
        os.environ.get(
            "VMAF_KONVID_150K_DIR",
            str(Path(__file__).resolve().parents[2] / ".corpus" / "konvid-150k"),
        )
    )
    ap.add_argument(
        "--konvid-1k",
        type=Path,
        default=_konvid_1k_dir / "konvid_1k.jsonl",
        help=(
            "Path to the KonViD-1k JSONL corpus drop (Phase 1, PR #440). "
            "Parent dir overridden via ``VMAF_KONVID_1K_DIR``."
        ),
    )
    ap.add_argument(
        "--konvid-150k",
        type=Path,
        default=_konvid_150k_dir / "konvid_150k.jsonl",
        help=(
            "Path to the KonViD-150k JSONL corpus drop (Phase 2, PR #447). "
            "Parent dir overridden via ``VMAF_KONVID_150K_DIR``."
        ),
    )
    ap.add_argument(
        "--feature-parquet",
        type=Path,
        action="append",
        default=[],
        help=(
            "FULL_FEATURES parquet corpus table from extract_k150k_features.py; "
            "may be repeated for CHUG/K150K shards."
        ),
    )
    ap.add_argument(
        "--smoke",
        action="store_true",
        help="Synthesize a deterministic-seeded corpus instead of loading "
        "from disk; used by CI smoke + the test harness.",
    )
    ap.add_argument("--epochs", type=int, default=30)
    ap.add_argument("--smoke-epochs", type=int, default=30)
    ap.add_argument("--batch-size", type=int, default=64)
    ap.add_argument("--lr", type=float, default=1e-3)
    ap.add_argument("--weight-decay", type=float, default=1e-5)
    ap.add_argument("--k-folds", type=int, default=5)
    ap.add_argument("--seed", type=int, default=20260508)
    ap.add_argument(
        "--out-onnx",
        type=Path,
        default=REPO_ROOT / "model" / "konvid_mos_head_v1.onnx",
    )
    ap.add_argument(
        "--out-card",
        type=Path,
        default=REPO_ROOT / "model" / "konvid_mos_head_v1_card.md",
        help="Model-card path (this script does not rewrite a hand-authored card; "
        "set this to a tmp path to force the auto-generated stub).",
    )
    ap.add_argument(
        "--out-manifest",
        type=Path,
        default=REPO_ROOT / "model" / "konvid_mos_head_v1.json",
    )
    ap.add_argument(
        "--no-export",
        action="store_true",
        help="Skip ONNX export + manifest write (dev mode).",
    )
    args = ap.parse_args(argv)

    if args.smoke:
        n_rows = 600
        features, encoder, mos = _synthesize_corpus(n_rows=n_rows, seed=args.seed)
        splits = np.empty((features.shape[0],), dtype="<U5")
        epochs = args.smoke_epochs
        synthetic = True
        print(
            f"[konvid-mos] smoke mode: {n_rows} synthetic rows, "
            f"epochs={epochs}, seed={args.seed}",
            flush=True,
        )
    else:
        paths = [p for p in (args.konvid_1k, args.konvid_150k) if p is not None]
        feature_parquets = [p for p in args.feature_parquet if p is not None]
        arrays = _load_corpus_arrays(paths, parquet_paths=feature_parquets)
        features, encoder, mos, splits = (
            arrays.features,
            arrays.encoder,
            arrays.mos,
            arrays.splits,
        )
        if features.shape[0] == 0:
            print(
                "[konvid-mos] no real corpus rows found at "
                f"{[str(p) for p in (*paths, *feature_parquets)]}; "
                "falling back to synthetic. "
                "Pass --smoke to silence this message.",
                file=sys.stderr,
            )
            features, encoder, mos = _synthesize_corpus(n_rows=600, seed=args.seed)
            splits = np.empty((features.shape[0],), dtype="<U5")
            synthetic = True
        else:
            synthetic = False
        epochs = args.epochs
        print(
            f"[konvid-mos] {'real' if not synthetic else 'synthetic-fallback'} "
            f"mode: {features.shape[0]} rows, epochs={epochs}",
            flush=True,
        )

    if features.shape[0] < args.k_folds * 2:
        print(
            f"[konvid-mos] error: corpus has only {features.shape[0]} rows; "
            f"need at least {args.k_folds * 2} for {args.k_folds}-fold CV.",
            file=sys.stderr,
        )
        return 2

    # Per-corpus standardisation. The MLP carries a LayerNorm at its
    # input so this is informational only (recorded in the sidecar so
    # downstream consumers can replicate the recipe), but we still
    # compute it for parity with the fr_regressor_v2_ensemble manifest.
    feature_mean = features.mean(axis=0).astype(np.float32).tolist()
    feature_std = features.std(axis=0).astype(np.float32).tolist()

    t0 = time.time()
    folds_report: list[dict[str, Any]] = []
    split_indices = None if synthetic else _heldout_split_indices(splits)
    if split_indices is None:
        fold_indices = _kfold_indices(features.shape[0], args.k_folds, args.seed)
        validation_policy = f"{args.k_folds}-fold-random"
    else:
        train_idx, val_idx = split_indices
        fold_indices = [(train_idx, val_idx)]
        validation_policy = "explicit-corpus-split"
        print(
            f"[konvid-mos] explicit split validation: "
            f"n_train={len(train_idx)} n_val={len(val_idx)}",
            flush=True,
        )
    for fold_idx, (train_idx, val_idx) in enumerate(fold_indices):
        _val_pred, metrics = _train_one_fold(
            features_train=features[train_idx],
            encoder_train=encoder[train_idx],
            mos_train=mos[train_idx],
            features_val=features[val_idx],
            encoder_val=encoder[val_idx],
            mos_val=mos[val_idx],
            epochs=epochs,
            batch_size=args.batch_size,
            lr=args.lr,
            weight_decay=args.weight_decay,
            seed=args.seed + fold_idx,
        )
        metrics["fold"] = fold_idx
        metrics["validation_policy"] = validation_policy
        folds_report.append(metrics)
        print(
            f"[konvid-mos] fold {fold_idx}: "
            f"plcc={metrics['plcc']:.4f} srocc={metrics['srocc']:.4f} "
            f"rmse={metrics['rmse']:.4f} (n_val={metrics['n_val']})",
            flush=True,
        )
    gate = _evaluate_gate(folds_report, synthetic=synthetic)
    print(
        f"[konvid-mos] gate={'PASS' if gate['passed'] else 'FAIL'} "
        f"mean_plcc={gate['mean_plcc']:.4f} "
        f"mean_srocc={gate['mean_srocc']:.4f} "
        f"mean_rmse={gate['mean_rmse']:.4f} "
        f"spread={gate['plcc_spread']:.4f}",
        flush=True,
    )

    # Train the ship checkpoint on the full corpus once the LOSO
    # report is in. Per ADR-0303 / fr_regressor_v3 §Training recipe
    # the LOSO fold *is* the gate — the ship checkpoint goes on the
    # entire corpus.
    if args.no_export:
        print("[konvid-mos] --no-export: skipping ONNX + manifest write", flush=True)
        return 0
    if split_indices is None:
        ship_features = features
        ship_encoder = encoder
        ship_mos = mos
    else:
        ship_idx = split_indices[0]
        ship_features = features[ship_idx]
        ship_encoder = encoder[ship_idx]
        ship_mos = mos[ship_idx]
    ship_model = _train_full(
        features=ship_features,
        encoder=ship_encoder,
        mos=ship_mos,
        epochs=epochs,
        batch_size=args.batch_size,
        lr=args.lr,
        weight_decay=args.weight_decay,
        seed=args.seed,
    )
    n_params = _count_parameters(ship_model)
    sha256 = _export_onnx(ship_model, args.out_onnx)
    manifest = _build_manifest(
        onnx_path=args.out_onnx,
        sha256=sha256,
        folds=folds_report,
        gate=gate,
        feature_mean=feature_mean,
        feature_std=feature_std,
        n_params=n_params,
        smoke=synthetic,
        n_rows=int(ship_features.shape[0]),
        epochs=epochs,
        seed=args.seed,
    )
    args.out_manifest.parent.mkdir(parents=True, exist_ok=True)
    args.out_manifest.write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    wall_s = time.time() - t0
    print(
        f"[konvid-mos] wrote {args.out_onnx} ({n_params} params, "
        f"sha256={sha256[:16]}…); manifest={args.out_manifest.name}; "
        f"wall={wall_s:.1f}s",
        flush=True,
    )
    return 0


__all__ = [
    "CANONICAL_6",
    "ENCODER_VOCAB_V4",
    "ENCODER_VOCAB_V4_VERSION",
    "EXTRA_FEATURES",
    "FEATURE_COLUMNS",
    "GATE_MEAN_PLCC",
    "GATE_RMSE_MAX",
    "GATE_SPREAD_MAX",
    "GATE_SROCC",
    "MOS_MAX",
    "MOS_MIN",
    "N_ENCODERS",
    "N_FEATURES",
    "SYNTHETIC_GATE_PLCC",
    "CorpusArrays",
    "_evaluate_gate",
    "_heldout_split_indices",
    "_kfold_indices",
    "_load_corpus",
    "_load_corpus_arrays",
    "_row_to_features",
    "_synthesize_corpus",
    "main",
]


if __name__ == "__main__":
    sys.exit(main())
