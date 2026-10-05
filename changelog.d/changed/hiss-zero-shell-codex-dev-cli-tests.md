- **The codex hooks, dev scripts and CLI shell tests meet the HISS shell rules
  (ADR-1142).** The agent hooks under `.codex/hooks/`, the dev-container scripts
  under `dev/scripts/` and the CLI shell tests under `core/tools/test/` handle
  the exit status of every command they used to discard with `|| true`, run with
  the full strict mode (`set -eu`), and bound their daemon loops
  (`SUPERVISOR_ITERATION_CAP`, `PROBE_MAX_CYCLES`). `smoke-probe-loop.sh` keeps
  its embedded Python in variables so `probe_backend` and `_mcp_call` are under
  60 lines. A failed formatter or an unwritable work directory is now reported
  on stderr instead of vanishing. The HISS baseline loses 38 infractions.
