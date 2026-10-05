- **The Go MCP server is checked against the Python server's own tool
  list.** Its parity tests compared it with 15 hand-copied tool names, so the
  four sidecar tools and every property type went unchecked. The Python server
  now writes `mcp-server/vmaf-mcp/tool-contract.json` from its tool list
  (`python3 -m vmaf_mcp.tool_contract --write`, checked by its test suite), and
  `vmafx-mcp`'s tests require every tool in it with the same properties, types
  and required arguments, and no undeclared extra tool. Both servers already
  agreed; no tool changes.
