<!-- markdownlint-disable MD013 MD060 -->
# Embedded MCP server (in-process, inside libvmaf)

The embedded MCP server runs inside the host process that loaded
`libvmaf.so` (typically `vmaf`, `ffmpeg`'s `libvmaf` filter, or a Python
harness) and answers JSON-RPC 2.0 requests over stdio, a Unix domain socket
or loopback SSE. It serves two tools today: `list_features` and
`compute_vmaf`. It is off by default; build with `-Denable_mcp=true`.

!!! note "Status: v3 runtime"
    The v3 runtime landed on 2026-05-09 (T5-2d). `vmaf_mcp_init`,
    `vmaf_mcp_start_stdio`, `vmaf_mcp_start_uds`, `vmaf_mcp_start_sse`,
    `vmaf_mcp_stop` and `vmaf_mcp_close` are wired and answer `tools/list`,
    `tools/call`, `resources/list` and `initialize`. See the
    [status table](#status-table) for each component.

Unlike the standalone Python server, which wraps the `vmaf` CLI, the
embedded server lives in the measurement process. The design goal is tools
that introspect and steer a running measurement (per-feature scores
mid-stream, model hot-swap at a frame boundary, queue pressure). v3 ships
only the read-only and out-of-band tools; the steering tools wait for the
v4 SPSC bridge described under [What lands next](#what-lands-next-v4-roadmap).

See [ADR-0128](../adr/0128-embedded-mcp-in-libvmaf.md) for the governance
decision and [Research-0005](../research/0005-embedded-mcp-transport.md) for
the design rationale.

## Which MCP surface to use

| Workflow | Surface | Notes |
| --- | --- | --- |
| "Score a video, hand the result to my agent." | **External MCP server** (Go `cmd/vmafx-mcp` or Python `mcp-server/vmaf-mcp/`) | Recommended default. Spawns `vmaf` as a child process. See [`docs/mcp/index.md`](index.md). |
| "Score from inside an embedding process, or prepare an in-process control plane for a running measurement." | **Embedded MCP server** (this doc) | Runs in-process. Stdio, UDS, and loopback SSE are live; `list_features` and `compute_vmaf` are implemented. Mutating mid-stream tools wait on the v4 SPSC bridge. |

The two are additive — running both at the same time is fine.

## Build

The default build omits the embedded server. To opt in:

1. Configure with the umbrella flag and the transports you want.

    ```bash
    meson setup build core -Denable_mcp=true \
                           -Denable_mcp_sse=enabled \
                           -Denable_mcp_uds=true \
                           -Denable_mcp_stdio=true
    ```

2. Build.

    ```bash
    ninja -C build
    ```

3. Run the tests; the suite includes `test_mcp_smoke`.

    ```bash
    python3 scripts/ci/run_meson_test.py -- -C build
    ```

| Flag | Default | Purpose |
| --- | --- | --- |
| `-Denable_mcp` | `false` | Umbrella — compiles `core/src/mcp/` + installs `libvmaf_mcp.h` + builds `test_mcp_smoke`. |
| `-Denable_mcp_sse` | `auto` (compiled in unless explicitly disabled) | Compile in the Server-Sent-Events / loopback HTTP transport. Requires `enable_mcp=true`. Implemented in plain POSIX sockets — no third-party HTTP library is vendored. |
| `-Denable_mcp_uds` | `false` | Compile in the Unix domain socket transport. POSIX-only; non-POSIX hosts return `-ENODEV` at runtime. Requires `enable_mcp=true`. |
| `-Denable_mcp_stdio` | `false` | Compile in the stdio (newline-delimited JSON-RPC on a caller-supplied fd pair; LSP `Content-Length:` framing is a v4 item) transport. Requires `enable_mcp=true`. |

The umbrella flag is independent of the per-transport sub-flags.
A library built with `-Denable_mcp=true -Denable_mcp_sse=true`
exposes the public `vmaf_mcp_*` symbols, advertises SSE
availability via `vmaf_mcp_transport_available(VMAF_MCP_TRANSPORT_SSE)`,
and reports unavailability for UDS / stdio.

## API at a glance

```c
#include <libvmaf/libvmaf.h>
#include <libvmaf/libvmaf_mcp.h>

VmafContext *ctx = NULL;
/* ... vmaf_init(&ctx, ...) ... */

VmafMcpServer *server = NULL;
VmafMcpConfig cfg = { .queue_depth = 64, .max_drain_per_frame = 4 };
int rc = vmaf_mcp_init(&server, ctx, &cfg);
if (rc < 0) {
    /* -ENOSYS means libvmaf was built without -Denable_mcp=true. */
    fprintf(stderr, "MCP unavailable: %d\n", rc);
    /* Fall back to the external MCP server, or proceed without. */
}

/* Each transport spawns a dedicated MCP pthread on the server. */
VmafMcpSseConfig sse = { .port = 8723, .path = "/mcp/sse" };
(void)vmaf_mcp_start_sse(server, &sse);

/* ... vmaf_read_pictures / vmaf_score_pooled ... */

(void)vmaf_mcp_stop(server);
vmaf_mcp_close(&server);
int close_rc = vmaf_close(ctx);
if (close_rc != 0)
    close_rc = vmaf_close(ctx); /* retained teardown-only context */
if (close_rc == 0)
    ctx = NULL;
/* A persistent error retains ctx and every borrowed dependency. */
```

Close the MCP handle before context teardown. Only exact-zero
`vmaf_close()` success invalidates the context; any nonzero status retains the
teardown-only context and its borrowed dependencies for retry.

The full API is documented in
[`core/include/libvmaf/libvmaf_mcp.h`](../../core/include/libvmaf/libvmaf_mcp.h).

## Transport summary

### SSE (Server-Sent Events)

- **Spec status:** canonical MCP transport (introduced 2024-11).
- **Bind:** `127.0.0.1` only by construction — the SSE listener
  binds via `INADDR_LOOPBACK`, never to `INADDR_ANY`. Non-loopback
  exposure would require a separate ADR.
- **Wire:** HTTP/1.1 over loopback TCP, two endpoints on the same socket.
- **`GET /mcp/sse`** (default path; configurable via `VmafMcpSseConfig.path`)
  returns `Content-Type: text/event-stream` and emits SSE frames, starting
  with an `event: ready\ndata: {…}\n\n` handshake frame.
- **`POST /mcp/sse`** accepts a JSON-RPC request body and replies inline with
  the dispatcher's response. SSE-stream broadcast (the POST reply routed onto
  a subscribed GET stream) is reserved for v4.
- **Framing reference:** WHATWG SSE section 9.2
  (<https://html.spec.whatwg.org/multipage/server-sent-events.html>).
- **Auth:** none (loopback-only). v3 explicitly does not implement
  CORS, Bearer tokens, or per-session keys; the embedded surface
  is a same-host trust boundary.
- **Smoke test:** the C test
  [`core/test/test_mcp_smoke.c::test_sse_event_stream`](../../core/test/test_mcp_smoke.c)
  performs both round-trips below without a `curl` subprocess. By hand,
  spawn the server on an ephemeral port and run the two commands below.
- **Use case:** Claude Desktop, Cursor, and other agents speaking
  the canonical MCP remote transport.

To check the event-stream framing and a JSON-RPC round-trip by hand:

```bash
curl --max-time 3 -s -N http://127.0.0.1:<port>/mcp/sse
curl -X POST -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":1,"method":"tools/list"}' \
  http://127.0.0.1:<port>/mcp/sse
```

The first command shows the `text/event-stream` framing; the second
returns a JSON-RPC `tools/list` response.

#### Browser usage

A minimal browser-side subscriber (loopback only — the page must
be served from `http://127.0.0.1:*` because the SSE server emits
no CORS headers):

```javascript
const es = new EventSource("http://127.0.0.1:7411/mcp/sse");
es.addEventListener("ready", (ev) => {
  console.log("server hello:", JSON.parse(ev.data));
});
// JSON-RPC requests go via fetch(); responses come back inline
// (v3) — broadcast on the EventSource is reserved for v4.
fetch("http://127.0.0.1:7411/mcp/sse", {
  method: "POST",
  headers: {"Content-Type": "application/json"},
  body: JSON.stringify({jsonrpc: "2.0", id: 1, method: "tools/list"}),
}).then(r => r.json()).then(console.log);
```

### UDS (Unix domain socket — fork extension)

- **Spec status:** *not* in the MCP spec. A libvmaf-specific
  extension matching how systemd-managed pipelines already work.
- **Bind:** filesystem path you supply; server creates the socket
  file mode 0700.
- **Wire:** newline-delimited JSON-RPC (one JSON object per line,
  reply on the same connection).
- **Use case:** headless Linux boxes where filesystem permissions
  give cleaner auth than loopback TCP.

### stdio (newline-delimited JSON-RPC)

- **Spec status:** canonical MCP transport.
- **Bind:** caller hands over an fd pair (typically fd 3 / fd 4
  from a parent-spawned wrapper). The host's own stdin / stdout
  are *not* claimed.
- **Wire:** newline-delimited JSON-RPC, one object per line. LSP
  `Content-Length:` framing is still a v4 roadmap item.
- **Use case:** child-process spawned by an agent that already
  sets up an inheritable fd pair.

## Status table

| Component | Status | PR / ADR |
| --- | --- | --- |
| Public header `libvmaf_mcp.h` | Landed | T5-2 / [ADR-0209](../adr/0209-mcp-embedded-scaffold.md) |
| TU `core/src/mcp/mcp.c` | Landed (v1 runtime; init / start_stdio / stop / close wired) | T5-2b / ADR-0209 § Status update 2026-05-08 |
| Vendored cJSON v1.7.18 (MIT) under `core/src/mcp/3rdparty/cJSON/` | Landed | T5-2b |
| JSON-RPC dispatcher (`tools/list`, `tools/call`, `resources/list`, `initialize`) | Landed | T5-2b |
| Build flags + per-transport sub-flags | Landed (default off) | T5-2 |
| Smoke + protocol test (`core/test/test_mcp_smoke.c`, real round-trip) | Landed | T5-2b |
| stdio transport body | Landed (line-delimited JSON-RPC; LSP `Content-Length:` framing remains a v4 roadmap item) | T5-2b |
| UDS transport body | Landed (line-delimited JSON-RPC; mode-0700 socket file) | T5-2c / [ADR-0402](../adr/0402-mcp-runtime-v2.md) |
| SSE transport body | Landed (loopback HTTP/1.1 + `text/event-stream`; no third-party HTTP library — see ADR-0402 § "v3 SSE" for the license-driven mongoose pivot) | T5-2d / [ADR-0402](../adr/0402-mcp-runtime-v2.md) § "Status update 2026-05-09 (v3 SSE)" |
| Tool: `list_features` (read-only) | Landed | T5-2b |
| Tool: `compute_vmaf` (real libvmaf scoring binding: pooled mean VMAF over a YUV420p 8/10/12/16-bit pair via `vmaf_model_load` + `vmaf_read_pictures` + `vmaf_score_pooled`) | Landed | T5-2c + high-bit-depth follow-up |
| Tool: `vmaf.request_model_swap` (mutating, separate ADR) | Future | post-v3 |
| `enable_mcp` default flip from `false` → `auto` | Future | post all transports stable |

## What lands next (v4 roadmap)

v3 (T5-2d) shipped the SSE transport. The remaining work:

- **SSE-stream broadcast.** v3 emits POST replies inline on the
  POST socket; v4 will fan replies out on subscribed `GET
  /mcp/sse` streams via the `event:` / `id:` / `data:` pattern,
  using the `sse_emit_event` + `sse_extract_id` helpers v3 ships
  marked `__attribute__((unused))`.
- **LSP-framed stdio** (`Content-Length:` headers) — v1–v3 ship
  line-delimited JSON-RPC only.
- **Broader YUV layouts in `compute_vmaf`** — the tool accepts
  YUV420p at 8/10/12/16 bpc. YUV422P / YUV444P remain future schema
  work because the tool does not expose a `pixel_format` argument yet.
- **SPSC ring drain at frame boundaries** — v1–v3 dispatcher
  runs to completion on the transport thread; the measurement-
  thread hot path is not yet bridged. Tools that mutate
  measurement state (`request_model_swap`, etc.) require this
  bridge first.
- Tool-surface expansion (`vmaf.status`, `vmaf.score`,
  `vmaf.request_model_swap`) follows in its own PR per
  [ADR-0128](../adr/0128-embedded-mcp-in-libvmaf.md) § "Tool surface (v1)".

## Sample request / response

Request (newline-delimited JSON-RPC over stdio):

```json
{"jsonrpc":"2.0","id":1,"method":"tools/list"}
```

Response:

```json
{"jsonrpc":"2.0","id":1,"result":{"tools":[{"name":"list_features","description":"…","inputSchema":{…}},{"name":"compute_vmaf","description":"…","inputSchema":{…}}]}}
```

Tool call:

```json
{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"list_features","arguments":{}}}
```

Tool response (MCP `content` envelope wrapping the tool's JSON). `count` is
the number of listed extractors that are compiled into this libvmaf, so it
varies by build:

```json
{"jsonrpc":"2.0","id":2,"result":{"content":[{"type":"text","text":"{\"features\":[\"float_adm\",\"float_vif\",…],\"count\":<n>}"}],"isError":false}}
```

## Maintainer notes

These invariants matter to people changing `core/src/mcp/`, not to users of
the server. "SPSC" is a single-producer, single-consumer ring buffer;
"Power-of-10" is the NASA/JPL coding rule set the project follows
([principles](../principles.md)).

### Listener-shutdown invariant

On Linux, plain `close()` of a listening socket (whether AF_INET
or AF_UNIX) from one thread does NOT unblock `accept()` on another
thread (verified empirically; see also `accept(2)`). Both the SSE
and UDS stop paths therefore call `shutdown(listen_fd, SHUT_RDWR)`
before `close()` so the worker thread observes accept returning
`-1` (with `errno == EINVAL`) and exits cleanly before `pthread_join()`.

### Threading and Power-of-10 invariants

- One **MCP pthread** per active transport. The thread owns the
  socket, owns JSON parsing, owns all per-request allocation. It
  does **not** touch measurement state directly.
- v3 tools execute on the transport thread and do not mutate the
  host measurement state. `compute_vmaf` creates a short-lived
  private `VmafContext` for the requested YUV pair instead of
  borrowing the host's active scorer.
- The SPSC ring-buffer bridge described by ADR-0128 is still v4
  work. `VmafMcpConfig.queue_depth` and `max_drain_per_frame`
  remain validated API fields so hosts can keep one configuration
  shape across v3 and v4, but v3 does not yet drain envelopes on
  frame boundaries.
- Tools that need measurement-thread mutation, such as
  `vmaf.request_model_swap`, must wait for that SPSC bridge. Until
  then the embedded surface is read-only plus out-of-band scoring.

These invariants are documented in the public header
([`libvmaf_mcp.h`](../../core/include/libvmaf/libvmaf_mcp.h)).

## See also

- [`docs/mcp/index.md`](index.md) — overview of the MCP surfaces.
- [`docs/mcp/tools.md`](tools.md) — tool-surface reference for
  the existing standalone Python MCP server.
- [ADR-0128](../adr/0128-embedded-mcp-in-libvmaf.md) — governance.
- [Research-0005](../research/0005-embedded-mcp-transport.md) — design.
- [`core/include/libvmaf/libvmaf_mcp.h`](../../core/include/libvmaf/libvmaf_mcp.h)
  — API reference.
