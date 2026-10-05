# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""The tool contract both MCP servers serve, derived from this server's tool list.

``tool-contract.json`` next to this package's ``pyproject.toml`` records, for
every tool ``_list_tools()`` advertises, its input properties with their JSON
types and its required arguments. The Python test suite fails when the file
differs from what this module derives, and the Go server's parity tests
(``cmd/vmafx-mcp/server_test.go``) read the same file, so the Go server is
compared with the Python server's own list rather than with a copy of it.

Regenerate after changing a tool declaration::

    PYTHONPATH=mcp-server/vmaf-mcp/src python3 -m vmaf_mcp.tool_contract --write
"""

from __future__ import annotations

import argparse
import asyncio
import json
import sys
from pathlib import Path
from typing import Any

from mcp.types import Tool

from vmaf_mcp import server

CONTRACT_PATH = Path(__file__).resolve().parents[2] / "tool-contract.json"
SCHEMA_VERSION = 1
DESCRIPTION = "Write or check the MCP tool contract both servers serve."


def tool_entry(tool: Tool) -> dict[str, Any]:
    """Properties (name -> JSON type) and sorted required names of one tool."""
    # The wire name; the attribute name differs between SDK releases.
    schema = tool.model_dump(by_alias=True)["inputSchema"]
    properties = schema.get("properties", {})
    return {
        "properties": {name: spec.get("type") for name, spec in sorted(properties.items())},
        "required": sorted(schema.get("required", [])),
    }


def contract_from_tools(tools: list[Tool]) -> dict[str, Any]:
    """The contract document for a tool list; tool order does not matter."""
    names = [tool.name for tool in tools]
    assert len(names) == len(set(names)), f"duplicate tool names in {names}"
    return {
        "schema_version": SCHEMA_VERSION,
        "source": "mcp-server/vmaf-mcp/src/vmaf_mcp/server.py::_list_tools",
        "tools": {tool.name: tool_entry(tool) for tool in sorted(tools, key=lambda t: t.name)},
    }


def derived_contract() -> dict[str, Any]:
    """The contract of the tools this server advertises now."""
    return contract_from_tools(asyncio.run(server._list_tools()))


def render(contract: dict[str, Any]) -> str:
    return json.dumps(contract, indent=2, sort_keys=True) + "\n"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=DESCRIPTION)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--write", action="store_true", help="rewrite tool-contract.json")
    mode.add_argument("--check", action="store_true", help="fail when the file is stale")
    args = parser.parse_args(argv)
    text = render(derived_contract())
    if args.write:
        CONTRACT_PATH.write_text(text, encoding="utf-8")
        return 0
    current = CONTRACT_PATH.read_text(encoding="utf-8") if CONTRACT_PATH.exists() else ""
    if current != text:
        print(
            f"{CONTRACT_PATH} is stale: run python3 -m vmaf_mcp.tool_contract --write",
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
