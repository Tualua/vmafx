# Copyright 2026 Lusoris and Claude (Anthropic)
# SPDX-License-Identifier: BSD-3-Clause-Plus-Patent
"""Tiny-AI training entry points for the Netflix corpus.

* :mod:`ai.train.dataset`  — PyTorch :class:`Dataset` over per-frame
  feature vectors with ``vmaf_v0.6.1`` distillation targets.
* :mod:`ai.train.eval`     — PLCC / SROCC / KROCC / RMSE harness plus
  inference-latency timing.
* :mod:`ai.train.train`    — Lightning-driven training entry point with
  ONNX export per epoch.

ADR-0203 documents the architecture-, split-, and caching-strategy
decisions; see ``docs/ai/training.md`` for the user-facing guide.
"""

from __future__ import annotations
