# Copyright 2026 Lusoris and Claude (Anthropic)
# SPDX-License-Identifier: BSD-3-Clause-Plus-Patent
"""Top-level ``ai`` package.

Houses the Netflix-corpus loader, feature extractor, distillation
scoring, and Lightning-driven training harness. The original
``vmaf-train`` CLI / pyproject package lives at ``ai/src/vmaf_train``;
this top-level ``ai`` package complements it with the smaller, more
focused entry points required by ADR-0242 / ADR-0203.
"""

from __future__ import annotations
