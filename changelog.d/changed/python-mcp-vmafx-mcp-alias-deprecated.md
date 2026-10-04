- **The Python wheel's `vmafx-mcp` script is a deprecated alias; use `vmaf-mcp`.** `vmafx-mcp`
  is the Go server (`cmd/vmafx-mcp`). For one release the wheel's script of that name prints a
  notice on stderr and hands over to the Go binary when one is on `PATH`, otherwise it runs the
  Python server ([ADR-1521](docs/adr/1521-python-mcp-console-script-name.md),
  [release channel](docs/mcp/release-channel.md)). FFmpeg patch impact: none.
