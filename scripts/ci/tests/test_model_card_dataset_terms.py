#!/usr/bin/env python3
# SPDX-License-Identifier: EUPL-1.2
# Copyright 2026 Lusoris
"""Model cards quote the terms of the data their model was trained on (ADR-1570).

`docs/ai/training-data.md#dataset-terms` holds each dataset's terms verbatim.
Every card of a model trained on that dataset carries a "Training data terms"
section with the same quotes, the fork's reading and the RC9 retrain row, so a
card cannot drift from the canonical text or lose the terms.
"""

from __future__ import annotations

import re
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
TERMS_PAGE = ROOT / "docs/ai/training-data.md"
RETRAIN_ROW = "T-TINY-AI-RETRAIN-CLEARED-DATA-2026-10-04"
NETFLIX = "Netflix Public Dataset"
KONVID = "KoNViD-1k"
BVI = "BVI-DVC"
DUTS = "DUTS-TR (images from ImageNet)"
IMAGENET = "ImageNet"
TORCHVISION = "ImageNet through torchvision's pretrained weights"
CARDS = {
    "docs/ai/models/fr_regressor_v1.md": (NETFLIX,),
    "docs/ai/models/fr_regressor_v2.md": (NETFLIX,),
    "docs/ai/models/fr_regressor_v3.md": (NETFLIX,),
    "docs/ai/models/vmaf_tiny_v1.md": (NETFLIX,),
    "docs/ai/models/vmaf_tiny_v1_medium.md": (NETFLIX,),
    "docs/ai/models/vmaf_tiny_v2.md": (NETFLIX, KONVID, BVI),
    "docs/ai/models/vmaf_tiny_v3.md": (NETFLIX, KONVID, BVI),
    "docs/ai/models/vmaf_tiny_v4.md": (NETFLIX, KONVID, BVI),
    "docs/ai/models/nr_metric_v1.md": (KONVID,),
    "docs/ai/models/learned_filter_v1.md": (KONVID,),
    "docs/ai/models/saliency_student_v1.md": (DUTS, IMAGENET),
    "docs/ai/models/saliency_student_v2.md": (DUTS, IMAGENET),
    "model/tiny/saliency_student_v2_card.md": (DUTS, IMAGENET),
    "docs/ai/models/lpips_sq_v1.md": (TORCHVISION, IMAGENET),
}


def section(text: str, heading: str, level: str) -> str:
    """The body of the `level` heading named `heading`, up to the next heading of
    that level or higher."""
    match = re.search(rf"^{level} {re.escape(heading)}\n(.*?)(?=^#{{1,{len(level)}}} |\Z)",
                      text, re.MULTILINE | re.DOTALL)  # fmt: skip
    if match is None:
        raise AssertionError(f"no {level} {heading!r} section")
    return match.group(1)


def quotes(text: str) -> list[str]:
    """Blockquote paragraphs of `text`, each joined into one line."""
    paragraphs: list[str] = []
    current: list[str] = []
    for line in [*text.splitlines(), ""]:
        if line.startswith(">") and line.strip(">").strip():
            current.append(line.lstrip(">").strip())
            continue
        if current:
            paragraphs.append(" ".join(current))
            current = []
    return paragraphs


def canonical() -> dict[str, list[str]]:
    page = section(TERMS_PAGE.read_text(encoding="utf-8"), "Dataset terms", "##")
    names = {name for names in CARDS.values() for name in names}
    return {name: quotes(section(page, name, "###")) for name in names}


def card_problems(card: Path, datasets: tuple[str, ...], terms: dict[str, list[str]]) -> list[str]:
    try:
        body = section(card.read_text(encoding="utf-8"), "Training data terms", "##")
    except AssertionError as error:
        return [f"{card.name}: {error}"]
    found = quotes(body)
    problems = [f"{card.name}: does not quote {name!r}: {quote[:60]}..."
                for name in datasets for quote in terms[name] if quote not in found]  # fmt: skip
    for needle in (RETRAIN_ROW, "1490-rc3-rc9-candidate-map-cpu-capability.md", "**Reading.**"):
        if needle not in body:
            problems.append(f"{card.name}: the section lacks {needle}")
    return problems


class ModelCardDatasetTerms(unittest.TestCase):
    def test_every_dataset_has_canonical_quotes(self) -> None:
        for name, found in canonical().items():
            with self.subTest(dataset=name):
                self.assertTrue(found, f"{TERMS_PAGE.name} quotes nothing under {name!r}")

    def test_every_affected_card_quotes_its_datasets_terms(self) -> None:
        terms = canonical()
        problems = [p for card, names in CARDS.items()
                    for p in card_problems(ROOT / card, names, terms)]  # fmt: skip
        self.assertEqual(problems, [])

    def test_a_card_with_a_changed_quote_fails(self) -> None:
        """Planted defect: one word of the KoNViD-1k quote changed in a copy of a card."""
        terms = canonical()
        text = (ROOT / "docs/ai/models/nr_metric_v1.md").read_text(encoding="utf-8")
        planted = text.replace("> KoNViD-1k is freely available to the research community.",
                               "> KoNViD-1k is freely available to everyone.", 1)  # fmt: skip
        self.assertNotEqual(planted, text)
        with tempfile.TemporaryDirectory() as tmp:
            card = Path(tmp) / "nr_metric_v1.md"
            card.write_text(planted, encoding="utf-8")
            problems = card_problems(card, (KONVID,), terms)
        self.assertEqual(len(problems), 1, problems)
        self.assertIn("does not quote 'KoNViD-1k'", problems[0])

    def test_a_card_without_the_section_fails(self) -> None:
        terms = canonical()
        text = (ROOT / "docs/ai/models/vmaf_tiny_v1.md").read_text(encoding="utf-8")
        planted = text.replace("## Training data terms", "## Training data", 1)
        with tempfile.TemporaryDirectory() as tmp:
            card = Path(tmp) / "vmaf_tiny_v1.md"
            card.write_text(planted, encoding="utf-8")
            problems = card_problems(card, (NETFLIX,), terms)
        self.assertEqual(problems, ["vmaf_tiny_v1.md: no ## 'Training data terms' section"])


if __name__ == "__main__":
    unittest.main()
