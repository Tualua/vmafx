---
paths:
  - .zed/*
  - scripts/ci/tests/test_zed_project_config.py
invariant: `.zed/` holds project-scoped settings only; test and files change together, on exact installed-version proof.
---
<!-- markdownlint-disable MD013 MD060 -->
# Zed project-configuration contract

`tests/test_zed_project_config.py` is the fail-closed contract for the
project-scoped Zed files. It must keep rejecting user-only `agent` and
`agent_servers` roots, the retired numbered workspace root, `.venv/bin/`
assumptions, deleted helper paths, the deprecated Python MCP entrypoint, and
loss of the three standards-governance tasks. Update the test together with
`.zed/` only when exact installed-version source proves a schema or executable
change; a Zed JSON parse alone does not prove that project settings apply the
keys.
