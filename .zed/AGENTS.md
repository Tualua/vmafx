<!-- markdownlint-disable MD013 -->
# `.zed/` project-configuration invariants

Parent: [../AGENTS.md](../AGENTS.md).

Zed reads `.zed/settings.json` through `ProjectSettingsContent`, not through
full user-settings schema. Keep `agent`, `agent_servers`, extension
installation, UI preferences, telemetry, provider/model selection, and agent
permission policy out of this directory. External ACP agents own their own
authentication, model selection, and write/approval mode.

Checked-in MCP context server launches current Go executable as
`docker exec -i vmaf-dev-mcp vmafx-mcp`. Do not restore deprecated Python
`vmaf-mcp` entrypoint or old `source: "custom"` field. Tasks and debugger
scenarios must use current tracked entrypoints, writable container
`/probes` area, and active `.workingdir/` root; never restore deleted
helpers, `.venv/bin/*` assumptions, or retired numbered workspace root.

Preserve three `Standards:` governance tasks. After any change under this
directory, run:
Zed reads `.zed/settings.json` through `ProjectSettingsContent`, not through full user-settings schema. Keep `agent`, `agent_servers`, extension installation, UI preferences, telemetry, provider/model selection, agent permission policy out of this directory. External ACP agents own authentication, model selection, write/approval mode.

Checked-in MCP context server launches current Go executable as `docker exec -i vmaf-dev-mcp vmafx-mcp`. Do not restore deprecated Python `vmaf-mcp` entrypoint or old `source: "custom"` field. Tasks, debugger scenarios must use current tracked entrypoints, writable container `/probes` area, active `.workingdir/` root; never restore deleted helpers, `.venv/bin/*` assumptions, retired numbered workspace root.

Preserve three `Standards:` governance tasks. After changes under this directory, run:

```bash
python3 -m pytest -q scripts/ci/tests/test_zed_project_config.py
```
