# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""The deprecated ``vmafx-mcp`` console script of this wheel (ADR-1521).

``vmafx-mcp`` is the name of the Go server (``cmd/vmafx-mcp``). The wheel's script
of the same name shadowed it, so it is kept for one release only. It lives in its
own module so ``server.py`` stays untouched; ``main`` is imported on use.
"""

from __future__ import annotations

import os
import sys


def other_vmafx_mcp(own_script: str, search_path: str) -> str | None:
    """Return the first executable ``vmafx-mcp`` on ``search_path`` that is not
    ``own_script`` (the Go binary of ``cmd/vmafx-mcp``), or None."""
    own = os.path.realpath(own_script)
    for directory in search_path.split(os.pathsep):
        candidate = os.path.join(directory, "vmafx-mcp") if directory else ""
        if candidate and os.access(candidate, os.X_OK) and os.path.realpath(candidate) != own:
            return candidate
    return None


def deprecated_vmafx_mcp_alias() -> None:
    """Warn on stderr (stdout carries JSON-RPC), hand over to the Go binary when
    one is on ``PATH``, otherwise run the Python server. Use ``vmaf-mcp`` for the
    Python server."""
    print(
        "vmafx-mcp (Python wheel script) is deprecated and will be removed in the next "
        "release: `vmafx-mcp` is the Go server (cmd/vmafx-mcp); use `vmaf-mcp` for this "
        "Python server.",
        file=sys.stderr,
    )
    go_binary = other_vmafx_mcp(sys.argv[0], os.environ.get("PATH", ""))
    if go_binary is not None:
        os.execv(go_binary, [go_binary, *sys.argv[1:]])
        return
    from vmaf_mcp.server import main

    main()
