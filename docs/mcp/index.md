# MCP server — `vmaf-mcp`

The Lusoris VMAF fork ships **two** MCP surfaces:

1. **External Python MCP server** (`vmaf-mcp`, this document) —
   wraps the `vmaf` CLI, recommended for "score a video and hand
   the result to my agent" workflows. Stable, in production use.
2. **Embedded MCP server inside libvmaf** — runs in-process on
   the host that loaded `libvmaf.so`; serves stdio, UDS, and
   loopback SSE transports with `list_features` and `compute_vmaf`.
   It is the right surface when an embedding host needs an
   in-process control plane rather than a child `vmaf` process.
   Model hot-swap and frame-boundary SPSC draining remain future
   work. See [`docs/mcp/embedded.md`](embedded.md) for build flags,
   transport limits, and the C API reference.

The two surfaces are additive; running both at once is fine.

`vmaf-mcp` is a [Model Context Protocol](https://modelcontextprotocol.io)
server that exposes the Lusoris VMAF fork's scoring CLI to LLM tooling
(Claude Desktop, Cursor, custom MCP clients) over JSON-RPC on stdio.
It lives in [mcp-server/vmaf-mcp/](../../mcp-server/vmaf-mcp/).

Use it when you want an LLM to:

- score a `(reference, distorted)` YUV pair and reason about the result,
- enumerate which VMAF models shipped with the build,
- probe which runtime backends (CPU / CUDA / SYCL / Vulkan / HIP / Metal) the local
  binary can dispatch to,
- run the Netflix benchmark harness and summarise the output,
- evaluate a tiny-AI ONNX regressor against a parquet feature cache
  on a deterministic split and report PLCC / SROCC / RMSE,
- rank several candidate tiny-AI models on the same split.

The server exec's the repo's own built `vmaf` binary under argv — it
never passes a shell string — and refuses any file path that is not
under an allowlisted root. See [security](#security-model) below.

## Tool catalogue

| Tool | Purpose | Detail |
|---|---|---|
| `vmaf_score` | Score one `(ref, dis)` YUV pair; return the full JSON report | [tools.md#vmaf_score](tools.md#vmaf_score) |
| `list_models` | Enumerate `.json` / `.pkl` / `.onnx` under `model/` | [tools.md#list_models](tools.md#list_models) |
| `list_backends` | Report which backends the local `vmaf` binary was built with | [tools.md#list_backends](tools.md#list_backends) |
| `run_benchmark` | Run `testdata/bench_all.sh` on a pair | [tools.md](tools.md) |
| `eval_model_on_split` | Evaluate a tiny-AI ONNX model on a parquet feature cache | [tools.md#eval_model_on_split](tools.md#eval_model_on_split) |
| `compare_models` | Rank several ONNX models on the same split by descending PLCC | [tools.md#compare_models](tools.md#compare_models) |
| `describe_worst_frames` | Score a pair, extract the N worst-VMAF frames as PNGs, and describe visible artefacts via a local VLM  | [tools.md#describe_worst_frames](tools.md#describe_worst_frames) |

All tools return a single `TextContent` message whose body is a JSON
document. On error the body is `{"error": "<message>"}` with the same
shape so the client can always `json.loads()` the response.

## Install

From a checkout of the repo:

```bash
# 1. build vmaf (Meson + Ninja; see CLAUDE.md §2)
meson setup build -Denable_cuda=false -Denable_sycl=false
ninja -C build

# 2. install the MCP server package
cd mcp-server/vmaf-mcp
pip install -e .

# optional: pull in ML deps for eval_model_on_split / compare_models
pip install -e '.[eval]'
```

The server binary lands as `vmaf-mcp` on your PATH. It expects to find
the vmaf CLI at `build/tools/vmaf` relative to the repo root. Override
with `VMAF_BIN=/abs/path/to/vmaf`.

## Run

```bash
# Default stdio transport — what Claude Desktop / Cursor use
vmaf-mcp
```

No network ports are opened. The server reads JSON-RPC requests from
stdin and writes responses to stdout; diagnostic logs go to stderr.

### Claude Desktop configuration

Drop this into
`~/Library/Application Support/Claude/claude_desktop_config.json`
(macOS) or `%APPDATA%/Claude/claude_desktop_config.json` (Windows):

```json
{
  "mcpServers": {
    "vmaf-local": {
      "command": "vmaf-mcp",
      "env": {
        "VMAF_BIN": "/home/you/dev/vmaf/build/tools/vmaf",
        "VMAF_MCP_ALLOW": "/home/you/yuv-corpus:/home/you/renders"
      }
    }
  }
}
```

A complete example covering the Docker image variant lives in
[mcp-server/vmaf-mcp/claude-desktop-config-example.json](../../mcp-server/vmaf-mcp/claude-desktop-config-example.json).

## Environment variables

| Variable           | Purpose                                                                            | Default                                 |
|--------------------|------------------------------------------------------------------------------------|-----------------------------------------|
| `VMAF_BIN`         | Absolute path to the `vmaf` CLI binary                                             | `<repo>/build/tools/vmaf`               |
| `VMAF_MCP_ALLOW`   | Colon-separated extra roots under which file paths are accepted                    | (empty — only built-in roots)           |
| `VMAF_MCP_ASYNC`   | AnyIO backend (`asyncio` / `trio`)                                                 | `asyncio`                               |

## Security model

The server is meant to run on the user's own machine, driven by a local
LLM client. Even so, any JSON-RPC input could be crafted by the LLM to
try to coerce the server into reading arbitrary host paths — so the
server enforces a path allowlist:

- Built-in roots (always allowed):
  - `testdata/`
  - `python/test/resource/`
  - `model/`
- Extra roots can be added via `VMAF_MCP_ALLOW=<abs-path>[:<abs-path>...]`.

Any tool argument that names a file (`ref`, `dis`, `model`, `features`,
each member of `models`) is resolved with `Path.resolve()` and rejected
unless it lands under one of the allowed roots **and** refers to an
existing regular file. `..` segments and symlinks that escape the
allowlist are rejected by resolution.

The underlying CLI is exec'd with an `argv` list — never a shell
string — so there is no pathway for shell-metacharacter injection.

See also [ai/security.md](../ai/security.md) for the tiny-AI-specific
hardening (ONNX operator allowlist, model size cap).

## When not to use the MCP server

- **Bulk scoring in a pipeline** — use the
  [`vmaf` CLI directly](../usage/cli.md). MCP is request/response; the
  CLI streams pictures and does not pay JSON-RPC overhead per frame.
- **Integration into your own code** — use the
  [C API](../api/index.md) or the
  [Python bindings](../usage/python.md) for an in-process surface.
- **CI checks** — the [Docker image](../usage/docker.md) is a better
  fit than stdio-attached MCP.

MCP shines when the caller is an LLM that benefits from having a
tool-calling interface with declared schemas and a JSON-shaped response.

## Related

- [Tool reference](tools.md) — request/response schemas and error codes
  for every tool.
- [Backend discovery and default allowlist](backends.md) — how
  `list_backends` probes compiled-in GPU runtimes and which paths the
  server accepts without `VMAF_MCP_ALLOW` (ADR-0511).
- [ADR-0100](../adr/0100-project-wide-doc-substance-rule.md) — the
  per-surface doc bar this page satisfies (MCP tool: what / schema /
  allowed paths / example / error codes).
- [mcp-server/vmaf-mcp/README.md](../../mcp-server/vmaf-mcp/README.md) —
  short-form README kept alongside the code.
