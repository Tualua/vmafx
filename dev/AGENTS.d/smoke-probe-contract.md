---
paths:
  - dev/scripts/smoke-probe-loop.sh
  - dev/scripts/test-smoke-probe-loop.sh
invariant: Exclusive backend selector and backend_used check; Go vmafx-mcp over stdio; stable JSON keys; paired tests.
---
<!-- markdownlint-disable MD013 -->
# Smoke-probe contract (Research-2083)

`dev/scripts/smoke-probe-loop.sh` is evidence-producing code, not liveness
ping. Preserve all of these constraints when CLI, Go MCP server, or probe
schema is rebased:

1. Every CLI run uses exclusive `--backend cpu|cuda|sycl|hip` selector,
   writes `--json` to real temporary file, and validates both
   `pooled_metrics.vmaf.mean` and matching `backend_used` receipt. Raw YUV
   geometry is `--pixel_format 420 --bitdepth 8`; `yuv420p`, retired
   `--cuda` / `--sycl` / `--hip` switches, and `--no_prediction` are invalid
   probe contracts.
2. MCP probes execute production `vmafx-mcp` Go binary over stdio, perform
   `initialize` followed by `notifications/initialized`, and only then call
   `list_extractors` or `vmaf_score`. client keeps stdin open until    response with request ID 2 arrives; EOF disconnects Go SDK session and
   can otherwise race response. Do not restore retired
   `vmaf-mcp-server`, `list_features`, or `compute_vmaf` operations.
3. JSON keys `mcp_results.list_features` and
   `mcp_results.compute_vmaf` remain stable for existing probe consumers even
   though underlying tool names changed. Error text is JSON-encoded, and
   helper results use non-whitespace delimiter so empty score cannot
   shift duration and error fields.
4. Keep `dev/scripts/test-smoke-probe-loop.sh` and    `test-dev-mcp-smoke-probe` pre-commit hook coupled to changes in either
   script. MCP stub responds before EOF so test also rejects stdio
   close-before-response race. It covers healthy path, error strings
   containing JSON metacharacters/control bytes, and backend receipt
   mismatch.
