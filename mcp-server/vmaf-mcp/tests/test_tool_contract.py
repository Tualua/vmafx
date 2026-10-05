# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""``tool-contract.json`` is the tool list this server advertises.

The Go server's parity tests compare ``vmafx-mcp`` with this file, so a
stale file would let the two servers drift apart while both suites pass.
"""

from __future__ import annotations

import asyncio
import json

import pytest

from vmaf_mcp import server, tool_contract


def _committed() -> dict:
    return json.loads(tool_contract.CONTRACT_PATH.read_text(encoding="utf-8"))


def test_committed_contract_is_the_servers_tool_list() -> None:
    assert _committed() == tool_contract.derived_contract(), (
        "tool-contract.json is stale: "
        "PYTHONPATH=mcp-server/vmaf-mcp/src python3 -m vmaf_mcp.tool_contract --write"
    )


def test_check_mode_accepts_the_committed_file() -> None:
    assert tool_contract.main(["--check"]) == 0


def test_contract_names_every_advertised_tool() -> None:
    tools = asyncio.run(server._list_tools())
    assert sorted(_committed()["tools"]) == sorted(tool.name for tool in tools)
    # The four sidecar tools the hand-copied Go list once left out.
    for name in ("vmaf_per_shot", "vmaf_roi", "vmaf_bench", "vmaf_vpl"):
        assert name in _committed()["tools"]


def test_contract_records_properties_and_required() -> None:
    entry = _committed()["tools"]["vmaf_score"]
    assert entry["required"] == ["bitdepth", "dis", "height", "pixfmt", "ref", "width"]
    assert entry["properties"]["width"] == "integer"
    assert entry["properties"]["ref"] == "string"


def test_check_mode_refuses_a_changed_tool_list(monkeypatch: pytest.MonkeyPatch) -> None:
    tools = asyncio.run(server._list_tools())

    async def without_last_tool() -> list:
        return tools[:-1]

    monkeypatch.setattr(server, "_list_tools", without_last_tool)
    assert tool_contract.derived_contract() != _committed()
    assert tool_contract.main(["--check"]) == 1


def test_check_mode_refuses_a_changed_required_set(monkeypatch: pytest.MonkeyPatch) -> None:
    tools = asyncio.run(server._list_tools())
    wire = tools[0].model_dump(by_alias=True)
    wire["inputSchema"] = {**wire["inputSchema"], "required": []}
    changed = type(tools[0]).model_validate(wire)

    async def with_optional_arguments() -> list:
        return [changed, *tools[1:]]

    monkeypatch.setattr(server, "_list_tools", with_optional_arguments)
    assert tool_contract.main(["--check"]) == 1


def test_duplicate_tool_names_are_refused() -> None:
    tool = asyncio.run(server._list_tools())[0]
    with pytest.raises(AssertionError, match="duplicate tool names"):
        tool_contract.contract_from_tools([tool, tool])
