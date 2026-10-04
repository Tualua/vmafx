<!-- markdownlint-disable MD060 -->
# vmaf-mcp

> **DEPRECATED (ADR-1229).** The MCP server is now the Go binary `vmafx-mcp`
> (`cmd/vmafx-mcp/`), installed at `/usr/local/bin/vmafx-mcp` in every container
> image. Attach with `docker exec -i vmaf-dev-mcp vmafx-mcp`. This Python
> package serves 19 tools (the Go server adds 5 control-plane tools) and is
> retained for one release as a reference implementation; it is no longer
> installed by `dev/Containerfile` and a follow-up removes it. Do not add
> tools here — add them to `cmd/vmafx-mcp/`.
>
> **Console script names.** This wheel installs `vmaf-mcp`. It also installs a
> deprecated `vmafx-mcp` alias for one release: that name belongs to the Go
> server, so the alias prints a deprecation notice on stderr and hands over to
> the Go binary when one is on `PATH` (otherwise it runs this server). Switch
> scripts and client configs to `vmaf-mcp` (this server) or the Go `vmafx-mcp`.

MCP (Model Context Protocol) server that exposes the VMAFx fork's
scoring CLI to LLM tooling via JSON-RPC over stdio.

## Tools

The full tool list, with schemas and examples, is
[docs/mcp/tools.md](../../docs/mcp/tools.md); the Python-versus-Go comparison is
in [docs/mcp/index.md](../../docs/mcp/index.md).

## Install

```bash
cd mcp-server/vmaf-mcp
pip install -e .
```

Requires a built `libvmaf` binary at `build/tools/vmaf` (override via
`VMAF_BIN=/abs/path/to/vmaf`).

## Run

```bash
# Stdio transport (default for Claude Desktop, Cursor, etc.)
vmaf-mcp
```

## Path allowlisting

For safety, the server only reads files under `testdata/`,
`python/test/resource/`, and `model/`. Extend via colon-separated
`VMAF_MCP_ALLOW`:

```bash
VMAF_MCP_ALLOW=/data/my-corpus:/mnt/yuv vmaf-mcp
```

## Claude Desktop config

```json
{
  "mcpServers": {
    "vmaf": {
      "command": "vmaf-mcp",
      "env": {
        "VMAF_BIN": "/home/you/dev/vmaf/build/tools/vmaf",
        "VMAF_MCP_ALLOW": "/data/yuv-corpus"
      }
    }
  }
}
```
