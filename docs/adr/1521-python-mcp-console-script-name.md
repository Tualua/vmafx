<!-- markdownlint-disable MD013 MD060 -->
# ADR-1521: `vmafx-mcp` names the Go server; the Python wheel's script of that name is a one-release alias

- **Status**: Accepted
- **Date**: 2026-10-04
- **Deciders**: maintainer, agent
- **Tags**: mcp, python, go, packaging, deprecation, fork-local

## Context

Two programs were installed under the command name `vmafx-mcp`: the Go binary
built from `cmd/vmafx-mcp/` (the MCP server since ADR-1229, installed at
`/usr/local/bin/vmafx-mcp` in every container image) and a console script of the
Python wheel `mcp-server/vmaf-mcp/` (`vmafx-mcp = "vmaf_mcp.server:main"`). Whichever
directory came first on `PATH` won, so `vmafx-mcp` served 24 tools or 19 depending
on where the wheel was installed (the documentation audit of 2026-10-03, defect 34).
The wheel already installs the same server as `vmaf-mcp`.

The Python package is deprecated by ADR-1229 and kept for one release as a
reference implementation. The Go server is the documented, containerised
entry point and the name `vmafx-mcp` appears in client configs, the container
healthcheck and the Zed project settings.

## Decision

`vmafx-mcp` keeps naming the Go server. The Python server is `vmaf-mcp` only. The
wheel's `vmafx-mcp` script stays for one release as a deprecated alias
(`vmaf_mcp.console_alias:deprecated_vmafx_mcp_alias`): it prints a deprecation notice on
stderr (stdout carries JSON-RPC), then hands over with `exec` to the first other
`vmafx-mcp` executable on `PATH` (the Go binary) and runs the Python server only
when there is none. The alias is removed with the Python package.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Rename the Go binary | No collision | Breaks every container image, healthcheck, client config and the Zed settings; the Go server is the supported one | The Go name has far more users |
| Drop the wheel script at once | No collision, no code | A client config that ran the wheel's `vmafx-mcp` fails with "command not found" with no hint | The brief asks for a one-release alias |
| Keep both names as they were, document the shadowing | No change | The collision stays; which server answers depends on `PATH` order | A defect, not a policy |
| Alias that always runs the Python server | Simple | Still shadows the Go binary for a release | The alias would keep the defect |

## Consequences

- **Positive**: `vmafx-mcp` is the Go server wherever the Go binary is installed; a
  wheel-only install still starts, with a notice that names `vmaf-mcp`.
- **Negative**: for one release the wheel installs a script that shadows the Go
  binary by name, though it hands over to it.
- **Follow-ups**: remove the alias with the Python package; the
  `tests/test_vmafx_mcp_alias.py` cases pin the hand-over, the notice and the
  `pyproject.toml` entry points. Documented in `docs/mcp/release-channel.md` and
  the package README.

## References

- Documentation audit 2026-10-03, defect 34 (code-defects list); maintainer popup
  2026-10-04 (paraphrased): track and fix every audit defect now.
- [ADR-1229](1229-mcp-go-runtime.md).
