# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Hardware-device discovery helpers for vmaf-tune.

The helpers stay small and filesystem-driven so unit tests can inject a
temporary ``/dev/dri`` + ``/sys/class/drm`` view without requiring real GPUs.
"""

from __future__ import annotations

import contextlib
import os
from collections.abc import Iterator
from pathlib import Path

AUTO_VAAPI_DEVICE: str = "auto"
FALLBACK_VAAPI_DEVICE: str = "/dev/dri/renderD128"
INTEL_PCI_VENDOR_ID: str = "0x8086"
# Environment override of the VA-API render node, read wherever a QSV
# encode or probe resolves ``auto`` (ADR-0601 / ADR-0641).
VAAPI_DEVICE_ENV: str = "VMAFTUNE_VAAPI_DEVICE"

# The render node a CLI flag (``compare --vaapi-device``) chose for the
# running command; set only inside :func:`session_vaapi_device`.
_SESSION_DEVICE: list[str] = [""]


def _candidate_render_nodes(dri_dir: Path) -> tuple[Path, ...]:
    """Return render nodes, preferring stable udev by-path entries."""
    candidates: list[Path] = []
    seen: set[Path] = set()
    by_path = dri_dir / "by-path"
    for entry in sorted(by_path.glob("*-render")):
        target = entry.resolve()
        if target.name.startswith("renderD") and target not in seen:
            candidates.append(target)
            seen.add(target)
    for entry in sorted(dri_dir.glob("renderD*")):
        target = entry.resolve()
        if target not in seen:
            candidates.append(target)
            seen.add(target)
    return tuple(candidates)


def _vendor_id_for_render_node(render_node: Path, sys_class_drm: Path) -> str:
    vendor_path = sys_class_drm / render_node.name / "device" / "vendor"
    try:
        return vendor_path.read_text(encoding="utf-8").strip().lower()
    except OSError:
        return ""


def discover_intel_vaapi_device(
    *,
    dri_dir: Path = Path("/dev/dri"),
    sys_class_drm: Path = Path("/sys/class/drm"),
) -> str | None:
    """Return the first Intel VA-API render node, or ``None`` if absent."""
    for node in _candidate_render_nodes(dri_dir):
        if _vendor_id_for_render_node(node, sys_class_drm) == INTEL_PCI_VENDOR_ID:
            return str(node)
    return None


def resolve_vaapi_device(
    requested: str | None = None,
    *,
    dri_dir: Path = Path("/dev/dri"),
    sys_class_drm: Path = Path("/sys/class/drm"),
    fallback: str = FALLBACK_VAAPI_DEVICE,
) -> str:
    """Resolve the VA-API render node a QSV encode or probe initialises.

    An explicit path wins. ``auto`` (or empty) resolves, in order, to the
    node a CLI flag set for this command (:func:`session_vaapi_device`),
    to ``$VMAFTUNE_VAAPI_DEVICE``, to the first Intel render node under
    ``/sys/class/drm``, and to ``fallback``.
    """
    for candidate in (requested, _SESSION_DEVICE[0], os.environ.get(VAAPI_DEVICE_ENV)):
        value = (candidate or "").strip()
        if value and value != AUTO_VAAPI_DEVICE:
            return value
    return discover_intel_vaapi_device(dri_dir=dri_dir, sys_class_drm=sys_class_drm) or fallback


@contextlib.contextmanager
def session_vaapi_device(device: str | None) -> Iterator[None]:
    """Make ``device`` what every ``auto`` resolution in this block returns.

    The CLI wraps a command in it when the user passed ``--vaapi-device``,
    so the encodes deep inside the bisect resolve the same node as the
    availability probe. ``None`` or ``auto`` leaves resolution unchanged.
    """
    previous = _SESSION_DEVICE[0]
    value = (device or "").strip()
    _SESSION_DEVICE[0] = "" if value == AUTO_VAAPI_DEVICE else value
    try:
        yield
    finally:
        _SESSION_DEVICE[0] = previous


__all__ = [
    "AUTO_VAAPI_DEVICE",
    "FALLBACK_VAAPI_DEVICE",
    "INTEL_PCI_VENDOR_ID",
    "VAAPI_DEVICE_ENV",
    "discover_intel_vaapi_device",
    "resolve_vaapi_device",
    "session_vaapi_device",
]
