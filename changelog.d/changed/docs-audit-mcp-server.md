- Corrected the MCP, server and architecture pages against the code: 24 tools
  in the Go MCP server and 19 in the Python one, environment-only configuration
  of the controller, server and node (ADR-1119; the documented `--port` style
  flags do not exist), the real Prometheus metric names, the CRD group
  `vmafx.dev/v1` with four CRDs, and runtime versions from `build-config.env`.
  The C4 diagrams now render (Mermaid instead of PlantUML), the FAQ no longer
  calls HIP "planned", and benchmark tables without date, host and command are
  marked as not citable.
