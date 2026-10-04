# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""The one parser of ``ffmpeg -encoders`` output.

The QSV and AMF availability helpers and the ``compare`` probe all ask
whether an FFmpeg build advertises an encoder; they share this parser
(HISS-19) instead of each matching the listing their own way.
"""

from __future__ import annotations


def encoder_listed(listing: str, encoder: str) -> bool:
    """True iff ``ffmpeg -encoders`` output advertises ``encoder``.

    Each encoder line has the form ``" V..... NAME    description"``; the
    name is the second whitespace-separated token. A token match keeps
    ``libx264`` from matching ``libx264rgb`` and ``h264_nvenc`` from
    matching ``h264_v4l2m2m``, which a substring test would. Header and
    separator lines are skipped.
    """
    for raw in listing.splitlines():
        if "------" in raw or not raw.strip():
            continue
        tokens = raw.split()
        if len(tokens) >= 2 and tokens[1] == encoder:
            return True
    return False


__all__ = ["encoder_listed"]
